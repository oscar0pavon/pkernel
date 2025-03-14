#include "ps2_keyboard.h"
#include <stdbool.h>
#include <stdint.h>

#include "../input_output.h"

typedef uint8_t key[3];

static key keyboard[2] = {
	{PS2_KEYBOARD_A_PRESSED,PS2_KEYBOARD_A_RELEASED,'a'},
	{PS2_KEYBOARD_D_PRESSED,PS2_KEYBOARD_D_RELEASED,'d'}
};

static int current_procces_key = 254;
static bool proccess_key = false;
static bool error_procces = false;

char ps2_keyboard_get_input() {
  int keyboard_size = sizeof(keyboard) / 3;

  byte ps2_response = port_60();

  char buff[2];
  buff[0] = '\0';
  buff[1] = '\0';

  if (proccess_key) {
    if (ps2_response == keyboard[current_procces_key][1]) {
      proccess_key = false;
      return keyboard[current_procces_key][2];
    }
    return '\0';
  }

  for (int i = 0; i < keyboard_size; i++) {
    if (ps2_response == keyboard[i][0]) {
      current_procces_key = i;
      proccess_key = true;
      break;
    }
  }

  return '\0';
}
