# linkcli — a Link peer for the bench

An interactive Ableton Link peer. It exists because the firmware can't be one.

`X32Link/link_protocol.c` is a **listener**: it parses the Link gossip timeline (tempo,
peers, StartStopState) but never transmits. So a device alone on the network never forms
a session, reports `peers:0`, and silently drops transport intents — `/transport` returns
200 and does nothing. `linkcli` is the Ableton stand-in that makes the session real.

## Build

```sh
cmake -S . -B build
cmake --build build
```

No dependencies to fetch: the Link SDK is vendored at `LinkAudioPoC/third_party/link`
with asio bundled under `modules/asio-standalone`. Override the SDK path with
`-DLINK_SDK_DIR=/path/to/link` if it moves.

## Run

```sh
./build/linkcli                       # join/create a session at 120 BPM
./build/linkcli --tempo 128 --play    # ...at 128, already playing
./build/linkcli --quantum 8           # 8-beat bar
```

| key | action |
|---|---|
| `↑` / `↓` | tempo ±1 BPM |
| `←` / `→` | tempo ±0.1 BPM |
| `q` / `Q` | quantum ± 1 beat |
| `space` | start / stop transport |
| `p` | force beat 0 onto *now* (a known downbeat to trigger the analyzer on) |
| `s` | toggle start/stop sync |
| `a` | toggle Link on/off |
| `x` | quit |

## Two things that will confuse you

**A peer that joins a session that is already playing stays stopped.** This is Link's
design, not a bug: start/stop is last-writer-wins by timestamp (`Controller.hpp:89`), and
a booting peer initialises its start/stop state with `timestamp = hostTime`
(`Controller.hpp:67`) — so its fresh "stopped" is *newer* than your earlier "playing" and
wins. Tempo, by contrast, *is* adopted by late joiners, which makes the asymmetry extra
confusing.

> **Get every peer into the session first, then press play.** If a device reboots
> mid-test it comes back stopped even though the session is playing — re-press play
> rather than hunting for a firmware transport bug.

**This is a live peer.** Pressing play starts every Link-enabled app on the LAN — Ableton
Live, Note on your phone. A peer count higher than you expect is usually a real device,
not a bug.

## Licensing

Ableton Link is GPLv2. This is a dev tool and is not distributed, so that's a non-issue
here — but it would become one if any of this shipped.
