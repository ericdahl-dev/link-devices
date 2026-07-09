/*
 * KitchenSync web UI (P4-007) — rack-panel status + config page styled like X32Link,
 * served over esp_http_server. Thin glue: live values from pure ks_status.c
 * (/status, polled 1 Hz); config model is pure ks_config.c; NVS + reboot are
 * the only side effects. CSS/aesthetic lifted from X32Link/web_config.cpp.
 */
#include <string>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "midi_clock_in.h"   /* detected MIDI-clock-IN tempo for /status (P4-011) */
#include "link_protocol.h"
#include "wifi_link.h"
#include "usb_midi_host.h"
#include "fw_version.h"      /* LNK-038: shared FW_VERSION / FW_BUILD (X32Link/ include path) */
#include "ks_status.h"
#include "follow_beat_io.h"   /* P4-020: mic tempo-follow estimate for /status (Task 9 creates this header) */
#include "ks_config.h"
#include "ks_config_nvs.h"
#include "ks_form.h"
#include "ks_web.h"
#include "metronome_audio.h"   // P4-029: live vol/voice re-apply

static const char *TAG = "ks_web";
static KsConfig *s_cfg = nullptr;
static volatile uint32_t *s_gen = nullptr;   // bumped on /live so the clock task re-primes
static SemaphoreHandle_t s_cfg_mutex = nullptr;  // ARC-016: guards the /live patch

