#include "usb.h"
#include "usb_keyboard.h"
#include "../console.h"

// ============================================================================
// USB core: route an addressed+configured device to its class driver.
// ============================================================================
// The xHCI layer enumerates a device (Address Device, GET_DESCRIPTOR,
// SET_CONFIGURATION) and then hands it here. This is the single place that
// knows about USB device classes; the host controller stays generic, so a
// new class (e.g. mass storage / disk) is added by extending this switch
// rather than touching xhci.c.
int usb_attach_device(uint32_t slot_id, uint8_t iface_class,
                      uint8_t iface_subclass, uint8_t iface_protocol) {
  (void)iface_subclass;
  (void)iface_protocol;

  switch (iface_class) {
    case USB_CLASS_HID:
      usb_kbd_attach(slot_id);
      return 1;

    case USB_CLASS_MASS_STORAGE:
      // TODO: USB disk (Bulk-Only Transport + SCSI) goes here.
      printf("USB: mass-storage device on slot %d (no driver yet)\n", slot_id);
      return 0;

    default:
      printf("USB: unsupported interface class 0x%x on slot %d\n",
             iface_class, slot_id);
      return 0;
  }
}
