#include "wav_player.h"

#include <stdio.h>
#include <string.h>
#include "audio_out.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define MOUNT_POINT "/audio"
#define MAX_NAME 32
#define QUEUE_DEPTH 6

static const char *TAG = "wav_player";
static QueueHandle_t s_queue;
static volatile bool s_busy;

typedef struct {
    uint16_t channels;
    uint32_t rate;
    uint16_t bits;
    long data_off;
    uint32_t data_len;
} wav_info_t;

static bool parse_wav(FILE *f, wav_info_t *w)
{
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        return false;
    }
    bool have_fmt = false;
    uint8_t ck[8];
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t len = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16) {
                return false;
            }
            w->channels = fmt[2] | (fmt[3] << 8);
            w->rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            w->bits = fmt[14] | (fmt[15] << 8);
            have_fmt = true;
            fseek(f, len - 16 + (len & 1), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            w->data_off = ftell(f);
            w->data_len = len;
            return have_fmt;
        } else {
            fseek(f, len + (len & 1), SEEK_CUR);
        }
    }
    return false;
}

static void play_beeps(int count)
{
    for (int i = 0; i < count; i++) {
        audio_out_play_tone(1000.0f, 150, 0.25f);
        audio_out_play_tone(0.0f, 100, 0.0f);
    }
}

static void play_file(const char *name)
{
    // "!beepN" is a synthesized cue (N beeps) that shares the same ordered queue.
    if (name[0] == '!') {
        play_beeps(name[5] - '0');
        return;
    }
    char path[sizeof(MOUNT_POINT) + MAX_NAME + 2];
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return;
    }
    wav_info_t w;
    if (!parse_wav(f, &w) || w.channels != 1 || w.bits != 16 || w.rate != CONFIG_KF_SAMPLE_RATE) {
        ESP_LOGE(TAG, "%s must be 16-bit mono %d Hz PCM WAV", path, CONFIG_KF_SAMPLE_RATE);
        fclose(f);
        return;
    }
    ESP_LOGI(TAG, "playing %s (%lu bytes)", path, (unsigned long)w.data_len);

    int16_t buf[512];
    uint32_t remaining = w.data_len;
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t got = fread(buf, 1, want, f);
        if (got == 0) {
            break;
        }
        if (audio_out_write(buf, got / 2) != ESP_OK) {
            break;
        }
        remaining -= got;
    }
    fclose(f);

    // Flush with silence so the tail isn't cut off or looped.
    memset(buf, 0, sizeof(buf));
    audio_out_write(buf, 512);
}

static void player_task(void *arg)
{
    char name[MAX_NAME];
    while (1) {
        if (xQueueReceive(s_queue, name, portMAX_DELAY) == pdTRUE) {
            s_busy = true;
            play_file(name);
            s_busy = uxQueueMessagesWaiting(s_queue) > 0;
        }
    }
}

esp_err_t wav_player_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = "audio",
        .max_files = 2,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
        return err;
    }
    s_queue = xQueueCreate(QUEUE_DEPTH, MAX_NAME);
    xTaskCreate(player_task, "wav_player", 4096, NULL, 4, NULL);
    return ESP_OK;
}

bool wav_player_is_busy(void)
{
    return s_busy || uxQueueMessagesWaiting(s_queue) > 0;
}

void wav_player_play(const char *name)
{
    if (wav_player_is_busy()) {
        return;
    }
    wav_player_enqueue(name);
}

void wav_player_enqueue(const char *name)
{
    char buf[MAX_NAME] = {0};
    strlcpy(buf, name, sizeof(buf));
    xQueueSend(s_queue, buf, 0);
}
