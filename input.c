#include "input.h"

#include "console.h"

#include "drivers/ps2_keyboard.h"

void input_loop(){
	
	print_in_line_number(20,"demand# ");
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

		//DEBUG character
		//print_in_line_number(19, buff);
	}
	//not got here
}
