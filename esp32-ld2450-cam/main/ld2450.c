#include "ld2450.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ld2450";

static bool s_powered = true;

static void power_pin_init(void) {
#if CONFIG_LD2450_POWER_GPIO >= 0
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_LD2450_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_LD2450_POWER_GPIO, 1);
    s_powered = true;
#endif
}

esp_err_t ld2450_init(void) {
    power_pin_init();
#if CONFIG_LD2450_METHOD_GPIO
    gpio_config_t in_conf = {
        .pin_bit_mask = 1ULL << CONFIG_LD2450_PRESENCE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_conf);
#elif CONFIG_LD2450_METHOD_UART
    uart_config_t uart_config = {
        .baud_rate = CONFIG_LD2450_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(CONFIG_LD2450_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CONFIG_LD2450_UART_NUM, CONFIG_LD2450_UART_TX, CONFIG_LD2450_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_LD2450_UART_NUM, 1024, 0, 0, NULL, 0));
#endif
    return ESP_OK;
}

void ld2450_deinit(void) {
#if CONFIG_LD2450_METHOD_UART
    uart_driver_delete(CONFIG_LD2450_UART_NUM);
#endif
}

void ld2450_set_power(bool on) {
#if CONFIG_LD2450_POWER_GPIO >= 0
    gpio_set_level(CONFIG_LD2450_POWER_GPIO, on ? 1 : 0);
#endif
    s_powered = on;
}

bool ld2450_is_powered(void) { return s_powered; }

bool ld2450_presence(void) {
#if CONFIG_LD2450_METHOD_GPIO
    int level = gpio_get_level(CONFIG_LD2450_PRESENCE_GPIO);
    return level == (CONFIG_LD2450_PRESENCE_ACTIVE_HIGH ? 1 : 0);
#elif CONFIG_LD2450_METHOD_UART
    // Simplified presence: check if there is a valid frame within timeout
    uint8_t buf[64];
    int len = uart_read_bytes(CONFIG_LD2450_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(10));
    if (len <= 0) return false;
    // LD2450 UART protocol: frames start with 0xAA 0xFF 0x03 ... for target info
    for (int i = 0; i < len - 3; ++i) {
        if (buf[i] == 0xAA && buf[i+1] == 0xFF) {
            return true; // movement detected in recent frame
        }
    }
    return false;
#else
    return false;
#endif
}
