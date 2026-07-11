#!/usr/bin/env python3
"""Validate every generated ESP Web Tools manifest BEFORE the site is published.

A broken manifest is not a build failure -- it is a device that bricks halfway through a
flash in someone's living room, with no serial console and no idea why. The whole point of
running this in the release job is to fail on the runner instead.

Checks, per variant:
  - manifest is well-formed JSON with the fields ESP Web Tools actually reads
  - every referenced .bin exists and is non-empty
  - offsets are the exact ESP32-S3 quartet: 0x0 / 0x8000 / 0xe000 / 0x10000
  - parts are ordered by offset and do not OVERLAP -- an app that starts before the
    partition table ends would silently corrupt the table it was flashed with
  - the app fits the app slot declared in that variant's own partitions.bin

The last check is the one that matters most and is the easiest to get wrong: KitchenSync
Touch ships a 4MB-slot table while X32Link ships min_spiffs (1.9MB). Building the Touch
under the wrong PartitionScheme silently yields a 1.25MB slot -- the build SUCCEEDS and the
mistake only surfaces when a bigger image later refuses to fit, or worse, overruns.

Usage: validate_manifests.py <site/firmware>
"""
import json
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1])
EXPECTED_OFFSETS = [0x0, 0x8000, 0xE000, 0x10000]
errors: list[str] = []


def app_slot_size(partitions_bin: Path) -> int:
    """Smallest app partition in a partitions.bin (32-byte entries, 0xAA50 magic)."""
    raw = partitions_bin.read_bytes()
    sizes = []
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if len(e) < 32 or e[:2] != b"\xaa\x50":
            continue
        ptype, _sub = e[2], e[3]
        _off, size = struct.unpack("<II", e[4:12])
        if ptype == 0:      # app
            sizes.append(size)
    return min(sizes) if sizes else 0


variants = sorted(p for p in root.iterdir() if p.is_dir())
if not variants:
    sys.exit("FATAL: no variants found -- the build produced nothing to publish")

for v in variants:
    mf = v / "manifest.json"
    if not mf.exists():
        errors.append(f"{v.name}: no manifest.json")
        continue
    try:
        m = json.loads(mf.read_text())
    except json.JSONDecodeError as e:
        errors.append(f"{v.name}: manifest is not valid JSON ({e})")
        continue

    for field in ("name", "version", "builds"):
        if field not in m:
            errors.append(f"{v.name}: manifest missing '{field}'")
    if not m.get("builds"):
        errors.append(f"{v.name}: manifest has no builds")
        continue

    build = m["builds"][0]
    if not build.get("chipFamily"):
        # Without this the browser cannot refuse a mismatched chip -- the one guard we get for free.
        errors.append(f"{v.name}: build has no chipFamily")

    parts = build.get("parts", [])
    offsets = [p["offset"] for p in parts]
    if offsets != EXPECTED_OFFSETS:
        errors.append(f"{v.name}: offsets {[hex(o) for o in offsets]} != "
                      f"expected {[hex(o) for o in EXPECTED_OFFSETS]}")

    prev_end = -1
    for p in parts:
        f = v / p["path"]
        if not f.exists():
            errors.append(f"{v.name}: manifest references missing file {p['path']}")
            continue
        size = f.stat().st_size
        if size == 0:
            errors.append(f"{v.name}: {p['path']} is empty")
        start = p["offset"]
        if start < prev_end:
            errors.append(f"{v.name}: {p['path']} at {hex(start)} OVERLAPS the previous part "
                          f"(which ends at {hex(prev_end)})")
        prev_end = start + size

    # Does the app actually fit the slot in the table it ships WITH?
    pbin, abin = v / "partitions.bin", v / "app.bin"
    if pbin.exists() and abin.exists():
        slot = app_slot_size(pbin)
        app = abin.stat().st_size
        if slot == 0:
            errors.append(f"{v.name}: partitions.bin declares no app partition")
        elif app > slot:
            errors.append(f"{v.name}: app is {app} bytes but the app slot is only {slot} "
                          f"-- this image CANNOT boot")
        else:
            print(f"  {v.name:<22} app {app:>8,} / slot {slot:>9,}  ({100*app/slot:.0f}% used)")

if errors:
    print("\nMANIFEST VALIDATION FAILED:\n", file=sys.stderr)
    for e in errors:
        print(f"  - {e}", file=sys.stderr)
    sys.exit(1)

print(f"\n{len(variants)} variants validated")
