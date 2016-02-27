#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/printk.h>

#include "kyouko3.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keerthan Jaic");

#define PCI_VENDOR_ID_CCORSI 0x1234
#define PCI_DEVICE_ID_CCORSI_KYOUKO3 0x1113

#define FIFO_ENTRIES 1024

#define DMA_BUFNUM 8
#define DMA_BUFSIZE (124*1024)

struct phys_region {
  phys_addr_t p_base;
  unsigned long len;
  unsigned int * k_base;
};


struct _fifo {
  dma_addr_t p_base;
  struct fifo_entry *k_base;
  u32 head;
  u32 tail_cache;
};

struct k3_dma_buf {
  unsigned int * k_base;
  unsigned long u_base;
  dma_addr_t handle;
} dma[DMA_BUFNUM];


struct kyouko3_vars {
  struct phys_region control;
  struct phys_region fb;
  bool graphics_on;
  struct _fifo fifo;
  struct pci_dev *pdev;
  u32 fill;
  u32 drain;
} k3;


inline void K_WRITE_REG(unsigned int reg, unsigned int value) {
	*(k3.control.k_base+(reg>>2)) = value;
}

inline unsigned int K_READ_REG(unsigned int reg){
  rmb();
	return *(k3.control.k_base+(reg>>2));
}

void fifo_init(void) {
  k3.fifo.k_base =  pci_alloc_consistent(k3.pdev, 8*FIFO_ENTRIES, &k3.fifo.p_base);
  K_WRITE_REG(FIFO_START, k3.fifo.p_base);
  K_WRITE_REG(FIFO_END, k3.fifo.p_base + 8*FIFO_ENTRIES);
  k3.fifo.head = 0;
  k3.fifo.tail_cache = 0;
  if (k3.fifo.head >= FIFO_ENTRIES) {
    k3.fifo.head = 0;
  }
}


void fifo_flush(void) {
  K_WRITE_REG(FIFO_HEAD, k3.fifo.head);
  while(k3.fifo.tail_cache != k3.fifo.head) {
    k3.fifo.tail_cache = K_READ_REG(FIFO_TAIL);
    schedule();
  }
}

void fifo_write(u32 cmd, u32 val) {
  k3.fifo.k_base[k3.fifo.head].command = cmd;
  k3.fifo.k_base[k3.fifo.head].value = val;
  k3.fifo.head++;
}

irqreturn_t dma_isr(int irq, void *dev_id, struct pt_regs *regs){
  u32 iflags = K_READ_REG(INFO_STATUS);
  pr_info("DMA ISR. Flags: %x\n", iflags);
  K_WRITE_REG(INFO_STATUS, 0xf);
  
  // spurious interrupt
  if ((iflags & 0x02) == 0){
    return IRQ_NONE;
  }

  // if not-spurious, then 
  return IRQ_HANDLED;
}

int kyouko3_open(struct inode *inode, struct file *fp) {
  pr_info("kyouko3_open\n");
  // ioremap_wc is faster than ioremap on some hardware
  k3.control.k_base = ioremap_wc(k3.control.p_base, k3.control.len);
  k3.fb.k_base = ioremap_wc(k3.fb.p_base, k3.fb.len);
  fifo_init();
  return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp) {
  pr_info("kyouko3_release\n");
  iounmap(k3.control.k_base);
  iounmap(k3.fb.k_base);
  pci_free_consistent(k3.pdev, 8192, k3.fifo.k_base, k3.fifo.p_base);
  return 0;
}

