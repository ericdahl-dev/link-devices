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
#ifdef HAS_TOUCH_DISPLAY
#include "ktouch_display.h"
#endif
#ifdef HAS_BUTTONS
#include "buttons.h"            // ESP-025: pure debounce (shared, ADR-0007)
#include "transport_led.h"      // ESP-025: lamp state for the two lit buttons
#include "ktouch_transport.h"   // ESP-025: the same mailbox the touch UI posts to
#endif
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


#ifdef HAS_BUTTONS
// ESP-025 bench rig (classic ESP32 DevKit + screw-terminal breakout). Two illuminated
// buttons are this board's transport surface, in place of the Touch's LCD. They post
// the SAME TransportLaunchIntent into the SAME ktouch_transport mailbox the touch UI
// uses, so the 1 ms writer is identical on both boards -- what the bench measures is
// what the product runs.
//
// GPIO32/33 are the cleanest inputs on this board: no strapping role, real internal
// pull-ups. Deliberately NOT 34/35/VP/VN (input-only, and they have NO internal
// pull-up, so INPUT_PULLUP compiles and silently does nothing), and not 0/2/5/12/15
// (strapping -- GPIO12 held high at boot sets the flash rail to 1.8 V and the board
// will not come up). Switch to GND + INPUT_PULLUP: pressed reads LOW.
//
// The lamps are load-bearing, not decoration: a press is QUANTIZED, so nothing happens
// for up to a bar. Without the blink that reads as a dead button and the user presses
// again. See transport_led.h.
#define BTN_TRANSPORT_PIN 32
#define BTN_REALIGN_PIN   33
#define LED_TRANSPORT_PIN 25
#define LED_REALIGN_PIN   26

static Button s_btn_transport;
static Button s_btn_realign;
// ESP-025 bring-up: latch what the pin is doing, so a press can be checked from /status
// without having to catch it inside a serial window.
static volatile uint32_t s_btn_presses = 0;   // debounced press edges, ever
static volatile uint32_t s_btn_lows    = 0;   // loop() iterations that saw the pin LOW
uint32_t ktouch_btn_presses(void) { return s_btn_presses; }
uint32_t ktouch_btn_lows(void)    { return s_btn_lows; }
int      ktouch_btn_level(void)   { return digitalRead(BTN_TRANSPORT_PIN); }

static void buttons_begin() {
    pinMode(BTN_TRANSPORT_PIN, INPUT_PULLUP);
    pinMode(BTN_REALIGN_PIN,   INPUT_PULLUP);
    pinMode(LED_TRANSPORT_PIN, OUTPUT);
    pinMode(LED_REALIGN_PIN,   OUTPUT);
    digitalWrite(LED_TRANSPORT_PIN, LOW);
    digitalWrite(LED_REALIGN_PIN,   LOW);
    button_reset(&s_btn_transport);
    button_reset(&s_btn_realign);
}

static const char* tl_name(int s) {
    return s == TL_RUNNING ? "RUNNING" : s == TL_ARMED ? "ARMED" : "STOPPED";
}

