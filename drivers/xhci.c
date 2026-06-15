#include "xhci.h"
#include "../console.h"
#include <stdint.h>

#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14

#define HCIVERSION 0x02
#define RTSOFF 0x18

XHCIDevice xhci_dev = {0};

uint64_t xhci_base_mmio = 0;

static char* xhci_operational_registers;
static char* xhci_runtime_registers;

// Track our position inside the circular command ring array
uint32_t current_trb_index = 0;
uint32_t current_cycle_state = 1; // xHCI rings MUST start with Cycle State = 1

// Global tracker to know where we are currently reading inside the event ring
// array
uint32_t event_ring_index = 0;
uint32_t event_ring_cycle = 1; // Expected initial cycle bit written by hardware


// Allocate a 4KB chunk of raw physical memory aligned to a 64-byte boundary
// This will hold your primary command tracking loops
__attribute__((aligned(64))) uint8_t command_ring_buffer[4096];

#define aligned4k __attribute__((aligned(4096)))

// Allocate 64-byte aligned tracking structures in your kernel's RAM
aligned4k volatile uint64_t dcbaap[64] = {0};
// 256 TRBs (each is 16 bytes/4 dwords)
aligned4k volatile uint32_t command_ring[256 * 4] = {0};
aligned4k volatile uint32_t event_ring[256 * 4] = {0};

aligned4k struct EventRingSegmentEntry erst;

static uint32_t xhci_max_ports = 0;

void init_xhci_driver(void) {

  // Force the pointers to point straight to 
  // your physical memory address space
  //
  xhci_dev.cap_regs = (XhciCapabilityRegs*)xhci_base_mmio;
  
  // Calculate where the operational registers start using CapLength
  uint64_t op_base = xhci_base_mmio + xhci_dev.cap_regs->CapLength;

  xhci_dev.op_regs = (XhciOperationalRegs*)op_base;

  xhci_max_ports = (xhci_dev.cap_regs->HcsParams1 >> 24) & 0xFF;

  printf("Total USB Ports Available: %d\n", xhci_max_ports);

  // Wait for the hardware to clear the reset bit automatically
  printf("Resetting xHCI Controller...\n");
  // Set Bit 1 (Host Controller Reset) in the USB Command Register
  xhci_dev.op_regs->UsbCmd |= (1 << 1); // Set Bit 1 (HCRST)
                               //

  while (xhci_dev.op_regs->UsbSts & (1 << 11)) {
    __asm__("pause");
  }


  printf("Waiting for Controller to become Ready (CNR)...\n");
  for (volatile uint64_t delay = 0; delay < 50000000ULL; delay++) {
    __asm__("pause");
  }

  xhci_dev.pci_regs[1] |= (1 << 1) | (1 << 2);
  
  xhci_dev.op_regs->UsbSts = 0xFFFFFFFF;

  test_xhci_dma_identity();

  //setup_xhci_hardware(cap_regs, op_regs);
  
}
void poll_xhci_event_ring(volatile XhciCapabilityRegs *cap_regs,
                          volatile XhciOperationalRegs *op_regs) {

  uint64_t rt_base = xhci_base_mmio + cap_regs->Rtsoff;

  volatile struct XhciInterrupterRegs *int_0 =
      (volatile struct XhciInterrupterRegs *)(rt_base + 0x20);

  // CRITICAL FIX: Cast directly onto our physical event ring allocation space!
  volatile struct XhciEventTRB *ring = (volatile struct XhciEventTRB *)0x2002000;

  printf("Waiting for xHCI Command Completion Event...\n");
  uint32_t diagnostic_timer = 0;

  while (1) {
    uint32_t control = ring[event_ring_index].Control;
    uint32_t hardware_cycle_bit = control & 0x01;

    if (hardware_cycle_bit == event_ring_cycle) {
      uint8_t trb_type = (control >> 10) & 0x3F;

      if (trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
        uint8_t completion_code = (ring[event_ring_index].Status >> 24) & 0xFF;
        uint8_t assigned_slot_id = (control >> 24) & 0xFF;

        printf("--> Event Received!\n");
        printf("    Completion Code: %d (1 = Success)\n", completion_code);
        printf("    Assigned Slot ID for Keyboard: %d\n", assigned_slot_id);

        event_ring_index++;
        if (event_ring_index >= 256) { 
          event_ring_index = 0;
          event_ring_cycle = !event_ring_cycle; 
        }

        uint64_t next_dequeue_pointer = (uint64_t)&ring[event_ring_index];
        next_dequeue_pointer &= 0xFFFFFFFFFFFFFFF0ULL;

        int_0->Erdp = next_dequeue_pointer | (1 << 3);
        break; 
      }
    }

    diagnostic_timer++;
    if (diagnostic_timer > 80000000) {
      diagnostic_timer = 0;
      uint32_t status = op_regs->UsbSts;
      printf("Polling... Live USBSTS Register: 0x%x\n", status);

      if (status & (1 << 2))  printf("Host System Error (HSE)!\n");
      if (status & (1 << 12)) printf("Host Controller Error (HCE)!\n");
      if (status & (1 << 0))  printf("Controller Halted!\n");
    }
    __asm__("pause"); 
  }
}

