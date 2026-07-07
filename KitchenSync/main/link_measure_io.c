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

static void send_ping(uint32_t ip, uint16_t port, bool has_prev, int64_t prev_ghost,
                      int64_t when_us)
{
    uint8_t buf[LINK_MEASUREMENT_PING_MAX_LEN];
    int n = link_measurement_build_ping(buf, sizeof(buf), when_us, has_prev, prev_ghost);
    if (n <= 0) return;   /* shouldn't happen; buffer is sized for worst case */

    /* ip/port from link_proto_peer_endpoint() are wire-order be32 / host-order
     * uint16 (be32()/be16() of the gossiped mep4 TLV). htonl/htons put them into
     * the network byte order lwip's sockaddr_in expects — the ESP-IDF equivalent
     * of the S3 glue's ip_from_be32() 4-octet IPAddress ctor. */
    struct sockaddr_in dest = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(ip),
    };
    sendto(s_sock, buf, (size_t)n, 0, (struct sockaddr *)&dest, sizeof(dest));
}

/* Non-blocking read of one pending datagram; <=0 when nothing is queued. */
static int recv_one(uint8_t *buf, int cap)
{
    return recvfrom(s_sock, buf, (size_t)cap, 0, NULL, NULL);
}

/* Execute one session action against real lwip I/O. */
static void run_action(const LinkSessionAct *a)
{
    switch (a->type) {
        case LS_FLUSH_RX: {
            uint8_t junk[128];
            while (recv_one(junk, sizeof(junk)) > 0) { /* discard stale pongs */ }
            break;
        }
        case LS_START_ATTEMPT: link_measurement_attempt_begin(); break;
        case LS_SEND_PING:     send_ping(a->ip, a->port, a->has_prev_ghost,
                                         a->prev_ghost_us, a->send_time_us); break;
        case LS_END_OK:        link_measurement_attempt_end(true);  break;
        case LS_END_FAIL:      link_measurement_attempt_end(false); break;
        case LS_RESET_XFORM:   link_measurement_reset();            break;
    }
}

static void run_all(const LinkSessionAct *acts, int n)
{
    for (int i = 0; i < n; i++) run_action(&acts[i]);
}

void link_measure_io_poll(void)
{
    if (s_sock < 0 && !open_socket()) return;

    LinkSessionAct acts[4];
    int n;

    /* 1. Timeline gossip -> epoch-reset detection (LNK-026). */
    LinkTimeline tl;
    bool tl_valid = link_proto_timeline(&tl);
    n = link_session_on_timeline(&s_session, tl_valid, tl_valid ? tl.time_origin_us : 0,
                                 acts, 4);
    run_all(acts, n);

    /* 2. Trigger: first peer with an advertised mep4 endpoint. */
    uint32_t ip = 0; uint16_t port = 0; bool found = false;
    int peer_count = link_proto_peers();
    for (int i = 0; i < peer_count && !found; i++) {
        if (link_proto_peer_endpoint(i, &ip, &port)) found = true;
    }
    n = link_session_on_trigger(&s_session, found, ip, port, now_us(),
                                link_measurement_active(), acts, 4);
    run_all(acts, n);

    if (!link_measurement_active()) return;

    /* 3. Drain pongs; the session decides commit-vs-reping per pong. */
    uint8_t buf[128];
    int len;
    while ((len = recv_one(buf, sizeof(buf))) > 0) {
        int64_t h_recv = now_us();

        LinkPongFields fields;
        if (!link_measurement_parse_pong(buf, len, &fields)) continue;

        link_measurement_add_pong_samples(h_recv, &fields);   /* side effect: sample buffer */
        n = link_session_on_pong(&s_session, h_recv, fields.has_ghost_time,
                                 fields.ghost_time_us, link_measurement_samples_count(),
                                 acts, 4);
        bool ended = false;
        for (int i = 0; i < n; i++) {
            run_action(&acts[i]);
            if (acts[i].type == LS_END_OK) ended = true;
        }
        if (ended) return;   /* attempt committed this poll; nothing left to drive */
    }

    /* 4. Silence watchdog. */
    n = link_session_on_watchdog(&s_session, now_us(), acts, 4);
    run_all(acts, n);
}
