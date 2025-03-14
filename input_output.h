#ifndef __INPUT_OUTPUT_H__
#define __INPUT_OUTPUT_H__

#include "types.h"
#include <stdint.h>

extern byte port_60();

extern u32 input(u32);
extern u32 output(u32);

#endif
