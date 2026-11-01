#include "audio_out.h"

#include <math.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "audio_out";
static i2s_chan_handle_t s_tx;

// Linear gain per volume level 1..4.
static const float k_gain[AUDIO_VOLUME_LEVELS] = {0.15f, 0.35f, 0.65f, 1.0f};
static volatile int s_level = AUDIO_VOLUME_DEFAULT;

int audio_out_get_volume(void)
{
    return s_level;
}

static void load_volume(void)
{
    nvs_handle_t h;
    uint8_t v;
    if (nvs_open("kf", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "volume", &v) == ESP_OK && v >= 1 && v <= AUDIO_VOLUME_LEVELS) {
            s_level = v;
        }
        nvs_close(h);
    }
}

int audio_out_next_volume(void)
{
    int next = s_level % AUDIO_VOLUME_LEVELS + 1;
    s_level = next;
    nvs_handle_t h;
    if (nvs_open("kf", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "volume", (uint8_t)next);
        nvs_commit(h);
        nvs_close(h);
    }
    return next;
}

esp_err_t audio_out_init(void)
{
    load_volume();
    ESP_LOGI(TAG, "volume level %d", s_level);
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    // Send silence (not stale DMA data) whenever we underrun, e.g. between tones.
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "new channel");

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_KF_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_KF_I2S_BCLK_GPIO,
            .ws = CONFIG_KF_I2S_WS_GPIO,
            .dout = CONFIG_KF_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &cfg), TAG, "init std");
    return i2s_channel_enable(s_tx);
}

esp_err_t audio_out_write(const int16_t *samples, size_t count)
{
    enum { CHUNK = 256 };
    int16_t scaled[CHUNK];
    float gain = k_gain[s_level - 1];

    while (count > 0) {
        size_t n = count < CHUNK ? count : CHUNK;
        for (size_t i = 0; i < n; i++) {
            scaled[i] = (int16_t)(samples[i] * gain);
        }
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, scaled, n * sizeof(int16_t), &written, 10000), TAG, "write");
        samples += n;
        count -= n;
    }
    return ESP_OK;
}

esp_err_t audio_out_play_tone(float freq_hz, int duration_ms, float amplitude)
{
    enum { CHUNK = 256 };
    int16_t buf[CHUNK];
    size_t total = (size_t)CONFIG_KF_SAMPLE_RATE * duration_ms / 1000;
    float step = 2.0f * (float)M_PI * freq_hz / CONFIG_KF_SAMPLE_RATE;
    float phase = 0;

    for (size_t done = 0; done < total;) {
        size_t n = total - done < CHUNK ? total - done : CHUNK;
        for (size_t i = 0; i < n; i++) {
            // 10 ms linear fade in/out to avoid clicks at tone boundaries.
            size_t pos = done + i;
            size_t ramp = CONFIG_KF_SAMPLE_RATE / 100;
            float env = 1.0f;
            if (pos < ramp) {
                env = (float)pos / ramp;
            } else if (total - pos < ramp) {
                env = (float)(total - pos) / ramp;
            }
            buf[i] = (int16_t)(sinf(phase) * amplitude * env * 32767.0f);
            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        ESP_RETURN_ON_ERROR(audio_out_write(buf, n), TAG, "write");
        done += n;
    }
    return ESP_OK;
}
