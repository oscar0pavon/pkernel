#include "xhci.h"
#include "../console.h"
#include "pci.h"

#define U32_MAX 0xFFFFFFFF

static u64 base_address_host_controller;

void xhci_set_base_address(u64 address){
  base_address_host_controller = address;
}

void pci_read_bar_64(u8 bar_number, u8 pci_bus, u16 device_function,  u64* out_bar){

      u32 bar0, bar1, bar0_copy, bar1_copy;

      pci_read_32(pci_bus,device_function,BAR0,&bar0_copy);
      pci_read_32(pci_bus,device_function,BAR1,&bar1_copy);

      pci_write_32(pci_bus,device_function,BAR0,0xFFFFFFFF);
      pci_write_32(pci_bus,device_function,BAR1,0xFFFFFFFF);

      bar0 = U32_MAX;
      bar1 = U32_MAX;
      pci_read_32(pci_bus,device_function,BAR0,&bar0);
      pci_write_32(pci_bus,device_function,BAR0,bar0_copy);

      pci_read_32(pci_bus,device_function,BAR1,&bar1);
      pci_write_32(pci_bus,device_function,BAR1,bar1_copy);
      

      printf("BAR0 %x\n",bar0);
      printf("BAR1 %x\n",bar1);

      //pci_write_32(pci_bus,device_function,bar_number,bar0_copy);


      //printf("BAR original %x\n",bar0_copy);
      //bar0 = ~bar0+1;

      u64 address = 0;

      
      address = ((bar0 & 0xFFFFFFF0) + (((u64)bar1 & 0xFFFFFFFF) << 32));
      
      *out_bar = address;

      //size of BAR 64bits
      printf("Address mode 1 %x\n",address);
      address = ~address;
      printf("Address inverted %x\n",address);

      address = address + 1;
      printf("Address plus 1 %x\n",address);

      //address = (bar1 << 32) | bar1;
      //uint64_t base_host_controller = ((uint64_t)bar0 << 32) | bar1;



}
void pci_read_bar(u8 bar_number, u8 pci_bus, u16 device_function,  u32* out_bar){

      u32 bar0, bar1, bar0_copy, bar1_copy;

      pci_read_32(pci_bus,device_function,bar_number,&bar0_copy);
      pci_write_32(pci_bus,device_function,bar_number,0xFFFFFFFF);
      pci_read_32(pci_bus,device_function,bar_number,&bar0);

      pci_write_32(pci_bus,device_function,bar_number,bar0_copy);


      printf("BAR original %x\n",bar0_copy);
      //bar0 = ~bar0+1;
      printf("BAR %x\n",bar0);

      *out_bar = bar0;

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


      u64 xhci_base;
      pci_read_bar_64(BAR0,pci_bus,device_function,&xhci_base);
      
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
