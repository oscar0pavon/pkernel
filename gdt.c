#include <stdint.h>
#include "console.h"
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

// Statically allocate our clean GDT inside our kernel's data space
// We need 5 entries: Null, Kernel Code, Kernel Data, User Code, User Data
__attribute__((aligned(16))) static struct GdtEntry kernel_gdt[5];
static struct GdtPointer gdt_ptr;

// This assembly helper is implemented right below in Step 2
extern void load_gdt_asm(uint64_t gdt_ptr_address);

void init_gdt(void) {
  // 1. Entry 0: The Null Descriptor (Mandatory hardware placeholder)
  kernel_gdt[0] = (struct GdtEntry){0, 0, 0, 0, 0, 0};

  // 2. Entry 1: Kernel Code Segment (Ring 0)
  // Access 0x9A = Present, Ring 0, Executable, Read/Write allowed
  // Flags  0xA0 = Long Mode (64-bit Execution active)
  kernel_gdt[1] = (struct GdtEntry){0, 0, 0, 0x9A, 0xA0, 0};

  // 3. Entry 2: Kernel Data Segment (Ring 0)
  // Access 0x92 = Present, Ring 0, Data Segment, Read/Write allowed
  // Flags  0x00 = Regular 64-bit Data bounds alignment
  kernel_gdt[2] = (struct GdtEntry){0, 0, 0, 0x92, 0x00, 0};

  // 4. Entry 3: User Code Segment (Ring 3 - For apps later!)
  // Access 0xFA = Present, Ring 3, Executable, Read/Write allowed
  kernel_gdt[3] = (struct GdtEntry){0, 0, 0, 0xFA, 0xA0, 0};

  // 5. Entry 4: User Data Segment (Ring 3 - For apps later!)
  // Access 0xF2 = Present, Ring 3, Data Segment, Read/Write allowed
  kernel_gdt[4] = (struct GdtEntry){0, 0, 0, 0xF2, 0x00, 0};

  // 6. Configure the master tracking pointer
  gdt_ptr.Limit = (sizeof(struct GdtEntry) * 5) - 1;
  gdt_ptr.Base = (uint64_t)&kernel_gdt;

  // 7. Pass the pointer to assembly to update the CPU execution states
  load_gdt_asm((uint64_t)&gdt_ptr);

  printf("GDT configured\n");
}
