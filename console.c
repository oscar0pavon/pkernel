#include "console.h"
#include "library.h"
#include "framebuffer.h"
#include "gop.h"
#include <stdint.h>

int console_current_line = 0;


const uint32_t black = 0x000000;
const uint32_t white = 0xFFFFFF;
const uint32_t red_pixel = 0xFF0000;
const uint32_t green_pixel = 0x00FF00;
const uint32_t blue_pixel = 0x0000FF;
const uint32_t background_color = 0x282C34;

int console_horizonal = 0;
int console_vertical = 0;

void print(const char* string){
 
 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], i*8, console_current_line*16, white, background_color);
 }

 console_current_line++;

}

void clear(){

	for(int y = 0; y < console_horizonal; y++){
		for(int x = 0; x < console_vertical; x++){
			plot_pixel(x, y, background_color);
		}
	}
}

void print_uint(uint32_t number){

	char buf[16];
	char *pos = buf;

	do {
		const unsigned d = number % 10;

		number = number / 10;
		if (d < 10)
			*pos = d + '0';
		else
			*pos = d - 10 + 'A';
		++pos;
	} while (number);

	for (char *l = buf, *r = pos - 1; l < r; ++l, --r) {
		const char c = *l;

		*l = *r;
		*r = c;
	}

 int char_count = string_length(buf);
 for(int i = 0; i < char_count ; i++){
	draw_character(buf[i], i*8, console_current_line*16, white, background_color);
 }

 console_current_line++;
}

