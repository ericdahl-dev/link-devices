// Shared web chrome for both firmwares' config UIs — see ui_chrome.h (ARC-017).
#include "ui_chrome.h"
#include <stdio.h>

/* The rack-panel look. Every rule here was byte-identical in
 * X32Link/web_config.cpp and KitchenSync/main/ks_web.cpp, except five that had
 * drifted and are resolved below. Nothing framework-specific.
 *
 * Drift resolved (nobody had decided either way; both were accidents):
 *   --mut                       #717a82 (S3) vs #838d95 (P4) -> the P4's. It is
 *                               the later value and lighter; the S3's fails a
 *                               contrast check against --panel-2.
 *   .sw .swlbl::after           "Off" vs " Off" -> no leading space. The gap is
 *                               already supplied by .sw{gap:13px}; the space was
 *                               a stray keystroke that shifted the P4's label.
 *   .sw .track/.knob transition explicit props (S3) vs blanket `.2s` (P4) -> the
 *                               S3's. `transition:.2s` animates every property,
 *                               including the knob's `left` AND its background,
 *                               which is what we want, but also any future one.
 *   .sw input:checked + .track  the S3's glow (box-shadow) -> kept. The P4 had
 *                               silently lost it.
 *   .unit                       the S3's `animation:rise` is an entry animation
 *                               it alone has -> pushed back to its own block.
 */
static const char CSS[] =
":root{--bg:#070809;--panel-2:#0f1216;--ink:#e9ece6;--mut:#838d95;--line:#262b31;"
"--led:#b6ff36;--led-dim:#36431a;--amber:#ff9d3b;"
"--mono:'DM Mono',ui-monospace,Menlo,monospace;--disp:'Bricolage Grotesque','Arial Narrow',sans-serif;--seg:'DSEG7 Classic','DM Mono',monospace;}"
"*{box-sizing:border-box}html,body{margin:0}"
"body{min-height:100vh;background:var(--bg);color:var(--ink);font-family:var(--mono);font-size:14px;"
"display:flex;align-items:flex-start;justify-content:center;padding:34px 16px 80px;"
"background-image:radial-gradient(900px 500px at 50% -10%,#14181d 0%,transparent 60%),radial-gradient(700px 500px at 90% 110%,#0e1318 0%,transparent 55%);}"
".unit{width:100%;max-width:430px;border:1px solid var(--line);border-radius:16px;position:relative;overflow:hidden;"
"background:repeating-linear-gradient(180deg,rgba(255,255,255,.012) 0 2px,transparent 2px 4px),linear-gradient(180deg,#191d22,#101317);"
"box-shadow:0 1px 0 rgba(255,255,255,.04),0 22px 60px -20px #000;}"
".unit::before{content:\"\";position:absolute;inset:0 0 auto 0;height:120px;background:linear-gradient(180deg,rgba(255,255,255,.05),transparent);pointer-events:none}"
".screw{position:absolute;width:11px;height:11px;border-radius:50%;background:radial-gradient(circle at 35% 30%,#3a4047,#0d0f12 70%);box-shadow:inset 0 1px 1px rgba(255,255,255,.18)}"
".screw::after{content:\"\";position:absolute;inset:3px;border-top:1px solid #05070a;transform:rotate(28deg)}"
".tl{top:12px;left:12px}.tr{top:12px;right:12px}.bl{bottom:12px;left:12px}.br{bottom:12px;right:12px}"
".brand{display:flex;align-items:center;gap:11px;padding:18px 26px 14px;border-bottom:1px solid var(--line)}"
".pwr{width:9px;height:9px;border-radius:50%;background:var(--led);box-shadow:0 0 10px 1px var(--led);animation:breathe 3.4s ease-in-out infinite}"
".wordmark{font-family:var(--disp);font-weight:800;font-size:20px;letter-spacing:.06em;text-transform:uppercase}"
".wordmark b{color:var(--led)}"
".rev{margin-left:auto;font-size:10.5px;color:var(--mut);letter-spacing:.18em;text-transform:uppercase}"
".scr{margin:18px;padding:18px 20px 16px;border-radius:11px;position:relative;border:1px solid #1f261b;"
"background:radial-gradient(120% 120% at 50% -20%,rgba(182,255,54,.08),transparent 60%),linear-gradient(180deg,#0a0d0a,#070907);"
"box-shadow:inset 0 0 32px rgba(0,0,0,.9)}"
".scr-top{display:flex;align-items:center;gap:8px;margin-bottom:6px}"
".beat{width:8px;height:8px;border-radius:2px;background:var(--led-dim)}"
".beat.on{background:var(--led);box-shadow:0 0 9px 1px var(--led)}"
".scr-lbl{font-size:10.5px;letter-spacing:.22em;color:#6f8a4d;text-transform:uppercase}"
".scr-src{margin-left:auto;font-size:10.5px;letter-spacing:.2em;color:var(--amber);text-transform:uppercase}"
".readout{position:relative;font-family:var(--seg);line-height:1;padding:4px 0 2px}"
".readout .ghost{position:absolute;inset:4px 0 2px;color:#1a2113}"
".readout .live{position:relative;color:var(--led);text-shadow:0 0 14px rgba(182,255,54,.45)}"
".bignum{font-size:58px}"
".unit-bpm{font-family:var(--mono);font-size:13px;color:#6f8a4d;letter-spacing:.2em;margin-left:6px}"
".fld{display:flex;align-items:center;background:var(--panel-2);border:1px solid var(--line);border-radius:9px;padding:0 12px}"
".fld:focus-within{border-color:#4a5a2c;box-shadow:0 0 0 3px rgba(182,255,54,.08)}"
".fld .pre{color:#4b535b;font-size:12px;letter-spacing:.1em;padding-right:9px;border-right:1px solid var(--line);margin-right:11px}"
".sw{display:flex;align-items:center;gap:13px;cursor:pointer;user-select:none}"
".sw input{position:absolute;opacity:0;width:0;height:0}"
".sw .track{position:relative;flex:none;width:52px;height:28px;border-radius:999px;background:var(--panel-2);border:1px solid var(--line);transition:background .2s,border-color .2s,box-shadow .2s}"
".sw .knob{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#5b636b;box-shadow:0 1px 2px rgba(0,0,0,.5);transition:left .2s,background .2s}"
".sw input:checked + .track{background:linear-gradient(180deg,#caff5a,#9be32a);border-color:#7fbf1f;box-shadow:0 0 14px -3px var(--led)}"
".sw input:checked + .track .knob{left:28px;background:#0a0d07}"
".sw .swlbl{font-family:var(--mono);font-size:12.5px;letter-spacing:.14em;text-transform:uppercase;color:var(--mut)}"
".sw .swlbl::after{content:\"Off\"}"
".sw input:checked ~ .swlbl{color:var(--led)}"
".sw input:checked ~ .swlbl::after{content:\"On\"}"
".write{width:100%;border:0;cursor:pointer;border-radius:11px;font-family:var(--disp);font-weight:800;font-size:16px;letter-spacing:.14em;text-transform:uppercase;color:#0a0d07;padding:17px;"
"background:linear-gradient(180deg,#d2ff63,#9be32a);box-shadow:0 6px 0 #5e8a16,0 16px 30px -12px rgba(182,255,54,.5)}"
".foot{text-align:center;color:#3c444c;font-size:10.5px;letter-spacing:.18em;text-transform:uppercase}"
"@keyframes breathe{0%,100%{opacity:1}50%{opacity:.45}}"
"@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}";

