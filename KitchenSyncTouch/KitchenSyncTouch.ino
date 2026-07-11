// KitchenSync Touch (ESP-016) — ESP32-S3-Touch-LCD-1.47. Grabs Ableton Link and
// drives a DIN synth (Dan's RC-505) with MIDI clock + transport, plus a full-screen
// touch transport toggle. NO OSC, NO X32 mixer. All config is on the web UI (touch
// is transport-only). Design: docs/plans/2026-07-10-kitchensynctouch-sketch-design.md.
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"          // first-boot WiFi seed if NVS is empty (gitignored)
#else
#define KSTOUCH_WIFI_SSID ""
#define KSTOUCH_WIFI_PASS ""
#endif
#include "fw_version.h"
#include "app_config.h"
#include "tempo_source.h"
#include "ktouch_display.h"
#include "ktouch_web.h"

AppConfig g_config;                       // the one config instance
char      g_ks_host[32] = "kstouch";      // display name: mDNS name or AP address
bool      g_ap_mode = false;              // true when serving the setup SoftAP

static void start_ap(void) {
    g_ap_mode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("KSTouch-Config");
    strlcpy(g_ks_host, "192.168.4.1", sizeof(g_ks_host));   // display shows this
    Serial.println("[KSTouch] SoftAP 'KSTouch-Config' @ 192.168.4.1 for setup");
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n[KSTouch] KitchenSync Touch fw:%s -- booting\n", FW_VERSION);

    config_load(&g_config);   // NVS
    // First boot with no saved SSID: seed from the compile-time secrets so a bench
    // unit connects; a shipped unit is configured through the SoftAP + web form.
    if (g_config.wifi_ssid[0] == '\0' && sizeof(KSTOUCH_WIFI_SSID) > 1) {
        strlcpy(g_config.wifi_ssid, KSTOUCH_WIFI_SSID, sizeof(g_config.wifi_ssid));
        strlcpy(g_config.wifi_pass, KSTOUCH_WIFI_PASS, sizeof(g_config.wifi_pass));
    }

    ktouch_display_begin();
    tempo_source_select(TEMPO_SRC_LINK);
    tempo_source_pre_net();

    if (g_config.wifi_ssid[0] == '\0') {
        start_ap();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);   // modem sleep drops buffered multicast -> Link never rx
        WiFi.begin(g_config.wifi_ssid, g_config.wifi_pass);
        Serial.printf("[KSTouch] joining %s", g_config.wifi_ssid);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(250); Serial.print("."); }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            uint8_t mac[6]; WiFi.macAddress(mac);
            snprintf(g_ks_host, sizeof(g_ks_host), "kstouch-%02x%02x", mac[4], mac[5]);
            if (MDNS.begin(g_ks_host)) MDNS.addService("http", "tcp", 80);
            Serial.printf("[KSTouch] ip %s  http://%s.local\n",
                          WiFi.localIP().toString().c_str(), g_ks_host);
            tempo_source_begin();   // Link + the DIN clock task
        } else {
            Serial.println("[KSTouch] WiFi failed -> setup AP");
            start_ap();
        }
    }

    ktouch_web_begin();   // config form (on the STA IP or the AP)
}

void loop() {
    ktouch_web_tick();
    if (!g_ap_mode) tempo_source_poll();
    ktouch_display_tick();
    delay(5);
}
