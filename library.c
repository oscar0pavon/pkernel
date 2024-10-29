
#include "library.h"
#include "framebuffer.h"

int string_length(const char* string){
	const char* position = string;
	while(*position++)
		;
	return position - string - 1;
}


const uint32_t black = 0x00000000;
const uint32_t white = 0xFFFFFFFF;

void print(const char* string){
 
 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], i*8, 0, white, black);
 }

}
