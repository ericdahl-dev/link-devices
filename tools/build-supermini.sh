#!/usr/bin/env bash
# ESP-036: build/flash KitchenSyncTouch for the HEADLESS ESP32-S3 Super Mini variant
# instead of the Waveshare S3 Touch product board.
#
#   tools/build-supermini.sh              # compile only
#   tools/build-supermini.sh /dev/cu.usbmodemXXXX   # compile + upload
#
# The headless variant is the small, cheap clock box: a bare S3 + a DIN/TRS MIDI jack,
# no screen, no buttons (yet). Transport comes entirely from the iOS app over
# /transport + /live. DIN MIDI TX is GPIO11 (din_midi_out.h #else branch).
#
# TWO things differ from the product build, and getting either wrong is nasty:
#
#   config.h        board flag. Waveshare + Super Mini both defined -> #error
#                   (deliberate). This swaps to BOARD_S3_SUPERMINI and restores on exit.
#
#   FQBN            NO PSRAM. The firmware allocates none (no ps_malloc / SPIRAM), and
#                   the bench Super Mini has QUAD (not octal) PSRAM -- passing PSRAM=opi
#                   makes the SDK assert a size mismatch at boot and crash-loop (the same
#                   trap the LoraLink notes hit with FlashSize=16M on an 8 MB board).
#
#   partitions.csv  THE TRAP. Arduino gives a sketch exactly ONE partitions.csv, and it
#                   OVERRIDES the FQBN's PartitionScheme menu. So a Super Mini build
#                   silently inherits the product's 16 MB table unless we swap it -- and
#                   a 16 MB table on this 4 MB chip boot-loops with "partition 2 invalid
#                   ... exceeds flash chip size", no app, no banner. This is exactly what
#                   bit the first flash of the bench Super Mini. So, like build-bench.sh,
#                   this swaps BOTH config.h and partitions.csv and restores both on exit.
#
# FlashSize=4M matches the bench board (ESP32-S3, 4 MB, 2 MB quad PSRAM). A larger Super
# Mini would want a bigger table in partitions_supermini.csv AND a matching FlashSize --
# but 4M is the floor that boots on any of them.
#
# Never leave the swap committed: HEAD must always build the product.
set -euo pipefail

cd "$(dirname "$0")/.."
SKETCH=KitchenSyncTouch
FQBN='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=custom'
PORT="${1:-}"

restore() {
  [ -f /tmp/ks_config_sm.h.bak     ] && mv /tmp/ks_config_sm.h.bak     "$SKETCH/config.h"
  [ -f /tmp/ks_partitions_sm.bak   ] && mv /tmp/ks_partitions_sm.bak   "$SKETCH/partitions.csv"
  echo "restored $SKETCH to the product board (S3 Touch)"
}
trap restore EXIT

cp "$SKETCH/config.h"       /tmp/ks_config_sm.h.bak
cp "$SKETCH/partitions.csv" /tmp/ks_partitions_sm.bak

# Board flag: comment-swap. awk + mv, NOT `sed -i` -- `sed -i ''` is BSD-only and GNU
# sed (CI) reads the '' as a filename and dies (see build-bench.sh for the full story).
awk '
  /^#define BOARD_WAVESHARE_S3_TOUCH_LCD_147/ { print "// " $0; next }
  /^\/\/ #define BOARD_S3_SUPERMINI/          { sub(/^\/\/ +/, ""); print; next }
  { print }
' "$SKETCH/config.h" > "$SKETCH/config.h.tmp" && mv "$SKETCH/config.h.tmp" "$SKETCH/config.h"

grep -q '^#define BOARD_S3_SUPERMINI' "$SKETCH/config.h" || {
  echo "FAIL: could not select BOARD_S3_SUPERMINI in $SKETCH/config.h" >&2; exit 1; }
grep -q '^// #define BOARD_WAVESHARE_S3_TOUCH_LCD_147' "$SKETCH/config.h" || {
  echo "FAIL: the Waveshare flag is still active -- config.h would #error on both" >&2; exit 1; }

cp "$SKETCH/partitions_supermini.csv" "$SKETCH/partitions.csv"
head -1 "$SKETCH/partitions.csv" | grep -q '4 MB' || {
  echo "FAIL: partitions.csv is not the 4 MB Super Mini table -- a 16 MB table boot-loops this chip" >&2; exit 1; }

echo "==> compiling $SKETCH for the headless Super Mini ($FQBN)"
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

if [ -n "$PORT" ]; then
  echo "==> uploading to $PORT"
  arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH"
fi
