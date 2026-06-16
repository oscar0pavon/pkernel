#include "xhci.h"
#include "../console.h"
#include "../input.h"
#include "../lapic.h"
#include <stdint.h>
#include <string.h>

#define aligned4k __attribute__((aligned(4096)))

// ============================================================================
// DATA STRUCTURES - All page-aligned for DMA
// ============================================================================

aligned4k volatile uint64_t dcbaap[64] = {0};
aligned4k volatile struct XhciTRB command_ring[256] = {0};
aligned4k volatile struct XhciEventTRB event_ring[256] = {0};
// xHCI spec requires 64-byte alignment for ERST, not page alignment.
// Using aligned4k caused erst to share the same 4K address as event_ring.
__attribute__((aligned(64))) volatile struct EventRingSegmentEntry erst = {0};

// Address Device buffers (one slot supported for now)
aligned4k volatile struct XhciInputContext  input_ctx  = {0};
aligned4k volatile struct XhciDeviceContext device_ctx = {0};
aligned4k volatile struct XhciTRB           ep0_ring[256] = {0};

// DMA receive buffer for USB descriptors (256 bytes covers any standard descriptor)
aligned4k volatile uint8_t descriptor_buffer[256] = {0};

// EP1 IN transfer ring (used from Step 12 onward)
aligned4k volatile struct XhciTRB ep1in_ring[256] = {0};

XHCIDevice xhci_dev = {0};

// Tracking state for command/event rings
uint32_t command_ring_enqueue = 0;
uint32_t command_ring_cycle = 1;

uint32_t event_ring_dequeue = 0;
uint32_t event_ring_cycle = 1;

uint32_t ep0_enqueue = 0;
uint32_t ep0_cycle   = 1;

uint32_t ep1in_enqueue = 0;
uint32_t ep1in_cycle   = 1;

// Device info saved across enumeration steps
static uint32_t dev_speed = 0;
static uint32_t dev_port  = 0;

// Endpoint info populated by Step 11, consumed by Steps 12–14
uint8_t  ep1_in_addr     = 0;
uint16_t ep1_in_mps      = 8;
uint8_t  ep1_in_interval = 10;
uint8_t  ep1_in_number   = 1;
uint16_t hid_report_len  = 0;

// Saved by xhci_arm_keyboard(), consumed by xhci_keyboard_isr()
static uint32_t kbd_slot_id  = 0;
static uint32_t kbd_db_target = 0;

// ============================================================================
// DEBUG: Print status register bits
// ============================================================================
void xhci_print_status(void) {
  uint32_t status = xhci_dev.op_regs->UsbSts;

  printf("USBSTS bits:\n");
  printf("  HCHalted (0): %d\n", (status >> 0) & 1);
  printf("  HSE (2):      %d\n", (status >> 2) & 1);
  printf("  HCE (12):     %d\n", (status >> 12) & 1);
  printf("  CNR (11):     %d\n", (status >> 11) & 1);
  printf("  Raw value: 0x%x\n", status);
}

// ============================================================================
// TEST: Verify address translation (optional, you already did this)
// ============================================================================
void xhci_test_dma_identity(void) {
  printf("=== DMA Address Translation Test ===\n");

  dcbaap[0] = 0x1122334455667788ULL;
  xhci_dev.op_regs->Dcbaap = (uint64_t)&dcbaap[0];

  uint64_t hardware_address = xhci_dev.op_regs->Dcbaap;
  volatile uint64_t *chip_memory = (volatile uint64_t *)hardware_address;

  printf("Kernel virtual address: 0x%lx\n", (uint64_t)&dcbaap[0]);
  printf("Hardware sees address:  0x%lx\n", hardware_address);
  printf("Value at hardware addr: 0x%lx\n", *chip_memory);

  if (*chip_memory == 0x1122334455667788ULL) {
    printf("✓ SUCCESS: 1:1 mapping confirmed!\n");
  } else {
    printf("✗ FAIL: Address translation issue!\n");
  }

  // Clean up
  dcbaap[0] = 0;

  printf("=== Test Complete ===\n\n");
}

// ============================================================================
// STEP 1: RESET CONTROLLER (xHCI Spec 4.2.1)
// ============================================================================
void xhci_reset_controller(void) {
  printf("=== STEP 1: Reset Controller ===\n");

  // Set HCRST bit (bit 1 of UsbCmd)
  printf("Setting HCRST bit...\n");
  xhci_dev.op_regs->UsbCmd |= (1 << 1);

  // Wait for HCRST to clear (hardware does this)
  printf("Waiting for HCRST to clear...\n");
  uint32_t timeout = 1000000;
  while ((xhci_dev.op_regs->UsbCmd & (1 << 1)) && timeout--) {
    __asm__("pause");
  }

  if (timeout == 0) {
    printf("ERROR: HCRST did not clear!\n");
    return;
  }

  printf("HCRST cleared.\n");

  // Wait for Controller Not Ready (CNR, bit 11 of UsbSts) to clear
  printf("Waiting for CNR to clear...\n");
  timeout = 1000000;
  while ((xhci_dev.op_regs->UsbSts & (1 << 11)) && timeout--) {
    __asm__("pause");
  }

  if (timeout == 0) {
    printf("ERROR: CNR did not clear!\n");
    return;
  }

  printf("Controller is ready for configuration.\n");

  // Clear any existing status bits
  xhci_dev.op_regs->UsbSts = 0xFFFFFFFF;

  printf("=== STEP 1: COMPLETE ===\n\n");
}

// ============================================================================
// STEP 2: INITIALIZE DATA STRUCTURES
// ============================================================================
void xhci_init_structures(void) {
  printf("=== STEP 2: Initialize Data Structures ===\n");

  // Zero out all rings and tables
  memset((void*)command_ring, 0, sizeof(command_ring));
  memset((void*)event_ring, 0, sizeof(event_ring));
  memset((void*)dcbaap, 0, sizeof(dcbaap));
  memset((void*)&erst, 0, sizeof(erst));

  printf("Zeroed command ring, event ring, DCBAAP, ERST.\n");
  printf("=== STEP 2: COMPLETE ===\n\n");
}

