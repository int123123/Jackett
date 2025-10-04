#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_mqtt_start(void);
bool wifi_is_connected(void);
bool mqtt_is_connected(void);
esp_mqtt_client_handle_t mqtt_get_client(void);

// Publishes cJSON as UTF-8 text to base/deviceId/subtopic
// Takes ownership of `root` and deletes it after publish attempt
esp_err_t mqtt_publish_json(const char *subtopic, void *root /* cJSON* */);

// Convenience: publish { key: value } as a JSON string field value
esp_err_t mqtt_publish_kv(const char *subtopic, const char *key, const char *value);

// Returns cached device id based on MAC (e.g., esp32cam_ABCDEF)
const char *get_device_id(void);

#ifdef __cplusplus
}
#endif
