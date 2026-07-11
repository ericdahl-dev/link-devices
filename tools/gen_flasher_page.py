#!/usr/bin/env python3
"""Generate the web-flasher page.

The CSS is extracted from X32Link/ui_chrome.c -- the SAME string the devices serve. The
design doc calls for compiling a host program that prints ui_chrome_css(); reading the C
string literal directly gets the identical bytes with no compiler in the loop, and it
fails loudly if the source stops looking the way we expect. Either way the point stands:
the page's look is generated from the device-UI source, never hand-copied, so it cannot
drift into a lookalike that is subtly wrong.

Usage: gen_flasher_page.py <site-dir> <version>
"""
import re
import sys
from pathlib import Path

site = Path(sys.argv[1])
version = sys.argv[2]


def ui_chrome_css() -> str:
    """Concatenate the C string literal that is ui_chrome.c's CSS[]."""
    src = Path("X32Link/ui_chrome.c").read_text()
    m = re.search(r"static const char CSS\[\]\s*=\s*(.*?);\s*\n", src, re.S)
    if not m:
        raise SystemExit("FATAL: could not find CSS[] in X32Link/ui_chrome.c -- "
                         "the page's styling is generated from it, so this must not be guessed at")
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    if not parts:
        raise SystemExit("FATAL: CSS[] in ui_chrome.c had no string literals")
    css = "".join(parts)
    return css.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


variants = []
vf = site / ".variants"
if vf.exists():
    for line in vf.read_text().splitlines():
        if line.strip():
            variants.append(line.split("\t"))

# Page-specific rules layered ON TOP of the device chrome, same as ks_web.cpp does.
page_css = """
/* --- web flasher page (generated; device chrome above) --- */
body{display:block;padding:34px 16px 80px}
.wrap{max-width:900px;margin:0 auto}
.hdr{display:flex;align-items:center;gap:11px;margin-bottom:8px}
.sub{color:var(--mut);font-size:12.5px;letter-spacing:.06em;margin:0 0 26px}
.note{border:1px solid var(--line);border-radius:11px;padding:14px 16px;margin-bottom:26px;
background:linear-gradient(180deg,rgba(255,255,255,.02),transparent);font-size:12.5px;line-height:1.6;color:var(--mut)}
.note b{color:var(--ink)}
.note.warn{border-color:#4a3a1a}
.note.warn b{color:var(--amber)}
.cards{display:grid;grid-template-columns:1fr;gap:16px}
@media (min-width:760px){.cards{grid-template-columns:1fr 1fr}}
.card{border:1px solid var(--line);border-radius:16px;padding:20px 22px;position:relative;overflow:hidden;
background:repeating-linear-gradient(180deg,rgba(255,255,255,.012) 0 2px,transparent 2px 4px),linear-gradient(180deg,#191d22,#101317);
box-shadow:0 1px 0 rgba(255,255,255,.04),0 22px 60px -20px #000}
.card h2{font-family:var(--disp);font-weight:800;font-size:17px;letter-spacing:.04em;margin:0 0 6px}
.card p{color:var(--mut);font-size:12.5px;line-height:1.55;margin:0 0 16px}
esp-web-install-button{--esp-tools-button-color:#0a0d07;--esp-tools-button-text-color:#0a0d07}
esp-web-install-button button{width:100%;border:0;cursor:pointer;border-radius:11px;font-family:var(--disp);
font-weight:800;font-size:15px;letter-spacing:.14em;text-transform:uppercase;color:#0a0d07;padding:15px;
background:linear-gradient(180deg,#d2ff63,#9be32a);box-shadow:0 6px 0 #5e8a16,0 16px 30px -12px rgba(182,255,54,.5)}
esp-web-install-button button:active{transform:translateY(4px);box-shadow:0 1px 0 #5e8a16}
esp-web-install-button[install-unsupported] button{opacity:.4;cursor:not-allowed}
.unsupported{color:var(--amber);font-size:12px;letter-spacing:.04em}
.foot{text-align:center;color:#3c444c;font-size:10.5px;letter-spacing:.18em;text-transform:uppercase;margin-top:34px}
.foot a{color:#4b535b}
"""

cards = "\n".join(f"""    <div class="card">
      <h2>{name}</h2>
      <p>{blurb}</p>
      <esp-web-install-button manifest="firmware/{slug}/manifest.json">
        <button slot="activate">Flash</button>
        <span slot="unsupported" class="unsupported">Needs Chrome or Edge (WebSerial).</span>
        <span slot="not-allowed" class="unsupported">Open this page over HTTPS.</span>
      </esp-web-install-button>
    </div>""" for slug, name, blurb in variants)

html = f"""<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flash — link-devices</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,600;12..96,800&family=DM+Mono:wght@400;500&display=swap" rel="stylesheet">
<link rel="stylesheet" href="style.css">
<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
</head><body>
<div class="wrap">
  <div class="hdr"><span class="pwr"></span><span class="wordmark">KITCHEN&middot;<b>SYNC</b></span>
    <span class="rev" style="margin-left:auto">FW {version}</span></div>
  <p class="sub">Flash firmware straight from the browser. No toolchain, no terminal.</p>

  <div class="note">
    <b>You need:</b> Chrome or Edge (Safari and Firefox have no WebSerial), a USB data cable
    &mdash; not a charge-only one &mdash; and the board plugged in.<br>
    <b>Pick the board you actually have.</b> The browser checks the chip and will refuse a
    mismatched image, but it <em>cannot</em> tell one ESP32-S3 board from another &mdash; a
    Super Mini and a QT Py are silicon-identical. That choice is yours.
  </div>

  <div class="note warn">
    <b>Flashing erases everything, including saved WiFi.</b> The device comes up in setup
    mode afterwards &mdash; join its <b>-Setup</b> WiFi network and browse to
    <b>192.168.4.1</b> to configure it again.<br>
    Already on your network and just want a newer build? Use the device's own
    <b>/update</b> page instead &mdash; that keeps your config.
  </div>

  <div class="cards">
{cards}
  </div>

  <p class="foot">
    <a href="https://github.com/ericdahl-dev/link-devices">github.com/ericdahl-dev/link-devices</a>
    &middot; {version}
  </p>
</div>
</body></html>
"""

(site / "style.css").write_text(ui_chrome_css() + page_css)
(site / "index.html").write_text(html)
(site / ".nojekyll").write_text("")   # Pages must not run Jekyll over firmware/
print(f"  page: {len(variants)} variants, css from X32Link/ui_chrome.c")
