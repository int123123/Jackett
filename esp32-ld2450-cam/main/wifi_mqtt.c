#include "wifi_mqtt.h"

#include <string.h>
#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "wifi_mqtt";

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static EventGroupHandle_t s_event_group;
static esp_mqtt_client_handle_t s_mqtt = NULL;
static char s_device_id[32] = {0};

static void build_device_id_once(void) {
    if (s_device_id[0] != '\0') return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "esp32cam_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

const char *get_device_id(void) {
    build_device_id_once();
    return s_device_id;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            xEventGroupClearBits(s_event_group, MQTT_CONNECTED_BIT);
            break;
        default:
            break;
    }
}

static void start_mqtt_client(void) {
    if (s_mqtt) return;

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_MQTT_BROKER_URI,
#if defined(CONFIG_MQTT_USERNAME) && defined(CONFIG_MQTT_PASSWORD)
        .username = strlen(CONFIG_MQTT_USERNAME) ? CONFIG_MQTT_USERNAME : NULL,
        .password = strlen(CONFIG_MQTT_PASSWORD) ? CONFIG_MQTT_PASSWORD : NULL,
#endif
        .keepalive = 30,
        .disable_auto_reconnect = false,
    };

    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Got IP, starting MQTT");
        start_mqtt_client();
    }
}

void wifi_mqtt_start(void) {
    esp_err_t err;
    s_event_group = xEventGroupCreate();

    // NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Netif and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(s_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool mqtt_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(s_event_group);
    return (bits & MQTT_CONNECTED_BIT) != 0;
}

esp_mqtt_client_handle_t mqtt_get_client(void) { return s_mqtt; }

static esp_err_t publish_string(const char *topic, const char *payload) {
    if (!s_mqtt) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 0);
    if (msg_id < 0) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t mqtt_publish_json(const char *subtopic, void *root_void) {
    if (!root_void) return ESP_ERR_INVALID_ARG;
    cJSON *root = (cJSON *)root_void;
    char topic[256];
    build_device_id_once();

    // Compose topic: base/deviceId/subtopic
    snprintf(topic, sizeof(topic), "%s/%s/%s", CONFIG_MQTT_BASE_TOPIC, s_device_id, subtopic ? subtopic : "state");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    esp_err_t res = publish_string(topic, json_str);
    free(json_str);
    return res;
}

esp_err_t mqtt_publish_kv(const char *subtopic, const char *key, const char *value) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, key ? key : "msg", value ? value : "");
    return mqtt_publish_json(subtopic, root);
}
