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

MODULE_LICENSE ("Proprietary");
MODULE_AUTHOR ("Sriram Madhivanan, Tyler Allen, Keerthan Jaic," 
               " Praarthana Ramakrishnan");

// This queue head is used for snoozing while DMA buffers are full. 
DECLARE_WAIT_QUEUE_HEAD (dma_snooze);
// This queue head is used for snoozing while waiting for DMA buffers to 
// completely empty before exiting.
DECLARE_WAIT_QUEUE_HEAD (unbind_snooze);

// Struct containing info on a physical region of memory.
struct phys_region
{
    // physical address
    phys_addr_t p_base;
    // length of this region
    unsigned long len;
    // kernel addr to this region.
    unsigned int *k_base;
};

// Structure containing fifo information.
struct _fifo
{
    // phyiscal addr of fifo.
    dma_addr_t p_base;
    // kernel base of fifo
    struct fifo_entry *k_base;
    // software head of fifo
    u32 head;
    // tail cache of fifo
    u32 tail_cache;
};

// Structure containing DMA buffers.
struct k3_dma_buf
{
    // kernel base
    unsigned int *k_base;
    // user base
    unsigned long u_base;
    // address 
    dma_addr_t handle;
    // number of bytes filled in this buffer.
    int current_size;
} dma[DMA_BUFNUM];

// Driver variables.
struct kyouko3_vars
{
    // control region
    struct phys_region control;
    // framebuffer
    struct phys_region fb;
    // True if VMODE is on.
    bool graphics_on;
    // True if DMA is enabled.
    bool dma_on;
    // Fifo
    struct _fifo fifo;
    // Device
    struct pci_dev *pdev;
    // Next available address to fill in DMA buffers.
    u32 fill;
    // next dma buffer to drain.
    u32 drain;
} k3;


/*
 * Macro for writing to kyouko3 register. 
 * Params:
 * reg - register to write to.
 * value - value to write to register.
 */
inline void
K_WRITE_REG (unsigned int reg, unsigned int value)
{
    *(k3.control.k_base + (reg >> 2)) = value;
}

/*
 * Macro for reading from kyouko3 register. 
 * Params:
 * reg - register to read.
 */
inline unsigned int
K_READ_REG (unsigned int reg)
{
    rmb ();
    return *(k3.control.k_base + (reg >> 2));
}

/*
 * Init function for the fifo. Sets up fifo head and tail values, and 
 * allocates memory for the fifo.
 */
void
fifo_init (void)
{
    k3.fifo.k_base =
	pci_alloc_consistent (k3.pdev, 8 * FIFO_ENTRIES, &k3.fifo.p_base);
    K_WRITE_REG (FIFO_START, k3.fifo.p_base);
    K_WRITE_REG (FIFO_END, k3.fifo.p_base + 8 * FIFO_ENTRIES);
    k3.fifo.head = 0;
    k3.fifo.tail_cache = 0;
}


/*
 * Flush function for synchronizing the software-copy of the FIFO with the 
 * on-card fifo.
 */
void
fifo_flush (void)
{
    K_WRITE_REG (FIFO_HEAD, k3.fifo.head);
    while (k3.fifo.tail_cache != k3.fifo.head)
    {
        k3.fifo.tail_cache = K_READ_REG (FIFO_TAIL);
        schedule ();
    }
}

/*
 * Function for writing commands to fifo.
 * Parameters:
 * cmd - FIFO command to execute.
 * val - Value used for the command.
 */
void
fifo_write (u32 cmd, u32 val)
{
    k3.fifo.k_base[k3.fifo.head].command = cmd;
    k3.fifo.k_base[k3.fifo.head].value = val;
    k3.fifo.head++;
    if (k3.fifo.head >= FIFO_ENTRIES)
    {
	    k3.fifo.head = 0;
    }
}

/*
 * DMA interrupt handler.
 */
