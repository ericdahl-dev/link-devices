#include "transport.h"

void transport_reset(Transport* t) {
    t->primed  = false;
    t->playing = false;
}

TransportAction transport_update(Transport* t, bool valid, bool playing) {
    if (!valid) return TRANSPORT_NONE;         // no real reading yet — hold
    if (!t->primed) {                          // first observation primes silently
        t->primed  = true;
        t->playing = playing;
        return TRANSPORT_NONE;
    }
    if (playing == t->playing) return TRANSPORT_NONE;
    t->playing = playing;
    return playing ? TRANSPORT_START : TRANSPORT_STOP;
}