const char* ui_chrome_css(void) { return CSS; }

/* The client-side plumbing both pages share. The divergence — X32Link phase-locks
 * the beat dot off {phase,valid,quantum} and interpolates with rAF (LNK-022),
 * KitchenSync free-runs off bpm and also paints peers/usb/min/tx — lives in each
 * page's own onStatus(d). Hence a hook, not a `product` switch. */
static const char JS[] =
"var bpmEl=document.getElementById('bpm'),beatEl=document.getElementById('beat');"
"var beatTimer=null,shownBpm=-1,chromePhaseLocked=false;"
"function flashBeat(){beatEl.classList.add('on');setTimeout(function(){beatEl.classList.remove('on')},90)}"
/* Free-running fallback blink. A page that drives the dot itself sets
 * chromePhaseLocked and this stays parked. */
"function setBeat(bpm){if(beatTimer){clearInterval(beatTimer);beatTimer=null}"
"if(bpm>0){beatTimer=setInterval(flashBeat,60000/bpm)}}"
"function showBpm(bpm){if(Math.abs(bpm-shownBpm)<0.05)return;shownBpm=bpm;"
"bpmEl.textContent=bpm>0?bpm.toFixed(1):'--.-';if(!chromePhaseLocked)setBeat(bpm)}"
/* Live preview: POST one changed field to /live, no reboot (LNK-037 / P4-015). */
"function postLive(el){var n=el.name;if(!n)return;"
"var v=el.type==='checkbox'?(el.checked?'1':'0'):el.value;"
"fetch('/live',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"body:encodeURIComponent(n)+'='+encodeURIComponent(v)}).catch(function(){})}"
/* /status is 1Hz on purpose — too coarse to drive a smooth dot, fine to correct it. */
"function poll(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json()})"
".then(function(d){if(typeof d.bpm==='number')showBpm(d.bpm);"
"if(typeof onStatus==='function')onStatus(d)}).catch(function(){})}";

const char* ui_chrome_js(void) { return JS; }