irqreturn_t
dma_isr (int irq, void *dev_id, struct pt_regs *regs)
{
    int full;
    int empty;
    int size;
    u32 iflags = K_READ_REG (INFO_STATUS);

    K_WRITE_REG (INFO_STATUS, 0xf);

    // spurious interrupt
    if ((iflags & 0x02) == 0)
    {
	    return IRQ_NONE;
    }
    // Check if all buffers full when interrupt was called.
    full = k3.fill == k3.drain;
    k3.drain = (k3.drain + 1) % DMA_BUFNUM;
    // Check if buffers are now empty.
    empty = k3.fill == k3.drain;
    // If buffers left in queue, start next one
    if (!empty)
    {
        size = dma[k3.fill].current_size;
        fifo_write (BUFA_ADDR, dma[k3.drain].handle);
        fifo_write (BUFA_CONF, size);
        K_WRITE_REG (FIFO_HEAD, k3.fifo.head);
    }
    // Buffer is empty and dma is off, we are shutting down so wake user.
    else if (!k3.dma_on)
    {
	    wake_up_interruptible (&unbind_snooze);
    }
    // Wake up sleeping user if buffer was full.
    if (full)
    {
	    wake_up_interruptible (&dma_snooze);
    }
    // if not-spurious, then 
    return IRQ_HANDLED;
}

/*
 * Initiate a DMA request if possible.
 */
int
initiate_transfer (unsigned long size)
{
    int ret;
    unsigned long flags;
    // grab dma_snooze lock so that we can use wait_event_interruptible_locked.
    spin_lock_irqsave (&dma_snooze.lock, flags);
    dma[k3.fill].current_size = size;
    if (k3.fill == k3.drain)
    {
        // SET LOCKED RETURN VALUE
        k3.fill = (k3.fill + 1) % DMA_BUFNUM;
        ret = k3.fill;

        // push next item to card.
        fifo_write (BUFA_ADDR, dma[k3.drain].handle);
        fifo_write (BUFA_CONF, size);
        K_WRITE_REG (FIFO_HEAD, k3.fifo.head);

        spin_unlock_irqrestore (&dma_snooze.lock, flags);
        return ret;
    }
    // SET LOCKED RETURN VALUE
    k3.fill = (k3.fill + 1) % DMA_BUFNUM;
    ret = k3.fill;
    if (k3.fill == k3.drain)
    {
	    //release lock while asleep, but do condition testing w/ lock
        //thanks to interruptible_locked.
	    wait_event_interruptible_locked (dma_snooze, k3.fill != k3.drain);
    }
    spin_unlock_irqrestore (&dma_snooze.lock, flags);
    return ret;
}

/*
 * IOCTL function containing logic for turning on/off graphics mode, on/off
 * dma, writing to the fifo, and flushing the fifo queue.
 */