// ============================================================================
// STEP 3: SETUP COMMAND RING (xHCI Spec 4.6.2.1)
// ============================================================================
void xhci_setup_command_ring(void) {
  printf("=== STEP 3: Setup Command Ring ===\n");

  // Place Link TRB at the end that points back to start
  // This allows the ring to wrap around
  volatile struct XhciTRB *link_trb = &command_ring[255];

  link_trb->Parameter = (uint64_t)&command_ring[0];  // Points back to start
  link_trb->Status = 0;
  // Control: TRB Type (Link=6), Toggle Cycle (bit 1)=1, Cycle bit=1
  link_trb->Control = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;

  printf("Link TRB placed at index 255.\n");
  printf("Link TRB Parameter: 0x%lx\n", link_trb->Parameter);
  printf("Link TRB Control: 0x%x\n", link_trb->Control);

  // Write Command Ring pointer to hardware
  // Bit 0 (RCS) = Cycle State (1 initially)
  uint64_t crcr = (uint64_t)&command_ring[0] | 1;
  xhci_dev.op_regs->Crcr = crcr;

  printf("CRCR written: 0x%lx\n", crcr);
  printf("=== STEP 3: COMPLETE ===\n\n");
}

// ============================================================================
// STEP 4: SETUP EVENT RING AND ERST (xHCI Spec 4.9.4)
// ============================================================================
void xhci_setup_event_ring(void) {
  printf("=== STEP 4: Setup Event Ring and ERST ===\n");

  // Get runtime registers
  uint64_t rt_base = xhci_dev.base_mmio + xhci_dev.cap_regs->Rtsoff;
  volatile struct XhciInterrupterRegs *int_0 =
      (volatile struct XhciInterrupterRegs *)(rt_base + 0x20);

  xhci_dev.runtime_regs = (struct XhciRuntimeRegs *)rt_base;
  xhci_dev.int_0_regs = int_0;

  // Setup ERST entry (one segment in our case)
  erst.RingSegmentBaseAddress = (uint64_t)&event_ring[0];
  erst.RingSegmentSize = 256;
  erst.Reserved = 0;

  printf("  &erst:       0x%lx\n", (uint64_t)&erst);
  printf("  &event_ring: 0x%lx\n", (uint64_t)&event_ring[0]);
  printf("ERST Entry:\n");
  printf("  Base Address: 0x%lx\n", erst.RingSegmentBaseAddress);
  printf("  Ring Size: %d\n", erst.RingSegmentSize);

  // Write to interrupter 0 in correct order per spec:
  // 1. Erstsz (table size) FIRST
  int_0->Erstsz = 1;  // 1 segment entry
  printf("ERSTSZ written: 1\n");

  // 2. Erstba (table base address)
  int_0->Erstba = (uint64_t)&erst;
  printf("ERSTBA written: 0x%lx\n", int_0->Erstba);

  // 3. Erdp (dequeue pointer) - must be 16-byte aligned
  uint64_t erdp = (uint64_t)&event_ring[0];
  erdp &= 0xFFFFFFFFFFFFFFF0ULL;  // Align to 16 bytes
  erdp |= (1 << 3);               // Set EHB (Event Handler Busy clear)
  int_0->Erdp = erdp;
  printf("ERDP written: 0x%lx\n", erdp);

  // 4. Iman (interrupt management) - keep interrupts disabled initially
  int_0->Iman = 0;
  printf("IMAN written: 0 (interrupts disabled)\n");

  printf("=== STEP 4: COMPLETE ===\n\n");
}

// ============================================================================
// STEP 5: CONFIGURE AND START CONTROLLER (xHCI Spec 4.2.2 and 4.2.3)
// ============================================================================
void xhci_start_controller(void) {
  printf("=== STEP 5: Configure and Start Controller ===\n");

  // Set Config register - number of device slots to enable
  // We start with 1 slot for a single device
  uint32_t max_slots = (xhci_dev.cap_regs->HcsParams1 & 0xFF);
  printf("Hardware supports max %d slots\n", max_slots);

  xhci_dev.op_regs->Config = 1;  // Enable 1 slot
  printf("CONFIG set to 1 slot.\n");

  // Check if the controller needs scratchpad buffers (xHCI spec 4.20)
  // MaxScratchpadBufs = HcsParams2[31:27] (low 5 bits of 10-bit field)
  uint32_t hcs2 = xhci_dev.cap_regs->HcsParams2;
  uint32_t max_scratch = (hcs2 >> 27) & 0x1F;
  printf("Max scratchpad bufs: %d\n", max_scratch);
  if (max_scratch > 0) {
    // dcbaap[0] must point to a scratchpad buffer array before RS=1.
    // For now we halt — this controller requires scratchpad support.
    printf("ERROR: Scratchpad buffers required but not implemented!\n");
    return;
  }

  // Set DCBAAP (Device Context Base Address Array Pointer)
  xhci_dev.op_regs->Dcbaap = (uint64_t)&dcbaap[0];
  printf("DCBAAP written: 0x%lx\n", (uint64_t)&dcbaap[0]);

  // Set Run bit (bit 0 of UsbCmd)
  printf("Setting Run bit...\n");
  xhci_dev.op_regs->UsbCmd |= (1 << 0);

  // Wait for HCHalted to clear (bit 0 of UsbSts)
  printf("Waiting for controller to start...\n");
  uint32_t timeout = 100000;
  while ((xhci_dev.op_regs->UsbSts & (1 << 0)) && timeout--) {
    __asm__("pause");
  }

  if (timeout == 0) {
    printf("ERROR: Controller failed to start!\n");
    printf("USBSTS: 0x%x\n", xhci_dev.op_regs->UsbSts);
    xhci_print_status();
    return;
  }

  printf("Controller is now RUNNING!\n");
  printf("USBSTS: 0x%x\n", xhci_dev.op_regs->UsbSts);

  printf("=== STEP 5: COMPLETE ===\n\n");
}

// ============================================================================
// STEP 6: SCAN PORTS FOR DEVICES
// ============================================================================
void xhci_scan_ports(void) {

  uint32_t max_ports = xhci_dev.max_ports;
  printf("Scanning %d ports...\n", max_ports);

  for (uint32_t port = 0; port < max_ports; port++) {
    uint32_t portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;

    // Bit 0 = Current Connect Status (CCS)
    if (portsc & (1 << 0)) {
      printf("\n--> Device connected on Port %d!\n", port + 1);
      printf("    Raw PORTSC: 0x%x\n", portsc);

      // Extract Port Speed (bits 10-13)
      uint8_t speed = (portsc >> 10) & 0x0F;

      // Reset the port
      printf("    Resetting port...\n");

      // Set Port Reset (bit 4)
      uint32_t portsc_reset = portsc | (1 << 4);
      // Clear other write-1-to-clear bits (bits 17-20)
      portsc_reset &= ~(0x0F << 17);
      xhci_dev.op_regs->PortRegisterSet[port].PortSc = portsc_reset;

      // Wait for reset to complete (hardware clears bit 4)
      uint32_t reset_timeout = 100000;
      while ((xhci_dev.op_regs->PortRegisterSet[port].PortSc & (1 << 4)) &&
             reset_timeout--) {
        __asm__("pause");
      }

      printf("    Port reset complete.\n");

      // Verify port is now enabled (bit 1) before proceeding
      uint32_t portsc_after = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
      if (!(portsc_after & (1 << 1))) {
        printf("    WARNING: Port %d not enabled after reset (PORTSC=0x%x)\n",
               port + 1, portsc_after);
        continue;
      }

      xhci_enable_slot(port);
    }
  }

}

