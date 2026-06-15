#ifndef __LIBRARY_H__
#define __LIBRARY_H__

#include "types.h"

int string_length(const char* string);

	
void *memset(void *pointer, int value, size_t size);
void copy_memory(void *destination, const void *source, size_t size);


#endif
