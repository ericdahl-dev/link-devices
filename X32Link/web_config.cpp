#include "web_config.h"
#include "ui_chrome.h"   // ARC-017: shared CSS/JS + result & update pages
#include "app_config.h"
#include "fw_version.h"
#include "web_status_json.h"
#include "tempo_snapshot.h"
#include "config_persist.h"   // ARC-022: debounced write-through for /live edits
#ifdef HAS_BATTERY_GAUGE
#include "battery_snapshot.h"
#endif
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>

extern AppConfig g_config;
extern void config_save(const AppConfig*);

// ARC-022: /live marks this dirty; web_config_handle() writes the blob once the edits
// settle. Marked rather than written because config is ONE nvs blob -- a colour picker
// dragged across the wheel emits a POST per frame.
static ConfigPersist s_persist;
// Live tempo comes from the tempo_snapshot seam (ARC-001): one atomic read
// yields a torn-free {bpm,phase,valid,quantum}, so this file needs no
// tempo_source calls (the reason the old shared globals existed).

// ARC-017 page buffers. Static, not stack: WebServer dispatches one request at a
// time from loopTask, and a multi-KB local is how the P4's httpd task earned a
// stack-protection panic (ESP-013).
#define RESULT_PAGE_MAX 1536   // ui_result_page worst case + slack
#define UPDATE_PAGE_MAX 2560   // ui_update_page worst case + slack

static WebServer  server(80);
static DNSServer  s_dns;
static bool       s_captive = false;
static IPAddress  s_ap_ip;

