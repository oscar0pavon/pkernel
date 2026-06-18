#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <stdint.h>

// HID keyboard class driver. usb_attach_device() calls usb_kbd_attach() once
// a HID device has been addressed and configured; it fetches the HID report
// descriptor and arms the interrupt IN endpoint. From then on usb_kbd_isr()
// (invoked from irq_xhci_handler) drains the event ring and feeds decoded
// keystrokes into the input buffer.
void usb_kbd_attach(uint32_t slot_id);
void usb_kbd_isr(void);

#endif
