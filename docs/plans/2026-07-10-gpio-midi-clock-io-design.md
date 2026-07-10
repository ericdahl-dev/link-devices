# Hardware MIDI + clock I/O on GPIO — design (2026-07-10)

Designed with the Embedded Firmware Engineer against the actual tree, sdkconfig,
and tickets P4-011 / P4-021 / LNK-028. Target: `KitchenSync/` (ESP-IDF v5.5,
Waveshare ESP32-P4-NANO). Motivation: put real MIDI/clock on GPIO **in parallel
to USB**, and — because a digital logic analyzer cannot decode USB — get a
UART MIDI stream the analyzer's Async Serial decoder can read, so ESP-011's
launch timing and the ≤1 ms jitter claim finally get measured.

## Pin collisions — the verified forbidden set

From `KitchenSync/sdkconfig` (ESP-Hosted SDIO to the onboard C6):

| Signal | GPIO |
|---|---|
| I2C SDA/SCL | 7, 8 |
| I2S DOUT/WS/DIN/BCLK/MCLK | 9, 10, 11, 12, 13 |
| WS2812 LED (RMT) | 2 |
| **C6 SDIO CLK/CMD/D0-D3 (WiFi)** | **18, 19, 14, 15, 16, 17** |
| **C6 reset** | **54** |
| amp PA enable | 53 |
| **console UART0 (default IOMUX)** | **37, 38** |

**Touch 14-19 or 54 and WiFi (therefore Link) dies.** These were NOT in the
original used-pin list — the trap. Free pool: roughly `0,1,3-6,20-36,39-52`,
minus whatever the P4-NANO muxes to its MIPI-DSI / MIPI-CSI / microSD connectors
(commonly in the 20-36 window). **Candidate picks below are pending a
schematic/silk check; the forbidden set is certain.**

## Pin map (candidates — confirm on the board)

| Function | PR | Candidate | Fallback |
|---|---|---|---|
| MIDI OUT (UART1 TX, 31250) | 1 | GPIO20 | 39-48 block |
| Downbeat strobe (analyzer trigger) | 1 | GPIO21 | " |
| MIDI IN (UART1 RX, 31250) | 2 | GPIO22 | " |
| Analog clock out A / B | 3 | GPIO23 / 32 | 33, 45-48 |
| Analog reset out | 3 | GPIO33 | " |

**Confirmed against the P4-NANO pinout (2026-07-10).** Header-broken-out and free:
GPIO 5, 20, 21, 22, 23, 32, 33, 36, 45, 46, 47, 48. GPIO20/21/22 verified for
PR1/PR2. **Do NOT use GPIO24-27 — they are the USB1 PHY (`USB1P1_N0/P0/N1/P1`),
i.e. the USB-MIDI host port; driving them kills USB.** Also avoid GPIO0/1 (XTAL_32K
RTC clock source), GPIO2/3/4/6 (touch), and the C6 pins (C6_IO9/12/13, C6_U0RXD/TXD).

## PR sequence

| PR | Pure (host-tested) | Glue (impure IO) | Pins |
|---|---|---|---|
| **1 — MIDI OUT mirror + strobe (ESP-015)** | `ks_tick` gains `plan.downbeat` via existing `bar_reset_due` | `midi_uart_out.c` (UART1 TX @31250); ~4 lines in the emit loop | TX 20, strobe 21 |
| **2 — MIDI IN (P4-011)** | `midi_parser.{h,c}` — byte-stream realtime extractor, running-status-safe | `midi_uart_in.c` (RX reader → parser → `midi_clock_in`) | RX 22 |
| **3 — Analog sync (P4-021 / LNK-028)** | already landed (`clock_ticker`, `BarReset`) + config fields | `analog_sync_io.c` (RMT clocks, GPIO+esp_timer reset) — needs 3.3→5V buffer HW | 23/24/25 |

Through-line: PR1's `bar_reset_due`/downbeat *is* PR3's reset pulse; PR2's feed
point *is* the existing USB `midi_clock_in` feed. Each increment reuses the last.

## Key decisions

- **UART1, not UART0** (console). 31250/8N1 divides cleanly → no baud error.
- **Realtime MIDI = single bytes, no packing.** `usb_midi_pack` exists only
  because USB-MIDI wraps every message in a 4-byte event packet; on a wire, 0xF8
  is one byte. So the UART path writes one byte, no `usb_midi_pack`.
