idf_component_register(
    SRCS
        "app_main.c"
        "wifi.c"
        "app_mqtt.c"
        "radar_ld2450.c"
        "camera_controller.c"
        "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash mqtt esp_netif esp_timer driver
    PRIV_REQUIRES esp32-camera
)
