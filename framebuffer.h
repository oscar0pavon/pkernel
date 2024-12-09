#ifndef __FRAME_BUFFER_H__
#define __FRAME_BUFFER_H__

#include <stdint.h>

struct FrameBuffer{
	uint64_t frame_buffer;
	uint32_t pixel_per_scan_line;
	uint32_t vertical_resolution;
	uint32_t horizontal_resolution;
};

typedef struct FrameBuffer FrameBuffer;

extern FrameBuffer frame_buffer;

void draw_character(unsigned char character, int x, int y,
		int foreground, int background);


void plot_pixel(int x, int y, uint32_t pixel);

#endif
