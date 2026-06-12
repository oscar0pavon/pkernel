
#include "pci.h"
#include "../types.h"
#include "../input_output.h"
#include "../console.h"
#include <stdint.h>

#include "xhci.h"

void pci_read_32(u8 bus, u8 device_function, u8 offset, u32* out) {
    // 1. Properly extract the independent sub-components
    u8 device   = (device_function >> 3) & 0x1F; // Top 5 bits
    u8 function = device_function & 0x07;        // Bottom 3 bits

    // 2. Build the hardware address matching the exact PCI register spec
    u32 address = PCI_ENABLE_BIT | 
                  ((u32)bus << 16) | 
                  ((u32)device << 11) | 
                  ((u32)function << 8) | 
                  (offset & 0xFC);

    output(address, PCI_CONFIG_ADDRESS);
    *out = input(PCI_CONFIG_DATA);
}

void pci_write_32(u8 bus, u8 device_function, u8 offset, u32 value) {
    u8 device   = (device_function >> 3) & 0x1F;
    u8 function = device_function & 0x07;

    u32 address = PCI_ENABLE_BIT | 
                  ((u32)bus << 16) | 
                  ((u32)device << 11) | 
                  ((u32)function << 8) | 
                  (offset & 0xFC);

    output(address, PCI_CONFIG_ADDRESS);
    output(value, PCI_CONFIG_DATA);
}


uint16_t read_pci_data16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint16_t tmp = 0;
  
    // Create configuration address as per Figure 1
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
  
    // Write out the address
    output(0xCF8, address);
    
    // Read in the data
    // (offset & 2) * 8) = 0 will choose the first word of the 32-bit register
    tmp = (uint16_t)((input(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    return tmp;
}
//Bit 31 	    Bits 30-24 	Bits 23-16 	Bits 15-11 	    Bits 10-8 	      Bits 7-0
//Enable Bit 	Reserved 	  Bus Number 	Device Number 	Function Number 	Register Offset
u32 read_pci_data(u8 bus, u8 device, u8 func, u8 pcireg) {

  u32 configuarion = PCI_ENABLE_BIT | (bus << 16) | (device << 11) | (func << 8) | (pcireg << 2);

  output(configuarion, PCI_CONFIG_ADDRESS);

  u32 ret = input(PCI_CONFIG_DATA);

  return ret;
}


void pci_enable_mmio(PciDevice dev) {
  uint32_t command_reg = 0;
  
  // 1. Read the current configuration state of the command register
  pci_read_32(dev.bus, dev.device_funtion, PCI_REG_COMMAND, &command_reg);
  
  // 2. Set the Memory Space and Bus Master bits using a bitwise OR (|)
  // We use |= so we don't overwrite other firmware-configured bits
  command_reg |= (PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER); 
  
  // 3. Write the updated tracking bits back to the hardware
  pci_write_32(dev.bus, dev.device_funtion, PCI_REG_COMMAND, command_reg);
}

int print_pci_list(void) {
  u8 bus, device, func;
  u32 data;

  u8 pci_bus = 0;
  u32 device_number = 0;

  u16 device_function;

  for (u8 device = 0; device < 32; device++) {
   
    for (u8 function = 0; function < 8; function++) { 
      
      u8 device_function_token = (device << 3) | function;

      u32 pci_id;

      pci_read_32(pci_bus, device_function_token, 0, &pci_id);
      
      // If the vendor ID is 0xFFFF or 0x0000, no hardware exists here
      if (pci_id == 0xFFFFFFFF || pci_id == 0x00000000 || pci_id == PCI_ERROR) {
          // If function 0 doesn't exist, the whole device doesn't exist. 
          // We can break the inner loop early to save time!
          if (function == 0) break; 
          continue;
      }

      device_number++;

      u16 device_id = pci_id >> 16;
      u16 vendor_id = pci_id & 0xFFFF;

      u32 register2;
      pci_read_32(pci_bus, device_function_token, 0x08, &register2);
      u8 class = register2 >> 24;
      u8 sub_class = (register2 >> 16) & 0xFF;
      u8 prog_if = (register2 >> 8) & 0xFF;

      printf("Found Dev %d: Bus %d, Slot %d, Func %d -> ID %x:%x, Class %x\n", 
             device_number, pci_bus, device, function, vendor_id, device_id, class);


      if(class == PCI_CLASS_SERIAL_BUS 
          && sub_class == PCI_SUBCLASS_USB_CONTROLLER){

        if(prog_if == PCI_INTERFACE_XHCI){
          PciDevice new_pci_device;
          new_pci_device.bus = pci_bus;
          new_pci_device.device_funtion = device_function;

          pci_enable_mmio(new_pci_device);

          uint64_t xhci_base = xhci_get_base_address2(new_pci_device);
          printf("--> True xHCI found at Address: 0x%x\n", xhci_base);

          init_xhci_driver(xhci_base);
        }

      }

    }

  }


  return 0;
}
