
#include "library.h"
#include "framebuffer.h"

int string_length(const char* string){
	const char* position = string;
	while(*position++)
		;
	return position - string - 1;
}


void print(const char* string){
	
const uint32_t black = 0x00000000;
const uint32_t white = 0xFFFFFFFF;
draw_character(string[0], 0, 0, white, black);
// draw_character('u', 8, 0, white, black);
// draw_character('c', 16, 0, white, black);
// draw_character('k', 24, 0, white, black);
}
