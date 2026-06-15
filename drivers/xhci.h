#ifndef __XHCI_H__
#define __XHCI_H__

#include "../types.h"
#include "pci.h"
#include <stdint.h>

#define MY_USB_ID 0x7a60
#define PCI_INTERFACE_XHCI 0x30

// TRB Types
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_LINK           6
#define TRB_TYPE_TRANSFER_EVENT 32
#define TRB_TYPE_COMMAND_COMPLETION_EVENT 33  // xHCI spec Table 6-91

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

struct XhciInterrupterRegs {
  volatile uint32_t Iman;
  volatile uint32_t Imod;
  volatile uint16_t Erstsz;
  volatile uint16_t Reserved;
  volatile uint64_t Erstba;
  volatile uint64_t Erdp;
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
// DEVICE CONTEXT (for future use - device enumeration phase)
// ============================================================================

// Placeholder - you'll expand this when implementing device enumeration
struct XhciSlotContext {
  uint32_t RouteString;
  uint32_t DeviceSpeed;
  // ... more fields as needed
} __attribute__((packed));

struct XhciInputContext {
  uint32_t DropContextFlags;
  uint32_t AddContextFlags;
  uint32_t Reserved[6];
  struct XhciSlotContext SlotContext;
  // EndpointContexts would follow
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

// Device enumeration (to be implemented)
void xhci_enable_slot(uint32_t port);
void xhci_address_device(uint32_t slot_id);

#endif
