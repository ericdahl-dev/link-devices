#pragma once
// ARC-014: the Link measurement poll, once. The two boards' glue
// (KitchenSync/main/link_measure_io.c, X32Link/link_measurement_io.cpp) were ~90%
// identical — the same run_action dispatch and the same 4-step poll (epoch reset,
// trigger, drain pongs, watchdog), copied. The only genuine per-target difference is
// three socket primitives. This module owns the poll; each board supplies a
// LinkMeasureOps vtable. The sequence now exists in one host-testable place instead of
// drifting between copies.
#include <stdint.h>
#include "link_measurement_session.h"   // LinkSession

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Send a prebuilt ping datagram to the peer's advertised endpoint (ip is wire-order
    // be32, port host-order — as from link_proto_peer_endpoint()).
    void    (*send_to)(uint32_t ip, uint16_t port, const uint8_t* buf, int len);
    // Read one pending datagram into buf (<= cap); returns <= 0 when nothing queued.
    int     (*recv_one)(uint8_t* buf, int cap);
    // Monotonic microsecond clock (esp_timer_get_time() on both boards).
    int64_t (*now_us)(void);
} LinkMeasureOps;

// Drive one measurement poll against `ops`. Owns the action dispatch + the 4-step
// sequence; `session` is the per-board LinkSession. Assumes the socket is already open.
void link_measure_pump(LinkSession* session, const LinkMeasureOps* ops);

#ifdef __cplusplus
}
#endif
