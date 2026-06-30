// serial.c
#include "serial.h"
#include "../input_output.h"

#define COM1 0x3F8

void serial_init(void) {
    output_byte(0x00, COM1 + 1);  // disable interrupts
    output_byte(0x80, COM1 + 3);  // enable DLAB (set baud rate divisor)
    output_byte(0x03, COM1 + 0);  // divisor low byte → 38400 baud
    output_byte(0x00, COM1 + 1);  // divisor high byte
    output_byte(0x03, COM1 + 3);  // 8 bits, no parity, 1 stop bit
    output_byte(0xC7, COM1 + 2);  // enable FIFO, 14-byte threshold
    output_byte(0x0B, COM1 + 4);  // RTS/DSR set
}

void serial_putc(char c) {
    while (!(input_byte(COM1 + 5) & 0x20));  // wait for transmit buffer empty
    output_byte(c, COM1);
}
