#pragma once

typedef enum {
    KF_STATE_NOT_PAIRED,
    KF_STATE_PAIRING,
    KF_STATE_PAIRED,
} kf_state_t;

void status_led_init(void);
void status_led_set(kf_state_t state);
