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
