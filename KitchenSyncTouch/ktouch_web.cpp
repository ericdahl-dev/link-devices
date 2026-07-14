// KitchenSync Touch web config server — see ktouch_web.h (ESP-016, Inc3).
#include "ktouch_web.h"
#include "app_config.h"
#include "ui_chrome.h"
#include "fw_version.h"
#include "tempo_source.h"     // live BPM / sync for /status
#include "link_protocol.h"    // link_proto_peers()
#include "ktouch_transport.h" // transport state for /status
#include "ktouch_display.h"   // live backlight brightness
#include "ktouch_midi_out.h"  // ESP-018 tick health
#include "ks_status.h"        // ESP-029: the SHARED /status builder — one shape across the fleet
#include "ks_config_json.h"   // ESP-030: the SHARED /config.json builder + KsCaps
#include "config.h"           // ESP-030: KSTOUCH_HAS_* — what is actually wired to this board
// ESP-025 bench-rig button telemetry. Only the DevKit has buttons, and these live in the
// .ino behind HAS_BUTTONS -- referencing them unconditionally breaks the S3 product link.
#ifdef HAS_BUTTONS
uint32_t ktouch_btn_presses(void); uint32_t ktouch_btn_lows(void); int ktouch_btn_level(void);
#else
static inline uint32_t ktouch_btn_presses(void) { return 0; }
static inline uint32_t ktouch_btn_lows(void)    { return 0; }
static inline int      ktouch_btn_level(void)   { return -1; }   // -1 = no buttons fitted
#endif
#include "config_persist.h"   // ARC-022: debounced write-through for live edits
#include <string.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>           // ESP-020: OTA into the inactive app slot

#define UPDATE_PAGE_MAX 3072   // ui_update_page worst case + slack (ESP-020: the fetch
                               // variant renders 2386 bytes, so 2560 left only 174 spare)

extern AppConfig g_config;

static WebServer server(80);

// ARC-022: /nudge and /bright used to apply and return -- the edit was real until the
// next power cycle and then silently wasn't. They now mark the config dirty and
// ktouch_web_tick() writes the blob once the edits settle. Marked, not written,
// because config is ONE nvs blob: a slider drag is dozens of POSTs.
static ConfigPersist s_persist;

// The config form body (between the shared <style> and <script>). Panel classes
// come from ui_chrome_css(). %TOKENS% are filled per request.
static const char FORM[] = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KitchenSync Touch</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/dseg@0.46.0/css/dseg.css" rel="stylesheet">
<style>%CSS%
/* Page-specific: everything above is the shared chrome (ui_chrome_css). These
   rules extend it and win only by coming second -- the same way ks_web.cpp (the
   P4 gold standard) extends the chrome. Touch mirrors the P4's structure with a
   smaller feature set. */
.unit{padding:0 0 20px}.brand{padding:16px 22px 12px}
.grp{padding:0 22px}
.frow{padding:12px 0;border-top:1px solid var(--line)}
.cap{font-family:var(--disp);font-weight:600;font-size:12.5px;letter-spacing:.12em;color:var(--ink);margin-bottom:10px;display:block}
/* Group header: the section title (with a dim status tick) pairs with the
   feature's master toggle -- the P4's .frow.head device. */
.frow.head{padding:18px 0 12px}
.frow.head .cap{margin-bottom:12px}
.frow.head .cap::before{content:"";display:inline-block;width:6px;height:6px;border-radius:1px;background:var(--led-dim);margin-right:10px;vertical-align:2px}
.frow.head .sw{margin-top:0}
/* Accent rail: a feature's settings live behind its toggle and appear when it
   flips on -- the P4's .sect. */
