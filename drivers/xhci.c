#include "xhci.h"
#include "../console.h"

static u64 base_address_host_controller;

void xhci_set_base_address(u64 address){
  base_address_host_controller = address;
}

void xhci_init(){
 printf("Base address %x\n",base_address_host_controller);
 u64 cap_length = *(u64*)base_address_host_controller; 
 printf("Cap lengh %x\n",cap_length);
}
