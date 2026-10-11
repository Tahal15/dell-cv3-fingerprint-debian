#ifndef BCM_CV_HOST_EVENT_SHIM_H
#define BCM_CV_HOST_EVENT_SHIM_H

#include <stdint.h>
#include <time.h>

uint32_t bcm_cv_pack_timestamp(const struct tm *local_time);

#endif