// ============================================================================
// STEP 7: ENABLE SLOT (xHCI spec 4.3.2)
// Sends an Enable Slot command and waits for the controller to assign a slot.
// ============================================================================
void xhci_enable_slot(uint32_t port) {
  printf("=== STEP 7: Enable Slot (port %d) ===\n", port + 1);

  xhci_send_command(TRB_TYPE_ENABLE_SLOT, 0);

  uint32_t slot_id = xhci_poll_event_ring();
  if (slot_id == 0) {
    printf("ERROR: Enable Slot failed on port %d\n", port + 1);
    return;
  }

  printf("Port %d -> slot %d assigned\n", port + 1, slot_id);
  printf("=== STEP 7: COMPLETE ===\n\n");

  xhci_address_device(slot_id, port);
}

// ============================================================================
// STEP 8: ADDRESS DEVICE (xHCI spec 4.3.3 / 4.6.5)
// Builds Input Context with Slot + EP0, sets DCBAAP[slot], sends
// Address Device command (BSR=0 → controller issues USB SET_ADDRESS).
// ============================================================================
void xhci_address_device(uint32_t slot_id, uint32_t port) {
  printf("=== STEP 8: Address Device (slot %d, port %d) ===\n", slot_id, port + 1);

  // Read port speed from PORTSC bits [13:10]
  uint32_t portsc = xhci_dev.op_regs->PortRegisterSet[port].PortSc;
  uint8_t  speed  = (portsc >> 10) & 0x0F;
  dev_speed = speed;
  dev_port  = port;
  printf("Speed code: %d\n", speed);

  // EP0 max packet size for initial enumeration (xHCI spec 4.8.2.1)
  // Speed: 1=Full, 2=Low, 3=High, 4=SuperSpeed
  uint16_t mps;
  switch (speed) {
    case 3:  mps = 64;  break;  // High Speed
    case 4:  mps = 512; break;  // SuperSpeed Gen1x1
    case 2:  mps = 8;   break;  // Low Speed
    default: mps = 8;   break;  // Full Speed (safe default for enumeration)
  }
  printf("EP0 max packet size: %d\n", mps);

  // === EP0 Transfer Ring ===
  memset((void *)ep0_ring, 0, sizeof(ep0_ring));
  volatile struct XhciTRB *link = &ep0_ring[255];
  link->Parameter = (uint64_t)&ep0_ring[0];
  link->Status    = 0;
  link->Control   = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
  ep0_enqueue = 0;
  ep0_cycle   = 1;

  // === Input Context ===
  memset((void *)&input_ctx, 0, sizeof(input_ctx));

  // Input Control Context: A0 (slot) + A1 (EP0)
  input_ctx.ctrl.add_flags = (1U << 0) | (1U << 1);

  // Slot Context dw0: Context Entries=1, Speed, Route String=0
  input_ctx.slot.dw0 = ((uint32_t)speed << 20) | (1U << 27);
  // Slot Context dw1: Root Hub Port Number (1-indexed)
  input_ctx.slot.dw1 = (uint32_t)(port + 1) << 16;

  // EP0 Endpoint Context
  // dw1: CErr=3, EP Type=Control Bidirectional(4), Max Packet Size
  input_ctx.ep0.dw1 = (3U << 1) | (EP_TYPE_CONTROL_BIDIR << 3) | ((uint32_t)mps << 16);
  // dw2/dw3: TR Dequeue Pointer | DCS=1
  uint64_t ep0_addr = (uint64_t)&ep0_ring[0];
  input_ctx.ep0.dw2 = (uint32_t)(ep0_addr & ~0xFULL) | 1;
  input_ctx.ep0.dw3 = (uint32_t)(ep0_addr >> 32);
  // dw4: Average TRB Length = 8 (standard for control endpoints)
  input_ctx.ep0.dw4 = 8;

  printf("Input Context:  0x%lx\n", (uint64_t)&input_ctx);
  printf("Device Context: 0x%lx\n", (uint64_t)&device_ctx);

  // === Point DCBAAP[slot_id] at the output Device Context ===
  dcbaap[slot_id] = (uint64_t)&device_ctx;

  // === Address Device TRB ===
  // Parameter = Input Context pointer (16-byte aligned, page-aligned here)
  // Control:  TRB Type[15:10]=11 | BSR[9]=0 | Slot ID[31:24] | Cycle[0]
  volatile struct XhciTRB *trb = &command_ring[command_ring_enqueue];
  trb->Parameter = (uint64_t)&input_ctx;
  trb->Status    = 0;
  trb->Control   = (TRB_TYPE_ADDRESS_DEVICE << 10)
                 | ((uint32_t)slot_id << 24)
                 | command_ring_cycle;

  command_ring_enqueue++;
  if (command_ring_enqueue == 255) {
    command_ring[255].Control = (TRB_TYPE_LINK << 10) | (1 << 1) | command_ring_cycle;
    command_ring_cycle ^= 1;
    command_ring_enqueue = 0;
  }
  xhci_dev.doorbell_regs[0] = 0;

  uint32_t result = xhci_poll_event_ring();
  if (result == 0) {
    printf("ERROR: Address Device failed on slot %d\n", slot_id);
    return;
  }

  printf("Slot %d is now in Addressed state.\n", slot_id);
  printf("=== STEP 8: COMPLETE ===\n\n");

  if (!xhci_get_descriptor(slot_id, USB_DESC_TYPE_DEVICE, 18)) return;

  // Step 10: if reported EP0 MPS differs from the speed-based default, update it
  uint8_t reported_mps = descriptor_buffer[7];
  if (reported_mps != mps) {
    if (!xhci_evaluate_context(slot_id, reported_mps)) return;
  } else {
    printf("=== STEP 10: EP0 MPS confirmed (%d), no update needed ===\n\n", mps);
  }

  if (!xhci_get_config_descriptor(slot_id)) return;

  // Step 12: SET_CONFIGURATION + Configure Endpoint
  uint8_t config_val = descriptor_buffer[5];  // bConfigurationValue from Config Descriptor
  if (!xhci_set_configuration(slot_id, config_val)) return;

  if (!xhci_get_hid_report_descriptor(slot_id)) return;  // Step 13
  xhci_arm_keyboard(slot_id);                            // Step 14 — queue first TRB and return
}

