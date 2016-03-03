/*
 * Authors: Praarthana Ramakrishnan, Keerthan Jaic, Tyler Allen,
 *          Sriram Madhivanan
 *
 * Version: 03/02/2016
 *
 * This is a driver for the Kyouko3 virtual graphics card.
 */

#include <linux/wait.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include "kyouko3.h"

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Sriram Madhivanan, Tyler Allen, Keerthan Jaic,"
	      " Praarthana Ramakrishnan");

// This queue head is used for snoozing while DMA buffers are full.
DECLARE_WAIT_QUEUE_HEAD(dma_snooze);
// This queue head is used for snoozing while waiting for DMA buffers to
// completely empty before exiting.
DECLARE_WAIT_QUEUE_HEAD(unbind_snooze);

struct phys_region {
	phys_addr_t p_base;
	unsigned long len;
	unsigned int *k_base;
};

struct _fifo {
	dma_addr_t p_base;
	struct fifo_entry *k_base;
	u32 head;
	u32 tail_cache;
};

struct k3_dma_buf {
	unsigned int *k_base;
	unsigned long u_base;
	dma_addr_t handle;
	int size;
} dma[DMA_BUFNUM];

struct kyouko3_vars {
	struct phys_region control;
	struct phys_region fb;
	bool graphics_on;
	bool dma_on;
	struct _fifo fifo;
	struct pci_dev *pdev;
	u32 fill;
	u32 drain;
	bool full;
	spinlock_t lock;
} k3;

/* Efficient way to increment index of circ buffer whose size is a power of two.
 * Equivalent to (idx + 1) % size
 * Inspired by Documentation/circ-buf.txt */
static inline void dmaq_inc_idx(u32 *idx)
{
	*idx = (*idx + 1) & (DMA_BUFNUM - 1);
}

static inline void K_WRITE_REG(u32 reg, u32 value)
{
	*(k3.control.k_base + (reg >> 2)) = value;
}

static inline u32 K_READ_REG(u32 reg)
{
	rmb();
	return *(k3.control.k_base + (reg >> 2));
}

void fifo_init(void)
{
	k3.fifo.k_base =
	    pci_alloc_consistent(k3.pdev, 8 * FIFO_ENTRIES, &k3.fifo.p_base);
	K_WRITE_REG(FIFO_START, k3.fifo.p_base);
	K_WRITE_REG(FIFO_END, k3.fifo.p_base + 8 * FIFO_ENTRIES);
	k3.fifo.head = 0;
	k3.fifo.tail_cache = 0;
}

void fifo_flush(void)
{
	K_WRITE_REG(FIFO_HEAD, k3.fifo.head);
	while (k3.fifo.tail_cache != k3.fifo.head) {
		k3.fifo.tail_cache = K_READ_REG(FIFO_TAIL);
		schedule();
	}
}

void fifo_write(u32 cmd, u32 val)
{
	k3.fifo.k_base[k3.fifo.head].command = cmd;
	k3.fifo.k_base[k3.fifo.head].value = val;
	k3.fifo.head++;
	if (k3.fifo.head >= FIFO_ENTRIES) {
		k3.fifo.head = 0;
	}
}

/*
 * DMA interrupt handler.
 */
irqreturn_t dma_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	int empty;
	u32 iflags = K_READ_REG(INFO_STATUS);

	K_WRITE_REG(INFO_STATUS, 0xf);

	// spurious interrupt
	if ((iflags & 0x02) == 0) {
		return IRQ_NONE;
	}

	// Protect data which might be accessed from process context
	spin_lock(&k3.lock);

	// we are ready to drain the next buffer
	dmaq_inc_idx(&k3.drain);

	empty = k3.fill == k3.drain;

	if (!empty) {
		// Queue is not empty. Dispatch the next buffer
		fifo_write(BUFA_ADDR, dma[k3.drain].handle);
		fifo_write(BUFA_CONF, dma[k3.drain].size);
		K_WRITE_REG(FIFO_HEAD, k3.fifo.head);
	} else {
		// Queue is empty. Wake up unbind_dma
		if (!k3.dma_on) {
			wake_up_interruptible(&unbind_snooze);
		}
	}

	// Wake up sleeping user if buffer was full.
	if (k3.full) {
		k3.full = 0;
		wake_up_interruptible(&dma_snooze);
	}

	spin_unlock(&k3.lock);

	return IRQ_HANDLED;
}

/*
 * Initiate a DMA request if possible.
 */
