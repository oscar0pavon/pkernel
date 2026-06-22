
#include "pci.h"
#include "../console.h"
#include "../types.h"
#include "../paging.h"
#include "xhci.h"

uint64_t pcie_mmio_base_address = 0;

int get_pci_list(PciDevice *list, int max_count) {
  uint64_t mcfg_base = pcie_mmio_base_address;
  int count = 0;

  for (uint8_t bus = 0; bus < 8 && count < max_count; bus++) {
    for (uint8_t dev = 0; dev < 32 && count < max_count; dev++) {
      for (uint8_t func = 0; func < 8 && count < max_count; func++) {

        uint64_t cfg_addr = mcfg_base + ((uint64_t)bus << 20) |
                                        ((uint64_t)dev << 15) |
                                        ((uint64_t)func << 12);

        volatile uint32_t *regs = (volatile uint32_t *)cfg_addr;

        uint32_t id_reg = regs[0];
        if (id_reg == 0xFFFFFFFF || id_reg == 0x00000000) {
          if (func == 0)
            break;
          continue;
        }

        uint32_t class_reg = regs[2];

        PciDevice *d = &list[count++];
        d->bus         = bus;
        d->device      = dev;
        d->function    = func;
        d->vendor_id   = (uint16_t)(id_reg & 0xFFFF);
        d->device_id   = (uint16_t)(id_reg >> 16);
        d->revision_id = (uint8_t)(class_reg & 0xFF);
        d->prog_if     = (uint8_t)((class_reg >> 8) & 0xFF);
        d->subclass    = (uint8_t)((class_reg >> 16) & 0xFF);
        d->class_code  = (uint8_t)(class_reg >> 24);
        d->config_space = regs;
      }
    }
  }

  return count;
}

void setup_pci() {
  printf("pcie mmio %lx\n", pcie_mmio_base_address);

  static PciDevice pci_devices[MAX_PCI_DEVICES];
  int count = get_pci_list(pci_devices, MAX_PCI_DEVICES);

  // A board can expose several xHCI controllers (e.g. the Intel PCH plus a
  // discrete ASMedia for the 10G/20G ports). They share the single global
  // xhci_dev, so probe them one at a time and stop on the first that actually
  // has a device connected — otherwise a later controller's init would clobber
  // the state of the one holding our keyboard/mouse.
  for (int i = 0; i < count; i++) {
    PciDevice *d = &pci_devices[i];

    if (d->class_code == PCI_CLASS_SERIAL_BUS &&
        d->subclass == PCI_SUBCLASS_USB_CONTROLLER &&
        d->prog_if == PCI_INTERFACE_XHCI) {

      uint32_t bar0 = d->config_space[4];
      uint32_t bar1 = d->config_space[5];

      uint64_t base = (bar0 & 0xFFFFFFF0);
      if ((bar0 & 0x06) == 0x04) {
        base |= ((uint64_t)bar1 << 32);
      }

      printf("xHCI controller %02x:%02x.%x at MMIO 0x%lx\n",
             d->bus, d->device, d->function, base);

      xhci_dev.pci_regs  = d->config_space;
      xhci_dev.base_mmio = base;

      // The BAR can sit far above the low 4 GB the kernel maps at boot, so map
      // its MMIO window before the driver dereferences it. 64 KB covers the
      // xHCI capability/operational/runtime/doorbell register file.
      paging_map_mmio(base, 0x10000);

      init_xhci_driver();

      // init_xhci_driver() resets the controller, starts it, and scans its
      // root-hub ports. If something enumerated, this is the controller to
      // keep; leave xhci_dev pointing at it and stop probing.
      if (xhci_dev.device_attached) {
        printf("xHCI: device found on controller %02x:%02x.%x\n",
               d->bus, d->device, d->function);
        break;
      }
    }
  }
}
