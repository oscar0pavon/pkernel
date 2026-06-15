#ifndef __XHCI_H__
#define __XHCI_H__

#include "../types.h"
#include "pci.h"
#include <stdint.h>

#define MY_USB_ID 0x7a60
#define PCI_INTERFACE_XHCI 0x30

// TRB Types (xHCI spec Table 6-91)
#define TRB_TYPE_SETUP_STAGE              2
#define TRB_TYPE_DATA_STAGE               3
#define TRB_TYPE_STATUS_STAGE             4
#define TRB_TYPE_ENABLE_SLOT              9
#define TRB_TYPE_ADDRESS_DEVICE           11
#define TRB_TYPE_LINK                     6
#define TRB_TYPE_TRANSFER_EVENT           32
#define TRB_TYPE_COMMAND_COMPLETION_EVENT 33

// USB Descriptor types
#define USB_DESC_TYPE_DEVICE 1

// EP Type field for Endpoint Context dw1[5:3]
#define EP_TYPE_CONTROL_BIDIR 4

// ============================================================================
// REGISTER STRUCTURES
// ============================================================================

struct XhciCapabilityRegs {
  uint8_t CapLength;
  uint8_t Reserved;
  uint16_t HciVersion;
  uint32_t HcsParams1;
  uint32_t HcsParams2;
  uint32_t HcsParams3;
  uint32_t HccParams1;
  uint32_t Dboff;
  uint32_t Rtsoff;
  uint32_t HccParams2;
} __attribute__((packed));

typedef struct XhciCapabilityRegs XhciCapabilityRegs;

struct XhciOperationalRegs {
  volatile uint32_t UsbCmd;
  volatile uint32_t UsbSts;
  volatile uint32_t PageSize;
  volatile uint32_t Reserved1[2];
  volatile uint32_t DnCtrl;
  volatile uint64_t Crcr;
  volatile uint32_t Reserved2[4];
  volatile uint64_t Dcbaap;
  volatile uint32_t Config;
  volatile uint32_t Reserved3[241];

  struct {
    volatile uint32_t PortSc;
    volatile uint32_t Reserved[3];
  } PortRegisterSet[256];
} __attribute__((packed));

typedef struct XhciOperationalRegs XhciOperationalRegs;

// ============================================================================
// DATA STRUCTURE DEFINITIONS
// ============================================================================

struct XhciTRB {
  uint64_t Parameter;
  uint32_t Status;
  uint32_t Control;
} __attribute__((packed));

struct XhciEventTRB {
  uint64_t CommandTrbPointer;
  uint32_t Status;
  uint32_t Control;
} __attribute__((packed));

// xHCI spec 5.5.2 - Interrupter Register Set (32 bytes / 0x20)
//   0x00 IMAN | 0x04 IMOD | 0x08 ERSTSZ | 0x0C RsvdP |
//   0x10 ERSTBA (64-bit) | 0x18 ERDP (64-bit)
// ERSTSZ is a full 32-bit register and there is a reserved DWORD at 0x0C;
// without it ERSTBA/ERDP land 4 bytes too low and ERSTBA gets zeroed.
struct XhciInterrupterRegs {
  volatile uint32_t Iman;      // 0x00
  volatile uint32_t Imod;      // 0x04
  volatile uint32_t Erstsz;    // 0x08 (low 16 bits = segment table size)
  volatile uint32_t Reserved;  // 0x0C
  volatile uint64_t Erstba;    // 0x10
  volatile uint64_t Erdp;      // 0x18
} __attribute__((packed));

struct XhciRuntimeRegs {
  volatile uint32_t MicroframeIndex;
  volatile uint32_t Reserved[7];
  struct XhciInterrupterRegs Interrupter[1024];
} __attribute__((packed));

typedef struct XhciRuntimeRegs XhciRuntimeRegs;

struct EventRingSegmentEntry {
  uint64_t RingSegmentBaseAddress;
  uint32_t RingSegmentSize;
  uint32_t Reserved;
} __attribute__((packed));

