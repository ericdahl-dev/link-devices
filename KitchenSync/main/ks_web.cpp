/*
 * KitchenSync web UI (P4-007) — rack-panel status + config page styled like X32Link,
 * served over esp_http_server. Thin glue: live values from pure ks_status.c
 * (/status, polled 1 Hz); config model is pure ks_config.c; NVS + reboot are
 * the only side effects. The panel look and the client-side plumbing are the
 * shared, host-tested ui_chrome.c (ARC-017) -- this file owns only KitchenSync's
 * own form and the rules that extend the chrome.
 */
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "ui_chrome.h"    /* ARC-017: shared CSS/JS + result & update pages */
#include "wifi_link.h"
#include "usb_midi_host.h"
#include "fw_version.h"      /* LNK-038: shared FW_VERSION / FW_BUILD (X32Link/ include path) */
#include "ks_status.h"
#include "ks_config_json.h"   /* P4-041: GET /config.json, the read-side counterpart to /save+/live */
#include "link_measurement.h"   // P4-038: LinkPhaseHealth
#include "follow_beat_io.h"   /* P4-020: mic tempo-follow estimate for /status (Task 9 creates this header) */
#include "ks_config.h"
#include "ks_config_nvs.h"
#include "ks_form.h"
#include "ks_web.h"
#include "metronome_audio.h"   // P4-029: live vol/voice re-apply
#include "transport_intent.h"   // ESP-011: quantized launch presses
#include "ks_tick_health.h"      // P4-038: 1ms clock task probe -> /status
#include "config_persist.h"   // ARC-022: debounced write-through for /live edits

#define RESULT_PAGE_MAX 1536   /* ui_result_page worst case + slack */
#define UPDATE_PAGE_MAX 3072   /* ui_update_page worst case + slack. ESP-020 added the "updating
                                * stops the clock" warning; the fetch variant this file renders
                                * is now 2386 bytes, so 2560 left only 174 spare. */

static const char *TAG = "ks_web";
static KsConfig *s_cfg = nullptr;
static volatile uint32_t *s_gen = nullptr;   // bumped on /live so the clock task re-primes
static SemaphoreHandle_t s_cfg_mutex = nullptr;  // ARC-016: guards the /live patch
// ARC-022: a live edit is a real edit. /live marks this dirty; ks_web_config_persist_tick()
// -- called from the low-priority status task -- does the actual blob write once the
// edits settle. The policy is pure and host-tested; this is the only state it needs.
static ConfigPersist s_persist;
// ESP-011: last-seen per-output launch state, published by the clock task.
static volatile int s_launch[KS_CLOCK_OUTPUTS] = {0,0,0,0};
void ks_web_publish_launch(const int st[KS_CLOCK_OUTPUTS]) {
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) s_launch[i] = st[i];
}

// %WIFI% / %MCKCHK% / %CABLE% are filled per-request from the live config.
static const char PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>KitchenSync</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/dseg@0.46.0/css/dseg.css" rel="stylesheet">
<style>%CSS%
/* Page-specific: everything above is the shared chrome (ui_chrome_css, ARC-017).
   These rules extend or follow it, and only win by coming second. */
.rows{padding:6px 26px 4px}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:15px 0;border-top:1px solid var(--line)}
.row label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut)}
.val{font-family:var(--mono);font-size:14px;color:var(--ink);letter-spacing:.06em}
.pill{font-size:10.5px;letter-spacing:.16em;text-transform:uppercase;padding:4px 9px;border-radius:999px;border:1px solid var(--line);color:var(--mut)}
.pill.on{color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f}
form{padding:2px 26px 26px}
.frow{padding:14px 0;border-top:1px solid var(--line)}
.cap{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--mut);margin-bottom:9px;display:block}
.fld{margin-top:6px}
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
/* !important because the desktop media query's `.grp--wide .sect`
   (two classes) otherwise out-specifies this single-class rule, leaving the MIDI
   Clock Out group unable to collapse above 760px while every other group could. */
