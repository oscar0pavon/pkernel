#ifndef __XHCI_H__
#define __XHCI_H__

#include "../types.h"
#include "pci.h"
#include <stdint.h>
#include "../console.h"
#include "usb.h"

#define MY_USB_ID 0x7a60
#define PCI_INTERFACE_XHCI 0x30

#define XDBG(...) do { if (xhci_debug) printf(__VA_ARGS__); } while(0)

typedef struct XhciInterrupterRegs XhciInterrupterRegs;
typedef struct XhciRuntimeRegs XhciRuntimeRegs;
typedef struct EventRingSegmentEntry EventRingSegmentEntry;
typedef struct XhciCapabilityRegs XhciCapabilityRegs;
typedef struct XhciOperationalRegs XhciOperationalRegs;

typedef struct XhciTRB XhciTRB;
typedef struct XhciEventTRB XhciEventTRB;
typedef struct XhciInputContext XhciInputContext;
typedef struct XhciDeviceContext XhciDeviceContext;

// Transfer Request Block (TRB)
// TRB Types (xHCI spec Table 6-91)
#define TRB_TYPE_NORMAL                   1
#define TRB_TYPE_SETUP_STAGE              2
#define TRB_TYPE_DATA_STAGE               3
#define TRB_TYPE_STATUS_STAGE             4
#define TRB_TYPE_ENABLE_SLOT              9
#define TRB_TYPE_DISABLE_SLOT             10
#define TRB_TYPE_ADDRESS_DEVICE           11
#define TRB_TYPE_LINK                     6
#define TRB_TYPE_CONFIGURE_ENDPOINT       12
#define TRB_TYPE_EVALUATE_CONTEXT         13
#define TRB_TYPE_TRANSFER_EVENT           32
#define TRB_TYPE_COMMAND_COMPLETION_EVENT 33

// USB descriptor types, class codes and EP types live in usb.h.

// ============================================================================
// REGISTER STRUCTURES
// ============================================================================
//
//Host Controller Structural Parameters (HCSPARAMS)
//
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


struct EventRingSegmentEntry {
  uint64_t RingSegmentBaseAddress;
  uint32_t RingSegmentSize;
  uint32_t Reserved;
} __attribute__((packed));



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

// Endpoint contexts are addressed by Device Context Index (DCI):
//   DCI = endpoint_number * 2 + (direction == IN ? 1 : 0); EP0 = DCI 1.
// ep[] is indexed by DCI-1, so ep[0] is EP0 (DCI 1), ep[2] is EP1-IN (DCI 3),
// ep[4] is EP2-IN (DCI 5), etc. A device's interrupt endpoint can land at any
// DCI, so the array must cover all 31 possible endpoint contexts -- writing the
// context to a fixed slot (ep1in) made any non-EP1 device fail Configure
// Endpoint with a Parameter Error (code 17).
#define XHCI_MAX_EP_CTX 31

// Input Context = Control + Slot + 31 endpoint contexts
struct XhciInputContext {
  struct XhciInputControlContext ctrl;                 // offset 0x00
  struct XhciSlotContext         slot;                 // offset 0x20
  struct XhciEndpointContext     ep[XHCI_MAX_EP_CTX];  // offset 0x40, DCI 1..31
} __attribute__((packed));

// Output Device Context = Slot + 31 endpoint contexts (DCBAAP[slot] points here)
struct XhciDeviceContext {
  struct XhciSlotContext     slot;                 // offset 0x00
  struct XhciEndpointContext ep[XHCI_MAX_EP_CTX];  // offset 0x20, DCI 1..31
} __attribute__((packed));

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef struct XHCIDevice {
  uint64_t base_mmio;
  uint32_t max_ports;

  // Set to 1 once a device has been fully enumerated (addressed + configured)
  // on this controller. setup_pci() uses it to decide whether to keep probing
  // the remaining xHCI controllers or stop on this one.
  int device_attached;

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

// Debug toggle (0=off, 1=on)
void xhci_set_debug(int enabled);

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
int  xhci_address_device(uint32_t slot_id, uint32_t port);
void xhci_disable_slot(uint32_t slot_id);

// USB control transfers
uint32_t xhci_poll_transfer_event(void);
int xhci_get_descriptor(uint32_t slot_id, uint8_t desc_type, uint16_t length);
int xhci_evaluate_context(uint32_t slot_id, uint8_t new_mps);
int xhci_get_config_descriptor(uint32_t slot_id);
int xhci_set_configuration(uint32_t slot_id, uint8_t config_val);
void xhci_enable_msi(uint8_t vector);

// Generic EP0 control IN transfer (Setup + Data + Status). Used by class
// drivers to issue class-specific GET_DESCRIPTOR / control requests.
// Returns 1 on success, 0 on failure.
uint32_t xhci_control_in(uint32_t slot_id, uint64_t setup,
                         volatile uint8_t *buf, uint16_t length);
// Control transfer with no data stage (e.g. SET_PROTOCOL, SET_CONFIGURATION).
uint32_t ep0_control_nodata(uint32_t slot_id, uint64_t setup);

extern volatile uint8_t descriptor_buffer[256];
extern volatile struct XhciTRB ep1in_ring[256];
extern uint32_t ep1in_enqueue;
extern uint32_t ep1in_cycle;

// Endpoint info populated during Step 11, consumed by Steps 12–14
extern uint8_t  ep1_in_addr;
extern uint16_t ep1_in_mps;
extern uint8_t  ep1_in_interval;
extern uint8_t  ep1_in_number;
extern uint16_t hid_report_len;
extern uint8_t  iface_number;

extern uint32_t command_ring_enqueue;
extern uint32_t command_ring_cycle;

extern uint32_t event_ring_dequeue;
extern uint32_t event_ring_cycle;

extern int xhci_debug;

extern volatile XhciEventTRB event_ring[256];

#endif
