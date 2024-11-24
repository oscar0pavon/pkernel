#include "pci.h"
#include "../io.h"

uint32_t pci_read(struct pci_dev_t* dev, uint8_t offset)
{
    uint32_t address, temp;
    address = (uint32_t)((dev->bus << 16) | (dev->slot << 11) |
              (dev->function << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
  
    outl(0xCF8, address);
    temp = (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    return temp;
}

static inline int pci_extract_bus(uint32_t device) 
{
	return (uint8_t)((device >> 16));
}

static inline int pci_extract_slot(uint32_t device) 
{
	return (uint8_t)((device >> 8));
}

static inline int pci_extract_func(uint32_t device) 
{
	return (uint8_t)(device);
}

uint64_t pcie_addr(uint32_t device, int field) 
{
	return (uint64_t)((pci_extract_bus(device) << 20) | (pci_extract_slot(device) << 15) | (pci_extract_func(device) << 12) | (field));
}

void pci_get_vendor(struct pci_dev_t* dev)
{
    dev->vendor_id = pci_read(dev, 0);
}

void pci_write(struct pci_dev_t* dev, uint8_t offset, uint16_t data)
{
	uint32_t address = (uint32_t)((dev->bus << 16) | (dev->slot << 11) | (dev->function << 8) | (offset & 0xfc) | 0x80000000);

	outportl(0xCF8, address);
	outportl(0xCFC, (inportl(0xCFC) & (~(0xFFFF << ((offset & 2) * 8)))) |
						(uint32_t)(data << ((offset & 2) * 8)));
}

int pci_is_xhci(struct pci_dev_t* dev)
{
    return (dev->class_code == 0x0C && dev->subclass_code == 0x03); 
}

uint32_t pci_read_field(uint32_t device, int field, int size) {
	outportl(0xCF8, (uint64_t)pcie_addr(device, field));

	if (size == 4) {
		uint32_t t = inportl(0xCFC);
		return t;
	} else if (size == 2) {
		uint16_t t = inports(0xCFC + (field & 2));
		return t;
	} else if (size == 1) {
		uint8_t t = inportb(0xCFC + (field & 3));
		return t;
	}

	return 0xFFFF;
}

uint32_t pci_get_interrupt(uint32_t device)
{
	return pci_read_field(device, PCI_INTERRUPT_LINE, 1);
}

uint32_t pci_get_bar(uint32_t device, int index)
{
    uint32_t base_address = 0;

    if (index < 6) {
        base_address = pci_read_field(device, PCI_BAR0 + (index * 4), 4);
    }

    if (base_address & 0x01) {
        base_address = pci_read_field(device, PCI_BAR0 + ((index + 1) * 4), 4);
    } else {
        base_address &= PCI_BAR_MASK;
    }

    return base_address;
}

void pci_fill_dev(struct pci_dev_t* dev, uint32_t device)
{
    dev->bus = pci_extract_bus(device);
    dev->slot = pci_extract_slot(device);
    dev->function = pci_extract_func(device);
    dev->irq = pci_get_interrupt(device);
}