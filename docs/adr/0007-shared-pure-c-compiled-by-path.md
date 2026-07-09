# 7. Shared pure C is compiled by path, not symlinked

Date: 2026-07-09
Status: accepted

Supersedes [ADR-0006](0006-shared-pure-c-lives-in-arduino-sketch-root.md).
Amends the sharing-mechanism paragraph of [ADR-0003](0003-firmware-pure-c-glue-split.md).

## Context

ADR-0003 established the pure-C / thin-glue split and described how the shared
modules reach each firmware:

> Shared C files are **real in `X32Link/` and symlinked into `X32MidiClock/`**
> (Arduino requires flat sketch dirs); the `midi_*` files are the reverse.

ADR-0006 then declined the "co-locate shared modules into `link/` or `shared/`"
refactors (ARC-008, ARC-009), reasoning that any clean move would be undone by
"the symlink farm the flat-sketch rule forces."

Both statements described a repo with **two** Arduino sketches. LNK-024
(2026-07-08) retired `X32MidiClock/` — X32Link had supported MIDI clock since
LNK-010, so the second sketch was a duplicate product. Its `midi_*` files became
real files in `X32Link/`, and its 18 inbound symlinks went with it.

What remains:

- **X32Link** (ESP32-S3, arduino-cli) — owns the shared pure C as real files in
  its sketch root.
- **KitchenSync** (ESP32-P4, ESP-IDF) — compiles those same files by relative
  path via `SRCS` + `X32LINK_DIR` in `main/CMakeLists.txt`.
- **Host tests** — compile them via `-I../X32Link` and explicit paths in
  `test/Makefile`.
- **X32_emulator** — one vestigial symlink, `X32_emulator/fw_version.h`.

The symlink farm is gone. Nothing was decided about it; it evaporated as a
side effect of deleting a duplicate sketch. This ADR records what the sharing
mechanism actually is now, because two accepted ADRs describe one that isn't.

## Decision

**Shared pure C is compiled into each target by path.** ESP-IDF targets list it
in `SRCS`; host tests list it in `test/Makefile`. Symlinking is no longer a
sharing mechanism — it is a workaround reserved for the case that forces it: a
*second* arduino-cli sketch that needs a shared file in its own flat root.
`X32_emulator/fw_version.h` is the only instance. Do not add more without that
constraint actually applying.

**Shared pure C still physically lives in the `X32Link/` sketch root**, and
ARC-008 / ARC-009 stay declined — but on the narrower, more durable grounds that
ADR-0006 buried under the symlink argument: **arduino-cli compiles only the
sketch root directory** (plus `src/` with its own include rules). It does not
recurse. A file moved to `X32Link/link/` or a top-level `shared/` stops being
compiled into the firmware we flash, and the failure is a link error, not a
warning. That constraint is untouched by LNK-024 and is the whole reason. The
symlink farm was a *consequence* of the constraint, never the reason itself, and
ADR-0006 mistook one for the other.

ADR-0003's pure-C / thin-glue split is unaffected and, with two glue frameworks
under ADR-0005, more load-bearing than when it was written. Only its
sharing-mechanism sentence is amended.

## Consequences

- `X32Link/` still contains both S3-specific glue and target-neutral shared
  logic; its name still under-sells what lives there. `AGENTS.md` remains the
  place that says which modules are shared vs S3-only.
- Adding a shared module now means one edit — a line in
  `KitchenSync/main/CMakeLists.txt` and one in `test/Makefile`. No symlink.
- **The silent-fork failure mode is gone.** A symlink can be replaced by a copy
  (tarball export, `cp -r`, a Windows checkout without `core.symlinks`) and drift
  from its target with nothing complaining; ADR-0003 named this risk in its own
  Consequences. A CMake `SRCS` path cannot silently fork — a missing file is a
  build error. This closes the risk rather than guarding it.
- The reason ARC-008/009 are declined is now a single, checkable fact about
  arduino-cli. If the S3 ever migrates off arduino-cli, the constraint lifts and
  co-location becomes worth revisiting — same escape hatch ADR-0006 left, now
  resting on the real blocker.
- `X32_emulator/fw_version.h` is the last symlink in the repo. If the emulator is
  ever folded in or retired, no symlink mechanism remains and this ADR's second
  paragraph can be dropped entirely.
