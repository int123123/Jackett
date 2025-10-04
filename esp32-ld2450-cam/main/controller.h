#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int delay_after_no_person_ms;    // how long to wait after person lost before stopping camera
    int radar_to_camera_hold_ms;     // minimum camera run after radar triggers
    int person_confirm_frames;       // consecutive frames to confirm person
    int person_lost_frames;          // consecutive frames to consider lost
} controller_config_t;

esp_err_t controller_start(const controller_config_t *cfg);

#ifdef __cplusplus
}
#endif
