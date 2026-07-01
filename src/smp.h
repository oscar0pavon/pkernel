#ifndef SMP_H
#define SMP_H

// Bring up all Application Processors (every enumerated CPU except the BSP)
// via the LAPIC INIT-SIPI-SIPI sequence and the low-memory trampoline.
// Requires the LAPIC, paging, GDT and IDT to already be initialised, and
// cpu_apic_ids[] to have been filled by get_cpus() during ACPI setup.
void smp_init(void);

// Long-mode entry point every AP lands on once the trampoline finishes.
void ap_main(void);

// Number of cores currently running (starts at 1 for the BSP).
extern volatile int cpus_online;

#endif
