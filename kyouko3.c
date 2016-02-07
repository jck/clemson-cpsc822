#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>

#include "kyouko3.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keerthan Jaic");

#define PCI_VENDOR_ID_CCORSI 0x1234
#define PCI_DEVICE_ID_CCORSI_KYOUKO3 0x1113

#define FIFO_ENTRIES 1024

struct phys_region {
  phys_addr_t p_base;
  unsigned long len;
  unsigned int * k_base;
};


struct _fifo {
  // dma_addr_t p_base;
  dma_addr_t p_base;
  // u32 p_base;
  struct fifo_entry *k_base;
  u32 head;
  u32 tail_cache;
};


struct kyouko3_vars {
  struct phys_region control;
  struct phys_region fb;
  bool graphics_on;
  struct _fifo fifo;
  struct pci_dev *pdev;
} kyouko3;


inline void K_WRITE_REG(unsigned int reg, unsigned int value) {
	*(kyouko3.control.k_base+(reg>>2)) = value;
}

inline unsigned int K_READ_REG(unsigned int reg){
  rmb();
	return *(kyouko3.control.k_base+(reg>>2));
}

void fifo_init(void) {
  kyouko3.fifo.k_base =  pci_alloc_consistent(kyouko3.pdev, 8*FIFO_ENTRIES, &kyouko3.fifo.p_base);
  K_WRITE_REG(FIFO_START, kyouko3.fifo.p_base);
  K_WRITE_REG(FIFO_END, kyouko3.fifo.p_base + 8*FIFO_ENTRIES);
  kyouko3.fifo.head = 0;
  kyouko3.fifo.tail_cache = 0;
  if (kyouko3.fifo.head >= FIFO_ENTRIES) {
    kyouko3.fifo.head = 0;
  }
}


void fifo_flush(void) {
  K_WRITE_REG(FIFO_HEAD, kyouko3.fifo.head);
  while(kyouko3.fifo.tail_cache != kyouko3.fifo.head) {
    kyouko3.fifo.tail_cache = K_READ_REG(FIFO_TAIL);
    schedule();
  }
}

void fifo_write(u32 cmd, u32 val) {
    printk(KERN_ALERT "FIFO_write fifo.head: %d\n", kyouko3.fifo.head);
    printk(KERN_ALERT "ERR command, value: %x, %x", cmd, val);
  kyouko3.fifo.k_base[kyouko3.fifo.head].command = cmd;
  kyouko3.fifo.k_base[kyouko3.fifo.head].value = val;
  kyouko3.fifo.head++;
}

int kyouko3_open(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_open\n");
  // ioremap_wc is faster than ioremap on some hardware
  kyouko3.control.k_base = pci_ioremap_wc_bar(kyouko3.pdev, 1);
  kyouko3.fb.k_base = pci_ioremap_wc_bar(kyouko3.pdev, 2);
  fifo_init();
  printk(KERN_ALERT "RAM: %u\n", K_READ_REG(0x20));
  return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_release\n");
  iounmap(kyouko3.control.k_base);
  iounmap(kyouko3.fb.k_base);
  pci_free_consistent(kyouko3.pdev, 8192, kyouko3.fifo.k_base, kyouko3.fifo.p_base);
  return 0;
}

int kyouko3_mmap(struct file *fp, struct vm_area_struct *vma) {
  printk(KERN_ALERT "mmap\n");
  int ret = 0;

  switch(vma->vm_pgoff<<PAGE_SHIFT) {
  case VM_PGOFF_CONTROL:
    ret = vm_iomap_memory(vma, kyouko3.control.p_base, kyouko3.control.len);
    break;
  case VM_PGOFF_FB:
    // vm_iomap_memory fails for the fb for some reason
    // Investigate it
    // ret = vm_iomap_memory(vma, kyouko3.fb.p_base>>PAGE_SHIFT, kyouko3.fb.len);
    ret = io_remap_pfn_range(vma, vma->vm_start, kyouko3.fb.p_base>>PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
    break;
  }
  return ret;
}

static long kyouko3_ioctl(struct file* fp, unsigned int cmd, unsigned long arg){
  struct fifo_entry entry;

  switch(cmd) {
    case VMODE:
      if(arg == GRAPHICS_ON) {

        printk(KERN_ALERT "Turning ON Graphics\n");

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

        kyouko3.graphics_on = 1;
        printk(KERN_ALERT "Graphics ON\n");
      }

      else if(arg == GRAPHICS_OFF) {
        K_WRITE_REG(CONFIG_REBOOT, 0);
        kyouko3.graphics_on = 0;
        printk(KERN_ALERT "Graphics OFF\n");
      }
      break;
    case FIFO_QUEUE:
      printk(KERN_ALERT "FIFO_QUEUE\n");
      if (copy_from_user(&entry, (struct fifo_entry*) arg, sizeof(struct fifo_entry))) {
        return -EFAULT;
      }
      fifo_write(entry.command, entry.value);
      break;
    case FIFO_FLUSH:
      printk(KERN_ALERT "FIFO_FLUSH\n");
      fifo_flush();
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

inline void get_region_info(struct pci_dev *pdev, int num, struct phys_region *region) {
  region->p_base = pci_resource_start(pdev, num);
  region->len = pci_resource_len(pdev, num);
}


int kyouko3_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id) {
  printk(KERN_ALERT "starting probe\n");
  kyouko3.pdev = pdev;
  get_region_info(pdev, 1, &kyouko3.control);
  get_region_info(pdev, 2, &kyouko3.fb);

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
  printk(KERN_ALERT "kyouko3_init\n");
  cdev_init(&kyouko3_dev, &kyouko3_fops);
  cdev_add(&kyouko3_dev, MKDEV(500, 127), 1);
  return pci_register_driver(&kyouko3_pci_drv);
}

void kyouko3_exit(void) {
  cdev_del(&kyouko3_dev);
  pci_unregister_driver(&kyouko3_pci_drv);
  printk(KERN_ALERT "kyouko3_exit\n");
}

module_init(kyouko3_init);
module_exit(kyouko3_exit);
