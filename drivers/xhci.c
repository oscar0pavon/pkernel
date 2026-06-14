#include "xhci.h"
#include "../console.h"
#include <stdint.h>


uint64_t xhci_base_mmio = 0;

static uint64_t xhci_base_address;
static char* xhci_operational_registers;
static char* xhci_runtime_registers;

// Track our position inside the circular command ring array
uint32_t current_trb_index = 0;
uint32_t current_cycle_state = 1; // xHCI rings MUST start with Cycle State = 1

// Global tracker to know where we are currently reading inside the event ring
// array
uint32_t event_ring_index = 0;
uint32_t event_ring_cycle = 1; // Expected initial cycle bit written by hardware

#define HCIVERSION 0x02
#define RTSOFF 0x18

// Allocate a 4KB chunk of raw physical memory aligned to a 64-byte boundary
// This will hold your primary command tracking loops
__attribute__((aligned(64))) uint8_t command_ring_buffer[4096];

// Allocate 64-byte aligned tracking structures in your kernel's RAM
__attribute__((aligned(4096))) volatile uint64_t dcbaap[64] = {0};
__attribute__((aligned(4096))) volatile uint32_t command_ring[256 * 4] = {
    0}; // 256 TRBs (each is 16 bytes/4 dwords)
__attribute__((aligned(4096))) volatile uint32_t event_ring[256 * 4] = {0};

__attribute__((aligned(4096))) struct EventRingSegmentEntry erst;


#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14

static uint32_t xhci_max_ports = 0;

void init_xhci_driver(uint64_t xhci_base) {
  // Force the pointers to point straight to your physical memory address space
  volatile struct XhciCapabilityRegs* cap_regs = (struct XhciCapabilityRegs*)xhci_base;
  
  // Calculate where the operational registers start using CapLength
  uint64_t op_base = xhci_base + cap_regs->CapLength;
  volatile struct XhciOperationalRegs* op_regs = (struct XhciOperationalRegs*)op_base;

  // --- Verification Test Loop ---
  printf("xHCI Version: %x\n", cap_regs->HciVersion);
  
  // Extract maximum number of physical ports supported by this controller
  xhci_max_ports = (cap_regs->HcsParams1 >> 24) & 0xFF;

  printf("Total USB Ports Available: %d\n", xhci_max_ports);

  // --- Perform a Hardware Reset to prepare for the keyboard ---
  // Set Bit 1 (Host Controller Reset) in the USB Command Register
  op_regs->UsbCmd |= (1 << 1);

  // Wait for the hardware to clear the reset bit automatically
  printf("Resetting xHCI Controller...\n");
  while (op_regs->UsbCmd & (1 << 1)) {
    // In a real system, add a timeout constraint here so it won't hang forever
    __asm__("pause"); 
  }

  printf("xHCI Reset Bit Cleared.\n");

  printf("Waiting for Controller to become Ready (CNR)...\n");

  while (op_regs->UsbSts & (1 << 11)) {
    __asm__("pause");
  }

  printf("xHCI Controller Ready for Configuration!\n");

  op_regs->UsbSts = 0xFFFFFFFF;

  // 4. Give the physical clock lines a brief moment to settle down completely
  for (volatile int delay = 0; delay < 10000000; delay++) {
    __asm__("pause");
  }

  printf("Running setup hardware\n");

  xhci_base_address = xhci_base;  

  setup_xhci_hardware(xhci_base, cap_regs, op_regs);
  
}

