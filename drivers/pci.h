#ifndef __PCI_H__
#define __PCI_H__
#include <stdint.h>

#define PCI_PORT1 0xCF8
#define PCI_PORT2 0xCFC

#define PCI_CLASS_UNCLASSIFIED 0x0
#define PCI_CLASS_STORAGE 0x1
#define PCI_CLASS_NETWORK 0x2
#define PCI_CLASS_DISPLAY 0x3
#define PCI_CLASS_MULTIMEDIA 0x4
#define PCI_CLASS_MEMORY 0x5
#define PCI_CLASS_BRIDGE 0x6
#define PCI_CLASS_COMMUNICATION 0x7
#define PCI_CLASS_PERIPHERAL 0x8
#define PCI_CLASS_INPUT_DEVICE 0x9
#define PCI_CLASS_DOCKING_STATION 0xA
#define PCI_CLASS_PROCESSOR 0xB
#define PCI_CLASS_SERIAL_BUS 0xC 
#define PCI_CLASS_WIRELESS_CONTROLLER 0xD
#define PCI_CLASS_INTELLIGENT_CONTROLLER 0xE
#define PCI_CLASS_SATELLITE_COMMUNICATION 0xF
#define PCI_CLASS_ENCRYPTON 0x10
#define PCI_CLASS_SIGNAL_PROCESSING 0x11
#define PCI_CLASS_COPROCESSOR 0x40
#define PCI_PROGIF_XHCI 0x30
#define PCI_INTERRUPT_LINE 0x3c

#define PCI_SUBCLASS_IDE 0x1
#define PCI_SUBCLASS_FLOPPY 0x2
#define PCI_SUBCLASS_ATA 0x5
#define PCI_SUBCLASS_SATA 0x6
#define PCI_SUBCLASS_NVM 0x8
#define PCI_SUBCLASS_ETHERNET 0x0
#define PCI_SUBCLASS_USB 0x3

#define PCI_CMD_INTERRUPT_DISABLE (1 << 10)
#define PCI_CMD_SPECIAL_CYCLES (1 << 3)
#define PCI_CMD_BUS_MASTER (1 << 2)
#define PCI_CMD_MEMORY_SPACE (1 << 1)
#define PCI_CMD_IO_SPACE (1 << 0)

#define PCI_BAR0      0x10
#define PCI_BAR1      0x14
#define PCI_BAR2      0x18
#define PCI_BAR3      0x1C
#define PCI_BAR4      0x20
#define PCI_BAR5      0x24
#define PCI_BAR_MASK  0xFFFFFFF0

struct pci_dev_t
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;

    uint8_t subclass_code;
    uint8_t subclass_code;
    uint8_t prog_if;

    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t slot;

    uint32_t bar[6];
    uint8_t interrupt_line;    // Interrupt line
    uint8_t interrupt_pin;     // Interrupt pin
    uint8_t irq;  
};

static inline int pci_extract_bus(uint32_t device);

static inline int pci_extract_slot(uint32_t device);

static inline int pci_extract_func(uint32_t device); 

uint64_t pcie_addr(uint32_t device, int field);

uint32_t pci_read(struct pci_dev_t* dev, uint8_t offset);

void pci_write(struct pci_dev_t* dev, uint8_t offset, uint16_t data);

void pci_get_vendor(struct pci_dev_t* dev);

int pci_is_xhci(struct pci_dev_t* dev);

uint32_t pci_read_field(uint32_t device, int field, int size);

uint32_t pci_get_interrupt(uint32_t device);

uint32_t pci_get_bar(uint32_t device, int index);

void pci_fill_dev(struct pci_dev_t* dev, uint32_t device);

#endif