#include "radar_ld2450.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ld2450";

#define UART_PORT       CONFIG_APP_LD2450_UART_PORT
#define UART_BAUD       CONFIG_APP_LD2450_BAUD
#define UART_TX_PIN     CONFIG_APP_LD2450_PIN_TX
#define UART_RX_PIN     CONFIG_APP_LD2450_PIN_RX

static volatile bool s_motion_detected = false;
static ld2450_event_t s_last_evt = {0};
static volatile int64_t s_last_packet_us = 0;

static void parse_ld2450_bytes(const uint8_t *data, size_t len)
{
    // Simplified heuristic: if we receive any valid-looking frame bytes, flag motion.
    // You should replace this with proper LD2450 frame parsing per vendor protocol.
    if (len > 0) {
        s_motion_detected = true;
        s_last_evt.motion_detected = true;
        s_last_evt.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        s_last_packet_us = esp_timer_get_time();
    }
}

static void uart_task(void *arg)
{
    uint8_t *buf = (uint8_t*) malloc(512);
    while (1) {
        int len = uart_read_bytes(UART_PORT, buf, 512, pdMS_TO_TICKS(200));
        if (len > 0) {
            parse_ld2450_bytes(buf, len);
        }
    }
}

esp_err_t ld2450_init(void)
{
    if (CONFIG_APP_RADAR_POWER_GPIO >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_APP_RADAR_POWER_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        gpio_set_level(CONFIG_APP_RADAR_POWER_GPIO, 1);
    }

    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_task, "ld2450_uart", 4096, NULL, 5, NULL);
    return ESP_OK;
}

void ld2450_start(void)
{
    // Potentially send commands to enable detection. Placeholder.
}

void ld2450_stop(void)
{
    // Potentially send commands to disable detection or set to sleep. Placeholder.
}

bool ld2450_get_motion_and_clear(ld2450_event_t *out_event)
{
    // Motion is considered true if there was recent packet activity within hold window
    int64_t now = esp_timer_get_time();
    int64_t hold_us = ((int64_t)CONFIG_APP_LD2450_MOTION_HOLD_MS) * 1000LL;
    bool motion = false;
    if (s_last_packet_us > 0 && (now - s_last_packet_us) <= hold_us) {
        motion = true;
    }
    if (motion && out_event) {
        *out_event = s_last_evt;
    }
    // do not clear; we keep continuous view based on time window
    return motion;
}

void ld2450_power_set(bool on)
{
    if (CONFIG_APP_RADAR_POWER_GPIO >= 0) {
        gpio_set_level(CONFIG_APP_RADAR_POWER_GPIO, on ? 1 : 0);
    }
}