long
kyouko3_ioctl (struct file *fp, unsigned int cmd, unsigned long arg)
{
    struct fifo_entry entry;
    void __user *argp = (void __user *) arg;
    int i;
    long ret = 0;
    int count;

    switch (cmd)
    {
	    case VMODE:
		{
            // init graphics mode
		    if (arg == GRAPHICS_ON)
		    {

                K_WRITE_REG (FRAME_COLUMNS, 1024);
                K_WRITE_REG (FRAME_ROWS, 768);
                K_WRITE_REG (FRAME_ROWPITCH, 1024 * 4);
                K_WRITE_REG (FRAME_PIXELFORMAT, 0xf888);
                K_WRITE_REG (FRAME_STARTADDRESS, 0);

                K_WRITE_REG (CONF_ACCELERATION, 0x40000000);

                K_WRITE_REG (ENC_WIDTH, 1024);
                K_WRITE_REG (ENC_HEIGHT, 768);
                K_WRITE_REG (ENC_OFFSETX, 0);
                K_WRITE_REG (ENC_OFFSETY, 0);
                K_WRITE_REG (ENC_FRAME, 0);

                K_WRITE_REG (CONF_MODESET, 0);

                msleep (10);

                fifo_write (CLEAR_COLOR, 0);
                fifo_write (CLEAR_COLOR + 0x0004, 0);
                fifo_write (CLEAR_COLOR + 0x0008, 0);
                fifo_write (CLEAR_COLOR + 0x000c, 0);

                fifo_write (RASTER_CLEAR, 3);
                fifo_write (RASTER_FLUSH, 0);
                fifo_flush ();

                k3.graphics_on = 1;
            }
            // disable graphics mode.
            else if (arg == GRAPHICS_OFF)
            {
                fifo_flush();
                K_WRITE_REG(CONF_ACCELERATION, 0x80000000);
                K_WRITE_REG (CONF_MODESET, 0);
                k3.graphics_on = 0;
            }
            break;
		}
        // Add item to fifo queue.
	    case FIFO_QUEUE:
		{
		    if ((ret = copy_from_user (&entry, argp, sizeof (struct fifo_entry))))
		    {
			    return -EFAULT;
		    }
		    fifo_write (entry.command, entry.value);
		    break;
		}
        // Sync software fifo with hardware fifo.
	    case FIFO_FLUSH:
		{
		    fifo_flush ();
		    break;
		}
        // Bind DMA buffers to begin dma actions.
	    case BIND_DMA:
		{
		    k3.dma_on = 1;
            // bail if we can't init
		    if ((ret = pci_enable_msi (k3.pdev)))
		    {
			    return ret;
		    }
		    if ((ret =
			        request_irq (k3.pdev->irq, (irq_handler_t) dma_isr,
				    IRQF_SHARED, "kyouku3 dma isr", &k3)))
		    {
			    return ret;
		    }
		    K_WRITE_REG (CONF_INTERRUPT, 0x02);
            // acquire dma buffers
		    for (i = 0; i < DMA_BUFNUM; i++)
		    {
                k3.fill = i;
                dma[i].k_base = pci_alloc_consistent (k3.pdev, DMA_BUFSIZE,
                                                      &dma[i].handle);
                dma[i].u_base = vm_mmap (fp, 0, DMA_BUFSIZE, 
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, VM_PGOFF_DMA);
		    }
            // init dma queue counters.
		    k3.fill = 0;
		    k3.drain = 0;
            // copy back first address or bail if we fail.
		    if ((ret = copy_to_user (argp, &dma[0].u_base, 
                                    sizeof (unsigned long))))
		    {
			    return ret;
		    }
		    break;
		}
        // Finish DMA and de-queue buffers.
	    case UNBIND_DMA:
		{
            // set flag to wake up user when buffer is empty
		    k3.dma_on = 0;
		    // snooze user and empty queue
		    if (k3.fill != k3.drain)
		    {
                wait_event_interruptible (unbind_snooze,
                                 k3.fill == k3.drain);
		    }
            // Unmap buffers.
		    for (i = 0; i < DMA_BUFNUM; i++)
		    {
                vm_munmap (dma[i].u_base, DMA_BUFSIZE);
                pci_free_consistent (k3.pdev, DMA_BUFSIZE,
                             dma[i].k_base, dma[i].handle);
		    }
		    K_WRITE_REG (CONF_INTERRUPT, 0);
		    free_irq (k3.pdev->irq, &k3);
		    pci_disable_msi (k3.pdev);
		    break;
		}
        // Start a dma buffer.
	    case START_DMA:
		{
            // using copies to preserve return as an error value.
		    if ((ret = copy_from_user (&count, argp, sizeof (unsigned int))))
		    {
			    return ret;
		    }
            // initiate transfer is the bulk of this function
		    ret = initiate_transfer (count);
		    if ((ret = copy_to_user (argp, &dma[ret].u_base, 
                                    sizeof (unsigned long))))
		    {
			    return ret;
		    }
		    break;
		}
    }
    return ret;
}

/*
 * Open function for kernel module.
 */