// ============================================================================
// COMMAND RING: Submit a TRB and ring doorbell 0 (xHCI spec 4.6)
// ============================================================================
void xhci_send_command(uint32_t trb_type, uint64_t parameter) {
  volatile struct XhciTRB *trb = &command_ring[command_ring_enqueue];

  trb->Parameter = parameter;
  trb->Status    = 0;
  // Bits 10-15: TRB Type; bit 0: Cycle bit
  trb->Control   = (trb_type << 10) | command_ring_cycle;

  command_ring_enqueue++;

  // Wrap at Link TRB position (index 255)
  if (command_ring_enqueue == 255) {
    // Update Link TRB cycle bit; TC=1 means hardware toggles on wrap
    command_ring[255].Control = (TRB_TYPE_LINK << 10) | (1 << 1) | command_ring_cycle;
    command_ring_cycle ^= 1;
    command_ring_enqueue = 0;
  }

  // Doorbell 0 = host controller command ring; value 0 = "no target"
  xhci_dev.doorbell_regs[0] = 0;

  printf("CMD: type=%d enq=%d cycle=%d\n", trb_type, command_ring_enqueue, command_ring_cycle);
}

// ============================================================================
// EVENT RING: Poll for a Command Completion Event (xHCI spec 4.11.5.1)
// Returns the slot_id on success (completion code 1), 0 on error/timeout.
// ============================================================================
uint32_t xhci_poll_event_ring(void) {
  uint32_t timeout = 2000000;

  while (timeout--) {
    volatile struct XhciEventTRB *event = &event_ring[event_ring_dequeue];

    // A TRB is valid when its cycle bit matches our expected cycle
    if ((event->Control & 1) == event_ring_cycle) {
      uint32_t trb_type       = (event->Control >> 10) & 0x3F;
      uint32_t completion_code = (event->Status  >> 24) & 0xFF;
      uint32_t slot_id        = (event->Control  >> 24) & 0xFF;

      printf("EVT: type=%d code=%d slot=%d\n", trb_type, completion_code, slot_id);

      // Advance dequeue pointer, toggle cycle on wrap
      event_ring_dequeue++;
      if (event_ring_dequeue >= 256) {
        event_ring_dequeue = 0;
        event_ring_cycle ^= 1;
      }

      // Acknowledge: update ERDP and clear EHB (bit 3)
      uint64_t erdp = (uint64_t)&event_ring[event_ring_dequeue] | (1 << 3);
      xhci_dev.int_0_regs->Erdp = erdp;

      if (trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
        if (completion_code != 1) {
          printf("ERROR: Command completion code %d\n", completion_code);
          return 0;
        }
        return slot_id;
      }
      // Non-command events (e.g. port status change): consume and keep polling
    }

    __asm__("pause");
  }

  printf("ERROR: Event ring timed out (USBSTS=0x%x)\n", xhci_dev.op_regs->UsbSts);
  return 0;
}

// ============================================================================
// EP0 HELPER: Submit a Control IN transfer on EP0 (Setup + Data + Status).
// Handles ep0_ring wrap at slot 255. Returns 1 on success, 0 on failure.
// ============================================================================
static uint32_t ep0_control_in(uint32_t slot_id, uint64_t setup,
                                volatile uint8_t *buf, uint16_t length) {
  // Setup Stage TRB: IDT=1 (immediate data), TRT=3 (IN data follows)
  volatile struct XhciTRB *s = &ep0_ring[ep0_enqueue++];
  s->Parameter = setup;
  s->Status    = 8;
  s->Control   = (TRB_TYPE_SETUP_STAGE << 10) | (3U << 16) | (1U << 6) | ep0_cycle;
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1; ep0_enqueue = 0;
  }

  // Data Stage TRB: DIR=1 (IN from device)
  volatile struct XhciTRB *d = &ep0_ring[ep0_enqueue++];
  d->Parameter = (uint64_t)buf;
  d->Status    = length;
  d->Control   = (TRB_TYPE_DATA_STAGE << 10) | (1U << 16) | ep0_cycle;
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1; ep0_enqueue = 0;
  }

  // Status Stage TRB: DIR=0 (OUT, opposite of IN data), IOC=1
  volatile struct XhciTRB *t = &ep0_ring[ep0_enqueue++];
  t->Parameter = 0;
  t->Status    = 0;
  t->Control   = (TRB_TYPE_STATUS_STAGE << 10) | (1U << 5) | ep0_cycle;
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1; ep0_enqueue = 0;
  }

  xhci_dev.doorbell_regs[slot_id] = 1;
  return xhci_poll_transfer_event();
}

// ============================================================================
// EVENT RING: Poll for a Transfer Event (TRB type 32) on EP0.
// Returns 1 on Success (code 1) or Short Packet (code 13), 0 on error/timeout.
// ============================================================================
uint32_t xhci_poll_transfer_event(void) {
  uint32_t timeout = 2000000;

  while (timeout--) {
    volatile struct XhciEventTRB *event = &event_ring[event_ring_dequeue];

    if ((event->Control & 1) == event_ring_cycle) {
      uint32_t trb_type        = (event->Control >> 10) & 0x3F;
      uint32_t completion_code = (event->Status  >> 24) & 0xFF;
      uint32_t slot_id         = (event->Control >> 24) & 0xFF;

      printf("EVT: type=%d code=%d slot=%d\n", trb_type, completion_code, slot_id);

      event_ring_dequeue++;
      if (event_ring_dequeue >= 256) {
        event_ring_dequeue = 0;
        event_ring_cycle ^= 1;
      }
      uint64_t erdp = (uint64_t)&event_ring[event_ring_dequeue] | (1 << 3);
      xhci_dev.int_0_regs->Erdp = erdp;

      if (trb_type == TRB_TYPE_TRANSFER_EVENT) {
        if (completion_code == 1 || completion_code == 13) {
          return 1;
        }
        printf("ERROR: Transfer completion code %d\n", completion_code);
        return 0;
      }
      // Consume non-transfer events and keep polling
    }

    __asm__("pause");
  }

  printf("ERROR: Transfer event timed out (USBSTS=0x%x)\n", xhci_dev.op_regs->UsbSts);
  return 0;
}

