#include "gdt.h"

#include "console.h"

// Statically allocate our clean GDT inside our kernel's data space.
// Entries 0-4: Null, Kernel Code, Kernel Data, User Code, User Data.
// Entries 5-6: the Task State Segment descriptor — in long mode a system
// descriptor is 16 bytes, so it occupies TWO 8-byte GDT slots. It is filled in
// later by gdt_install_tss() once user mode knows the TSS address.
#define GDT_ENTRIES 7
__attribute__((aligned(16))) static GdtEntry kernel_gdt[GDT_ENTRIES];

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

  // Entries 3-4 are ordered user-DATA then user-CODE on purpose: the `sysret`
  // instruction computes the ring-3 selectors from IA32_STAR[63:48] as
  // SS = base+8 and CS = base+16, so the data segment must sit immediately
  // above kernel data and the code segment above that. Swapping them breaks
  // sysret. (Selectors: user data = 0x18|3 = 0x1B, user code = 0x20|3 = 0x23.)

  // Entry 3: User Data Segment (Ring 3)
  // Access 0xF2 = Present, Ring 3, Data Segment, Read/Write allowed
  kernel_gdt[3] = (GdtEntry){0, 0, 0, 0xF2, 0x00, 0};

  // Entry 4: User Code Segment (Ring 3)
  // Access 0xFA = Present, Ring 3, Executable, Read/Write allowed
  // Flags  0xA0 = Long Mode (64-bit Execution active)
  kernel_gdt[4] = (GdtEntry){0, 0, 0, 0xFA, 0xA0, 0};

  // Entries 5-6: TSS descriptor slots, left zero until gdt_install_tss().
  kernel_gdt[5] = (GdtEntry){0, 0, 0, 0, 0, 0};
  kernel_gdt[6] = (GdtEntry){0, 0, 0, 0, 0, 0};

  // Configure the master tracking pointer
  gdt_ptr.Limit = (sizeof(GdtEntry) * GDT_ENTRIES) - 1;
  gdt_ptr.Base = (uint64_t)&kernel_gdt;

  // Pass the pointer to assembly to update the CPU execution states
  load_gdt((uint64_t)&gdt_ptr);

  printf("GDT configured\n");
}

// Load the already-built kernel GDT on the calling CPU. APs come up under the
// trampoline's throwaway GDT (CS=0x18); this switches them onto the real one
// (CS=0x08) that the BSP set up in init_gdt().
void gdt_load_on_ap(void) {
  load_gdt((uint64_t)&gdt_ptr);
}

// Write the 64-bit TSS system descriptor into GDT entries 5-6 and load it into
// the CPU's task register (ltr). Called from usermode_init() once the TSS
// address is known. The descriptor spans 16 bytes: the low 8 use the normal
// segment-descriptor field layout, the high 8 hold bits 63:32 of the base.
void gdt_install_tss(uint64_t tss_base, uint32_t tss_limit) {
  // Low half — same field order as a code/data descriptor.
  //   Access 0x89 = Present, DPL 0, type 0x9 (available 64-bit TSS).
  kernel_gdt[5].LimitLow   = (uint16_t)(tss_limit & 0xFFFF);
  kernel_gdt[5].BaseLow    = (uint16_t)(tss_base & 0xFFFF);
  kernel_gdt[5].BaseMiddle = (uint8_t)((tss_base >> 16) & 0xFF);
  kernel_gdt[5].Access     = 0x89;
  kernel_gdt[5].Flags      = (uint8_t)((tss_limit >> 16) & 0x0F);
  kernel_gdt[5].BaseHigh   = (uint8_t)((tss_base >> 24) & 0xFF);

  // High half — bits 63:32 of the base in the first 4 bytes, rest reserved 0.
  uint32_t *high = (uint32_t *)&kernel_gdt[6];
  high[0] = (uint32_t)(tss_base >> 32);
  high[1] = 0;

  // Selector = entry 5 * 8 = 0x28 (RPL 0).
  load_tss(0x28);
}
