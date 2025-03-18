#ifndef __XHCI_H__
#define __XHCI_H__

//eXtensible Host Controller Interface (XHCI)

#include "../types.h"

void xhci_set_base_address(u64 address);
void xhci_init();


#endif
