#include "console.h"
#include "framebuffer.h"
#include "library.h"

#include "drivers/serial.h"

#include <stdarg.h>
#include <stdint.h>

static int console_current_line = 0;

static const uint32_t black = 0x000000;
static const uint32_t white = 0xFFFFFF;
static const uint32_t red_pixel = 0xFF0000;
static const uint32_t green_pixel = 0x00FF00;
static const uint32_t blue_pixel = 0x0000FF;
static const uint32_t background_color = 0x282C34;

static int console_line_buffer_number = 0;
static int console_line_char_counter = 0;

static uint32_t console_max_lines = 0;
static uint32_t console_max_cols = 0;

// Software text buffer for the console. The framebuffer (VRAM) is
// write-combining memory: reading back from it is extremely slow, so we never
// scroll by copying pixels out of VRAM. Instead every glyph that is drawn is
// also recorded here, and scrolling shifts this RAM buffer then redraws the
// screen with pure writes.
#define CONSOLE_BUF_LINES 128
#define CONSOLE_BUF_COLS 256
static char console_buffer[CONSOLE_BUF_LINES][CONSOLE_BUF_COLS];

static void console_buffer_clear_line(uint32_t line) {
  for (uint32_t col = 0; col < console_max_cols; col++)
    console_buffer[line][col] = ' ';
}

static void console_buffer_clear(void) {
  for (uint32_t line = 0; line < console_max_lines; line++)
    console_buffer_clear_line(line);
}

void console_init(FrameBuffer *in_framebuffer) {

  copy_memory(&frame_buffer, in_framebuffer, sizeof(struct FrameBuffer));

  console_max_lines = (frame_buffer.vertical_resolution / 16) - 1;
  console_max_cols = frame_buffer.horizontal_resolution / 8;

  // Clamp to the static buffer so high resolutions can't overflow it.
  if (console_max_lines > CONSOLE_BUF_LINES)
    console_max_lines = CONSOLE_BUF_LINES;
  if (console_max_cols > CONSOLE_BUF_COLS)
    console_max_cols = CONSOLE_BUF_COLS;

  console_buffer_clear();
  clear();
}

// Scroll up one line. The RAM buffer mirrors exactly what is on screen, so we
// only repaint the cells whose glyph actually changes. Trailing blanks (most of
// a text screen) stay blank between lines and are skipped, making scroll cost
// proportional to the real text width rather than the whole framebuffer.
static void console_scroll(void) {
  for (uint32_t line = 0; line + 1 < console_max_lines; line++) {
    for (uint32_t col = 0; col < console_max_cols; col++) {
      char next = console_buffer[line + 1][col];
      if (console_buffer[line][col] != next) {
        console_buffer[line][col] = next;
        draw_character((unsigned char)next, col * 8, line * 16, white,
                       background_color);
      }
    }
  }
  // Clear the newly exposed bottom line, again only where it isn't already
  // blank.
  uint32_t last = console_max_lines - 1;
  for (uint32_t col = 0; col < console_max_cols; col++) {
    if (console_buffer[last][col] != ' ') {
      console_buffer[last][col] = ' ';
      draw_character(' ', col * 8, last * 16, white, background_color);
    }
  }
}

static void console_newline(void) {
  serial_putc('\n');
  console_current_line++;
  console_line_char_counter = 0;
  if (console_max_lines > 0 &&
      (uint32_t)console_current_line >= console_max_lines) {
    console_scroll();
    console_current_line = (int)(console_max_lines - 1);
  }
}

static void console_put_char(char c) {
  if (console_max_cols > 0 &&
      (uint32_t)console_line_char_counter >= console_max_cols) {
    console_newline();
  }
  serial_putc(c);
  console_buffer[console_current_line][console_line_char_counter] = c;
  draw_character((unsigned char)c, console_line_char_counter * 8,
                 console_current_line * 16, white, background_color);
  console_line_char_counter++;
}

// Static buffer: 16 hex characters + 1 null terminator
static char hex_buffer[17];

u32 get_background_color() { return background_color; }

void print_in_line_buffer_number(uint8_t line_number, char *string) {
  console_current_line = line_number;
  printf("%s", string);
}

void print_in_curent_line(const char *string) {
  clear_current_line();
  printf("%s", string);
}

void print_in_line_number(uint8_t line_number, char *string) {
  console_current_line = line_number;
  printf("%s", string);
}

const char *get_hex_string(uint64_t value) {
  // Array map for fast indexing
  const char hex_table[] = "0123456789abcdef";

  // Set the null terminator at the very end of the array
  hex_buffer[16] = '\0';

  // Loop through all 16 nibbles, writing characters backwards
  for (int i = 15; i >= 0; i--) {
    // Extract the lowest 4 bits safely
    uint8_t nibble = value & 0x0F;

    // Map the nibble to its text character and store it
    hex_buffer[i] = hex_table[nibble];

    // Shift right by 4 bits to prepare the next nibble
    value >>= 4;
  }

  // Return the address pointer to the front of the string
  return hex_buffer;
}

// Format `value` as hex into `out` with no leading zeros (except value 0 ->
// "0"). `uppercase` selects A-F vs a-f. Returns the number of digits written.
// out must hold at least 17 bytes. printf() applies any width/zero padding.
static int hex_to_buf(uint64_t value, char *out, int uppercase) {
  const char *table = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[16];
  int n = 0;

  do {
    tmp[n++] = table[value & 0x0F];
    value >>= 4;
  } while (value);

  for (int i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i]; // reverse: least-significant nibble was first
  out[n] = '\0';
  return n;
}

