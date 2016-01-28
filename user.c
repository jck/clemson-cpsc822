#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

struct u_kyouko_device {
  unsigned int *u_control_base;
} kyouko3;

#define KYOUKO_CONTROL_SIZE (65536)
#define Device_RAM (0x0020)

unsigned int U_READ_REG(unsigned int reg) {
  return (*(kyouko3.u_control_base+(reg>>2)));
}

int main() {
  int fd = open("/dev/kyouko3", O_RDWR);
  kyouko3.u_control_base = mmap(0, KYOUKO_CONTROL_SIZE, PROT_READ|PROT_WRITE,
      MAP_SHARED, fd, 0);
  unsigned int ram_size = U_READ_REG(Device_RAM);
  kyouko3.u_control_base = mmap(0, ram_size, PROT_READ|PROT_WRITE,
      MAP_SHARED, fd, 0x1000);
  close(fd);
  return 0;
}
