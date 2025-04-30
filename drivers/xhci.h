#ifndef __XHCI_H__
#define __XHCI_H__

//eXtensible Host Controller Interface (XHCI)

#include "../types.h"
#include "pci.h"

#define MY_USB_ID 0x7a60

void xhci_set_base_address(u64 address);
void xhci_init();

void xhci_get_base_address(PciDevice);

#endif