static void buttons_tick() {
    uint32_t now = millis();


    // Post intents; never touch the wire from loop(). A transport byte emitted here
    // would land wherever loop() happened to be, not on the bar line -- the writer
    // owns transport_launch and fires it on the grid.
    if (digitalRead(BTN_TRANSPORT_PIN) == LOW) s_btn_lows++;
    if (button_update(&s_btn_transport, digitalRead(BTN_TRANSPORT_PIN) == LOW, now)) {
        s_btn_presses++;
        bool running = ktouch_transport_state() != TL_STOPPED;
        ktouch_transport_post(running ? TL_INTENT_STOP : TL_INTENT_PLAY);
        Serial.printf("[btn] TRANSPORT (gpio%d) -> %s\n", BTN_TRANSPORT_PIN,
                      running ? "STOP (immediate)" : "PLAY (arms; fires on the bar line)");
    }
    if (button_update(&s_btn_realign, digitalRead(BTN_REALIGN_PIN) == LOW, now)) {
        ktouch_transport_post(TL_INTENT_REALIGN);
        Serial.printf("[btn] REALIGN (gpio%d) -> arms; 0xFC+0xFA on the bar line\n",
                      BTN_REALIGN_PIN);
    }

    // Serial IS the feedback surface until the lit buttons are wired. A quantized press
    // does nothing for up to a whole bar, so without SOMETHING saying "armed" it reads as
    // a dead button -- that is the entire reason transport_led exists. Log the edges only;
    // logging every loop() would drown the console (and this is loop(), never the 1 ms
    // writer, where an ESP_LOGx/Serial write is a blocking UART stall -- P4-033).
    static int  s_last_state   = -1;
    static bool s_last_realign = false;
    int  st = ktouch_transport_state();
    bool ra = ktouch_transport_realign_armed();
    if (st != s_last_state)     { Serial.printf("[transport] %s\n", tl_name(st)); s_last_state = st; }
    if (ra != s_last_realign)   { Serial.printf("[realign] %s\n", ra ? "ARMED" : "fired/cleared"); s_last_realign = ra; }

    digitalWrite(LED_TRANSPORT_PIN,
        transport_led_on((TransportLaunchState)ktouch_transport_state(), now) ? HIGH : LOW);
    digitalWrite(LED_REALIGN_PIN,
        realign_led_on(ktouch_transport_realign_armed(), now) ? HIGH : LOW);
}
#endif // HAS_BUTTONS

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n[KSTouch] KitchenSync Touch fw:%s -- booting\n", FW_VERSION);

    config_load(&g_config);   // NVS
    // First boot with no saved SSID: seed from the compile-time secrets so a bench
    // unit connects; a shipped unit is configured through the SoftAP + web form.
    if (g_config.wifi[0].ssid[0] == '\0' && sizeof(KSTOUCH_WIFI_SSID) > 1) {
        strlcpy(g_config.wifi[0].ssid, KSTOUCH_WIFI_SSID, sizeof(g_config.wifi[0].ssid));
        strlcpy(g_config.wifi[0].pass, KSTOUCH_WIFI_PASS, sizeof(g_config.wifi[0].pass));
    }

#ifdef HAS_TOUCH_DISPLAY
    ktouch_display_begin();
#endif
#ifdef HAS_BUTTONS
    buttons_begin();
#endif
    tempo_source_select(TEMPO_SRC_LINK);
    tempo_source_pre_net();

    if (g_config.wifi[0].ssid[0] == '\0') {
        start_ap();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);   // modem sleep drops buffered multicast -> Link never rx
        WiFi.begin(g_config.wifi[0].ssid, g_config.wifi[0].pass);
        Serial.printf("[KSTouch] joining %s", g_config.wifi[0].ssid);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(250); Serial.print("."); }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            uint8_t mac[6]; WiFi.macAddress(mac);
            snprintf(g_ks_host, sizeof(g_ks_host), "kstouch-%02x%02x", mac[4], mac[5]);
            if (MDNS.begin(g_ks_host)) {
                MDNS.addService("http", "tcp", 80);
                /* ESP-031: this unit advertises as kstouch-XXXX, which does not match the
                 * app's `kitchensync-*` hostname filter -- so a real Touch is invisible to
                 * it. Publish identity in TXT and let the client match on `dev`. `target`
                 * is the build's, so a client can refuse a cross-target OTA. */
                MDNS.addServiceTxt("http", "tcp", "dev",    "kitchensync");
                MDNS.addServiceTxt("http", "tcp", "model",  "touch");
                MDNS.addServiceTxt("http", "tcp", "target", CONFIG_IDF_TARGET);
                MDNS.addServiceTxt("http", "tcp", "fw",     FW_VERSION);
            }
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
#ifdef HAS_TOUCH_DISPLAY
    ktouch_display_tick();
#endif
#ifdef HAS_BUTTONS
    buttons_tick();
#endif
    delay(5);
}
