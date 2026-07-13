# 9. Converge the clock box on ESP-IDF; the shared engine moves out of X32Link

Date: 2026-07-12
Status: accepted

Supersedes [ADR-0007](0007-shared-pure-c-compiled-by-path.md) (placement + sharing
mechanism). Amends [ADR-0005](0005-per-target-firmware-framework.md) (per-target
framework) — its stated revisit condition has fired. Re-justifies, but does not weaken,
[ADR-0003](0003-firmware-pure-c-glue-split.md).

## Context

Three things are now true that were not true when 0005/0006/0007 were written.

**1. Two glue layers became a real burden — the exact condition ADR-0005 said to revisit
on.** ADR-0005 accepted Arduino-for-S3 / ESP-IDF-for-P4 and closed with: *"Revisit only
if … maintaining two glue layers becomes a real burden (not expected — glue is thin by
ADR-0003)."*

The glue was not thin enough. The 1 ms clock writer exists **twice** — `ks_main.c` (P4,
ESP-IDF) and `ktouch_midi_out.cpp` (Touch, Arduino) — because a FreeRTOS task plus UART
writes cannot be pure C. ESP-023 found, measured on the analyzer, and fixed a real bug in
that writer: transport (`0xFA`) was emitted **after** the downbeat clock (`0xF8`), so every
slave started one 24-PPQN tick late (20.8 ms at 120 BPM). **The fix landed on the P4 only.
The Touch carried the same bug, unfixed, until it was found by reading the code on
2026-07-12 while building the ESP-025 bench rig.**

That is the bill for a duplicate implementation: the bug is found once and fixed once, and
the other copy keeps it silently. This is not hypothetical any more.

**2. ADR-0007's factual claim is false.** It states that symlinking is "no longer a sharing
mechanism" and that `X32_emulator/fw_version.h` is "the only instance." In fact
`KitchenSyncTouch/` symlinks **41** files out of `X32Link/` and `LoraLink/` symlinks **8**.
The symlink farm ADR-0006 declined `shared/` in order to *avoid* exists anyway — we pay its
full cost and get none of its benefit, because the shared engine still lives inside a
product's sketch directory.

**3. That placement causes defects, not just untidiness.** ADR-0006 weighed the hoist as a
*navigability* question and reasonably found it not worth a symlink farm. But the real cost
is worse than navigability: `X32Link/` is simultaneously "the mixer product" and "the shared
library," so *"put the shared module in X32Link"* slides silently into *"put the feature in
X32Link."* On 2026-07-12 the entire ESP-025 bench rig — board profile, buttons, transport
wiring — was built into the mixer firmware and had to be backed out. The root AGENTS.md made
it worse by opening with "What this firmware is: Tempo → XR18/X32 FX delay sync," describing
a *related* product as if it were the repo.

**4. ESP-024 is a forcing function.** Its thesis is hardware-timed RMT edges and `IRAM_ATTR`
placement. The Arduino core ships a **fixed sdkconfig** — no `menuconfig` — so the knobs that
decide jitter (IRAM, cache behaviour, interrupt allocation) are not reachable. Doing that work
on the framework that hides the controls, and then doing it a second time on the P4, is the
worst of both. The timing work should happen once, on the framework that exposes the controls,
in the writer that ships.

## Decision

**Converge the clock box on ESP-IDF. Leave the related products on Arduino.**

- **KitchenSyncTouch migrates to ESP-IDF and becomes a board target of KitchenSync**
  (ESP-025 ticket: ESP-026). One ESP-IDF project already supports `esp32`, `esp32s3` and
  `esp32p4` via `idf.py set-target`. The result is **one** clock writer, one transport path
  and one timing implementation across the P4 product, the S3 touch unit, and the classic-
  ESP32 bench rig — so "what the bench measures is what the product runs" becomes literally
  true rather than aspirational.

  The port is bounded: KitchenSync **already has** the ESP-IDF equivalent of nearly every
  piece of Arduino glue the Touch uses — `wifi_link.c`, `ks_web.cpp` (HTTP + OTA),
  `ks_config_nvs.c`, `ks_hostname.c`, `midi_uart_out.c`, `usb_midi_host.c`,
  `transport_intent.c`, `ks_led.c`. The only genuinely new work is the display: LovyanGFX
  (which supports ESP-IDF natively) and the AXS5106L touch controller over I²C.

- **The shared pure C moves out of `X32Link/`** into a home owned by the primary product (a
  KitchenSync ESP-IDF component). Arduino sketches that still need it — `X32Link/`,
  `LoraLink/`, `X32_emulator/` — symlink it into their flat roots, which is the arduino-cli
  constraint ADR-0006 correctly identified and which has not gone away.

  **This is done as part of the migration, not before it.** Standalone, the move would *add*
  ~40 symlinks to `X32Link/` (which holds the real files today) on top of KitchenSyncTouch's
  41. Bundled with the migration, KitchenSyncTouch's 41 disappear — ESP-IDF compiles by path
  — so the move is roughly **symlink-neutral**. ADR-0006 anticipated precisely this: *"If the
  S3 firmware ever migrates off arduino-cli … this constraint lifts and the co-location
  refactors become worth revisiting."*

- **X32Link, LoraLink and X32_emulator stay on Arduino.** They are *related* products. They
  do not duplicate the clock engine, so they are not paying the ESP-023 tax, and migrating
  them buys almost nothing. Arduino is good at what it does there — the ESP-025 bench board
  was booting in one command.

- **ADR-0003 (pure C / thin glue) stands, with a corrected rationale.** Its value is **not**
  "so two frameworks can compile it" — that was a consequence of the split being sold as a
  justification for it. Its value is **host-testability**: 56 Unity suites run on the dev box
  in seconds. That payoff survives any framework decision and is the reason to keep writing
  logic in pure C even once the clock box is single-framework.

## Consequences

- The clock writer, transport path and timing work exist **once**. An ESP-023-class bug can
  no longer be fixed in one firmware and left in another.
- ESP-024's RMT/`IRAM_ATTR` work is done once, on ESP-IDF, with `menuconfig` available — and
  it is measured on the same code the product ships. **The migration should land before
  ESP-024, not after**, or the clock gets tuned twice and the two copies drift.
- Near-term risk is real and must be respected: **KitchenSyncTouch works today** and is close
  to shipping. The migration risks regressions on working hardware to buy a mostly
  future-facing benefit. It is justified by ESP-024; absent that timing work, leaving it alone
  would be defensible.
- Two build systems remain (arduino-cli for the related products, `idf.py` for the clock box),
  but they no longer straddle **the same product**, which was the actual source of the tax.
- ADR-0007's "symlinking is not a sharing mechanism / only one instance" rule is withdrawn as
  factually wrong. Symlinks into an arduino-cli flat root **are** the sharing mechanism for
  Arduino consumers, and always were. Do not re-derive the rule; count the symlinks.
- The firmware map at the top of `AGENTS.md` and the banner atop `X32Link.ino` (added
  2026-07-12) are the interim guard while `X32Link/` still holds the shared files. They can be
  relaxed once the engine physically moves.
