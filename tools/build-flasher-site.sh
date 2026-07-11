#!/usr/bin/env bash
# Build every flashable variant and assemble the GitHub Pages web-flasher site.
#
# Deliberately a script, not inline workflow YAML: this is the part with real logic, and
# it must be runnable on a laptop. A release pipeline you can only test by pushing a tag
# is a pipeline you cannot test.
#
#   VERSION=2.3.0 bash tools/build-flasher-site.sh
#
# Design: docs/plans/2026-07-10-github-pages-web-flasher-design.md
set -euo pipefail

VERSION="${VERSION:-dev}"
X32_FQBN="${X32_FQBN:-esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=min_spiffs}"
TOUCH_FQBN="${TOUCH_FQBN:-esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom}"
CORE_CACHE="${CORE_CACHE:-}"
SITE="site"

CACHE_ARG=()
[ -n "$CORE_CACHE" ] && CACHE_ARG=(--build-cache-path "$CORE_CACHE")

rm -rf "$SITE"
mkdir -p "$SITE/firmware"

# boot_app0.bin initialises otadata to boot app0. The core writes it on a normal upload,
# so we write it too. A blank otadata would ALSO boot app0 (the bootloader falls back),
# but relying on fallback is not the same as stating intent -- and this is the file that
# decides which slot a freshly-flashed device runs from.
#
# Ask arduino-cli where its data lives rather than hardcoding a path: it is ~/.arduino15
# on Linux (the CI runner) and ~/Library/Arduino15 on macOS (this laptop). Hardcoding
# either one makes the script un-runnable on the other, which defeats the purpose.
ARDUINO_DATA="$(arduino-cli config get directories.data 2>/dev/null || true)"
[ -n "$ARDUINO_DATA" ] || { echo "FATAL: cannot locate arduino-cli data dir"; exit 1; }
BOOT_APP0="$(find "$ARDUINO_DATA/packages/esp32/hardware/esp32" -name boot_app0.bin 2>/dev/null | head -1)"
[ -n "$BOOT_APP0" ] || { echo "FATAL: boot_app0.bin not found in the installed ESP32 core"; exit 1; }
echo "boot_app0: $BOOT_APP0"

# collect <sketch-dir> <slug> <display name> <blurb>
collect() {
  local sketch="$1" slug="$2" name="$3" blurb="$4"
  local out="$SITE/firmware/$slug"
  local bin_dir="$sketch/build"
  mkdir -p "$out"

  # arduino-cli --export-binaries drops everything under <sketch>/build/<fqbn-slug>/
  local src
  src="$(find "$bin_dir" -maxdepth 1 -type d -name 'esp32.esp32.*' | head -1)"
  [ -n "$src" ] || { echo "FATAL: no exported binaries for $sketch"; exit 1; }

  local app boot parts
  app="$(find "$src" -maxdepth 1 -name '*.ino.bin' ! -name '*merged*' | head -1)"
  boot="$(find "$src" -maxdepth 1 -name '*.bootloader.bin' | head -1)"
  parts="$(find "$src" -maxdepth 1 -name '*.partitions.bin' | head -1)"
  for f in "$app" "$boot" "$parts"; do
    [ -n "$f" ] && [ -f "$f" ] || { echo "FATAL: missing a bin for $slug (app=$app boot=$boot parts=$parts)"; exit 1; }
  done

  cp "$boot"      "$out/bootloader.bin"
  cp "$parts"     "$out/partitions.bin"
  cp "$BOOT_APP0" "$out/boot_app0.bin"
  cp "$app"       "$out/app.bin"

  # ESP Web Tools manifest. chipFamily is the ONE thing the browser can verify by itself:
  # it reads the chip from the ROM bootloader and refuses a mismatched image, so a P4
  # build can never land on an S3. It cannot tell WHICH BOARD an S3 is soldered to --
  # Super Mini and QT Py are silicon-identical -- so board choice stays with the human.
  #
  # ESP32-S3 flash offsets: bootloader 0x0, partitions 0x8000, otadata 0xe000, app 0x10000.
  cat > "$out/manifest.json" <<JSON
{
  "name": "$name",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "bootloader.bin", "offset": 0 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "boot_app0.bin",  "offset": 57344 },
        { "path": "app.bin",        "offset": 65536 }
      ]
    }
  ]
}
JSON

  # Consumed by the page generator below.
  printf '%s\t%s\t%s\n' "$slug" "$name" "$blurb" >> "$SITE/.variants"
  echo "  -> $slug  ($(du -h "$out/app.bin" | cut -f1))"
}

echo "== X32Link (4 board variants) =="
# Same sketch, same FQBN; only build_opt.h differs -- exactly as ci.yml compiles them.
build_x32() {
  local flags="$1" slug="$2" name="$3" blurb="$4"
  printf '%s' "$flags" > X32Link/build_opt.h
  arduino-cli compile --fqbn "$X32_FQBN" "${CACHE_ARG[@]+"${CACHE_ARG[@]}"}" --export-binaries X32Link >/dev/null
  collect X32Link "$slug" "$name" "$blurb"
  : > X32Link/build_opt.h    # restore to empty (the tracked state)
}

build_x32 ''                                                   headless        "X32Link — Headless"                  "Super Mini / XIAO. No display, no NeoPixel."
build_x32 '-DBOARD_WAVESHARE_S3_TOUCH_LCD_147'                 waveshare-touch "X32Link — Waveshare Touch LCD 1.47"  "1.47in touch LCD. On-device config UI."
build_x32 '-DBOARD_QTPY_ESP32S3'                               qtpy            "X32Link — QT Py ESP32-S3"            "Onboard NeoPixel beat indicator."
build_x32 $'-DBOARD_QTPY_ESP32S3\n-DHAS_BATTERY_GAUGE\n'       qtpy-battery    "X32Link — QT Py + LiPo BFF"          "QT Py plus MAX17048 battery gauge."

echo "== KitchenSync Touch =="
# The device that actually ships to a customer. Its own sketch, its own partition table
# (16MB, 4MB dual-OTA slots) -- hence TOUCH_FQBN with PartitionScheme=custom.
arduino-cli compile --fqbn "$TOUCH_FQBN" "${CACHE_ARG[@]+"${CACHE_ARG[@]}"}" --export-binaries KitchenSyncTouch >/dev/null
collect KitchenSyncTouch kitchensync-touch "KitchenSync Touch" \
  "Waveshare Touch LCD 1.47. Ableton Link → DIN MIDI clock + transport."

echo "== page =="
python3 tools/gen_flasher_page.py "$SITE" "$VERSION"
rm -f "$SITE/.variants"

echo "site/ assembled for $VERSION"