.sect{position:relative;margin:0 0 4px 3px;padding:0 0 6px 18px;border-left:2px solid var(--line)}
.sect::before{content:"";position:absolute;left:-2px;top:0;width:2px;height:28px;background:var(--led-dim)}
.sect > .frow:first-child{border-top:0;padding-top:12px}
.hide{display:none!important}
/* Help captions: one shared class, replacing the old per-element inline styles. */
.hint{font-family:var(--mono);font-size:11px;letter-spacing:.02em;line-height:1.45;color:var(--mut);margin-top:8px}
.fld{margin-top:8px}.sw{margin-top:12px}
.fld .pre{min-width:54px}
.fld input{flex:1;min-width:0;appearance:none;background:transparent;border:0;outline:0;color:var(--ink);font-family:var(--mono);font-size:14.5px;padding:12px 0}
.fld input::placeholder{color:#434a52}
.rows{padding:2px 22px 4px}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:12px 0;border-top:1px solid var(--line)}
.row label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut)}
.val{font-family:var(--mono);font-size:14px;color:var(--ink);letter-spacing:.06em}
.pill{font-size:10.5px;letter-spacing:.16em;text-transform:uppercase;padding:4px 9px;border-radius:999px;border:1px solid var(--line);color:var(--mut)}
.pill.on{color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f}
.fld.nudge{gap:9px}.fld.nudge input{text-align:center;padding:9px 0}
.stp{flex:none;width:44px;height:44px;border-radius:9px;border:1px solid var(--line);background:linear-gradient(180deg,#1a1f25,#12161b);color:var(--ink);font-family:var(--disp);font-weight:800;font-size:22px;line-height:1;cursor:pointer;display:flex;align-items:center;justify-content:center;user-select:none;-webkit-user-select:none;touch-action:manipulation}
.stp:active{background:#0d1014;transform:translateY(1px);border-color:#4a5a2c}
.write{margin-top:20px}
.write:active{transform:translateY(4px);box-shadow:0 1px 0 #5e8a16}
.foot{padding-top:16px}
/* Desktop: widen the card, flow the config into two columns, and turn the four
   status readouts into a 4-across meter bridge -- the P4's treatment, sized for
   Touch's shorter form. Status glass and Save button stay full width. */
@media (min-width:760px){
.unit{max-width:760px}
.formcols{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);column-gap:24px;align-items:start;padding:0 22px}
.grp{padding-left:0;padding-right:0}
.rows{display:grid;grid-template-columns:repeat(4,1fr);column-gap:24px;padding-top:4px}
.row{border-top:0;flex-direction:column;align-items:flex-start;gap:7px;padding:16px 0}
.bignum{font-size:74px}
.foot{max-width:760px}
}
</style></head><body>
<div class="unit">
<span class="screw tl"></span><span class="screw tr"></span><span class="screw bl"></span><span class="screw br"></span>
<div class="brand"><span class="pwr"></span><span class="wordmark">KITCHEN&middot;<b>SYNC</b> TOUCH</span><span class="rev">FW %FWVER%</span></div>
<div class="scr">
<div class="scr-top"><span class="beat" id="beat"></span><span class="scr-lbl">Session Tempo</span><span class="scr-src">Ableton Link</span></div>
<div class="readout"><span class="ghost bignum">188.8</span><span class="live"><span class="bignum" id="bpm">--.-</span><span class="unit-bpm">BPM</span></span></div>
</div>
<div class="rows">
<div class="row"><label>Link Peers</label><span class="val" id="peers">0</span></div>
<div class="row"><label>Sync</label><span class="pill" id="sync">No Link</span></div>
<div class="row"><label>MIDI Clock</label><span class="pill" id="clk">Off</span></div>
<div class="row"><label>Transport</label><span class="pill" id="tp">Stopped</span></div>
</div>
<form method="POST" action="/save">
<div class="formcols">
<div class="grp"><div class="frow head"><span class="cap">WiFi Network</span></div>
<div class="fld"><span class="pre">SSID</span><input name="wifi_ssid" value="%SSID%" autocomplete="off"></div>
<div class="fld"><span class="pre">PASS</span><input name="wifi_pass" type="password" placeholder="keep current"></div></div>
<div class="grp"><div class="frow head"><span class="cap">Transport</span>
<label class="sw"><input type="checkbox" name="transport" value="1" %TP%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="hint">quantized MIDI Start / Stop over the DIN jack</div></div>
<div class="sect" data-when="transport">
<div class="frow"><span class="cap">Launch Quantize</span>
<div class="fld"><span class="pre">BARS</span><input name="quantum" type="number" min="1" max="16" value="%Q%" inputmode="numeric"></div>
<div class="hint">bars per phrase (4/4) &mdash; start fires on the next phrase line</div></div>
<div class="frow"><span class="cap">Play On Release</span>
<label class="sw"><input type="checkbox" name="play_rel" value="1" %REL%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="hint">turntable feel &mdash; transport fires when you lift off</div></div>
</div></div>
<div class="grp"><div class="frow head"><span class="cap">MIDI Clock Out</span>
<label class="sw"><input type="checkbox" name="clock" value="1" %CLK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label>
<div class="hint">24 PPQN clock to the DIN jack</div></div>
<div class="sect" data-when="clock">
<div class="frow"><span class="cap">Clock Nudge</span>
<div class="fld nudge"><span class="pre">NUDGE</span><button type="button" class="stp" data-step="-5">&minus;</button><input type="number" id="nudge" name="nudge" value="%NUDGE%" min="-250" max="250" step="5" inputmode="numeric"><button type="button" class="stp" data-step="5">+</button></div>
<div class="hint">millibeats &mdash; slide the DIN clock into the pocket, live</div></div>
</div></div>
<div class="grp"><div class="frow head"><span class="cap">Display</span></div>
<div class="fld nudge"><span class="pre">BRIGHT</span><button type="button" class="stp" data-step="-10">&minus;</button><input type="number" id="bright" name="bright" value="%BRIGHT%" min="10" max="100" step="10" inputmode="numeric"><button type="button" class="stp" data-step="10">+</button></div>
<div class="hint">LCD backlight (%) &mdash; applies live</div></div>
</div>
<div style="padding:0 22px"><button class="write" type="submit">Save &amp; Reboot</button></div>
</form>
<div class="foot">KitchenSync Touch &middot; FW %FWVER% &middot; <a href="/update" style="color:#4b535b">Firmware Update</a></div>
</div>
<script>%JS%
// ui_chrome_js owns showBpm/setBeat/poll; onStatus paints our own rows.
var peersEl=document.getElementById('peers'),syncEl=document.getElementById('sync'),
    clkEl=document.getElementById('clk'),tpEl=document.getElementById('tp');
function onStatus(d){
peersEl.textContent=d.peers;
/* ESP-028: these pills used to describe INTENT, not reality. Sync showed whether a phase
   estimate existed and MIDI Clock showed a CONFIG FLAG -- so both read healthy through 138
   seconds of a dead wire. They now report what the writer is actually doing. */
var s=d.sync;syncEl.textContent=s===1?'Locked':s===0?'Free Run':'No Link';syncEl.className='pill'+(s===1?' on':'');
/* SILENT is the state that must never be quiet about itself. Not 'on' (green) -- it is a
   fault, and a green pill over a dead jack is how ESP-027 hid for 138 seconds. */
var c=d.clk;clkEl.textContent=c==='locked'?'Locked':c==='free'?'Free Run':d.clock?'SILENT':'Off';
clkEl.className='pill'+((c==='locked'||c==='free')?' on':'');
var t=d.transport;tpEl.textContent=t===2?'Playing':t===1?'Arming':'Stopped';tpEl.className='pill'+(t===2?' on':'');
}
poll();setInterval(poll,1000);
// Reveal each feature's settings when its master toggle is on -- the P4's
// syncSect. The server renders the toggle's checked state; we mirror it into the
// accent rail on load (so a disabled feature starts collapsed) and on every flip.
// Fields stay in the DOM while hidden, so they still submit and /save is unchanged.
function syncSect(cb){var s=document.querySelectorAll('.sect[data-when="'+cb.name+'"]');
for(var i=0;i<s.length;i++){cb.checked?s[i].classList.remove('hide'):s[i].classList.add('hide')}}
['transport','clock'].forEach(function(n){var cb=document.querySelector('input[name="'+n+'"]');
if(cb){cb.addEventListener('change',function(){syncSect(cb)});syncSect(cb)}});
// Steppers bump their sibling input, clamp, then fire its change handler; each
// input owns which endpoint it POSTs to (live, no reboot).
function bump(i,d){var v=(parseInt(i.value,10)||0)+d;var lo=parseInt(i.min,10),hi=parseInt(i.max,10);
if(v<lo)v=lo;if(v>hi)v=hi;i.value=v;i.dispatchEvent(new Event('change'))}
Array.prototype.forEach.call(document.querySelectorAll('.stp'),function(b){
b.addEventListener('click',function(){bump(b.parentNode.querySelector('input'),parseInt(b.getAttribute('data-step'),10)||0)})});
function live(id,url){var el=document.getElementById(id);if(el)el.addEventListener('change',function(){
fetch(url+this.value,{method:'POST'}).catch(function(){})})}
live('nudge','/nudge?mb=');live('bright','/bright?pct=');
</script>
</body></html>)HTML";

// The config model stores Link beats; the UI speaks bars (4/4). 4 beats = 1 bar.
static const int BEATS_PER_BAR = 4;

static void handle_root() {
    String h(FORM);
    h.replace("%CSS%", ui_chrome_css());
    h.replace("%JS%",  ui_chrome_js());
    h.replace("%SSID%", g_config.wifi[0].ssid);
    int bars = g_config.quantum_beats / BEATS_PER_BAR; if (bars < 1) bars = 1;
    h.replace("%Q%",    String(bars));
    h.replace("%NUDGE%", String(g_config.nudge_mbeats));
    h.replace("%BRIGHT%", String(g_config.brightness));
    h.replace("%CLK%",  g_config.clock_enable ? "checked" : "");
    h.replace("%TP%",   g_config.transport_enable ? "checked" : "");
    h.replace("%REL%",  g_config.play_on_release ? "checked" : "");
    h.replace("%FWVER%", FW_VERSION);
    server.send(200, "text/html", h);
}

static void handle_save() {
    AppConfig c = g_config;
    if (server.hasArg("wifi_ssid")) strlcpy(c.wifi[0].ssid, server.arg("wifi_ssid").c_str(), sizeof(c.wifi[0].ssid));
    // blank password = keep current
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length())
        strlcpy(c.wifi[0].pass, server.arg("wifi_pass").c_str(), sizeof(c.wifi[0].pass));
    // Checkboxes: absent = off. Numbers via the validating setter. The BARS field
    // is bars; store as Link beats (x4). NUDGE persists whatever /nudge set live.
    app_config_set(&c, ACF_QUANTUM_BEATS,    server.arg("quantum").toInt() * BEATS_PER_BAR);
    app_config_set(&c, ACF_CLOCK_ENABLE,     server.hasArg("clock")     ? 1 : 0);
    app_config_set(&c, ACF_TRANSPORT_ENABLE, server.hasArg("transport") ? 1 : 0);
    app_config_set(&c, ACF_PLAY_ON_RELEASE,  server.hasArg("play_rel")  ? 1 : 0);
    if (server.hasArg("nudge"))  app_config_set(&c, ACF_NUDGE_MBEATS, server.arg("nudge").toInt());
    if (server.hasArg("bright")) app_config_set(&c, ACF_BRIGHTNESS,   server.arg("bright").toInt());

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

// Live status for the 1 Hz poll.
//
// ESP-028: `sync` used to be tempo_source_phase_valid() -- whether a phase ESTIMATE exists.
// That is not what anyone is asking. During ESP-027 it reported sync:1 peers:1 clock:1 drop:0
// while the DIN wire was DEAD for 138 seconds. It did not merely fail to show the fault; it
// said the device was healthy, which is why the fault needed a logic analyzer to find.
//
// `sync` now reports whether PULSES ARE REACHING THE JACK, sourced from the writer, which is
// the only thing that knows:
//     1  locked  -- phase-locked to a Link session, pulses flowing
//     0  free    -- free-running (no xform, or solo via master_clock), pulses flowing
//    -1  silent  -- NOT EMITTING. The state that must never be invisible again.
//
// `clk` carries the same thing as a word, and `pulses` is a lifetime 0xF8 count so a poller
// can watch the wire advance without an analyzer.
static void handle_status() {
    // ESP-028: from the WRITER, not link_proto_bpm() -- see ktouch_midi_out.h. A free-running
    // clock has a tempo; reporting 0.0 while emitting 120 BPM is the same class of lie as sync.
    float       bpm  = ktouch_midi_bpm();
    if (bpm <= 0.0f) bpm = tempo_source_bpm();   // pre-writer (clock disabled): fall back
    const char* clk  = ktouch_midi_clock_state();          // "locked" | "free" | "silent"
    int         sync = (strcmp(clk, "locked") == 0) ?  1
                     : (strcmp(clk, "free")   == 0) ?  0
                     :                                -1;

    /* ESP-029: /status is now built by the SHARED ks_status_json (X32Link/ks_status.c),
     * the same function KitchenSync uses. This device used to hand-roll its own snprintf
     * here, and the two drifted -- the same nine tick-health fields, spelled `wbeats` here
     * and `w_beats` there. Nobody chose that; it is just what a second implementation does.
     *
     * The clock ENGINE was already shared (46 symlinked modules from X32Link/). Only the
     * web layer was not, which is exactly where the drift lived. Now it is shared too, so
     * the fleet emits one shape by construction rather than by discipline (ARC-024).
     *
     * A client (the iOS app) could not decode this device AT ALL before this change:
     * KsStatus threw on the first required key it was missing. */

    /* The launch state IS the shared TL_STOPPED/TL_ARMED/TL_RUNNING enum
     * (transport_launch.h) -- the same one the P4 puts in launch[]. Nothing to invent:
     * this device already tracked it, it simply never published it.
     *
     * ONE output. The array length IS the output count -- never padded to four, or a
     * client would render one real output card and three dead ones. */
    const int launch[1] = { ktouch_transport_state() };

    WebTickHealth tick = {
        .dropped    = ktouch_midi_dropped(),
        .bursts     = ktouch_midi_bursts(),
        .max_gap_us = ktouch_midi_max_gap(),
        .max_work_us= ktouch_midi_max_work(),
        .overruns   = ktouch_midi_overruns(),
        .w_beats    = ktouch_midi_w_beats(),
        .w_clock    = ktouch_midi_w_clock(),
        .core       = ktouch_midi_core(),
        .reprimes   = 0,
    };

    const uint32_t pulses = ktouch_midi_pulses();

    /* This device's OWN diagnostics, which its page reads. They ride in `extra` so the
     * SHARED keys stay byte-identical with KitchenSync's while this device can still say
     * more. `sync` is kept as a legacy alias of `clk` for the existing page JS. */
    char extra[220];
    snprintf(extra, sizeof(extra),
             "\"sync\":%d,\"clock\":%d,\"transport\":%d,\"cue\":%d,"
             "\"tfail\":%lu,\"tzero\":%lu,\"ccancel\":%lu,\"wtport\":%lu,"
             "\"beats\":%.2f,\"locked\":%d,\"bsactive\":%d,"
             "\"btn\":%d,\"btnlows\":%lu,\"btnpress\":%lu",
             sync, g_config.clock_enable ? 1 : 0, ktouch_transport_state(), ktouch_cueing(),
             (unsigned long)ktouch_touch_fails(), (unsigned long)ktouch_touch_zeros(),
             (unsigned long)ktouch_cue_cancels(), (unsigned long)ktouch_midi_w_tport(),
             (double)ktouch_midi_beats(), ktouch_midi_locked(), ktouch_midi_bs_active(),
             ktouch_btn_level(), (unsigned long)ktouch_btn_lows(),
             (unsigned long)ktouch_btn_presses());

    /* Honest zeroes, not flattering ones. This device has no MIDI-clock IN, no USB-MIDI
     * host, and no mic follow-beat -- so those fields report the truth about hardware that
     * is not fitted, rather than a plausible-looking default. Reporting `usb:true` on a
     * device with no USB-MIDI is the same class of lie as ESP-028's `sync:1` over a dead
     * wire, and this file of all files should not repeat it. */
    char buf[720];
    ks_status_json(buf, sizeof(buf),
                   bpm,
                   0.0f,                      // min: no MIDI-clock IN on this hardware
                   link_proto_peers(),
                   false,                     // usb: DIN out only, no USB-MIDI host
                   pulses,                    // tx: the clock-pulse count == pulses here
                   FW_VERSION,
                   false, 0.0f, 0.0f, false,  // follow_*: no mic follow-beat on this hardware
                   launch, 1,
                   ktouch_transport_state() == TL_RUNNING,
                   link_proto_start_stop_seen(),
                   &tick,
                   nullptr,                   // phase: no GhostXForm gauge published here yet
                   clk, &pulses,              // ESP-028 writer's truth -- this device HAS it
                   extra);
    server.send(200, "application/json", buf);
}

/* ESP-030: GET /config.json — the device's ACTUAL settings, from the SHARED builder.
 *
 * Without this a client is read-only for configuration FOREVER: POST /save is a full
 * form, so with no read there is no read-modify-write, and a client would have to POST
 * fabricated defaults and clobber every setting it could not see. The iOS app correctly
 * refuses to do that and disables its settings screen. This is what turns it back on.
 *
 * This device reports ONLY what is actually wired to it (config.h's KSTOUCH_HAS_*).
 * Absent hardware is ABSENT from the document, never reported false: `led:false` with no
 * strip attached is the same class of lie as ESP-028's `sync:1` over a dead wire, and a
 * client would draw an LED section for a board that cannot light anything.
 *
 * Solder a strip on, flip KSTOUCH_HAS_LED, and the section appears here AND in the app,
 * with no app change. Capability, not product identity.
 */
static void handle_config_json() {
    static const KsCaps caps = {
        .metronome   = (bool)KSTOUCH_HAS_METRONOME,
        .led         = (bool)KSTOUCH_HAS_LED,
        .follow_beat = (bool)KSTOUCH_HAS_FOLLOWBEAT,
        .outputs     = KSTOUCH_CLOCK_OUTPUTS,
    };

    /* This device's own AppConfig, expressed in the shared KsConfig shape. Only the
     * fields it really has are populated; the rest are defaults that `caps` then stops
     * from ever being emitted. */
    KsConfig c;
    ks_config_defaults(&c);

    for (int i = 0; i < KS_WIFI_SLOTS; i++) c.wifi[i] = g_config.wifi[i];

    c.clock_out_enable = g_config.clock_enable;

    /* The single DIN output. `nudge_mbeats` IS the phase trim -- the same concept the P4
     * calls clk0_phase, so it maps 1:1 and a client edits one field, not two. DIN MIDI is
     * fixed at 24 PPQN and has no USB cable, so those carry their honest constants. */
    c.clock[0].enable       = g_config.clock_enable;
    c.clock[0].cable        = 0;          /* DIN, not a USB-MIDI virtual cable */
    c.clock[0].ppqn         = 24;         /* MIDI clock; not configurable on this jack */
    c.clock[0].phase_mbeats = g_config.nudge_mbeats;
    c.clock[0].swing_mbeats = 0;
    c.clock[0].follow_link  = g_config.transport_enable ? 0 : 1;

    char buf[768];
    ks_config_json(buf, sizeof(buf), &c, &caps);
    server.send(200, "application/json", buf);
}

// Live clock nudge — the writer task reads g_config.nudge_mbeats every tick, so
// this takes effect immediately with no reboot. ARC-022: and it is now also KEPT.
// The old comment here said "persisted only on Save (no flash wear while the DJ
// dials it in)" -- the flash-wear worry was real, but the answer to it is a
// debounce, not throwing the edit away at the next power cycle.
static void handle_nudge() {
    if (server.hasArg("mb") && app_config_set(&g_config, ACF_NUDGE_MBEATS, server.arg("mb").toInt()))
        config_persist_mark(&s_persist, millis());
    server.send(200, "text/plain", "ok");
}

// ESP-025 transport over HTTP. The Touch could only be driven from its LCD, which leaves
// a HEADLESS unit -- the bench rig -- with no way to start the clock at all. It also makes
// the ESP-023 analyzer check (is 0xFA emitted BEFORE the downbeat 0xF8?) scriptable instead
// of dependent on a human finger, which is what ESP-024's timing runs need.
//
// Posts an INTENT, exactly like a button or a touch. It does not touch the wire: the 1 ms
// writer owns transport_launch and fires START on the bar line (STOP is immediate).
//
//   POST /transport?play=1   PLAY      (arms; fires on the next bar line)
//   POST /transport?play=0   STOP      (immediate)
//   POST /transport?realign=1          (arms; 0xFC then 0xFA on the next bar line)
static void handle_transport() {
    if (server.hasArg("realign")) {
        ktouch_transport_post(TL_INTENT_REALIGN);
        server.send(200, "text/plain", "realign armed\n");
        return;
    }
    if (server.hasArg("play")) {
        bool play = server.arg("play").toInt() != 0;
        ktouch_transport_post(play ? TL_INTENT_PLAY : TL_INTENT_STOP);
        server.send(200, "text/plain", play ? "play armed\n" : "stopped\n");
        return;
    }
    server.send(400, "text/plain", "want ?play=1|0 or ?realign=1\n");
}

// Live backlight brightness. Same pattern as /nudge: apply now, persist once settled.
static void handle_bright() {
    if (server.hasArg("pct") && app_config_set(&g_config, ACF_BRIGHTNESS, server.arg("pct").toInt())) {
        ktouch_display_set_brightness(g_config.brightness);
        config_persist_mark(&s_persist, millis());
    }
    server.send(200, "text/plain", "ok");
}

// ESP-020: web OTA. The Touch was the ONE firmware in this repo with no OTA path at
// all -- and it is the firmware in Dan's unit. Without this, every future fix on a box
// that lives in someone else's rig needs physical access and a USB cable.
//
// This is wiring, not building. The page is ui_update_page() from ui_chrome, already
// shared with X32Link and KitchenSync (ARC-017) -- do not write a second one. The
// partition table already has the app0/app1 slots (4 MB each); they have just been
// sitting there unused.
//
// SAFETY: Update.h streams into the INACTIVE app slot. The running firmware is never
// touched, and the bootloader only switches slots after the image validates. So a
// truncated or aborted upload fails to validate and the device simply reboots into
// what it was already running. That is a property of the mechanism, not a hope -- but
// it is on the ticket to verify by deliberately killing an upload halfway.
static void handle_update_page() {
    static char page[UPDATE_PAGE_MAX];
    // Show what is about to be overwritten.
    int n = ui_update_page(page, sizeof(page), "KitchenSync Touch", FW_VERSION, FW_BUILD, true);
    if (n < 0 || n >= (int)sizeof(page)) { server.send(500, "text/plain", "page too large"); return; }
    server.send(200, "text/html", page);
}

static void handle_update_upload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[KSTouch] OTA: receiving %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[KSTouch] OTA: wrote %u bytes, restarting\n", upload.totalSize);
        else Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();   // release the slot; the running image is untouched
    }
}

