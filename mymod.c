#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>

#include "kyouko.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keerthan Jaic");

#define PCI_VENDOR_ID_CCORSI 0x1234
#define PCI_DEVICE_ID_CCORSI_KYOUKO3 0x1113

struct phys_region {
  unsigned int p_base;
  unsigned long len;
  unsigned int * k_base;
};


struct kyouko3_vars {
  struct phys_region control;
  struct phys_region fb;
  bool graphics_on;
} kyouko3;


inline void K_WRITE_REG(unsigned int reg, unsigned int value) {
	*(kyouko3.control.k_base+(reg>>2)) = value;
}

inline unsigned int K_READ_REG(unsigned int reg){
	return *(kyouko3.control.k_base+(reg>>2));
}

int kyouko3_open(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_open\n");
  kyouko3.control.k_base = ioremap(kyouko3.control.p_base, kyouko3.control.len);
  kyouko3.fb.k_base = ioremap(kyouko3.fb.p_base, kyouko3.fb.len);
  printk(KERN_ALERT "RAM: %u\n", K_READ_REG(0x20));
  return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_release\n");
  iounmap(kyouko3.control.k_base);
  iounmap(kyouko3.fb.k_base);
  return 0;
}

int kyouko3_mmap(struct file *fp, struct vm_area_struct *vma) {
  printk(KERN_ALERT "mmap\n");
  int ret = 0;

  switch(vma->vm_pgoff<<PAGE_SHIFT) {
  case 0:
    ret = vm_iomap_memory(vma, kyouko3.control.p_base, kyouko3.control.len);
    break;
  case 0x400000:
    printk(KERN_ALERT "%d\n", kyouko3.fb.len);
    // vm_iomap_memory fails for the fb for some reason
    // Investigate it
    // ret = vm_iomap_memory(vma, kyouko3.fb.p_base>>PAGE_SHIFT, kyouko3.fb.len);
    ret = io_remap_pfn_range(vma, vma->vm_start, kyouko3.fb.p_base>>PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot);
    break;
  }
  return ret;
}

static long kyouko3_ioctl(struct file* fp, unsigned int cmd, unsigned long arg){

  float float_one = 1.0;
  unsigned int int_float_one = *(unsigned int *)&float_one;

  switch(cmd) {
    case VMODE:
      if(arg == GRAPHICS_ON) {

        printk(KERN_ALERT "Turning ON Graphics\n");

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

        K_WRITE_REG(CONF_ACCELERATION, 0x40000000);
        K_WRITE_REG(CONF_MODESET, 0);

        K_WRITE_REG(CLEAR_COLOR, int_float_one);
        K_WRITE_REG(CLEAR_COLOR + 0x0004, int_float_one);
        K_WRITE_REG(CLEAR_COLOR + 0x0008, int_float_one);
        K_WRITE_REG(CLEAR_COLOR + 0x000c, int_float_one);

        K_WRITE_REG(RASTER_FLUSH, 0);
        K_WRITE_REG(RASTER_CLEAR, 1);

        kyouko3.graphics_on = 1;
        printk(KERN_ALERT "Graphics ON\n");
      }

      else if(arg == GRAPHICS_OFF) {
        K_WRITE_REG(CONFIG_REBOOT, 0);
        kyouko3.graphics_on = 0;
        printk(KERN_ALERT "Graphics OFF\n");
      }
      break;
    // case FIFO_QUEUE:
    //   break;
    // case FIFO_FLUSH:
    //   break;
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
  get_region_info(pdev, 1, &kyouko3.control);
  get_region_info(pdev, 2, &kyouko3.fb);

  pci_enable_device(pdev);
  pci_set_master(pdev);

  return 0;
}

struct pci_device_id kyouko3_dev_ids[] = {
  {PCI_DEVICE(PCI_VENDOR_ID_CCORSI, PCI_DEVICE_ID_CCORSI_KYOUKO3)},
  {0}
};

struct pci_driver kyouko3_pci_drv = {
  .name = "kyouko3_pci_drv",
  .id_table = kyouko3_dev_ids,
  .probe = kyouko3_probe,
  .remove = NULL
  // .remove = kyouko3_remove
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
