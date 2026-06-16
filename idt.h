#ifndef IDT_H
#define IDT_H

#include "types.h"

// External assembly stubs
extern void exception_handler_0(void);
extern void exception_handler_13(void);
extern void exception_handler_14(void);
extern void irq_spurious_handler(void);
extern void irq_xhci_handler(void);
extern void irq_lapic_timer_handler(void);
extern void irq_sched_handler(void);
extern void load_idt_asm(uint64_t idt_ptr_address);

void set_idt_gate(int vector, uint64_t handler_address);
void init_idt(void);

#endif
