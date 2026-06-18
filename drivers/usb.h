#ifndef USB_H
#define USB_H

#include <stdint.h>

// ============================================================================
// USB protocol constants (controller-independent)
// ============================================================================
// These describe the USB device model itself, not the xHCI host controller.
// xHCI-specific values (TRB types, EP context types) stay here too where they
// are part of speaking USB; pure register layout lives in xhci.h.

// Standard descriptor types (bDescriptorType)
#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIG        0x02
#define USB_DESC_STRING        0x03
#define USB_DESC_INTERFACE     0x04
#define USB_DESC_ENDPOINT      0x05
#define USB_DESC_HID           0x21  // class-specific (HID spec 7.1)
#define USB_DESC_HID_REPORT    0x22  // class-specific (HID spec 7.1)

// Interface class codes (bInterfaceClass)
#define USB_CLASS_HID          0x03
#define USB_CLASS_MASS_STORAGE 0x08

// HID subclass / protocol (bInterfaceSubClass / bInterfaceProtocol)
#define USB_HID_SUBCLASS_BOOT  0x01
#define USB_HID_PROTOCOL_KBD   0x01
#define USB_HID_PROTOCOL_MOUSE 0x02

// Mass-storage subclass / protocol (used by the future USB disk driver)
#define USB_MSC_SUBCLASS_SCSI  0x06
#define USB_MSC_PROTOCOL_BBB   0x50  // Bulk-Only Transport

// Standard requests (bRequest)
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIG     0x09

// bEndpointAddress direction bit
#define USB_EP_DIR_IN          0x80

// xHCI Endpoint Context EP Type — dw1[5:3] (xHCI spec Table 6-9)
#define XHCI_EP_TYPE_ISOCH_OUT     1
#define XHCI_EP_TYPE_BULK_OUT      2
#define XHCI_EP_TYPE_INTERRUPT_OUT 3
#define XHCI_EP_TYPE_CONTROL       4  // bidirectional control endpoint (EP0)
#define XHCI_EP_TYPE_ISOCH_IN      5
#define XHCI_EP_TYPE_BULK_IN       6
#define XHCI_EP_TYPE_INTERRUPT_IN  7

// ============================================================================
// USB core: class-driver dispatch
// ============================================================================
// Called by the host controller once a device has been addressed and
// configured. Routes the device to the matching class driver (HID keyboard
// today; mass storage / disk later) based on its interface descriptor.
void usb_attach_device(uint32_t slot_id, uint8_t iface_class,
                       uint8_t iface_subclass, uint8_t iface_protocol);

#endif
