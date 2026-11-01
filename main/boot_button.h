#pragma once

typedef enum {
    BTN_EVENT_SHORT_PRESS,  // released before 3 s
    BTN_EVENT_PAIR_READY,   // held 3 s: releasing now opens pairing
    BTN_EVENT_PAIR,
    BTN_EVENT_UNPAIR_READY, // held 10 s: unpair is about to happen
    BTN_EVENT_UNPAIR,
} boot_button_event_t;

typedef void (*boot_button_cb_t)(boot_button_event_t ev);

// BOOT button (GPIO9): short press -> SHORT_PRESS; 3 s -> PAIR_READY, release after that -> PAIR;
// 10 s -> UNPAIR_READY then UNPAIR.
void boot_button_init(boot_button_cb_t cb);