int kyouko3_mmap(struct file *fp, struct vm_area_struct *vma) {
  pr_info("mmap\n");
  int ret = 0;

  // vm_iomap_memory provides a simpler API than io_remap_pfn_range and reduces possibilities for bugs

  // Offset is just used to choose regions, it isn't a real offset.
  unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
  vma->vm_pgoff = 0;
  switch(off) {
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

void initiate_transfer(unsigned long size)
{
      fifo_write(BUFA_ADDR, dma[0].handle);
      fifo_write(BUFA_CONF, size);
      K_WRITE_REG(FIFO_HEAD, k3.fifo.head);
      pr_info("cnt: %ld\n", size);
}

static long kyouko3_ioctl(struct file* fp, unsigned int cmd, unsigned long arg){
  struct fifo_entry entry;
  struct dma_req req;
	void __user *argp = (void __user *)arg;

  switch(cmd) {
    case VMODE:
      if(arg == GRAPHICS_ON) {

        pr_info("Turning ON Graphics\n");

        K_WRITE_REG(CONF_ACCELERATION, 0x40000000);

        K_WRITE_REG(FRAME_COLUMNS, 1024);        
        K_WRITE_REG(FRAME_ROWS, 768);        
        K_WRITE_REG(FRAME_ROWPITCH, 1024*4);        
        K_WRITE_REG(FRAME_PIXELFORMAT, 0xf888);
        K_WRITE_REG(FRAME_STARTADDRESS, 0);

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
        pr_info("Graphics ON\n");
      }

      else if(arg == GRAPHICS_OFF) {
        K_WRITE_REG(CONFIG_REBOOT, 0);
        k3.graphics_on = 0;
        pr_info("Graphics OFF\n");
      }
      break;
    case FIFO_QUEUE:
      pr_info("FIFO_QUEUE\n");
      if (copy_from_user(&entry, argp, sizeof(struct fifo_entry))) {
        return -EFAULT;
      }
      fifo_write(entry.command, entry.value);
      break;
    case FIFO_FLUSH:
      pr_info("FIFO_FLUSH\n");
      fifo_flush();
      break;
    case BIND_DMA:
          pr_info("BIND_DMA\n");
          if (pci_enable_msi(k3.pdev)) {
            pr_warn("pci_enable_msi failed\n");
          }
          if (request_irq(k3.pdev->irq, (irq_handler_t)dma_isr, IRQF_SHARED, "kyouku3 dma isr", &k3)) {
            pr_warn("request_irq failed\n");
          }
          K_WRITE_REG(CONF_INTERRUPT, 0x02);

          for (int i=0; i<DMA_BUFNUM; i++) {
            k3.fill = i;
            dma[i].k_base = pci_alloc_consistent(k3.pdev, DMA_BUFSIZE, &dma[i].handle);
            dma[i].u_base = vm_mmap(fp, 0, DMA_BUFSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, VM_PGOFF_DMA);
          }
          k3.fill = 0;
          k3.drain = 0;
          if (copy_to_user(argp, &dma[0].u_base, sizeof(unsigned long))) {
            pr_info("ctu fail\n");
          }
      break;
    case UNBIND_DMA:
          pr_info("UNBIND_DMA\n");
          for (int i=0; i<DMA_BUFNUM; i++) {
            vm_munmap(dma[i].u_base, DMA_BUFSIZE);
            pci_free_consistent(k3.pdev, DMA_BUFSIZE, dma[i].k_base, dma[i].handle);
          }
          K_WRITE_REG(CONF_INTERRUPT, 0);
          free_irq(k3.pdev->irq, &k3);
          pci_disable_msi(k3.pdev);
      break;
    case START_DMA:
          if (copy_from_user(&req.count, argp, sizeof(unsigned int))) {
            return -EFAULT;
          }
          initiate_transfer(req.count);
      break;
  }
  return 0;
}

struct file_operations kyouko3_fops = {
  .open=kyouko3_open,
  .release=kyouko3_release,
  .mmap=kyouko3_mmap,
  .unlocked_ioctl=kyouko3_ioctl,
  .owner=THIS_MODULE
};

struct cdev kyouko3_dev;

int kyouko3_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id) {
  pr_info("kyouko3 probe\n");
  k3.pdev = pdev;

  k3.control.p_base = pci_resource_start(pdev, 1);
  k3.control.len = pci_resource_len(pdev, 1);

  k3.fb.p_base = pci_resource_start(pdev, 2);
  k3.fb.len = pci_resource_len(pdev, 2);

  pci_set_master(pdev);
  return pci_enable_device(pdev);
}

void kyouko3_remove(struct pci_dev *pdev) {
	pci_disable_device(pdev);
}

struct pci_device_id kyouko3_dev_ids[] = {
  {PCI_DEVICE(PCI_VENDOR_ID_CCORSI, PCI_DEVICE_ID_CCORSI_KYOUKO3)},
  {0}
};

struct pci_driver kyouko3_pci_drv = {
  .name = "kyouko3_pci_drv",
  .id_table = kyouko3_dev_ids,
  .probe = kyouko3_probe,
  .remove = kyouko3_remove
};

int kyouko3_init(void) {
  pr_info("kyouko3_init");
  cdev_init(&kyouko3_dev, &kyouko3_fops);
  cdev_add(&kyouko3_dev, MKDEV(500, 127), 1);
  return pci_register_driver(&kyouko3_pci_drv);
}

void kyouko3_exit(void) {
  cdev_del(&kyouko3_dev);
  pci_unregister_driver(&kyouko3_pci_drv);
  pr_info("kyouko3_exit\n");
}

module_init(kyouko3_init);
module_exit(kyouko3_exit);
