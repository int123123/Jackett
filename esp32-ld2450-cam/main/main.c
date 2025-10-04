#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "controller.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "starting");

    controller_config_t cfg = {
        .delay_after_no_person_ms = CONFIG_DELAY_AFTER_NO_PERSON_MS,
        .radar_to_camera_hold_ms = CONFIG_MIN_CAMERA_HOLD_MS,
        .person_confirm_frames = CONFIG_PERSON_CONFIRM_FRAMES,
        .person_lost_frames = CONFIG_PERSON_LOST_FRAMES,
    };

    controller_start(&cfg);
}
