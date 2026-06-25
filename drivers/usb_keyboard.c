#include "usb_keyboard.h"
#include "usb.h"
#include "xhci.h"
#include "../lapic.h"
#include "../input.h"
#include "../sched.h"

// Per-keyboard state. Indexed 0..num_kbds-1; slot_id is the xHCI slot number.
#define MAX_KBD XHCI_MAX_SLOTS
struct KbdState {
  uint32_t slot_id;
  uint32_t db_target;
  volatile uint8_t report[64];
  uint8_t prev_kc[6];
};
static struct KbdState kbd[MAX_KBD];
static int num_kbds = 0;

// USB HID Usage Page 0x07 keycode → ASCII (indices 0x00–0x38)
static const char kbd_ascii[2][57] = {
    {0,   0,    0,   0,   'a',  'b', 'c',  'd',  'e', 'f', 'g', 'h',
     'i', 'j',  'k', 'l', 'm',  'n', 'o',  'p',  'q', 'r', 's', 't',
     'u', 'v',  'w', 'x', 'y',  'z', '1',  '2',  '3', '4', '5', '6',
     '7', '8',  '9', '0', '\n', 0,   '\b', '\t', ' ', '-', '=', '[',
     ']', '\\', 0,   ';', '\'', '`', ',',  '.',  '/'},
    {0,   0,   0,   0,   'A',  'B', 'C',  'D',  'E', 'F', 'G', 'H',
     'I', 'J', 'K', 'L', 'M',  'N', 'O',  'P',  'Q', 'R', 'S', 'T',
     'U', 'V', 'W', 'X', 'Y',  'Z', '!',  '@',  '#', '$', '%', '^',
     '&', '*', '(', ')', '\n', 0,   '\b', '\t', ' ', '_', '+', '{',
     '}', '|', 0,   ':', '"',  '~', '<',  '>',  '?'},
};

// Decode one 8-byte HID Boot Protocol report for device k and enqueue new keys.
static void xhci_process_key_report(int k) {
  uint8_t modifier = kbd[k].report[0];
  uint8_t shifted  = (modifier & 0x22) ? 1 : 0;  // bit1=LShift, bit5=RShift

  for (int i = 2; i < 8; i++) {
    uint8_t kc = kbd[k].report[i];
    if (kc == 0) continue;

    int held = 0;
    for (int j = 0; j < 6; j++) if (kbd[k].prev_kc[j] == kc) { held = 1; break; }
    if (held) continue;

    if (kc < 57) {
      char c = kbd_ascii[shifted][kc];
      if (c != 0)
        input_putc(c);
    }
  }

  for (int i = 0; i < 6; i++) kbd[k].prev_kc[i] = kbd[k].report[i + 2];
}

