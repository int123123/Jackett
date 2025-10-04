#include "esp_log.h"
#include "wifi.h"
#include "app_mqtt.h"
#include "radar_ld2450.h"
#include "state_machine.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting LD2450 + CAM + MQTT app");

    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(app_mqtt_start());
    ESP_ERROR_CHECK(ld2450_init());
    ld2450_start();

    app_state_machine_start();
}
