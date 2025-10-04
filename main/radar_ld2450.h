#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool motion_detected;
    uint32_t timestamp_ms;
} ld2450_event_t;

esp_err_t ld2450_init(void);
void ld2450_start(void);
void ld2450_stop(void);
bool ld2450_get_motion_and_clear(ld2450_event_t *out_event);
void ld2450_power_set(bool on);

#ifdef __cplusplus
}
#endif
