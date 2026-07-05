/*
 * P4Hub web UI (P4-007) — rack-panel status page styled like X32Link, served
 * over esp_http_server. Thin glue: the page is a static string; live data comes
 * from the pure p4hub_status.c via /status (polled 1 Hz). CSS/aesthetic lifted
 * from X32Link/web_config.cpp (dark rack unit, screws, green LED, DSEG 7-seg).
 *
 * v1 is status-only; config rows (WiFi, clock outputs) land next in P4-007/P4-010.
 */
#include "esp_http_server.h"
#include "esp_log.h"
#include "link_protocol.h"
#include "wifi_link.h"
#include "usb_midi_host.h"
#include "p4hub_status.h"

static const char *TAG = "p4hub_web";

// Rack-unit front panel, matched to X32Link. Fully static — all live values are
// filled by the /status poll below, so no server-side templating.
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
.rows{padding:6px 26px 22px}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:15px 0;border-top:1px solid var(--line)}
.row label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut)}
.val{font-family:var(--mono);font-size:14px;color:var(--ink);letter-spacing:.06em}
.val.ok{color:var(--led)}.val.off{color:#5e666e}
.pill{font-size:10.5px;letter-spacing:.16em;text-transform:uppercase;padding:4px 9px;border-radius:999px;border:1px solid var(--line);color:var(--mut)}
.pill.on{color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f}
.foot{text-align:center;color:#3c444c;font-size:10.5px;letter-spacing:.18em;margin:2px 0 20px;text-transform:uppercase}
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
<div class="foot">ESP32-P4 &middot; Ableton Link &rarr; USB-MIDI</div>
</div>
<script>
var bpmEl=document.getElementById('bpm'),beatEl=document.getElementById('beat');
var peersEl=document.getElementById('peers'),usbEl=document.getElementById('usb'),txEl=document.getElementById('tx');
var beatTimer=null,shownBpm=-1;
function setBeat(bpm){if(beatTimer){clearInterval(beatTimer);beatTimer=null}
if(bpm>0){beatTimer=setInterval(function(){beatEl.classList.add('on');setTimeout(function(){beatEl.classList.remove('on')},90)},60000/bpm)}}
function showBpm(bpm){if(Math.abs(bpm-shownBpm)<0.05)return;shownBpm=bpm;
bpmEl.textContent=bpm>0?bpm.toFixed(1):'--.-';setBeat(bpm)}
function poll(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
if(typeof d.bpm==='number')showBpm(d.bpm);
peersEl.textContent=d.peers;
usbEl.textContent=d.usb?'Connected':'Waiting';usbEl.className='pill'+(d.usb?' on':'');
txEl.textContent=(d.tx||0)+' pulses';
}).catch(function(){})}
poll();setInterval(poll,1000);
</script></body></html>)HTML";

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
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

extern "C" void p4hub_web_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET, .handler = root_handler,   .user_ctx = NULL };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    ESP_LOGI(TAG, "web UI on :80");
}
