#ifndef __GDT_H__
#define __GDT_H__

#include <stdint.h>

// Structure for the GDT pointer that the CPU instruction reads
struct GdtPointer {
  uint16_t Limit; // Size of the GDT table minus 1
  uint64_t Base;  // Absolute physical address of our GDT array
} __attribute__((packed));

// Structure for a standard 64-bit GDT Segment Entry
struct GdtEntry {
  uint16_t LimitLow;
  uint16_t BaseLow;
  uint8_t BaseMiddle;
  uint8_t Access;
  uint8_t Flags;
  uint8_t BaseHigh;
} __attribute__((packed));

typedef struct GdtEntry GdtEntry;
typedef struct GdtPointer GdtPointer;

// This assembly helper
extern void load_gdt(uint64_t gdt_ptr_address);

// Load the task register with the given GDT selector (implemented in gdt.s).
extern void load_tss(uint16_t selector);

void init_gdt(void);

// Fill the TSS descriptor (GDT entries 5-6) and run ltr. Called from user mode
// setup once the TSS base address is known.
void gdt_install_tss(uint64_t tss_base, uint32_t tss_limit);

#endif
