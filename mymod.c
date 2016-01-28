#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>

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
} kyouko3;


inline void K_WRITE_REG(unsigned int reg, unsigned int value) {
	*(kyouko3.control.k_base+(reg>>2)) = value;
}

int kyouko3_open(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_open\n");
  kyouko3.control.k_base = ioremap(kyouko3.control.p_base, kyouko3.control.len);
  kyouko3.fb.k_base = ioremap(kyouko3.fb.p_base, kyouko3.fb.len);
  return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_release\n");
  iounmap(kyouko3.control.p_base);
  iounmap(kyouko3.fb.k_base);
  return 0;
}

int kyouko3_mmap(struct file *fp, struct vm_area_struct *vma) {
  printk(KERN_ALERT "mmap\n");
  int ret = 0;
  int vma_size = vma->vm_end - vma->vm_start;

  switch(vma->vm_pgoff) {
  case 0:
    ret = vm_iomap_memory(vma, kyouko3.control.p_base, kyouko3.control.len);
    break;
  case 1:
    ret = vm_iomap_memory(vma, kyouko3.fb.p_base, kyouko3.fb.len);
    break;
  }
  return ret;
}

struct file_operations kyouko3_fops = {
  .open=kyouko3_open,
  .release=kyouko3_release,
  .mmap=kyouko3_mmap,
  .owner=THIS_MODULE
};

struct cdev kyouko3_dev;


int kyouko3_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id) {
  printk(KERN_ALERT "starting probe\n");
  // 1. physical base addr of ctrl region
  kyouko3.control.p_base = pci_resource_start(pdev, 1);
  kyouko3.control.len = pci_resource_len(pdev, 1);
  printk(KERN_DEBUG "control base, len: %x, %x\n", kyouko3.control.p_base, kyouko3.control.len);

  //2. physicla base address of the onboard ram(framebuffer)
  kyouko3.fb.p_base = pci_resource_start(pdev, 2);
  kyouko3.fb.len = pci_resource_len(pdev, 2);
  printk(KERN_DEBUG "ram len: %lu\n", kyouko3.fb.len);

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
