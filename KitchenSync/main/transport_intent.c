// See transport_intent.h (ESP-011). Thin glue: a tiny mailbox between the HTTP
// task and the clock task.
#include "transport_intent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TransportLaunchIntent s_pending[KS_CLOCK_OUTPUTS];

void transport_intent_post(int out, TransportLaunchIntent intent) {
    portENTER_CRITICAL(&s_lock);
    if (out < 0) {
        for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) s_pending[i] = intent;
    } else if (out < KS_CLOCK_OUTPUTS) {
        s_pending[out] = intent;
    }
    portEXIT_CRITICAL(&s_lock);
}

void transport_intent_take(TransportLaunchIntent dst[KS_CLOCK_OUTPUTS]) {
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        dst[i] = s_pending[i];
        s_pending[i] = TL_INTENT_NONE;   // consume: one press, one action
    }
    portEXIT_CRITICAL(&s_lock);
}
