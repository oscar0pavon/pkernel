#include "lapic.h"
#include "console.h"
#include "input_output.h"
#include <stdint.h>


#define LAPIC_ID  lapic_reg(0x020)
#define LAPIC_SVR lapic_reg(0x0F0)  // Spurious Interrupt Vector Register
#define LAPIC_EOI lapic_reg(0x0B0)  // End of Interrupt

void init_lapic(void) {
  // Mask all legacy 8259 PIC interrupts so they don't fire on our vectors.
  // This is required even when using MSI — a spurious PIC IRQ would hit
  // vector 0x07 or 0x0F and fault since those entries are unhandled.
  output_byte(0xFF, 0xA1);  // slave  PIC: mask all 8 lines
  output_byte(0xFF, 0x21);  // master PIC: mask all 8 lines

  // Enable the xAPIC and route spurious interrupts to vector 0xFF.
  // Bit 8 of SVR is the software-enable bit; bits 7:0 are the spurious vector
  // (must be 0xF0–0xFF per the Intel SDM).
  *LAPIC_SVR = (1U << 8) | 0xFF;

  printf("LAPIC enabled (ID=0x%x)\n", *LAPIC_ID >> 24);
}

// Write to EOI register to acknowledge a non-spurious interrupt.
// Call this at the end of every ISR before IRETQ.
void lapic_eoi(void) {
  *LAPIC_EOI = 0;
}