// Rack-unit front-panel config page. Self-contained; webfonts load when the
// network has internet, fall back to mono on the first-setup SoftAP.
// NOTE: input_source toggle is cosmetic until LNK-012 wires the NVS field.
static const char HTML_TMPL[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>X32&middot;SYNC</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/dseg@0.46.0/css/dseg.css" rel="stylesheet">
<style>%CSS%
/* Page-specific: everything above is the shared chrome (ui_chrome_css, ARC-017).
   These rules extend or follow it, and only win by coming second. */
body::after{content:"";position:fixed;inset:0;pointer-events:none;opacity:.05;z-index:99;
background-image:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="120" height="120"><filter id="n"><feTurbulence type="fractalNoise" baseFrequency=".9" numOctaves="2"/></filter><rect width="100%" height="100%" filter="url(%23n)"/></svg>');}
.unit{animation:rise .7s cubic-bezier(.2,.8,.2,1) both}
.scr::before{content:"";position:absolute;inset:0;border-radius:11px;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.25) 0 2px,transparent 2px 3px);opacity:.5}
.scr-foot{display:flex;justify-content:space-between;margin-top:8px;font-size:11px;color:#5e7044;letter-spacing:.14em}
.batt{display:none}
form{padding:6px 26px 26px}
.row{padding:15px 0;border-top:1px solid var(--line)}.row:first-child{border-top:none}
.cap{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:9px}
.cap label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut)}
.cap .hint{font-size:10.5px;color:#4b535b}
.seg{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:4px;background:var(--panel-2);border:1px solid var(--line);border-radius:9px;padding:4px;position:relative}
.seg button{appearance:none;border:0;background:transparent;color:var(--mut);font-family:var(--mono);font-size:13px;letter-spacing:.08em;text-transform:uppercase;padding:11px 8px;border-radius:6px;cursor:pointer;transition:color .18s;z-index:1;position:relative}
.seg button .sub{display:block;font-size:9.5px;color:#454c54;letter-spacing:.1em;margin-top:2px}
.seg button[aria-pressed=true]{color:#0a0d07}.seg button[aria-pressed=true] .sub{color:#2c3a16}
.glide{position:absolute;top:4px;bottom:4px;border-radius:6px;background:linear-gradient(180deg,#caff5a,#9be32a);box-shadow:0 0 16px -2px var(--led),inset 0 1px 0 rgba(255,255,255,.4);transition:left .26s cubic-bezier(.3,1.3,.5,1),width .26s;z-index:0}
.fld{transition:border-color .18s,box-shadow .18s}
.fld input{flex:1;appearance:none;background:transparent;border:0;outline:0;color:var(--ink);font-family:var(--mono);font-size:14.5px;padding:13px 0}
.fld input::placeholder{color:#434a52}
.sw{padding:6px 0}
.sw input:focus-visible + .track{box-shadow:0 0 0 3px rgba(182,255,54,.18)}
.slots{display:grid;grid-template-columns:repeat(8,1fr);gap:6px}
.slot{appearance:none;border:1px solid var(--line);background:var(--panel-2);color:#5b636b;font-family:var(--mono);font-size:13px;border-radius:7px;padding:12px 0;cursor:pointer;transition:.16s}
.slot[aria-pressed=true]{color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f;box-shadow:0 0 14px -3px var(--led)}
.slot:disabled{opacity:.22;cursor:not-allowed}
.write{margin-top:24px;transition:transform .06s,box-shadow .06s}
.write:active{transform:translateY(5px);box-shadow:0 1px 0 #5e8a16,0 8px 18px -12px rgba(182,255,54,.5)}
.write small{display:block;font-family:var(--mono);font-weight:400;font-size:10px;letter-spacing:.2em;color:#2c3a16;margin-top:3px}
.foot{margin-top:18px}
.row,.scr,.brand{animation:rise .55s cubic-bezier(.2,.8,.2,1) both}.scr{animation-delay:.05s}
form .row:nth-child(2){animation-delay:.10s}form .row:nth-child(3){animation-delay:.15s}form .row:nth-child(4){animation-delay:.20s}form .row:nth-child(5){animation-delay:.25s}form .row:nth-child(6){animation-delay:.30s}
@keyframes rise{from{opacity:0;transform:translateY(14px)}to{opacity:1;transform:none}}
</style></head><body>
<div class="unit">
<span class="screw tl"></span><span class="screw tr"></span><span class="screw bl"></span><span class="screw br"></span>
<div class="brand"><span class="pwr"></span><span class="wordmark">X32&middot;<b>SYNC</b></span><span class="rev" id="rev">FW %FWVER%</span></div>
<div class="scr">
<div class="scr-top"><span class="beat" id="beat"></span><span class="scr-lbl">Session Tempo</span><span class="scr-src" id="srcLbl">Ableton Link</span></div>
<div class="readout"><span class="ghost bignum">188.8</span><span class="live"><span class="bignum" id="bpm">%BPM%</span><span class="unit-bpm">BPM</span></span></div>
<div class="scr-foot"><span id="srcFoot">SOURCE&middot;LINK</span><span id="battFoot" class="batt"></span><span>OUT&middot;/fx/&middot;/par/01</span></div>
</div>
<form method="POST" action="/save">
<input type="hidden" name="input_source" id="h_src" value="%SRC%">
<input type="hidden" name="model" id="h_model" value="%MODEL%">
<input type="hidden" name="fx_slot" id="h_slot" value="%SLOT%">
<div class="row">
<div class="cap"><label>Tempo Source</label><span class="hint">restart to apply</span></div>
<div class="seg" id="segSrc"><span class="glide"></span>
<button type="button" data-v="0">Link<span class="sub">multicast UDP</span></button>
<button type="button" data-v="1">MIDI<span class="sub">USB clock</span></button></div>
</div>
<div class="row">
<div class="cap"><label>Mixer Model</label><span class="hint" id="portHint">OSC&middot;:10024</span></div>
<div class="seg" id="segModel"><span class="glide"></span>
<button type="button" data-v="1">XR-Series<span class="sub">XR18 / 16 / 12</span></button>
<button type="button" data-v="2">X32<span class="sub">X32 / Compact</span></button></div>
</div>
<div class="row">
<div class="cap"><label>FX Slot</label><span class="hint" id="slotHint">1 &ndash; 4</span></div>
<div class="slots" id="slots"></div>
</div>
<div class="row">
<div class="cap"><label>Bar Quantum</label><span class="hint">beats per bar &middot; 1&ndash;16</span></div>
<div class="fld"><span class="pre">BEATS</span><input type="number" class="live" name="quantum_beats" value="%QUANTUM%" min="1" max="16" step="1" inputmode="numeric"></div>
</div>
<div class="row">
<div class="cap"><label>MIDI Clock Out</label><span class="hint">Link&rarr;USB 24PPQN 0xF8 &middot; restart to apply</span></div>
<label class="sw"><input type="checkbox" name="midi_clock_out" value="1" %MCKCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
</div>
<div class="row">
<div class="cap"><label>Phase Display</label><span class="hint">beat-flash dot vs sweep wheel &middot; colours also set the beat LED</span></div>
<label class="sw"><input type="checkbox" class="live" name="phase_flash" value="1" %PHFLASHCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="fld" style="margin-top:6px"><span class="pre">BEAT</span><input type="color" class="live" name="dot_beat" value="%DOTBEAT%"></div>
<div class="fld" style="margin-top:6px"><span class="pre">BAR1</span><input type="color" class="live" name="dot_acc" value="%DOTACC%"></div>
</div>
<div class="row">
<div class="cap"><label>Mixer IP</label><span class="hint">on this network</span></div>
<div class="fld"><span class="pre">HOST</span><input type="text" name="mixer_ip" value="%IP%" inputmode="decimal" autocomplete="off"></div>
</div>
<div class="row">
<div class="cap"><label>WiFi Network</label><span class="hint">2.4 GHz</span></div>
<div class="fld"><span class="pre">SSID</span><input type="text" name="wifi_ssid" value="%SSID%" autocomplete="off"></div>
<div class="fld" style="margin-top:6px"><span class="pre">PASS</span><input type="password" name="wifi_pass" placeholder="keep current"></div>
</div>
<button class="write" type="submit">Write &amp; Reboot<small>commit to flash &middot; device restarts</small></button>
<div class="foot">ESP32-S3 &middot; Ableton Link &harr; OSC &middot; FW %FWVER% &middot; %FWBUILD% &middot; <a href="/update" style="color:#4b535b">Firmware Update</a></div>
</form>
</div>
<script>%JS%
// Page-specific. ui_chrome_js (ARC-017) already owns setBeat / flashBeat / showBpm /
// postLive and the poll() loop; poll() hands each /status to onStatus below.
function wireSeg(id,init,onChange){var seg=document.getElementById(id),glide=seg.querySelector('.glide');
var btns=[].slice.call(seg.querySelectorAll('button'));
function move(b){btns.forEach(function(x){x.setAttribute('aria-pressed',x===b)});glide.style.left=b.offsetLeft+'px';glide.style.width=b.offsetWidth+'px';onChange(b.dataset.v)}
btns.forEach(function(b){b.addEventListener('click',function(){move(b)})});
var start=btns.filter(function(b){return b.dataset.v===init})[0]||btns[0];
requestAnimationFrame(function(){move(start)});}
var hSrc=document.getElementById('h_src'),hModel=document.getElementById('h_model'),hSlot=document.getElementById('h_slot');
wireSeg('segSrc',hSrc.value,function(v){hSrc.value=v;
document.getElementById('srcLbl').textContent=v==='0'?'Ableton Link':'USB MIDI Clock';
document.getElementById('srcFoot').textContent=v==='0'?'SOURCE·LINK':'SOURCE·MIDI';
document.getElementById('rev').textContent=v==='0'?'FW %FWVER%·LNK':'FW %FWVER%·MCK';});
var slotsEl=document.getElementById('slots'),maxSlot=4,curSlot=parseInt(hSlot.value)||1;
function renderSlots(){slotsEl.innerHTML='';for(var i=1;i<=8;i++){(function(i){var b=document.createElement('button');
b.type='button';b.className='slot';b.textContent=i;b.disabled=i>maxSlot;b.setAttribute('aria-pressed',i===curSlot);
b.onclick=function(){curSlot=i;hSlot.value=i;renderSlots()};slotsEl.appendChild(b)})(i)}}
wireSeg('segModel',hModel.value,function(v){hModel.value=v;maxSlot=v==='1'?4:8;
document.getElementById('slotHint').textContent='1 – '+maxSlot;
document.getElementById('portHint').textContent='OSC·:'+(v==='1'?'10024':'10023');
if(curSlot>maxSlot){curSlot=maxSlot;hSlot.value=maxSlot}renderSlots()});
renderSlots();
var battFootEl=document.getElementById('battFoot');
var seedBpm=parseFloat(bpmEl.textContent)||0;
// Phase-locked beat dot (LNK-022): poll-correct + client-side interpolate.
// /status is 1Hz (intentionally), too coarse to *drive* a smooth dot directly.
// Each poll that reports valid:true snaps a local anchor {phase,pollMs,bpm,quantum};
// a requestAnimationFrame loop extrapolates phase_now between polls. anchor.phase/
// quantum are the *bar*-quantized reading, so we wrap-detect on phase%1 to flash
// every beat, same as the LED's led_phase_should_flash (LNK-021), not only on the
// once-per-bar quantum wrap. Setting chromePhaseLocked parks the chrome's
// free-running setBeat() fallback.
var anchor=null,prevPhase=-1,rafId=null;
function phaseTick(){
if(!anchor){rafId=null;return}
var elapsedS=(Date.now()-anchor.pollMs)/1000;
var phase=(anchor.phase+elapsedS*anchor.bpm/60)%anchor.quantum;
if(phase<0)phase+=anchor.quantum;
var beatPhase=phase%1;
if(prevPhase>=0&&beatPhase<prevPhase)flashBeat();
prevPhase=beatPhase;
rafId=requestAnimationFrame(phaseTick);
}
function onStatus(d){
if(typeof d.batt_pct==='number'){battFootEl.textContent='BATT·'+d.batt_pct.toFixed(0)+'%';battFootEl.style.display=''}
var valid=d.valid===true&&typeof d.phase==='number'&&typeof d.quantum==='number'&&d.quantum>0;
if(valid){
anchor={phase:d.phase,pollMs:Date.now(),bpm:d.bpm,quantum:d.quantum};
if(!chromePhaseLocked){chromePhaseLocked=true;if(beatTimer){clearInterval(beatTimer);beatTimer=null}
prevPhase=-1;rafId=requestAnimationFrame(phaseTick)}
}else if(chromePhaseLocked){
chromePhaseLocked=false;if(rafId){cancelAnimationFrame(rafId);rafId=null}anchor=null;prevPhase=-1;
setBeat(shownBpm);
}
}
var t=0,boot=setInterval(function(){bpmEl.textContent=(Math.random()*200+40).toFixed(1);
if(++t>6){clearInterval(boot);showBpm(seedBpm);poll();setInterval(poll,1000)}},70);
[].forEach.call(document.querySelectorAll('.live'),function(el){
el.addEventListener(el.type==='color'||el.type==='number'?'input':'change',function(){postLive(el)});});
</script></body></html>)HTML";

// Only the slice between %CSS% and %JS% needs per-request values; the chrome on
// either side is streamed straight from flash.
static String build_body(const char* begin, const char* end) {
    String h;
    h.concat(begin, end - begin);
    TempoSnapshot ts; tempo_snapshot_read(&ts);
    char bpm[8];
    snprintf(bpm, sizeof(bpm), "%.1f", ts.bpm);
    h.replace("%MODEL%", String(g_config.model));
    h.replace("%IP%",    g_config.mixer_ip);
    h.replace("%SLOT%",  String(g_config.fx_slot));
    h.replace("%QUANTUM%", String(g_config.quantum_beats));
    h.replace("%MCKCHK%", g_config.midi_clock_out_enable ? "checked" : "");
    h.replace("%PHFLASHCHK%", g_config.phase_display_mode == 1 ? "checked" : "");
    char dotbuf[8];
    snprintf(dotbuf, sizeof(dotbuf), "#%06X", g_config.dot_beat_color & 0xFFFFFF);
    h.replace("%DOTBEAT%", dotbuf);
    snprintf(dotbuf, sizeof(dotbuf), "#%06X", g_config.dot_accent_color & 0xFFFFFF);
    h.replace("%DOTACC%", dotbuf);
    h.replace("%SRC%",   String(g_config.input_source));
    h.replace("%SSID%",  g_config.wifi_ssid);
    h.replace("%BPM%",   bpm);
    h.replace("%FWVER%",   FW_VERSION);  // LNK-038 — replaces the old hardcoded "FW 2.0"
    h.replace("%FWBUILD%", FW_BUILD);
    return h;
}

// ARC-017: sent chunked from parts. The shared CSS + JS are ~4KB of flash that
// used to be copied into an Arduino String -- and then realloc'd by every
// .replace() pass over it. Now only the middle slice is ever copied.
static void handle_root() {
    static const char CSS_MARK[] = "%CSS%", JS_MARK[] = "%JS%";
    const char *css = strstr(HTML_TMPL, CSS_MARK);
    const char *js  = strstr(HTML_TMPL, JS_MARK);
    if (!css || !js) { server.send(500, "text/plain", "template"); return; }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(HTML_TMPL, css - HTML_TMPL);
    server.sendContent(ui_chrome_css());
    server.sendContent(build_body(css + strlen(CSS_MARK), js));
    server.sendContent(ui_chrome_js());
    // The page's own JS still carries %FWVER% (the source-badge text), so the tail
    // gets a substitution pass too -- sending it raw would print the marker.
    String tail(js + strlen(JS_MARK));
    tail.replace("%FWVER%", FW_VERSION);
    server.sendContent(tail);
    server.sendContent("");   // terminate the chunked response
}

// LNK-034: web-based OTA. Plain multipart form (no JS needed) posts a compiled
// .bin to /update; Update.h streams it straight into the inactive OTA app slot
// (the Arduino esp32 core's default partition scheme reserves app0/app1) and
// the result page reboots into it on success, same shape as handle_save().
/* ARC-017: the /update page is ui_update_page() -- shared with KitchenSync.
 * multipart=true selects the plain form Arduino's Update.h consumes. */

static void handle_update_page() {
    static char page[UPDATE_PAGE_MAX];
    // LNK-038: show what's about to be overwritten.
    int n = ui_update_page(page, sizeof(page), "X32&middot;SYNC", FW_VERSION, FW_BUILD, true);
    if (n < 0 || n >= (int)sizeof(page)) { server.send(500, "text/plain", "page too large"); return; }
    server.send(200, "text/html", page);
}

static void handle_update_upload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA: receiving %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("OTA: wrote %u bytes, restarting\n", upload.totalSize);
        else Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
    }
}

// Live tempo for the panel's 7-seg readout, plus phase/valid/quantum for
// the web beat dot (LNK-022) — same data the LED (LNK-021) and touch wheel
// (LNK-015) already drive off, just surfaced over HTTP so the JS can
// poll-correct + client-side-interpolate instead of free-running off bpm
// alone. quantum is g_config.quantum_beats (bar-quantized, matching the
// touch UI's phase wheel, not the LED's per-beat 1.0f quantum). One atomic
// tempo_snapshot_read() (ARC-001 seam) — never tempo_source_* directly.
static void handle_status() {
    TempoSnapshot ts; tempo_snapshot_read(&ts);
    char buf[128];
#ifdef HAS_BATTERY_GAUGE
    BatterySnapshot bs; battery_snapshot_read(&bs);
    web_status_json(buf, sizeof(buf), ts.bpm, ts.phase, ts.valid, ts.quantum, FW_VERSION,
                     bs.present, bs.volts, bs.percent);
#else
    web_status_json(buf, sizeof(buf), ts.bpm, ts.phase, ts.valid, ts.quantum, FW_VERSION,
                     false, 0.0f, 0.0f);
#endif
    server.send(200, "application/json", buf);
}

// Minimal dark result page matching the panel aesthetic.
// reboot=true adds a script that waits for the device to come back after the restart
// and then loads the config homepage — so Write & Reboot / OTA return to '/' on their
// own instead of leaving the browser stranded on the result page.
static void send_result(int code, const char* title, const char* body, bool reboot) {
    // ARC-017: the page is ui_result_page() -- pure, shared with KitchenSync, tested.
    static char p[RESULT_PAGE_MAX];
    int n = ui_result_page(p, sizeof(p), title, body, reboot);
    if (n < 0 || n >= (int)sizeof(p)) { server.send(500, "text/plain", "page too large"); return; }
    server.send(code, "text/html; charset=utf-8", p);
}

static void handle_update_result() {
    bool ok = !Update.hasError();
    send_result(ok ? 200 : 500,
                ok ? "Updated — Restarting" : "Update Failed",
                ok ? "New firmware written. The device restarts now."
                   : "Upload was interrupted or the image was rejected. Try again.", ok);
    if (ok) {
        delay(1000);
        ESP.restart();
    }
}

static void handle_save() {
    AppConfig cfg = g_config;

    // ARC-012: int fields go through app_config_set — one range owner
    // (config_validate); out-of-range posts are ignored (old value kept) rather than
    // 400-ing. Model first so fx_slot validates against the new model's slot max.
    app_config_set(&cfg, ACF_MODEL,     server.arg("model").toInt());

    String ip = server.arg("mixer_ip");
    if (ip.length() > 0 && ip.length() < (int)sizeof(cfg.mixer_ip))
        ip.toCharArray(cfg.mixer_ip, sizeof(cfg.mixer_ip));

    app_config_set(&cfg, ACF_FX_SLOT,       server.arg("fx_slot").toInt());
    app_config_set(&cfg, ACF_QUANTUM_BEATS, server.arg("quantum_beats").toInt());

    String ssid = server.arg("wifi_ssid");
    if (ssid.length() > 0 && ssid.length() < (int)sizeof(cfg.wifi_ssid))
        ssid.toCharArray(cfg.wifi_ssid, sizeof(cfg.wifi_ssid));

    String pass = server.arg("wifi_pass");
    if (pass.length() > 0 && pass.length() < (int)sizeof(cfg.wifi_pass))
        pass.toCharArray(cfg.wifi_pass, sizeof(cfg.wifi_pass));

    app_config_set(&cfg, ACF_INPUT_SOURCE,   server.arg("input_source").toInt());
    app_config_set(&cfg, ACF_MIDI_CLOCK_OUT, server.arg("midi_clock_out").toInt());  // LNK-027

    // LNK-036: phase display mode + dot colours (unchecked box -> "" -> 0 -> sweep).
    app_config_set(&cfg, ACF_PHASE_DISPLAY_MODE, server.arg("phase_flash").toInt() == 1 ? 1 : 0);
    String cb = server.arg("dot_beat");              // "#RRGGBB" from <input type=color>
    if (cb.length() == 7 && cb[0] == '#')
        app_config_set(&cfg, ACF_DOT_BEAT_COLOR, (int)strtol(cb.c_str() + 1, NULL, 16));
    String ca = server.arg("dot_acc");
    if (ca.length() == 7 && ca[0] == '#')
        app_config_set(&cfg, ACF_DOT_ACCENT_COLOR, (int)strtol(ca.c_str() + 1, NULL, 16));

    if (config_validate(&cfg)) {
        g_config = cfg;
        config_save(&g_config);
        send_result(200, "Saved — Restarting", "Reconnect to WiFi if credentials changed.", true);
        delay(1000);
        ESP.restart();
    } else {
        send_result(400, "Invalid Config", "Check the mixer IP and FX slot, then go back.", false);
    }
}

// LNK-037: apply live-safe fields to the running config with no reboot, mirroring the
// P4's /live. touch_display reads g_config each render, so the phase dot mode/colours
// update instantly.
//
// ARC-022: these edits are now KEPT. This handler had the same bug as the P4's and the
// Touch's -- it applied the value and returned, so a colour or quantum set here looked
// right until the next power cycle and then quietly wasn't. It marks the config dirty
// now; web_config_handle() writes the blob once the edits settle.
static void handle_live() {
    // ARC-012: same validated setter as save; ranges live in config_validate only.
    // Only a value the setter ACCEPTED dirties the config -- a rejected one changed nothing.
    bool edited = false;
    if (server.hasArg("phase_flash"))
        edited |= app_config_set(&g_config, ACF_PHASE_DISPLAY_MODE, server.arg("phase_flash").toInt() == 1 ? 1 : 0);
    if (server.hasArg("dot_beat")) {
        String cb = server.arg("dot_beat");
        if (cb.length() == 7 && cb[0] == '#')
            edited |= app_config_set(&g_config, ACF_DOT_BEAT_COLOR, (int)strtol(cb.c_str() + 1, NULL, 16));
    }
    if (server.hasArg("dot_acc")) {
        String ca = server.arg("dot_acc");
        if (ca.length() == 7 && ca[0] == '#')
            edited |= app_config_set(&g_config, ACF_DOT_ACCENT_COLOR, (int)strtol(ca.c_str() + 1, NULL, 16));
    }
    if (server.hasArg("quantum_beats"))
        edited |= app_config_set(&g_config, ACF_QUANTUM_BEATS, server.arg("quantum_beats").toInt());
    if (edited) config_persist_mark(&s_persist, millis());
    server.send(200, "text/plain", "ok");
}

static void handle_captive_redirect() {
    String url = "http://";
    url += s_ap_ip.toString();
    url += "/";
    server.sendHeader("Location", url, true);
    server.send(302, "text/plain", "");
}

void web_config_begin() {
    config_persist_reset(&s_persist);   // ARC-022: clean at boot — never write on the way up
    server.on("/",       HTTP_GET,  handle_root);
    server.on("/status", HTTP_GET,  handle_status);
    server.on("/save",   HTTP_POST, handle_save);
    server.on("/live",   HTTP_POST, handle_live);   // LNK-037: no-reboot preview
    server.on("/update", HTTP_GET,  handle_update_page);
    server.on("/update", HTTP_POST, handle_update_result, handle_update_upload);
    server.begin();
}

void web_config_ap_begin() {
    config_persist_reset(&s_persist);   // ARC-022: this path serves /live too
    s_ap_ip   = WiFi.softAPIP();
    s_captive = true;

    // Wildcard DNS: every domain resolves to our AP IP.
    s_dns.start(53, "*", s_ap_ip);

    server.on("/",       HTTP_GET,  handle_root);
    server.on("/status", HTTP_GET,  handle_status);
    server.on("/save",   HTTP_POST, handle_save);
    server.on("/live",   HTTP_POST, handle_live);   // LNK-037: no-reboot preview

    // OS captive-portal detection endpoints → redirect to config page.
    server.on("/hotspot-detect.html",  HTTP_GET, handle_captive_redirect);  // iOS/macOS
    server.on("/success.html",         HTTP_GET, handle_captive_redirect);
    server.on("/generate_204",         HTTP_GET, handle_captive_redirect);  // Android
    server.on("/ncsi.txt",             HTTP_GET, handle_captive_redirect);  // Windows
    server.on("/connecttest.txt",      HTTP_GET, handle_captive_redirect);
    server.onNotFound(handle_captive_redirect);

    server.begin();
}

void web_config_handle() {
    if (s_captive) s_dns.processNextRequest();
    server.handleClient();
    // ARC-022: write the blob once the live edits have settled. due() is a couple of
    // unsigned compares when nothing is owed, so polling it from loop() is free; the
    // write itself is rare by construction, which matters because a flash write
    // suspends the cache and freezes both cores.
    if (config_persist_due(&s_persist, millis())) config_save(&g_config);
}
