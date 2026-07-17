#pragma once
// Pure MIDI-transport state machine (P4-008): turns the Link session's isPlaying
// flag into MIDI Start (0xFA) / Stop (0xFC) edges, emitting exactly once per
// transition. No Arduino/ESP-IDF dependency — host-tested in test/test_transport.c.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRANSPORT_NONE = 0,
    TRANSPORT_START,   // Link went stopped -> playing  (emit 0xFA)
    TRANSPORT_STOP,    // Link went playing -> stopped  (emit 0xFC)
    TRANSPORT_RESTART, // realign on the bar line: stop THEN start (emit 0xFC then 0xFA)
} TransportAction;

typedef struct {
    bool primed;
    bool playing;
} Transport;

void transport_reset(Transport* t);

// Feed the current (valid, playing) state and get the edge action. `valid=false`
// is a no-op (returns NONE, state held) — pass link_proto_start_stop_seen() so we
// prime on the first REAL observation, not on the default. The first valid call
// primes (records `playing`, returns NONE) so joining mid-play does NOT emit a
// spurious Start (which would jump downstream gear to bar 1). Subsequent valid
// calls emit START / STOP on a change, NONE otherwise.
TransportAction transport_update(Transport* t, bool valid, bool playing);

#ifdef __cplusplus
}
#endif
