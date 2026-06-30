#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include "acpi.h"

#define MAX_CPUS 64
extern int cpu_count;
extern uint8_t cpu_apic_ids[MAX_CPUS];


void get_cpus(struct MADT *madt);


#endif
