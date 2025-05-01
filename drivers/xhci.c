#include "xhci.h"
#include "../console.h"
#include "pci.h"

#define U32_MAX 0xFFFFFFFF

static u64 base_address_host_controller;

void xhci_set_base_address(u64 address){
  base_address_host_controller = address;
}


void xhci_get_base_address(PciDevice pci_device){

      u8 pci_bus = pci_device.bus;
      u16 device_function = pci_device.device_funtion;
    
      u16 device = device_function>>3;
      u8 function= device_function>>7;

      printf("USB Host controller: %d:%d:%d\n",pci_bus,device,function);

      u32 register1 = 0;//command byte
      pci_read_32(pci_bus,device_function,PCI_REGISTER_1,&register1);
      printf("Register 1 USB: %x\n",register1);


      //disable both I/O and memory decoding
      u32 commnad_byte = register1 & 0xFC;
      printf("Command byte: %x\n",commnad_byte);
      pci_write_32(pci_bus, device_function, PCI_REGISTER_1, commnad_byte);

      u32 bar0, bar1;

      pci_read_32(pci_bus,device_function,BAR0,&bar0);

      pci_read_32(pci_bus,device_function,BAR1,&bar1);
      
      u64 xhci_base = ((bar0 & 0xFFFFFFF0) + (((u64)bar1 & 0xFFFFFFFF) << 32));
     
      //restore decoding
      pci_write_32(pci_bus, device_function, PCI_REGISTER_1, register1);

      uint64_t base_host_controller = xhci_base;
      printf("Base USB host controller: %x\n",base_host_controller);
      xhci_set_base_address(base_host_controller);
      xhci_init();
}

void xhci_init(){
 printf("Base XHCI address %x\n",base_address_host_controller);
 u64 cap_length = *(u64*)base_address_host_controller; 
 printf("XHCI Cap lengh %x\n",cap_length);
}