.hide{display:none!important}
.tp{font-family:var(--mono);font-size:11px;letter-spacing:.14em;padding:6px 12px;margin-left:6px;
border-radius:7px;border:1px solid var(--line);background:linear-gradient(180deg,#1a1f25,#12161b);
color:var(--ink);cursor:pointer}
.tp:active{transform:translateY(1px);border-color:#4a5a2c}
.tp.armed{border-color:var(--amber);color:var(--amber)}
.tp.running{border-color:#7fbf1f;color:var(--led)}
.tp:disabled{opacity:.4;cursor:not-allowed;border-color:var(--line);color:var(--mut)}
/* ESP-011: per-output transport toggle — one control, state is the label/colour
   (the KitchenSync Touch device's full-screen toggle, shrunk to a row). */
.tgl{flex:1;font-family:var(--disp);font-weight:800;font-size:13px;letter-spacing:.14em;
padding:11px 12px;border-radius:9px;cursor:pointer;border:1px solid var(--line);
background:linear-gradient(180deg,#2a1512,#1c0f0d);color:#ff7a6b;transition:background .15s,color .15s,border-color .15s}
.tgl.arming{border-color:var(--amber);color:var(--amber);background:linear-gradient(180deg,#2c2113,#1d160d)}
.tgl.playing{border-color:#7fbf1f;color:#0a0d07;background:linear-gradient(180deg,#caff5a,#9be32a)}
.tgl:active{transform:translateY(1px)}
.tgl:disabled{opacity:.45;cursor:not-allowed}
/* ESP-019: keyboard transport. The shortcut is printed on the control it fires —
   a key nobody can see is not a feature. */
.kbd{flex:none;font-family:var(--mono);font-size:10px;letter-spacing:.08em;color:var(--mut);
border:1px solid var(--line);border-radius:5px;padding:2px 6px;margin-left:8px;background:#12161b}
/* A key aimed at a Link-owned output must not silently do nothing: flash the greyed
   toggle and pulse the note that names the owner, so the user learns why. */
@keyframes nak{0%,100%{border-color:var(--line);color:var(--mut)}30%,65%{border-color:var(--amber);color:var(--amber)}}
@keyframes nakrow{0%,100%{background:transparent}30%{background:rgba(224,168,58,.14)}}
.tgl.nak:disabled{animation:nak .6s ease-out}
#townerrow.nak{animation:nakrow 1.2s ease-out;border-radius:7px}
.folsw{margin-top:10px}
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
.write{margin-top:22px}
.write:active{transform:translateY(4px);box-shadow:0 1px 0 #5e8a16}
.foot{margin:8px 0 20px}
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
<div class="row"><label>Transport</label><span class="val"><span class="pill" id="tstate">Stopped</span><button type="button" class="tp" data-out="all" data-play="1">PLAY</button><button type="button" class="tp" data-out="all" data-play="0">STOP</button><span class="kbd">space</span></span></div>
<div class="row" id="townerrow" style="display:none;padding-top:0;border-top:0"><label></label><span class="val" id="towner" style="color:var(--amber);font-size:11px;letter-spacing:.12em">Link owns transport for outputs set to follow it</span></div>
</div>
<form method="POST" action="/save">
<div class="formcols">
<div class="grp"><div class="frow head"><span class="cap">WiFi Networks</span>
%WIFI%</div></div>
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
<div class="fld"><span class="pre">VOICE</span><select class="live" name="metro_voice" id="mvoice">%MVOICE%</select></div></div></div>
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
<script>%JS%
// Page-specific. ui_chrome_js (ARC-017) already owns setBeat / showBpm / postLive
// and the poll() loop; poll() hands each /status here.
var peersEl=document.getElementById('peers'),usbEl=document.getElementById('usb'),txEl=document.getElementById('tx'),minEl=document.getElementById('min');
var followEl=document.getElementById('follow');
var tstateEl=document.getElementById('tstate'),townerRow=document.getElementById('townerrow');
var lastStatus=null;   // ESP-019: `space` flips whichever state the last /status showed
// Overall transport state for the header pill. When a Link peer owns transport the
// manual launch[] is frozen, so reflect the session's real play flag instead.
function transportState(d){
if(d.link_owns)return d.playing?'PLAYING':'STOPPED';
var a=d.launch||[],run=false,arm=false;
for(var i=0;i<a.length;i++){if(a[i]===2)run=true;else if(a[i]===1)arm=true;}
return run?'PLAYING':(arm?'ARMING':'STOPPED');}
function onStatus(d){
lastStatus=d;
peersEl.textContent=d.peers;
usbEl.textContent=d.usb?'Connected':'Waiting';usbEl.className='pill'+(d.usb?' on':'');
minEl.textContent=(d.min>0)?d.min.toFixed(1)+' BPM':'——.— BPM';
txEl.textContent=(d.tx||0)+' pulses';showLaunch(d.launch,d);
// Transport pill + arbitration. The ALL buttons stay live if ANY output is manual --
// outputs that follow Link simply ignore the intent, so "all" is still useful.
var ts=transportState(d);
tstateEl.textContent=ts.charAt(0)+ts.slice(1).toLowerCase();
tstateEl.className='pill'+(ts==='PLAYING'?' on':'');
var anyManual=false;
Array.prototype.forEach.call(document.querySelectorAll('.tgl'),function(b){
if(!outFollows(+b.dataset.out))anyManual=true});
var allOwned=!!d.link_owns&&!anyManual;
Array.prototype.forEach.call(document.querySelectorAll('.tp'),function(b){b.disabled=allOwned});
townerRow.style.display=d.link_owns?'flex':'none';
if(typeof d.follow_enabled!=='undefined'){
  followEl.textContent=!d.follow_enabled?'off':(d.follow_valid?(d.follow_bpm.toFixed(1)+' BPM'):'listening...');
}
}
poll();setInterval(poll,1000);
// ESP-011: transport presses. Start is quantized on the device (fires on the
// next bar line); stop is immediate. The button only posts the intent.
Array.prototype.forEach.call(document.querySelectorAll('.tp'),function(b){
b.addEventListener('click',function(){
fetch('/transport?out='+b.dataset.out+'&play='+b.dataset.play,{method:'POST'}).catch(function(){})})});
// Per-output transport (ESP-011). Each output has ONE master: Link when its
// "follow Link" switch is on and the session publishes transport, otherwise you.
// A Link-owned output shows the SESSION's play state (its manual launch state is
// frozen and would lie) and its toggle greys out.
function outFollows(o){var cb=document.querySelector('input[name="clk'+o+'_follow"]');return !!(cb&&cb.checked)}
function showLaunch(a,d){
Array.prototype.forEach.call(document.querySelectorAll('.tgl'),function(b){
var o=+b.dataset.out, owned=!!(d&&d.link_owns)&&outFollows(o);
var st=owned?((d&&d.playing)?2:0):((a&&a[o])|0);
b.textContent=st===2?'PLAYING':(st===1?'ARMING':'STOPPED');
b.classList.toggle('playing',st===2);b.classList.toggle('arming',st===1);
b.disabled=owned;b.dataset.state=st})}
// Click = flip, exactly like the Touch device: stopped -> play (bar-quantized),
// arming/playing -> stop (immediate).
Array.prototype.forEach.call(document.querySelectorAll('.tgl'),function(b){
b.addEventListener('click',function(){
var play=((+(b.dataset.state||0))===0)?1:0;
fetch('/transport?out='+b.dataset.out+'&play='+play,{method:'POST'}).catch(function(){})})});
// ESP-019: keyboard transport. `1`..`4` toggle Clock Out 1..4, `space` toggles all.
// The key never posts anything itself — it clicks the button the mouse would, so the
// key and the click are one path that cannot drift into two.
//
// The guard that decides whether this feature is correct: a number typed into a text
// field must NEVER fire a transport. A WiFi passphrase containing a `1` would
// otherwise launch a drum machine mid-set.
function typingInto(t){if(!t)return false;
var n=(t.tagName||'').toLowerCase();
return n==='input'||n==='select'||n==='textarea'||!!t.isContentEditable}
// An output is keyable only if the clock-out feature is on AND that output is enabled.
function outEnabled(o){var m=document.querySelector('input[name="clock_out"]');
if(m&&!m.checked)return false;
var cb=document.querySelector('input[name="clk'+o+'_en"]');return !!(cb&&cb.checked)}
// A key on a Link-owned output must not act — but it must not fail *silently* either,
// or the user concludes the feature is broken. Flash the greyed toggle and pulse the
// note that already says who owns the transport.
function nak(b){
if(b){b.classList.remove('nak');void b.offsetWidth;b.classList.add('nak');
setTimeout(function(){b.classList.remove('nak')},600)}
townerRow.classList.remove('nak');void townerRow.offsetWidth;townerRow.classList.add('nak');
setTimeout(function(){townerRow.classList.remove('nak')},1200)}
document.addEventListener('keydown',function(e){
if(typingInto(e.target))return;
if(e.ctrlKey||e.metaKey||e.altKey||e.repeat)return;
var b=null;
if(e.key===' '||e.key==='Spacebar'){
// The "all" transport is a PLAY/STOP pair, not a toggle: press whichever one flips
// the state the page is currently showing.
var st=lastStatus?transportState(lastStatus):'STOPPED';
b=document.querySelector('.tp[data-out="all"][data-play="'+(st==='STOPPED'?'1':'0')+'"]')}
else if(e.key>='1'&&e.key<='4'){
var o=+e.key-1;                       // Clock Out 1..4 are data-out 0..3
if(!outEnabled(o))return;             // disabled output: nothing to start
b=document.querySelector('.tgl[data-out="'+o+'"]')}
else return;
e.preventDefault();   // space must not scroll the page or re-press a focused button
if(!b)return;
if(b.disabled){nak(b.classList.contains('tgl')?b:null);return}   // Link owns this one
b.click()});
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

// Escape a string for use inside a double-quoted HTML attribute. An SSID is
// user-controlled text: a bare '"' in it would otherwise close the value= and let
// the rest of the name inject markup into the page.
static std::string attr_esc(const char *s)
{
    std::string o;
    for (const char *p = s; *p; ++p) {
        switch (*p) {
            case '&':  o += "&amp;";  break;
            case '"':  o += "&quot;"; break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            default:   o += *p;       break;
        }
    }
    return o;
}

// ESP-013: one SSID/PASS pair per saved network. Slot 0 keeps the unsuffixed field
// names; the rest are wifi_ssid1.. / wifi_pass1.. Clearing an SSID forgets that slot.
static std::string build_wifi()
{
    if (!s_cfg) return "";
    std::string o;
    for (int i = 0; i < KS_WIFI_SLOTS; i++) {
        std::string sfx = (i == 0) ? "" : std::to_string(i);
        o += "<div class=\"fld\"><span class=\"pre\">SSID</span><input name=\"wifi_ssid" + sfx +
             "\" value=\"" + attr_esc(s_cfg->wifi[i].ssid) + "\" autocomplete=\"off\" placeholder=\"" +
             (i == 0 ? "required" : "optional") + "\"></div>"
             "<div class=\"fld\"><span class=\"pre\">PASS</span><input name=\"wifi_pass" + sfx +
             "\" type=\"password\" placeholder=\"" +
             (s_cfg->wifi[i].ssid[0] ? "keep current" : "") + "\"></div>";
    }
    return o;
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
        // ESP-011: one Touch-style toggle per output (stopped/arming/playing), plus
        // the per-output transport master. Follow Link on => this output tracks the
        // session and its toggle greys out; off => the toggle is yours.
        // ESP-019: the key that fires this toggle is printed on it. Clock Out 1..4 are
        // data-out 0..3, so the badge is the 1-based label the user already reads.
        s += std::string("<div class=\"fld\"><span class=\"pre\">RUN</span>")
             + "<button type=\"button\" class=\"tgl\" data-out=\"" + N + "\">STOPPED</button>"
             + "<span class=\"kbd\">" + std::to_string(o + 1) + "</span></div>";
        s += std::string("<label class=\"sw folsw\"><input type=\"checkbox\" class=\"live\" name=\"clk") + N
             + "_follow\" value=\"1\"" + (c->follow_link ? " checked" : "")
             + "><span class=\"track\"><span class=\"knob\"></span></span>"
             + "<span class=\"swlbl\"></span></label>"
             + "<div class=\"cap\" style=\"margin:4px 0 0;color:var(--mut)\">follow Link transport</div>";
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

// The metronome VOICE options, with the live one marked `selected` (P4-029).
//
// This used to be static HTML in PAGE with no `selected` at all, so the dropdown ALWAYS
// rendered "Tone" no matter what was saved -- and %MVOICE% was substituted into a token
// that appeared nowhere, silently doing nothing.
//
// It was not merely cosmetic: NVS held the right voice (and the metronome SOUNDED right),
// but the form showed Tone. So the next Save for any unrelated reason -- a WiFi edit --
// POSTed metro_voice=0 and wiped the saved voice back to Tone. A read-back bug that eats
// data on the next write.
//
// Generated like every other select in this file (build_outputs, build_led) rather than
// hand-written, which is exactly why those never had the bug.
static std::string build_voice()
{
    static const char* VOICES[3] = { "Tone", "Click", "Wood" };
    std::string s;
    for (int v = 0; v < 3; v++)
        s += "<option value=\"" + std::to_string(v) + "\""
             + ((s_cfg && s_cfg->metronome_voice == v) ? " selected" : "")
             + ">" + VOICES[v] + "</option>";
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

// Only the slice between %CSS% and %JS% needs per-request values. The chrome on
// either side goes out straight from static storage, never copied.
static std::string build_body(const char* begin, const char* end)
{
    std::string h(begin, end);
    subst(h, "%WIFI%",    build_wifi());
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
    subst(h, "%MVOICE%",  build_voice());
    subst(h, "%OUTPUTS%", build_outputs());
    subst(h, "%FWVER%",   FW_VERSION);   // LNK-038
    subst(h, "%FWBUILD%", FW_BUILD);
    return h;
}

// ARC-017: assembled from parts and sent chunked. The shared CSS and JS are ~4KB
// of static rodata that used to be copied into the page string on every request.
static esp_err_t root_handler(httpd_req_t *req)
{
    static const char CSS_MARK[] = "%CSS%", JS_MARK[] = "%JS%";
    const char *css = strstr(PAGE, CSS_MARK);
    const char *js  = strstr(PAGE, JS_MARK);
    if (!css || !js) return ESP_FAIL;   // template lost its markers

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE, css - PAGE);
    httpd_resp_send_chunk(req, ui_chrome_css(), HTTPD_RESP_USE_STRLEN);

    std::string body = build_body(css + strlen(CSS_MARK), js);
    httpd_resp_send_chunk(req, body.c_str(), body.size());

    httpd_resp_send_chunk(req, ui_chrome_js(), HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, js + strlen(JS_MARK), HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);   // terminate
}

static esp_err_t status_handler(httpd_req_t *req)
{
    // P4-038 grew this by the tick-health block (~90 bytes worst case); 448 keeps the
    // same headroom test_ks_status.c asserts against.
    char buf[576];
    /* P4-038: the origin-step gauge. `max_step_us` is how far a GhostXForm commit has thrown
     * the beat origin -- the number that, unread, cost 138 seconds of silent DIN clock in
     * ESP-027. It survives a peer churn now (ESP-028 split invalidate from reset), so this
     * is a lifetime record and not a fresh page every time a laptop closes. */
    LinkPhaseHealth phase;
    link_measurement_phase_health(&phase);
    bool fb_enabled = s_cfg && s_cfg->follow_beat_enable;
    FollowBeatOut fb = fb_enabled ? follow_beat_io_status() : FollowBeatOut{};
    int ls[KS_CLOCK_OUTPUTS];
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) ls[i] = s_launch[i];
    WebTickHealth tick;                                  // P4-038: the 1 ms clock task's health
    bool have_tick = ks_tick_health(&tick);
    /* ESP-029: ks_status_json is now the SHARED builder (X32Link/ks_status.c), emitted by
     * this firmware AND by KitchenSyncTouch, so the two can no longer drift apart.
     *
     * KS_CLOCK_OUTPUTS is passed as the launch COUNT — the array length IS the device's
     * output count. A Touch passes 1. Never padded.
     *
     * clk/pulses are nullptr: this firmware does not yet publish ESP-028's writer's-truth
     * fields (the Touch does). NULL OMITS them rather than faking a `clk:"locked"` over a
     * writer we have not measured — which is exactly the lie ESP-028 exists to prevent.
     * Wiring the P4's writer state in is follow-up; this device's wire shape is unchanged. */
    ks_status_json(buf, sizeof(buf),
                      ks_clock_effective_bpm(), midi_clock_in_bpm(esp_timer_get_time()),  /* ESP-037: effective, not link_proto_bpm (zeroes solo) */
                      wifi_link_peers(), usb_midi_host_ready(), usb_midi_host_tx(),
                      FW_VERSION, fb_enabled, fb.bpm, fb.confidence, fb.valid,
                      ls, KS_CLOCK_OUTPUTS,
                      link_proto_playing(), link_proto_start_stop_seen(),
                      have_tick ? &tick : nullptr, &phase,
                      nullptr, nullptr, nullptr);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// P4-041: GET /config.json — the read-side counterpart to POST /save and POST
// /live, for a client (the iOS companion app) that needs the device's ACTUAL
// current settings instead of scraping the rendered HTML form. Snapshot-under-
// mutex-then-format-outside-it, the same principle
// ks_web_config_persist_tick() uses: the clock task takes s_cfg_mutex every
// tick, so holding it across the JSON formatting (slower, and followed by a
// network write) would stall the clock for no reason. Unlike that function,
// `snap` is a plain stack local, not a file static — this handler runs on the
// httpd task (~4 KB stack, same context status_handler's 576-byte local buffer
// already lives on above), not the tighter 3 KB status task, and a static here
// would race two concurrently-handled requests.
static esp_err_t config_json_handler(httpd_req_t *req)
{
    if (!s_cfg) return ESP_FAIL;
    KsConfig snap;
    if (s_cfg_mutex) xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
    snap = *s_cfg;
    if (s_cfg_mutex) xSemaphoreGive(s_cfg_mutex);

    // Worst case (every wifi SSID + every clock field maxed) measures 692 bytes
    // (test_fits_in_a_generously_sized_stack_buffer); 768 keeps the same kind of
    // headroom test_ks_status.c's buffer asserts against (P4-038's precedent).
    char buf[768];
    /* ESP-030: the P4 is fully fitted — speaker (P4-006), LED strip (P4-018), mic
     * (P4-020), four clock outputs. A board without them simply does not emit those
     * keys; nothing here fakes hardware. */
    static const KsCaps caps = { .metronome = true, .led = true, .follow_beat = true,
                                 .outputs = KS_CLOCK_OUTPUTS,
                                 .wifi_slots = KS_WIFI_SLOTS,     /* ESP-035 */
                                 .settable_tempo = true };        /* ESP-037: clock box */
    ks_config_json(buf, sizeof(buf), &snap, &caps);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ARC-017: the page itself is ui_result_page() — pure, shared with X32Link, tested.
// reboot=true makes it poll until the device answers again and then return to '/',
// so Write & Reboot doesn't strand the browser on /save.
static esp_err_t send_result(httpd_req_t *req, const char *title, const char *msg, bool reboot)
{
    /* Heap, not stack: the httpd task has ~4KB and this page is over 1KB. */
    char *p = (char *)malloc(RESULT_PAGE_MAX);
    if (!p) return ESP_ERR_NO_MEM;
    int n = ui_result_page(p, RESULT_PAGE_MAX, title, msg, reboot);
    if (n < 0 || n >= RESULT_PAGE_MAX) { free(p); return ESP_FAIL; }   // truncated
    httpd_resp_set_type(req, "text/html");
    esp_err_t e = httpd_resp_send(req, p, n);
    free(p);
    return e;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    /* ESP-013: 3 wifi slots (32-char ssid + 63-char pass, each up to 3x longer
     * url-encoded) plus 4 outputs x 5 fields plus the led/metronome fields. The old
     * 1024-byte buffer no longer covers the worst case.
     *
     * On the HEAP, not the stack: the httpd task gets ~4KB total, so a buffer this
     * size as a local overflows it and panics with a stack-protection fault the
     * instant anyone saves. */
    const size_t BODY_MAX = 3072;
    /* A truncated body used to be parsed anyway: the tail fields simply vanished and
     * a half-applied config was saved as if the user had asked for it. Refuse instead. */
    if (req->content_len >= BODY_MAX)
        return send_result(req, "Config Too Large", "Too many characters in the form. Shorten a field and retry.", false);

    char *body = (char *)malloc(req->content_len + 1);
    if (!body) return send_result(req, "Out of Memory", "Try again.", false);

    int len = (int)req->content_len;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) { free(body); return ESP_FAIL; }
        got += r;
    }
    body[got] = '\0';

    // Decode + parse the POST body into a candidate config (pure, host-tested).
    KsConfig cfg;
    ks_form_resolve(body, s_cfg, &cfg);
    free(body);

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
// ESP-011: POST /transport?out=N&play=1|0  (out=all for every enabled output).
// A press, not a level -- the clock task consumes it exactly once, and the pure
// transport_launch decides WHEN it becomes MIDI Start (next bar line) vs Stop
// (immediately).
static esp_err_t transport_handler(httpd_req_t *req)
{
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "need ?out=N|all&play=1|0", HTTPD_RESP_USE_STRLEN);
    }
    char out_s[8] = {0}, play_s[4] = {0};
    httpd_query_key_value(q, "out",  out_s,  sizeof(out_s));
    httpd_query_key_value(q, "play", play_s, sizeof(play_s));

    int out = (strcmp(out_s, "all") == 0) ? -1 : atoi(out_s);
    if (out >= KS_CLOCK_OUTPUTS) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "bad out", HTTPD_RESP_USE_STRLEN);
    }
    transport_intent_post(out, play_s[0] == '1' ? TL_INTENT_PLAY : TL_INTENT_STOP);

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

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

    // ARC-022: mark it, don't write it. NVS is one blob and a slider drag lands
    // dozens of these, so writing here would blob-write flash tens of times per
    // gesture. The write happens once the edits settle, on the status task.
    config_persist_mark(&s_persist, (uint32_t)(esp_timer_get_time() / 1000));

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

// ARC-022: poll from a LOW-PRIORITY task, never from the clock writer. A flash
// write suspends the cache and freezes both cores regardless of priority, so this
// cannot be made safe by ranking it -- it can only be kept off the RT path and made
// rare. Debounce is what makes it rare.
//
// The snapshot is a file static, not a local: KsConfig is a few hundred bytes and
// the status task's stack is 3 KB.
void ks_web_config_persist_tick(void)
{
    if (!s_cfg) return;

    /* ESP-037: while a Link session is DRIVING, remember its tempo as ours -- the same
     * behaviour the Touch has. Link teaches the box the tempo; when Link goes away the
     * box keeps playing it (the arbiter's free-run seed, applied by the clock task) AND
     * persists it, so a power-cycle comes back at the last tempo it actually played.
     *
     * Only while peers>0: solo, tempo_mbpm is the user's own set value and the clock task
     * applies it, so mirroring then would fight it. Written under the config mutex; the
     * clock task picks up the change on its next tick without a re-prime (no g_cfg_gen
     * bump -- a tempo change is not a grid realign). Marked (debounced), not written. */
    if (wifi_link_peers() > 0) {
        int eff = (int)((float)link_proto_bpm() * 1000.0f + 0.5f);
        if (eff >= 20000 && eff <= 300000 && eff != s_cfg->tempo_mbpm) {
            if (s_cfg_mutex) xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
            s_cfg->tempo_mbpm = eff;
            if (s_cfg_mutex) xSemaphoreGive(s_cfg_mutex);
            config_persist_mark(&s_persist, (uint32_t)(esp_timer_get_time() / 1000));
        }
    }

    if (!config_persist_due(&s_persist, (uint32_t)(esp_timer_get_time() / 1000))) return;

    // Copy under the mutex, write outside it: the clock task takes this mutex every
    // tick, and holding it across a flash write would stall the clock for the whole
    // write on top of the unavoidable cache freeze.
    static KsConfig snap;
    if (s_cfg_mutex) xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
    snap = *s_cfg;
    if (s_cfg_mutex) xSemaphoreGive(s_cfg_mutex);

    esp_err_t e = ks_config_save(&snap);
    if (e != ESP_OK) ESP_LOGW(TAG, "live config save failed: %s", esp_err_to_name(e));
}

// P4-017: web-based OTA. The partition table is CONFIG_PARTITION_TABLE_TWO_OTA
// (otadata + ota_0 + ota_1, no factory slot), so a push always targets "the
// other" slot and boots into it on success — the running image is never
// touched until esp_ota_set_boot_partition commits.
/* ARC-017: the /update page is ui_update_page() — shared with X32Link.
 * multipart=false selects the fetch(body:file) uploader that esp_ota_ops wants. */

static esp_err_t update_page_handler(httpd_req_t *req)
{
    char *page = (char *)malloc(UPDATE_PAGE_MAX);   /* not the 4KB httpd stack */
    if (!page) return ESP_ERR_NO_MEM;
    /* LNK-038: show what's about to be overwritten. */
    int n = ui_update_page(page, UPDATE_PAGE_MAX, "KitchenSync", FW_VERSION, FW_BUILD, false);
    if (n < 0 || n >= UPDATE_PAGE_MAX) { free(page); return ESP_FAIL; }
    httpd_resp_set_type(req, "text/html");
    esp_err_t e = httpd_resp_send(req, page, n);
    free(page);
    return e;
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
    config_persist_reset(&s_persist);   // ARC-022: clean at boot — never write on the way up
    httpd_handle_t server = NULL;
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    // P4-037: purge the least-recently-used connection when httpd's pool is full, rather
    // than refusing the new one. Defence in depth beside the real fix (LWIP_MAX_SOCKETS=16
    // in sdkconfig.defaults): the pool is 7 with 3 reserved internally, so 4 client slots,
    // and a desktop browser opens up to 6 keep-alive connections to one host.
    hcfg.lru_purge_enable = true;
    if (httpd_start(&server, &hcfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    httpd_uri_t root       = { .uri = "/",       .method = HTTP_GET,  .handler = root_handler,        .user_ctx = NULL };
    httpd_uri_t status     = { .uri = "/status", .method = HTTP_GET,  .handler = status_handler,      .user_ctx = NULL };
    httpd_uri_t config_json= { .uri = "/config.json", .method = HTTP_GET, .handler = config_json_handler, .user_ctx = NULL };
    httpd_uri_t save       = { .uri = "/save",   .method = HTTP_POST, .handler = save_handler,        .user_ctx = NULL };
    httpd_uri_t live       = { .uri = "/live",   .method = HTTP_POST, .handler = live_handler,         .user_ctx = NULL };
    httpd_uri_t transport  = { .uri = "/transport", .method = HTTP_POST, .handler = transport_handler, .user_ctx = NULL };
    httpd_uri_t update_get = { .uri = "/update", .method = HTTP_GET,  .handler = update_page_handler,  .user_ctx = NULL };
    httpd_uri_t update_post= { .uri = "/update", .method = HTTP_POST, .handler = update_handler,       .user_ctx = NULL };
    httpd_register_uri_handler(server, &transport);
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &config_json);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &live);
    httpd_register_uri_handler(server, &update_get);
    httpd_register_uri_handler(server, &update_post);
    ESP_LOGI(TAG, "web UI on :80");
}
