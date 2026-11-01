#include "status_led.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_GPIO 8

static const char *TAG = "status_led";
static led_strip_handle_t s_strip;
static volatile kf_state_t s_state = KF_STATE_NOT_PAIRED;

static void led_task(void *arg)
{
    bool phase = false;
    while (1) {
        switch (s_state) {
        case KF_STATE_NOT_PAIRED:
            led_strip_set_pixel(s_strip, 0, 20, 0, 0);
            break;
        case KF_STATE_PAIRING:
            if (phase) {
                led_strip_set_pixel(s_strip, 0, 0, 0, 25);
            } else {
                led_strip_clear(s_strip);
            }
            break;
        case KF_STATE_PAIRED:
            led_strip_set_pixel(s_strip, 0, 0, 12, 0);
            break;
        }
        led_strip_refresh(s_strip);
        phase = !phase;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void status_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);
    xTaskCreate(led_task, "status_led", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "LED on GPIO%d", LED_GPIO);
}

void status_led_set(kf_state_t state)
{
    s_state = state;
}
