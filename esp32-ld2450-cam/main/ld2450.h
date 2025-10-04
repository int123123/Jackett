#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LD2450_METHOD_GPIO = 0,
    LD2450_METHOD_UART = 1,
} ld2450_method_t;

esp_err_t ld2450_init(void);
void ld2450_deinit(void);
void ld2450_set_power(bool on);
bool ld2450_is_powered(void);
// Returns instantaneous presence based on configured method
bool ld2450_presence(void);

#ifdef __cplusplus
}
#endif
