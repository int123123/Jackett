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
    ST_RADAR_ACTIVE = 0,
    ST_CAMERA_ACTIVE
} app_state_t;

static app_state_t s_state = ST_RADAR_ACTIVE;
static int64_t s_last_person_seen_us = 0;

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
            case ST_RADAR_ACTIVE: {
                ld2450_event_t evt;
                if (ld2450_get_motion_and_clear(&evt)) {
                    publish_state("radar/motion", "1");
                    ld2450_stop();
                    ld2450_power_set(false);
                    if (camera_init() == 0) {
                        s_state = ST_CAMERA_ACTIVE;
                        publish_state("camera/state", "on");
                    } else {
                        // fallback: re-enable radar if camera failed
                        ld2450_power_set(true);
                        ld2450_start();
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }
            case ST_CAMERA_ACTIVE: {
                camera_status_t st;
                bool person = camera_check_person(&st);
                if (person) {
                    s_last_person_seen_us = esp_timer_get_time();
                    publish_state("camera/person", "1");
                } else {
                    publish_state("camera/person", "0");
                    int64_t now = esp_timer_get_time();
                    if (s_last_person_seen_us == 0) {
                        s_last_person_seen_us = now;
                    }
                    if ((now - s_last_person_seen_us) > person_loss_timeout_us) {
                        camera_deinit();
                        publish_state("camera/state", "off");
                        ld2450_power_set(true);
                        ld2450_start();
                        s_state = ST_RADAR_ACTIVE;
                        publish_state("radar/motion", "0");
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
    s_state = ST_RADAR_ACTIVE;
    s_last_person_seen_us = 0;
    xTaskCreate(fsm_task, "fsm", 4096, NULL, 5, NULL);
}
