#ifndef IDT_H
#define IDT_H

#include "types.h"

// External assembly stub triggers (Implemented in Step 2)
extern void exception_handler_0(void);  // Division by zero
extern void exception_handler_13(void); // General Protection Fault (#GP)
extern void exception_handler_14(void); // Page Fault (#PF)
extern void load_idt_asm(uint64_t idt_ptr_address);

void init_idt(void);

#endif
