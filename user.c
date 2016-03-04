/* vim: sw=2 ts=2 sts=3 et
 * Authors: Praarthana Ramakrishnan, Keerthan Jaic, Tyler Allen,
 *          Sriram Madhivanan
 *
 * Version: 03/02/2016
 *
 * This is some test user code for the kyouko3 driver. It will draw a red
 * line using the framebuffer, followed by a triangle using the fifo, followed
 * by many random triangles using DMA.
 */

#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "kyouko3.h"

// Debug macros

// Print current function name
#define PFN() printf("%s\n", __PRETTY_FUNCTION__);

/*
 * Container for briefly storing dma information.
 */
struct dma_req {
  unsigned int *u_base;
  __u32 count;
};

struct u_kyouko_device {
  unsigned int *u_control_base;
  unsigned int *u_fb_base;
  unsigned long fb_len;
  int fd;
} k3;

/*
 * This is a series of wrapper functions.
 */

unsigned int U_READ_REG(unsigned int reg) {
  return (*(k3.u_control_base + (reg >> 2)));
}

void U_WRITE_FB(unsigned int reg, unsigned int value) {
  *(k3.u_fb_base + reg) = value;
}

void gfx_on(void) { ioctl(k3.fd, VMODE, GRAPHICS_ON); }

void gfx_off(void) { ioctl(k3.fd, VMODE, GRAPHICS_OFF); }

void fifo_queue(unsigned int cmd, unsigned int val) {
  struct fifo_entry entry = {cmd, val};
  ioctl(k3.fd, FIFO_QUEUE, &entry);
}

static inline void fifo_flush() {
  printf("flushing fifo\n");
  ioctl(k3.fd, FIFO_FLUSH, 0);
}

void bind_dma(struct dma_req *req) {
  printf("bind dma\n");
  ioctl(k3.fd, BIND_DMA, (unsigned long)&req->u_base);
}

void start_dma(struct dma_req *req) { ioctl(k3.fd, START_DMA, &req->count); }

void unbind_dma(void) {
  printf("unbind dma\n");
  ioctl(k3.fd, UNBIND_DMA, 0);
}

void user_init() {
  k3.fd = open("/dev/kyouko3", O_RDWR);
  k3.u_control_base = mmap(0, KYOUKO_CONTROL_SIZE, PROT_READ | PROT_WRITE,
                           MAP_SHARED, k3.fd, VM_PGOFF_CONTROL);
  k3.fb_len = U_READ_REG(Device_RAM) * 1024 * 1024;
  k3.u_fb_base = mmap(0, k3.fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, k3.fd,
                      VM_PGOFF_FB);
  srand(time(NULL));
}

void user_exit() {
  munmap(k3.u_control_base, KYOUKO_CONTROL_SIZE);
  munmap(k3.u_fb_base, k3.fb_len);
  close(k3.fd);
}

unsigned int rand_f_range(float min, float max) {
  float f = (max - min) * ((((float)rand()) / (float)RAND_MAX)) + min;
  return *(unsigned int *)&f;
}

unsigned int rand_col(void) { return rand_f_range(0, 1); }

unsigned int rand_vtx(void) { return rand_f_range(-1, 1); }

void draw_line_fb() {
  for (int i = 200 * 1024; i < 201 * 1024; i++) {
    U_WRITE_FB(i, 0xff0000);
  }
}

void fifo_triangle() {
  sleep(2);
  gfx_on();

  float triangle[3][2][4] = {
      {{-0.5, -0.5, 0, 1.0}, {1.0, 0, 0, 0}},
      {{0.5, 0, 0, 1.0}, {0, 1.0, 0, 0}},
      {{0.125, 0.5, 0, 1.0}, {0, 0, 1.0, 0}},
  };

  fifo_queue(COMMAND_PRIMITIVE, 1);

  for (int i = 0; i < 3; i++) {
    float *pos = triangle[i][0];
    float *col = triangle[i][1];

    for (int j = 0; j < 4; j++) {
      fifo_queue(VERTEX_COORD + 4 * j, *(unsigned int *)&pos[j]);
      fifo_queue(VERTEX_COLOR + 4 * j, *(unsigned int *)&col[j]);
    }
    fifo_queue(VERTEX_EMIT, 0);
  }
  fifo_queue(COMMAND_PRIMITIVE, 0);
  fifo_queue(RASTER_FLUSH, 0);
  fifo_flush();

  sleep(2);
  gfx_off();
}

// Writes random dma formatted triangles into a buffer
void gen_dma_triangles(struct dma_req *req, int num) {
  unsigned int *buf = req->u_base;

  struct kyouko3_dma_hdr hdr = {
      .stride = 5, .rgb = 1, .b12 = 1, .opcode = 0x14, .count = num * 3};
  *buf++ = *(unsigned int *)&hdr;

  for (int i = 0; i < num; i++) {
    for (int j = 0; j < 3; j++) {
      for (int k = 0; k < 3; k++) {
        *buf++ = rand_col();
      }
      for (int k = 0; k < 3; k++) {
        *buf++ = rand_vtx();
      }
    }
  }
  req->count = (1 + num * 18) * sizeof(unsigned int);
}

void dma_triangles() {
  sleep(2);
  gfx_on();

  struct dma_req req;

  bind_dma(&req);

  for (int i = 0; i < 1000; i++) {
    gen_dma_triangles(&req, 2);
    start_dma(&req);
    fifo_queue(RASTER_FLUSH, 0);
  }
  fifo_flush();
  unbind_dma();

  sleep(6);
  gfx_off();
}


int demos() {
  // Demos
  user_init();

  fifo_triangle();
  dma_triangles();

  user_exit();
  return 0;
}

void test_fifo_stress_simple() {
  // Works most of the time. However, sometimes K_WRITE_REG fifo_head
  // doesn't seem to work, and it ends up in an infinite loop.
  for (int i=0; i < 100; i++) {
    fifo_triangle();
  }
}

void test_gfx_on_then_close() {
  // Works
  PFN();
  user_init();
  gfx_on();
  sleep(5);
  user_exit();
}

void test_dma_bind_unbind() {
  // Works
  user_init();
  gfx_on();
  struct dma_req req;
  bind_dma(&req);
  sleep(10);
  unbind_dma();
  user_exit();
}

void test_dma_bind_close() {
  // Works
  PFN();
  user_init();
  gfx_on();
  sleep(10);
  struct dma_req req;
  bind_dma(&req);
  sleep(10);
  user_exit();
}

int tests() {
  // test_gfx_on_then_close();
  // test_dma_bind_unbind();
  test_dma_bind_close();
  return 0;
}


int main() {
#ifdef TESTING
  return tests();
#else
  return demos();
#endif
}
