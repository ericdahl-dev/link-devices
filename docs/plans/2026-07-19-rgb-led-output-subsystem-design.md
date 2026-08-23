# RGB LED Output Subsystem — reusable, per-output, three styles

Date: 2026-07-19
Status: design (approved in brainstorming; not yet implemented)
Reviewed by: Embedded Firmware Engineer pass (findings folded in)

## Motivation

The codebase has **two** RGB LED "styles" that grew independently and were never
unified:

1. **Visual Metronome** — `X32Link/metro_strip.{h,c}` (pure renderer: beat position +
   config → fills an N-pixel RGB array; CHASE/FLASH/FILL patterns, colors, brightness,
   fade; host-tested) driven on the P4 via `KitchenSync/main/ks_led.{h,c}` (RMT
   `led_strip`). Fully user-configurable.
2. **Status Light** — `KitchenSyncTouch/ktouch_status_rgb.{h,c}` (pure: device state →
   one RGB; fixed semantics stopped/armed/running/locked/downbeat; ESP-043) driven on
   the headless S3 Super Mini via the Arduino core's `rgbLedWrite(GPIO48, …)`. Fixed
   palette.

The trigger for this work: on the Super Mini you get **no color/brightness control** —
because `KSTOUCH_HAS_LED` (the *external strip* metronome capability) is off, and the
onboard RGB is the *separate*, fixed Status feature. The two were never connected. The
goal is to make **both styles first-class, reusable, per-output subsystems** any board
can pull in, with runtime control — and to leave room for a third.

## Goals / non-goals

**Goals**
- One reusable RGB-output subsystem; a board declares its LEDs and inherits full app control.
- Independent outputs, each assigned a style (a board may have zero, one, or several).
- Runtime-configurable style + settings via `/config.json` + the iOS app; hardware fixed at build.
- Preserve the 1 ms MIDI-clock timing integrity absolutely.

**Non-goals (this pass)**
- Implementing the **Effect** style (defined + seamed only — see Scope).
- Sound-reactive audio (mic → features) — a separate future subsystem.
- Bringing up the WLED controller board (future; flashed via WLED web OTA).

## The three styles

Both existing renderers already share the shape *"given inputs, fill `out[0..npix)`."*
A third — **Effect** — is added because the styles differ on the one axis worth
separating: their **timing/input contract**.

| Style | Reads from the snapshot | Tiers | Status |
|---|---|---|---|
| **Status** | `{transport, link_locked}` + `beats` | ambient base color **+ tempo pulse** | real now |
| **Metronome** | `{beats, quantum}` | pure **tempo** | real now |
| **Effect** | `{anim clock, audio, palette/params}` (+ optional `beats`) | ambient / audio (+ optional tempo) | **defined, not built** |

### Timing is per-tier, not per-style

Every output render has up to two tiers:

- **Tempo tier** — flash / pulse / chase driven by the **phase-accurate beat**. *Always tight.*
- **Ambient tier** — base state color, or a free-running / audio effect. *Loose.*

Status is *not* purely loose: its green beat-flash and amber armed-blink **are a tempo
representation** and must be tight. Metronome is all tempo. Effect is ambient/audio with
an *optional* tempo tier. The renderers already derive flash on/off from `beats`, so the
math is correct — tightness is entirely a function of how phase-accurate/fresh the
`beats` we hand them is, and how often the output is pushed.

## Architecture

### Core model

```
led_out[]   (build-time, per board):  { gpio, npix, driver, external_power }
led[i]      (runtime, NVS):            { enable, style, bright, ...style-specific }
```

`driver ∈ { ONBOARD_SINGLE, STRIP_RMT }` (STRIP_SPI reserved). Descriptors, per board:

| Board | `led_out[]` |
|---|---|
| Super Mini (S3) | `{{ 48, 1, ONBOARD_SINGLE, false }}` |
| KitchenSync (P4) | `{{ 2, 8, STRIP_RMT, true }}` |
| Waveshare Touch (S3) | `{}` — GPIO48 is the panel; emits **no** `led` capability |
| WLED controller (ESP32, future) | `{{ 16, 256, STRIP_RMT, true }}` — **2-D 32×8 matrix**, see caveat |

**Confirmed WLED-controller profile** (read from a live WeGoIOT box running WLED
16.0.0, MAC `e0:8c:fe:34:15:dc`, `192.168.1.252`): **ESP32 classic**, LED data
**GPIO16** (2nd out GPIO2, button GPIO0), **256 LEDs as a 32×8 matrix** (maxpwr
capped 1000 mA), **I2S digital mic** (AudioReactive). Flashable via **WLED web OTA**
(no serial adapter; serial for recovery only).

