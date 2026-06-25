#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

// xAPIC registers are memory-mapped at 0xFEE00000 (identity-mapped in kernel_pd3)
#define LAPIC_BASE 0xFEE00000UL

void init_lapic(void);
void lapic_eoi(void);


static inline volatile uint32_t *lapic_reg(uint32_t off) {
  return (volatile uint32_t *)(LAPIC_BASE + off);
}

#endif
