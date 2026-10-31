#define _GNU_SOURCE

#include "bcm_cv_host_event_shim.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct libusb_device_handle libusb_device_handle;

typedef int (*interrupt_transfer_fn)(libusb_device_handle *, unsigned char,
                                     unsigned char *, int, int *, unsigned int);
typedef int (*control_transfer_fn)(libusb_device_handle *, uint8_t, uint8_t,
                                   uint16_t, uint16_t, unsigned char *,
                                   uint16_t, unsigned int);
typedef int (*cv_update_enrollment_fn)(void *, void *);
typedef int (*cv_commit_enrollment_fn)(void *, void *);
typedef int (*cv_identify_fn)(void *, unsigned int, unsigned int *);

enum {
  LIBUSB_ERROR_OTHER = -99,
  BCM_CV_INTERRUPT_ENDPOINT = 0x85,
  BCM_CV_GET_HOST_TIMESTAMP_INTERRUPT = 5,
  BCM_CV_TIMESTAMP_SYNC_REQUEST = 0x0d,
  BCM_CV_VENDOR_DEVICE_OUT = 0x40,
  BCM_CV_MAX_HOST_EVENTS = 8,
  BCM_CV_CONTROL_TIMEOUT_MS = 3000,
  BCM_CV_STATUS_NO_MATCH = 137,
};

static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static interrupt_transfer_fn real_interrupt_transfer;
static control_transfer_fn real_control_transfer;
static pthread_mutex_t cv_api_resolve_lock = PTHREAD_MUTEX_INITIALIZER;
static cv_update_enrollment_fn real_cv_update_enrollment;
static cv_commit_enrollment_fn real_cv_commit_enrollment;
static cv_identify_fn real_cv_identify;

static void
trace_status(const char *operation, int status)
{
  char message[128];
  int message_length = snprintf(message, sizeof message,
                                "BCM_CV: %s returned status=%d (0x%x)\n",
                                operation, status, (unsigned) status);
  if (message_length > 0)
    (void) write(STDERR_FILENO, message, (size_t) message_length);
}

static void *
resolve_cv_api_symbol(const char *name, void **cached)
{
  pthread_mutex_lock(&cv_api_resolve_lock);
  if (*cached == NULL) {
    *cached = dlsym(RTLD_NEXT, name);
    if (*cached == NULL) {
      void *driver = dlopen(
          "/usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so",
          RTLD_LAZY | RTLD_NOLOAD);
      if (driver == NULL) {
        driver = dlopen(
            "/usr/lib/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so",
            RTLD_LAZY | RTLD_NOLOAD);
      }
      if (driver != NULL)
        *cached = dlsym(driver, name);
    }
  }
  void *resolved = *cached;
  pthread_mutex_unlock(&cv_api_resolve_lock);
  return resolved;
}

static void
resolve_libusb(void)
{
  *(void **) &real_interrupt_transfer = dlsym(RTLD_NEXT,
                                               "libusb_interrupt_transfer");
  *(void **) &real_control_transfer = dlsym(RTLD_NEXT,
                                             "libusb_control_transfer");
}

