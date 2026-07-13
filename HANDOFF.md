# Handoff — 2026-07-13

Read `AGENTS.md` first (the firmware map at the top). This file is the state of play, the
gotchas that cost real time, and what to do next.

---

## The one thing to understand

**Most of the bugs found in this session existed because the clock writer is implemented
twice** — `ks_main.c`/`ks_tick.c` (KitchenSync, ESP-IDF) and `ktouch_midi_out.cpp`
(KitchenSyncTouch, Arduino). A fix lands in one, the other keeps the bug, silently.

Worse: **a branch full of already-written, already-tested, analyzer-verified fixes had never
merged.** `feat/esp-019-keyboard-transport` held seven of them while the primary product
shipped every one of those bugs. All rescued now (#102, #103, #105, #109). That branch has
nothing of value left — **delete it.**

**ESP-026 converges the two writers.** It is the root cause, not hygiene. It blocks ESP-024.

---

## Bench rig — how to actually test anything

The rig is `KitchenSyncTouch` on a **classic ESP32 DevKit** (WROOM-32, screw-terminal
breakout), at `192.168.2.125` / `kstouch-15c0.local`, on `/dev/cu.usbserial-0001`.

```sh
tools/build-bench.sh [port]        # the ONLY supported way to build it
./tools/linkcli/build/linkcli      # REQUIRED — the firmware can't form a Link session alone
curl -X POST 'http://192.168.2.125/transport?play=1'   # headless transport (ESP-027)
```

- **`linkcli` first, always.** The firmware's Link stack is receive-only: alone on the wire it
  sits at `peers:0` and does nothing. Every "the device is broken" moment traces back to this.
- **Never hand-swap the board flag.** `build-bench.sh` swaps `config.h` *and* `partitions.csv`
  together. The product's 16 MB S3 partition table on the DevKit's 4 MB chip **boot-loops with
  no app and no serial banner** — indistinguishable from a brick. (It did.)
- **Get every peer into the session BEFORE pressing play.** Link start/stop is last-writer-wins
  by timestamp and *booting counts as a write*, so a late joiner comes up stopped. That is not
  a firmware bug.
- The **physical button does not work.** Pin, terminal, pull-up and debounce are all *proven
  good* (bridging GPIO32→GND reads 0; the button never does). `/status` exposes `btn`,
  `btnlows`, `btnpress` so a replacement can be diagnosed by polling. If it has three legs, use
  **COM + NO** — NO+NC never closes, which is exactly the symptom seen.

### The logic analyzer is scriptable

A **Saleae Logic** on **CH0 = GPIO17** (DIN MIDI TX). Logic 2 exposes an MCP server on
`http://127.0.0.1:10530` (enable it in *Settings → Automation*). Drive it with plain JSON-RPC:

```python
call("start_capture", {"deviceId":"A7D1BB81883C0092",
  "logicDeviceConfiguration":{"logicChannels":{"digitalChannels":[0]},"digitalSampleRate":8000000},
  "captureConfiguration":{"timedCaptureMode":{"durationSeconds":30}}})
call("add_analyzer", {"captureId":cid, "analyzerName":"Async Serial",
  "settings":{"Input Channel":{"numberValue":0},"Bit Rate (Bits/s)":{"numberValue":31250}}})
call("export_data_table_csv", {"captureId":cid, "filepath":"/tmp/midi.csv"})
```

Gotchas that cost time: this device has **no configurable threshold** (drop
`digitalThresholdVolts`), analyzer settings must be wrapped (`{"numberValue": 0}`), and a
`peers` count higher than expected is usually **a real device** (a phone running Ableton Note),
not a bug.

---

## Numbers that are real (measured, not assumed)

| | |
|---|---|
| **DIN clock jitter** | **373 µs stdev, 1124 µs peak-to-peak** at 120 BPM |
| What that spread IS | **exactly the 1 ms writer task.** The edge can only land on a tick boundary |
| ESP-023 verified | `0xFA` precedes the downbeat `0xF8` by **320.0 µs** — one MIDI byte at 31250 baud |

**Every jitter figure previously in this repo was the task's own view of itself.** The 373 µs is
the first taken from the wire, and it confirms ESP-024's thesis outright: the jitter is the tick
rate, not the silicon.

---

## What shipped this session

| PR | |
|---|---|
| #96 | ESP-023 — the Touch started every slave a tick late |
| #98 | ESP-025 bench rig, ADR-0009, ESP-026 |
| #99 | ADRs + linkcli made searchable (`tools/reindex.sh`) |
| #100 | **ESP-027 — the DIN clock went silent for 138 s while `/status` said `sync:1`** |
| #101 | button telemetry; a latent product-board link break |
| #102 | **P4-038 RTT filter** — GhostXForm commits throwing the beat origin |
| #103 | **P4: ESP-023 + clock task priority 6 → 19** (it ran *below* lwIP) |
| #104 | CI: parallel compiles + the bench rig had **zero** CI coverage |
| #105 | **P4-037** — the web UI dies from one laptop visit (lwIP out of sockets) |
| #107 | ticket truth-up; filed ESP-028 |
| #108 | **ESP-028 — `/status` stops lying; stale GhostXForm on peer churn** |
| #109 | rescued the last of the stranded branch (ESP-019, phase gauge) |
| #110 | **ESP-026 — KitchenSync builds for esp32p4 + esp32s3 + esp32** |

---

## Next, in order

### 1. ESP-026 — the display spike (**needs the S3 touch board on USB**)

The multi-target build is **done and proven** (#110): one ESP-IDF project, three chips, 102
lines. And an assumption in the ticket was **wrong** — the WiFi stack was expected to be a major
port (the P4 has no radio, gets it from the C6 over ESP-Hosted). **`wifi_link.c` compiled for the
S3 and the classic ESP32 unchanged**, against native `esp_wifi`.

**The display is now the only real unknown.** LovyanGFX + AXS5106L on ESP-IDF. Spike it *before
deleting anything*; if it doesn't come up in a day, stop and re-plan rather than half-migrate.

Then: transport surface → `transport_intent.c`, move the shared pure C out of `X32Link/`
(ADR-0009 — symlink-neutral once the Touch's 41 symlinks go), delete `KitchenSyncTouch/` and
`tools/build-bench.sh`.

### 2. ESP-024 — blocked on ESP-026

Do **not** tune the clock on the Arduino core (fixed sdkconfig, no `menuconfig`, so IRAM/cache/
interrupt knobs are unreachable) and then again on the P4.

**Its cheapest first experiment is still undone: measure the Korg ER-1's own clock floor.** If
the ER-1's onset jitter is already wider than the 373 µs we'd be chasing, the DIN half of the
work is dead on arrival and you skip it entirely. Do that before writing a line of RMT code.

### 3. Loose ends worth closing

- **A peer restarting on the SAME port is still invisible** to `link_measurement_session`: new
  ghost epoch, unchanged endpoint. Fix is upstream — expose the peer's 8-byte node id (already
  in `s_peers[].id`) instead of identifying sessions by `{ip,port}`.
- **`KitchenSyncTouch/secrets.h`** holds real WiFi creds (gitignored). Don't commit it.

---

## Hard-won gotchas

- **`git checkout <old-commit> -- file`** takes the file **wholesale** and silently reverts
  anything that landed on master since. Apply the **delta**, never the file. This nearly ate the
  P4's transport buttons.
- **Renaming a CI job orphans a required status check.** Branch protection requires the exact
  context name; a renamed job never reports and auto-merge blocks forever with every check
  green. There is now a gate job carrying the required name — grow the matrix freely.
- **`sed -i ''` is BSD/macOS only.** GNU sed reads the `''` as a filename. `build-bench.sh` was
  macOS-only until CI compiled the bench rig for the first time and caught it.
- **The codebase-memory indexer excludes `docs/` and `tools/`** — so the ADRs and `linkcli` were
  invisible to every structural query. `tools/reindex.sh` indexes them as separate projects.
  `tasks/` is *not* excluded.
- **A probe that keeps its old labels after a reorder measures one stage and names another.**
- **The tick-health probe was blind to ESP-027 by construction**: a stalled *grid* drops nothing
  and bursts nothing, so every counter stayed green while the wire was dead. `reprimes` (P4-038,
  now landed) is the counter that sees it.
