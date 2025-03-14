
#include "../types.h"
#include "../input_output.h"
#include "../console.h"

const u32 PCI_ENABLE_BIT     = 0x80000000;
const u32 PCI_CONFIG_ADDRESS = 0xCF8;
const u32 PCI_CONFIG_DATA    = 0xCFC;

u32 read_pci_data(u8 bus, u8 device, u8 func, u8 pcireg) {

  output(PCI_ENABLE_BIT | (bus << 16) | (device << 11) | (func << 8) |
             (pcireg << 2),
         PCI_CONFIG_ADDRESS);

  u32 ret = input(PCI_CONFIG_DATA);

  return ret;
}

int print_pci_list(void) {
  u8 bus, device, func;
  u32 data;

  for (bus = 0; bus != 0xff; bus++) {
    for (device = 0; device < 32; device++) {
      for (func = 0; func < 8; func++) {
        data = read_pci_data(bus, device, func, 0);

        if (data != 0xffffffff) {
          print("Bus");
          print_uint(bus);
          print("Device");
          print_uint(device);
          print("Func");
          print_uint(func);
          print("Vendor");
          //print_uint(vendor); TODO: create printf
        }
      }
    }
  }
  return 0;
}