static uint32_t
load_le32(const unsigned char *data)
{
  return (uint32_t) data[0] | ((uint32_t) data[1] << 8) |
         ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

uint32_t
bcm_cv_pack_timestamp(const struct tm *local_time)
{
  unsigned year = (unsigned) (local_time->tm_year + 1900);
  unsigned year_since_2000 = year >= 2000 ? year - 2000 : 0;

  return ((unsigned) local_time->tm_mday & 0x1fU) |
         (((unsigned) local_time->tm_mon + 1U) & 0x0fU) << 5 |
         (year_since_2000 & 0x3fU) << 9 |
         ((unsigned) local_time->tm_hour & 0x1fU) << 15 |
         ((unsigned) local_time->tm_min & 0x3fU) << 20 |
         ((unsigned) local_time->tm_sec & 0x3fU) << 26;
}

static int
send_timestamp_sync(libusb_device_handle *device)
{
  time_t now = time(NULL);
  struct tm local_time;

  if (now == (time_t) -1 || localtime_r(&now, &local_time) == NULL)
    return LIBUSB_ERROR_OTHER;

  uint32_t packed = bcm_cv_pack_timestamp(&local_time);
  return real_control_transfer(device, BCM_CV_VENDOR_DEVICE_OUT,
                               BCM_CV_TIMESTAMP_SYNC_REQUEST,
                               (uint16_t) packed, (uint16_t) (packed >> 16),
                               NULL, 0, BCM_CV_CONTROL_TIMEOUT_MS);
}

int
libusb_interrupt_transfer(libusb_device_handle *device, unsigned char endpoint,
                          unsigned char *data, int length,
                          int *actual_length, unsigned int timeout)
{
  pthread_once(&resolve_once, resolve_libusb);
  if (real_interrupt_transfer == NULL || real_control_transfer == NULL)
    return LIBUSB_ERROR_OTHER;

  for (unsigned handled = 0; handled < BCM_CV_MAX_HOST_EVENTS; handled++) {
    int status = real_interrupt_transfer(device, endpoint, data, length,
                                         actual_length, timeout);
    if (status != 0 || endpoint != BCM_CV_INTERRUPT_ENDPOINT ||
        actual_length == NULL || *actual_length < 4 || data == NULL ||
        load_le32(data) != BCM_CV_GET_HOST_TIMESTAMP_INTERRUPT)
      return status;

    int sync_status = send_timestamp_sync(device);
    char message[160];
    int message_length = snprintf(
        message, sizeof message,
        "BCM_CV: handled host timestamp interrupt, control status=%d\n",
        sync_status);
    if (message_length > 0)
      (void) write(STDERR_FILENO, message, (size_t) message_length);
    if (sync_status < 0)
      return sync_status;
  }

  return LIBUSB_ERROR_OTHER;
}

/*
 * The proprietary TOD plugin calls these exported CV API entry points through
 * its PLT.  Trace only their integer status at the boundary; never inspect or
 * persist capture/enrollment buffers.  This keeps diagnostics useful without
 * collecting biometric or session payloads.
 */
int
cvif_fingerprint_update_enrollment(void *capture_id, void *enrollment_data)
{
  cv_update_enrollment_fn function;
  *(void **) &function = resolve_cv_api_symbol(
      "cvif_fingerprint_update_enrollment",
      (void **) &real_cv_update_enrollment);
  if (function == NULL)
    return LIBUSB_ERROR_OTHER;

  int status = function(capture_id, enrollment_data);
  trace_status("update enrollment", status);
  return status;
}

int
cvif_fingerprint_commit_enrollment(void *enrollment_data, void *template_id)
{
  cv_commit_enrollment_fn function;
  *(void **) &function = resolve_cv_api_symbol(
      "cvif_fingerprint_commit_enrollment",
      (void **) &real_cv_commit_enrollment);
  if (function == NULL)
    return LIBUSB_ERROR_OTHER;

  int status = function(enrollment_data, template_id);
  trace_status("commit enrollment", status);
  return status;
}

int
cvif_fingerprint_identify(void *template_handles, unsigned int template_count,
                          unsigned int *match_result)
{
  cv_identify_fn function;
  *(void **) &function = resolve_cv_api_symbol(
      "cvif_fingerprint_identify", (void **) &real_cv_identify);
  if (function == NULL)
    return LIBUSB_ERROR_OTHER;

  int status = function(template_handles, template_count, match_result);
  char message[192];
  int message_length = snprintf(
      message, sizeof message,
      "BCM_CV: identify returned status=%d (0x%x), templates=%u, result=%u\n",
      status, (unsigned) status, template_count,
      match_result != NULL ? *match_result : 0U);
  if (message_length > 0)
    (void) write(STDERR_FILENO, message, (size_t) message_length);

  /*
   * Fix for Broadcom TOD driver bug:
   * When the ControlVault chip finds no match (or bad swipe/scan), cvif_fingerprint_identify
   * returns status 137 (0x89 = CV_STATUS_NO_MATCH) with match_result = 0.
   * However, the Broadcom driver's verify/identify state handlers only check if status == 0.
   * If status != 0, the driver logs "Verify failed!" and jumps straight to
   * fpi_device_verify_complete(dev, NULL) WITHOUT calling fpi_device_verify_report(dev, FP_VERIFY_NO_MATCH).
   * Libfprint flags this as a protocol violation ("Driver reported successful verify complete
   * but did not report the result earlier. Reporting error instead"), causing PAM / kscreenlocker
   * to fail with an unspecified error and abort the biometric session.
   * By translating status 137 to status 0 (with match_result = 0), the driver properly invokes
   * fpi_device_verify_report(dev, FP_VERIFY_NO_MATCH) followed by complete, reporting a clean NO_MATCH
   * to libfprint and allowing retry.
   */
  if (status == BCM_CV_STATUS_NO_MATCH) {
    if (match_result != NULL)
      *match_result = 0;
    status = 0;
  }

  return status;
}
