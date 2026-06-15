#include "xhci.h"
#include "../console.h"
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

// DMA receive buffer for USB descriptors
aligned4k volatile uint8_t descriptor_buffer[64] = {0};

XHCIDevice xhci_dev = {0};

// Tracking state for command/event rings
uint32_t command_ring_enqueue = 0;
uint32_t command_ring_cycle = 1;

uint32_t event_ring_dequeue = 0;
uint32_t event_ring_cycle = 1;

uint32_t ep0_enqueue = 0;
uint32_t ep0_cycle   = 1;

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
  struct XhciTRB *link_trb = &command_ring[255];

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
void xhci_get_descriptor(uint32_t slot_id, uint8_t desc_type, uint16_t length) {
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
    return;
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

  xhci_get_descriptor(slot_id, USB_DESC_TYPE_DEVICE, 18);
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
