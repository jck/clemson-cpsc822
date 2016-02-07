#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "kyouko3.h"

struct u_kyouko_device {
  unsigned int *u_control_base;
  unsigned int *u_fb_base;
  int fd;
} kyouko3;

#define KYOUKO_CONTROL_SIZE (65536)
#define Device_RAM (0x0020)

unsigned int U_READ_REG(unsigned int reg) {
  return (*(kyouko3.u_control_base+(reg>>2)));
}

void U_WRITE_FB(unsigned int reg, unsigned int value){
	*(kyouko3.u_fb_base + reg) = value;
}

void fifo_queue(unsigned int cmd, unsigned int val){
  struct fifo_entry entry = {cmd, val};
  ioctl(kyouko3.fd, FIFO_QUEUE, &entry);
}

static inline void fifo_flush(){
  ioctl(kyouko3.fd, FIFO_FLUSH, 0);
}

void draw_line_fb() {
  printf("Drawing line by writing to FB\n");
  for (int i=200*1024; i<201*1024; i++)
    U_WRITE_FB(i, 0xff0000);
}

void fifo_triangle() {
  printf("Drawing triangle by queing FIFO cmds\n");
  float triangle [3][2][4] = {
    {{-0.5, -0.5, 0, 1.0}, {1.0, 0, 0, 0}},
    {{0.5, 0, 0, 1.0}, {0, 1.0, 0, 0}},
    {{0.125, 0.5, 0, 1.0}, {0, 0, 1.0, 0}},
  };

  fifo_queue(COMMAND_PRIMITIVE, 1);

  for (int i=0; i<3; i++) {
    float *pos = triangle[i][0];
    float *col = triangle[i][1];

    for (int j=0; j<4; j++) {
      fifo_queue(VERTEX_COORD + 4*j, *(unsigned int *)&pos[j]);
      fifo_queue(VERTEX_COLOR + 4*j, *(unsigned int*)&col[j]);
    }
    fifo_queue(VERTEX_EMIT, 0);
  }
  fifo_queue(COMMAND_PRIMITIVE, 0);
  fifo_queue(RASTER_FLUSH, 0);
  fifo_flush();
}


int main() {
  kyouko3.fd = open("/dev/kyouko3", O_RDWR);
  kyouko3.u_control_base = mmap(0, KYOUKO_CONTROL_SIZE, PROT_READ|PROT_WRITE,
      MAP_SHARED, kyouko3.fd, VM_PGOFF_CONTROL);
  kyouko3.u_fb_base = mmap(0, U_READ_REG(Device_RAM)*1024*1024, PROT_READ|PROT_WRITE,
      MAP_SHARED, kyouko3.fd, VM_PGOFF_FB);
  ioctl(kyouko3.fd, VMODE, GRAPHICS_ON);
  draw_line_fb();
  sleep(2);
  fifo_triangle();
  sleep(2);
  ioctl(kyouko3.fd, VMODE, GRAPHICS_OFF);
  close(kyouko3.fd);
  return 0;
}
