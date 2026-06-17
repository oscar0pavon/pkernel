#include "console.h"
#include "memory.h"
#include <stdint.h>

// Page table entry bitmask flags defined by x86-64 hardware architecture
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_READWRITE (1ULL << 1)
#define PAGE_HUGE (1ULL << 7) // Tells the CPU to treat this entry as a 2MB page

// Allocate 4KB blocks in the .bss section for our tables.
// Each directory holds exactly 512 entries (512 * 8 bytes = 4096 bytes).
aligned4k static uint64_t kernel_pml4[512];
aligned4k static uint64_t kernel_pdpt[512];
aligned4k static uint64_t kernel_pd0[512]; // Maps 0GB - 1GB
aligned4k static uint64_t kernel_pd1[512]; // Maps 1GB - 2GB
aligned4k static uint64_t kernel_pd2[512]; // Maps 2GB - 3GB
aligned4k static uint64_t kernel_pd3[512]; // Maps 3GB - 4GB (Includes your
aligned4k static uint64_t kernel_pd_xhci[512];// high xHCI MMIO / ACPI zones)


void init_kernel_paging(void) {
  // 1. Zero out the tables completely to wipe any random RAM junk bytes
  for (int i = 0; i < 512; i++) {
    kernel_pml4[i] = 0;
    kernel_pdpt[i] = 0;
    kernel_pd0[i] = 0;
    kernel_pd1[i] = 0;
    kernel_pd2[i] = 0;
    kernel_pd3[i] = 0;
    kernel_pd_xhci[i] = 0;
  }

  // 2. Link PML4 Entry 0 to our Page Descriptor Pointer Table (PDPT)
  kernel_pml4[0] = (uint64_t)kernel_pdpt | PAGE_PRESENT | PAGE_READWRITE;

  // 3. Link the first 4 entries of the PDPT to our 4 individual Page
  // Directories
  kernel_pdpt[0] = (uint64_t)kernel_pd0 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[1] = (uint64_t)kernel_pd1 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[2] = (uint64_t)kernel_pd2 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[3] = (uint64_t)kernel_pd3 | PAGE_PRESENT | PAGE_READWRITE;

  kernel_pdpt[32] = (uint64_t)&kernel_pd_xhci | PAGE_PRESENT | PAGE_READWRITE;

  // 4. Fill the directories to identity-map the lower 4GB using fast 2MB huge
  // pages
  for (uint64_t i = 0; i < 512; i++) {
    // 512 entries * 2MB = 1GB per directory table block
    uint64_t chunk_2mb = i * 0x200000; 

    // Directory 0: 0GB to 1GB (Maps your kernel code space and stack zones!)
    kernel_pd0[i] =
        (0x00000000ULL + chunk_2mb) | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;

    // Directory 1: 1GB to 2GB
    kernel_pd1[i] =
        (0x40000000ULL + chunk_2mb) | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;

    // Directory 2: 2GB to 3GB
    kernel_pd2[i] =
        (0x80000000ULL + chunk_2mb) | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;

    // Directory 3: 3GB to 4GB (Maps your PCI configuration spaces and high MMIO
    // bars!)
    kernel_pd3[i] =
        (0xC0000000ULL + chunk_2mb) | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;

    kernel_pd_xhci[i] = (0x800000000ULL + chunk_2mb) 
      | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;
  }

  // 5. Load your new unrestricted page tables directly into the CPU!
  // Writing to CR3 forces the processor to instantly ditch UEFI's zombie maps.
  uint64_t pml4_addr = (uint64_t)kernel_pml4;
  asm volatile("mov %0, %%cr3" : : "r"(pml4_addr) : "memory");

  printf("Paging enabled\n");
}

void test_identity_mapping(void) {
  // 1. Declare a volatile local variable on the stack
  volatile uint64_t magic_probe = 0xDEADC0DECAFEFEEDULL;

  // 2. Get the Virtual Address of this variable (what the C code sees)
  uint64_t virtual_address = (uint64_t)&magic_probe;

  // 3. Create a pointer using a completely raw integer address
  // If it's a true identity map, reading from the raw integer value
  // MUST return our exact same stack value!
  volatile uint64_t *physical_probe = (volatile uint64_t *)virtual_address;

  printf("\n--- IDENTITY MAP VERIFICATION ---\n");
  printf("Virtual Pointer Address:  0x%lx\n", virtual_address);
  printf("Value via direct variable: 0x%lx\n", magic_probe);
  printf("Value via raw int pointer: 0x%lx\n", *physical_probe);

  if (*physical_probe == 0xDEADC0DECAFEFEEDULL) {
    printf("SUCCESS: Virtual Address maps 1-to-1 with Physical RAM!\n");
  } else {
    printf("ERROR: Address mismatch! Paging table layout is skewed.\n");
  }
  printf("---------------------------------\n");
}
