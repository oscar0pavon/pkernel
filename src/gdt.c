#include "gdt.h"

#include "console.h"

// Statically allocate our clean GDT inside our kernel's data space
// We need 5 entries: Null, Kernel Code, Kernel Data, User Code, User Data
__attribute__((aligned(16))) static GdtEntry kernel_gdt[5];

static GdtPointer gdt_ptr;


void init_gdt(void) {
  // Entry 0: The Null Descriptor (Mandatory hardware placeholder)
  kernel_gdt[0] = (GdtEntry){0, 0, 0, 0, 0, 0};

  // Entry 1: Kernel Code Segment (Ring 0)
  // Access 0x9A = Present, Ring 0, Executable, Read/Write allowed
  // Flags  0xA0 = Long Mode (64-bit Execution active)
  kernel_gdt[1] = (GdtEntry){0, 0, 0, 0x9A, 0xA0, 0};

  // Entry 2: Kernel Data Segment (Ring 0)
  // Access 0x92 = Present, Ring 0, Data Segment, Read/Write allowed
  // Flags  0x00 = Regular 64-bit Data bounds alignment
  kernel_gdt[2] = (GdtEntry){0, 0, 0, 0x92, 0x00, 0};

  // Entry 3: User Code Segment (Ring 3 - For apps later!)
  // Access 0xFA = Present, Ring 3, Executable, Read/Write allowed
  kernel_gdt[3] = (GdtEntry){0, 0, 0, 0xFA, 0xA0, 0};

  //  Entry 4: User Data Segment (Ring 3 - For apps later!)
  // Access 0xF2 = Present, Ring 3, Data Segment, Read/Write allowed
  kernel_gdt[4] = (GdtEntry){0, 0, 0, 0xF2, 0x00, 0};

  // Configure the master tracking pointer
  gdt_ptr.Limit = (sizeof(GdtEntry) * 5) - 1;
  gdt_ptr.Base = (uint64_t)&kernel_gdt;

  // Pass the pointer to assembly to update the CPU execution states
  load_gdt((uint64_t)&gdt_ptr);

  printf("GDT configured\n");
}
