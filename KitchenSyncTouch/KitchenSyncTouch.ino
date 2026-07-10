// KitchenSync Touch (ESP-016) — ESP32-S3-Touch-LCD-1.47. Grabs Ableton Link and
// drives a DIN synth (Dan's RC-505) with MIDI clock + transport, plus a touch UI.
// NO OSC, NO X32 mixer. Design:
// docs/plans/2026-07-10-kitchensynctouch-sketch-design.md.
//
// Increment 1b: WiFi + Link + 24-PPQN MIDI clock on DIN (GPIO11) + USB. Reuses the
// clock task via the Link-only tempo seam (ktouch_tempo). Display (1c), transport
// (Inc2), web/captive-portal (Inc3) layer on next.
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define KSTOUCH_WIFI_SSID "changeme"
#define KSTOUCH_WIFI_PASS ""
#endif
#include "fw_version.h"
#include "app_config.h"
#include "tempo_source.h"
#include "ktouch_display.h"   // status screen (no-op on screenless builds)

AppConfig g_config;   // the one config instance; ktouch_tempo reads it
char      g_ks_host[32] = "kstouch";   // mDNS name; per-unit suffix set at boot

static uint32_t s_last_log = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n[KSTouch] KitchenSync Touch fw:%s -- booting\n", FW_VERSION);

    config_defaults(&g_config);
    ktouch_display_begin();   // splash on the LCD before WiFi

    // USB MIDI must enumerate before WiFi (host sees the port); ktouch_tempo does
    // it iff the clock is enabled.
    tempo_source_select(TEMPO_SRC_LINK);
    tempo_source_pre_net();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // modem sleep drops buffered multicast -> Link never rx
    WiFi.begin(KSTOUCH_WIFI_SSID, KSTOUCH_WIFI_PASS);
    Serial.printf("[KSTouch] joining %s", KSTOUCH_WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(250); Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        // Per-unit mDNS name so two units don't collide and you reach it by name,
        // not a DHCP address. S3's WiFi MAC is on-die (esp_read_mac works here).
        uint8_t mac[6]; WiFi.macAddress(mac);
        snprintf(g_ks_host, sizeof(g_ks_host), "kstouch-%02x%02x", mac[4], mac[5]);
        if (MDNS.begin(g_ks_host)) MDNS.addService("http", "tcp", 80);
        Serial.printf("[KSTouch] ip %s  http://%s.local\n",
                      WiFi.localIP().toString().c_str(), g_ks_host);
        tempo_source_begin();   // Link multicast + measurement + the DIN clock task
    } else {
        Serial.println("[KSTouch] WiFi failed (Inc1 has no captive portal yet)");
    }
}

void loop() {
    tempo_source_poll();
    ktouch_display_tick();
    uint32_t now = millis();
    if (now - s_last_log >= 1000) {
        s_last_log = now;
        Serial.printf("[KSTouch] bpm:%.2f active:%d phase_valid:%d beats:%.2f\n",
                      tempo_source_bpm(), (int)tempo_source_active(),
                      (int)tempo_source_phase_valid(), tempo_source_beats_now());
    }
    delay(5);
}
