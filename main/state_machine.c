#include "state_machine.h"
#include "radar_ld2450.h"
#include "camera_controller.h"
#include "app_mqtt.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "fsm";

typedef enum {
    ST_IDLE = 0,
    ST_CAMERA_ACTIVE
} app_state_t;

static app_state_t s_state = ST_IDLE;
static int64_t s_last_person_seen_us = 0;
static int64_t s_camera_start_us = 0;

static void publish_state(const char *what, const char *value)
{
    char topic[64];
    char payload[64];
    snprintf(topic, sizeof(topic), "home/%s", what);
    snprintf(payload, sizeof(payload), "%s", value);
    app_mqtt_publish(topic, payload, 0, false);
}

static void fsm_task(void *arg)
{
    const int64_t person_loss_timeout_us = ((int64_t)CONFIG_APP_PERSON_LOSS_TIMEOUT_MS) * 1000LL;

    while (1) {
        switch (s_state) {
            case ST_IDLE: {
                ld2450_event_t evt;
                bool motion = ld2450_get_motion_and_clear(&evt);
                publish_state("radar/motion", motion ? "1" : "0");
                if (motion) {
                    if (camera_init() == 0) {
                        s_state = ST_CAMERA_ACTIVE;
                        s_camera_start_us = esp_timer_get_time();
                        publish_state("camera/state", "on");
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            case ST_CAMERA_ACTIVE: {
                camera_status_t st;
                bool person = camera_check_person(&st);
                // keep camera on if person visible OR radar still reports motion
                ld2450_event_t evt;
                bool motion = ld2450_get_motion_and_clear(&evt);
                publish_state("radar/motion", motion ? "1" : "0");

                if (person) {
                    s_last_person_seen_us = esp_timer_get_time();
                    publish_state("camera/person", "1");
                } else {
                    publish_state("camera/person", "0");
                    int64_t now = esp_timer_get_time();
                    if (s_last_person_seen_us == 0) {
                        s_last_person_seen_us = now;
                    }
                    if (!motion && (now - s_last_person_seen_us) > person_loss_timeout_us) {
                        camera_deinit();
                        publish_state("camera/state", "off");
                        s_state = ST_IDLE;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(150));
                break;
            }
        }
    }
}

void app_state_machine_start(void)
{
    s_state = ST_IDLE;
    s_last_person_seen_us = 0;
    xTaskCreate(fsm_task, "fsm", 4096, NULL, 5, NULL);
}