// %SSID% / %MCKCHK% / %CABLE% are filled per-request from the live config.
static const char PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KitchenSync</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/dseg@0.46.0/css/dseg.css" rel="stylesheet">
<style>
:root{--bg:#070809;--panel-2:#0f1216;--ink:#e9ece6;--mut:#838d95;--line:#262b31;
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
.fld.nudge{gap:9px}
.fld.nudge input{text-align:center;padding:9px 0}
.stp{flex:none;width:46px;height:46px;border-radius:9px;border:1px solid var(--line);
background:linear-gradient(180deg,#1a1f25,#12161b);color:var(--ink);font-family:var(--disp);
font-weight:800;font-size:24px;line-height:1;cursor:pointer;display:flex;align-items:center;
justify-content:center;user-select:none;-webkit-user-select:none;touch-action:manipulation}
.stp:active{background:#0d1014;transform:translateY(1px);border-color:#4a5a2c}
.fld input[type=color]{-webkit-appearance:none;appearance:none;flex:none;width:52px;height:30px;padding:0;border:1px solid var(--line);border-radius:7px;background:transparent;cursor:pointer}
.fld input[type=color]::-webkit-color-swatch-wrapper{padding:2px}
.fld input[type=color]::-webkit-color-swatch{border:0;border-radius:5px}
.sect{position:relative;margin:0 0 8px 3px;padding:0 0 10px 18px;border-left:2px solid var(--line)}
.sect::before{content:"";position:absolute;left:-2px;top:0;width:2px;height:28px;background:var(--led-dim)}
.sect > .frow:first-child{border-top:0;padding-top:12px}
.hide{display:none}
.frow.head{padding:18px 0 12px}
.frow.head .cap{font-family:var(--disp);font-weight:600;font-size:12.5px;letter-spacing:.12em;color:var(--ink);margin-bottom:12px}
.frow.head .cap::before{content:"";display:inline-block;width:6px;height:6px;border-radius:1px;background:var(--led-dim);margin-right:10px;vertical-align:2px}
.frow.out{border-top:0;margin-top:10px;padding:14px;border:1px solid var(--line);border-radius:11px;background:linear-gradient(180deg,rgba(255,255,255,.02),transparent)}
.frow.out:first-child{margin-top:2px}
.frow.out .cap{color:#8b949c;margin-bottom:4px}
.fld .pre{min-width:54px}
.grid2,.colrow{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:6px}
.grid2 .fld,.colrow .fld{margin-top:0}
.fld.color{margin-top:0}
.fld.color input[type=color]{flex:1;width:auto;height:30px;margin-left:0}
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
/* Let the 2-up grid cells shrink to their track so wide inner controls (the
   nudge steppers) don't spill past the card edge. Applies at every width. */
.grid2,.colrow{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}
.fld input{min-width:0}
/* Each clock output stacks RATE / NUDGE / SWING as full rows at every width —
   side-by-side pinches the rate label to "MIDI c" and squeezes the nudge input. */
.sect[data-when="clock_out"] .grid2{grid-template-columns:minmax(0,1fr)}
/* Wide viewports (desktop): widen the card and pack the config form into two
   masonry-flow columns so it isn't a long, skinny strip. Column-flow (not grid)
   keeps a short group from waiting on a tall neighbour's row height. Status
   readout stays full-width above. */
@media (min-width:760px){
.unit{max-width:900px}
/* Real grid (not masonry) so the tall MIDI Clock Out group can span both
   columns as a hero band instead of being dumped into one column. order:-1
   floats it to the top on desktop while the DOM keeps WiFi first for the
   phone setup flow (order is ignored in the mobile block layout). */
.formcols{display:grid;grid-template-columns:1fr 1fr;column-gap:26px;align-items:start}
.grp{margin-bottom:16px}
.grp--wide{grid-column:1/-1;order:-1}
.grp--wide .sect{display:grid;grid-template-columns:1fr 1fr;gap:6px 22px}
.grp--wide .sect .frow.out{margin-top:0}
.grp .frow.head:first-child{padding-top:2px}
/* Status rows become a 4-across meter bridge so they fill the width and read
   like a rack unit, instead of label/value flung to opposite edges. */
.rows{display:grid;grid-template-columns:repeat(4,1fr);column-gap:26px;padding-top:4px}
.row{border-top:0;flex-direction:column;align-items:flex-start;gap:7px;padding:16px 0}
/* Own the wider tempo glass — scale the segmented readout up (ghost + live
   share .bignum, so the unlit-segment effect stays intact). */
.bignum{font-size:74px}
.foot{max-width:900px}
}
</style></head><body>
<div class="unit">
<span class="screw tl"></span><span class="screw tr"></span><span class="screw bl"></span><span class="screw br"></span>
<div class="brand"><span class="pwr"></span><span class="wordmark">KITCHEN&middot;<b>SYNC</b></span><span class="rev">ESP32-P4 &middot; FW %FWVER%</span></div>
<div class="scr">
<div class="scr-top"><span class="beat" id="beat"></span><span class="scr-lbl">Session Tempo</span><span class="scr-src">Ableton Link</span></div>
<div class="readout"><span class="ghost bignum">188.8</span><span class="live"><span class="bignum" id="bpm">--.-</span><span class="unit-bpm">BPM</span></span></div>
</div>
<div class="rows">
<div class="row"><label>Link Peers</label><span class="val" id="peers">0</span></div>
<div class="row"><label>USB-MIDI Device</label><span class="pill" id="usb">Waiting</span></div>
<div class="row"><label>MIDI Clock In</label><span class="val" id="min">&mdash;&mdash;.&mdash; BPM</span></div>
<div class="row"><label>Clock Out</label><span class="val" id="tx">0 pulses</span></div>
<div class="row"><label>Follow Beat</label><span class="val" id="follow">off</span></div>
</div>
<form method="POST" action="/save">
<div class="formcols">
<div class="grp"><div class="frow head"><span class="cap">WiFi Network</span>
<div class="fld"><span class="pre">SSID</span><input name="wifi_ssid" value="%SSID%" autocomplete="off"></div>
<div class="fld"><span class="pre">PASS</span><input name="wifi_pass" type="password" placeholder="keep current"></div></div></div>
<div class="grp grp--wide"><div class="frow head"><span class="cap">MIDI Clock Out</span>
<label class="sw"><input type="checkbox" class="live" name="clock_out" value="1" %MCKCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="sect %CLKSECT%" data-when="clock_out">%OUTPUTS%</div></div>
<div class="grp"><div class="frow head"><span class="cap">Metronome (Speaker)</span>
<label class="sw"><input type="checkbox" name="metronome" value="1" %MTOCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="sect %METSECT%" data-when="metronome">
<div class="frow"><span class="cap">Accent Bar 1</span>
<label class="sw"><input type="checkbox" class="live" name="metro_accent" value="1" %MTACHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="frow"><span class="cap">Metronome Sound</span>
<div class="grid2">
<div class="fld"><span class="pre">VOL</span><input type="number" class="live" name="metro_vol" value="%MVOL%" min="0" max="100" step="5"></div>
<div class="fld"><span class="pre">VOICE</span><select class="live" name="metro_voice" id="mvoice"><option value="0">Tone</option><option value="1">Click</option><option value="2">Wood</option></select></div></div></div>
</div></div>
<div class="grp"><div class="frow head"><span class="cap">LED Strip &middot; Visual Metronome</span>
<label class="sw"><input type="checkbox" class="live" name="led" value="1" %LEDCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
<div class="sect %LEDSECT%" data-when="led">%LEDCTL%</div></div>
<div class="grp"><div class="frow head"><span class="cap">Follow Beat (Mic)</span>
<label class="sw"><input type="checkbox" name="follow_beat" value="1" %FBCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
</div>
</div>
<button class="write" type="submit">Write &amp; Reboot</button>
</form>
<div class="foot">Everything and the kitchen sync &middot; FW %FWVER% &middot; %FWBUILD% &middot; <a href="/update" style="color:#4b535b">Firmware Update</a></div>
</div>
<script>
var bpmEl=document.getElementById('bpm'),beatEl=document.getElementById('beat');
var peersEl=document.getElementById('peers'),usbEl=document.getElementById('usb'),txEl=document.getElementById('tx'),minEl=document.getElementById('min');
var followEl=document.getElementById('follow');
var beatTimer=null,shownBpm=-1;
function setBeat(bpm){if(beatTimer){clearInterval(beatTimer);beatTimer=null}
if(bpm>0){beatTimer=setInterval(function(){beatEl.classList.add('on');setTimeout(function(){beatEl.classList.remove('on')},90)},60000/bpm)}}
function showBpm(bpm){if(Math.abs(bpm-shownBpm)<0.05)return;shownBpm=bpm;bpmEl.textContent=bpm>0?bpm.toFixed(1):'--.-';setBeat(bpm)}
function poll(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
if(typeof d.bpm==='number')showBpm(d.bpm);peersEl.textContent=d.peers;
usbEl.textContent=d.usb?'Connected':'Waiting';usbEl.className='pill'+(d.usb?' on':'');
minEl.textContent=(d.min>0)?d.min.toFixed(1)+' BPM':'——.— BPM';
txEl.textContent=(d.tx||0)+' pulses';
if(typeof d.follow_valid!=='undefined'){
  followEl.textContent=d.follow_valid?(d.follow_bpm.toFixed(1)+' BPM'):'listening...';
}
}).catch(function(){})}
poll();setInterval(poll,1000);
// Live controls (P4-015): POST the changed field to /live so timing/division are
// audible immediately, no reboot. Save still persists everything via /save.
function postLive(el){var n=el.name;if(!n)return;
var v=el.type==='checkbox'?(el.checked?'1':'0'):el.value;
fetch('/live',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:encodeURIComponent(n)+'='+encodeURIComponent(v)}).catch(function(){})}
var liveT=null;
Array.prototype.forEach.call(document.querySelectorAll('.live'),function(el){
var num=el.type==='number';
el.addEventListener(num?'input':'change',function(){
if(num){clearTimeout(liveT);liveT=setTimeout(function(){postLive(el)},60)}else postLive(el)})});
// Show/hide each feature's settings block when its toggle flips.
function syncSect(cb){var s=document.querySelectorAll('.sect[data-when="'+cb.name+'"]');
for(var i=0;i<s.length;i++){if(cb.checked)s[i].classList.remove('hide');else s[i].classList.add('hide')}}
['clock_out','metronome','led'].forEach(function(n){
var cb=document.querySelector('input[name="'+n+'"]');
if(cb)cb.addEventListener('change',function(){syncSect(cb)})});
// NUDGE +/- steppers: bump the phase input by its step, clamp, POST immediately.
Array.prototype.forEach.call(document.querySelectorAll('.stp'),function(b){
b.addEventListener('click',function(){
var inp=b.parentNode.querySelector('.nudgev');if(!inp)return;
var v=(parseInt(inp.value,10)||0)+(parseInt(b.getAttribute('data-step'),10)||0);
var lo=parseInt(inp.min,10),hi=parseInt(inp.max,10);
if(v<lo)v=lo;if(v>hi)v=hi;
inp.value=v;postLive(inp)})});
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
    for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
        const ClockOutputCfg* c = &s_cfg->clock[o];
        std::string N = std::to_string(o);
        s += "<div class=\"frow out\"><span class=\"cap\">Clock Out " + std::to_string(o + 1) + "</span>";
        s += "<label class=\"sw\"><input type=\"checkbox\" class=\"live\" name=\"clk" + N + "_en\" value=\"1\""
             + (c->enable ? " checked" : "")
             + "><span class=\"track\"><span class=\"knob\"></span></span><span class=\"swlbl\"></span></label>";
        s += "<div class=\"fld\"><span class=\"pre\">CABLE</span><select class=\"live\" name=\"clk" + N + "_cable\">";
        for (int p = 0; p < 4; p++)
            s += "<option value=\"" + std::to_string(p) + "\"" + (c->cable == p ? " selected" : "")
                 + ">" + PORTS[p] + "</option>";
        s += "</select></div>";
        s += "<div class=\"fld\"><span class=\"pre\">RATE</span><select class=\"live\" name=\"clk" + N + "_ppqn\">";
        for (size_t k = 0; k < sizeof(PPQN) / sizeof(PPQN[0]); k++)
            s += "<option value=\"" + std::to_string(PPQN[k]) + "\"" + (c->ppqn == PPQN[k] ? " selected" : "")
                 + ">" + PPQN_LBL[k] + "</option>";
        s += "</select></div>";
        s += std::string("<div class=\"fld nudge\"><span class=\"pre\">NUDGE</span>")
             + "<button type=\"button\" class=\"stp\" data-step=\"-5\">&minus;</button>"
             + "<input type=\"number\" class=\"live nudgev\" name=\"clk" + N + "_phase\" value=\""
             + std::to_string(c->phase_mbeats) + "\" min=\"-250\" max=\"250\" step=\"5\">"
             + "<button type=\"button\" class=\"stp\" data-step=\"5\">+</button></div>";
        s += std::string("<div class=\"fld nudge\"><span class=\"pre\">SWING</span>")
             + "<button type=\"button\" class=\"stp\" data-step=\"-5\">&minus;</button>"
             + "<input type=\"number\" class=\"live nudgev\" name=\"clk" + N + "_swing\" value=\""
             + std::to_string(c->swing_mbeats) + "\" min=\"0\" max=\"250\" step=\"5\">"
             + "<button type=\"button\" class=\"stp\" data-step=\"5\">+</button></div>";
        s += "</div>";
    }
    return s;
}