// ============================================================================
// STEP 9: GET_DESCRIPTOR via EP0 control transfer (xHCI spec 4.11.2.2)
// Submits Setup + Data + Status TRBs on EP0, rings doorbell, reads descriptor.
// ============================================================================
int xhci_get_descriptor(uint32_t slot_id, uint8_t desc_type, uint16_t length) {
  printf("=== STEP 9: GET_DESCRIPTOR (type=0x%x, len=%d) ===\n", desc_type, length);

  // 8-byte USB SETUP packet (little-endian in a uint64_t):
  // bmRequestType=0x80 | bRequest=0x06 | wValue=(desc_type<<8) | wIndex=0 | wLength=length
  uint64_t setup = (uint64_t)0x80
                 | ((uint64_t)0x06      << 8)
                 | ((uint64_t)desc_type << 24)
                 | ((uint64_t)length    << 48);

  // Clear receive buffer
  for (uint32_t i = 0; i < 64; i++) descriptor_buffer[i] = 0;

  // 1. Setup Stage TRB: setup packet in Parameter field (IDT=1),
  //    TRT=3 (IN data stage follows), TRB Type=2
  volatile struct XhciTRB *setup_trb = &ep0_ring[ep0_enqueue++];
  setup_trb->Parameter = setup;
  setup_trb->Status    = 8;  // TRB Transfer Length = 8 (size of SETUP packet)
  setup_trb->Control   = (TRB_TYPE_SETUP_STAGE << 10) | (3U << 16) | (1U << 6) | ep0_cycle;

  // 2. Data Stage TRB: receive buffer pointer, DIR=1 (IN from device)
  volatile struct XhciTRB *data_trb = &ep0_ring[ep0_enqueue++];
  data_trb->Parameter = (uint64_t)descriptor_buffer;
  data_trb->Status    = length;
  data_trb->Control   = (TRB_TYPE_DATA_STAGE << 10) | (1U << 16) | ep0_cycle;

  // 3. Status Stage TRB: DIR=0 (OUT, opposite of IN data), IOC=1
  volatile struct XhciTRB *status_trb = &ep0_ring[ep0_enqueue++];
  status_trb->Parameter = 0;
  status_trb->Status    = 0;
  status_trb->Control   = (TRB_TYPE_STATUS_STAGE << 10) | (1U << 5) | ep0_cycle;

  // Ring EP0 doorbell (DB Target 1 = EP0 bidirectional)
  printf("Ringing EP0 doorbell for slot %d\n", slot_id);
  xhci_dev.doorbell_regs[slot_id] = 1;

  if (!xhci_poll_transfer_event()) {
    printf("ERROR: GET_DESCRIPTOR failed\n");
    return 0;
  }

  // Parse Device Descriptor fields
  uint8_t  bLength            = descriptor_buffer[0];
  uint8_t  bDescriptorType    = descriptor_buffer[1];
  uint16_t bcdUSB             = (uint16_t)descriptor_buffer[2] | ((uint16_t)descriptor_buffer[3] << 8);
  uint8_t  bDeviceClass       = descriptor_buffer[4];
  uint8_t  bMaxPacketSize0    = descriptor_buffer[7];
  uint16_t idVendor           = (uint16_t)descriptor_buffer[8]  | ((uint16_t)descriptor_buffer[9]  << 8);
  uint16_t idProduct          = (uint16_t)descriptor_buffer[10] | ((uint16_t)descriptor_buffer[11] << 8);
  uint8_t  bNumConfigurations = descriptor_buffer[17];

  printf("Device Descriptor:\n");
  printf("  bLength:            %d\n", bLength);
  printf("  bDescriptorType:    %d\n", bDescriptorType);
  printf("  bcdUSB:             0x%x\n", bcdUSB);
  printf("  bDeviceClass:       0x%x\n", bDeviceClass);
  printf("  bMaxPacketSize0:    %d\n", bMaxPacketSize0);
  printf("  idVendor:           0x%x\n", idVendor);
  printf("  idProduct:          0x%x\n", idProduct);
  printf("  bNumConfigurations: %d\n", bNumConfigurations);

  printf("=== STEP 9: COMPLETE ===\n\n");
  return 1;
}

// ============================================================================
// STEP 10: Evaluate Context — update EP0 MPS if device reports a different
// value than the speed-based default set during Address Device (xHCI 4.3.3).
// ============================================================================
int xhci_evaluate_context(uint32_t slot_id, uint8_t new_mps) {
  printf("=== STEP 10: Evaluate Context (new EP0 MPS=%d) ===\n", new_mps);

  // Reuse input_ctx: only A1 (EP0) is set; slot context is not evaluated
  memset((void *)&input_ctx, 0, sizeof(input_ctx));
  input_ctx.ctrl.drop_flags = 0;
  input_ctx.ctrl.add_flags  = (1U << 1);  // A1 = EP0 only
  input_ctx.ep0.dw1 = (3U << 1) | (EP_TYPE_CONTROL_BIDIR << 3) | ((uint32_t)new_mps << 16);

  volatile struct XhciTRB *trb = &command_ring[command_ring_enqueue];
  trb->Parameter = (uint64_t)&input_ctx;
  trb->Status    = 0;
  trb->Control   = (TRB_TYPE_EVALUATE_CONTEXT << 10)
                 | ((uint32_t)slot_id << 24)
                 | command_ring_cycle;

  command_ring_enqueue++;
  if (command_ring_enqueue == 255) {
    command_ring[255].Control = (TRB_TYPE_LINK << 10) | (1 << 1) | command_ring_cycle;
    command_ring_cycle ^= 1;
    command_ring_enqueue = 0;
  }
  xhci_dev.doorbell_regs[0] = 0;

  if (!xhci_poll_event_ring()) {
    printf("ERROR: Evaluate Context failed\n");
    return 0;
  }

  printf("EP0 MPS updated to %d\n", new_mps);
  printf("=== STEP 10: COMPLETE ===\n\n");
  return 1;
}