int initiate_transfer(unsigned long size)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&k3.lock, flags);
	dma[k3.fill].size = size;

	if (k3.fill == k3.drain) {
		// Queue is empty, dispatch buffer
		fifo_write(BUFA_ADDR, dma[k3.drain].handle);
		fifo_write(BUFA_CONF, dma[k3.drain].size);
		K_WRITE_REG(FIFO_HEAD, k3.fifo.head);

		dmaq_inc_idx(&k3.fill);
		ret = k3.fill;

		spin_unlock_irqrestore(&k3.lock, flags);
		return ret;
	}

	dmaq_inc_idx(&k3.fill);
	ret = k3.fill;
	// Next buf is still being drained. Queue is full.
	k3.full = k3.fill == k3.drain;
	spin_unlock_irqrestore(&k3.lock, flags);
	if (k3.full) {
		// Wait till a buffer is drained
		wait_event_interruptible(dma_snooze, !k3.full);
	}
	return ret;
}

/* Set up pci interrupts and dma buffers */
int dma_init(struct file *fp)
{
	// If dma was already on, skip the initialization.
	// re-running the buffer allocation loop will cause us to lose the old
	// dma handles and they would never be freed.
	if (k3.dma_on) {
		return 0;
	}

	int ret = 0;
	ret = pci_enable_msi(k3.pdev);
	if (ret) {
		pr_warn("pci_enable_msi failed\n");
		return ret;
	}

	ret = request_irq(k3.pdev->irq, (irq_handler_t)dma_isr, IRQF_SHARED,
			  "kyouku3 dma isr", &k3);
	if (ret) {
		pr_warn("pci_enable_msi failed\n");
		return ret;
	}

	for (int i = 0; i < DMA_BUFNUM; i++) {
		k3.fill = i;
		dma[i].k_base =
		    pci_alloc_consistent(k3.pdev, DMA_BUFSIZE, &dma[i].handle);
		dma[i].u_base =
		    vm_mmap(fp, 0, DMA_BUFSIZE, PROT_READ | PROT_WRITE,
			    MAP_SHARED, VM_PGOFF_DMA);
	}
	// We don't need locking here because we have not enabled interrupts on
	// the device yet.
	k3.fill = 0;
	k3.drain = 0;
	spin_lock_init(&k3.lock);
	k3.dma_on = true;
	K_WRITE_REG(CONF_INTERRUPT, 0x02);

	return ret;
}

long kyouko3_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct fifo_entry entry;
	void __user *argp = (void __user *)arg;
	int i;
	long ret = 0;
	int count;

	switch (cmd) {
	case VMODE:
		if (arg == GRAPHICS_ON) {

			K_WRITE_REG(FRAME_COLUMNS, 1024);
			K_WRITE_REG(FRAME_ROWS, 768);
			K_WRITE_REG(FRAME_ROWPITCH, 1024 * 4);
			K_WRITE_REG(FRAME_PIXELFORMAT, 0xf888);
			K_WRITE_REG(FRAME_STARTADDRESS, 0);

			K_WRITE_REG(CONF_ACCELERATION, 0x40000000);

			K_WRITE_REG(ENC_WIDTH, 1024);
			K_WRITE_REG(ENC_HEIGHT, 768);
			K_WRITE_REG(ENC_OFFSETX, 0);
			K_WRITE_REG(ENC_OFFSETY, 0);
			K_WRITE_REG(ENC_FRAME, 0);

			K_WRITE_REG(CONF_MODESET, 0);

			msleep(10);

			fifo_write(CLEAR_COLOR, 0);
			fifo_write(CLEAR_COLOR + 0x0004, 0);
			fifo_write(CLEAR_COLOR + 0x0008, 0);
			fifo_write(CLEAR_COLOR + 0x000c, 0);

			fifo_write(RASTER_CLEAR, 3);
			fifo_write(RASTER_FLUSH, 0);
			fifo_flush();

			k3.graphics_on = 1;
		}
		// disable graphics mode.
		else if (arg == GRAPHICS_OFF) {
			if (k3.dma_on) {
				kyouko3_ioctl(fp, UNBIND_DMA, 0);
			} else {
				fifo_flush();
			}
			K_WRITE_REG(CONF_ACCELERATION, 0x80000000);
			K_WRITE_REG(CONF_MODESET, 0);
			k3.graphics_on = 0;
		}
		break;
	case FIFO_QUEUE:
		if (copy_from_user(&entry, argp, sizeof(struct fifo_entry)))
			return -EFAULT;
		fifo_write(entry.command, entry.value);
		break;
	case FIFO_FLUSH:
		fifo_flush();
		break;
	case BIND_DMA:
		ret = dma_init(fp);
		if (ret) {
			pr_warn("BIND_DMA failed\n");
			return ret;
		}
		if (copy_to_user(argp, &dma[0].u_base, sizeof(unsigned long))) {
			return -EFAULT;
		}
		return 0;
	case UNBIND_DMA:
		// set flag to wake up user when buffer is empty
		k3.dma_on = 0;
		// snooze user and empty queue
		if (k3.fill != k3.drain) {
			wait_event_interruptible(unbind_snooze,
						 k3.fill == k3.drain);
		}
		// Unmap buffers.
		for (i = 0; i < DMA_BUFNUM; i++) {
			vm_munmap(dma[i].u_base, DMA_BUFSIZE);
			pci_free_consistent(k3.pdev, DMA_BUFSIZE, dma[i].k_base,
					    dma[i].handle);
		}
		K_WRITE_REG(CONF_INTERRUPT, 0);
		free_irq(k3.pdev->irq, &k3);
		pci_disable_msi(k3.pdev);
		break;
	case START_DMA:
		if (copy_from_user(&count, argp, sizeof(unsigned int)))
			return -EFAULT;
		// initiate transfer is the bulk of this function
		ret = initiate_transfer(count);
		if (copy_to_user(argp, &dma[ret].u_base, sizeof(unsigned long)))
			return -EFAULT;
		break;
	}
	return ret;
}

