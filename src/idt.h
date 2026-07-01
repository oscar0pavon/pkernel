#ifndef IDT_H
#define IDT_H

#include "types.h"

// IDT entry structure explicitly matching 64-bit hardware specs
struct IdtEntry {
  uint16_t OffsetLow;
  uint16_t Selector;  // GDT Kernel Code offset (0x08)
  uint8_t Ist;        // Interrupt Stack Table index (0)
  uint8_t Attributes; // Type flags (0x8E = Present, Ring 0, Interrupt Gate)
  uint16_t OffsetMiddle;
  uint32_t OffsetHigh;
  uint32_t Reserved;
} __attribute__((packed));

struct IdtPointer {
  uint16_t Limit;
  uint64_t Base;
} __attribute__((packed));

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

// Load the kernel IDT on the calling CPU (used by APs during SMP bring-up).
void idt_load_on_ap(void);

#endif