// ============================================================================
// STEP 11: GET_DESCRIPTOR (Configuration) — two-phase fetch:
//   Phase 1: request 9 bytes to read wTotalLength
//   Phase 2: request wTotalLength bytes for the full descriptor tree
// Parses Config, Interface, HID, and Endpoint descriptors and prints them.
// ============================================================================
int xhci_get_config_descriptor(uint32_t slot_id) {
  printf("=== STEP 11: GET_DESCRIPTOR Configuration ===\n");

  // Phase 1: fetch the 9-byte Config Descriptor header to get wTotalLength
  for (uint32_t i = 0; i < 256; i++) descriptor_buffer[i] = 0;

  uint64_t setup9 = (uint64_t)0x80
                  | ((uint64_t)0x06 << 8)
                  | ((uint64_t)USB_DESC_TYPE_CONFIG << 24)
                  | ((uint64_t)9 << 48);

  if (!ep0_control_in(slot_id, setup9, descriptor_buffer, 9)) {
    printf("ERROR: GET_DESCRIPTOR Config (9 bytes) failed\n");
    return 0;
  }

  uint16_t total_length = (uint16_t)descriptor_buffer[2]
                        | ((uint16_t)descriptor_buffer[3] << 8);
  printf("wTotalLength = %d\n", total_length);

  if (total_length < 9 || total_length > 255) {
    printf("ERROR: Invalid wTotalLength %d\n", total_length);
    return 0;
  }

  // Phase 2: fetch the full descriptor tree
  for (uint32_t i = 0; i < 256; i++) descriptor_buffer[i] = 0;

  uint64_t setup_full = (uint64_t)0x80
                      | ((uint64_t)0x06 << 8)
                      | ((uint64_t)USB_DESC_TYPE_CONFIG << 24)
                      | ((uint64_t)total_length << 48);

  if (!ep0_control_in(slot_id, setup_full, descriptor_buffer, total_length)) {
    printf("ERROR: GET_DESCRIPTOR Config (full) failed\n");
    return 0;
  }

  // Parse the descriptor tree starting with the Config Descriptor at offset 0
  printf("Config Descriptor:\n");
  printf("  bNumInterfaces:      %d\n", descriptor_buffer[4]);
  printf("  bConfigurationValue: %d\n", descriptor_buffer[5]);
  printf("  bmAttributes:        0x%x\n", descriptor_buffer[7]);
  printf("  bMaxPower:           %dmA\n", descriptor_buffer[8] * 2);

  // Walk all sub-descriptors
  uint32_t offset = descriptor_buffer[0];  // skip Config Descriptor itself
  while (offset + 1 < total_length) {
    uint8_t bLength = descriptor_buffer[offset];
    uint8_t bType   = descriptor_buffer[offset + 1];

    if (bLength < 2 || offset + bLength > total_length) break;

    if (bType == 0x04) {  // Interface Descriptor
      printf("Interface Descriptor:\n");
      printf("  bInterfaceNumber:   %d\n", descriptor_buffer[offset + 2]);
      printf("  bNumEndpoints:      %d\n", descriptor_buffer[offset + 4]);
      printf("  bInterfaceClass:    0x%x\n", descriptor_buffer[offset + 5]);
      printf("  bInterfaceSubClass: 0x%x\n", descriptor_buffer[offset + 6]);
      printf("  bInterfaceProtocol: 0x%x\n", descriptor_buffer[offset + 7]);
    } else if (bType == 0x21) {  // HID Descriptor
      uint16_t bcdHID    = (uint16_t)descriptor_buffer[offset + 2]
                         | ((uint16_t)descriptor_buffer[offset + 3] << 8);
      uint16_t rpt_len   = (uint16_t)descriptor_buffer[offset + 7]
                         | ((uint16_t)descriptor_buffer[offset + 8] << 8);
      hid_report_len = rpt_len;  // save for Step 13
      printf("HID Descriptor:\n");
      printf("  bcdHID:             0x%x\n", bcdHID);
      printf("  bCountryCode:       %d\n",   descriptor_buffer[offset + 4]);
      printf("  bNumDescriptors:    %d\n",   descriptor_buffer[offset + 5]);
      printf("  wDescriptorLength:  %d\n",   rpt_len);
    } else if (bType == 0x05) {  // Endpoint Descriptor
      uint8_t  addr = descriptor_buffer[offset + 2];
      uint16_t mps  = (uint16_t)descriptor_buffer[offset + 4]
                    | ((uint16_t)descriptor_buffer[offset + 5] << 8);
      // Save IN endpoint info for Steps 12-14
      if (addr & 0x80) {
        ep1_in_addr     = addr;
        ep1_in_mps      = mps;
        ep1_in_interval = descriptor_buffer[offset + 6];
        ep1_in_number   = addr & 0x0F;
      }
      printf("Endpoint Descriptor:\n");
      printf("  bEndpointAddress:   0x%x (%s EP%d)\n",
             addr, (addr & 0x80) ? "IN" : "OUT", addr & 0x0F);
      printf("  bmAttributes:       0x%x\n", descriptor_buffer[offset + 3]);
      printf("  wMaxPacketSize:     %d\n",   mps);
      printf("  bInterval:          %d\n",   descriptor_buffer[offset + 6]);
    }

    offset += bLength;
  }

  printf("=== STEP 11: COMPLETE ===\n\n");
  return 1;
}

// ============================================================================
// EP0 HELPER: Control transfer with no data stage (e.g. SET_CONFIGURATION).
// Setup TRB has TRT=0; Status Stage is IN (DIR=1) per xHCI spec 4.11.2.2.
// ============================================================================
static uint32_t ep0_control_nodata(uint32_t slot_id, uint64_t setup) {
  // Setup Stage TRB: IDT=1, TRT=0 (no data stage)
  volatile struct XhciTRB *s = &ep0_ring[ep0_enqueue++];
  s->Parameter = setup;
  s->Status    = 8;
  s->Control   = (TRB_TYPE_SETUP_STAGE << 10) | (0U << 16) | (1U << 6) | ep0_cycle;
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1; ep0_enqueue = 0;
  }

  // Status Stage TRB: DIR=1 (IN), IOC=1
  volatile struct XhciTRB *t = &ep0_ring[ep0_enqueue++];
  t->Parameter = 0;
  t->Status    = 0;
  t->Control   = (TRB_TYPE_STATUS_STAGE << 10) | (1U << 16) | (1U << 5) | ep0_cycle;
  if (ep0_enqueue >= 255) {
    ep0_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | ep0_cycle;
    ep0_cycle ^= 1; ep0_enqueue = 0;
  }

  xhci_dev.doorbell_regs[slot_id] = 1;
  return xhci_poll_transfer_event();
}

