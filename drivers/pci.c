
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

int print_pci_list(void) {
  u8 bus, device, func;
  u32 data;

  u8 pci_bus = 0;
  u16 device_function;
  u32 device_number = 0;

  for(device_function = 0; device_function < 256; device_function++){
    u32 pci_id;

    pci_read_32(pci_bus,device_function,0,&pci_id);
    if( pci_id == PCI_ERROR )
      continue;

    device_number++;

    

    // FIXED BIT SHIFTS: Extract proper Device and Function numbers
    u8 device = (device_function >> 3) & 0x1F;
    u8 function = device_function & 0x07;
    u16 device_id = pci_id >> 16;
    u16 vendor_id = pci_id & 0xFFFF;

    printf("Device %d bus %d device %d function %d id: %x\n",\
        device_number,pci_bus,device,function,device_id);
  
    // Get PCI class, subclass, and programming interface (ProgIF)
    u32 register2;

    pci_read_32(pci_bus,device_function,PCI_REGISTER_2,&register2);

    u8 class = register2 >> 24;
    u8 sub_class = (register2 >> 16) & 0xFF;
    u8 prog_if = (register2 >> 8) & 0xFF; // Tells us if it's UHCI, EHCI, or XHCI



    if(class == PCI_CLASS_SERIAL_BUS && sub_class == PCI_SUBCLASS_USB_CONTROLLER){

      if(prog_if == PCI_INTERFACE_XHCI){
        PciDevice new_pci_device;
        new_pci_device.bus = pci_bus;
        new_pci_device.device_funtion = device_function;

        uint64_t xhci_base = xhci_get_base_address(new_pci_device);
      }



    }

    if(device_id == MY_USB_ID){
      printf("USB: bus %d device %d function %d id: %x\n",\
          device_number,pci_bus,device,function,device_id);
    }
  
    // Offset 0x0C contains Header Type in bits 16-23. 
    // If we are looking at function 0, check if it's a single-function device.
    if (function == 0) {
      u32 register3;
      pci_read_32(pci_bus, device_function, 0x0C, &register3);
      u8 header_type = (register3 >> 16) & 0xFF;
      
      // If bit 7 (0x80) is NOT set, this device has no functions 1-7. Skip them!
      if ((header_type & 0x80) == 0) {
          device_function += 7; // Fast-forward loop straight to next device
      }

    }
  
  }

  return 0;
}