static void handle_update_result() {
    bool ok = !Update.hasError();
    char p[1024];
    ui_result_page(p, sizeof(p),
                   ok ? "Updated &mdash; Restarting" : "Update Failed",
                   ok ? "New firmware written. The device restarts now."
                      : "Upload was interrupted or the image was rejected. The device is still "
                        "running the firmware it booted with. Try again.",
                   ok);
    server.send(ok ? 200 : 500, "text/html", p);
    if (ok) { delay(1000); ESP.restart(); }
}

void ktouch_web_begin(void) {
    config_persist_reset(&s_persist);   // ARC-022: clean at boot — never write on the way up
    server.on("/",       handle_root);
    server.on("/status", handle_status);
    server.on("/config.json", handle_config_json);   // ESP-030
    server.on("/save",   HTTP_POST, handle_save);
    server.on("/nudge",  HTTP_POST, handle_nudge);
    server.on("/transport", HTTP_POST, handle_transport);   // ESP-025: headless transport
    server.on("/bright", HTTP_POST, handle_bright);
    server.on("/update", HTTP_GET,  handle_update_page);              // ESP-020
    server.on("/update", HTTP_POST, handle_update_result, handle_update_upload);
    server.begin();
}

void ktouch_web_tick(void) {
    server.handleClient();
    // ARC-022: write the blob once the live edits have settled. loop() calls this
    // every ~5 ms, and due() is a couple of unsigned compares when nothing is owed,
    // so polling it here is free. The write itself is rare by construction -- once
    // per settled burst -- which matters because a flash write freezes both cores.
    if (config_persist_due(&s_persist, millis())) config_save(&g_config);
}
