/*
 * P4Hub web UI (P4-007) — rack-panel status + config page styled like X32Link,
 * served over esp_http_server. Thin glue: live values from pure p4hub_status.c
 * (/status, polled 1 Hz); config model is pure p4hub_config.c; NVS + reboot are
 * the only side effects. CSS/aesthetic lifted from X32Link/web_config.cpp.
 */
#include <string>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "link_protocol.h"
#include "wifi_link.h"
#include "usb_midi_host.h"
#include "p4hub_status.h"
#include "p4hub_config.h"
#include "p4hub_config_nvs.h"
#include "p4hub_web.h"

static const char *TAG = "p4hub_web";
static P4HubConfig *s_cfg = nullptr;

// %SSID% / %MCKCHK% / %CABLE% are filled per-request from the live config.
static const char PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>P4&middot;HUB</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/dseg@0.46.0/css/dseg.css" rel="stylesheet">
<style>
:root{--bg:#070809;--panel-2:#0f1216;--ink:#e9ece6;--mut:#717a82;--line:#262b31;
--led:#b6ff36;--led-dim:#36431a;--amber:#ff9d3b;
--mono:'DM Mono',ui-monospace,Menlo,monospace;--disp:'Bricolage Grotesque','Arial Narrow',sans-serif;--seg:'DSEG7 Classic','DM Mono',monospace;}
*{box-sizing:border-box}html,body{margin:0}
body{min-height:100vh;background:var(--bg);color:var(--ink);font-family:var(--mono);font-size:14px;
display:flex;align-items:flex-start;justify-content:center;padding:34px 16px 80px;
background-image:radial-gradient(900px 500px at 50% -10%,#14181d 0%,transparent 60%),radial-gradient(700px 500px at 90% 110%,#0e1318 0%,transparent 55%);}
.unit{width:100%;max-width:430px;border:1px solid var(--line);border-radius:16px;position:relative;overflow:hidden;
background:repeating-linear-gradient(180deg,rgba(255,255,255,.012) 0 2px,transparent 2px 4px),linear-gradient(180deg,#191d22,#101317);
box-shadow:0 1px 0 rgba(255,255,255,.04),0 22px 60px -20px #000;}
.unit::before{content:"";position:absolute;inset:0 0 auto 0;height:120px;background:linear-gradient(180deg,rgba(255,255,255,.05),transparent);pointer-events:none}
.screw{position:absolute;width:11px;height:11px;border-radius:50%;background:radial-gradient(circle at 35% 30%,#3a4047,#0d0f12 70%);box-shadow:inset 0 1px 1px rgba(255,255,255,.18)}
.screw::after{content:"";position:absolute;inset:3px;border-top:1px solid #05070a;transform:rotate(28deg)}
.tl{top:12px;left:12px}.tr{top:12px;right:12px}.bl{bottom:12px;left:12px}.br{bottom:12px;right:12px}
.brand{display:flex;align-items:center;gap:11px;padding:18px 26px 14px;border-bottom:1px solid var(--line)}
.pwr{width:9px;height:9px;border-radius:50%;background:var(--led);box-shadow:0 0 10px 1px var(--led);animation:breathe 3.4s ease-in-out infinite}
.wordmark{font-family:var(--disp);font-weight:800;font-size:20px;letter-spacing:.06em;text-transform:uppercase}
.wordmark b{color:var(--led)}
.rev{margin-left:auto;font-size:10.5px;color:var(--mut);letter-spacing:.18em;text-transform:uppercase}
.scr{margin:18px;padding:18px 20px 16px;border-radius:11px;position:relative;border:1px solid #1f261b;
background:radial-gradient(120% 120% at 50% -20%,rgba(182,255,54,.08),transparent 60%),linear-gradient(180deg,#0a0d0a,#070907);
box-shadow:inset 0 0 32px rgba(0,0,0,.9)}
.scr-top{display:flex;align-items:center;gap:8px;margin-bottom:6px}
.beat{width:8px;height:8px;border-radius:2px;background:var(--led-dim)}
.beat.on{background:var(--led);box-shadow:0 0 9px 1px var(--led)}
.scr-lbl{font-size:10.5px;letter-spacing:.22em;color:#6f8a4d;text-transform:uppercase}
.scr-src{margin-left:auto;font-size:10.5px;letter-spacing:.2em;color:var(--amber);text-transform:uppercase}
.readout{position:relative;font-family:var(--seg);line-height:1;padding:4px 0 2px}
.readout .ghost{position:absolute;inset:4px 0 2px;color:#1a2113}
.readout .live{position:relative;color:var(--led);text-shadow:0 0 14px rgba(182,255,54,.45)}
.bignum{font-size:58px}
.unit-bpm{font-family:var(--mono);font-size:13px;color:#6f8a4d;letter-spacing:.2em;margin-left:6px}
.rows{padding:6px 26px 4px}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:15px 0;border-top:1px solid var(--line)}
.row label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut)}
.val{font-family:var(--mono);font-size:14px;color:var(--ink);letter-spacing:.06em}
.pill{font-size:10.5px;letter-spacing:.16em;text-transform:uppercase;padding:4px 9px;border-radius:999px;border:1px solid var(--line);color:var(--mut)}
.pill.on{color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f}
form{padding:2px 26px 26px}
.frow{padding:14px 0;border-top:1px solid var(--line)}
.cap{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut);margin-bottom:9px;display:block}
.fld{display:flex;align-items:center;background:var(--panel-2);border:1px solid var(--line);border-radius:9px;padding:0 12px;margin-top:6px}
.fld:focus-within{border-color:#4a5a2c;box-shadow:0 0 0 3px rgba(182,255,54,.08)}
.fld .pre{color:#4b535b;font-size:12px;letter-spacing:.1em;padding-right:9px;border-right:1px solid var(--line);margin-right:11px}
.fld input,.fld select{flex:1;appearance:none;background:transparent;border:0;outline:0;color:var(--ink);font-family:var(--mono);font-size:14.5px;padding:13px 0}
.fld option{background:#0f1216}
.sw{display:flex;align-items:center;gap:13px;cursor:pointer;user-select:none}
.sw input{position:absolute;opacity:0;width:0;height:0}
.sw .track{position:relative;flex:none;width:52px;height:28px;border-radius:999px;background:var(--panel-2);border:1px solid var(--line);transition:.2s}
.sw .knob{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#5b636b;transition:.2s}
.sw input:checked + .track{background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f}
.sw input:checked + .track .knob{left:28px;background:#0a0d07}
.sw .swlbl{font-family:var(--mono);font-size:12.5px;letter-spacing:.14em;text-transform:uppercase;color:var(--mut)}
.sw .swlbl::after{content:" Off"}
.sw input:checked ~ .swlbl{color:var(--led)}
.sw input:checked ~ .swlbl::after{content:" On"}
.write{width:100%;margin-top:22px;border:0;cursor:pointer;border-radius:11px;font-family:var(--disp);font-weight:800;font-size:16px;letter-spacing:.14em;text-transform:uppercase;color:#0a0d07;padding:17px;
background:linear-gradient(180deg,#d2ff63,#9be32a);box-shadow:0 6px 0 #5e8a16,0 16px 30px -12px rgba(182,255,54,.5)}
.write:active{transform:translateY(4px);box-shadow:0 1px 0 #5e8a16}
.foot{text-align:center;color:#3c444c;font-size:10.5px;letter-spacing:.18em;margin:8px 0 20px;text-transform:uppercase}
@keyframes breathe{0%,100%{opacity:1}50%{opacity:.45}}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style></head><body>
<div class="unit">
<span class="screw tl"></span><span class="screw tr"></span><span class="screw bl"></span><span class="screw br"></span>
<div class="brand"><span class="pwr"></span><span class="wordmark">P4&middot;<b>HUB</b></span><span class="rev">ESP32-P4</span></div>
<div class="scr">
<div class="scr-top"><span class="beat" id="beat"></span><span class="scr-lbl">Session Tempo</span><span class="scr-src">Ableton Link</span></div>
<div class="readout"><span class="ghost bignum">188.8</span><span class="live"><span class="bignum" id="bpm">--.-</span><span class="unit-bpm">BPM</span></span></div>
</div>
<div class="rows">
<div class="row"><label>Link Peers</label><span class="val" id="peers">0</span></div>
<div class="row"><label>USB-MIDI Device</label><span class="pill" id="usb">Waiting</span></div>
<div class="row"><label>Clock Out</label><span class="val" id="tx">0 pulses</span></div>
</div>
<form method="POST" action="/save">
<div class="frow"><span class="cap">WiFi Network</span>
<div class="fld"><span class="pre">SSID</span><input name="wifi_ssid" value="%SSID%" autocomplete="off"></div>
<div class="fld"><span class="pre">PASS</span><input name="wifi_pass" type="password" placeholder="keep current"></div></div>
<div class="frow"><span class="cap">MIDI Clock Out</span>
<label class="sw"><input type="checkbox" name="clock_out" value="1" %MCKCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="frow"><span class="cap">Metronome (Speaker)</span>
<label class="sw"><input type="checkbox" name="metronome" value="1" %MTOCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="frow"><span class="cap">Accent Bar 1</span>
<label class="sw"><input type="checkbox" name="metro_accent" value="1" %MTACHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="frow"><span class="cap">Metronome Sound</span>
<div class="fld"><span class="pre">VOL</span><input type="number" name="metro_vol" value="%MVOL%" min="0" max="100" step="5"></div>
<div class="fld"><span class="pre">VOICE</span><select name="metro_voice" id="mvoice"><option value="0">Tone</option><option value="1">Click</option><option value="2">Wood</option></select></div></div>
%OUTPUTS%
<button class="write" type="submit">Write &amp; Reboot</button>
</form>
<div class="foot">ESP32-P4 &middot; Ableton Link &rarr; USB-MIDI</div>
</div>
<script>
var bpmEl=document.getElementById('bpm'),beatEl=document.getElementById('beat');
var peersEl=document.getElementById('peers'),usbEl=document.getElementById('usb'),txEl=document.getElementById('tx');
var beatTimer=null,shownBpm=-1;
function setBeat(bpm){if(beatTimer){clearInterval(beatTimer);beatTimer=null}
if(bpm>0){beatTimer=setInterval(function(){beatEl.classList.add('on');setTimeout(function(){beatEl.classList.remove('on')},90)},60000/bpm)}}
function showBpm(bpm){if(Math.abs(bpm-shownBpm)<0.05)return;shownBpm=bpm;bpmEl.textContent=bpm>0?bpm.toFixed(1):'--.-';setBeat(bpm)}
function poll(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
if(typeof d.bpm==='number')showBpm(d.bpm);peersEl.textContent=d.peers;
usbEl.textContent=d.usb?'Connected':'Waiting';usbEl.className='pill'+(d.usb?' on':'');
txEl.textContent=(d.tx||0)+' pulses';}).catch(function(){})}
poll();setInterval(poll,1000);
</script></body></html>)HTML";

// Replace every "%KEY%" in s with val.
static void subst(std::string &s, const char *key, const std::string &val)
{
    size_t p;
    while ((p = s.find(key)) != std::string::npos) s.replace(p, strlen(key), val);
}

// Build the 4 per-output clock config rows (enable / cable / rate / phase nudge),
// with the live config marked checked/selected (P4-010).
static std::string build_outputs()
{
    if (!s_cfg) return "";
    static const char* PORTS[4]   = { "USB A", "USB B", "USB C", "USB D" };
    static const int   PPQN[]     = { 24, 48, 12, 6, 4, 2, 1 };
    static const char* PPQN_LBL[] = { "MIDI clock (24)", "&times;2 (48)", "&divide;2 (12)",
                                      "&divide;4 (6)", "1/16 (4)", "1/8 (2)", "1/4 (1)" };
    std::string s;
    for (int o = 0; o < P4HUB_CLOCK_OUTPUTS; o++) {
        const ClockOutputCfg* c = &s_cfg->clock[o];
        std::string N = std::to_string(o);
        s += "<div class=\"frow\"><span class=\"cap\">Clock Out " + std::to_string(o + 1) + "</span>";
        s += "<label class=\"sw\"><input type=\"checkbox\" name=\"clk" + N + "_en\" value=\"1\""
             + (c->enable ? " checked" : "")
             + "><span class=\"track\"><span class=\"knob\"></span></span><span class=\"swlbl\"></span></label>";
        s += "<div class=\"fld\"><span class=\"pre\">CABLE</span><select name=\"clk" + N + "_cable\">";
        for (int p = 0; p < 4; p++)
            s += "<option value=\"" + std::to_string(p) + "\"" + (c->cable == p ? " selected" : "")
                 + ">" + PORTS[p] + "</option>";
        s += "</select></div>";
        s += "<div class=\"fld\"><span class=\"pre\">RATE</span><select name=\"clk" + N + "_ppqn\">";
        for (size_t k = 0; k < sizeof(PPQN) / sizeof(PPQN[0]); k++)
            s += "<option value=\"" + std::to_string(PPQN[k]) + "\"" + (c->ppqn == PPQN[k] ? " selected" : "")
                 + ">" + PPQN_LBL[k] + "</option>";
        s += "</select></div>";
        s += "<div class=\"fld\"><span class=\"pre\">NUDGE</span><input type=\"number\" name=\"clk" + N
             + "_phase\" value=\"" + std::to_string(c->phase_mbeats) + "\" min=\"-250\" max=\"250\" step=\"5\"></div>";
        s += "</div>";
    }
    return s;
}

static std::string build_page()
{
    std::string h(PAGE);
    subst(h, "%SSID%",    s_cfg ? std::string(s_cfg->wifi_ssid) : "");
    subst(h, "%MCKCHK%",  (s_cfg && s_cfg->clock_out_enable) ? "checked" : "");
    subst(h, "%MTOCHK%",  (s_cfg && s_cfg->metronome_enable) ? "checked" : "");
    subst(h, "%MTACHK%",  (s_cfg && s_cfg->metronome_accent) ? "checked" : "");
    subst(h, "%MVOL%",    std::to_string(s_cfg ? s_cfg->metronome_volume : 80));
    subst(h, "%MVOICE%",  std::to_string(s_cfg ? s_cfg->metronome_voice : 0));
    subst(h, "%OUTPUTS%", build_outputs());
    return h;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    std::string page = build_page();
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page.c_str(), page.size());
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char buf[96];
    p4hub_status_json(buf, sizeof(buf),
                      (float)link_proto_bpm(), wifi_link_peers(),
                      usb_midi_host_ready(), usb_midi_host_tx());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// Minimal result page in the panel palette.
static esp_err_t send_result(httpd_req_t *req, const char *title, const char *msg)
{
    std::string p =
        "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<body style='margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:#070809;color:#e9ece6;font-family:ui-monospace,Menlo,monospace;text-align:center'>"
        "<div style='padding:2rem'><div style='width:10px;height:10px;border-radius:50%;margin:0 auto 1.2rem;"
        "background:#b6ff36;box-shadow:0 0 14px 2px #b6ff36'></div><h2 style='font-weight:600'>";
    p += title; p += "</h2><p style='color:#717a82'>"; p += msg; p += "</p></div></body>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, p.c_str(), p.size());
}

// In-place URL-decode (%XX and '+') of a form field value.
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *o++ = ' '; }
        else if (*p == '%' && p[1] && p[2]) {
            auto hex = [](char c){ return c<='9'?c-'0':(c|0x20)-'a'+10; };
            *o++ = (char)(hex(p[1]) * 16 + hex(p[2])); p += 2;
        } else *o++ = *p;
    }
    *o = '\0';
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[1024];   /* wifi + 4 outputs x 4 fields fits comfortably */
    int len = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = '\0';

    P4HubConfig cfg = *s_cfg;
    // Walk "key=value&key=value" pairs, URL-decoding each side. Track whether the
    // clock_out checkbox was present: an unchecked box is simply absent from the
    // POST body, so its absence means "off". (Can't strstr(body) after this loop —
    // strtok_r has already split body at the '&' separators.)
    bool saw_clock_out = false, saw_metronome = false, saw_metro_accent = false;
    bool saw_out_en[P4HUB_CLOCK_OUTPUTS] = { false };
    char *save = nullptr;
    for (char *pair = strtok_r(body, "&", &save); pair; pair = strtok_r(nullptr, "&", &save)) {
        char *eq = strchr(pair, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = pair, *val = eq + 1;
        url_decode(key); url_decode(val);
        if (strcmp(key, "clock_out") == 0)    saw_clock_out = true;
        if (strcmp(key, "metronome") == 0)    saw_metronome = true;
        if (strcmp(key, "metro_accent") == 0) saw_metro_accent = true;
        if (strncmp(key, "clk", 3) == 0 && key[3] >= '0' && key[3] <= '9' && strcmp(key + 4, "_en") == 0) {
            int idx = key[3] - '0';
            if (idx >= 0 && idx < P4HUB_CLOCK_OUTPUTS) saw_out_en[idx] = true;
        }
        p4hub_config_set(&cfg, key, val);
    }
    // An unchecked checkbox is simply absent from the POST body -> "off".
    if (!saw_clock_out)    cfg.clock_out_enable = 0;
    if (!saw_metronome)    cfg.metronome_enable = 0;
    if (!saw_metro_accent) cfg.metronome_accent = 0;
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++)
        if (!saw_out_en[i]) cfg.clock[i].enable = 0;

    if (!p4hub_config_valid(&cfg)) return send_result(req, "Invalid Config", "Check the values and go back.");
    *s_cfg = cfg;
    p4hub_config_save(s_cfg);
    esp_err_t rc = send_result(req, "Saved — Restarting", "Reconnect to WiFi if credentials changed.");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return rc;
}

void p4hub_web_start(P4HubConfig* cfg)
{
    s_cfg = cfg;
    httpd_handle_t server = NULL;
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &hcfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET,  .handler = root_handler,   .user_ctx = NULL };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET,  .handler = status_handler, .user_ctx = NULL };
    httpd_uri_t save   = { .uri = "/save",   .method = HTTP_POST, .handler = save_handler,   .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &save);
    ESP_LOGI(TAG, "web UI on :80");
}
