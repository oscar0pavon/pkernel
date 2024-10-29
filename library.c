
#include "library.h"

int string_length(const char* string){
	const char* position = string;
	while(*position++)
		;
	return position - string - 1;
}


