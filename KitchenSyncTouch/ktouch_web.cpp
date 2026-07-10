// KitchenSync Touch web config server — see ktouch_web.h (ESP-016, Inc3).
#include "ktouch_web.h"
#include "app_config.h"
#include "ui_chrome.h"
#include "fw_version.h"
#include <WebServer.h>
#include <WiFi.h>

extern AppConfig g_config;

static WebServer server(80);

// The config form body (between the shared <style> and <script>). Panel classes
// come from ui_chrome_css(). %TOKENS% are filled per request.
static const char FORM[] = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KitchenSync Touch</title><style>%CSS%
.unit{padding:0 0 20px}.brand{padding:18px 22px 12px}
.grp{padding:2px 22px}.frow{padding:12px 0;border-top:1px solid var(--line)}
.cap{font-family:var(--disp);font-weight:600;font-size:12.5px;letter-spacing:.12em;color:var(--ink);margin-bottom:10px;display:block}
.fld{margin-top:8px}.sw{margin-top:12px}
</style></head><body>
<div class="unit">
<div class="brand"><span class="pwr"></span><span class="wordmark">KITCHEN&middot;<b>SYNC</b> TOUCH</span></div>
<form method="POST" action="/save">
<div class="grp"><div class="frow head"><span class="cap">WiFi Network</span>
<div class="fld"><span class="pre">SSID</span><input name="wifi_ssid" value="%SSID%" autocomplete="off"></div>
<div class="fld"><span class="pre">PASS</span><input name="wifi_pass" type="password" placeholder="keep current"></div></div></div>
<div class="grp"><div class="frow head"><span class="cap">Transport</span>
<div class="fld"><span class="pre">BAR</span><input name="quantum" type="number" min="1" max="16" value="%Q%" inputmode="numeric"></div>
<label class="sw"><input type="checkbox" name="clock" value="1" %CLK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="cap" style="margin:2px 0 0;color:var(--mut)">MIDI clock out</div>
<label class="sw"><input type="checkbox" name="transport" value="1" %TP%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="cap" style="margin:2px 0 0;color:var(--mut)">transport enable</div>
<label class="sw"><input type="checkbox" name="play_rel" value="1" %REL%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="cap" style="margin:2px 0 0;color:var(--mut)">play on release (turntable feel)</div>
</div></div>
<div style="padding:0 22px"><button class="write" type="submit">Save &amp; Reboot</button></div>
</form>
<div class="foot">KitchenSync Touch &middot; FW %FWVER%</div>
</div></body></html>)HTML";

static void handle_root() {
    String h(FORM);
    h.replace("%CSS%", ui_chrome_css());
    h.replace("%SSID%", g_config.wifi_ssid);
    h.replace("%Q%",    String(g_config.quantum_beats));
    h.replace("%CLK%",  g_config.clock_enable ? "checked" : "");
    h.replace("%TP%",   g_config.transport_enable ? "checked" : "");
    h.replace("%REL%",  g_config.play_on_release ? "checked" : "");
    h.replace("%FWVER%", FW_VERSION);
    server.send(200, "text/html", h);
}

static void handle_save() {
    AppConfig c = g_config;
    if (server.hasArg("wifi_ssid")) strlcpy(c.wifi_ssid, server.arg("wifi_ssid").c_str(), sizeof(c.wifi_ssid));
    // blank password = keep current
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length())
        strlcpy(c.wifi_pass, server.arg("wifi_pass").c_str(), sizeof(c.wifi_pass));
    // Checkboxes: absent = off. Numbers via the validating setter.
    app_config_set(&c, ACF_QUANTUM_BEATS,    server.arg("quantum").toInt());
    app_config_set(&c, ACF_CLOCK_ENABLE,     server.hasArg("clock")     ? 1 : 0);
    app_config_set(&c, ACF_TRANSPORT_ENABLE, server.hasArg("transport") ? 1 : 0);
    app_config_set(&c, ACF_PLAY_ON_RELEASE,  server.hasArg("play_rel")  ? 1 : 0);

    if (!config_validate(&c)) {
        char p[1024]; ui_result_page(p, sizeof(p), "Invalid Config", "Check the values and go back.", false);
        server.send(200, "text/html", p); return;
    }
    g_config = c;
    config_save(&g_config);
    char p[1024]; ui_result_page(p, sizeof(p), "Saved &mdash; Restarting", "Reconnect if WiFi changed.", true);
    server.send(200, "text/html", p);
    delay(800);
    ESP.restart();
}

void ktouch_web_begin(void) {
    server.on("/",     handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.begin();
}

void ktouch_web_tick(void) { server.handleClient(); }
