#include "ps2_keyboard.h"


bool key_processed = false;
char ps2_keyboard_get_input(byte(*port_60)()){
	
	byte ps2_response = port_60();
	if(ps2_response == PS2_KEYBOARD_A_PRESSED){
		
			if(key_processed==false){
				key_processed = true;
				return 'a';
			}
			
	}


	return '\0';
}
