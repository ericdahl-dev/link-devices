# 6. Shared pure C lives in the X32Link Arduino sketch root

Date: 2026-07-05
Status: accepted

## Context

ADR-0003 splits the firmware into pure, host-tested C logic + thin per-target
glue, and ADR-0005 has each target use its own framework: **X32Link** (ESP32-S3)
builds with **arduino-cli**, **KitchenSync** (ESP32-P4) with **ESP-IDF**. The pure C is
meant to be shared across both targets, unchanged.

A recurring instinct — including from the automated architecture review that
produced ARC-006..010 — is to *co-locate* the shared pure modules into tidy
subdirectories: a `link/` package for the four Link modules (ARC-008), or a
neutral `shared/` root for all of them (ARC-009). The directory name would then
tell the truth about ownership instead of burying shared code inside one target's
folder.

The blocker is the **arduino-cli sketch model**. arduino-cli compiles only the
files in the **sketch root directory** (`X32Link/`), plus a `src/` subfolder with
its own include-path rules. It does **not** recurse into arbitrary subdirectories.
So a file moved to `X32Link/link/` or a top-level `shared/` simply stops being
compiled into the S3 firmware — the one we flash to hardware — producing link
errors, not a warning.

The existing codebase already encodes the workaround: **X32MidiClock** (a second
Arduino sketch) consumes the shared modules by **symlinking each file back into
its own sketch root** (`X32MidiClock/bpm_tracker.c -> ../X32Link/bpm_tracker.c`,
~16 of them). KitchenSync, being ESP-IDF, has no such constraint — it references the
files by path from its `CMakeLists.txt`.

So any "clean move" of a shared module is really: move the real file, then
symlink it back into every Arduino sketch root that needs it, and repoint the
ESP-IDF `CMakeLists.txt` and the host-test `Makefile`. The physical grouping is
undone by the symlink farm the flat-sketch rule forces.

## Decision

Shared pure C modules **physically live in the `X32Link/` sketch root**. Arduino
sketches other than X32Link consume them by **symlinking the file into their own
root**; ESP-IDF targets reference them by relative path from `CMakeLists.txt`;
host tests compile them via `-I../X32Link` and explicit paths in `test/Makefile`.

We do **not** relocate shared modules into per-concept subdirectories (`link/`)
or a neutral `shared/` root. The navigability gain does not justify a symlink
farm in front of the firmware we flash. Navigability is instead served by
documentation (module tables in `AGENTS.md`, header-comment pipelines) and by the
deep-module refactors that *do* pay off — the pure, host-tested seams from
ARC-006 (`ks_form`) and ARC-007 (`beat_source`).

## Consequences

- `X32Link/` contains both S3-specific glue and target-neutral shared logic; the
  directory name under-sells what lives there. `AGENTS.md` documents which
  modules are shared vs S3-only.
- Adding a shared module used by another Arduino sketch means adding a symlink in
  that sketch's root (mirror the X32MidiClock pattern).
- ARC-008 (co-locate `link/`) and ARC-009 (hoist `shared/`) are **declined** on
  this basis; ARC-010 (prune shallow modules) is deferred to the P4-013 swing
  outcome, independent of layout. Future architecture reviews should not
  re-propose relocating shared modules out of the sketch root.
- If the S3 firmware ever migrates off arduino-cli (e.g. to ESP-IDF or a
  PlatformIO library layout), this constraint lifts and the co-location refactors
  become worth revisiting.
