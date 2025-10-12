// ESP-IDF 5.4 + NimBLE implementation for ESP32-C3
#include <cstdio>
#include <vector>
#include <cstring>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
}

static const char *TAG = "virtual_pult";

// Adjust pins for ESP32-C3 super mini: GPIOs are 0-21 typically available.
// Use GPIO4 and GPIO5 as safe inputs with pull-ups (avoid BOOT/strap pins).
#define BTN1_PIN GPIO_NUM_4
#define BTN2_PIN GPIO_NUM_5

#define REPEAT_COUNT 2
static const uint32_t DEBOUNCE_MS = 50;

static uint8_t PULT_MAC[6] = {0xA4, 0xC1, 0x38, 0x6C, 0x5E, 0x8B};
static uint8_t PACKET_BASE[16] = {
    0x14, 0x16, 0x95, 0xFE, 0x50, 0x30, 0x53, 0x01,
    0xDF, 0x8D, 0x5E, 0x6C, 0x38, 0xC1, 0xA4, 0x01
};
static uint8_t globalCounter = 0;

// tails
static const uint8_t TAIL_OFF[5]         = {0x10,0x03,0x01,0x00,0x00};
static const uint8_t TAIL_ON[5]          = {0x10,0x03,0x00,0x00,0x00};
static const uint8_t TAIL_BRIGHT_MIN[5]  = {0x10,0x03,0x05,0x00,0x02};
static const uint8_t TAIL_BRIGHT_HALF[5] = {0x10,0x03,0x03,0x00,0x00};
static const uint8_t TAIL_BRIGHT_FULL[5] = {0x10,0x03,0x03,0x00,0x02};
static const uint8_t TAIL_SPECIAL[5]     = {0x10,0x03,0x05,0x00,0x00};

enum Command {
    CMD_OFF,
    CMD_ON,
    CMD_BRIGHT_MIN,
    CMD_BRIGHT_HALF,
    CMD_BRIGHT_FULL,
    CMD_SPECIAL
};

// NimBLE state
static uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
static volatile bool nimble_ready = false;

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising complete, reason=%d", event->adv_complete.reason);
            break;
        case BLE_GAP_EVENT_ADV_START:
            ESP_LOGI(TAG, "Advertising started");
            break;
        case BLE_GAP_EVENT_ADV_STOP:
            ESP_LOGI(TAG, "Advertising stopped");
            break;
        default:
            break;
    }
    return 0;
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    uint8_t addr_val[6];
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "NimBLE synced, addr_type=%u, addr=%02X:%02X:%02X:%02X:%02X:%02X",
             own_addr_type, addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);
    nimble_ready = true;
}

static std::vector<uint8_t> buildPacket(Command cmd)
{
    std::vector<uint8_t> packet;
    packet.insert(packet.end(), PACKET_BASE, PACKET_BASE + 16);
    globalCounter++;
    packet[8] = globalCounter;

    switch (cmd) {
        case CMD_OFF:          packet.insert(packet.end(), TAIL_OFF,         TAIL_OFF + 5); break;
        case CMD_ON:           packet.insert(packet.end(), TAIL_ON,          TAIL_ON + 5); break;
        case CMD_BRIGHT_MIN:   packet.insert(packet.end(), TAIL_BRIGHT_MIN,  TAIL_BRIGHT_MIN + 5); break;
        case CMD_BRIGHT_HALF:  packet.insert(packet.end(), TAIL_BRIGHT_HALF, TAIL_BRIGHT_HALF + 5); break;
        case CMD_BRIGHT_FULL:  packet.insert(packet.end(), TAIL_BRIGHT_FULL, TAIL_BRIGHT_FULL + 5); break;
        case CMD_SPECIAL:      packet.insert(packet.end(), TAIL_SPECIAL,     TAIL_SPECIAL + 5); break;
    }
    return packet;
}

