#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_mqtt_start(void);
void app_mqtt_publish(const char *topic, const char *payload, int qos, bool retain);

#ifdef __cplusplus
}
#endif
