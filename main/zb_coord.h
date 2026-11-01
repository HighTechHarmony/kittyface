#pragma once

#include "esp_err.h"

// Starts the Zigbee coordinator task. Call after NVS and wav_player are initialised.
esp_err_t zb_coord_start(void);

// Open the network for joining (pairing mode). Safe to call from any task.
void zb_coord_start_pairing(void);

// Forget the paired button, wipe Zigbee state and reboot.
void zb_coord_unpair(void);
