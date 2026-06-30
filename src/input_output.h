#ifndef __INPUT_OUTPUT_H__
#define __INPUT_OUTPUT_H__

#include "types.h"
#include <stdint.h>

extern byte port_60();

extern u32 input(u16);
extern byte input_byte(u16);
extern byte output_byte(byte data, u16 port);
extern u32 output(u32 data, u16 port);
extern void clear_interptions(void);

#endif
