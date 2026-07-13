#!/usr/bin/env bash
# ESP-025: build/flash KitchenSyncTouch for the BENCH RIG (classic ESP32 DevKit,
# WROOM-32) instead of the S3 Touch product board.
#
#   tools/build-bench.sh              # compile only
#   tools/build-bench.sh /dev/cu.usbserial-0001   # compile + upload
#
# Two tracked files differ between the boards, and getting either one wrong is nasty:
#
#   config.h        board flag. Both defined or neither -> #error (deliberate).
#   partitions.csv  the product's table is a 16 MB S3 layout. On the DevKit's 4 MB chip
#                   the bootloader rejects it and the board boot-loops with no app and
#                   no banner -- it looks bricked.
#
# So this swaps BOTH, builds, and restores BOTH on exit (including on failure or ^C).
# Never leave the swap committed: HEAD must always build the product.
set -euo pipefail

cd "$(dirname "$0")/.."
SKETCH=KitchenSyncTouch
FQBN=esp32:esp32:esp32          # classic ESP32, 4 MB, no PSRAM
PORT="${1:-}"

restore() {
  [ -f /tmp/ks_config.h.bak    ] && mv /tmp/ks_config.h.bak    "$SKETCH/config.h"
  [ -f /tmp/ks_partitions.bak  ] && mv /tmp/ks_partitions.bak  "$SKETCH/partitions.csv"
  echo "restored $SKETCH to the product board (S3 Touch)"
}
trap restore EXIT

cp "$SKETCH/config.h"       /tmp/ks_config.h.bak
cp "$SKETCH/partitions.csv" /tmp/ks_partitions.bak

# board flag: comment-swap
sed -i '' \
  -e 's|^#define BOARD_WAVESHARE_S3_TOUCH_LCD_147|// #define BOARD_WAVESHARE_S3_TOUCH_LCD_147|' \
  -e 's|^// #define BOARD_ESP32_DEVKIT|#define BOARD_ESP32_DEVKIT|' \
  "$SKETCH/config.h"
grep -q '^#define BOARD_ESP32_DEVKIT' "$SKETCH/config.h" || {
  echo "FAIL: could not select BOARD_ESP32_DEVKIT in $SKETCH/config.h" >&2; exit 1; }

cp "$SKETCH/partitions_devkit.csv" "$SKETCH/partitions.csv"

echo "==> compiling $SKETCH for the bench rig ($FQBN)"
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

if [ -n "$PORT" ]; then
  echo "==> uploading to $PORT"
  arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH"
fi
