#include "console.h"
#include "library.h"
#include "framebuffer.h"

#include "drivers/serial.h"

#include <stdint.h>
#include <stdarg.h>

static int console_current_line = 0;


static const uint32_t black = 0x000000;
static const uint32_t white = 0xFFFFFF;
static const uint32_t red_pixel = 0xFF0000;
static const uint32_t green_pixel = 0x00FF00;
static const uint32_t blue_pixel = 0x0000FF;
static const uint32_t background_color = 0x282C34;

static int console_line_buffer_number = 0;
static int console_line_char_counter = 0;

static uint32_t console_max_lines = 0;
static uint32_t console_max_cols  = 0;

void console_init(void) {
  FrameBuffer *fb = get_framebuffer();
  console_max_lines = (fb->vertical_resolution / 16) - 1;
  console_max_cols  = fb->horizontal_resolution / 8;
}

static void console_newline(void) {
  serial_putc('\n');
  console_current_line++;
  console_line_char_counter = 0;
  if (console_max_lines > 0 && (uint32_t)console_current_line >= console_max_lines) {
    scroll_up();
    console_current_line = (int)(console_max_lines - 1);
  }
}

static void console_put_char(char c) {
  if (console_max_cols > 0 && (uint32_t)console_line_char_counter >= console_max_cols) {
    console_newline();
  }
  serial_putc(c);
  draw_character((unsigned char)c, console_line_char_counter * 8,
                 console_current_line * 16, white, background_color);
  console_line_char_counter++;
}

// Static buffer: 16 hex characters + 1 null terminator
static char hex_buffer[17];

u32 get_background_color(){
  return background_color;
}

void print_in_line_buffer_number(uint8_t line_number, char* string){
  console_current_line=line_number;
  printf("%s",string);
}

void print_in_curent_line(const char* string){
  clear_current_line();
  printf("%s",string);
}

void print_in_line_number(uint8_t line_number, char* string){
  console_current_line=line_number;
  printf("%s",string);
}



const char* get_hex_string(uint64_t value) {
    // Array map for fast indexing
    const char hex_table[] = "0123456789abcdef";
    
    // Set the null terminator at the very end of the array
    hex_buffer[16] = '\0';
    
    // Loop through all 16 nibbles, writing characters backwards
    for (int i = 15; i >= 0; i--) {
        // Extract the lowest 4 bits safely
        uint8_t nibble = value & 0x0F;
        
        // Map the nibble to its text character and store it
        hex_buffer[i] = hex_table[nibble];
        
        // Shift right by 4 bits to prepare the next nibble
        value >>= 4;
    }
    
    // Return the address pointer to the front of the string
    return hex_buffer;
}


void print_uint(uint32_t number){

	char buf[16];
	memset(buf, 0, 16);		
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

 printf(buf);

}

void print(const char* format){
 
  while (*format) {
    console_put_char(*format);
    format++;
  }

}

void console_backspace(void) {
  if (console_line_char_counter <= 0) return;
  console_line_char_counter--;
  draw_character(' ', console_line_char_counter * 8,
                 console_current_line * 16, white, background_color);
  serial_putc('\b');
  serial_putc(' ');
  serial_putc('\b');
}

void console_clear(void) {
  clear();
  console_current_line    = 0;
  console_line_char_counter = 0;
}

void clear_current_line(){
  u16 save_char_counter = console_line_char_counter;
  console_line_char_counter=0;
  for(int i=0; i < save_char_counter; i++ ){
   printf(" "); 
  }
  console_line_char_counter=0;
}

void printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
  
    while (*format) {
        if (*format == '%') {
            format++; // Move past '%'
            
            // Check for 64-bit Long modifier (e.g., %lx or %llx)
            int is_long = 0;
            if (*format == 'l') {
                is_long = 1;
                format++;
                if (*format == 'l') {
                    format++; // Consume second 'l' if using %llx
                }
            }

            if (*format == 'd') {
                if (is_long) {
                    // Handle printing 64-bit signed/unsigned longs if you have a helper
                    print_uint(va_arg(arguments, uint64_t)); 
                } else {
                    print_uint(va_arg(arguments, int));
                }
            } 
            else if (*format == 's') {
                char *string = va_arg(arguments, char *);
                // FIX RECURSION BUG: Print characters directly instead of calling printf() recursively!
                while (*string) {
                    if (*string == '\n') {
                        console_newline();
                    } else {
                        console_put_char(*string);
                    }
                    string++;
                }
            } 
            else if (*format == 'x') {
                uint64_t number;
                if (is_long) {
                    // CRITICAL FIX: Extract full 64-bit word from the variadic stack arguments
                    number = va_arg(arguments, uint64_t); 
                } else {
                    // Standard 32-bit hex, explicitly cast to unsigned to stop sign-extension
                    number = va_arg(arguments, uint32_t); 
                }
                
                const char *hex = get_hex_string(number);
                
                while (*hex) {
                    console_put_char(*hex);
                    hex++;
                }
            } 
            else if (*format == 'b') {
                // TODO: print binary
            }
            format++;
        } 
        else if (*format == '\n') {
            format++;
            console_newline();
        }
        else {
            console_put_char(*format);
            format++;
        }
    }
  
    va_end(arguments);
}

