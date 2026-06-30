#include "framebuffer.h"

#include "font.h"
#include "console.h"
#include "types.h"
#include <stdint.h>

FrameBuffer frame_buffer;

void* frame_buffer_in_memory;

#define PIXEL_FORMAT 4

FrameBuffer* get_framebuffer(){
	return &frame_buffer;
}


void plot_pixel(int x, int y, uint32_t pixel){
  FrameBuffer *framebuffer = get_framebuffer();
  u32 pitch = PIXEL_FORMAT * framebuffer->pixel_per_scan_line;
  *((u32*)(framebuffer->vram + pitch * y + PIXEL_FORMAT * x)) = pixel;
}



void clear(){
  FrameBuffer* framebuffer = get_framebuffer();
	// One linear non-temporal fill across the whole scan-line stride. Filling the
	// per-line padding (pixel_per_scan_line >= horizontal_resolution) is harmless
	// and keeps the write contiguous for the write-combine buffers.
	uint64_t count = (uint64_t)framebuffer->pixel_per_scan_line *
			 framebuffer->vertical_resolution;
	fb_fill_dwords((void *)framebuffer->vram, get_background_color(), count);
}

void draw_character(unsigned char character, int x, int y,
		int foreground, int background)
{
	FrameBuffer *fb = get_framebuffer();
	u32 pitch = PIXEL_FORMAT * fb->pixel_per_scan_line;
	unsigned char *glyph = font + (int)character * 16;

	// Walk one scanline at a time; compute the row pointer once and write the
	// 8 pixels directly instead of recomputing the offset per pixel.
	uint8_t *row = (uint8_t *)(fb->vram + pitch * y + PIXEL_FORMAT * x);
	for (int cy = 0; cy < 16; cy++) {
		uint32_t *pixel = (uint32_t *)row;
		unsigned char bits = glyph[cy];
		for (int cx = 0; cx < 8; cx++)
			pixel[cx] = (bits & (0x80 >> cx)) ? foreground : background;
		row += pitch;
	}
}
