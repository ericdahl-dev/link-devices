// One-slot transport intent mailbox — see ktouch_transport.h (ESP-016).
#include "ktouch_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE          s_mux  = portMUX_INITIALIZER_UNLOCKED;
static TransportLaunchIntent s_slot = TL_INTENT_NONE;
static volatile int          s_state = TL_STOPPED;   // published by the writer

void ktouch_transport_post(TransportLaunchIntent intent) {
    portENTER_CRITICAL(&s_mux);
    s_slot = intent;   // latest press wins (a fast STOP after PLAY overrides)
    portEXIT_CRITICAL(&s_mux);
}

TransportLaunchIntent ktouch_transport_take(void) {
    portENTER_CRITICAL(&s_mux);
    TransportLaunchIntent i = s_slot;
    s_slot = TL_INTENT_NONE;
    portEXIT_CRITICAL(&s_mux);
    return i;
}

void ktouch_transport_publish_state(int launch_state) { s_state = launch_state; }
int  ktouch_transport_state(void)                     { return s_state; }

// ESP-025. Plain scalar, one writer (the 1 ms MIDI task) and one reader (loop()) —
// same shape as the launch-state publish above, no lock needed.
static volatile bool s_realign_armed = false;
void ktouch_transport_publish_realign(bool armed) { s_realign_armed = armed; }
bool ktouch_transport_realign_armed(void)         { return s_realign_armed; }
