#include "xhci.h"
#include <stdint.h>
#include <string.h>
#include "../memory.h"
#include "../lapic_timer.h"

// ============================================================================
// DMA-coherent data structures (page-aligned for xHCI DMA requirements)
// ============================================================================

aligned4k volatile uint64_t dcbaap[64] = {0};
aligned4k volatile XhciTRB command_ring[256] = {0};
aligned4k volatile XhciEventTRB event_ring[256] = {0};
// ERST requires 64-byte alignment; page-aligning it caused address aliasing
// with event_ring on some controllers.
aligned64 volatile EventRingSegmentEntry erst = {0};

aligned4k volatile XhciInputContext  input_ctx  = {0};
aligned4k volatile XhciDeviceContext device_ctx = {0};
aligned4k volatile XhciTRB           ep0_ring[256] = {0};
// DMA receive buffer for USB descriptors (256 bytes covers any standard descriptor)
aligned4k volatile uint8_t descriptor_buffer[256] = {0};
// EP1 IN transfer ring
aligned4k volatile XhciTRB ep1in_ring[256] = {0};

#define XHCI_MAX_SCRATCHPAD 256
aligned4k volatile uint64_t scratchpad_array[XHCI_MAX_SCRATCHPAD] = {0};
aligned4k volatile uint8_t  scratchpad_pages[XHCI_MAX_SCRATCHPAD][4096] = {0};

XHCIDevice xhci_dev = {0};

uint32_t command_ring_enqueue = 0;
uint32_t command_ring_cycle   = 1;
uint32_t event_ring_dequeue   = 0;
uint32_t event_ring_cycle     = 1;
uint32_t ep0_enqueue = 0;
uint32_t ep0_cycle   = 1;
uint32_t ep1in_enqueue = 0;
uint32_t ep1in_cycle   = 1;

// Device state captured across enumeration steps
static uint32_t dev_speed = 0;
static uint32_t dev_port  = 0;

static uint8_t iface_class    = 0;
uint8_t        iface_subclass = 0;
static uint8_t iface_protocol = 0;
uint8_t iface_number = 0;

uint8_t  ep1_in_addr     = 0;
uint16_t ep1_in_mps      = 8;
uint8_t  ep1_in_interval = 10;
uint8_t  ep1_in_number   = 1;
uint16_t hid_report_len  = 0;

// ============================================================================
// Internal helpers
// ============================================================================

// Dequeue one event TRB if one is available (cycle bit matches). Fills
// *type/*code/*slot and advances the dequeue pointer + updates ERDP.
// Returns 1 on success, 0 if the ring is empty.
static int xhci_dequeue_event(uint32_t *type, uint32_t *code, uint32_t *slot) {
  volatile struct XhciEventTRB *ev = &event_ring[event_ring_dequeue];
  if ((ev->Control & 1) != event_ring_cycle) return 0;
  *type = (ev->Control >> 10) & 0x3F;
  *code = (ev->Status  >> 24) & 0xFF;
  *slot = (ev->Control >> 24) & 0xFF;
  if (++event_ring_dequeue >= 256) { event_ring_dequeue = 0; event_ring_cycle ^= 1; }
  xhci_dev.int_0_regs->Erdp = (uint64_t)&event_ring[event_ring_dequeue] | (1U << 3);
  return 1;
}

// Advance ep0_ring past slot ep0_enqueue-1, wrapping at the link TRB (slot 255).
static inline void ep0_advance(void) {
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1;
    ep0_enqueue = 0;
  }
}

// ============================================================================
// Reset controller (xHCI spec 4.2.1)
// ============================================================================
void xhci_reset_controller(void) {
  xhci_dev.op_regs->UsbCmd |= (1 << 1);  // HCRST
  uint32_t timeout = 1000000;
  while ((xhci_dev.op_regs->UsbCmd & (1 << 1)) && timeout--) __asm__("pause");
  if (!timeout) { printf("ERROR: HCRST did not clear!\n"); return; }

  timeout = 1000000;
  while ((xhci_dev.op_regs->UsbSts & (1 << 11)) && timeout--) __asm__("pause");  // CNR
  if (!timeout) { printf("ERROR: CNR did not clear!\n"); return; }

  xhci_dev.op_regs->UsbSts = 0xFFFFFFFF;  // clear all RW1C status bits
}

