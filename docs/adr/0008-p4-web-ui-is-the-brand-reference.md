# 8. The KitchenSync P4 web page is the brand reference; per-firmware pages mirror it

Date: 2026-07-10
Status: accepted

## Context

Three firmwares serve a rack-panel web config page and share one design system:
`ui_chrome.{h,c}` (ARC-017) owns the *look* (colour tokens, panel, screws, tempo
glass, switches, Save button) and the client plumbing (`poll()`, `showBpm`,
`postLive`). Each firmware keeps its **own form** — the fields and the
page-specific CSS/markup that extend the chrome — because the feature sets differ
(P4: multi-output clock + metronome + LED strip; Touch: DIN clock + transport;
X32Link: mixer OSC).

"The forms stay per-firmware" left a hole: *how much finish* each form should
carry was never written down. During ESP-016 (Inc3b) the KitchenSync Touch page
had drifted well below the others — flat stacked toggles, no section grouping, and
help text built by misusing the `.cap` heading class with inline
`style="…color:var(--mut)"` overrides (a design-system bypass). It read as a
different, lesser product than the P4 page even though it used the same chrome.

Two failure modes were possible when closing that gap:
1. Fix one page by editing the *shared* `ui_chrome.c` — which silently changes the
   other two firmwares (and X32Link ships to different hardware).
2. Let each page invent its own structure ad hoc, so "on brand" means nothing and
   the pages diverge again on the next increment.

## Decision

**`KitchenSync/main/ks_web.cpp` (the P4 page) is the canonical brand/UX
reference.** It is the most complete, most polished config surface, and its
page-specific patterns are the house style every firmware's page should mirror,
scaled to its own feature set:

- **`.frow.head`** header rows pair a section title (with the LED-dim status tick)
  with that feature's master toggle.
- **`.grp` groups + `.sect` accent rails**: a feature's settings live behind its
  master toggle and reveal when it flips on (`syncSect`).
- **Structured `.fld` fields** with `.pre` prefix labels — never inline `style=`.
- Help text is a **token-driven class** (`.hint` / equivalent), never a
  repurposed `.cap` with inline colour/spacing.
- A **responsive desktop treatment** (two-column form, 4-across status "meter
  bridge") when the form is long enough to benefit.

**The shared chrome is never edited to fix a single page.** Anything one page
needs that isn't already in `ui_chrome.c` is added as page-specific CSS in that
page's own `<style>` block — exactly the way `ks_web.cpp` extends the chrome.
Changes to `ui_chrome.{h,c}` are cross-firmware changes: they must make sense for
P4 **and** Touch **and** X32Link, and they keep the host tests green.

Divergence from the P4 patterns is allowed only for a device reason (fewer
features, smaller screen), not by accident. Per ADR-0004, a Touch page carrying
*fewer fields* than the P4 is intentional; this ADR governs the *finish* of the
fields it does carry.

## Consequences

- Future work on any config page starts by reading `ks_web.cpp` and mirroring its
  structure; "make it match the P4" is now a defined, reviewable bar, not taste.
- A code/brand review that finds a page using flat toggles, inline-styled help
  text, or ad-hoc structure should treat it as **drift to fix**, not a style
  choice — and the fix goes in that page's CSS, never in `ui_chrome.c`.
- Editing `ui_chrome.{h,c}` for a one-page tweak is a **red flag**: it changes the
  other two firmwares. Reviewers should push such a change back into page-specific
  CSS unless it is genuinely a shared-system improvement.
- ESP-016 Inc3b brought the Touch page to this bar (`ktouch_web.cpp` only:
  `.frow.head`, `.grp`/`.sect` reveal, `.hint`, desktop meter bridge). It is the
  reference for what "aligned to the P4" looks like on a smaller feature set.
