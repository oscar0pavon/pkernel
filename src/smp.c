#include "smp.h"

#include "cpu.h"
#include "lapic.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "memory.h"
#include "library.h"
#include "lapic_timer.h"
#include "console.h"

// Preferred low page for the trampoline. Any 4 KB page below 1 MB works — the
// trampoline is position independent (it derives its own base from CS) — so if
// firmware marks this one unusable we scan for another. SIPI carries a "vector"
// that the CPU shifts left by 12 to form the start address (page = vector<<12),
// so the base must be 4 KB aligned and its vector fits in a byte.
#define TRAMPOLINE_PREF 0x8000UL

// Chosen at runtime by smp_init(); the base every AP starts executing from.
static uint64_t trampoline_base;

// Pick a usable low page for the trampoline, preferring TRAMPOLINE_PREF. Scans
// vectors 0x01..0x9F (skipping page 0 and the 0xA0000+ video/ROM hole) and asks
// the bootloader memory map whether each candidate is conventional RAM.
static uint64_t pick_trampoline_page(void) {
  if (pmm_phys_usable(TRAMPOLINE_PREF))
    return TRAMPOLINE_PREF;
  for (uint64_t vec = 1; vec < 0xA0; vec++) {
    uint64_t pa = vec << 12;
    if (pmm_phys_usable(pa))
      return pa;
  }
  return 0;
}

// LAPIC registers used for cross-processor signalling.
#define LAPIC_ID_REG   lapic_reg(0x020)
#define LAPIC_ICR_LOW  lapic_reg(0x300) // Interrupt Command Register, low dword
#define LAPIC_ICR_HIGH lapic_reg(0x310) // ICR high dword (destination field)

// Delivery-mode encodings for the ICR (bits 8-10) with assert + edge.
#define ICR_INIT   0x00004500u          // delivery mode 0b101 (INIT)
#define ICR_STARTUP(vec) (0x00004600u | (vec)) // 0b110 (Startup) | vector

// Symbols exported by src/asm/smp_trampoline.s. Declared as arrays so that
// subtracting ap_trampoline_start yields the byte offset of a patch slot inside
// the copied blob (the same at the link address and at the chosen load base).
extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_tramp_cr3[];
extern uint8_t ap_tramp_stack[];
extern uint8_t ap_tramp_entry[];

volatile int cpus_online = 1;           // the BSP is already running

// Set by the freshly-started AP once it has switched to its own stack, so the
// BSP knows it is safe to overwrite the shared stack slot for the next core.
static volatile int ap_ready;

// Send an inter-processor interrupt to a single physical APIC ID and wait for
// the LAPIC to report delivery complete (ICR low bit 12 = Delivery Status).
static void lapic_send_ipi(uint8_t apic_id, uint32_t command) {
  *LAPIC_ICR_HIGH = (uint32_t)apic_id << 24; // destination in bits 24-31
  *LAPIC_ICR_LOW = command;                  // writing low dword fires the IPI
  while (*LAPIC_ICR_LOW & (1u << 12))
    __asm__ volatile("pause");
}

// Absolute address of a trampoline patch slot, at the chosen load base.
static void *tramp_slot(uint8_t *sym) {
  return (void *)(trampoline_base + (uint64_t)(sym - ap_trampoline_start));
}

// Every AP arrives here in long mode, on its own stack, still using the
// trampoline's throwaway GDT and a null IDT.
void ap_main(void) {
  gdt_load_on_ap(); // switch to the real kernel GDT (CS=0x08)
  idt_load_on_ap(); // share the BSP's exception/IRQ vectors
  init_lapic();     // software-enable this core's local APIC

  cpus_online++;
  ap_ready = 1;     // release the BSP to start the next core

  __asm__ volatile("sti");
  // No per-CPU scheduler yet — park the core. See smp.c notes for what wiring
  // this into sched.c requires.
  for (;;)
    __asm__ volatile("hlt");
}

void smp_init(void) {
  // The trampoline must sit below 1 MB (SIPI can only reach vector<<12), a
  // region pmm_alloc_page() never allocates. Pick a page firmware reports as
  // usable RAM — on some platforms low memory is firmware-reserved, and
  // clobbering it would be silent corruption.
  trampoline_base = pick_trampoline_page();
  if (!trampoline_base) {
    printf("SMP: no usable low page for the trampoline, skipping AP startup\n");
    return;
  }
  uint8_t sipi_vector = (uint8_t)(trampoline_base >> 12);

  // 1. Copy the trampoline into the chosen low page.
  memcpy((void *)trampoline_base, ap_trampoline_start,
         (uint64_t)(ap_trampoline_end - ap_trampoline_start));

  // 2. Patch the constants shared by every AP: the kernel page tables and the
  //    long-mode entry point.
  *(volatile uint64_t *)tramp_slot(ap_tramp_cr3) = read_cr3();
  *(volatile uint64_t *)tramp_slot(ap_tramp_entry) = (uint64_t)ap_main;

  uint8_t bsp_id = (uint8_t)(*LAPIC_ID_REG >> 24);

  // 3. Walk every enumerated CPU and start the ones that aren't us.
  for (int i = 0; i < cpu_count; i++) {
    uint8_t id = cpu_apic_ids[i];
    if (id == bsp_id)
      continue;

    void *stack = pmm_alloc_page();
    if (!stack) {
      printf("SMP: out of memory starting APIC %d\n", id);
      continue;
    }
    // Hand this AP the TOP of its 4 KB stack (stacks grow down).
    *(volatile uint64_t *)tramp_slot(ap_tramp_stack) =
        (uint64_t)stack + 4096;

    ap_ready = 0;

    // INIT-SIPI-SIPI: assert INIT, wait ~10 ms, then two Startup IPIs.
    lapic_send_ipi(id, ICR_INIT);
    busy_wait_ms(10);
    lapic_send_ipi(id, ICR_STARTUP(sipi_vector));
    busy_wait_ms(1); // spec calls for ~200 us; 1 ms is comfortably safe
    lapic_send_ipi(id, ICR_STARTUP(sipi_vector));

    // Wait for the AP to take its stack before we reuse the shared slot.
    for (int spin = 0; spin < 100000000 && !ap_ready; spin++)
      __asm__ volatile("pause");

    if (!ap_ready)
      printf("SMP: APIC %d failed to come online\n", id);
  }

  printf("SMP: %d CPU(s) online\n", cpus_online);
}
