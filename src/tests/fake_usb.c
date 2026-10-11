#include "fake_usb.h"

#include <string.h>

static int emit_timestamp;
static int interrupt_calls;
static int control_calls;
static uint8_t request_type;
static uint8_t request;
static uint16_t value;
static uint16_t index_value;

void
fake_usb_reset(int emit_timestamp_event)
{
  emit_timestamp = emit_timestamp_event;
  interrupt_calls = 0;
  control_calls = 0;
  request_type = 0;
  request = 0;
  value = 0;
  index_value = 0;
}

int
libusb_interrupt_transfer(libusb_device_handle *device, unsigned char endpoint,
                          unsigned char *data, int length, int *actual_length,
                          unsigned int timeout)
{
  (void) device;
  (void) endpoint;
  (void) timeout;
  if (length < 8)
    return -2;

  memset(data, 0, 8);
  if (emit_timestamp && interrupt_calls == 0)
    data[0] = 5;
  else
    data[4] = 44;
  interrupt_calls++;
  *actual_length = 8;
  return 0;
}

int
libusb_control_transfer(libusb_device_handle *device, uint8_t type,
                        uint8_t req, uint16_t val, uint16_t idx,
                        unsigned char *data, uint16_t length,
                        unsigned int timeout)
{
  (void) device;
  (void) data;
  (void) length;
  (void) timeout;
  control_calls++;
  request_type = type;
  request = req;
  value = val;
  index_value = idx;
  return 0;
}

int fake_usb_interrupt_calls(void) { return interrupt_calls; }
int fake_usb_control_calls(void) { return control_calls; }
uint8_t fake_usb_request_type(void) { return request_type; }
uint8_t fake_usb_request(void) { return request; }
uint16_t fake_usb_value(void) { return value; }
uint16_t fake_usb_index(void) { return index_value; }