void print_uint(uint32_t number) {

  char buf[16];
  memset(buf, 0, 16);
  char *pos = buf;

  do {
    const unsigned d = number % 10;

    number = number / 10;
    if (d < 10)
      *pos = d + '0';
    else
      *pos = d - 10 + 'A';
    ++pos;
  } while (number);

  for (char *l = buf, *r = pos - 1; l < r; ++l, --r) {
    const char c = *l;

    *l = *r;
    *r = c;
  }

  printf(buf);
}

void print(const char *format) {

  while (*format) {
    console_put_char(*format);
    format++;
  }
}

void console_backspace(void) {
  if (console_line_char_counter <= 0)
    return;
  console_line_char_counter--;
  console_buffer[console_current_line][console_line_char_counter] = ' ';
  draw_character(' ', console_line_char_counter * 8, console_current_line * 16,
                 white, background_color);
  serial_putc('\b');
  serial_putc(' ');
  serial_putc('\b');
}

void console_clear(void) {
  console_buffer_clear();
  clear();
  console_current_line = 0;
  console_line_char_counter = 0;
}

void clear_current_line() {
  u16 save_char_counter = console_line_char_counter;
  console_line_char_counter = 0;
  for (int i = 0; i < save_char_counter; i++) {
    printf(" ");
  }
  console_line_char_counter = 0;
}

void printf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);

  while (*format) {
    if (*format == '%') {
      format++; // Move past '%'

      // Conversion flags / width, parsed in printf-order:
      //   '-'  left-justify within the field (pad on the right)
      //   '#'  alternate form -> emit a "0x" prefix for hex
      //   '0'  pad to `width` with leading zeros instead of spaces
      //   N    minimum field width (e.g. %08x, %016lx, %2x, %-12s)
      int alt = 0;      // '#'
      int zero_pad = 0; // '0'
      int left = 0;     // '-'
      int width = 0;

      while (*format == '#' || *format == '0' || *format == '-') {
        if (*format == '#')
          alt = 1;
        else if (*format == '-')
          left = 1;
        else
          zero_pad = 1;
        format++;
      }
      while (*format >= '0' && *format <= '9') {
        width = width * 10 + (*format - '0');
        format++;
      }

      // Length modifier: 'l' / 'll' selects a 64-bit argument.
      int is_long = 0;
      if (*format == 'l') {
        is_long = 1;
        format++;
        if (*format == 'l') {
          format++; // Consume second 'l' if using %llx
        }
      }

      if (*format == 'd' || *format == 'u') {
        if (is_long) {
          // Handle printing 64-bit signed/unsigned longs if you have a helper
          print_uint(va_arg(arguments, uint64_t));
        } else {
          print_uint(va_arg(arguments, int));
        }
      } else if (*format == 's') {
        char *string = va_arg(arguments, char *);
        // Field width: pad with spaces to `width`. '-' left-justifies
        // (pad after); otherwise pad before (right-justify).
        int len = 0;
        for (char *p = string; *p; p++)
          len++;
        int pad = width - len;
        if (!left)
          while (pad-- > 0)
            console_put_char(' ');
        // Print characters directly (avoid recursive printf()).
        while (*string) {
          if (*string == '\n') {
            console_newline();
          } else {
            console_put_char(*string);
          }
          string++;
        }
        if (left)
          while (pad-- > 0)
            console_put_char(' ');
      } else if (*format == 'x' || *format == 'X') {
        int uppercase = (*format == 'X');

        uint64_t number;
        if (is_long) {
          // CRITICAL FIX: Extract full 64-bit word from the variadic stack
          // arguments
          number = va_arg(arguments, uint64_t);
        } else {
          // Standard 32-bit hex, explicitly cast to unsigned to stop
          // sign-extension
          number = va_arg(arguments, uint32_t);
        }

        char digits[17];
        int len = hex_to_buf(number, digits, uppercase);

        // The "0x" prefix counts toward the requested field width.
        int pad = width - len - (alt ? 2 : 0);

        if (zero_pad) {
          // 0x first, then zeros, then digits:  0x0000012b1
          if (alt) {
            console_put_char('0');
            console_put_char(uppercase ? 'X' : 'x');
          }
          while (pad-- > 0)
            console_put_char('0');
          for (int i = 0; i < len; i++)
            console_put_char(digits[i]);
        } else if (left) {
          // 0x, digits, then trailing spaces:  "0x12b1    "
          if (alt) {
            console_put_char('0');
            console_put_char(uppercase ? 'X' : 'x');
          }
          for (int i = 0; i < len; i++)
            console_put_char(digits[i]);
          while (pad-- > 0)
            console_put_char(' ');
        } else {
          // spaces first, then 0x, then digits:  "    0x12b1"
          while (pad-- > 0)
            console_put_char(' ');
          if (alt) {
            console_put_char('0');
            console_put_char(uppercase ? 'X' : 'x');
          }
          for (int i = 0; i < len; i++)
            console_put_char(digits[i]);
        }
      } else if (*format == 'b') {
        // TODO: print binary
      }
      format++;
    } else if (*format == '\n') {
      format++;
      console_newline();
    } else {
      console_put_char(*format);
      format++;
    }
  }

  va_end(arguments);
}
