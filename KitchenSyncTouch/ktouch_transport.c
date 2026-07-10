// One-slot transport intent mailbox — see ktouch_transport.h (ESP-016).
#include "ktouch_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE          s_mux  = portMUX_INITIALIZER_UNLOCKED;
static TransportLaunchIntent s_slot = TL_INTENT_NONE;

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
