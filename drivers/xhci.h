#ifndef __XHCI_H__
#define __XHCI_H__

// eXtensible Host Controller Interface (XHCI)

#include "../types.h"
#include "pci.h"

#define MY_USB_ID 0x7a60

#define PCI_INTERFACE_XHCI 0x30 // xHCI Interface code

// The official xHCI Command IDs (TRB Types)
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_LINK 6

#define TRB_TYPE_COMMAND_COMPLETION_EVENT 32

extern uint64_t xhci_base_mmio;

// Host Controller Capability Registers (Read-Only)
struct XhciCapabilityRegs {
  uint8_t CapLength;   // Offset 0x00: Length of this capability structure
  uint8_t Reserved;    // Offset 0x01
  uint16_t HciVersion; // Offset 0x02: xHCI Interface Version (e.g., 0x0100 or
                       // 0x0110)
  uint32_t HcsParams1; // Offset 0x04: Structural Parameters 1 (Number of
                       // slots/ports)
  uint32_t HcsParams2; // Offset 0x08: Structural Parameters 2
  uint32_t HcsParams3; // Offset 0x0C: Structural Parameters 3
  uint32_t HccParams1; // Offset 0x10: Capability Parameters 1 (Data structures
                       // configuration)
  uint32_t Dboff;      // Offset 0x14: Doorbell Offset
  uint32_t Rtsoff;     // Offset 0x18: Runtime Registers Offset
  uint32_t HccParams2; // Offset 0x1C: Capability Parameters 2
} __attribute__((packed));

typedef struct XhciCapabilityRegs XhciCapabilityRegs;

// Host Controller Operational Registers (Read/Write)
// These control the actual state of the USB system
struct XhciOperationalRegs {
  volatile uint32_t
      UsbCmd; // Offset 0x00: USB Command Register (Start/Stop/Reset)
  volatile uint32_t UsbSts;   // Offset 0x04: USB Status Register
  volatile uint32_t PageSize; // Offset 0x08: Page Size Register
  volatile uint32_t Reserved1[2];
  volatile uint32_t DnCtrl; // Offset 0x14: Device Notification Control
  volatile uint64_t Crcr; // Offset 0x18: Command Ring Control Register (64-bit
                          // physical address)
  volatile uint32_t Reserved2[4];
  volatile uint64_t
      Dcbaap; // Offset 0x30: Device Context Base Address Array Pointer
  volatile uint32_t
      Config; // Offset 0x38: Configure Register (Max Device Slots Enabled)

  // CRITICAL: Padding up to offset 0x400
  //  // Config ends at 0x3C. PORTSC starts at 0x400.
  // (0x400 - 0x3C) = 0x3C4 bytes.
  // 0x3C4 bytes divided by 4 bytes per DWORD = 241 DWORDs of padding!
  volatile uint32_t Reserved3[241];

  // Offset 0x400: An array of PORTSC registers (Each port takes 4 dwords / 16
  // bytes of space) We only need the first dword of each block to check status
  // flags
  struct {
    volatile uint32_t PortSc;
    volatile uint32_t Reserved[3];
  } PortRegisterSet[256]; // Maximum potential ports supported by xHCI

} __attribute__((packed));

typedef struct XhciOperationalRegs XhciOperationalRegs;

struct XhciTRB {
  uint64_t Parameter; // Usually a physical RAM pointer, or 0 for basic commands
  uint32_t Status;    // Command modifiers and options (0 for basic commands)
  uint32_t Control;   // TRB Type, Cycle bit, and other control flags
} __attribute__((packed));

struct XhciEventTRB {
  uint64_t CommandTrbPointer; // The physical address of the command TRB we sent
  uint32_t Status;  // Bits 24-31: Completion Code. Bits 0-23: Parameter/Slot ID
  uint32_t Control; // Bits 10-15: TRB Type. Bit 0: Cycle bit
} __attribute__((packed));

struct XhciInterrupterRegs {
  volatile uint32_t Iman;   // Interrupt Management (Enable/Status)
  volatile uint32_t Imod;   // Interrupt Moderation (Throttling)
  volatile uint16_t Erstsz; // Event Ring Segment Table Size
  volatile uint16_t Reserved;
  volatile uint64_t Erstba; // Event Ring Segment Table Base Address
  volatile uint64_t Erdp;   // Event Ring Dequeue Pointer (Read/Write pointer)
} __attribute__((packed));

struct XhciRuntimeRegs {
  volatile uint32_t MicroframeIndex;
  volatile uint32_t Reserved[7];
  struct XhciInterrupterRegs
      Interrupter[1024]; // Primary interrupter is at index 0
}__attribute__((packed));

// Event Ring Segment Table (ERST) - Tells xHCI where the Event Ring sits
struct EventRingSegmentEntry {
  uint64_t RingSegmentBaseAddress;
  uint32_t RingSegmentSize; // Number of TRBs
  uint32_t Reserved;
} __attribute__((packed));

typedef struct XHCIDevice{
  volatile uint32_t *pci_regs;

}XHCIDevice;

extern XHCIDevice xhci_device;


void setup_xhci_hardware(uint64_t xhci_base,
                         volatile struct XhciCapabilityRegs *cap_regs,
                         volatile struct XhciOperationalRegs *op_regs);

void xhci_set_base_address(u64 address);
void xhci_init();

void init_xhci_driver(uint64_t xhci_base);

uint64_t xhci_get_base_address(PciDevice dev);

uint64_t xhci_get_base_address2(PciDevice dev);

#endif
