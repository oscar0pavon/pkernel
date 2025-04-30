#ifndef __PCI_H__
#define __PCI_H__

#include "../types.h"


#define PCI_ENABLE_BIT			0x80000000
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_ERROR 0xFFFFFFFF

#define PCI_REGISTER_2 0x8
#define PCI_REGISTER_3 0xC

#define PCI_CLASS_SERIAL_BUS 0xC
#define PCI_SUBCLASS_USB_CONTROLLER 0x3

#define BAR0 0x10
#define BAR1 0x14

typedef struct PciDevice {
	u8 bus;
	u16 device_funtion;
} PciDevice;

extern byte create_base_address(void);

int print_pci_list(void);

void pci_read_32(u8 bus, u8 device_function, u8 offset, u32* out);
void pci_write_32(u8 bus, u8 device_function, u8 offset, u32 value);

#endif
