#include "cpu.h"
#include "console.h"

int cpu_count = 0;
uint8_t cpu_apic_ids[MAX_CPUS];



void get_cpus(struct MADT *madt) {

  uint8_t *p = (uint8_t *)madt + 44; // skip fixed MADT header (36 + 4 + 4)
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  while (p < end) {
    uint8_t type = p[0];
    uint8_t len  = p[1];

    if (type == 0) { // Processor Local APIC
      struct ProcessorLocalAPIC *lapic = (struct ProcessorLocalAPIC *)p;
      if (lapic->flags & 1) { // bit 0: processor enabled
        if (cpu_count < MAX_CPUS)
          cpu_apic_ids[cpu_count++] = lapic->APIC_ID;
      }
    }

    p += len;
  }

  printf("CPUs detected: %d\n", cpu_count);
  for (int i = 0; i < cpu_count; i++){
    int k = 0;
    for( ; k < 4; k++){
      printf("CPU %d: APIC ID %d | ", i+k, cpu_apic_ids[i+k]);
    }
    printf("\n");
    i +=4;
  }



}
