#include "xhci.h"
#include "../console.h"
#include <stdint.h>

#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14

#define HCIVERSION 0x02
#define RTSOFF 0x18

XHCIDevice xhci_dev = {0};


#define aligned4k __attribute__((aligned(4096)))

// Allocate 64-byte aligned tracking structures in your kernel's RAM
aligned4k volatile uint64_t dcbaap[64] = {0};
// 256 TRBs (each is 16 bytes/4 dwords)
aligned4k volatile uint32_t command_ring[256 * 4] = {0};
aligned4k volatile uint32_t event_ring[256 * 4] = {0};

aligned4k EventRingSegmentEntry erst;


void init_xhci_driver(void) {

  // Force the pointers to point straight to 
  // your physical memory address space
  //
  xhci_dev.cap_regs = (XhciCapabilityRegs*)xhci_dev.base_mmio;
  
  // Calculate where the operational registers start using CapLength
  uint64_t op_base = xhci_dev.base_mmio + xhci_dev.cap_regs->CapLength;

  xhci_dev.op_regs = (XhciOperationalRegs*)op_base;

  xhci_dev.max_ports = (xhci_dev.cap_regs->HcsParams1 >> 24) & 0xFF;

  printf("Total USB Ports Available: %d\n", xhci_dev.max_ports);

  // Wait for the hardware to clear the reset bit automatically
  printf("Resetting xHCI Controller...\n");
  // Set Bit 1 (Host Controller Reset) in the USB Command Register
  xhci_dev.op_regs->UsbCmd |= (1 << 1); // Set Bit 1 (HCRST)
                               //

  while (xhci_dev.op_regs->UsbSts & (1 << 11)) {
    __asm__("pause");
  }


  printf("Waiting for Controller to become Ready (CNR)...\n");
  for (volatile uint64_t delay = 0; delay < 50000000ULL; delay++) {
    __asm__("pause");
  }

  xhci_dev.pci_regs[1] |= (1 << 1) | (1 << 2);
  
  xhci_dev.op_regs->UsbSts = 0xFFFFFFFF;

  test_xhci_dma_identity();

  //setup_xhci_hardware(cap_regs, op_regs);
  
}

void test_xhci_dma_identity(void) {
  // 1. Point op_regs->Dcbaap to our global array using its compiler virtual
  // address We place a recognizable magic signature right inside index 0
  dcbaap[0] = 0x1122334455667788ULL;

  // 2. Write the address of our array directly into the xHCI hardware register
  xhci_dev.op_regs->Dcbaap = (uint64_t)&dcbaap;

  // 3. Forcibly read the register BACK from the xHCI chip hardware lines
  uint64_t hardware_read_address = xhci_dev.op_regs->Dcbaap;

  // 4. Now, look at the physical memory address the chip is actually holding
  volatile uint64_t *chip_target_memory =
      (volatile uint64_t *)hardware_read_address;

  printf("\n--- xHCI HARDWARE DMA CHECK ---\n");
  printf("Compiler Symbol Address (&dcbaap): 0x%lx\n", (uint64_t)&dcbaap);
  printf("Register Readback Address:        0x%lx\n", hardware_read_address);
  printf("Value inside Chip Memory Target:  0x%lx\n", *chip_target_memory);

  if (*chip_target_memory == 0x1122334455667788ULL) {
    printf("SUCCESS: The xHCI hardware and your CPU see the exact same RAM "
           "bytes!\n");
  } else {
    printf("FAIL: The xHCI hardware is looking at a different physical RAM "
           "cell!\n");
  }
  printf("--------------------------------\n");
}
