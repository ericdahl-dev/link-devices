#pragma once
// KitchenSync glue: ESP-IDF/lwip port of X32Link/link_measurement_io.cpp — the
// unicast ping/pong measurement client that derives this device's GhostXForm
// (host->ghost clock offset) from a Link peer's mep4 endpoint. All protocol
// logic (TLV build/parse, sample math, GhostXForm derivation) and all decision
// policy (peer targeting, re-measure scheduling, epoch-reset response, RX flush,
// silence watchdog) live in the pure, host-tested link_measurement.{h,c} +
// link_measurement_session.{h,c}, reused UNCHANGED (ADR-0003/0005). This file is
// socket + timing glue only — not host-testable (needs real lwip sockets).
//
// The committed GhostXForm is read back via link_measurement_current_xform()
// from link_measurement.h (module-global, same as the S3).

#ifdef __cplusplus
extern "C" {
#endif

// Service the measurement client once: detect the reference-peer trigger via
// link_proto_peer_endpoint(), drive the in-flight ping/pong exchange, the 50 ms
// watchdog retry, the 250 ms failure path, and the transport-reorigin (epoch)
// reset. Opens its own unicast UDP socket lazily on the first call (non-blocking,
// so it is safe to call from the 1 ms clock loop). Call every clock-loop tick,
// mirroring the S3's link_measurement_io_poll().
void link_measure_io_poll(void);

#ifdef __cplusplus
}
#endif
