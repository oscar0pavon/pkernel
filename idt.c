#include "idt.h"
#include "console.h"

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

__attribute__((aligned(16))) static struct IdtEntry kernel_idt[256];
static struct IdtPointer idt_ptr;


void set_idt_gate(int vector, uint64_t handler_address) {
  kernel_idt[vector].OffsetLow    = (uint16_t)(handler_address & 0xFFFF);
  kernel_idt[vector].Selector     = 0x08; 
  kernel_idt[vector].Ist          = 0;
  kernel_idt[vector].Attributes   = 0x8E; 
  kernel_idt[vector].OffsetMiddle = (uint16_t)((handler_address >> 16) & 0xFFFF);
  kernel_idt[vector].OffsetHigh   = (uint32_t)((handler_address >> 32) & 0xFFFFFFFF);
  kernel_idt[vector].Reserved     = 0;
}


// The Core Debugger Print Screen
void c_exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {
  printf("\n================================================\n");
  printf("           BARE-METAL KERNEL PANIC          \n");
  printf("================================================\n");
  printf("Exception Vector Detected: %d\n", (int)vector);
  printf("Hardware Error Code:      0x%lx\n", error_code);
  printf("Crashed at Instruction Pointer (RIP): 0x%lx\n", rip);
  
  // If it is a Page Fault, extract the target address the CPU tried to read/write
  if (vector == 14) {
      uint64_t cr2_val;
      __asm__ volatile("mov %%cr2, %0" : "=r"(cr2_val));
      printf("Faulting Memory Address Target (CR2): 0x%lx\n", cr2_val);
  }
  printf("================================================\n");
  printf("System execution securely frozen via HLT.\n");

  // Securely freeze the CPU core to prevent reboots
  while(1) { __asm__ volatile("cli; hlt"); }
}

void init_idt(void) {

  // CPU exceptions
  set_idt_gate(0,  (uint64_t)exception_handler_0);
  set_idt_gate(13, (uint64_t)exception_handler_13);
  set_idt_gate(14, (uint64_t)exception_handler_14);

  // LAPIC spurious vector (must be 0xF0–0xFF per spec; we use 0xFF)
  set_idt_gate(0xFF, (uint64_t)irq_spurious_handler);

  idt_ptr.Limit = (sizeof(struct IdtEntry) * 256) - 1;
  idt_ptr.Base  = (uint64_t)&kernel_idt;

  load_idt_asm((uint64_t)&idt_ptr);

  printf("IDT configured\n");
}
