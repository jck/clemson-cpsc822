#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "kyouko.h"

struct u_kyouko_device {
  unsigned int *u_control_base;
  unsigned int *u_fb_base;
} kyouko3;

#define KYOUKO_CONTROL_SIZE (65536)
#define Device_RAM (0x0020)

unsigned int U_READ_REG(unsigned int reg) {
  return (*(kyouko3.u_control_base+(reg>>2)));
}

void U_WRITE_FB(unsigned int reg, unsigned int value){
	*(kyouko3.u_fb_base + reg) = value;
}

void draw_line_fb() {
  for (int i=200*1024; i<201*1024; i++)
    U_WRITE_FB(i, 0xff0000);
}


int main() {
  int fd = open("/dev/kyouko3", O_RDWR);
  kyouko3.u_control_base = mmap(0, KYOUKO_CONTROL_SIZE, PROT_READ|PROT_WRITE,
      MAP_SHARED, fd, 0);
  kyouko3.u_fb_base = mmap(0, U_READ_REG(Device_RAM)*1024*1024, PROT_READ|PROT_WRITE,
      MAP_SHARED, fd, 0x400000);
  // printf("%u\n", kyouko3.u_fb_base);
  ioctl(fd, VMODE, GRAPHICS_ON);
  sleep(2);
  draw_line_fb();
  sleep(2);
  ioctl(fd, VMODE, GRAPHICS_OFF);
  close(fd);
  return 0;
}
