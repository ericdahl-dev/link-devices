# Analog sync output board — BOM + schematic (2026-07-10)

Hardware companion to the GPIO MIDI/clock design
(`2026-07-10-gpio-midi-clock-io-design.md`), for **PR3 / P4-021 / LNK-028**: take
3.3 V logic pulses off the P4-NANO header, level-shift to a clean 5 V, protect
against patch-cable back-feed, and present them on 3.5 mm Eurorack jacks (and,
optionally, a Roland DIN-sync connector).

The firmware (the existing `clock_ticker` + `plan.downbeat`) already decides *when*
each edge fires. This board only makes those edges electrically safe and Eurorack-
level. No DAC — clock/reset/gate are logic pulses; pitch CV is explicitly out of
scope (the P4 has no DAC).

## Why a board at all

- P4 GPIO is **3.3 V**. Eurorack triggers nominally want **~5 V**; 3.3 V is above
  many modules' threshold but *marginal* and not universal — some gear misses it.
- A patch cable can dump **±12 V** from a case's rails back into your output. A bare
  GPIO or an unprotected buffer dies on the first accidental patch.

So: one HCT buffer (3.3→5 V translation, the key trick) + per-jack clamp/series
protection. ~$6 of parts.

## Channel plan (one 74HCT125 = 4 buffers)

| Ch | Source GPIO | Jack | Firmware |
|---|---|---|---|
| 1 | GPIO23 | Clock A (3.5 mm) | RMT, configurable PPQN |
| 2 | GPIO32 | Clock B (3.5 mm) | RMT, per-output division |
| 3 | GPIO33 | Reset / downbeat (3.5 mm) | `plan.downbeat`, GPIO+esp_timer |
| 4 | GPIO36 | spare / DIN-sync run-stop | optional |

RMT has only 3 free TX channels (the WS2812 LED owns one), so at most 2–3 outputs
get hardware-shaped edges; the reset rides an `esp_timer` one-shot, which is plenty
clean for a 5–15 ms pulse. Pins are confirmed free on the P4-NANO header
(GPIO23/32/33/36; **not** 24–27 — those are the USB1 PHY).

## Schematic

Shared power rail + protection, then one channel drawn in full (×3–4 identical).

```
 P4-NANO 2×20 header
 ┌──────────────┐
 │ 5V   ●───────┼──────────┬─────────────┬────────────────►  +5V rail
 │ GND  ●───────┼────┐     │             │
 │ GPIO23 ●──┐  │    │  [C1 10µF]    [C2 100nF]     (bulk + IC decoupling)
 │ GPIO32 ●─┐│  │    │     │             │
 │ GPIO33 ●┐││  │    └─────┴──────┬──────┴────────────────►  GND rail
 │ GPIO36 ●│││  │                 │
 └─────────┼┼┼──┘                 │
           │││                    │
           │││   ┌──────── 74HCT125 (U1) ────────┐
           │││   │ Vcc(14)=+5V   GND(7)=GND       │
           ││└───┤1A(2)                    1Y(3)  ├──[R1 1k]──┬───────►  JACK A tip
           ││    │1OE(1)=GND                      │           │
           ││    │                                │    D1 ▲(BAT85)  D2 ▲
           │└────┤2A(5)                    2Y(6)  ├──[R2 1k]  │ tip→+5V  │ GND→tip
           │     │2OE(4)=GND                      │           │  (cathode  (anode
           └─────┤3A(9)                    3Y(8)  ├──[R3 1k]  │   to +5V)   to GND)
                 │3OE(10)=GND                     │           │
   (spare) ──────┤4A(12)                   4Y(11) ├──[R4 1k]  └── JACK A sleeve → GND
                 │4OE(13)=GND                     │
                 └────────────────────────────────┘

 Per output (repeat the R + D1/D2 + jack for channels B, reset, spare):
        U1 Yn ──[Rn 1kΩ]──┬──────────────► 3.5mm TIP  (0 / +5 V logic pulse)
                          │
                    +5V ──┤◄─ D1 (BAT85)     clamp HIGH: tip can't exceed +5V+0.3
                          │
                    GND ──►│─ D2 (BAT85)     clamp LOW:  tip can't go below −0.3
                          │
                       (jack SLEEVE → GND)
```

