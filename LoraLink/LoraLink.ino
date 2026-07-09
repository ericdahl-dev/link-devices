// LoraLink — relays the Ableton Link session BPM over LoRa for out-of-WiFi-range
// FX tempo control. No phase alignment — see docs/plans/2026-07-09-loralink-design.md.
#include <WiFi.h>
#include <Arduino.h>
#include <stdlib.h>  // abs()

#include "lora_config.h"
#include "lora_secrets.h"   // WIFI_SSID / WIFI_PASSWORD — see lora_secrets.h.example
#include "lora_radio.h"
#include "lora_display.h"
#include "lora_bpm_packet.h"
#include "lora_freshness.h"

// Reused unchanged from ../X32Link (ADR-0003: pure-C-logic + thin-glue split).
// link_listener.{h,cpp} below are symlinks to ../X32Link/link_listener.{h,cpp}
// — arduino-cli only compiles sources physically inside the sketch folder,
// so a relative "../X32Link/link_listener.h" include alone would leave the
// .cpp unbuilt (link error). The symlinks keep the source as the single
// unchanged file, just visible from both sketch folders.
#include "link_listener.h"

#if LORA_ROLE == LORA_ROLE_SENDER

static uint8_t s_seq = 0;

static void wifi_connect_blocking() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[LoraLink] connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\n[LoraLink] WiFi up, ip=%s\n", WiFi.localIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    lora_display_begin();
    lora_radio_begin();
    wifi_connect_blocking();
    link_listener_begin();
}

void loop() {
    link_listener_poll();
    link_listener_tick();

    int   peers  = link_listener_peers();
    bool  active = peers > 0;
    float bpm    = active ? (float)link_listener_bpm() : 0.0f;

    static uint32_t s_last_send_ms = 0;
    static uint16_t s_last_sent_bpm_x100 = 0xFFFF;  // force an initial send
    uint32_t now = millis();
    uint16_t bpm_x100 = (uint16_t)(bpm * 100.0f + 0.5f);

    bool changed = active &&
        (abs((int)bpm_x100 - (int)s_last_sent_bpm_x100) >= LORA_BPM_CHANGE_EPSILON_X100);
    bool heartbeat_due = (uint32_t)(now - s_last_send_ms) >= LORA_HEARTBEAT_MS;

    if (changed || heartbeat_due) {
        lora_bpm_packet_t pkt;
        pkt.msg_type = active ? LORA_MSG_BPM : LORA_MSG_NO_SESSION;
        pkt.seq      = s_seq++;
        pkt.bpm_x100 = active ? bpm_x100 : 0;

        uint8_t buf[LORA_BPM_PACKET_SIZE];
        lora_bpm_packet_encode(&pkt, buf);
        lora_radio_send(buf, sizeof(buf));

        s_last_send_ms = now;
        s_last_sent_bpm_x100 = bpm_x100;
    }

    lora_display_show_sender(peers, bpm, active);
    delay(LORA_LINK_POLL_MS);
}

#else  // LORA_ROLE_RECEIVER

static uint32_t s_last_seen_ms = 0;
static bool     s_has_received = false;
static float    s_last_bpm     = 0.0f;

void setup() {
    Serial.begin(115200);
    lora_display_begin();
    lora_radio_begin();
}

void loop() {
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    int len = 0;
    if (lora_radio_try_receive(buf, sizeof(buf), &len)) {
        lora_bpm_packet_t pkt;
        if (lora_bpm_packet_decode(buf, len, &pkt) && pkt.msg_type == LORA_MSG_BPM) {
            s_last_bpm     = pkt.bpm_x100 / 100.0f;
            s_last_seen_ms = millis();
            s_has_received = true;
        }
    }

    bool stale = lora_freshness_is_stale(millis(), s_last_seen_ms,
                                          LORA_STALE_THRESHOLD_MS, s_has_received);
    lora_display_show_receiver(s_last_bpm, stale);
    delay(50);
}

#endif
