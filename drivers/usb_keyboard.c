#include "usb_keyboard.h"
#include "xhci.h"
#include "../lapic.h"
#include "../input.h"

// Saved by xhci_arm_keyboard(), consumed by xhci_keyboard_isr()
static uint32_t kbd_slot_id  = 0;
static uint32_t kbd_db_target = 0;

// USB HID Usage Page 0x07 keycode → ASCII (indices 0x00–0x38)
static const char kbd_ascii[2][57] = {
  { 0,   0,   0,   0,   'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 0, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/' },
  { 0,   0,   0,   0,   'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0, '\b', '\t', ' ', '_', '+', '{', '}', '|',  0, ':', '"',  '~', '<', '>', '?' },
};

static volatile uint8_t kbd_report[8] = {0};
static uint8_t kbd_prev_kc[6] = {0};

// Decode one 8-byte HID Boot Protocol report and enqueue changed keys.
static void xhci_process_key_report(void) {
  uint8_t modifier = kbd_report[0];
  uint8_t shifted  = (modifier & 0x22) ? 1 : 0;  // bit1=LShift, bit5=RShift

  for (int i = 2; i < 8; i++) {
    uint8_t kc = kbd_report[i];
    if (kc == 0) continue;

    int held = 0;
    for (int j = 0; j < 6; j++) if (kbd_prev_kc[j] == kc) { held = 1; break; }
    if (held) continue;

    if (kc < 57) {
      char c = kbd_ascii[shifted][kc];
      if (c != 0)
        input_putc(c);
    }
  }

  for (int i = 0; i < 6; i++) kbd_prev_kc[i] = kbd_report[i + 2];
}
// Queue one Normal TRB on EP1 IN and ring the doorbell.
// Called once from xhci_arm_keyboard() to prime the pipeline, then again
// at the end of every xhci_keyboard_isr() to re-arm for the next report.
static void xhci_queue_kbd_trb(void) {
  uint16_t report_sz = ep1_in_mps ? ep1_in_mps : 8;

  volatile struct XhciTRB *trb = &ep1in_ring[ep1in_enqueue];
  trb->Parameter = (uint64_t)kbd_report;
  trb->Status    = report_sz;
  // IOC=1 (interrupt on completion), ISP=1 (interrupt on short packet)
  trb->Control   = (TRB_TYPE_NORMAL << 10) | (1U << 5) | (1U << 2) | ep1in_cycle;

  ep1in_enqueue++;
  if (ep1in_enqueue >= 255) {
    ep1in_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep1in_cycle;
    ep1in_cycle ^= 1;
    ep1in_enqueue = 0;
  }

  xhci_dev.doorbell_regs[kbd_slot_id] = kbd_db_target;
}

// C-level ISR called from irq_xhci_handler (idt_asm.s).
// Runs with interrupts disabled (interrupt gate clears RFLAGS.IF on entry).
void xhci_keyboard_isr(void) {
  // Clear IMAN.IP (bit 0 = Interrupt Pending; write-1-to-clear)
  xhci_dev.int_0_regs->Iman |= 1;
  // Clear USBSTS.EINT (bit 3; write-1-to-clear)
  xhci_dev.op_regs->UsbSts = (1U << 3);

  int got_transfer = 0;

  // Drain all pending events from the event ring
  while (1) {
    volatile struct XhciEventTRB *ev = &event_ring[event_ring_dequeue];
    if ((ev->Control & 1) != event_ring_cycle) break;

    uint32_t trb_type        = (ev->Control >> 10) & 0x3F;
    uint32_t completion_code = (ev->Status  >> 24) & 0xFF;

    event_ring_dequeue++;
    if (event_ring_dequeue >= 256) {
      event_ring_dequeue = 0;
      event_ring_cycle ^= 1;
    }
    xhci_dev.int_0_regs->Erdp =
      (uint64_t)&event_ring[event_ring_dequeue] | (1U << 3);

    if (trb_type == TRB_TYPE_TRANSFER_EVENT &&
        (completion_code == 1 || completion_code == 13)) {
      xhci_process_key_report();
      got_transfer = 1;
    }
  }

  // Re-arm only after a real HID report arrived; spurious/port-status
  // events do not consume a TRB so there is nothing to replace.
  if (got_transfer)
    xhci_queue_kbd_trb();

  // Acknowledge the interrupt to the LAPIC
  lapic_eoi();
}


// ============================================================================
// STEP 14: Interrupt-driven keyboard via xHCI MSI
// ============================================================================
//
// Queue the first TRB and return. MSI will fire when the device sends a report.
void xhci_arm_keyboard(uint32_t slot_id) {
  XDBG("=== STEP 14: Arming keyboard interrupt (slot %d, EP%d IN, MPS=%d) ===\n",
       slot_id, ep1_in_number, ep1_in_mps);

  kbd_slot_id   = slot_id;
  kbd_db_target = (uint32_t)ep1_in_number * 2 + 1;

  xhci_queue_kbd_trb();

  XDBG("=== STEP 14: Keyboard armed — waiting for MSI ===\n");
}
