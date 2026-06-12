#include "xhci.h"
#include "../console.h"
#include "pci.h"
#include <stdint.h>


#define PCI_BAR0_OFFSET 0x10
#define PCI_BAR1_OFFSET 0x14

#define U32_MAX 0xFFFFFFFF

static char* base_address_host_controller;
static char* xhci_operational_registers;
static char* xhci_runtime_registers;


#define HCIVERSION 0x02
#define RTSOFF 0x18

// Allocate a 4KB chunk of raw physical memory aligned to a 64-byte boundary
// This will hold your primary command tracking loops
__attribute__((aligned(64))) uint8_t command_ring_buffer[4096];

// Allocate 64-byte aligned tracking structures in your kernel's RAM
__attribute__((aligned(64))) uint64_t dcbaap[64] = {0};
__attribute__((aligned(64))) uint32_t command_ring[256 * 4] = {0}; // 256 TRBs (each is 16 bytes/4 dwords)
__attribute__((aligned(64))) uint32_t event_ring[256 * 4] = {0};

__attribute__((aligned(64))) struct EventRingSegmentEntry erst;

void xhci_set_base_address(u64 address){
  base_address_host_controller = (char*)address;
}

#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14

uint64_t xhci_get_base_address2(PciDevice dev) {
    uint32_t bar0 = 0;
    uint32_t bar1 = 0;

    // 1. Read BOTH consecutive registers
    pci_read_32(dev.bus, dev.device_funtion, PCI_REG_BAR0, &bar0);
    pci_read_32(dev.bus, dev.device_funtion, PCI_REG_BAR1, &bar1);

    // 2. Clear out the lower 4 status attribute bits from BAR0 
    // Bit 0 = Memory/IO flag, Bits 1-2 = 64-bit flag
    uint64_t physical_address = (bar0 & 0xFFFFFFF0);

    // 3. Check if bits 1 and 2 indicate this is a 64-bit address layout
    // If (bar0 & 0x06) == 0x04, then BAR1 holds the upper 32 bits of the address!
    if ((bar0 & 0x06) == 0x04) {
        physical_address |= ((uint64_t)bar1 << 32);
    }

    // 4. FAIL-SAFE: If the computer firmware truly left it unassigned (0)
    // We force write our safe bare-metal 32-bit MMIO region.
    // Real computer chipsets accept this perfectly if Bus Mastering is turned on.
    if (physical_address == 0) {
        pci_write_32(dev.bus, dev.device_funtion, PCI_REG_BAR1, 0x00000000);
        pci_write_32(dev.bus, dev.device_funtion, PCI_REG_BAR0, 0xFE000000);
        
        physical_address = 0xFE000000;
    }

    return physical_address;
}


uint64_t xhci_get_base_address(PciDevice dev){
  
  // We clear BAR1 first, then write the target address to BAR0
  pci_write_32(dev.bus, dev.device_funtion, PCI_BAR0_OFFSET, 0x00000000);
  pci_write_32(dev.bus, dev.device_funtion, PCI_BAR1_OFFSET, 0xFE000000);


  uint32_t bar0 = 0;
  uint32_t bar1 = 0;

  // 1. Read BAR0 (Offset 0x10) and BAR1 (Offset 0x14)
  pci_read_32(dev.bus, dev.device_funtion, PCI_BAR0_OFFSET, &bar0);
  pci_read_32(dev.bus, dev.device_funtion, PCI_BAR1_OFFSET, &bar1);

  // 2. Check if this is actually a Memory-Mapped BAR (Bit 0 must be 0)
  if (bar0 & 0x1) {
      printf("Error: xHCI BAR0 is I/O mapped, expected Memory-Mapped.\n");
      return 0;
  }

  // 3. Check if it's a 64-bit address (Bits 1 and 2 must equal 2, meaning 0x4)
  uint64_t xhci_base_mmio = 0;
  if ((bar0 & 0x6) == 0x4) {
      // 64-bit address: Combine BAR0 and BAR1, clearing the lower 4 attribute bits
      xhci_base_mmio = ((uint64_t)(bar1) << 32) | (bar0 & 0xFFFFFFF0);
  } else {
      // 32-bit address: Just use BAR0, clearing the lower 4 attribute bits
      xhci_base_mmio = (bar0 & 0xFFFFFFF0);
  }
  printf("base test %x\n",0xFE000000);

  printf("xHCI Controller found! MMIO Base Address: %x\n", xhci_base_mmio);

  // printf("USB Host controller: %d:%d:%d\n",pci_bus,device,function);
  //
  // printf("Base USB host controller: %x\n",base_host_controller);
  //
  return xhci_base_mmio;
}

