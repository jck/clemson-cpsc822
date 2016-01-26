#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Keerthan Jaic");

int kyouko3_open(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_open\n");
  return 0;
}

int kyouko3_release(struct inode *inode, struct file *fp) {
  printk(KERN_ALERT "kyouko3_release\n");
  return 0;
}

struct file_operations kyouko3_fops = {
  .open=kyouko3_open,
  .release=kyouko3_release,
  .owner=THIS_MODULE
};

struct cdev kyouko3_dev;

int kyouko3_init(void) {
  cdev_init(&kyouko3_dev, &kyouko3_fops);
  cdev_add(&kyouko3_dev, MKDEV(500, 127), 1);
  printk(KERN_ALERT "kyouko3_init\n");
  return 0;
}

void kyouko3_exit(void) {
  cdev_del(&kyouko3_dev);
  printk(KERN_ALERT "kyouko3_exit\n");
}

module_init(kyouko3_init);
module_exit(kyouko3_exit);
