#include "boot_button.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BTN_GPIO 9
#define PAIR_MS 3000
#define UNPAIR_MS 10000
#define SHORT_PRESS_MIN_MS 100
#define POLL_MS 50
#define UNPAIR_WARN_MS 1200

static boot_button_cb_t s_cb;

static void button_task(void *arg)
{
    int held_ms = 0;
    bool fired_unpair = false;
    while (1) {
        bool pressed = gpio_get_level(BTN_GPIO) == 0;
        if (pressed) {
            held_ms += POLL_MS;
            if (held_ms == PAIR_MS) {
                s_cb(BTN_EVENT_PAIR_READY);
            }
            if (held_ms >= UNPAIR_MS && !fired_unpair) {
                fired_unpair = true;
                s_cb(BTN_EVENT_UNPAIR_READY);
                vTaskDelay(pdMS_TO_TICKS(UNPAIR_WARN_MS));
                s_cb(BTN_EVENT_UNPAIR);
            }
        } else {
            if (held_ms >= PAIR_MS && !fired_unpair) {
                s_cb(BTN_EVENT_PAIR);
            } else if (held_ms >= SHORT_PRESS_MIN_MS && held_ms < PAIR_MS) {
                s_cb(BTN_EVENT_SHORT_PRESS);
            }
            held_ms = 0;
            fired_unpair = false;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void boot_button_init(boot_button_cb_t cb)
{
    s_cb = cb;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    xTaskCreate(button_task, "boot_btn", 3072, NULL, 3, NULL);
}
