#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <stdint.h>

extern int console_current_line;

void print(const char* string);

void print_uint(uint32_t number);

#endif
