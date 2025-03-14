#ifndef __PS2_KEYBOARD_H__
#define __PS2_KEYBOARD_H__

#include "../types.h"

#define PS2_KEYBOARD_A_RELEASED 0x9E
#define PS2_KEYBOARD_A_PRESSED 0x1E
#define PS2_KEYBOARD_D_PRESSED 0x20
#define PS2_KEYBOARD_D_RELEASED 0xA0


char ps2_keyboard_get_input();

#endif
