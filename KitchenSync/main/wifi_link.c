/*
 * KitchenSync glue: WiFi station (C6 hosted stack) + Ableton Link multicast listener.
 *
 * The gossip parsing + tempo/timeline math is the pure, host-tested
 * link_protocol.c (reused unchanged). This file is thin I/O: associate to WiFi,
 * join the Link group, and feed datagrams to the parser. Proven in the
 * scratchpad p4_wifi_link spike (P4-004).
 */
#include <string.h>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_link.h"
#include "wifi_conn_policy.h"   // ARC-013: shared connection lifecycle

static const char *TAG = "wifi_link";

#define LINK_MCAST_ADDR "224.76.78.75"   /* Ableton Link session multicast group */
#define LINK_PORT       20808

/* link_protocol.c calls this weak platform hook for a millisecond clock. */
uint32_t link_proto_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- Link multicast listener --------------------------------------------- */

static int open_link_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket errno %d", errno); return -1; }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(LINK_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind errno %d", errno); close(sock); return -1;
    }

    struct ip_mreq mreq = {0};
    mreq.imr_multiaddr.s_addr = inet_addr(LINK_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "join errno %d", errno); close(sock); return -1;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "joined %s:%d", LINK_MCAST_ADDR, LINK_PORT);
    return sock;
}

static void link_task(void *arg)
{
    link_proto_reset();
    int sock = open_link_socket();
    if (sock < 0) { vTaskDelete(NULL); return; }

    uint8_t buf[512];
    int64_t last_tick = 0;
    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
        if (n > 0) link_proto_parse(buf, n);

        int64_t now = esp_timer_get_time();
        if (now - last_tick >= 100000) { last_tick = now; link_proto_tick(); }
    }
}

/* ---- WiFi station -------------------------------------------------------- */

#define AP_SSID "KitchenSync-Setup"

/* ARC-013: the whole connection lifecycle (give-up budget, retry-forever-after-IP,
 * spawn-listener-once, AP-terminal) is the pure wifi_conn_policy; this glue just
 * translates esp_wifi events and executes the returned action. */
static WifiConnPolicy s_pol;

/* ESP-013: the compacted saved networks, copied at start so the caller's KsConfig
 * need not outlive us. s_pol.slot indexes this array. */
static WifiCred s_creds[KS_WIFI_SLOTS];
static int      s_ncreds;

/* Point the STA at slot `i` without restarting the WiFi stack. Called for the cold
 * start and again on every WCA_TRY_SLOT. */
static void apply_slot(int i)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     s_creds[i].ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, s_creds[i].pass, sizeof(wc.sta.password) - 1);
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_LOGI(TAG, "connecting to SSID:%s (slot %d/%d)", s_creds[i].ssid, i + 1, s_ncreds);
}

/* SoftAP config fallback — reached either from a first-boot empty ssid, or from
 * give_up_and_start_ap() once the policy's budget trips. Does not re-init the WiFi
 * stack or re-register events — wifi_link_start() did that once. */
static void start_ap(void)
{
    s_pol.state = WCS_AP;      // park the policy (covers first-boot AP + give-up)
    esp_netif_create_default_wifi_ap();

    wifi_config_t wc = {0};
    strcpy((char *)wc.ap.ssid, AP_SSID);
    wc.ap.ssid_len       = strlen(AP_SSID);
    wc.ap.max_connection = 4;
    wc.ap.authmode       = WIFI_AUTH_OPEN;   // open network for first-boot/fallback setup
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "SoftAP '%s' at 192.168.4.1 for config", AP_SSID);
}

static void give_up_and_start_ap(void)
{
    ESP_LOGW(TAG, "no connection after %llds — giving up on STA, falling back to AP",
             (long long)(WIFI_CONN_TIMEOUT_US / 1000000));
    esp_wifi_disconnect();
    esp_wifi_stop();
    start_ap();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    /* Once parked in the config AP the policy is terminal; ignore stray STA events so
     * a queued DISCONNECTED can't re-enter give_up_and_start_ap() and double-init the
     * AP netif (ESP_ERROR_CHECK would abort). */
    if (s_pol.state == WCS_AP) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *dc = (wifi_event_sta_disconnected_t *)data;
        WifiConnAction a = wifi_conn_policy_step(&s_pol, WCE_DISCONNECTED, esp_timer_get_time());
        if (a == WCA_GIVE_UP_TO_AP) {
            give_up_and_start_ap();
        } else {  // WCA_CONNECT (same slot) or WCA_TRY_SLOT (next saved network)
            ESP_LOGW(TAG, "disconnected (reason=%d), retrying", dc ? dc->reason : -1);
            /* ESP-013: this slot's share of the budget ran out — re-point the STA at
             * the next saved network before reconnecting. Safe while started; only
             * the config changes, the stack keeps running. */
            if (a == WCA_TRY_SLOT) apply_slot(s_pol.slot);
            /* Backoff before reconnecting: without it the C6 posts DISCONNECTED as
             * fast as it fails (reason 201 while a hotspot wakes), spinning the hosted
             * RPC. Runs in the event-loop task, not an ISR, so a short delay is safe. */
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        /* Name the network, not just the address. A device that silently joined a
         * different saved SSID than expected looks like a routing fault. */
        ESP_LOGI(TAG, "got ip " IPSTR " on SSID:%s (slot %d/%d)", IP2STR(&e->ip_info.ip),
                 s_pol.slot < s_ncreds ? s_creds[s_pol.slot].ssid : "?",
                 s_pol.slot + 1, s_ncreds);
        /* Multicast must not be dropped by modem sleep (the P4 equivalent of the
         * S3's WiFi.setSleep(false)). */
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (wifi_conn_policy_step(&s_pol, WCE_GOT_IP, esp_timer_get_time()) == WCA_SPAWN_LISTENER) {
            xTaskCreate(link_task, "link", 4096, NULL, 5, NULL);
        }
    }
}

static void start_sta(void)
{
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_slot(0);
    wifi_conn_policy_reset(&s_pol, esp_timer_get_time(), s_ncreds);
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_link_start(const WifiCred* creds, int n)
{
    if (n > KS_WIFI_SLOTS) n = KS_WIFI_SLOTS;
    s_ncreds = (n < 0) ? 0 : n;
    for (int i = 0; i < s_ncreds; i++) s_creds[i] = creds[i];

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Init the stack + register events once — start_sta() and the AP fallback
     * both rely on this being done regardless of which path runs. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    if (s_ncreds > 0) start_sta();
    else              start_ap();
}

bool wifi_link_ap_mode(void)               { return s_pol.state == WCS_AP; }
bool wifi_link_timeline(LinkTimeline* out) { return link_proto_timeline(out); }
int  wifi_link_peers(void)                 { return link_proto_peers(); }
