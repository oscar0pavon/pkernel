#include "xhci.h"
#include "../console.h"
#include "pci.h"


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

void xhci_init(){
 printf("Base address %x\n",base_address_host_controller);
 u64 cap_length = *(u64*)base_address_host_controller; 
 printf("Cap lengh %x\n",cap_length);
}