typedef struct EventRingSegmentEntry EventRingSegmentEntry;

// ============================================================================
// DEVICE CONTEXT STRUCTURES (xHCI spec 6.2)
// Every context entry is exactly 32 bytes (8 DWORDs).
// ============================================================================

// Slot Context (spec 6.2.2)
struct XhciSlotContext {
  uint32_t dw0; // RouteString[19:0] | Speed[23:20] | MTT[24] | Hub[25] | CtxEntries[31:27]
  uint32_t dw1; // MaxExitLatency[15:0] | RootHubPortNum[23:16] | NumPorts[31:24]
  uint32_t dw2; // ParentHubSlotId[7:0] | ParentPortNum[15:8] | TTThinkTime[17:16]
  uint32_t dw3; // DevAddr[7:0] | SlotState[31:27]
  uint32_t reserved[4];
} __attribute__((packed));

// Endpoint Context (spec 6.2.3)
struct XhciEndpointContext {
  uint32_t dw0; // EPState[2:0] | Mult[9:8] | MaxPStreams[14:10] | Interval[23:16]
  uint32_t dw1; // CErr[2:1] | EPType[5:3] | MaxBurstSize[15:8] | MaxPacketSize[31:16]
  uint32_t dw2; // DCS[0] | TRDequeuePtr_lo[31:4]
  uint32_t dw3; // TRDequeuePtr_hi[31:0]
  uint32_t dw4; // AvgTRBLength[15:0] | MaxESITPayload[31:16]
  uint32_t reserved[3];
} __attribute__((packed));

// Input Control Context (spec 6.2.5.1)
struct XhciInputControlContext {
  uint32_t drop_flags; // D[31:2]; D0/D1 are RsvdZ
  uint32_t add_flags;  // A[31:0]; A0=slot, A1=EP0, A2=EP1-OUT, ...
  uint32_t reserved[6];
} __attribute__((packed));

// Input Context = Control + Slot + EP0  (96 bytes for Address Device)
struct XhciInputContext {
  struct XhciInputControlContext ctrl; // offset 0x00
  struct XhciSlotContext         slot; // offset 0x20
  struct XhciEndpointContext     ep0;  // offset 0x40
} __attribute__((packed));

// Output Device Context = Slot + EP0  (64 bytes; DCBAAP[slot] points here)
struct XhciDeviceContext {
  struct XhciSlotContext     slot; // offset 0x00
  struct XhciEndpointContext ep0;  // offset 0x20
} __attribute__((packed));

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef struct XHCIDevice {
  uint64_t base_mmio;
  uint32_t max_ports;

  // Register pointers
  volatile uint32_t *pci_regs;
  volatile XhciCapabilityRegs *cap_regs;
  volatile XhciOperationalRegs *op_regs;
  volatile XhciRuntimeRegs *runtime_regs;
  volatile struct XhciInterrupterRegs *int_0_regs;
  volatile uint32_t *doorbell_regs;  // base_mmio + Dboff; [0] = command ring
} XHCIDevice;

extern XHCIDevice xhci_dev;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Core initialization functions
void init_xhci_driver(void);
void xhci_reset_controller(void);
void xhci_init_structures(void);
void xhci_setup_command_ring(void);
void xhci_setup_event_ring(void);
void xhci_start_controller(void);
void xhci_scan_ports(void);

// Debug/utility functions
void xhci_test_dma_identity(void);
void xhci_print_status(void);

// Command submission
void xhci_send_command(uint32_t trb_type, uint64_t parameter);
uint32_t xhci_poll_event_ring(void);  // returns slot_id on success, 0 on error

// Device enumeration
void xhci_enable_slot(uint32_t port);
void xhci_address_device(uint32_t slot_id, uint32_t port);

// USB control transfers
uint32_t xhci_poll_transfer_event(void);
void xhci_get_descriptor(uint32_t slot_id, uint8_t desc_type, uint16_t length);

extern volatile uint8_t descriptor_buffer[64];

#endif
