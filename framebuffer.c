#include "framebuffer.h"


#include "font.h"
#include "library.h"
#include "console.h"
static FrameBuffer frame_buffer;

void* frame_buffer_in_memory;

FrameBuffer* get_framebuffer(){
	return &frame_buffer;
}

void init_frambuffer(FrameBuffer* in_framebuffer){
	copy_memory(get_framebuffer(), in_framebuffer, sizeof(struct FrameBuffer));
	clear();
	printf("Horizontal %d\n", frame_buffer.horizontal_resolution);
}

void plot_pixel(int x, int y, uint32_t pixel){
  FrameBuffer *framebuffer = get_framebuffer();
  u8 pixel_format = 4;
  u32 pitch = pixel_format * framebuffer->pixel_per_scan_line;
  *((u32*)(framebuffer->vram + pitch * y + pixel_format * x)) = pixel;
}

void draw_character(unsigned char character, int x, int y,
		int foreground, int background)
{
	int cx,cy;
	int mask[8]={128,64,32,16,8,4,2,1};
	unsigned char *glyph=font+(int)character*16;

	for(cy=0;cy<16;cy++){
		for(cx=0;cx<8;cx++){
			plot_pixel(x+cx, y+cy, glyph[cy]&mask[cx]?foreground:background);
		}
	}
}