// LED-strip customization controls (P4-019): brightness, pattern, fade, colours —
// all on the /live path so they apply instantly with no reboot.
static std::string build_led()
{
    if (!s_cfg) return "";
    char col[8];
    static const char* MODES[3] = { "Chase", "Flash", "Fill" };
    std::string s = "<div class=\"frow\"><span class=\"cap\">Strip Look</span>";
    s += "<div class=\"grid2\">";
    s += "<div class=\"fld\"><span class=\"pre\">BRIGHT</span><input type=\"number\" class=\"live\" name=\"led_bright\" value=\""
         + std::to_string(s_cfg->led_brightness) + "\" min=\"0\" max=\"100\" step=\"5\"></div>";
    s += "<div class=\"fld\"><span class=\"pre\">FADE</span><input type=\"number\" class=\"live\" name=\"led_fade\" value=\""
         + std::to_string(s_cfg->led_fade) + "\" min=\"0\" max=\"100\" step=\"5\"></div>";
    s += "</div>";
    s += "<div class=\"fld\"><span class=\"pre\">MODE</span><select class=\"live\" name=\"led_mode\">";
    for (int m = 0; m < 3; m++)
        s += "<option value=\"" + std::to_string(m) + "\"" + (s_cfg->led_mode == m ? " selected" : "")
             + ">" + MODES[m] + "</option>";
    s += "</select></div>";
    s += "<div class=\"colrow\">";
    snprintf(col, sizeof(col), "#%06X", s_cfg->led_beat_color & 0xFFFFFF);
    s += std::string("<div class=\"fld color\"><span class=\"pre\">BEAT</span><input type=\"color\" class=\"live\" name=\"led_beat\" value=\"")
         + col + "\"></div>";
    snprintf(col, sizeof(col), "#%06X", s_cfg->led_accent_color & 0xFFFFFF);
    s += std::string("<div class=\"fld color\"><span class=\"pre\">ACCENT</span><input type=\"color\" class=\"live\" name=\"led_accent\" value=\"")
         + col + "\"></div>";
    s += "</div>";
    s += "</div>";
    return s;
}

