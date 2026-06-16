#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

void init_lapic(void);
void lapic_eoi(void);

#endif
