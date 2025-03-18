#include "xhci.h"

static u64 base_address_host_controller;

void xhci_set_base_address(u64 address){
  base_address_host_controller = address;
}

void xhci_init(){
  
}