> **2-D matrix caveat.** This board is a **32×8 matrix, not a linear strip**, and the
> current model (`metro_strip` / `led_output`) is **1-D** (`npix` linear). Two paths for
> the Effect device: (a) treat it as a 256-px serpentine linear run — fine for a beat
> metronome, ignores the 2nd dimension; (b) add a 2-D XY-map layer — required for real
> WLED-parity 2-D effects. This is a genuine gap between the current model and this
> hardware; it belongs to the Effect ticket (ESP-050 follow-on), not this pass.

### Data flow (the RT-safe split)

```
clock/beat task (RT, core 1) ── publishes ──▶ LedSnapshot
                                              { beats, quantum, transport, link_locked }
                                              (one-writer / many-reader, like s_stat;
                                               phase-accurate — same beat the MIDI clock uses)

LED task(s) (core 0, low pri) ── read snapshot ──▶ for each enabled output i:
      buf ← switch(led[i].style) {                       // 3-case, no plugin system
              STATUS:    status_render(snap, npix, out)
              METRONOME: metro_strip_render(snap.beats, snap.quantum, npix, cfg, out)
              EFFECT:    effect_render(...)               // future
            }                                             // renderers emit FULL-SCALE
      apply_brightness(buf, npix, led[i].bright)          // ← single owner, once
      glue_write(i, buf, npix)                            // ONBOARD_SINGLE | STRIP
```

### Dispatch & cadence

- Top-level style selection is a **3-case `switch`**. No dispatch table at the style
  level. The **Effect** style is the only one that grows internally, via an
  **effect-function table** (pure `(clock, audio, params, npix) → fill out[]` functions).
- Render+push runs on its **own task, off the 1 ms clock task** (never core 1). Cheap
  tempo-bearing outputs (1-px status, small strips) tick **fast** (a 1-px push is ~30 µs,
  so 500 Hz–1 kHz is essentially free) → tight beat edges. Heavy ambient Effect strips run
  their **own slower cadence**; independent per output, so a ~5 ms effect frame never
  delays a tight status/metronome pulse.
- **Brightness is applied once, in the dispatcher** — removed from the renderers.

## Config schema + migration

```
led[i] = { enable, style, bright,                       // all styles
           beat_color, accent_color, mode, fade }        // Metronome
```

- **Status** uses only `enable`/`style`/`bright` (fixed palette; dim/enable only).
- **Effect** fields `{effect_id, speed, intensity, palette}` are **deferred** to a later
  version bump when Effect is implemented — the enum value exists now, `ks_config_valid`
  rejects selecting `EFFECT` on a build that doesn't implement it, but no speculative NVS
  fields land today (YAGNI).

**Migration (the error-prone part):** today's flat `led_*` scalars → `led[0]` with
`style=METRONOME` (preserves current P4 behavior). Bump `KS_CONFIG_VERSION`, freeze the
prior struct, write the mapping, fix the `_Static_assert(sizeof==…)`, and add the new
fields to **both** `ks_config_live_safe_copy` (so style/color/brightness/mode/fade apply
live, no reboot) and `ks_config_valid` (clamp `bright 0..100`, range-check `style`/`mode`).
The migration chain in `X32Link/ks_config.c` is meticulous and fail-closed — follow it
exactly. Per-board **descriptor count** drives how many `led[]` entries `/config.json`
surfaces; a board with zero outputs emits **no** `led` capability, so the app draws
nothing for absent hardware (the existing `KSTOUCH_HAS_*` honesty rule, extended).

## Module placement & cross-framework glue

Per ADR-0003 (pure logic host-tested; glue thin) and the ks-core direction (ADR-0015):

- **Pure, shared, host-tested:** the three renderers — `metro_strip.c`;
  `ktouch_status_rgb.c` wrapped as `status_render(snap, npix, out[])` (fills all `npix`
  with the one status color); future effect renderers — **plus a new pure `led_output.c`
  dispatcher** (snapshot + `led[]` + descriptor → filled buffers, brightness applied once).
- **Thin per-framework glue:**
  - **ESP-IDF:** generalize `ks_led.c` to `write(output, px, npix)` over RMT `led_strip`,
    and own the **core-0 low-priority LED task**.
  - **Arduino:** two backends keyed off `descriptor.driver` — `ONBOARD_SINGLE` →
    `rgbLedWrite` (asserts `npix==1`); `STRIP` → the `espressif/led_strip` component under
    the Arduino core (arduino-esp32 is IDF underneath, so both frameworks collapse onto one
    driver) or a NeoPixel lib. **`rgbLedWrite` cannot synthesize an N-pixel strip** by
    calling it N times (it re-latches at pixel 0) — the `driver` field makes the output
    honest about which backend it has. The Arduino tempo tier runs on its **own timer/task,
    not `loop()+delay(5)`** (5 ms cadence quantizes the flash edge — too loose for tempo).

