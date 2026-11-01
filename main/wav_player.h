#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Mounts the SPIFFS "audio" partition and starts the playback task.
esp_err_t wav_player_init(void);

// Play /audio/<name> now, or ignore the request if something is already playing/queued.
void wav_player_play(const char *name);

// Queue /audio/<name> behind anything already playing (for prompts that must not be dropped).
void wav_player_enqueue(const char *name);

bool wav_player_is_busy(void);
