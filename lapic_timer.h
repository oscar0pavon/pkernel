#ifndef LAPIC_TIMER_H
#define LAPIC_TIMER_H

#include <stdint.h>

// Calibrate using PIT polling, then start periodic LAPIC timer at `hz` Hz.
void lapic_timer_init(uint32_t hz);

void     lapic_timer_isr(void);
uint64_t lapic_timer_get_ticks(void);
uint64_t lapic_timer_uptime_ms(void);

// Busy-wait `ms` milliseconds via the ACPI PM timer; safe before interrupts.
void busy_wait_ms(uint32_t ms);

#endif
