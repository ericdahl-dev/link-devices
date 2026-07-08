/*
 * KitchenSync glue: ESP-IDF/lwip port of X32Link/link_measurement_io.cpp (P4-009).
 *
 * Unicast ping/pong exchange against a Link peer's mep4 endpoint (discovered by
 * link_protocol.c's gossip parse) to derive this device's GhostXForm. All
 * decision policy — peer targeting, re-measure scheduling, the transport
 * re-origin (epoch) response, the RX flush, the silence watchdog — lives in the
 * pure, host-tested link_measurement_session.c (LNK-031); the wire format +
 * sample/offset math is link_measurement.c. This file owns only the socket,
 * timing, and translating the session's actions into lwip I/O — the ESP-IDF
 * twin of the S3's WiFiUDP glue (ADR-0003/0005).
 *
 * Socket style mirrors wifi_link.c's multicast listener, but this is a separate
 * *unicast* conversation on an ephemeral local port (the multicast discovery
 * socket owns port 20808). recv is non-blocking so this poll can run inside
 * clock_out_task's 1 ms loop without ever stalling the clock generator; the
 * session's 50 ms watchdog, not a blocking recv, sets the retry cadence.
 */
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "link_measure_io.h"
#include "link_measurement.h"
#include "link_measurement_session.h"
#include "link_measure_pump.h"   // ARC-014: shared poll behind a socket vtable
#include "link_protocol.h"

static const char *TAG = "link_measure";

static int         s_sock = -1;
static LinkSession s_session;

/* esp_timer_get_time(), not millis()/micros() — microsecond precision matters in
 * this path (same clock the measurement math is expressed in; see LNK-019). */
static int64_t now_us(void) { return esp_timer_get_time(); }

/* Open the unicast measurement socket: ephemeral local port, non-blocking.
 * Separate from wifi_link.c's multicast socket. Returns true once open. */
static bool open_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket errno %d", errno); return false; }

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(0),           /* OS-assigned ephemeral port */
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind errno %d", errno); close(sock); return false;
    }

    /* Non-blocking: this poll runs in the 1 ms clock loop and must never block
     * on recv. The session watchdog drives the retry/timeout cadence instead. */
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    s_sock = sock;
    link_session_reset(&s_session);
    link_measurement_reset();
    ESP_LOGI(TAG, "unicast measurement socket open (ephemeral port)");
    return true;
}

/* ARC-014: the poll sequence + action dispatch live in the shared link_measure_pump;
 * this glue supplies only the three lwip socket ops. */

/* ip/port from link_proto_peer_endpoint() are wire-order be32 / host-order uint16;
 * htonl/htons put them into the network byte order lwip's sockaddr_in expects. */
static void io_send_to(uint32_t ip, uint16_t port, const uint8_t *buf, int len)
{
    struct sockaddr_in dest = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(ip),
    };
    sendto(s_sock, buf, (size_t)len, 0, (struct sockaddr *)&dest, sizeof(dest));
}

/* Non-blocking read of one pending datagram; <=0 when nothing is queued. */
static int io_recv_one(uint8_t *buf, int cap)
{
    return recvfrom(s_sock, buf, (size_t)cap, 0, NULL, NULL);
}

static const LinkMeasureOps s_ops = {
    .send_to  = io_send_to,
    .recv_one = io_recv_one,
    .now_us   = now_us,
};

void link_measure_io_poll(void)
{
    if (s_sock < 0 && !open_socket()) return;
    link_measure_pump(&s_session, &s_ops);
}
