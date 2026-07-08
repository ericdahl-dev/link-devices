// Thin Arduino glue: WiFiUDP unicast ping/pong exchange against a Link peer's
// mep4 endpoint (discovered by LNK-017's link_protocol.c). All decision policy —
// peer targeting, re-measure scheduling, the transport re-origin response, the
// RX flush, the silence watchdog — lives in the pure, host-tested
// link_measurement_session.c (LNK-031); the wire format + sample/offset math is
// in link_measurement.c. This file owns only sockets, timing, and translating
// the session's actions into I/O.
#include "link_measurement_io.h"
#include "link_measurement.h"
#include "link_measurement_session.h"
#include "link_measure_pump.h"   // ARC-014: shared poll behind a socket vtable
#include "link_protocol.h"
#include <WiFiUdp.h>
#include "esp_timer.h"

// Independent socket from link_listener.cpp's multicast discovery conversation
// (port 20808) — a separate unicast conversation. Port 0 = OS-assigned ephemeral
// local port; we only ever talk to the peer's advertised mep4 endpoint.
static WiFiUDP    s_udp;
static bool       s_socket_open = false;
static LinkSession s_session;

// esp_timer_get_time(), not millis()/micros() — see LNK-019 for why microsecond
// precision matters specifically in this path.
static int64_t now_us() { return esp_timer_get_time(); }

// Build an IPAddress from our wire-format uint32_t (big-endian octets). The
// 4-octet ctor is used deliberately — IPAddress(uint32_t) assigns the raw word
// into a little-endian layout with no byteswap, which would mis-order the octets.
static IPAddress ip_from_be32(uint32_t ip) {
    return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                      (ip >> 8) & 0xFF, ip & 0xFF);
}

// ARC-014: the poll sequence + action dispatch live in the shared link_measure_pump;
// this glue supplies only the three WiFiUDP socket ops.
static void io_send_to(uint32_t ip, uint16_t port, const uint8_t* buf, int len) {
    s_udp.beginPacket(ip_from_be32(ip), port);
    s_udp.write(buf, len);
    s_udp.endPacket();
}

static int io_recv_one(uint8_t* buf, int cap) {
    if (s_udp.parsePacket() <= 0) return 0;   // nothing queued
    return s_udp.read(buf, cap);
}

static int64_t io_now_us() { return now_us(); }

static const LinkMeasureOps s_ops = {
    io_send_to,
    io_recv_one,
    io_now_us,
};

void link_measurement_io_begin() {
    s_udp.stop();
    s_socket_open = s_udp.begin((uint16_t)0) != 0;
    link_session_reset(&s_session);
    link_measurement_reset();
}

void link_measurement_io_poll() {
    if (!s_socket_open) return;
    link_measure_pump(&s_session, &s_ops);
}
