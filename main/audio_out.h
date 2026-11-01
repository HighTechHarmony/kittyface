#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_out_init(void);

#define AUDIO_VOLUME_LEVELS 4
#define AUDIO_VOLUME_DEFAULT 3

int audio_out_get_volume(void);
// Advance to the next level (wraps 4 -> 1), persist it, and return the new level.
int audio_out_next_volume(void);
esp_err_t audio_out_write(const int16_t *samples, size_t count);
esp_err_t audio_out_play_tone(float freq_hz, int duration_ms, float amplitude);