// ============================================================================
// Setup command ring (xHCI spec 4.6.2.1)
// ============================================================================
void xhci_setup_command_ring(void) {
  command_ring[255].Parameter = (uint64_t)&command_ring[0];
  command_ring[255].Status    = 0;
  command_ring[255].Control   = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
  xhci_dev.op_regs->Crcr     = (uint64_t)&command_ring[0] | 1;
}

// ============================================================================
// Setup event ring and ERST (xHCI spec 4.9.4)
// ============================================================================
void xhci_setup_event_ring(void) {
  uint64_t rt_base = xhci_dev.base_mmio + xhci_dev.cap_regs->Rtsoff;
  volatile struct XhciInterrupterRegs *int_0 =
      (volatile XhciInterrupterRegs *)(rt_base + 0x20);

  xhci_dev.runtime_regs = (XhciRuntimeRegs *)rt_base;
  xhci_dev.int_0_regs   = int_0;

  erst.RingSegmentBaseAddress = (uint64_t)&event_ring[0];
  erst.RingSegmentSize        = 256;
  erst.Reserved               = 0;

  int_0->Erstsz = 1;
  int_0->Erstba = (uint64_t)&erst;
  int_0->Erdp   = ((uint64_t)&event_ring[0] & ~0xFULL) | (1U << 3);
  int_0->Iman   = 0;
}

// ============================================================================
// Configure slot count, scratchpad, DCBAAP, and start the controller (spec 4.2.2-3)
// ============================================================================
void xhci_start_controller(void) {
  uint32_t max_slots      = xhci_dev.cap_regs->HcsParams1 & 0xFF;
  uint32_t max_dcbaa_slots = (sizeof(dcbaap) / sizeof(dcbaap[0])) - 1;
  uint32_t slots_en       = (max_slots > max_dcbaa_slots) ? max_dcbaa_slots : max_slots;
  if (!slots_en) slots_en = 1;
  xhci_dev.op_regs->Config = slots_en;

  uint32_t hcs2       = xhci_dev.cap_regs->HcsParams2;
  uint32_t max_scratch = (((hcs2 >> 27) & 0x1F) << 5) | ((hcs2 >> 21) & 0x1F);
  if (max_scratch > XHCI_MAX_SCRATCHPAD) {
    printf("ERROR: Controller needs %d scratchpad bufs (max supported: %d)\n",
           max_scratch, XHCI_MAX_SCRATCHPAD);
    return;
  }
  if (max_scratch > 0) {
    for (uint32_t i = 0; i < max_scratch; i++)
      scratchpad_array[i] = (uint64_t)&scratchpad_pages[i][0];
    dcbaap[0] = (uint64_t)&scratchpad_array[0];
  }
  xhci_dev.op_regs->Dcbaap = (uint64_t)&dcbaap[0];

  xhci_dev.op_regs->UsbCmd |= (1 << 0);  // RS
  uint32_t timeout = 100000;
  while ((xhci_dev.op_regs->UsbSts & (1 << 0)) && timeout--) __asm__("pause");
  if (!timeout)
    printf("ERROR: Controller failed to start (USBSTS=0x%x)\n",
           xhci_dev.op_regs->UsbSts);
}