// Queue one Normal TRB on EP1 IN for device k and ring its doorbell.
static void xhci_queue_kbd_trb(int k) {
  uint32_t sid = kbd[k].slot_id;
  uint16_t report_sz = ep1_in_mps ? ep1_in_mps : 8;
  if (report_sz > sizeof(kbd[k].report)) report_sz = sizeof(kbd[k].report);

  volatile struct XhciTRB *trb = &ep1in_ring[sid][ep1in_enqueue[sid]];
  trb->Parameter = (uint64_t)kbd[k].report;
  trb->Status    = report_sz;
  // IOC=1 (interrupt on completion), ISP=1 (interrupt on short packet)
  trb->Control   = (TRB_TYPE_NORMAL << 10) | (1U << 5) | (1U << 2) | ep1in_cycle[sid];

  ep1in_enqueue[sid]++;
  if (ep1in_enqueue[sid] >= 255) {
    ep1in_ring[sid][255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep1in_cycle[sid];
    ep1in_cycle[sid] ^= 1;
    ep1in_enqueue[sid] = 0;
  }

  xhci_dev.doorbell_regs[sid] = kbd[k].db_target;
}

// Drain the event ring, process any keyboard reports, and re-arm the endpoint.
// Shared by usb_kbd_isr() (MSI path) and usb_kbd_poll_task() (polling path).
// The caller must guarantee this does not run re-entrantly: the ISR runs with
// RFLAGS.IF cleared (interrupt gate), and the poll task wraps it in cli/sti.
// Events are posted to the event ring by the controller whether or not the MSI
// is delivered, so polling works even when interrupts never arrive (real HW).
static void usb_kbd_service(void) {
  // Clear IMAN.IP (bit 0 = Interrupt Pending; write-1-to-clear)
  xhci_dev.int_0_regs->Iman |= 1;
  // Clear USBSTS.EINT (bit 3; write-1-to-clear)
  xhci_dev.op_regs->UsbSts = (1U << 3);

  // Per-device re-arm flags; re-arm after draining so we replace exactly one
  // consumed TRB per device per drain pass.
  int got_transfer[MAX_KBD] = {0};

  // Drain all pending events from the event ring
  while (1) {
    volatile struct XhciEventTRB *ev = &event_ring[event_ring_dequeue];
    if ((ev->Control & 1) != event_ring_cycle) break;

    uint32_t trb_type        = (ev->Control >> 10) & 0x3F;
    uint32_t completion_code = (ev->Status  >> 24) & 0xFF;
    uint32_t ev_slot         = (ev->Control >> 24) & 0xFF;

    event_ring_dequeue++;
    if (event_ring_dequeue >= 256) {
      event_ring_dequeue = 0;
      event_ring_cycle ^= 1;
    }
    xhci_dev.int_0_regs->Erdp =
      (uint64_t)&event_ring[event_ring_dequeue] | (1U << 3);

    if (trb_type == TRB_TYPE_TRANSFER_EVENT &&
        (completion_code == 1 || completion_code == 13)) {
      for (int k = 0; k < num_kbds; k++) {
        if (kbd[k].slot_id == ev_slot) {
          xhci_process_key_report(k);
          got_transfer[k] = 1;
          break;
        }
      }
    }
  }

  for (int k = 0; k < num_kbds; k++)
    if (got_transfer[k])
      xhci_queue_kbd_trb(k);
}

// C-level ISR called from irq_xhci_handler (idt_asm.s).
// Runs with interrupts disabled (interrupt gate clears RFLAGS.IF on entry).
void usb_kbd_isr(void) {
  usb_kbd_service();
  lapic_eoi();           // acknowledge the interrupt to the LAPIC
}

// Polling fallback: drain the event ring on a timer instead of relying on MSI.
// Real Intel xHCI controllers frequently fail to deliver the MSI to the LAPIC
// (firmware/IOMMU state), so the interrupt-driven path alone leaves the
// keyboard dead even though enumeration succeeded. The controller still posts
// transfer events to the event ring, so polling picks them up regardless.
// cli/sti makes the drain atomic against the MSI ISR if it ever does fire.
static void usb_kbd_poll_task(void) {
  while (1) {
    asm volatile("cli");
    usb_kbd_service();
    asm volatile("sti");
    task_sleep(8);       // ~100 Hz poll; well above the HID report interval
  }
}


// GET_DESCRIPTOR HID Report Descriptor (USB HID spec 7.1.1).
// bmRequestType=0x81 (D-to-H, Standard, Interface), bRequest=GET_DESCRIPTOR,
// wValue=0x2200 (type=HID Report, index=0), wIndex=0, wLength=hid_report_len.
// hid_report_len was captured by the xHCI config-descriptor walk.
static int usb_kbd_get_report_descriptor(uint32_t slot_id) {
  XDBG("=== GET_DESCRIPTOR HID Report (len=%d) ===\n", hid_report_len);

  if (hid_report_len == 0) {
    printf("ERROR: hid_report_len not set (config parse may have failed)\n");
    return 0;
  }

  uint16_t req_len = (hid_report_len > 256) ? 256 : hid_report_len;
  for (uint32_t i = 0; i < 256; i++) descriptor_buffer[i] = 0;

  // bmRequestType=0x81: D-to-H, Standard, Interface recipient.
  // wIndex (bits 32-47) must be the HID interface number, not a hardcoded 0,
  // or a composite device stalls the request.
  uint64_t setup = (uint64_t)0x81
                 | ((uint64_t)USB_REQ_GET_DESCRIPTOR << 8)
                 | ((uint64_t)USB_DESC_HID_REPORT << 24)
                 | ((uint64_t)iface_number << 32)
                 | ((uint64_t)req_len << 48);

  if (!xhci_control_in(slot_id, setup, descriptor_buffer, req_len)) {
    printf("ERROR: GET_DESCRIPTOR HID Report failed\n");
    return 0;
  }

  static const char hex_ch[] = "0123456789abcdef";
  XDBG("HID Report Descriptor (%d bytes):\n", req_len);
  if (xhci_debug) {
    for (uint16_t i = 0; i < req_len; i++) {
      uint8_t b = descriptor_buffer[i];
      char s[4] = {hex_ch[(b >> 4) & 0xF], hex_ch[b & 0xF], ' ', '\0'};
      printf("%s", s);
      if ((i + 1) % 16 == 0) printf("\n");
    }
    if (req_len % 16 != 0) printf("\n");
  }

  return 1;
}

// ============================================================================
// Attach: turn a configured HID device into a live keyboard.
// ============================================================================

// Returns 1 if the HID report descriptor in descriptor_buffer describes a
// keyboard application: Usage Page Generic Desktop (0x01) + Usage Keyboard
// (0x06). A mouse has Usage 0x02 at the same level — this rejects it even
// when the interface declared bInterfaceProtocol = 0x01. If the descriptor
// could not be fetched (got_desc == 0) the caller skips this check and falls
// back to the protocol-byte identification so boot-protocol-only keyboards
// that stall GET_DESCRIPTOR are not incorrectly rejected.
static int hid_desc_is_keyboard(void) {
  uint16_t len = (hid_report_len > 256) ? 256 : hid_report_len;
  uint8_t usage_page = 0;
  uint16_t i = 0;
  while (i < len) {
    uint8_t prefix = descriptor_buffer[i];
    if (prefix == 0xFE) break;          // long item — unsupported, stop
    uint8_t sz   = prefix & 0x03;
    uint8_t type = (prefix >> 2) & 0x03;
    uint8_t tag  = (prefix >> 4) & 0x0F;
    if (sz == 3) sz = 4;                // HID encoding: 3 → 4 data bytes

    uint32_t val = 0;
    for (uint8_t b = 0; b < sz && i + 1 + b < len; b++)
      val |= (uint32_t)descriptor_buffer[i + 1 + b] << (b * 8);

    if (type == 1 && tag == 0)          // Global: Usage Page
      usage_page = (uint8_t)val;
    else if (type == 2 && tag == 0)     // Local: Usage
      if (usage_page == 0x01 && val == 0x06) return 1;

    i += 1 + sz;
  }
  return 0;
}

// Called from usb_attach_device() after xHCI enumeration. Fetches the HID
// report descriptor, then queues the first interrupt-IN TRB. MSI fires when
// the device sends a report; usb_kbd_isr() takes over from there.
// SET_PROTOCOL(Boot): make the HID device emit the standard 8-byte boot report
// regardless of its native (report-protocol) format. Devices enumerate in
// Report Protocol by default, and this keyboard reports protocol 0 with a
// 32-byte endpoint, so force Boot Protocol for a parseable report.
static void usb_kbd_set_boot_protocol(uint32_t slot_id) {
  // bmRequestType=0x21 (H-to-D, Class, Interface), bRequest=SET_PROTOCOL,
  // wValue=0 (Boot), wIndex=interface, wLength=0.
  uint64_t setup = (uint64_t)0x21
                 | ((uint64_t)USB_HID_REQ_SET_PROTOCOL << 8)
                 | ((uint64_t)USB_HID_PROTOCOL_BOOT << 16)
                 | ((uint64_t)iface_number << 32);

  if (!ep0_control_nodata(slot_id, setup))
    printf("WARNING: SET_PROTOCOL(boot) failed; reports may not be 8-byte\n");
  else
    XDBG("SET_PROTOCOL(boot) accepted\n");
}

int usb_kbd_attach(uint32_t slot_id) {
  XDBG("=== Arming keyboard interrupt (slot %d, EP%d IN, MPS=%d) ===\n",
       slot_id, ep1_in_number, ep1_in_mps);

  // Only request Boot Protocol if the interface advertises the Boot subclass
  // (bInterfaceSubClass == 1). Sending SET_PROTOCOL to a non-boot device STALLs
  // and halts EP0, breaking later control transfers.
  if (iface_subclass == 1)
    usb_kbd_set_boot_protocol(slot_id);
  else
    printf("USB: keyboard is non-boot (subclass %d); using native reports\n",
           iface_subclass);

  // Fetch the HID report descriptor. If the fetch succeeds, verify the
  // descriptor declares a keyboard application (Usage Page 0x01 + Usage 0x06).
  // A mouse that also exposes bInterfaceProtocol=0x01 is caught here — its
  // descriptor will have Usage 0x02 (Mouse) instead. If the fetch fails (device
  // stalls) we skip the check so boot-protocol-only keyboards are not rejected.
  int got_desc = usb_kbd_get_report_descriptor(slot_id);
  if (got_desc && !hid_desc_is_keyboard()) {
    printf("USB: HID report descriptor is not a keyboard; skipping slot %d\n",
           slot_id);
    return 0;
  }

  if (num_kbds >= MAX_KBD) {
    printf("USB: too many keyboards; skipping slot %d\n", slot_id);
    return 0;
  }

  int k = num_kbds++;
  kbd[k].slot_id   = slot_id;
  kbd[k].db_target = (uint32_t)ep1_in_number * 2 + 1;

  xhci_queue_kbd_trb(k);

  // One shared poll task services all keyboards; only create it for the first.
  if (k == 0)
    task_create("usbkbd", usb_kbd_poll_task);

  printf("USB: keyboard ready on slot %d (kbd %d)\n", slot_id, k);
  XDBG("=== Keyboard armed — polling + MSI ===\n");
  return 1;
}