void poll_xhci_event_ring(uint64_t xhci_base,
                          volatile struct XhciCapabilityRegs *cap_regs,
                          volatile struct XhciOperationalRegs* op_regs) {
  // 1. Locate Interrupter 0 inside the Runtime Registers space
  uint64_t rt_base = xhci_base + cap_regs->Rtsoff;
  volatile struct XhciInterrupterRegs *int_0 =
      (volatile struct XhciInterrupterRegs *)(rt_base + 0x20);

  // Cast our raw event ring buffer array to the structured event layout
  volatile struct XhciEventTRB *ring =
      (volatile struct XhciEventTRB *)&event_ring;

  printf("Waiting for xHCI Command Completion Event...\n");

  uint32_t diagnostic_timer = 0;

  // 2. Poll the memory slot until the hardware writes the matching cycle bit
  while (1) {
    uint32_t control = ring[event_ring_index].Control;
    uint32_t hardware_cycle_bit = control & 0x01;

    // If the hardware cycle bit matches our expected cycle state, an event is
    // ready!
    if (hardware_cycle_bit == event_ring_cycle) {

      // Extract the TRB Type (Bits 10-15)
      uint8_t trb_type = (control >> 10) & 0x3F;

      if (trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
        // Extract Completion Code (Bits 24-31 of Status). 1 means Success!
        uint8_t completion_code = (ring[event_ring_index].Status >> 24) & 0xFF;

        // Extract the assigned Slot ID (Bits 24-31 of Control register)
        uint8_t assigned_slot_id = (control >> 24) & 0xFF;

        printf("--> Event Received!\n");
        printf("    Completion Code: %d (1 = Success)\n", completion_code);
        printf("    Assigned Slot ID for Keyboard: %d\n", assigned_slot_id);

        // Keep track of this assigned_slot_id globally so your keyboard driver
        // can use it later!

        // 3. Update our local index tracker
        event_ring_index++;
        if (event_ring_index >=
            256) { // Matching your event_ring allocation size
          event_ring_index = 0;
          event_ring_cycle =
              !event_ring_cycle; // Invert expected cycle bit on wrap
        }

        // 4. CRITICAL MANDATORY STEP: Update the ERDP register in the hardware
        // Calculate the physical memory address of the next processed slot
        uint64_t next_dequeue_pointer = (uint64_t)&ring[event_ring_index];
        next_dequeue_pointer &= 0xFFFFFFFFFFFFFFF0ULL;

        // Bit 3 MUST be written as 1 to clear the Event Handler Busy flag (EHB)
        int_0->Erdp = next_dequeue_pointer | (1 << 3);

        break; // Exit the poll loop successfully
      }
    }

    // Diagnostic helper if the loop stays stuck
    diagnostic_timer++;
    if (diagnostic_timer > 80000000) {
      diagnostic_timer = 0;
      uint32_t status = op_regs->UsbSts;
      printf("Polling... Live USBSTS Register: 0x%x\n", status);

      // Bit 2 is HSE (Host System Error)
      if (status& (1 << 12)) {
        printf("Hardware Halted (HCE). Raw control word at index: 0x%x\n",
               control);
      }
      // Freeze here so you can read the dump
    }

    __asm__("pause"); // Prevent CPU overheating while waiting
  }
}

volatile uint32_t *
get_xhci_doorbell_reg(uint64_t xhci_base,
                      volatile struct XhciCapabilityRegs *cap_regs,
                      uint32_t doorbell_index) {
  // Dboff gives the byte offset from the xHCI base address where the doorbells
  // start
  uint64_t doorbell_base = xhci_base + cap_regs->Dboff;

  // Each doorbell register is exactly 4 bytes apart
  return (volatile uint32_t *)(doorbell_base + (doorbell_index * 4));
}

void xhci_send_command(uint64_t xhci_base,
                       volatile struct XhciCapabilityRegs *cap_regs,
                       uint32_t trb_type) {
  // Typecast your generic array memory block to our explicit TRB structure
  // mapping
  struct XhciTRB *ring = (struct XhciTRB *)&command_ring;

  // 1. Pack the Control bits matching the xHCI specification:
  // Bits 0: Cycle State (tells hardware this TRB is valid)
  // Bits 10-15: TRB Type
  uint32_t control_bits = (trb_type << 10) | current_cycle_state;

  // 2. Write the values directly into our RAM ring slot
  ring[current_trb_index].Parameter =
      0; // Enable Slot doesn't need a parameter pointer
  ring[current_trb_index].Status = 0;
  ring[current_trb_index].Control = control_bits;

  printf("Posting TRB Type %d to Command Ring index %d...\n", trb_type,
         current_trb_index);

  // 3. Increment our array tracker for the next command execution
  current_trb_index++;
  if (current_trb_index >= 256) { // If we reach the end of your 256-sized array
    current_trb_index = 0;
    // In a complex driver, you would write a 'Link TRB' here to loop back the
    // ring hardware
  }

  // 4. RING THE DOORBELL!
  // Doorbell 0 controls the Host Controller Command processor.
  // Writing 0 to the register maps to "Target 0", meaning "Execute Command
  // Ring".
  volatile uint32_t *db_0 = get_xhci_doorbell_reg(xhci_base, cap_regs, 0);
  *db_0 = 0;

  printf("Doorbell 0 rung successfully!\n");
}