// ============================================================================
// Port scan: power, debounce, reset, enumerate connected ports (spec 4.19)
// ============================================================================
void xhci_scan_ports(void) {
  uint32_t max_ports = xhci_dev.max_ports;

  // Assert Port Power on every port. Preserve RW1C bits (PED bit 1, change
  // bits 17..23) to avoid inadvertently disabling a port or wiping status.
  for (uint32_t port = 0; port < max_ports; port++) {
    uint32_t portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
    if (!(portsc & (1U << 9)))
      xhci_dev.op_regs->PortRegisterSet[port].PortSc =
          (portsc & ~((1u << 1) | (0x7Fu << 17))) | (1u << 9);
  }

  // Allow time for power to stabilise and CCS to assert (USB2 spec debounce).
  busy_wait_ms(120);

  for (uint32_t port = 0; port < max_ports; port++) {
    uint32_t portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
    if (!(portsc & (1 << 0))) continue;  // CCS=0, nothing connected

    // Reset port (PR bit 4); mask change bits so we don't RW1C them.
    xhci_dev.op_regs->PortRegisterSet[port].PortSc =
        (portsc | (1 << 4)) & ~(0x0F << 17);

    uint32_t reset_timeout = 100000;
    while ((xhci_dev.op_regs->PortRegisterSet[port].PortSc & (1 << 4)) &&
           reset_timeout--) __asm__("pause");

    portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
    if (!(portsc & (1 << 1))) {
      printf("WARNING: Port %d not enabled after reset (PORTSC=0x%x)\n",
             port + 1, portsc);
      continue;
    }

    xhci_enable_slot(port);
    if (xhci_dev.device_attached) break;
  }
}

// ============================================================================
// Submit a command TRB and ring the command doorbell (xHCI spec 4.6).
// slot_id is placed in Control[31:24]; pass 0 for commands that don't use it.
// ============================================================================
void xhci_send_command(uint32_t trb_type, uint64_t parameter, uint32_t slot_id) {
  volatile struct XhciTRB *trb = &command_ring[command_ring_enqueue];
  trb->Parameter = parameter;
  trb->Status    = 0;
  trb->Control   = (trb_type << 10) | ((uint32_t)slot_id << 24) | command_ring_cycle;

  if (++command_ring_enqueue == 255) {
    command_ring[255].Control = (TRB_TYPE_LINK << 10) | (1 << 1) | command_ring_cycle;
    command_ring_cycle ^= 1;
    command_ring_enqueue = 0;
  }
  xhci_dev.doorbell_regs[0] = 0;
}

// ============================================================================
// Enable Slot (xHCI spec 4.3.2)
// ============================================================================
void xhci_enable_slot(uint32_t port) {
  xhci_send_command(TRB_TYPE_ENABLE_SLOT, 0, 0);
  uint32_t slot_id = xhci_poll_event_ring();
  if (!slot_id) { printf("ERROR: Enable Slot failed on port %d\n", port + 1); return; }
  if (!xhci_address_device(slot_id, port))
    xhci_disable_slot(slot_id);
}

// ============================================================================
// Disable Slot (xHCI spec 4.6.4): release slot back to the controller.
// ============================================================================
void xhci_disable_slot(uint32_t slot_id) {
  xhci_send_command(TRB_TYPE_DISABLE_SLOT, 0, slot_id);
  xhci_poll_event_ring();
  dcbaap[slot_id] = 0;
}

// ============================================================================
// Poll event ring for a Command Completion Event (xHCI spec 4.11.5.1).
// Returns slot_id on success (code 1), 0 on error or timeout.
// ============================================================================
uint32_t xhci_poll_event_ring(void) {
  uint32_t timeout = 2000000;
  while (timeout--) {
    uint32_t type, code, slot;
    if (xhci_dequeue_event(&type, &code, &slot)) {
      if (type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
        if (code != 1) { printf("ERROR: Command completion code %d\n", code); return 0; }
        return slot;
      }
      // Non-command events (e.g. port status change): discard and keep polling.
    }
    __asm__("pause");
  }
  printf("ERROR: Event ring timed out (USBSTS=0x%x)\n", xhci_dev.op_regs->UsbSts);
  return 0;
}

// ============================================================================
// Poll event ring for a Transfer Event on EP0 (xHCI spec 4.11.5.1).
// Returns 1 on Success (code 1) or Short Packet (code 13), 0 on error/timeout.
// ============================================================================
uint32_t xhci_poll_transfer_event(void) {
  uint32_t timeout = 2000000;
  while (timeout--) {
    uint32_t type, code, slot;
    if (xhci_dequeue_event(&type, &code, &slot)) {
      if (type == TRB_TYPE_TRANSFER_EVENT) {
        if (code == 1 || code == 13) return 1;
        printf("ERROR: Transfer completion code %d\n", code);
        return 0;
      }
    }
    __asm__("pause");
  }
  printf("ERROR: Transfer event timed out (USBSTS=0x%x)\n", xhci_dev.op_regs->UsbSts);
  return 0;
}

