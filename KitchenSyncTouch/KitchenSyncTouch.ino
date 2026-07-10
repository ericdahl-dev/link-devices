// KitchenSync Touch (ESP-016) — ESP32-S3-Touch-LCD-1.47. Grabs Ableton Link and
// drives a DIN synth (Dan's RC-505) with MIDI clock + transport, plus a touch UI.
// NO OSC, NO X32 mixer. Design:
// docs/plans/2026-07-10-kitchensynctouch-sketch-design.md.
//
// Increment 1b: WiFi + Link + 24-PPQN MIDI clock on DIN (GPIO11) + USB. Reuses the
// clock task via the Link-only tempo seam (ktouch_tempo). Display (1c), transport
// (Inc2), web/captive-portal (Inc3) layer on next.
#include <WiFi.h>
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

AppConfig g_config;   // the one config instance; ktouch_tempo reads it

static uint32_t s_last_log = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n[KSTouch] KitchenSync Touch fw:%s -- booting\n", FW_VERSION);

    config_defaults(&g_config);

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
        Serial.printf("[KSTouch] ip %s\n", WiFi.localIP().toString().c_str());
        tempo_source_begin();   // Link multicast + measurement + the DIN/USB clock task
    } else {
        Serial.println("[KSTouch] WiFi failed (Inc1 has no captive portal yet)");
    }
}

void loop() {
    tempo_source_poll();
    uint32_t now = millis();
    if (now - s_last_log >= 1000) {
        s_last_log = now;
        Serial.printf("[KSTouch] bpm:%.2f active:%d phase_valid:%d beats:%.2f\n",
                      tempo_source_bpm(), (int)tempo_source_active(),
                      (int)tempo_source_phase_valid(), tempo_source_beats_now());
    }
    delay(5);
}