static std::string build_page()
{
    std::string h(PAGE);
    subst(h, "%SSID%",    s_cfg ? std::string(s_cfg->wifi_ssid) : "");
    subst(h, "%MCKCHK%",  (s_cfg && s_cfg->clock_out_enable) ? "checked" : "");
    subst(h, "%MTOCHK%",  (s_cfg && s_cfg->metronome_enable) ? "checked" : "");
    subst(h, "%MTACHK%",  (s_cfg && s_cfg->metronome_accent) ? "checked" : "");
    subst(h, "%LEDCHK%",  (s_cfg && s_cfg->led_enable) ? "checked" : "");
    subst(h, "%LEDCTL%",  build_led());
    subst(h, "%FBCHK%",   (s_cfg && s_cfg->follow_beat_enable) ? "checked" : "");
    subst(h, "%CLKSECT%", (s_cfg && s_cfg->clock_out_enable) ? "" : "hide");
    subst(h, "%METSECT%", (s_cfg && s_cfg->metronome_enable) ? "" : "hide");
    subst(h, "%LEDSECT%", (s_cfg && s_cfg->led_enable) ? "" : "hide");
    subst(h, "%MVOL%",    std::to_string(s_cfg ? s_cfg->metronome_volume : 80));
    subst(h, "%MVOICE%",  std::to_string(s_cfg ? s_cfg->metronome_voice : 0));
    subst(h, "%OUTPUTS%", build_outputs());
    subst(h, "%FWVER%",   FW_VERSION);   // LNK-038
    subst(h, "%FWBUILD%", FW_BUILD);
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
    char buf[192];   // grew from 128 -- three more fields (P4-020)
    FollowBeatOut fb = s_cfg && s_cfg->follow_beat_enable ? follow_beat_io_status() : FollowBeatOut{};
    ks_status_json(buf, sizeof(buf),
                      (float)link_proto_bpm(), midi_clock_in_bpm(esp_timer_get_time()),
                      wifi_link_peers(), usb_midi_host_ready(), usb_midi_host_tx(),
                      FW_VERSION, fb.bpm, fb.confidence, fb.valid);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// Minimal result page in the panel palette.
// reboot=true adds a script that waits for the device to come back after the restart
// and then loads the config homepage — so a Write & Reboot returns to '/' on its own
// instead of leaving the browser stranded on /save.
static esp_err_t send_result(httpd_req_t *req, const char *title, const char *msg, bool reboot)
{
    std::string p =
        "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<body style='margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:#070809;color:#e9ece6;font-family:ui-monospace,Menlo,monospace;text-align:center'>"
        "<div style='padding:2rem'><div style='width:10px;height:10px;border-radius:50%;margin:0 auto 1.2rem;"
        "background:#b6ff36;box-shadow:0 0 14px 2px #b6ff36'></div><h2 style='font-weight:600'>";
    p += title; p += "</h2><p style='color:#717a82'>"; p += msg; p += "</p>";
    if (reboot)
        p += "<p style='color:#4b535b;font-size:12px'>returning to config&hellip;</p>"
             "<script>setTimeout(function r(){fetch('/',{cache:'no-store'})"
             ".then(function(){location.href='/'}).catch(function(){setTimeout(r,1500)})},6000)</script>";
    p += "</div></body>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, p.c_str(), p.size());
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

    // Decode + parse the POST body into a candidate config (pure, host-tested).
    KsConfig cfg;
    ks_form_resolve(body, s_cfg, &cfg);

    if (!ks_config_valid(&cfg)) return send_result(req, "Invalid Config", "Check the values and go back.", false);
    *s_cfg = cfg;
    ks_config_save(s_cfg);
    esp_err_t rc = send_result(req, "Saved — Restarting", "Reconnect to WiFi if credentials changed.", true);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return rc;
}

// POST /live (P4-015): apply the live-safe fields into the running config with no
// NVS write and no reboot, so timing (phase nudge, division, swing) is audible the
// moment a control moves. The body is a PARTIAL form, so it's applied as a patch
// (only present keys change). wifi + metronome enable/volume/voice are NOT touched
// here — they need a reconnect / codec re-init and only /save changes them.
static esp_err_t live_handler(httpd_req_t *req)
{
    char body[1024];
    int len = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = '\0';

    KsConfig cand;
    ks_form_apply(body, s_cfg, &cand);   // patch onto the current live config
    if (!ks_config_valid(&cand)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "invalid", HTTPD_RESP_USE_STRLEN);
    }

    // ARC-016: patch the live-safe fields into the running config under the mutex so
    // the clock task never reads a torn multi-field update (the clock[] array copy
    // was the widest window). ks_config_live_safe_copy owns the field set; the
    // generation bump makes the task re-prime its grids so a phase/rate change
    // realigns cleanly instead of dumping a catch-up burst.
    if (s_cfg_mutex) xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
    ks_config_live_safe_copy(s_cfg, &cand);
    if (s_gen) (*s_gen)++;
    if (s_cfg_mutex) xSemaphoreGive(s_cfg_mutex);

    // P4-029: re-render the tone bursts + re-set codec volume now, no reboot (no-op
    // if the metronome was off at boot — the codec isn't up until then).
    metronome_audio_set(s_cfg->metronome_volume, s_cfg->metronome_voice);

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

// P4-017: web-based OTA. The partition table is CONFIG_PARTITION_TABLE_TWO_OTA
// (otadata + ota_0 + ota_1, no factory slot), so a push always targets "the
// other" slot and boots into it on success — the running image is never
// touched until esp_ota_set_boot_partition commits.
static const char UPDATE_PAGE[] = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KitchenSync &middot; Firmware Update</title>
<style>
body{margin:0;min-height:100vh;background:#070809;color:#e9ece6;font-family:ui-monospace,Menlo,monospace;
display:flex;align-items:center;justify-content:center;padding:16px}
.card{width:100%;max-width:380px;border:1px solid #262b31;border-radius:14px;padding:28px 26px;background:#12151a}
h2{margin:0 0 6px;font-size:18px;letter-spacing:.04em}
p{color:#717a82;font-size:12.5px;margin:0 0 20px;letter-spacing:.03em}
input[type=file]{display:block;width:100%;margin-bottom:18px;color:#e9ece6;font-family:inherit;font-size:13px}
button{width:100%;border:0;cursor:pointer;border-radius:10px;font-weight:700;font-size:14px;
letter-spacing:.06em;text-transform:uppercase;color:#0a0d07;padding:14px;
background:linear-gradient(180deg,#d2ff63,#9be32a)}
#st{margin-top:14px;font-size:12px;color:#717a82;letter-spacing:.03em}
a{color:#b6ff36;text-decoration:none;font-size:12px}
</style></head><body>
<div class="card">
<h2>Firmware Update</h2>
<p>Running FW %FWVER% &middot; built %FWBUILD%</p>
<p>Select a compiled .bin. The device flashes the inactive OTA slot and reboots
into it automatically.</p>
<input type="file" id="fw" accept=".bin">
<button id="go">Upload &amp; Flash</button>
<div id="st"></div>
<p style="margin-top:16px"><a href="/">&larr; Back</a></p>
</div>
<script>
document.getElementById('go').addEventListener('click',function(){
var f=document.getElementById('fw').files[0],st=document.getElementById('st');
if(!f){st.textContent='Choose a .bin file first.';return}
st.textContent='Uploading '+f.size+' bytes...';
fetch('/update',{method:'POST',body:f}).then(function(r){
return r.text().then(function(t){return {ok:r.ok,t:t}})
}).then(function(res){st.textContent=res.ok?'Flashed — rebooting…':('Failed: '+res.t)})
.catch(function(e){st.textContent='Error: '+e})
});
</script></body></html>)HTML";

static esp_err_t update_page_handler(httpd_req_t *req)
{
    std::string page(UPDATE_PAGE);
    subst(page, "%FWVER%",   FW_VERSION);  /* LNK-038: show what's about to be overwritten */
    subst(page, "%FWBUILD%", FW_BUILD);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page.c_str(), page.size());
}

static esp_err_t send_plain(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

// POST /update — the request body IS the raw .bin (the page's JS does
// fetch('/update',{method:'POST',body:file}), no multipart), streamed straight
// into esp_ota_ops chunk by chunk so the whole image never sits in RAM at once.
static esp_err_t update_handler(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) return send_plain(req, "500 Internal Server Error", "no OTA partition available");

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota begin failed: %s", esp_err_to_name(err));
        return send_plain(req, "500 Internal Server Error", "ota begin failed");
    }

    char buf[4096];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(handle);
            return send_plain(req, "500 Internal Server Error", "upload interrupted");
        }
        err = esp_ota_write(handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota write failed: %s", esp_err_to_name(err));
            esp_ota_abort(handle);
            return send_plain(req, "500 Internal Server Error", "flash write failed");
        }
        remaining -= r;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota end failed: %s", esp_err_to_name(err));
        return send_plain(req, "500 Internal Server Error",
                           err == ESP_ERR_OTA_VALIDATE_FAILED ? "image validation failed" : "ota end failed");
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition failed: %s", esp_err_to_name(err));
        return send_plain(req, "500 Internal Server Error", "set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA ok, booting %s next", target->label);
    esp_err_t rc = send_plain(req, "200 OK", "OK - rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return rc;
}

void ks_web_start(KsConfig* cfg, volatile uint32_t* gen, SemaphoreHandle_t cfg_mutex)
{
    s_cfg = cfg;
    s_gen = gen;
    s_cfg_mutex = cfg_mutex;
    httpd_handle_t server = NULL;
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &hcfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    httpd_uri_t root       = { .uri = "/",       .method = HTTP_GET,  .handler = root_handler,        .user_ctx = NULL };
    httpd_uri_t status     = { .uri = "/status", .method = HTTP_GET,  .handler = status_handler,      .user_ctx = NULL };
    httpd_uri_t save       = { .uri = "/save",   .method = HTTP_POST, .handler = save_handler,        .user_ctx = NULL };
    httpd_uri_t live       = { .uri = "/live",   .method = HTTP_POST, .handler = live_handler,         .user_ctx = NULL };
    httpd_uri_t update_get = { .uri = "/update", .method = HTTP_GET,  .handler = update_page_handler,  .user_ctx = NULL };
    httpd_uri_t update_post= { .uri = "/update", .method = HTTP_POST, .handler = update_handler,       .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &live);
    httpd_register_uri_handler(server, &update_get);
    httpd_register_uri_handler(server, &update_post);
    ESP_LOGI(TAG, "web UI on :80");
}
