#include "lapic_timer.h"
#include "input_output.h"
#include "lapic.h"
#include "console.h"

#define LAPIC_BASE 0xFEE00000UL

static inline volatile uint32_t *lreg(uint32_t off) {
    return (volatile uint32_t *)(LAPIC_BASE + off);
}

#define LAPIC_LVT_TIMER  lreg(0x320)
#define LAPIC_TIMER_ICR  lreg(0x380)  // Initial Count
#define LAPIC_TIMER_CCR  lreg(0x390)  // Current Count
#define LAPIC_TIMER_DCR  lreg(0x3E0)  // Divide Config

#define PIT_CHANNEL0  0x40
#define PIT_COMMAND   0x43

// ~10ms at 1.193182 MHz PIT input clock
#define CALIBRATE_PIT_TICKS 11932

static volatile uint64_t ticks = 0;
static uint32_t lapic_timer_hz = 0;

// Program PIT channel 0, mode 2 (rate generator), for a single calibration
// period, then spin until one full period elapses.  In mode 2 the count runs
// CALIBRATE_PIT_TICKS → ... → 2 → 1 → CALIBRATE_PIT_TICKS → ...  Detecting
// cur > prev reliably catches exactly one wrap (the 1→max jump).
static void pit_wait_10ms(void) {
    output_byte(0x34, PIT_COMMAND);  // ch0, lohi, mode 2, binary
    output_byte(CALIBRATE_PIT_TICKS & 0xFF, PIT_CHANNEL0);
    output_byte(CALIBRATE_PIT_TICKS >> 8,   PIT_CHANNEL0);

    uint16_t prev = CALIBRATE_PIT_TICKS;
    while (1) {
        output_byte(0x00, PIT_COMMAND);  // latch channel 0
        uint8_t lo = input_byte(PIT_CHANNEL0);
        uint8_t hi = input_byte(PIT_CHANNEL0);
        uint16_t cur = ((uint16_t)hi << 8) | lo;
        if (cur > prev) break;
        prev = cur;
    }
}

void lapic_timer_init(uint32_t hz) {
    lapic_timer_hz = hz;

    // Divide by 1 for highest resolution
    *LAPIC_TIMER_DCR = 0x0B;

    // Mask timer while calibrating (one-shot, any vector)
    *LAPIC_LVT_TIMER = (1U << 16) | 0x20;

    // Start counting down from max
    *LAPIC_TIMER_ICR = 0xFFFFFFFF;

    // Wait exactly one PIT period (~10ms)
    pit_wait_10ms();

    uint32_t remaining  = *LAPIC_TIMER_CCR;
    uint32_t elapsed_10ms = 0xFFFFFFFF - remaining;
    uint32_t ticks_per_sec = elapsed_10ms * 100;  // 10ms → 1s

    printf("LAPIC timer: ~%d Hz bus clock\n", ticks_per_sec);

    // Configure periodic mode: vector 0x20, period = ticks_per_sec / hz
    *LAPIC_TIMER_ICR = ticks_per_sec / hz;
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
