#include "library.h"
#include "framebuffer.h"

int console_current_line = 0;


const uint32_t black = 0x00000000;
const uint32_t white = 0xFFFFFFFF;

void print(const char* string){
 
 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], i*8, console_current_line*16, white, black);
 }

 console_current_line++;

}

