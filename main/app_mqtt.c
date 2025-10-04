#include "app_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "app_mqtt";
static esp_mqtt_client_handle_t client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to MQTT");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from MQTT");
        break;
    default:
        break;
    }
}

esp_err_t app_mqtt_start(void)
{
    if (client) return ESP_OK;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_APP_MQTT_URI,
        .credentials = {
            .username = CONFIG_APP_MQTT_USERNAME,
            .authentication = {
                .password = CONFIG_APP_MQTT_PASSWORD,
            }
        }
    };

    client = esp_mqtt_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    return ESP_OK;
}

void app_mqtt_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!client) return;
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, qos, retain);
    (void)msg_id;
}