// ============================================================================
// STEP 12: SET_CONFIGURATION + Configure Endpoint (xHCI spec 4.3.4 / 4.6.6)
// 1. Issues USB SET_CONFIGURATION to put device into Configured state.
// 2. Issues xHCI Configure Endpoint command to register EP1 IN with the HC.
// ============================================================================
int xhci_set_configuration(uint32_t slot_id, uint8_t config_val) {
  printf("=== STEP 12: SET_CONFIGURATION (value=%d) ===\n", config_val);

  // USB SET_CONFIGURATION: bmRequestType=0x00, bRequest=0x09, wValue=config_val
  uint64_t setup = ((uint64_t)0x09 << 8) | ((uint64_t)config_val << 16);
  if (!ep0_control_nodata(slot_id, setup)) {
    printf("ERROR: SET_CONFIGURATION USB transfer failed\n");
    return 0;
  }
  printf("USB SET_CONFIGURATION accepted\n");

  // xHCI Configure Endpoint: register EP1 IN (context index = 2*N+1 for IN)
  uint32_t ep_ctx_idx = (uint32_t)(ep1_in_number * 2 + 1);  // e.g. EP1 IN = 3

  // Init EP1 IN transfer ring
  memset((void *)ep1in_ring, 0, sizeof(ep1in_ring));
  ep1in_ring[255].Parameter = (uint64_t)&ep1in_ring[0];
  ep1in_ring[255].Status    = 0;
  ep1in_ring[255].Control   = (TRB_TYPE_LINK << 10) | (1U << 1) | 1;
  ep1in_enqueue = 0;
  ep1in_cycle   = 1;

  // Convert USB bInterval → xHCI Interval (power-of-2 in 125 μs microframes)
  uint8_t xhci_interval;
  if (dev_speed == 3 || dev_speed == 4) {  // High Speed / SuperSpeed
    xhci_interval = (ep1_in_interval > 0) ? ep1_in_interval - 1 : 0;
  } else {
    // Full/Low Speed: bInterval is in ms; convert to microframes (×8), take floor(log2)
    uint32_t uf = (uint32_t)ep1_in_interval * 8;
    xhci_interval = 0;
    while (uf > 1 && xhci_interval < 15) { uf >>= 1; xhci_interval++; }
  }

  // Build Input Context for Configure Endpoint
  memset((void *)&input_ctx, 0, sizeof(input_ctx));
  // A0=slot, A{ep_ctx_idx}=EP1 IN
  input_ctx.ctrl.add_flags = (1U << 0) | (1U << ep_ctx_idx);
  // Slot: update ContextEntries to include EP1 IN
  input_ctx.slot.dw0 = ((uint32_t)dev_speed << 20) | (ep_ctx_idx << 27);
  input_ctx.slot.dw1 = (uint32_t)(dev_port + 1) << 16;
  // EP1 IN Endpoint Context
  input_ctx.ep1in.dw0 = (uint32_t)xhci_interval << 16;
  // CErr=3, EPType=7 (Interrupt IN), MaxPacketSize
  input_ctx.ep1in.dw1 = (3U << 1) | (7U << 3) | ((uint32_t)ep1_in_mps << 16);
  uint64_t ep1in_addr = (uint64_t)&ep1in_ring[0];
  input_ctx.ep1in.dw2 = (uint32_t)(ep1in_addr & ~0xFULL) | 1;  // DCS=1
  input_ctx.ep1in.dw3 = (uint32_t)(ep1in_addr >> 32);
  input_ctx.ep1in.dw4 = ep1_in_mps;  // AvgTRBLength = MPS for interrupt

  // Send Configure Endpoint command
  volatile struct XhciTRB *trb = &command_ring[command_ring_enqueue];
  trb->Parameter = (uint64_t)&input_ctx;
  trb->Status    = 0;
  trb->Control   = (TRB_TYPE_CONFIGURE_ENDPOINT << 10)
                 | ((uint32_t)slot_id << 24)
                 | command_ring_cycle;

  command_ring_enqueue++;
  if (command_ring_enqueue == 255) {
    command_ring[255].Control = (TRB_TYPE_LINK << 10) | (1U << 1) | command_ring_cycle;
    command_ring_cycle ^= 1;
    command_ring_enqueue = 0;
  }
  xhci_dev.doorbell_regs[0] = 0;

  if (!xhci_poll_event_ring()) {
    printf("ERROR: Configure Endpoint command failed\n");
    return 0;
  }

  printf("EP%d IN configured (MPS=%d, xhci_interval=%d)\n",
         ep1_in_number, ep1_in_mps, xhci_interval);
  printf("=== STEP 12: COMPLETE ===\n\n");
  return 1;
}

// ============================================================================
// STEP 13: GET_DESCRIPTOR HID Report Descriptor (USB HID spec 7.1.1)
// bmRequestType=0x81 (D-to-H, Standard, Interface), bRequest=0x06,
// wValue=0x2200 (type=HID Report, index=0), wIndex=0, wLength=hid_report_len
// ============================================================================
int xhci_get_hid_report_descriptor(uint32_t slot_id) {
  printf("=== STEP 13: GET_DESCRIPTOR HID Report (len=%d) ===\n", hid_report_len);

  if (hid_report_len == 0) {
    printf("ERROR: hid_report_len not set (Step 11 may have failed)\n");
    return 0;
  }

  uint16_t req_len = (hid_report_len > 256) ? 256 : hid_report_len;
  for (uint32_t i = 0; i < 256; i++) descriptor_buffer[i] = 0;

  // bmRequestType=0x81: D-to-H, Standard, Interface recipient
  // wValue high=0x22 (HID Report Descriptor type), wIndex=0 (interface 0)
  uint64_t setup = (uint64_t)0x81
                 | ((uint64_t)0x06 << 8)
                 | ((uint64_t)USB_DESC_TYPE_HID_REPORT << 24)
                 | ((uint64_t)req_len << 48);

  if (!ep0_control_in(slot_id, setup, descriptor_buffer, req_len)) {
    printf("ERROR: GET_DESCRIPTOR HID Report failed\n");
    return 0;
  }

  // Print raw descriptor bytes, 16 per line
  static const char hex_ch[] = "0123456789abcdef";
  printf("HID Report Descriptor (%d bytes):\n", req_len);
  for (uint16_t i = 0; i < req_len; i++) {
    uint8_t b = descriptor_buffer[i];
    char    hi = hex_ch[(b >> 4) & 0xF];
    char    lo = hex_ch[b & 0xF];
    char    s[4] = {hi, lo, ' ', '\0'};
    printf("%s", s);
    if ((i + 1) % 16 == 0) printf("\n");
  }
  if (req_len % 16 != 0) printf("\n");

  printf("=== STEP 13: COMPLETE ===\n\n");
  return 1;
}

