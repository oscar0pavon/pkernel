#ifndef __PS2_KEYBOARD_H__
#define __PS2_KEYBOARD_H__

#include "../types.h"

#define PS2_KEYBOARD_A_PRESSED 0x9E

char ps2_keyboard_get_input(byte(*port_60)());

#endif
