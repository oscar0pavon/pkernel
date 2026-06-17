#include "lapic_timer.h"
#include "input_output.h"
#include "lapic.h"
#include "console.h"
#include "acpi.h"

#define LAPIC_BASE 0xFEE00000UL

static inline volatile uint32_t *lreg(uint32_t off) {
    return (volatile uint32_t *)(LAPIC_BASE + off);
}

#define LAPIC_LVT_TIMER  lreg(0x320)
#define LAPIC_TIMER_ICR  lreg(0x380)  // Initial Count
#define LAPIC_TIMER_CCR  lreg(0x390)  // Current Count
#define LAPIC_TIMER_DCR  lreg(0x3E0)  // Divide Config

// The ACPI Power Management Timer always runs at this fixed rate.
#define PM_TIMER_FREQ 3579545u
#define CALIBRATE_MS  50
// Bound the calibration spin so a missing/stuck PM timer can't hang the boot.
#define CALIBRATE_SPIN_LIMIT 500000000ULL

static volatile uint64_t ticks = 0;
static uint32_t lapic_timer_hz = 0;

void lapic_timer_init(uint32_t hz) {
    lapic_timer_hz = hz;

    // Divide by 1 for highest resolution
    *LAPIC_TIMER_DCR = 0x0B;

    // Mask timer while calibrating (one-shot, any vector)
    *LAPIC_LVT_TIMER = (1U << 16) | 0x20;

    // Calibrate against the ACPI PM timer: a free-running 3.579545 MHz counter
    // read with a single 32-bit port access.  QEMU/KVM emulate it faithfully,
    // unlike the legacy PIT whose emulated count jitters and tears between reads
    // (every PIT-based attempt under KVM under-measured and stormed the LAPIC).
    uint64_t ticks_per_sec = 0;
    uint16_t pm_port = FADT ? (uint16_t)FADT->PMTimerBlock : 0;

    if (pm_port) {
        // FADT Flags bit 8 (TMR_VAL_EXT): set = 32-bit counter, clear = 24-bit.
        uint32_t pm_mask = (FADT->Flags & (1u << 8)) ? 0xFFFFFFFFu : 0x00FFFFFFu;
        uint32_t target  = PM_TIMER_FREQ / 1000 * CALIBRATE_MS;  // PM ticks in window

        uint32_t pm_start = input(pm_port) & pm_mask;
        *LAPIC_TIMER_ICR = 0xFFFFFFFF;

        uint32_t elapsed_pm = 0;
        uint64_t spins = 0;
        do {
            elapsed_pm = (input(pm_port) - pm_start) & pm_mask;  // masked sub handles wrap
        } while (elapsed_pm < target && ++spins < CALIBRATE_SPIN_LIMIT);

        if (elapsed_pm >= target) {
            uint64_t elapsed_lapic = 0xFFFFFFFFu - *LAPIC_TIMER_CCR;
            // ticks_per_sec = lapic_ticks / (pm_ticks / PM_FREQ)
            ticks_per_sec = elapsed_lapic * PM_TIMER_FREQ / elapsed_pm;
        }
    }

    if (ticks_per_sec == 0) {
        // No usable PM timer — fall back to KVM's fixed 1 GHz APIC clock rather
        // than risk a divide-by-zero or a tiny ICR that storms the CPU.
        ticks_per_sec = 1000000000ULL;
        printf("LAPIC timer: PM-timer calibration unavailable, assuming 1 GHz\n");
    }

    printf("LAPIC timer: ~%d Hz bus clock\n", (uint32_t)ticks_per_sec);

    // Configure periodic mode: vector 0x20, period = ticks_per_sec / hz
    *LAPIC_TIMER_ICR = (uint32_t)(ticks_per_sec / hz);
    *LAPIC_LVT_TIMER = 0x20 | (1U << 17);  // periodic (bit 17), unmasked
}

void lapic_timer_isr(void) {
    ticks++;
    lapic_eoi();
}

uint64_t lapic_timer_get_ticks(void) {
    return ticks;
}

uint64_t lapic_timer_uptime_ms(void) {
    return (lapic_timer_hz > 0) ? (ticks * 1000ULL / lapic_timer_hz) : 0;
}
