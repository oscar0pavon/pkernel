#include "input.h"

#include "console.h"

#include "drivers/ps2_keyboard.h"

void input_loop(){

	print_byte_hex(10);
	print_byte_hex(PS2_KEYBOARD_D_RELEASED);

	while(1){

		char restul = 'a';
		char buff[2];
		buff[0] = '\0';
		buff[1] = '\0';


		char input = ps2_keyboard_get_input();
		buff[0] = input;
		if(input != '\0'){
			print_in_line_buffer_number(20,buff);
		}


		print_in_line_number(19, buff);
	}

	print("executed successfully");

}
