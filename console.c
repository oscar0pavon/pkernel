#include "console.h"
#include "library.h"
#include "framebuffer.h"


#include <stdint.h>


int console_current_line = 0;


static const uint32_t black = 0x000000;
static const uint32_t white = 0xFFFFFF;
static const uint32_t red_pixel = 0xFF0000;
static const uint32_t green_pixel = 0x00FF00;
static const uint32_t blue_pixel = 0x0000FF;
static const uint32_t background_color = 0x282C34;

int console_horizonal = 0;
int console_vertical = 0;

int console_line_buffer_number = 0;

void print_in_line_buffer_number(uint8_t line_number, char* string){
 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], (i+console_line_buffer_number)*8, line_number*16, white, background_color);
 }
 console_line_buffer_number+=char_count;
}

void print_in_curent_line(const char* string){
 
 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], i*8, console_current_line*16, white, background_color);
 }

}

void print_in_line_number(uint8_t line_number, char* string){

 int char_count = string_length(string);
 for(int i = 0; i < char_count ; i++){
	draw_character(string[i], i*8, line_number*16, white, background_color);
 }
}

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
void digit_to_hex(char*buff, uint8_t digit){

    if (digit > 9) {

      if (digit == 10) {
        buff[0] = 'A';
      }
      if (digit == 11) {
        buff[0] = 'B';
      }
      if (digit == 12) {
        buff[0] = 'C';
      }
      if (digit == 13) {
        buff[0] = 'D';
      }
      if (digit == 14) {
        buff[0] = 'E';
      }
      if (digit == 15) {
        buff[0] = 'F';
      }
    } else {
      buff[0] = digit - '0';
    }
}

void print_byte_hex(uint8_t number) {
  char buff[5];
  set_memory(buff,0,5);

  buff[0] = '0';
  buff[1] = 'x';
  if (number > 16) {

    uint8_t result1 = number / 16;
    uint8_t result2 = number % 16;
    
    if(result1>16){
      
      digit_to_hex(&buff[3], result2);
    }else{
      digit_to_hex(&buff[2], result1);
    }



  } else {
    buff[2] = '0';
    digit_to_hex(&buff[3], number);
  }

  int char_count = string_length(buff);
  for (int i = 0; i < char_count; i++) {
    draw_character(buff[i], i * 8, console_current_line * 16, white,
                   background_color);
  }

  console_current_line++;
}

void print_uint(uint32_t number){

	char buf[16];
	set_memory(buf, 0, 16);		
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

