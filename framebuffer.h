#ifndef __FRAME_BUFFER_H__
#define __FRAME_BUFFER_H__

#include <stdint.h>

struct FrameBuffer{
	uint64_t vram;
	uint32_t pixel_per_scan_line;
	uint32_t vertical_resolution;
	uint32_t horizontal_resolution;
};

typedef struct FrameBuffer FrameBuffer;

extern void* frame_buffer_in_memory;

void draw_character(unsigned char character, int x, int y,
		int foreground, int background);


void plot_pixel(int x, int y, uint32_t pixel);

// Fill `count` 32-bit pixels at dest with `value` using non-temporal stores
// (implemented in framebuffer_asm.s).
extern void fb_fill_dwords(void *dest, uint32_t value, uint64_t count);

void clear();

FrameBuffer* get_framebuffer();

extern FrameBuffer frame_buffer;

#endif
