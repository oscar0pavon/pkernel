#include "input.h"

#define BUF_SIZE 256

static volatile char buf[BUF_SIZE];
static volatile int  head = 0;  // write index (ISR)
static volatile int  tail = 0;  // read index (shell)

void input_putc(char c) {
    int next = (head + 1) % BUF_SIZE;
    if (next != tail) {
        buf[head] = c;
        head = next;
    }
}

char input_getc(void) {
    if (tail == head) return '\0';
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}

int input_available(void) {
    return head != tail;
}
