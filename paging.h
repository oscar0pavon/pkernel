#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void init_paging(void);

// Identity-map an MMIO region (PCI BAR) found at runtime, after CR3 is loaded.
void paging_map_mmio(uint64_t phys, uint64_t size);

void test_identity_mapping(void);

#endif

