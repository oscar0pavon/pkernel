#include "framebuffer.h"


#include "font.h"
#include "library.h"
#include "console.h"
#include "types.h"
#include <stdint.h>
static FrameBuffer frame_buffer;

void* frame_buffer_in_memory;

#define PIXEL_FORMAT 4
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
  u32 pitch = PIXEL_FORMAT * framebuffer->pixel_per_scan_line;
  *((u32*)(framebuffer->vram + pitch * y + PIXEL_FORMAT * x)) = pixel;
}



void clear(){
  FrameBuffer* framebuffer = get_framebuffer();
	u32 width = framebuffer->horizontal_resolution;
	u32 height = framebuffer->vertical_resolution;

	for(int x = 0; x < width; x++){
		for(int y = 0; y < height; y++){
			plot_pixel(x, y, get_background_color());
		}
	}

	// u32 color = get_background_color();
	// byte r = color>>24;	
	// byte g = color>>16;	
	// byte b = color>>8;	
	//
	// byte *where = &framebuffer->vram;
	// int i,j;
	// for (i = 0; i < width; i++) {
 //        for (j = 0; j < height; j++) {
 //            //putpixel(vram, 64 + j, 64 + i, (r << 16) + (g << 8) + b);
 //            where[j*PIXEL_FORMAT] = r;
 //            where[j*PIXEL_FORMAT+ 1] = g;
 //            where[j*PIXEL_FORMAT+ 2] = b;
 //        }
 //        where+=PIXEL_FORMAT;
 //    }
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
