#include "xhci.h"
#include "../console.h"
#include "pci.h"


static u64 base_address_host_controller;

void xhci_set_base_address(u64 address){
  base_address_host_controller = address;
}

void pci_read_bar(u8 bar_number, u8 pci_bus, u16 device_function,  u32 out_bar){

      u32 bar0, bar1,bar0_copy, bar1_copy;

      pci_read_32(pci_bus,device_function,bar_number,&bar0_copy);
      pci_write_32(pci_bus,device_function,bar_number,0xFFFFFFFF);
      pci_read_32(pci_bus,device_function,bar_number,&bar0);

      pci_write_32(pci_bus,device_function,bar_number,bar0_copy);


      printf("BAR original %x\n",bar0_copy);
      bar0 = ~bar0+1;
      printf("BAR %x\n",bar0);

}

void xhci_get_base_address(PciDevice pci_device){

      u8 pci_bus = pci_device.bus;
      u16 device_function = pci_device.device_funtion;
    
      u16 device = device_function>>3;
      u8 function= device_function>>7;

      printf("USB Host controller: %d:%d:%d\n",pci_bus,device,function);

      u32 register2;
      pci_read_32(pci_bus,device_function,PCI_REGISTER_2,&register2);
      printf("Register 2 USB: %x\n",register2);

      u32 register3;
      pci_read_32(pci_bus,device_function,PCI_REGISTER_3,&register3);
      printf("Register 3 USB: %x\n",register3);


      //uint64_t base_host_controller = ((uint64_t)bar0 << 32) | bar1;
      uint64_t base_host_controller = (uint64_t)0;
     //   ((uint64_t)bar0 << 32);
      base_host_controller = (u64)(base_host_controller << 20);
      printf("Base USB host controller: %x\n",base_host_controller);
      xhci_set_base_address(base_host_controller);
      xhci_init();
}

void xhci_init(){
 printf("Base XHCI address %x\n",base_address_host_controller);
 u64 cap_length = *(u64*)base_address_host_controller; 
 printf("XHCI Cap lengh %x\n",cap_length);
}
