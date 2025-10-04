idf_component_register(SRCS
    "main.c"
    "wifi_mqtt.c"
    "camera_person.c"
    "ld2450.c"
    "controller.c"
  INCLUDE_DIRS "."
  REQUIRES
    esp_wifi
    nvs_flash
    mqtt
    esp32-camera
    esp_timer
    driver
    json
)