volatile uint32_t *
get_xhci_doorbell_reg(volatile XhciCapabilityRegs *cap_regs,
                      uint32_t doorbell_index) {
  // Dboff gives the byte offset from the xHCI base address where the doorbells
  // start
  uint64_t doorbell_base = xhci_base_mmio + cap_regs->Dboff;

  // Each doorbell register is exactly 4 bytes apart
  return (volatile uint32_t *)(doorbell_base + (doorbell_index * 4));
}

void xhci_send_command(uint64_t xhci_base,
                       volatile struct XhciCapabilityRegs *cap_regs,
                       uint32_t trb_type) {
  // CRITICAL FIX: Target our active 32MB physical memory slot instead of the .bss reference!
  uint64_t cmd_ring_phys = 0x2001000;
  struct XhciTRB *ring = (struct XhciTRB *)cmd_ring_phys;

  // Enforce a stable Link TRB at the end of our 256-sized hardware tracking block
  ring[255].Parameter = cmd_ring_phys; // Points back to index 0
  ring[255].Status = 0;
  // Bit 1 = TC (Toggle Cycle) bit. This is mandatory for the controller to loop correctly.
  ring[255].Control = (TRB_TYPE_LINK << 10) | (1 << 1) | current_cycle_state; 

  uint32_t control_bits = (trb_type << 10) | current_cycle_state;

  ring[current_trb_index].Parameter = 0;
  ring[current_trb_index].Status = 0;
  ring[current_trb_index].Control = control_bits;

  printf("Posting TRB Type %d to Command Ring index %d...\n", trb_type, current_trb_index);

  current_trb_index++;
  if (current_trb_index >= 255) { // Leave index 255 reserved for our Link TRB!
    current_trb_index = 0;
    current_cycle_state ^= 1; // Toggle tracking cycle bits on wrap pass
  }

  volatile uint32_t *db_0 = get_xhci_doorbell_reg(cap_regs, 0);
  *db_0 = 0; // Target 0 = Command Ring execution line

  printf("Doorbell 0 rung successfully!\n");
}


void scan_xhci_ports(volatile XhciCapabilityRegs *cap_regs,
                     volatile XhciOperationalRegs *op_regs) {

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
      xhci_send_command(xhci_base_mmio, cap_regs, TRB_TYPE_ENABLE_SLOT);

      poll_xhci_event_ring(cap_regs, op_regs);

    }
  }
}

void setup_xhci_hardware(void) {


}

void test_xhci_dma_identity(void) {
  // 1. Point op_regs->Dcbaap to our global array using its compiler virtual
  // address We place a recognizable magic signature right inside index 0
  dcbaap[0] = 0x1122334455667788ULL;

  // 2. Write the address of our array directly into the xHCI hardware register
  xhci_dev.op_regs->Dcbaap = (uint64_t)&dcbaap;

  // 3. Forcibly read the register BACK from the xHCI chip hardware lines
  uint64_t hardware_read_address = xhci_dev.op_regs->Dcbaap;

  // 4. Now, look at the physical memory address the chip is actually holding
  volatile uint64_t *chip_target_memory =
      (volatile uint64_t *)hardware_read_address;

  printf("\n--- xHCI HARDWARE DMA CHECK ---\n");
  printf("Compiler Symbol Address (&dcbaap): 0x%lx\n", (uint64_t)&dcbaap);
  printf("Register Readback Address:        0x%lx\n", hardware_read_address);
  printf("Value inside Chip Memory Target:  0x%lx\n", *chip_target_memory);

  if (*chip_target_memory == 0x1122334455667788ULL) {
    printf("SUCCESS: The xHCI hardware and your CPU see the exact same RAM "
           "bytes!\n");
  } else {
    printf("FAIL: The xHCI hardware is looking at a different physical RAM "
           "cell!\n");
  }
  printf("--------------------------------\n");
}