- **Tap the one emit point** (`ks_main.c` clock loop) so the UART byte and the USB
  packet leave microseconds apart — the analyzer-decoded UART timing is a faithful
  proxy for the undecodable USB timing.
- **Write directly from the clock task, no queue.** `uart_write_bytes` copies into
  the driver's TX ring (that IS the queue) and returns. A second task only adds a
  scheduler hop and jitter. Never `uart_wait_tx_done` in the loop (320 µs of the
  1 ms budget). At 4×24 PPQN / 300 BPM the wire is <16% utilized.
- **Mirror one output** (realtime bytes are cable-agnostic; mirroring all four
  would emit duplicate bytes). PR1 hardcodes output 0; a config field can come later.
- **Downbeat strobe belongs in PR1** — it's the measurement reference. Pure via
  `bar_reset_due` + a `BarReset` in `KsTickState`.

## MIDI IN (PR2) notes

- Real pure logic: a byte-wise parser. System Real-Time (0xF8-0xFF) are single
  bytes that interleave *inside* other messages and don't disturb running status;
  a byte ≥0xF8 is always a standalone realtime message (never a data byte, which is
  <0x80). So clock-in is robust. Emit the event on sight.
- **Electrical:** a real DIN MIDI IN is an opto-isolated current loop — needs an
  optocoupler (H11L1 / 6N138) + ~220 Ω. **Do not wire a DIN source to a bare 3.3 V
  GPIO.** Logic-level in (analyzer pattern-out, another 3.3 V board) works bare —
  bench convenience only; the product needs the opto front-end.

## Analog sync (PR3) notes

- **RMT channel budget: 4 TX total, the LED strip owns 1 → 3 free.** So NOT 4 RMT
  clock outs. MVP (matches LNK-028's own conclusion): 1-2 clock jacks on RMT +
  reset on GPIO+esp_timer.
- RMT gives a clean, exact-width pulse decoupled from task timing (matters for
  DFAM step-advance, where a smeared edge slips the pattern permanently). `esp_timer`
  one-shot is fine for the reset jack (tens-of-µs falling-edge jitter on a 5-15 ms
  pulse is invisible).
- **Electrical:** P4 GPIO is 3.3 V; Eurorack wants ~5 V (3.3 V is marginal) and a
  hostile patch can back-feed ±12 V. Each out needs a 3.3→5 V HCT buffer + series R
  + clamp. This is the hardware deliverable, and why LNK-028 is `priority: low` — the
  firmware is mostly reuse; the level-shifter/jack board is the unknown. No CV/DAC
  (the P4 has no onboard DAC).

## Logic-analyzer capture plan (ESP-011: 0xFA vs downbeat)

Wiring (3 of 8 channels + GND):
- **CH0 → MIDI TX (GPIO20)** — Async Serial analyzer, 31250 8N1, non-inverted,
  LSB-first. Decodes the real 0xF8/0xFA/0xFC.
- **CH1 → downbeat strobe (GPIO21)** — rising edge = Link bar/phase-0 crossing.
- **CH2 → an analog clock out (once PR3 exists)** — confirm it sits on the grid.

Measure:
1. Trigger on CH1 rising. Issue a transport Start on the mirrored output. On the
   capture, measure Δt from the CH1 downbeat edge to the 0xFA start bit on CH0.
   That Δt is ESP-011's unmeasured offset. Repeat across tempos.
2. Jitter: free-run, capture ~10 s of CH0, export the 0xF8 byte timestamps (Logic 2
   data-table CSV), compute the inter-0xF8 interval distribution. At 120 BPM/24 PPQN
   the nominal is 20.833 ms; its spread is the real number behind the `FREERTOS_HZ=1000`
   ≤1 ms assumption.

## Footguns

1. GPIO 14-19, 54 = C6 SDIO. Touch → WiFi dies. (The trap.)
2. Never UART0 for MIDI; GPIO37/38 reserved.
3. RMT: 4 TX, LED eats 1 → 3 left. Don't design for 4 RMT clock outs.
4. Keep the UART RX task ≤ clock-task priority (6) so RX bursts can't preempt emission.
5. 3.3 V vs 5 V: DIN IN needs an opto; Eurorack out needs an HCT buffer + protection.
6. No MIDI-TX queue — the driver's TX ring is the queue.
7. `FREERTOS_HZ=1000` is real + dual-core, so `pdMS_TO_TICKS(1)` is a genuine 1 ms
   tick — but that's also the emit-jitter floor the analyzer measures.
