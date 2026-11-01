#include <stdio.h>
#include "audio_out.h"
#include "boot_button.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "wav_player.h"
#include "zb_coord.h"

static const char *TAG = "kittyface";

static void on_button(boot_button_event_t ev)
{
    switch (ev) {
    case BTN_EVENT_SHORT_PRESS: {
        int level = audio_out_next_volume();
        char name[16];
        snprintf(name, sizeof(name), "level_%d.wav", level);
        wav_player_enqueue("volume.wav");
        wav_player_enqueue(name);
        break;
    }
    case BTN_EVENT_PAIR_READY:
        wav_player_enqueue("!beep1");
        break;
    case BTN_EVENT_PAIR:
        zb_coord_start_pairing();
        break;
    case BTN_EVENT_UNPAIR_READY:
        wav_player_enqueue("!beep2");
        break;
    case BTN_EVENT_UNPAIR:
        zb_coord_unpair();
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    status_led_init();
    ESP_ERROR_CHECK(audio_out_init());
    ESP_ERROR_CHECK(wav_player_init());
    wav_player_enqueue("welcome_msg.wav");
    ESP_ERROR_CHECK(zb_coord_start());
    boot_button_init(on_button);
    ESP_LOGI(TAG, "ready");
}