static void sendPacket(const std::vector<uint8_t> &packet)
{
    // Ensure BLE stack is ready
    if (!nimble_ready) {
        ESP_LOGW(TAG, "NimBLE not ready yet");
        return;
    }

    // Stop any ongoing advertising
    ble_gap_adv_stop();

    // Set raw advertising payload (legacy ADV, up to 31 bytes)
    int rc = ble_gap_adv_set_data(packet.data(), (int)packet.size());
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data failed: %d", rc);
        return;
    }

    // Prepare non-connectable, non-discoverable parameters to emulate ADV_NONCONN_IND
    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    params.itvl_min  = 0x20; // 20 ms units of 0.625ms
    params.itvl_max  = 0x40; // 40 ms
    params.channel_map = 0x07; // all channels

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(60));
    ble_gap_adv_stop();

    // Log sent bytes
    char buf[256];
    size_t idx = 0;
    for (auto b : packet) {
        if (idx + 2 >= sizeof(buf)) break;
        std::snprintf(buf + idx, sizeof(buf) - idx, "%02X", b);
        idx += 2;
    }
    ESP_LOGI(TAG, "Sent: %s", buf);
}

static void sendCommand(Command cmd)
{
    for (int i = 0; i < REPEAT_COUNT; ++i) {
        auto pkt = buildPacket(cmd);
        sendPacket(pkt);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static uint8_t readButtonsRaw()
{
    bool b1 = (gpio_get_level(BTN1_PIN) == 0);
    bool b2 = (gpio_get_level(BTN2_PIN) == 0);
    return (b1 ? 1 : 0) | (b2 ? 2 : 0);
}

static void handleStateChange(uint8_t prev, uint8_t state)
{
    ESP_LOGI(TAG, "State %u -> %u", prev, state);

    if (state == 1) {
        sendCommand(CMD_ON);
        vTaskDelay(pdMS_TO_TICKS(80));
        sendCommand(CMD_BRIGHT_MIN);
    } else if (state == 2) {
        sendCommand(CMD_ON);
        vTaskDelay(pdMS_TO_TICKS(80));
        sendCommand(CMD_BRIGHT_HALF);
    } else if (state == 3) {
        sendCommand(CMD_ON);
        vTaskDelay(pdMS_TO_TICKS(80));
        sendCommand(CMD_BRIGHT_FULL);
    } else if (state == 0) {
        sendCommand(CMD_OFF);
    }

    if (prev == 3 && state == 2) {
        sendCommand(CMD_SPECIAL);
    }
}

static void setBaseMac()
{
    esp_err_t r = esp_base_mac_addr_set(PULT_MAC);
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "Base MAC set");
    } else {
        ESP_LOGE(TAG, "Base MAC set failed: 0x%x", r);
    }
}

static void initBLE()
{
    // NVS is required by the controller
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Set base MAC before controller init
    setBaseMac();

    // Initialize controller + HCI for NimBLE
    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());

    // Initialize NimBLE host
    nimble_port_init();

    // Optional standard GAP/GATT services (not used, but safe)
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Configure host callbacks
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;

    // Start NimBLE host task (not strictly necessary to create custom task since we block in app_main)
    nimble_port_freertos_init([](void *param) {
        nimble_port_run();
        nimble_port_freertos_deinit();
    });
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Virtual Pult (ESP-IDF) ===");

    // Configure buttons with pull-ups
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    gpio_num_t pins[] = {BTN1_PIN, BTN2_PIN};
    for (gpio_num_t pin : pins) {
        io_conf.pin_bit_mask = 1ULL << pin;
        gpio_config(&io_conf);
    }

    globalCounter = 0;
    initBLE();

    // Wait for NimBLE to be ready
    while (!nimble_ready) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint8_t lastRaw = 0;
    uint8_t stableState = 0;
    uint32_t lastDebounceTime = 0;

    lastRaw = readButtonsRaw();
    stableState = lastRaw;
    lastDebounceTime = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Ready");

    while (true) {
        uint8_t raw = readButtonsRaw();
        uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (raw != lastRaw) {
            lastDebounceTime = nowMs;
            lastRaw = raw;
        }
        if ((nowMs - lastDebounceTime) > DEBOUNCE_MS) {
            if (raw != stableState) {
                uint8_t prev = stableState;
                stableState = raw;
                handleStateChange(prev, stableState);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
