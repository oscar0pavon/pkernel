#include "xhci.h"
#include "../console.h"
#include "pci.h"

#define U32_MAX 0xFFFFFFFF

static char* base_address_host_controller;
static char* xhci_operational_registers;
static char* xhci_runtime_registers;


#define HCIVERSION 0x02
#define RTSOFF 0x18


void xhci_set_base_address(u64 address){
  base_address_host_controller = (char*)address;
}


void xhci_get_base_address(PciDevice pci_device){

      u8 pci_bus = pci_device.bus;
      u16 device_function = pci_device.device_funtion;
    
      u16 device = device_function>>3;
      u8 function= device_function>>7;

      printf("USB Host controller: %d:%d:%d\n",pci_bus,device,function);

      u32 register1 = 0;//command byte
      pci_read_32(pci_bus,device_function,PCI_REGISTER_1,&register1);


      //disable both I/O and memory decoding
      u32 commnad_byte = register1 & 0xFC;
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
