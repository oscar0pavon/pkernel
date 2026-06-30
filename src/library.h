#ifndef __LIBRARY_H__
#define __LIBRARY_H__

#include "types.h"

int string_length(const char* string);

	
void *memset(void *pointer, int value, size_t size);
void *memcpy(void *dst, const void *src, size_t n);
void copy_memory(void *destination, const void *source, size_t size);


#endif