// ============================================================================
// EP0 control IN transfer: Setup + Data + Status TRBs (xHCI spec 4.11.2.2).
// Returns 1 on success, 0 on failure.
// ============================================================================
uint32_t xhci_control_in(uint32_t slot_id, uint64_t setup,
                         volatile uint8_t *buf, uint16_t length) {
  volatile struct XhciTRB *s = &ep0_ring[ep0_enqueue++];
  s->Parameter = setup;
  s->Status    = 8;
  s->Control   = (TRB_TYPE_SETUP_STAGE << 10) | (3U << 16) | (1U << 6) | ep0_cycle;
  ep0_advance();

  volatile struct XhciTRB *d = &ep0_ring[ep0_enqueue++];
  d->Parameter = (uint64_t)buf;
  d->Status    = length;
  d->Control   = (TRB_TYPE_DATA_STAGE << 10) | (1U << 16) | ep0_cycle;
  ep0_advance();

  volatile struct XhciTRB *t = &ep0_ring[ep0_enqueue++];
  t->Parameter = 0;
  t->Status    = 0;
  t->Control   = (TRB_TYPE_STATUS_STAGE << 10) | (1U << 5) | ep0_cycle;
  ep0_advance();

  xhci_dev.doorbell_regs[slot_id] = 1;
  return xhci_poll_transfer_event();
}

// ============================================================================
// EP0 control transfer with no data stage (e.g. SET_PROTOCOL, SET_CONFIGURATION).
// Status Stage is IN (DIR=1) per xHCI spec 4.11.2.2.
// ============================================================================
uint32_t ep0_control_nodata(uint32_t slot_id, uint64_t setup) {
  volatile struct XhciTRB *s = &ep0_ring[ep0_enqueue++];
  s->Parameter = setup;
  s->Status    = 8;
  s->Control   = (TRB_TYPE_SETUP_STAGE << 10) | (0U << 16) | (1U << 6) | ep0_cycle;
  ep0_advance();

  volatile struct XhciTRB *t = &ep0_ring[ep0_enqueue++];
  t->Parameter = 0;
  t->Status    = 0;
  t->Control   = (TRB_TYPE_STATUS_STAGE << 10) | (1U << 16) | (1U << 5) | ep0_cycle;
  ep0_advance();

  xhci_dev.doorbell_regs[slot_id] = 1;
  return xhci_poll_transfer_event();
}

