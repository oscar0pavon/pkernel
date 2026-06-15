
#include "pci.h"
#include "../console.h"
#include "../types.h"
#include "xhci.h"

uint64_t pcie_mmio_base_address = 0;

void setup_pci() {

  printf("pcie mmio %lx\n", pcie_mmio_base_address);
  uint64_t mcfg_base = pcie_mmio_base_address;
  uint8_t bus = 0;

  for (uint8_t dev = 0; dev < 32; dev++) {
    for (uint8_t func = 0; func < 8; func++) {
      
      // Calculate the 64-bit physical address for this PCI slot's registers
      uint64_t device_config_space = mcfg_base + ((uint64_t)bus << 20) |
                                     ((uint64_t)dev << 15) |
                                     ((uint64_t)func << 12);

      volatile uint32_t *pci_regs = (volatile uint32_t *)device_config_space;

      uint32_t id_reg = pci_regs[0]; // Offset 0x00: Vendor/Device ID
      if (id_reg == 0xFFFFFFFF || id_reg == 0x00000000) {
        if (func == 0)
          break;
        continue;
      }

      uint32_t class_reg = pci_regs[2]; // Offset 0x08: Class/Subclass/ProgIF
      uint8_t class = class_reg >> 24;
      uint8_t sub_class = (class_reg >> 16) & 0xFF;
      uint8_t prog_if = (class_reg >> 8) & 0xFF;

      // Match xHCI USB 3.0 Controller Spec
      if (class == PCI_CLASS_SERIAL_BUS &&
          sub_class == PCI_SUBCLASS_USB_CONTROLLER) {

        if (prog_if == PCI_INTERFACE_XHCI) {

          printf("xHCI Device Found at Slot %d, Func %d!\n", dev, func);
          xhci_dev.pci_regs = pci_regs;

          // 1. MANDATORY: 
          // Enable Memory Space and Bus Mastering in Command Reg
          // (Offset 0x04)
          //pci_regs[1] |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;

          // 2. Read BAR0 and BAR1 (Offsets 0x10 and 0x14 / Indices 4 and 5)
          uint32_t bar0 = pci_regs[4];
          uint32_t bar1 = pci_regs[5];

          xhci_base_mmio = (bar0 & 0xFFFFFFF0);
          if ((bar0 & 0x06) == 0x04) {
            xhci_base_mmio |= ((uint64_t)bar1 << 32);
          }

          printf("xHCI Controller Internal Registers live at: %lx\n",
                 xhci_base_mmio);

          init_xhci_driver();
        }
      }
    }
  }
}
