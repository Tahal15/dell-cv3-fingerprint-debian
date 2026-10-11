#ifndef BCM_CV_FAKE_USB_H
#define BCM_CV_FAKE_USB_H

#include <stdint.h>

typedef struct libusb_device_handle libusb_device_handle;

int libusb_interrupt_transfer(libusb_device_handle *, unsigned char,
                              unsigned char *, int, int *, unsigned int);
int libusb_control_transfer(libusb_device_handle *, uint8_t, uint8_t, uint16_t,
                            uint16_t, unsigned char *, uint16_t, unsigned int);
void fake_usb_reset(int emit_timestamp_event);
int fake_usb_interrupt_calls(void);
int fake_usb_control_calls(void);
uint8_t fake_usb_request_type(void);
uint8_t fake_usb_request(void);
uint16_t fake_usb_value(void);
uint16_t fake_usb_index(void);

#endif