int kyouko3_open(struct inode *inode, struct file *fp)
{
	// ioremap_wc is faster than ioremap on some hardware
	k3.control.k_base = ioremap_wc(k3.control.p_base, k3.control.len);
	k3.fb.k_base = ioremap_wc(k3.fb.p_base, k3.fb.len);
	fifo_init();
	return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp)
{
	kyouko3_ioctl(fp, VMODE, GRAPHICS_OFF);
	fifo_flush();
	iounmap(k3.control.k_base);
	iounmap(k3.fb.k_base);
	pci_free_consistent(k3.pdev, 8192, k3.fifo.k_base, k3.fifo.p_base);
	return 0;
}

int kyouko3_mmap(struct file *fp, struct vm_area_struct *vma)
{
	int ret = 0;
	unsigned long off;

	// vm_iomap_memory provides a simpler API than io_remap_pfn_range and
	// reduces possibilities for bugs
	// Offset is just used to choose regions, it isn't a real offset.
	off = vma->vm_pgoff << PAGE_SHIFT;
	vma->vm_pgoff = 0;
	switch (off) {
	case VM_PGOFF_CONTROL:
		ret = vm_iomap_memory(vma, k3.control.p_base, k3.control.len);
		break;
	case VM_PGOFF_FB:
		ret = vm_iomap_memory(vma, k3.fb.p_base, k3.fb.len);
		break;
	case VM_PGOFF_DMA:
		ret = vm_iomap_memory(vma, dma[k3.fill].handle, DMA_BUFSIZE);
		break;
	}
	return ret;
}

struct file_operations kyouko3_fops = {.open = kyouko3_open,
				       .release = kyouko3_release,
				       .mmap = kyouko3_mmap,
				       .unlocked_ioctl = kyouko3_ioctl,
				       .owner = THIS_MODULE};

struct cdev kyouko3_dev;

int kyouko3_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	k3.pdev = pdev;

	k3.control.p_base = pci_resource_start(pdev, 1);
	k3.control.len = pci_resource_len(pdev, 1);

	k3.fb.p_base = pci_resource_start(pdev, 2);
	k3.fb.len = pci_resource_len(pdev, 2);

	pci_set_master(pdev);
	return pci_enable_device(pdev);
}

void kyouko3_remove(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

static const struct pci_device_id kyouko3_dev_ids[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_CCORSI, PCI_DEVICE_ID_CCORSI_KYOUKO3)}, {0}};

struct pci_driver kyouko3_pci_drv = {.name = "kyouko3_pci_drv",
				     .id_table = kyouko3_dev_ids,
				     .probe = kyouko3_probe,
				     .remove = kyouko3_remove};

int kyouko3_init(void)
{
	cdev_init(&kyouko3_dev, &kyouko3_fops);
	cdev_add(&kyouko3_dev, MKDEV(500, 127), 1);
	k3.dma_on = 0;
	return pci_register_driver(&kyouko3_pci_drv);
}

void kyouko3_exit(void)
{
	cdev_del(&kyouko3_dev);
	pci_unregister_driver(&kyouko3_pci_drv);
}

module_init(kyouko3_init);
module_exit(kyouko3_exit);
