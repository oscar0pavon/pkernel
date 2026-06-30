#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void init_paging(void);

// Identity-map an MMIO region (PCI BAR) found at runtime, after CR3 is loaded.
void paging_map_mmio(uint64_t phys, uint64_t size);

// Reload CR3 to flush the TLB (implemented in paging_asm.s).
extern void flush_tlb(void);

extern void update_cr3(uint64_t pml4);

void test_identity_mapping(void);

#endif

