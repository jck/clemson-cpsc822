#include <linux/ioctl.h>
#include <linux/types.h>

struct fifo_entry {
  __u32 command;
  __u32 value;
};

struct dma_req {
	unsigned int *u_base;
	__u32 count;
};

struct kyouko3_dma_hdr {
  __u32 stride:5;
  __u32 w:1;
  __u32 rgb:1;
  __u32 unknown:5;
  __u32 b12:1;
  __u32 b13:1;

  __u32 count:10;
  __u32 opcode:8;
};

// Page offsets for mmap
#define VM_PGOFF_CONTROL 0
#define VM_PGOFF_FB 0x80000000
#define VM_PGOFF_DMA 0x40000000

// IOCTL
#define VMODE _IOW(0xcc,0,unsigned long)
#define FIFO_QUEUE _IOWR(0XCC,3,unsigned long)
#define FIFO_FLUSH _IO(0xcc,4)
#define BIND_DMA _IOW(0xcc, 1, unsigned long)
#define UNBIND_DMA _IOW(0xcc, 5, unsigned long)
#define START_DMA _IOWR(0xcc, 2, unsigned long)

#define GRAPHICS_OFF 0
#define GRAPHICS_ON 1

#define FRAME_COLUMNS 0x8000
#define FRAME_ROWS 0x8004
#define FRAME_ROWPITCH 0x8008
#define FRAME_PIXELFORMAT 0x800C
#define FRAME_STARTADDRESS 0x8010

#define ENC_WIDTH 0x9000
#define ENC_HEIGHT 0x9004
#define ENC_OFFSETX 0x9008
#define ENC_OFFSETY 0x900c
#define ENC_FRAME 0x9010


#define CLEAR_COLOR 0x5100

#define RASTER_CLEAR 0x3008
#define RASTER_FLUSH 0x3FFC

#define CONF_ACCELERATION 0x1010
#define CONF_MODESET 0x1008
#define CONFIG_REBOOT 0x1000

#define VERTEX_COORD 0x5000
#define VERTEX_COLOR 0x5010
#define VERTEX_EMIT 0x3004

#define COMMAND_PRIMITIVE 0x3000

#define FIFO_START 0x1020
#define FIFO_END 0x1024
#define FIFO_HEAD 0x4010
#define FIFO_TAIL 0x4014
#define FIFO_STATUS 0x4008

#define BUFA_ADDR 0x2000
#define BUFA_CONF 0x2008
