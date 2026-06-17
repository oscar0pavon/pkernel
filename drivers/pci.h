#ifndef __PCI_H__
#define __PCI_H__

#include "../types.h"


#define PCI_ENABLE_BIT			0x80000000
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_ERROR 0xFFFFFFFF

#define PCI_REGISTER_1 0x4 //command byte
#define PCI_REGISTER_2 0x8
#define PCI_REGISTER_3 0xC

#define PCI_CLASS_SERIAL_BUS 0xC
#define PCI_SUBCLASS_USB_CONTROLLER 0x3

#define BAR0 0x10
#define BAR1 0x14

#define PCI_BAR0_OFFSET 0x10
#define PCI_BAR1_OFFSET 0x14

//MMIO
#define PCI_REG_COMMAND 0x04

// Bits defined by the official PCI Specification

// Bit 1: Allows reading/writing to MMIO BARs
#define PCI_CMD_MEMORY_SPACE (1 << 1) 
// Bit 2: Allows device to perform DMA to RAM
#define PCI_CMD_BUS_MASTER   (1 << 2) 

// Global pointer to track the discovered MMIO base address
extern uint64_t pcie_mmio_base_address;

typedef struct PciDevice {
	u8 bus;
	u8 device;
	u8 function;
	u16 vendor_id;
	u16 device_id;
	u8 class_code;
	u8 subclass;
	u8 prog_if;
	u8 revision_id;
	volatile u32 *config_space;
} PciDevice;

#define MAX_PCI_DEVICES 256

uint64_t* get_pcie_mmio_address();

int get_pci_list(PciDevice *list, int max_count);

void setup_pci();

#endif