// ============================================================================
// Address Device (xHCI spec 4.3.3 / 4.6.5):
// Build Input Context, issue Address Device, then enumerate the device fully.
// ============================================================================
int xhci_address_device(uint32_t slot_id, uint32_t port) {
  uint32_t portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
  uint8_t  speed  = (portsc >> 10) & 0x0F;
  dev_speed = speed;
  dev_port  = port;

  printf("USB: port %d slot %d speed %d\n", port + 1, slot_id, speed);

  // Initial EP0 MPS from speed (spec 4.8.2.1): 1=Full 2=Low 3=High 4=SuperSpeed
  uint16_t mps;
  switch (speed) {
    case 3:  mps = 64;  break;
    case 4:  mps = 512; break;
    case 2:  mps = 8;   break;
    default: mps = 8;   break;
  }

  memset((void *)ep0_ring, 0, sizeof(ep0_ring));
  ep0_ring[255].Parameter = (uint64_t)&ep0_ring[0];
  ep0_ring[255].Control   = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
  ep0_enqueue = 0;
  ep0_cycle   = 1;

  memset((void *)&input_ctx, 0, sizeof(input_ctx));
  input_ctx.ctrl.add_flags = (1U << 0) | (1U << 1);
  input_ctx.slot.dw0 = ((uint32_t)speed << 20) | (1U << 27);
  input_ctx.slot.dw1 = (uint32_t)(port + 1) << 16;
  input_ctx.ep[0].dw1 = (3U << 1) | (XHCI_EP_TYPE_CONTROL << 3) | ((uint32_t)mps << 16);
  uint64_t ep0_addr = (uint64_t)&ep0_ring[0];
  input_ctx.ep[0].dw2 = (uint32_t)(ep0_addr & ~0xFULL) | 1;
  input_ctx.ep[0].dw3 = (uint32_t)(ep0_addr >> 32);
  input_ctx.ep[0].dw4 = 8;

  dcbaap[slot_id] = (uint64_t)&device_ctx;

  xhci_send_command(TRB_TYPE_ADDRESS_DEVICE, (uint64_t)&input_ctx, slot_id);
  if (!xhci_poll_event_ring()) {
    printf("ERROR: Address Device failed on slot %d\n", slot_id);
    return 0;
  }

  // Fetch 8 bytes first — guaranteed to fit a default-MPS packet. Requesting 18
  // upfront babbles on full-speed devices whose EP0 is 8-byte wide.
  if (!xhci_get_descriptor(slot_id, USB_DESC_DEVICE, 8)) return 0;

  // Update EP0 MPS if device reports a different value. SuperSpeed encodes this
  // as an exponent, already correct, so skip it there.
  uint8_t reported_mps = descriptor_buffer[7];
  if (speed != 4 && reported_mps != mps)
    if (!xhci_evaluate_context(slot_id, reported_mps)) return 0;

  if (!xhci_get_descriptor(slot_id, USB_DESC_DEVICE, 18)) return 0;
  if (!xhci_get_config_descriptor(slot_id)) return 0;

  uint8_t config_val = descriptor_buffer[5];  // bConfigurationValue
  if (!xhci_set_configuration(slot_id, config_val)) return 0;

  int kept = usb_attach_device(slot_id, iface_class, iface_subclass, iface_protocol);
  xhci_dev.device_attached = kept;
  return kept;
}

// ============================================================================
// GET_DESCRIPTOR via EP0 (xHCI spec 4.11.2.2)
// ============================================================================
int xhci_get_descriptor(uint32_t slot_id, uint8_t desc_type, uint16_t length) {
  uint64_t setup = (uint64_t)0x80
                 | ((uint64_t)0x06      << 8)
                 | ((uint64_t)desc_type << 24)
                 | ((uint64_t)length    << 48);
  memset((void *)descriptor_buffer, 0, length);
  if (!xhci_control_in(slot_id, setup, descriptor_buffer, length)) {
    printf("ERROR: GET_DESCRIPTOR failed\n");
    return 0;
  }
  return 1;
}

// ============================================================================
// Evaluate Context: update EP0 MPS after Address Device (xHCI spec 4.3.3)
// ============================================================================
int xhci_evaluate_context(uint32_t slot_id, uint8_t new_mps) {
  memset((void *)&input_ctx, 0, sizeof(input_ctx));
  input_ctx.ctrl.add_flags = (1U << 1);  // A1 = EP0 only; slot not evaluated
  input_ctx.ep[0].dw1 = (3U << 1) | (XHCI_EP_TYPE_CONTROL << 3) | ((uint32_t)new_mps << 16);

  xhci_send_command(TRB_TYPE_EVALUATE_CONTEXT, (uint64_t)&input_ctx, slot_id);
  if (!xhci_poll_event_ring()) {
    printf("ERROR: Evaluate Context failed\n");
    return 0;
  }
  return 1;
}

