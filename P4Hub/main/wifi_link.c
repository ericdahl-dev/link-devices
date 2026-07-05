/*
 * P4Hub glue: WiFi station (C6 hosted stack) + Ableton Link multicast listener.
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

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        /* Multicast must not be dropped by modem sleep (the P4 equivalent of the
         * S3's WiFi.setSleep(false)). */
        esp_wifi_set_ps(WIFI_PS_NONE);
        xTaskCreate(link_task, "link", 4096, NULL, 5, NULL);
    }
}

void wifi_link_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     CONFIG_P4HUB_WIFI_SSID,     sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, CONFIG_P4HUB_WIFI_PASSWORD, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to SSID:%s", CONFIG_P4HUB_WIFI_SSID);
}

bool wifi_link_timeline(LinkTimeline* out) { return link_proto_timeline(out); }
int  wifi_link_peers(void)                 { return link_proto_peers(); }
