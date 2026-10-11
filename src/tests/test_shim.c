#include "../bcm_cv_host_event_shim.h"
#include "fake_usb.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void
test_timestamp_packing(void)
{
  struct tm value = {
      .tm_year = 126,
      .tm_mon = 8,
      .tm_mday = 6,
      .tm_hour = 14,
      .tm_min = 37,
      .tm_sec = 42,
  };
  uint32_t packed = bcm_cv_pack_timestamp(&value);
  assert((packed & 0x1fU) == 6);
  assert(((packed >> 5) & 0x0fU) == 9);
  assert(((packed >> 9) & 0x3fU) == 26);
  assert(((packed >> 15) & 0x1fU) == 14);
  assert(((packed >> 20) & 0x3fU) == 37);
  assert(((packed >> 26) & 0x3fU) == 42);
}

static void
test_timestamp_event_is_consumed(void)
{
  unsigned char data[32];
  int actual = 0;
  fake_usb_reset(1);

  int status = libusb_interrupt_transfer(NULL, 0x85, data, sizeof data,
                                         &actual, 60000);
  assert(status == 0);
  assert(actual == 8);
  assert(memcmp(data, "\0\0\0\0\x2c\0\0\0", 8) == 0);
  assert(fake_usb_interrupt_calls() == 2);
  assert(fake_usb_control_calls() == 1);
  assert(fake_usb_request_type() == 0x40);
  assert(fake_usb_request() == 0x0d);

  uint32_t packed = (uint32_t) fake_usb_value() |
                    ((uint32_t) fake_usb_index() << 16);
  assert((packed & 0x1fU) >= 1 && (packed & 0x1fU) <= 31);
  assert(((packed >> 5) & 0x0fU) >= 1 && ((packed >> 5) & 0x0fU) <= 12);
}

static void
test_other_event_is_untouched(void)
{
  unsigned char data[32];
  int actual = 0;
  fake_usb_reset(0);

  int status = libusb_interrupt_transfer(NULL, 0x85, data, sizeof data,
                                         &actual, 2000);
  assert(status == 0);
  assert(fake_usb_interrupt_calls() == 1);
  assert(fake_usb_control_calls() == 0);
}

int
main(void)
{
  test_timestamp_packing();
  test_timestamp_event_is_consumed();
  test_other_event_is_untouched();
  puts("PASS: timestamp host event is synchronized and hidden from vendor blob");
  return 0;
}
