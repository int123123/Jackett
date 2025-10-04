#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool person_visible;
} camera_status_t;

esp_err_t camera_init(void);
void camera_deinit(void);
bool camera_check_person(camera_status_t *out_status);

#ifdef __cplusplus
}
#endif