void init_xhci_driver(uint64_t xhci_base) {
  // Force the pointers to point straight to your physical memory address space
  struct XhciCapabilityRegs* cap_regs = (struct XhciCapabilityRegs*)xhci_base;
  
  // Calculate where the operational registers start using CapLength
  uint64_t op_base = xhci_base + cap_regs->CapLength;
  struct XhciOperationalRegs* op_regs = (struct XhciOperationalRegs*)op_base;

  // --- Verification Test Loop ---
  printf("xHCI Version: %x\n", cap_regs->HciVersion);
  
  // Extract maximum number of physical ports supported by this controller
  uint32_t max_ports = (cap_regs->HcsParams1 >> 24) & 0xFF;
  printf("Total USB Ports Available: %d\n", max_ports);

  // --- Perform a Hardware Reset to prepare for the keyboard ---
  // Set Bit 1 (Host Controller Reset) in the USB Command Register
  op_regs->UsbCmd |= (1 << 1);

  // Wait for the hardware to clear the reset bit automatically
  printf("Resetting xHCI Controller...\n");
  while (op_regs->UsbCmd & (1 << 1)) {
      // In a real system, add a timeout constraint here so it won't hang forever
      __asm__("pause"); 
  }
  printf("xHCI Controller Ready for Setup!\n");

  setup_xhci_hardware(xhci_base, cap_regs, op_regs);
  
}


void setup_xhci_hardware(uint64_t xhci_base, struct XhciCapabilityRegs* cap_regs, struct XhciOperationalRegs* op_regs) {
  // 1. Calculate Runtime Registers position
  struct XhciRuntimeRegs* rt_regs = (struct XhciRuntimeRegs*)(xhci_base + cap_regs->Rtsoff);
  struct XhciInterrupterRegs* int_0 = &rt_regs->Interrupter[0];

  // 2. Clear the Controller Configuration (Stops all processing)
  op_regs->Config = 0;

  // 3. Set the DCBAAP address pointer
  op_regs->Dcbaap = (uint64_t)&dcbaap;

  // 4. Initialize and set the Command Ring Pointer
  // Bit 0 is the "Cycle State" bit. Set it to 1 to tell xHCI the ring is ready.
  uint64_t crcr_val = (uint64_t)&command_ring | 1; 
  op_regs->Crcr = crcr_val;

  // 5. Configure the Event Ring Segment Table (ERST)
  erst.RingSegmentBaseAddress = (uint64_t)&event_ring;
  erst.RingSegmentSize = 256; // Matching our allocation size
  erst.Reserved = 0;

  // 6. Hook ERST into Interrupter 0
  int_0->Erstsz = 1;               // We are using exactly 1 segment
  int_0->Erstba = (uint64_t)&erst; // Point to the segment definition block
  int_0->Erdp = (uint64_t)&event_ring; // Set current read pointer to the start

  // 7. Enable the Interrupter (Disable interrupts if you don't use an IDT yet)
  // Turning on IMAN bit 1 turns on system assertions, bit 0 clears pending flags
  int_0->Iman |= (1 << 0) | (1 << 1); 

  // 8. Determine how many hardware slots to turn on
  uint32_t max_slots = cap_regs->HcsParams1 & 0xFF; 
  op_regs->Config = max_slots; // Turn on all available device connection tracks

  // 9. START THE CONTROLLER!
  // Set Bit 0 (Run/Stop) in the USB Command Register to 1
  op_regs->UsbCmd |= (1 << 0);

  // Wait until the HCHalted bit (Bit 0) in USB Status drops to 0
  while (op_regs->UsbSts & (1 << 0)) {
      __asm__("pause");
  }

  printf("xHCI Controller is fully RUNNING and monitoring USB ports!\n");
}


void xhci_init(){
 printf("Base XHCI address %x\n",base_address_host_controller);
 u8 cap_length = *base_address_host_controller; 

 printf("XHCI Capability Register Length %d bytes\n",cap_length);
 
 char* base_address = base_address_host_controller;
 u16 version = *(base_address+HCIVERSION);
 printf("XHCI version %x\n",version);

 u32 runtime_offset = *(base_address+RTSOFF);
 
 printf("XHCI Runtime Register offset %d bytes\n",runtime_offset);

 xhci_runtime_registers = base_address+runtime_offset;
 
 printf("XHCI Runtime Register memory %x bytes\n",xhci_runtime_registers);


 
}