// ============================================================================
// STEP 14: Interrupt-driven keyboard via xHCI MSI
// ============================================================================

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

// Queue the first TRB and return. MSI will fire when the device sends a report.
void xhci_arm_keyboard(uint32_t slot_id) {
  printf("=== STEP 14: Arming keyboard interrupt (slot %d, EP%d IN, MPS=%d) ===\n",
         slot_id, ep1_in_number, ep1_in_mps);

  kbd_slot_id   = slot_id;
  kbd_db_target = (uint32_t)ep1_in_number * 2 + 1;

  xhci_queue_kbd_trb();

  printf("=== STEP 14: Keyboard armed — waiting for MSI ===\n");
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
// MSI CONFIGURATION (call after xhci_arm_keyboard, before STI)
// ============================================================================

// Walk the PCI capability list, configure MSI to fire on `vector`, then
// enable the xHCI interrupter and USBCMD.INTE.
void xhci_enable_msi(uint8_t vector) {
  printf("=== Enabling xHCI MSI-X (vector 0x%x) ===\n", vector);

  volatile uint32_t *pci = xhci_dev.pci_regs;
  if (!pci) { printf("ERROR: xHCI PCI regs not set\n"); return; }

  // Enable Memory Space (bit 1) + Bus Mastering (bit 2) in Command register
  pci[1] |= (1U << 1) | (1U << 2);

  // Capabilities Pointer is at PCI config offset 0x34 (DWORD index 13), low byte
  uint8_t cap_ptr = (uint8_t)(pci[0x34 / 4] & 0xFF);

  while (cap_ptr) {
    if (cap_ptr < 0x40) break;

    uint32_t cap_dw   = pci[cap_ptr / 4];
    uint8_t  cap_id   = cap_dw & 0xFF;
    uint8_t  cap_next = (cap_dw >> 8) & 0xFF;

    if (cap_id == 0x11) {  // MSI-X capability
      // Message Control: bits [31:16] of cap_dw
      //   [26:16] = Table Size (N-1)   → actual entries = (field)+1
      //   [30]    = Function Mask
      //   [31]    = MSI-X Enable
      uint16_t table_size = ((cap_dw >> 16) & 0x7FF) + 1;

      // DWORD at cap+4: Table BIR [2:0] + Table Offset [31:3]
      uint32_t table_reg = pci[(cap_ptr + 4) / 4];
      uint8_t  bir       = table_reg & 0x7;
      uint32_t table_off = table_reg & ~0x7U;

      printf("MSI-X cap at 0x%x: %d entries, BIR=%d, table_off=0x%x\n",
             cap_ptr, table_size, bir, table_off);

      // Resolve the BAR that holds the MSI-X table.
      // BAR0+BAR1 form a 64-bit BAR; xhci_dev.base_mmio already combines them.
      // For bir>0, read the raw BAR from PCI config space.
      uint64_t bar_base;
      if (bir == 0) {
        bar_base = xhci_dev.base_mmio;
      } else {
        uint32_t lo = pci[(0x10 + bir * 4) / 4] & 0xFFFFFFF0U;
        uint32_t hi = pci[(0x10 + bir * 4 + 4) / 4];
        bar_base = (uint64_t)lo | ((uint64_t)hi << 32);
      }

      // MSI-X table entry 0 (16 bytes: addr_lo, addr_hi, data, vector_ctrl)
      volatile uint32_t *entry = (volatile uint32_t *)(bar_base + table_off);
      entry[0] = 0xFEE00000U;      // message address: LAPIC, BSP, physical
      entry[1] = 0;                 // message address high
      entry[2] = (uint32_t)vector;  // message data: IDT vector
      entry[3] = 0;                 // vector control: bit 0=0 → unmasked

      // Enable MSI-X (bit 31) and clear Function Mask (bit 30)
      pci[cap_ptr / 4] = (cap_dw | (1U << 31)) & ~(1U << 30);

      printf("MSI-X configured: addr=0xFEE00000 data=0x%x\n", vector);

      // Enable xHCI Interrupter 0: IE=1, clear any pending IP
      xhci_dev.int_0_regs->Iman = 0x3;

      // Enable host controller interrupt generation: USBCMD.INTE (bit 2)
      xhci_dev.op_regs->UsbCmd |= (1U << 2);

      printf("xHCI interrupter enabled\n");
      printf("=== MSI-X ready - call STI to unmask ===\n");
      return;
    }

    cap_ptr = cap_next;
  }

  printf("ERROR: MSI-X capability not found in PCI config space\n");
}



// ============================================================================
// MAIN INITIALIZATION ENTRY POINT
// ============================================================================
void init_xhci_driver(void) {
  printf("MMIO Base: 0x%lx\n", xhci_dev.base_mmio);

  // Setup capability/operational register pointers
  xhci_dev.cap_regs = (XhciCapabilityRegs *)xhci_dev.base_mmio;
  uint64_t op_base = xhci_dev.base_mmio + xhci_dev.cap_regs->CapLength;
  xhci_dev.op_regs = (XhciOperationalRegs *)op_base;
  xhci_dev.max_ports = (xhci_dev.cap_regs->HcsParams1 >> 24) & 0xFF;

  xhci_dev.doorbell_regs = (volatile uint32_t *)(xhci_dev.base_mmio + xhci_dev.cap_regs->Dboff);

  printf("HCI Version: 0x%x\n", xhci_dev.cap_regs->HciVersion);
  printf("Max Ports: %d\n", xhci_dev.max_ports);
  printf("CapLength: 0x%x\n", xhci_dev.cap_regs->CapLength);
  printf("Operational Registers at: 0x%lx\n", op_base);
  printf("Doorbell Array at: 0x%lx\n", (uint64_t)xhci_dev.doorbell_regs);
  printf("\n");

  // Follow xHCI spec section 4.2 initialization order
  xhci_reset_controller();
  xhci_init_structures();
  xhci_setup_command_ring();
  xhci_setup_event_ring();
  xhci_start_controller();
  xhci_scan_ports();

}