## Embedded constraints (folded in from review)

- **Brightness has one owner** (dispatcher). `metro_strip_render` currently scales by
  `cfg->bright` internally; that must be **removed** or Metronome renders `brightness²`.
- **Never render/push on the 1 ms clock task.** Today the P4 LED push runs *inline* in
  `clock_out_task` (pri 19, core 1) and blocks on `rmt_tx_wait_all_done(-1)`. At 8 px
  (~290 µs) it fits; at 60 px (~1.8 ms) it blows a 1 ms tick and drops clock pulses. Move
  it to a separate core-0 task; use **DMA** + a **bounded** (never infinite) RMT wait for
  long strips.
- **`npix` stays build-time**, never in NVS (you can't rewire the board from the app; a
  persisted `npix` outrunning the strip/buffer is a bug class).
- **No VLAs on any task stack.** Static per-output buffers sized to a compile-time
  `LED_MAX_PIXELS`.
- **RMT scarcity:** one TX channel per RMT strip (S3 ≈ 4 total). The only RMT user today is
  the WS2812 (DIN strobe is GPIO, DIN MIDI is UART1), so there's headroom. Reserve a
  `STRIP_SPI` backend before promising multi-strip on one board.
- **Power is a board concern:** the `external_power` flag; a long external strip at full
  white can brown out the rail (≈60 mA/px). Full-white FILL on a long strip is a brownout
  waiting to happen — another reason `npix` is build-time.
- **GPIO48 is board-polymorphic** (onboard RGB on Super Mini, panel on Waveshare) — the
  descriptor is per board flag.
- **Status palette sits near the WS2812 floor** (`{0,40,0}` etc.); dimming below ~30%
  starts losing the green/cyan/amber distinction. Acceptable for a status light; set
  expectations. Use `scale8`-style rounding. Never apply `fade` to Status. Any future
  gamma lives in the dispatcher only.

## Testing

- **Renderers (Unity, host):** extend `test/test_metro_strip.c`; add `status_render`
  fill-npix, brightness-owner (full-scale out), and tempo-tier edge tests.
- **Dispatcher (Unity, host):** snapshot + `led[]` + descriptor → expected buffers.
- **Config invariants:** every `led[]` field is live-safe *or* reboot-required and
  `valid`-clamped (mirror the existing config discipline).
- **iOS:** a per-output LED section rendered from the capability (like clock outputs),
  covered by the existing live/reboot partition test.

## Rollout (bounded)

1. **Refactor** the shared renderers to the common `fill out[0..npix)` contract and move
   brightness to the dispatcher. Host tests green; **no P4 behavior change**.
2. **Per-output config** (`led[]`) + migration + the ESP-IDF core-0 LED task; route the P4
   metronome through the new path; verify unchanged on hardware (analyzer + eye).
3. **Super Mini onboard RGB as a Status output** through the same path — replacing the
   ad-hoc ESP-043 wiring, now with dim/enable config + an app section. **This delivers the
   original brightness ask.**
4. **iOS** per-output LED section.
5. **Effect style + WLED board** — a separate future project (board bring-up + effect
   library + the sound-reactive audio subsystem).

## Scope summary

- **Now:** Status + Metronome, unified per-output subsystem, on Super Mini (S3) + KitchenSync (P4).
- **Defined but unbuilt:** the Effect style (enum + dispatch seam + effect-table shape).
- **Future project:** the ESP32 WLED controller board (WeGoIOT, ESP32 classic, GPIO16 data,
  I2S mic, 16 A / 5–24 V, **256 LEDs as a 32×8 matrix**) as an Effect device — flashed via
  WLED web OTA — plus the mic → sound-reactive pipeline and a 2-D matrix layer.

## References

- ADR-0003 (pure C / thin glue), ADR-0009 (ESP-IDF convergence), ADR-0015 (ks-core extraction)
- `X32Link/metro_strip.{h,c}`, `KitchenSync/main/ks_led.{h,c}` (Metronome)
- `KitchenSyncTouch/ktouch_status_rgb.{h,c}`, ESP-043 (Status)
- `X32Link/ks_config.{h,c}` (config model + migration chain), `KitchenSync/main/ks_main.c`
  (`clock_out_task`, the inline LED push to relocate)
- `KitchenSyncTouch/config.h` (per-board capability flags; the GPIO48 trap)