// ============================================================================
// GET_DESCRIPTOR (Configuration): two-phase fetch + sub-descriptor walk.
// Phase 1 reads 9 bytes to learn wTotalLength; Phase 2 fetches the full tree.
// Parses Interface, HID, and Endpoint descriptors to populate iface_* / ep1_in_*.
// ============================================================================
int xhci_get_config_descriptor(uint32_t slot_id) {
  // Phase 1: 9-byte header to read wTotalLength
  uint64_t setup9 = (uint64_t)0x80
                  | ((uint64_t)0x06 << 8)
                  | ((uint64_t)USB_DESC_CONFIG << 24)
                  | ((uint64_t)9 << 48);
  memset((void *)descriptor_buffer, 0, 9);
  if (!xhci_control_in(slot_id, setup9, descriptor_buffer, 9)) {
    printf("ERROR: GET_DESCRIPTOR Config (9 bytes) failed\n");
    return 0;
  }

  uint16_t total_length = (uint16_t)descriptor_buffer[2]
                        | ((uint16_t)descriptor_buffer[3] << 8);
  if (total_length < 9 || total_length > 255) {
    printf("ERROR: Invalid wTotalLength %d\n", total_length);
    return 0;
  }

  // Phase 2: full descriptor tree
  uint64_t setup_full = (uint64_t)0x80
                      | ((uint64_t)0x06 << 8)
                      | ((uint64_t)USB_DESC_CONFIG << 24)
                      | ((uint64_t)total_length << 48);
  memset((void *)descriptor_buffer, 0, total_length);
  if (!xhci_control_in(slot_id, setup_full, descriptor_buffer, total_length)) {
    printf("ERROR: GET_DESCRIPTOR Config (full) failed\n");
    return 0;
  }

  // Walk sub-descriptors. A composite device exposes multiple HID interfaces
  // (boot keyboard, media keys, NKRO). Prefer bInterfaceProtocol=1 (boot
  // keyboard) which carries ordinary keystrokes; fall back to any HID iface.
  // Previously keeping the last interface seen left us polling a non-typing
  // endpoint (protocol 0) on composite devices.
  uint32_t offset    = descriptor_buffer[0];  // skip Config Descriptor itself
  int      locked_kbd = 0;
  int      have_iface = 0;
  int      capturing  = 0;

  while (offset + 1 < total_length) {
    uint8_t bLength = descriptor_buffer[offset];
    uint8_t bType   = descriptor_buffer[offset + 1];
    if (bLength < 2 || offset + bLength > total_length) break;

    if (bType == USB_DESC_INTERFACE) {
      uint8_t num   = descriptor_buffer[offset + 2];
      uint8_t cls   = descriptor_buffer[offset + 5];
      uint8_t sub   = descriptor_buffer[offset + 6];
      uint8_t proto = descriptor_buffer[offset + 7];

      printf("USB: iface %d class 0x%x sub 0x%x proto 0x%x\n",
             num, cls, sub, proto);

      int is_hid = (cls == USB_CLASS_HID);
      int is_kbd = (is_hid && proto == USB_HID_PROTOCOL_KBD);
      if ((is_kbd && !locked_kbd) || (is_hid && !have_iface)) {
        iface_number   = num;
        iface_class    = cls;
        iface_subclass = sub;
        iface_protocol = proto;
        have_iface     = 1;
        capturing      = 1;
        ep1_in_number  = 0;  // recapture this interface's IN endpoint
        if (is_kbd) locked_kbd = 1;
      } else {
        capturing = 0;
      }
    } else if (bType == USB_DESC_HID && capturing) {
      hid_report_len = (uint16_t)descriptor_buffer[offset + 7]
                     | ((uint16_t)descriptor_buffer[offset + 8] << 8);
    } else if (bType == USB_DESC_ENDPOINT) {
      uint8_t  addr  = descriptor_buffer[offset + 2];
      uint8_t  attrs = descriptor_buffer[offset + 3];
      uint16_t mps   = (uint16_t)descriptor_buffer[offset + 4]
                     | ((uint16_t)descriptor_buffer[offset + 5] << 8);
      // First interrupt IN endpoint of the bound interface.
      if (capturing && (addr & USB_EP_DIR_IN) && (attrs & 0x3) == 3 &&
          ep1_in_number == 0) {
        ep1_in_addr     = addr;
        ep1_in_mps      = mps;
        ep1_in_interval = descriptor_buffer[offset + 6];
        ep1_in_number   = addr & 0x0F;
      }
    }
    offset += bLength;
  }
  return 1;
}

