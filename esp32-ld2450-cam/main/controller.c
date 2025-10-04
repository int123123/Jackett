#include "controller.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "wifi_mqtt.h"
#include "camera_person.h"
#include "ld2450.h"

static const char *TAG = "controller";

typedef enum {
    ST_RADAR = 0,
    ST_CAMERA = 1,
} state_t;

static controller_config_t s_cfg;

static void publish_state(const char *state, const char *detail) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state);
    if (detail) cJSON_AddStringToObject(root, "detail", detail);
    mqtt_publish_json("state", root);
}

static void publish_event(const char *event, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", event);
    if (msg) cJSON_AddStringToObject(root, "msg", msg);
    mqtt_publish_json("event", root);
}

static void controller_task(void *arg) {
    s_cfg = *(const controller_config_t *)arg;

    state_t st = ST_RADAR;
    int confirm_cnt = 0;
    int lost_cnt = 0;
    int64_t camera_started_us = 0;

    publish_state("boot", NULL);

    while (1) {
        switch (st) {
            case ST_RADAR: {
                // Ensure radar on, camera off
                if (!ld2450_is_powered()) ld2450_set_power(true);
                if (camera_is_running()) camera_stop();

                bool presence = ld2450_presence();
                if (presence) {
                    publish_event("radar_motion", NULL);
                    if (camera_start() == ESP_OK) {
                        camera_started_us = esp_timer_get_time();
                        st = ST_CAMERA;
                        publish_state("camera", "warmup");
                        confirm_cnt = 0;
                        lost_cnt = 0;
                    } else {
                        publish_event("camera_error", "init_failed");
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            case ST_CAMERA: {
                // Turn off radar to save power/interference
                if (ld2450_is_powered()) ld2450_set_power(false);

                float ratio = 0.0f;
                bool person = camera_person_visible(&ratio);

                if (person) {
                    confirm_cnt++;
                    lost_cnt = 0;
                } else {
                    lost_cnt++;
                }

                // Confirm presence after N frames and hold camera at least radar_to_camera_hold_ms
                int64_t now = esp_timer_get_time();
                bool hold_elapsed = (now - camera_started_us) / 1000 >= s_cfg.radar_to_camera_hold_ms;

                if (confirm_cnt >= s_cfg.person_confirm_frames) {
                    publish_event("person_visible", NULL);
                    confirm_cnt = s_cfg.person_confirm_frames; // saturate
                }

                if (!person && lost_cnt >= s_cfg.person_lost_frames && hold_elapsed) {
                    // Small grace delay before stopping camera
                    vTaskDelay(pdMS_TO_TICKS(s_cfg.delay_after_no_person_ms));
                    // Check once again after delay
                    float ratio2 = 0.0f;
                    bool person2 = camera_person_visible(&ratio2);
                    if (!person2) {
                        camera_stop();
                        ld2450_set_power(true);
                        st = ST_RADAR;
                        publish_state("radar", NULL);
                        confirm_cnt = 0;
                        lost_cnt = 0;
                        camera_started_us = 0;
                        vTaskDelay(pdMS_TO_TICKS(50));
                        break;
                    } else {
                        // Still seeing person, continue
                        lost_cnt = 0;
                        publish_event("person_back", NULL);
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(80));
                break;
            }
        }
    }
}

esp_err_t controller_start(const controller_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;

    wifi_mqtt_start();
    ESP_ERROR_CHECK(ld2450_init());

    xTaskCreatePinnedToCore(controller_task, "ctrl", 4096, (void *)cfg, 5, NULL, 1);

    return ESP_OK;
}
