#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_camera.h"
}

// ESP-DL
#include "dl_image.hpp"
#include "dl_variable.hpp"
#include "dl_tensor.hpp"
#include "dl_postprocess/yolo.hpp"

static const char *TAG = "person_counter";

// Embedded model symbols if model is provided via EMBED_FILES
#include <cstring>
extern "C" {
extern const uint8_t _binary_models_yolov3_tiny_320x320_int8_bin_start[];
extern const uint8_t _binary_models_yolov3_tiny_320x320_int8_bin_end[];
}

// Map Kconfig frame size to esp32-camera frame size
static framesize_t framesize_from_str(const char *name)
{
    if (!name) return FRAMESIZE_QVGA;
    if (strcmp(name, "QQVGA") == 0) return FRAMESIZE_QQVGA;
    if (strcmp(name, "QVGA") == 0) return FRAMESIZE_QVGA;
    if (strcmp(name, "VGA") == 0) return FRAMESIZE_VGA;
    if (strcmp(name, "SVGA") == 0) return FRAMESIZE_SVGA;
    if (strcmp(name, "XGA") == 0) return FRAMESIZE_XGA;
    if (strcmp(name, "SXGA") == 0) return FRAMESIZE_SXGA;
    if (strcmp(name, "UXGA") == 0) return FRAMESIZE_UXGA;
    return FRAMESIZE_QVGA;
}

static esp_err_t init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_camera()
{
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CONFIG_CAM_PIN_D0;
    config.pin_d1 = CONFIG_CAM_PIN_D1;
    config.pin_d2 = CONFIG_CAM_PIN_D2;
    config.pin_d3 = CONFIG_CAM_PIN_D3;
    config.pin_d4 = CONFIG_CAM_PIN_D4;
    config.pin_d5 = CONFIG_CAM_PIN_D5;
    config.pin_d6 = CONFIG_CAM_PIN_D6;
    config.pin_d7 = CONFIG_CAM_PIN_D7;
    config.pin_xclk = CONFIG_CAM_PIN_XCLK;
    config.pin_pclk = CONFIG_CAM_PIN_PCLK;
    config.pin_vsync = CONFIG_CAM_PIN_VSYNC;
    config.pin_href = CONFIG_CAM_PIN_HREF;
    config.pin_sccb_sda = CONFIG_CAM_PIN_SIOD;
    config.pin_sccb_scl = CONFIG_CAM_PIN_SIOC;
    config.pin_pwdn = CONFIG_CAM_PIN_PWDN;
    config.pin_reset = CONFIG_CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000; // 20MHz XCLK for OV5640
    config.pixel_format = PIXFORMAT_RGB565; // For DL preprocessing to RGB888

    config.frame_size = framesize_from_str(CONFIG_PERSON_COUNTER_FRAME_SIZE);
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // OV5640 specific tweaks if needed
        s->set_framesize(s, config.frame_size);
        s->set_gainceiling(s, GAINCEILING_32X);
        s->set_brightness(s, 0);
        s->set_saturation(s, 0);
        s->set_contrast(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
    }

    return ESP_OK;
}

struct Detection {
    int x;
    int y;
    int w;
    int h;
    int class_id;
    float score;
};

static void rgb565_to_rgb888(const camera_fb_t *fb, std::vector<uint8_t> &rgb)
{
    const uint16_t *src = reinterpret_cast<const uint16_t *>(fb->buf);
    rgb.resize(fb->width * fb->height * 3);
    uint8_t *dst = rgb.data();
    for (size_t i = 0, j = 0; i < fb->len / 2; ++i) {
        uint16_t pixel = src[i];
        uint8_t r = (pixel >> 11) & 0x1F;
        uint8_t g = (pixel >> 5) & 0x3F;
        uint8_t b = (pixel) & 0x1F;
        dst[j++] = (r * 527 + 23) >> 6; // 5-bit to 8-bit
        dst[j++] = (g * 259 + 33) >> 6; // 6-bit to 8-bit
        dst[j++] = (b * 527 + 23) >> 6; // 5-bit to 8-bit
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(init_camera());

    // Prepare YOLO model from embedded binary
    const uint8_t *model_start = _binary_models_yolov3_tiny_320x320_int8_bin_start;
    const uint8_t *model_end = _binary_models_yolov3_tiny_320x320_int8_bin_end;
    size_t model_size = (size_t)(model_end - model_start);
    if (model_size == 0) {
        ESP_LOGE(TAG, "YOLO model not embedded. Please place model file under main/models/yolov3_tiny_320x320_int8.bin");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }

    // Create YOLO runtime
    dl::model::Model *net = nullptr;
    try {
        net = dl::model::Model::create(model_start, model_size);
    } catch (...) {
        ESP_LOGE(TAG, "Failed to create DL model from embedded data");
        return;
    }

    const int in_size = CONFIG_PERSON_COUNTER_YOLO_INPUT_SIZE;
    const float score_thresh = CONFIG_PERSON_COUNTER_SCORE_THRESHOLD / 100.0f;
    const float iou_thresh = CONFIG_PERSON_COUNTER_NMS_IOU / 100.0f;

    ESP_LOGI(TAG, "Starting detection loop (input %dx%d)", in_size, in_size);

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Convert to RGB888
        std::vector<uint8_t> rgb888;
        rgb565_to_rgb888(fb, rgb888);

        // Resize to model input
        dl::image::Image<dl::image::RGB888> img_src(rgb888.data(), fb->width, fb->height, fb->width * 3);
        std::vector<uint8_t> resized(in_size * in_size * 3);
        dl::image::Image<dl::image::RGB888> img_dst(resized.data(), in_size, in_size, in_size * 3);
        dl::image::resize(img_src, img_dst);

        // Run inference
        dl::Tensor input_tensor(dl::DataType::UINT8, {1, in_size, in_size, 3}, resized.data());
        std::vector<dl::Tensor> outputs;
        try {
            outputs = net->infer({input_tensor});
        } catch (...) {
            ESP_LOGE(TAG, "Inference failed");
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // YOLO postprocess (assumes COCO classes, person=0)
        std::vector<dl::postprocess::yolo::Detection> dets;
        try {
            dets = dl::postprocess::yolo::decode(outputs, in_size, in_size, score_thresh, iou_thresh);
        } catch (...) {
            ESP_LOGE(TAG, "Postprocess failed");
            dets.clear();
        }

        // Count persons
        int person_count = 0;
        for (const auto &d : dets) {
            if (d.class_id == 0 && d.score >= score_thresh) {
                person_count++;
            }
        }

        ESP_LOGI(TAG, "Persons: %d, dets: %d", person_count, (int)dets.size());

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