// ============================================================================
// SET_CONFIGURATION + Configure Endpoint (xHCI spec 4.3.4 / 4.6.6)
// ============================================================================
int xhci_set_configuration(uint32_t slot_id, uint8_t config_val) {
  uint64_t setup = ((uint64_t)0x09 << 8) | ((uint64_t)config_val << 16);
  if (!ep0_control_nodata(slot_id, setup)) {
    printf("ERROR: SET_CONFIGURATION failed\n");
    return 0;
  }

  uint32_t ep_ctx_idx = (uint32_t)(ep1_in_number * 2 + 1);

  memset((void *)ep1in_ring, 0, sizeof(ep1in_ring));
  ep1in_ring[255].Parameter = (uint64_t)&ep1in_ring[0];
  ep1in_ring[255].Control   = (TRB_TYPE_LINK << 10) | (1U << 1) | 1;
  ep1in_enqueue = 0;
  ep1in_cycle   = 1;

  // Convert USB bInterval → xHCI Interval (power-of-2 in 125 μs microframes)
  uint8_t xhci_interval;
  if (dev_speed == 3 || dev_speed == 4) {
    xhci_interval = (ep1_in_interval > 0) ? ep1_in_interval - 1 : 0;
  } else {
    // Full/Low Speed: bInterval in ms → microframes (×8) → floor(log2)
    uint32_t uf = (uint32_t)ep1_in_interval * 8;
    xhci_interval = 0;
    while (uf > 1 && xhci_interval < 15) { uf >>= 1; xhci_interval++; }
  }

  printf("USB: slot %d class 0x%x proto 0x%x ep%d mps %d intv %d->%d\n",
         slot_id, iface_class, iface_protocol, ep1_in_number,
         ep1_in_mps, ep1_in_interval, xhci_interval);

  // The interrupt endpoint context must be placed at its real DCI, not a fixed
  // slot — e.g. EP2-IN lives at DCI 5, EP3-IN at DCI 7. memset first, then
  // grab the pointer so it targets the right slot after the memset.
  memset((void *)&input_ctx, 0, sizeof(input_ctx));
  volatile struct XhciEndpointContext *epc = &input_ctx.ep[ep_ctx_idx - 1];
  input_ctx.ctrl.add_flags = (1U << 0) | (1U << ep_ctx_idx);
  input_ctx.slot.dw0 = ((uint32_t)dev_speed << 20) | (ep_ctx_idx << 27);
  input_ctx.slot.dw1 = (uint32_t)(dev_port + 1) << 16;
  epc->dw0 = (uint32_t)xhci_interval << 16;
  epc->dw1 = (3U << 1) | (XHCI_EP_TYPE_INTERRUPT_IN << 3) | ((uint32_t)ep1_in_mps << 16);
  uint64_t ep1in_addr = (uint64_t)&ep1in_ring[0];
  epc->dw2 = (uint32_t)(ep1in_addr & ~0xFULL) | 1;  // DCS=1
  epc->dw3 = (uint32_t)(ep1in_addr >> 32);
  epc->dw4 = ep1_in_mps;  // AvgTRBLength = MPS for interrupt

  xhci_send_command(TRB_TYPE_CONFIGURE_ENDPOINT, (uint64_t)&input_ctx, slot_id);
  if (!xhci_poll_event_ring()) {
    printf("ERROR: Configure Endpoint command failed\n");
    return 0;
  }
  return 1;
}

// ============================================================================
// MSI/MSI-X interrupt configuration
// ============================================================================

// Configure the MSI-X capability (cap id 0x11) at cap_ptr to deliver vector.
// MSI-X table entry 0 lives in a BAR pointed to by the BIR field.
static int xhci_setup_msix(volatile uint32_t *pci, uint8_t cap_ptr, uint8_t vector) {
  uint32_t cap_dw    = pci[cap_ptr / 4];
  uint32_t table_reg = pci[(cap_ptr + 4) / 4];
  uint8_t  bir       = table_reg & 0x7;
  uint32_t table_off = table_reg & ~0x7U;

  uint64_t bar_base;
  if (bir == 0) {
    bar_base = xhci_dev.base_mmio;
  } else {
    uint32_t lo = pci[(0x10 + bir * 4) / 4] & 0xFFFFFFF0U;
    uint32_t hi = pci[(0x10 + bir * 4 + 4) / 4];
    bar_base = (uint64_t)lo | ((uint64_t)hi << 32);
  }

  // Entry 0: addr=LAPIC BSP physical, data=vector, unmasked
  volatile uint32_t *entry = (volatile uint32_t *)(bar_base + table_off);
  entry[0] = 0xFEE00000U;
  entry[1] = 0;
  entry[2] = (uint32_t)vector;
  entry[3] = 0;

  pci[cap_ptr / 4] = (cap_dw | (1U << 31)) & ~(1U << 30);  // Enable, clear Function Mask
  return 1;
}