void scan_xhci_ports(volatile struct XhciCapabilityRegs *cap_regs,
                     volatile struct XhciOperationalRegs *op_regs) {
  // Extract the total number of physical ports from structural parameters 1
  uint32_t max_ports = (cap_regs->HcsParams1 >> 24) & 0xFF;
  printf("Scanning %d physical ports for connected devices...\n", max_ports);

  for (uint32_t p = 0; p < max_ports; p++) {
    // Read the raw 32-bit PORTSC register for the current port index
    uint32_t port_status = op_regs->PortRegisterSet[p].PortSc;

    // Bit 0 == Current Connect Status (CCS)
    if (port_status & (1 << 0)) {
      printf("--> Device detected on Port %d! (Raw Status: %x)\n", p + 1,
             port_status);

      // Extract Port Speed (Bits 10-13) to see if it's USB 1, 2, or 3
      uint8_t speed = (port_status >> 10) & 0x0F;
      printf("    Port Speed Identifier: %d\n", speed);

      // Trigger a Port Reset sequence to wake up the keyboard logic hardware
      // Set Bit 4 (Port Reset) to 1
      uint32_t reset_command = port_status & ~0x007E0000;
      reset_command |= (1 << 4);
      op_regs->PortRegisterSet[p].PortSc = reset_command;

      // Wait for the hardware to clear the Reset bit and set Bit 1 (Connect
      // Status Change)
      while (op_regs->PortRegisterSet[p].PortSc & (1 << 4)) {
        __asm__("pause");
      }
      printf("    Port %d Reset complete. Device initialized.\n", p + 1);

      // Send an Enable Slot Command to request a Slot ID for the keyboard on Port 5
      xhci_send_command(xhci_base_address, cap_regs, TRB_TYPE_ENABLE_SLOT);

      poll_xhci_event_ring(xhci_base_address, cap_regs, op_regs);

    }
  }
}


void setup_xhci_hardware(uint64_t xhci_base, 
    volatile struct XhciCapabilityRegs* cap_regs, 
    volatile struct XhciOperationalRegs* op_regs) {

  // 1. Calculate Runtime Registers position
  volatile struct XhciRuntimeRegs *rt_regs =
      (struct XhciRuntimeRegs *)(xhci_base + cap_regs->Rtsoff);

  volatile struct XhciInterrupterRegs* int_0 = &rt_regs->Interrupter[0];

  // 2. Clear the Controller Configuration (Stops all processing)
  op_regs->Config = 0;

  op_regs->UsbSts = 0xFFFFFFFF;

  // 3. Set the DCBAAP address pointer
  op_regs->Dcbaap = (uint64_t)(uintptr_t)&dcbaap;

  // 4. Initialize and set the Command Ring Pointer
  // Bit 0 is the "Cycle State" bit. Set it to 1 to tell xHCI the ring is ready.
  uint64_t crcr_val = (uint64_t)(uintptr_t)&command_ring | 1; 
  op_regs->Crcr = crcr_val;

  // 5. Configure the Event Ring Segment Table (ERST)
  erst.RingSegmentBaseAddress = (uint64_t)(uintptr_t)&event_ring;
  erst.RingSegmentSize = 256; // Matching our allocation size
  erst.Reserved = 0;

  // 6. Hook ERST into Interrupter 0
  int_0->Erstsz = 1;               // We are using exactly 1 segment
  int_0->Erstba = (uint64_t)(uintptr_t)&erst; // Point to the segment definition block


  uint64_t erdp_val = (uint64_t)(uintptr_t)&event_ring;
  erdp_val &= 0xFFFFFFFFFFFFFFF0ULL;
  int_0->Erdp = erdp_val | (1 << 3);

  // 7. Enable the Interrupter (Disable interrupts if you don't use an IDT yet)
  // Turning on IMAN bit 1 turns on system assertions, bit 0 clears pending flags
  
  int_0->Iman = (1 << 0); 

  // 8. Determine how many hardware slots to turn on
  uint32_t max_slots = cap_regs->HcsParams1 & 0xFF; 
  op_regs->Config = 1; // Turn on all available device connection tracks

  // 9. START THE CONTROLLER!
  // Set Bit 0 (Run/Stop) in the USB Command Register to 1
  op_regs->UsbCmd |= (1 << 0);

  // Wait until the HCHalted bit (Bit 0) in USB Status drops to 0
  while (op_regs->UsbSts & (1 << 0)) {
      __asm__("pause");
  }

  printf("xHCI Controller is fully RUNNING and monitoring USB ports!\n");

  scan_xhci_ports(cap_regs, op_regs);
}