int ui_result_page(char* buf, size_t len,
                   const char* title, const char* body, bool reboot)
{
    /* The reboot poller: wait for the device to answer again, then go home, so
     * Write & Reboot / OTA do not strand the browser on this page. */
    static const char REBOOT_JS[] =
        "<p style='color:#4b535b;font-size:12px'>returning to config&hellip;</p>"
        "<script>setTimeout(function r(){fetch('/',{cache:'no-store'})"
        ".then(function(){location.href='/'}).catch(function(){setTimeout(r,1500)})},6000)</script>";

    return snprintf(buf, len,
        "<!doctype html><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<body style='margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:#070809;color:#e9ece6;font-family:ui-monospace,Menlo,monospace;text-align:center'>"
        "<div style='padding:2rem'>"
        "<div style='width:10px;height:10px;border-radius:50%%;margin:0 auto 1.2rem;background:#b6ff36;"
        "box-shadow:0 0 14px 2px #b6ff36'></div>"
        "<h2 style='font-weight:600;letter-spacing:.04em;margin:0 0 .6rem'>%s</h2>"
        "<p style='color:#717a82;letter-spacing:.04em'>%s</p>"
        "%s"
        "</div></body>",
        title, body, reboot ? REBOOT_JS : "");
}

int ui_update_page(char* buf, size_t len, const char* product,
                   const char* fw, const char* build, bool multipart)
{
    /* Arduino's Update.h consumes a multipart form; esp_ota_ops streams the raw
     * body, so the P4 uploads with fetch() and reports progress in #st. */
    static const char MULTIPART_FORM[] =
        "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
        "<input type=\"file\" name=\"update\" accept=\".bin\">"
        "<input type=\"submit\" value=\"Upload &amp; Flash\">"
        "</form>";
    static const char FETCH_FORM[] =
        "<input type=\"file\" id=\"fw\" accept=\".bin\">"
        "<button id=\"go\">Upload &amp; Flash</button>"
        "<div id=\"st\"></div>"
        "<script>"
        "document.getElementById('go').addEventListener('click',function(){"
        "var f=document.getElementById('fw').files[0],st=document.getElementById('st');"
        "if(!f){st.textContent='Choose a .bin file first.';return}"
        "st.textContent='Uploading '+f.size+' bytes...';"
        "fetch('/update',{method:'POST',body:f}).then(function(r){"
        "return r.text().then(function(t){return {ok:r.ok,t:t}})"
        "}).then(function(res){st.textContent=res.ok?'Flashed — rebooting…':('Failed: '+res.t)})"
        ".catch(function(e){st.textContent='Error: '+e})});"
        "</script>";

    return snprintf(buf, len,
        "<!doctype html><html lang=\"en\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>%s &middot; Firmware Update</title>"
        "<style>"
        "body{margin:0;min-height:100vh;background:#070809;color:#e9ece6;font-family:ui-monospace,Menlo,monospace;"
        "display:flex;align-items:center;justify-content:center;padding:16px}"
        ".card{width:100%%;max-width:380px;border:1px solid #262b31;border-radius:14px;padding:28px 26px;background:#12151a}"
        "h2{margin:0 0 6px;font-size:18px;letter-spacing:.04em}"
        "p{color:#717a82;font-size:12.5px;margin:0 0 20px;letter-spacing:.03em}"
        "input[type=file]{display:block;width:100%%;margin-bottom:18px;color:#e9ece6;font-family:inherit;font-size:13px}"
        "input[type=submit],button{width:100%%;border:0;cursor:pointer;border-radius:10px;font-weight:700;font-size:14px;"
        "letter-spacing:.06em;text-transform:uppercase;color:#0a0d07;padding:14px;"
        "background:linear-gradient(180deg,#d2ff63,#9be32a)}"
        "#st{margin-top:14px;font-size:12px;color:#717a82;letter-spacing:.03em}"
        "a{color:#b6ff36;text-decoration:none;font-size:12px}"
        ".warn{color:#e8b64c;border:1px solid #4a3a17;background:#1b1608;border-radius:8px;"
        "padding:10px 12px;margin:0 0 18px;font-size:12px;line-height:1.5;letter-spacing:.02em}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<h2>Firmware Update</h2>"
        "<p>Running FW %s &middot; built %s</p>"
        /* ESP-020: say it plainly. Writing the image suspends the flash cache, which
         * freezes BOTH cores regardless of task priority -- so the 1 ms MIDI writer
         * cannot run while this happens. This is not a subtle timing effect: it is
         * seconds against a 1 ms tick. The clock WILL stop. Better the user reads that
         * here than discovers it in front of an audience. */
        "<p class=\"warn\"><b>Updating stops the clock.</b> Writing firmware freezes both "
        "CPU cores for several seconds, so MIDI clock and transport halt until the device "
        "reboots. Do not update mid-set.</p>"
        "<p>Select a compiled .bin. The device flashes it and reboots automatically. "
        "A failed or interrupted upload leaves the running firmware untouched.</p>"
        "%s"
        "<p style=\"margin-top:16px\"><a href=\"/\">&larr; Back</a></p>"
        "</div></body></html>",
        product, fw, build, multipart ? MULTIPART_FORM : FETCH_FORM);
}