int
kyouko3_open (struct inode *inode, struct file *fp)
{
    // ioremap_wc is faster than ioremap on some hardware
    k3.control.k_base = ioremap_wc (k3.control.p_base, k3.control.len);
    k3.fb.k_base = ioremap_wc (k3.fb.p_base, k3.fb.len);
    fifo_init ();
    return 0;
}

/*
 * Release function for kernel module.
 */
int
kyouko3_release (struct inode *inode, struct file *fp)
{
    kyouko3_ioctl(fp, VMODE, GRAPHICS_OFF);
    fifo_flush();
    iounmap (k3.control.k_base);
    iounmap (k3.fb.k_base);
    pci_free_consistent (k3.pdev, 8192, k3.fifo.k_base, k3.fifo.p_base);
    return 0;
}

/*
 * Memory mapping function. Maps to control region, framebuffer, or DMA
 * buffers.
 */
int
kyouko3_mmap (struct file *fp, struct vm_area_struct *vma)
{
    int ret = 0;
    unsigned long off;

    // vm_iomap_memory provides a simpler API than io_remap_pfn_range and 
    // reduces possibilities for bugs
    // Offset is just used to choose regions, it isn't a real offset.
    off = vma->vm_pgoff << PAGE_SHIFT;
    vma->vm_pgoff = 0;
    switch (off)
    {
        // Control region
	    case VM_PGOFF_CONTROL:
		{
		    ret =
			vm_iomap_memory (vma, k3.control.p_base,
					 k3.control.len);
		    break;
		}
        // Framebuffer
	    case VM_PGOFF_FB:
		{
		    ret = vm_iomap_memory (vma, k3.fb.p_base, k3.fb.len);
		    break;
		}
        // DMA
	    case VM_PGOFF_DMA:
		{
		    ret =
			vm_iomap_memory (vma, dma[k3.fill].handle, DMA_BUFSIZE);
		    break;
		}
    }
    return ret;
}

/*
 * File operations structure
 */ 
struct file_operations kyouko3_fops = {
    .open = kyouko3_open,
    .release = kyouko3_release,
    .mmap = kyouko3_mmap,
    .unlocked_ioctl = kyouko3_ioctl,
    .owner = THIS_MODULE
};

// Kyouko3 device.
struct cdev kyouko3_dev;

/*
 * Probe function.
 */
int
kyouko3_probe (struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
    k3.pdev = pdev;

    k3.control.p_base = pci_resource_start (pdev, 1);
    k3.control.len = pci_resource_len (pdev, 1);

    k3.fb.p_base = pci_resource_start (pdev, 2);
    k3.fb.len = pci_resource_len (pdev, 2);

    return 0
}

/*
 * Remove function.
 */
void
kyouko3_remove (struct pci_dev *pdev)
{
    pci_disable_device (pdev);
}

/*
 * Device ids.
 */
struct pci_device_id kyouko3_dev_ids[] = {
    {PCI_DEVICE (PCI_VENDOR_ID_CCORSI, PCI_DEVICE_ID_CCORSI_KYOUKO3)},
    {0}
};

/*
 * Driver struct
 */
struct pci_driver kyouko3_pci_drv = {
    .name = "kyouko3_pci_drv",
    .id_table = kyouko3_dev_ids,
    .probe = kyouko3_probe,
    .remove = kyouko3_remove
};

/*
 * Basic init function.
 */
int
kyouko3_init (void)
{
    int ret;
    cdev_init (&kyouko3_dev, &kyouko3_fops);
    cdev_add (&kyouko3_dev, MKDEV (500, 127), 1);
    k3.dma_on = 0;
    ret = pci_register_drive(&kyouko3_pci_drv);
    pci_enable_device (pdev);
    pci_set_master (pdev);
    return ret;
}

/*
 * Basic exit function.
 */
void
kyouko3_exit (void)
{
    cdev_del (&kyouko3_dev);
    pci_unregister_driver (&kyouko3_pci_drv);
}

module_init (kyouko3_init);
module_exit (kyouko3_exit);
