#include "camera_person.h"

#include <string.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "camera_person";
static bool s_cam_started = false;

// AI-Thinker ESP32-CAM default pinout
#ifndef CONFIG_CAMERA_CUSTOM_PINS
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22
#endif

static inline int fast_abs_int(int v) { return v < 0 ? -v : v; }

static inline bool is_skin_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // Simple RGB heuristic for skin detection
    // See: Peer-reviewed heuristics widely used for low-power detectors
    if (r > 95 && g > 40 && b > 20) {
        int maxc = r; if (g > maxc) maxc = g; if (b > maxc) maxc = b;
        int minc = r; if (g < minc) minc = g; if (b < minc) minc = b;
        if ((maxc - minc) > 15 && fast_abs_int(r - g) > 15 && r > g && r > b) {
            return true;
        }
    }
    return false;
}

esp_err_t camera_start(void) {
    if (s_cam_started) return ESP_OK;

    camera_config_t config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_RGB565,
#ifdef CONFIG_CAMERA_FRAMESIZE_QQVGA
        .frame_size = FRAMESIZE_QQVGA,
#else
        .frame_size = FRAMESIZE_QVGA,
#endif
        .jpeg_quality = 12,
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST
    };

#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
    config.fb_location = CAMERA_FB_IN_PSRAM;
#endif

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, config.frame_size);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
        s->set_contrast(s, 0);
        s->set_gain_ctrl(s, 1);
        s->set_exposure_ctrl(s, 1);
    }

    // Warmup for auto-exposure
    vTaskDelay(pdMS_TO_TICKS(CONFIG_CAMERA_WARMUP_MS));

    s_cam_started = true;
    return ESP_OK;
}

void camera_stop(void) {
    if (!s_cam_started) return;
    esp_camera_deinit();
    s_cam_started = false;
}

bool camera_is_running(void) { return s_cam_started; }

bool camera_person_visible(float *out_ratio) {
    if (!s_cam_started) return false;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;

    size_t width = fb->width;
    size_t height = fb->height;

    // Expect RGB565
    if (fb->format != PIXFORMAT_RGB565) {
        // Unsupported format
        esp_camera_fb_return(fb);
        return false;
    }

    const uint16_t *pixels = (const uint16_t *)fb->buf;
    size_t skin_count = 0;
    size_t sample_count = 0;

    // Downsample by step to save CPU
    const int step_x = 2;
    const int step_y = 2;

    for (size_t y = 0; y < height; y += step_y) {
        for (size_t x = 0; x < width; x += step_x) {
            size_t idx = y * width + x;
            uint16_t p = pixels[idx];
            uint8_t r5 = (p >> 11) & 0x1F;
            uint8_t g6 = (p >> 5) & 0x3F;
            uint8_t b5 = (p >> 0) & 0x1F;
            // Expand to 8-bit
            uint8_t r = (r5 * 527 + 23) >> 6;  // ~ *255/31
            uint8_t g = (g6 * 259 + 33) >> 6;  // ~ *255/63
            uint8_t b = (b5 * 527 + 23) >> 6;  // ~ *255/31

            if (is_skin_rgb(r, g, b)) {
                skin_count++;
            }
            sample_count++;
        }
    }

    esp_camera_fb_return(fb);

    float ratio = 0.0f;
    if (sample_count > 0) {
        ratio = (float)skin_count / (float)sample_count;
    }
    if (out_ratio) *out_ratio = ratio;

    // Threshold from percentage in Kconfig
    float min_ratio = (float)CONFIG_PERSON_MIN_SKIN_RATIO_PERCENT / 100.0f;
    return ratio >= min_ratio;
}
