
#include "pci.h"
#include "../types.h"
#include "../input_output.h"
#include "../console.h"
#include <stdint.h>

#include "xhci.h"

const u32 PCI_ENABLE_BIT     = 0x80000000;
const u32 PCI_CONFIG_ADDRESS = 0xCF8;
const u32 PCI_CONFIG_DATA    = 0xCFC;

#define PCI_ERROR 0xFFFFFFFF

#define PCI_REGISTER_2 0x8
#define PCI_REGISTER_3 0xC

#define PCI_CLASS_SERIAL_BUS 0xC
#define PCI_SUBCLASS_USB_CONTROLLER 0x3

#define MY_USB_ID 0x7a60

#define BAR0 0x10
#define BAR1 0x14


void pci_read_32(u8 bus, u8 device_function, u8 offset, u32* out){
  u32 address = (PCI_ENABLE_BIT | (bus<<16) | (device_function<<8) | (offset & 0xfc));
  output(address,PCI_CONFIG_ADDRESS);
  *out = input(PCI_CONFIG_DATA);
}

void pci_write_32(u8 bus, u8 device_function, u8 offset, u32 value){
  u32 address = (PCI_ENABLE_BIT | (bus<<16) | (device_function<<8) | (offset & 0xfc));
  output(address,PCI_CONFIG_ADDRESS);
  output(value,PCI_CONFIG_DATA);
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

    //TODO: scan other buses
    pci_read_32(pci_bus,device_function,0,&pci_id);
    if( pci_id == PCI_ERROR )
      continue;

    device_number++;
    u16 device = device_function>>3;
    u8 function= device_function>>7;
    u16 device_id = pci_id>>16;
    printf("Device %d bus %d device %d function %d id: %x\n",\
        device_number,pci_bus,device,function,device_id);
  
    u32 register2;
    pci_read_32(pci_bus,device_function,PCI_REGISTER_2,&register2);
    u8 class = register2>>24;
    u8 sub_class = register2>>16;
    if(class == PCI_CLASS_SERIAL_BUS && sub_class == PCI_SUBCLASS_USB_CONTROLLER){
      printf("USB Host controller: %d:%d:%d\n",bus,device,function);
      u32 register2;
      pci_read_32(pci_bus,device_function,PCI_REGISTER_2,&register2);
      printf("Register 2 USB: %x\n",register2);
      u32 register3;
      pci_read_32(pci_bus,device_function,PCI_REGISTER_3,&register3);
      printf("Register 3 USB: %x\n",register3);
      u32 bar0;
      u32 bar0_copy;
      u32 bar1_copy;
      u32 bar1;
      pci_read_32(pci_bus,device_function,BAR0,&bar0_copy);
      printf("BAR0 first read %x\n",bar0_copy);
      pci_write_32(pci_bus,device_function,BAR0,0xFFFFFFFF);
      pci_read_32(pci_bus,device_function,BAR0,&bar0);
      bar0 = ~bar0+1;
      printf("BAR0 %x\n",bar0);
      pci_write_32(pci_bus,device_function,BAR0,bar0_copy);


      pci_read_32(pci_bus,device_function,BAR1,&bar1_copy);
      printf("BAR1 first read %x\n",bar1_copy);
      pci_write_32(pci_bus,device_function,BAR1,0xFFFFFFFF);
      pci_read_32(pci_bus,device_function,BAR1,&bar1);
      pci_write_32(pci_bus,device_function,BAR1,bar1_copy);
      bar1 = ~bar1+1;
      printf("BAR1 %x\n",bar1);

      //uint64_t base_host_controller = ((uint64_t)bar0 << 32) | bar1;
      uint64_t base_host_controller = (uint64_t)bar0;
     //   ((uint64_t)bar0 << 32);
      base_host_controller = (u64)(base_host_controller << 20);
      printf("Base host: %x\n",base_host_controller);
      xhci_set_base_address(base_host_controller);
      xhci_init();


    }

    if(device_id == MY_USB_ID){
      printf("USB: bus %d device %d function %d id: %x\n",\
          device_number,pci_bus,device,function,device_id);
    }
  
    if(function==0){
      u32 address;
      pci_read_32(pci_bus,device_function,PCI_REGISTER_3,&address);//register 3 -> 0xC offset
      if(address == 0x00800000){//i don't know what is this value
        device_function +=7;
      }
    }

  }


  return 0;
}