// Configure the MSI capability (cap id 0x05) at cap_ptr to deliver vector.
// Unlike MSI-X the address/data are inline in PCI config space; the data
// offset shifts by 4 bytes on 64-bit capable controllers.
static int xhci_setup_msi(volatile uint32_t *pci, uint8_t cap_ptr, uint8_t vector) {
  uint32_t cap_dw  = pci[cap_ptr / 4];
  uint16_t msg_ctl = (cap_dw >> 16) & 0xFFFF;
  int      is_64   = (msg_ctl >> 7) & 1;

  pci[(cap_ptr + 0x04) / 4] = 0xFEE00000U;
  if (is_64) {
    pci[(cap_ptr + 0x08) / 4] = 0;
    pci[(cap_ptr + 0x0C) / 4] = vector;
  } else {
    pci[(cap_ptr + 0x08) / 4] = vector;
  }

  // Enable (bit 0), force Multiple Message Enable to 0 (one vector allocated).
  msg_ctl = (msg_ctl & ~(0x7U << 4)) | 1U;
  pci[cap_ptr / 4] = (cap_dw & 0x0000FFFFU) | ((uint32_t)msg_ctl << 16);
  return 1;
}

// Walk the PCI capability list, configure interrupt delivery, then enable the
// xHCI interrupter and USBCMD.INTE. Prefers MSI-X (0x11), falls back to MSI (0x05).
void xhci_enable_msi(uint8_t vector) {
  volatile uint32_t *pci = xhci_dev.pci_regs;
  if (!pci) { printf("ERROR: xHCI PCI regs not set\n"); return; }

  pci[1] |= (1U << 1) | (1U << 2);  // Memory Space + Bus Mastering

  uint8_t cap_ptr  = (uint8_t)(pci[0x34 / 4] & 0xFF);
  uint8_t msix_ptr = 0, msi_ptr = 0;
  while (cap_ptr >= 0x40) {
    uint32_t cap_dw = pci[cap_ptr / 4];
    uint8_t  cap_id = cap_dw & 0xFF;
    if      (cap_id == 0x11) msix_ptr = cap_ptr;
    else if (cap_id == 0x05) msi_ptr  = cap_ptr;
    cap_ptr = (cap_dw >> 8) & 0xFF;
  }

  int ok = 0;
  if      (msix_ptr) ok = xhci_setup_msix(pci, msix_ptr, vector);
  else if (msi_ptr)  ok = xhci_setup_msi(pci, msi_ptr, vector);
  else { printf("ERROR: neither MSI-X nor MSI capability found\n"); return; }
  if (!ok) return;

  xhci_dev.int_0_regs->Iman  = 0x3;       // IE=1, clear pending IP
  xhci_dev.op_regs->UsbCmd  |= (1U << 2); // INTE
}

// ============================================================================
// Driver entry point
// ============================================================================
void init_xhci_driver(void) {
  printf("xHCI Driver - MMIO Base: 0x%lx\n", xhci_dev.base_mmio);

  xhci_dev.device_attached  = 0;
  xhci_dev.cap_regs          = (XhciCapabilityRegs *)xhci_dev.base_mmio;
  uint64_t op_base           = xhci_dev.base_mmio + xhci_dev.cap_regs->CapLength;
  xhci_dev.op_regs           = (XhciOperationalRegs *)op_base;
  xhci_dev.max_ports         = (xhci_dev.cap_regs->HcsParams1 >> 24) & 0xFF;
  xhci_dev.doorbell_regs     = (volatile uint32_t *)(xhci_dev.base_mmio +
                                xhci_dev.cap_regs->Dboff);

  xhci_reset_controller();
  xhci_setup_command_ring();
  xhci_setup_event_ring();
  xhci_start_controller();
  xhci_scan_ports();
}
