#include "console.h"
#include "framebuffer.h"
#include "memory.h"
#include "paging.h"
#include <stdint.h>

// Intel CPU
// PML4 - Page Map Level 4                512 GB
// PDPT - Page Directory Pointer Table      1 Gb
// PD - Page Directory                      2 MB
// PT - Page Table                          4 kb

// Allocate 4KB blocks in the .bss section for our tables.
// Each directory holds exactly 512 entries (512 * 8 bytes = 4096 bytes).
aligned4k static uint64_t kernel_pml4[512];
aligned4k static uint64_t kernel_pdpt[512];
aligned4k static uint64_t kernel_pd0[512]; // Maps 0GB - 1GB
aligned4k static uint64_t kernel_pd1[512]; // Maps 1GB - 2GB
aligned4k static uint64_t kernel_pd2[512]; // Maps 2GB - 3GB
aligned4k static uint64_t kernel_pd3[512]; // Maps 3GB - 4GB

// Page table entry bitmask flags defined by x86-64 hardware architecture
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_READWRITE (1ULL << 1)
#define PAGE_HUGE (1ULL << 7) // Tells the CPU to treat this entry as a 2MB page

#define PAGE_2MB 0x200000ULL
#define PAGE_ADDR_MASK ~0xFFFULL

// On a 2 MB page the PAT-select bit is bit 12 (bit 7 is the huge-page bit).
// With PCD=PWT=0 this selects PAT slot 4, which we program to write-combining.
#define PAGE_PAT_2MB (1ULL << 12)

#define IA32_PAT 0x277  // MSR holding the 8 Page Attribute Table entries

// Reprogram PAT slot 4 to Write-Combining (type 0x01). Default slot 4 is WB.
// WC selected via PAT takes precedence over the firmware MTRRs, so a framebuffer
// page tagged with PAGE_PAT_2MB becomes WC regardless of how UEFI set the MTRRs.
static void setup_pat_wc(void) {
  uint64_t pat = read_msr(IA32_PAT);
  pat &= ~(0xFFULL << 32);  // clear slot 4 (bits 39:32)
  pat |= (0x01ULL << 32);   // slot 4 = WC
  write_msr(IA32_PAT, pat);
}

// Return (or allocate) the PDPT for a given PML4 slot.
// PML4[0] is pre-wired to kernel_pdpt in init_paging; other slots are
// allocated on demand so BARs above 512 GB (e.g. at 0x380000000000 with
// -smp on QEMU) can be mapped without a page fault.
static uint64_t *paging_get_pdpt(uint64_t pml4_index) {
  if (!(kernel_pml4[pml4_index] & PAGE_PRESENT)) {
    uint64_t *pdpt = (uint64_t *)pmm_alloc_page();
    for (int i = 0; i < 512; i++)
      pdpt[i] = 0;
    kernel_pml4[pml4_index] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_READWRITE;
  }
  return (uint64_t *)(kernel_pml4[pml4_index] & PAGE_ADDR_MASK);
}

// Return (or allocate) the PD for a given PML4/PDPT slot pair.
static uint64_t *paging_get_pd(uint64_t pml4_index, uint64_t pdpt_index) {
  uint64_t *pdpt = paging_get_pdpt(pml4_index);
  if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
    uint64_t *pd = (uint64_t *)pmm_alloc_page();
    for (int i = 0; i < 512; i++)
      pd[i] = 0;
    pdpt[pdpt_index] = (uint64_t)pd | PAGE_PRESENT | PAGE_READWRITE;
  }
  return (uint64_t *)(pdpt[pdpt_index] & PAGE_ADDR_MASK);
}

// How a virtual address is sliced

// A 48-bit virtual address splits into five fields:

// 47        39  38        30  29       21  20      12   11       0
// +------------+------------+------------+------------+------------+
// | PML4 index | PDPT index |  PD index  |  PT index  |   offset   |
// |   9 bits   |   9 bits   |   9 bits   |   9 bits   |  12 bits   |
// +------------+------------+------------+------------+------------+

// Identity-map [phys, phys+size) using 2 MB huge pages. Safe to call over a
// region that is already partly mapped; it just rewrites those entries.
static void identity_map_region(uint64_t phys, uint64_t size,
                                uint64_t extra_flags) {
  uint64_t start = phys & ~(PAGE_2MB - 1);
  uint64_t end = (phys + size + PAGE_2MB - 1) & ~(PAGE_2MB - 1);

  for (uint64_t addr = start; addr < end; addr += PAGE_2MB) {
    uint64_t pml4_index = (addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (addr >> 30) & 0x1FF;
    uint64_t pd_index = (addr >> 21) & 0x1FF;

    uint64_t *pd = paging_get_pd(pml4_index, pdpt_index);
    pd[pd_index] = addr | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE | extra_flags;
  }
}

// Identity-map an MMIO region (e.g. a PCI BAR) discovered at runtime, after
// init_paging has already loaded CR3. Device BARs on real hardware land at
// physical addresses far outside the low 4 GB we map up front (the xHCI BAR is
// commonly hundreds of GB up), so the driver must map its own window before
// touching it. Reloads CR3 to drop any cached not-present paging entries.
void paging_map_mmio(uint64_t phys, uint64_t size) {
  identity_map_region(phys, size, 0);
  flush_tlb();  // drop stale not-present TLB entries for the region just mapped
}

void init_paging(void) {

  // Program PAT slot 4 to write-combining before any entry references it.
  setup_pat_wc();

  // Link PML4 Entry 0 to our Page Descriptor Pointer Table (PDPT)
  kernel_pml4[0] = (uint64_t)kernel_pdpt | PAGE_PRESENT | PAGE_READWRITE;

  // Link the first 4 entries of the PDPT to our 4 individual Page
  // Directories
  kernel_pdpt[0] = (uint64_t)kernel_pd0 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[1] = (uint64_t)kernel_pd1 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[2] = (uint64_t)kernel_pd2 | PAGE_PRESENT | PAGE_READWRITE;
  kernel_pdpt[3] = (uint64_t)kernel_pd3 | PAGE_PRESENT | PAGE_READWRITE;

  // Fill the directories to identity-map the lower 4GB using fast 2MB huge
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

    // Directory 3: 3GB to 4GB
    kernel_pd3[i] =
        (0xC0000000ULL + chunk_2mb) | PAGE_PRESENT | PAGE_READWRITE | PAGE_HUGE;
  }

  // 4b. The GOP framebuffer base is a raw physical address chosen by the
  // firmware. On real hardware it frequently sits ABOVE 4 GB, outside the
  // windows mapped above, so the first printf after the CR3 load would fault
  // and triple-fault the machine. QEMU keeps it low, which is why this only
  // breaks on metal. Map its actual physical range explicitly.
  // Map the framebuffer write-combining (PAT slot 4) so pixel writes burst to
  // VRAM instead of crawling as uncached single-dword bus transactions. This
  // also re-tags the low-4 GB huge pages that already cover it on QEMU.
  uint64_t fb_base = frame_buffer.vram;
  uint64_t fb_size = (uint64_t)frame_buffer.pixel_per_scan_line *
                     frame_buffer.vertical_resolution * 4;
  if (fb_base && fb_size)
    identity_map_region(fb_base, fb_size, PAGE_PAT_2MB);

  // 5. Load your new unrestricted page tables directly into the CPU!
  // Writing to CR3 forces the processor to instantly ditch UEFI's zombie maps.
  uint64_t pml4_addr = (uint64_t)kernel_pml4;
  update_cr3(pml4_addr);

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