**How the protection works:** the 1 kΩ series resistor limits fault current; the
two Schottky diodes clamp the tip to within ~0.3 V of the 5 V and GND rails, so a
back-fed ±12 V is shunted to the rail instead of into the buffer. 12 V through 1 kΩ
is 12 mA / 0.14 W — a 0.25 W resistor survives a sustained short.

## Bill of materials

| Ref | Part | Value / spec | Qty | Notes |
|---|---|---|---|---|
| U1 | **74HCT125** | quad tri-state buffer, DIP-14 (or SOIC-14) | 1 | **HCT, not HC** — see caution |
| R1–R4 | resistor | 1 kΩ, 0.25 W | 4 | one series per output |
| D1–D8 | **BAT85** (or BAT54) | Schottky diode | 8 | 2 clamps per output |
| C1 | electrolytic | 10 µF, ≥10 V | 1 | 5 V bulk decoupling |
| C2 | ceramic | 100 nF | 1 | across U1 Vcc/GND, close to pin 14 |
| J1–J4 | 3.5 mm mono jack | Thonkiconn PJ301M-12 | 3–4 | Eurorack-standard TS jack |
| P1 | pin socket, 2.54 mm | 2×20 (or the needed pins) | 1 | mates the P4-NANO header |
| — | perfboard / PCB | — | 1 | see packaging |

Optional **DIN-sync (Roland Sync24)** for TR-808/909/303: add a 5-pin DIN and feed
its clock (24 PPQN) + run/stop from two buffer channels at 5 V. Sync24 pinout:
pin 3 = GND, pin 1 = clock, pin 2 = run/stop. Uses ch 1 + ch 4 above.

## Build cautions

- **Use 74HCT125, never 74HC125.** Plain HC needs V_IH ≈ 3.5 V at a 5 V supply, so
  a 3.3 V input is marginal → intermittent glitches. HCT has TTL thresholds
  (V_IH ≈ 2.0 V) — reading 3.3 V as a solid HIGH is the entire reason this part is
  here. This is the #1 way the board goes subtly wrong.
- **Tie every /OE (pins 1, 4, 10, 13) to GND.** They're active-low output enables;
  floating them tri-states the output and you get nothing.
- **Single common ground** with the P4 (jack sleeves → board GND → header GND).
- **Current draw is tiny** — buffers into high-Z (~100 kΩ) Eurorack inputs, a few mA
  total. The P4's 5 V header rail handles it.
- **Pulse width is firmware, not hardware:** the RMT/esp_timer path sets 5–15 ms
  triggers; the board just level-shifts whatever edge it's handed.
- Decouple: C2 (100 nF) right at U1's pins 14/7, C1 (10 µF) on the rail.

## Bring-up order

1. Board unpopulated except U1 + decoupling: power from the P4 5 V, confirm 5 V at
   pin 14, ~0 at pin 7.
2. One channel: drive GPIO23 high/low from a firmware scratch (`gpio_set_level`),
   scope/analyzer the jack tip — expect a clean 0/5 V swing.
3. Add the R + clamp diodes, re-check the tip still swings full 0/5 V into a real
   module (the 1 kΩ into 100 kΩ loses ~1%).
4. Then wire the RMT clock path (PR3) and verify the analog edge sits on the same
   grid as the MIDI 0xF8 on the analyzer (CH2 vs CH0 in the ESP-011 capture plan).

## Verify with the logic analyzer

Add **CH2 → a jack tip** (post-buffer, so you're reading the real 5 V edge — set the
analyzer threshold to 5 V logic on that channel, or keep it 3.3 V and read the
pre-buffer GPIO). Overlay against CH0 (MIDI 0xF8) and CH1 (downbeat strobe) from the
ESP-015 capture plan to confirm the analog clock is phase-aligned with the MIDI
clock and the reset lands on the bar line.
