#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t camera_start(void);
void camera_stop(void);
bool camera_is_running(void);
// Returns true if a person-like skin area ratio exceeds threshold.
bool camera_person_visible(float *out_ratio);

#ifdef __cplusplus
}
#endif
