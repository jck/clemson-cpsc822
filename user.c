/*
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

/*
 * Container for briefly storing dma information.
 */
struct dma_req
{
    unsigned int *u_base;
    __u32 count;
};

struct u_kyouko_device
{
    unsigned int *u_control_base;
    unsigned int *u_fb_base;
    int fd;
} kyouko3;

/*
 * This is a series of wrapper functions.
 */
unsigned int
U_READ_REG (unsigned int reg)
{
    return (*(kyouko3.u_control_base + (reg >> 2)));
}

void
U_WRITE_FB (unsigned int reg, unsigned int value)
{
    *(kyouko3.u_fb_base + reg) = value;
}

void
fifo_queue (unsigned int cmd, unsigned int val)
{
    struct fifo_entry entry = { cmd, val };
    ioctl (kyouko3.fd, FIFO_QUEUE, &entry);
}

static inline void
fifo_flush ()
{
    printf("flushing fifo\n");
    ioctl (kyouko3.fd, FIFO_FLUSH, 0);
}

void
bind_dma (struct dma_req *req)
{
    printf("bind dma\n");
    ioctl (kyouko3.fd, BIND_DMA, &req->u_base);
}

void
start_dma (struct dma_req *req)
{
    printf("start dma\n");
    ioctl (kyouko3.fd, START_DMA, &req->u_base);
}

void
unbind_dma (void)
{
    printf("unbind dma\n");
    ioctl (kyouko3.fd, UNBIND_DMA, 0);
}

/*
 * Draws red line,
 */
void
draw_line_fb ()
{
    for (int i = 200 * 1024; i < 201 * 1024; i++)
    {
	    U_WRITE_FB (i, 0xff0000);
    }
}

/*
 * DRaws a triangle using fifo.
 */
void
fifo_triangle ()
{
    float triangle[3][2][4] = {
	{{-0.5, -0.5, 0, 1.0}, {1.0, 0, 0, 0}},
	{{0.5, 0, 0, 1.0}, {0, 1.0, 0, 0}},
	{{0.125, 0.5, 0, 1.0}, {0, 0, 1.0, 0}},
    };

    fifo_queue (COMMAND_PRIMITIVE, 1);
    
    for (int i = 0; i < 3; i++)
    {
        float *pos = triangle[i][0];
        float *col = triangle[i][1];

        for (int j = 0; j < 4; j++)
        {
            fifo_queue (VERTEX_COORD + 4 * j, *(unsigned int *) &pos[j]);
            fifo_queue (VERTEX_COLOR + 4 * j, *(unsigned int *) &col[j]);
        }
        fifo_queue (VERTEX_EMIT, 0);
    }
    fifo_queue (COMMAND_PRIMITIVE, 0);
    fifo_queue (RASTER_FLUSH, 0);
    fifo_flush ();
}

/*
 * Draws random triangles that fill a  buffer.
 */
unsigned long
rand_dma_triangle (unsigned long arg)
{
    float triangle[3][6] = {
	{1.0, 0, 0, 0.5, 0.5, 0},
	{0, 1.0, 0, 0.5, 0, 0},
	{0, 0, 1.0, 0.125, 0.5, 0},
    };
    int vertices = 0;
    unsigned long c = 0;
    unsigned int *buf = (unsigned int *) arg;

    struct kyouko3_dma_hdr hdr = {
	.stride = 5,
	.rgb = 1,
	.b12 = 1,
	.opcode = 0x14
    };

    buf[c] = *(unsigned int *) &hdr;
    c++;

    // Generate random triangles
    while (c < 100)
    {
        vertices += 3;
        for (int i = 0; i < 3; i++)
        {
            for (int j = 3; j < 5; j++)
            {
            triangle[i][j] = ((float) rand ()) / (RAND_MAX / 2) - 1;
            }
        }
        for (int i = 0; i < 3; i++)
        {
            for (int j = 0; j < 6; j++)
            {
                buf[c] = *(unsigned int *) &triangle[i][j];
                c++;
            }
        }
    }
    arg = c * 4;
    hdr.count = vertices;
    ioctl (kyouko3.fd, START_DMA, &arg);
    fifo_queue (RASTER_FLUSH, 0);
    return arg;
}


int
main ()
{
    kyouko3.fd = open ("/dev/kyouko3", O_RDWR);
    kyouko3.u_control_base =
	    mmap (0, KYOUKO_CONTROL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	      kyouko3.fd, VM_PGOFF_CONTROL);
    kyouko3.u_fb_base =
	    mmap (0, U_READ_REG (Device_RAM) * 1024 * 1024, PROT_READ | PROT_WRITE,
	      MAP_SHARED, kyouko3.fd, VM_PGOFF_FB);

    // draw line
    printf ("Drawing line by writing to FB\n");
    sleep(2);
    ioctl (kyouko3.fd, VMODE, GRAPHICS_ON);
    draw_line_fb();
    sleep(2);
    ioctl (kyouko3.fd, VMODE, GRAPHICS_OFF);
    // draw fifo triangle
    sleep(2);

    printf ("Drawing triangle by queing FIFO cmds\n");
    sleep(2);
    ioctl (kyouko3.fd, VMODE, GRAPHICS_ON);
    fifo_triangle();
    sleep(2);
    ioctl (kyouko3.fd, VMODE, GRAPHICS_OFF);


    printf ("Drawing random triangles with dma.\n");
    sleep(2);
    // show off DMA
    ioctl (kyouko3.fd, VMODE, GRAPHICS_ON);
    unsigned long arg = 0;

    //BIND_DMA
    ioctl (kyouko3.fd, BIND_DMA, &arg);
    srand(time(NULL));
    for (int i = 0; i < 100; i++)
    {
    	arg = rand_dma_triangle (arg);
    }
    fifo_flush ();
    sleep(6);
    // UNBIND_DMA
    unbind_dma ();
    ioctl (kyouko3.fd, VMODE, GRAPHICS_OFF);

    // cleanup
    close (kyouko3.fd);
    return 0;
}
