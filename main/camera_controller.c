#include "camera_controller.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "camera";

// AI-Thinker ESP32-CAM (OV2640) default pin map
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

static bool s_camera_on = false;

static pixformat_t cfg_pixfmt_from_kconfig(void)
{
    return (CONFIG_APP_CAMERA_PIXEL_FORMAT == 1) ? PIXFORMAT_RGB565 : PIXFORMAT_JPEG;
}

static framesize_t cfg_framesize_from_kconfig(void)
{
    switch (CONFIG_APP_CAMERA_FRAME_SIZE) {
        case 0: return FRAMESIZE_QVGA;
        case 1: return FRAMESIZE_VGA;
        case 2: return FRAMESIZE_SVGA;
        case 3: return FRAMESIZE_XGA;
        case 4: return FRAMESIZE_SXGA;
        case 5: return FRAMESIZE_UXGA;
        default: return FRAMESIZE_QVGA;
    }
}

esp_err_t camera_init(void)
{
    if (s_camera_on) return ESP_OK;

    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
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
        .pixel_format = cfg_pixfmt_from_kconfig(),
        .frame_size = cfg_framesize_from_kconfig(),
        .jpeg_quality = 12,
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_camera_on = true;
    return ESP_OK;
}

void camera_deinit(void)
{
    if (!s_camera_on) return;
    esp_camera_deinit();
    s_camera_on = false;
}

bool camera_check_person(camera_status_t *out_status)
{
    if (out_status) out_status->person_visible = false;
    if (!s_camera_on) return false;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return false;
    }

    // Placeholder: use naive heuristic as we do not link ESP-WHO here.
    // A simple motion proxy: if frame size differs or brightness shifts, assume "person".
    static size_t last_len = 0;
    bool person = false;
    if (fb->len > 0) {
        if (last_len == 0) last_len = fb->len;
        size_t diff = (fb->len > last_len) ? (fb->len - last_len) : (last_len - fb->len);
        if (diff > (fb->len / 50)) { // >2% change
            person = true;
        }
        last_len = fb->len;
    }

    esp_camera_fb_return(fb);
    if (out_status) out_status->person_visible = person;
    return person;
}
