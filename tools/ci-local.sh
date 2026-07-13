#!/usr/bin/env bash
# Run what GitHub CI runs, locally, before you push.
#
#   tools/ci-local.sh              # everything
#   tools/ci-local.sh fast         # host tests + docs only (~20 s)
#   tools/ci-local.sh arduino      # host tests + docs + every arduino-cli board variant
#   tools/ci-local.sh idf          # host tests + docs + KitchenSync on all three chips
#
# WHY THIS EXISTS
#
# The esp32s3 CI leg failed with exit 2 on a change that built perfectly on this laptop.
# idf.py writes `sdkconfig` pinned to the target it built, KitchenSync/sdkconfig is TRACKED
# (the P4's), and CI runs all three targets back-to-back IN ONE WORKSPACE. Locally I cleaned
# between targets by hand and never saw it. CI was the only thing reproducing the real
# sequence -- so the feedback loop was a push and a four-minute wait.
#
# THE DRIFT PROBLEM, AND WHY THIS SCRIPT READS ci.yml
#
# A hand-copied local mirror rots the moment someone adds a board to the workflow, and then it
# lies -- green locally, red on push, which is worse than having no script at all. So the board
# matrix and the IDF targets are PARSED OUT OF .github/workflows/ci.yml at run time. Add a leg
# to CI and it appears here for free. If the parse ever comes up empty, this FAILS rather than
# quietly testing nothing.
set -uo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
WF=.github/workflows/ci.yml
MODE="${1:-all}"

RED=$'\e[31m'; GRN=$'\e[32m'; YLW=$'\e[33m'; DIM=$'\e[2m'; RST=$'\e[0m'
FAILED=()
step() { printf "\n${YLW}==>${RST} %s\n" "$1"; }
ok()   { printf "  ${GRN}PASS${RST}  %s\n" "$1"; }
bad()  { printf "  ${RED}FAIL${RST}  %s\n" "$1"; FAILED+=("$1"); }

# Restore anything the board-flag / partition juggling touches, however we exit.
restore() {
  git checkout -- X32Link/build_opt.h 2>/dev/null || true
  git checkout -- KitchenSyncTouch/config.h KitchenSyncTouch/partitions.csv 2>/dev/null || true
  git checkout -- LoraLink/build_opt.h 2>/dev/null || true
  rm -rf KitchenSync/build KitchenSync/sdkconfig 2>/dev/null || true
  git checkout -- KitchenSync/sdkconfig 2>/dev/null || true
}
trap restore EXIT

# ---------------------------------------------------------------- host + docs
step "Host logic tests (Unity)"
if (cd test && make all >/dev/null 2>&1); then
  f=0; n=0
  for t in $(ls test | grep -E '^test_[a-z_0-9]+$' | grep -v '\.'); do
    n=$((n+1))
    (cd test && ./"$t" 2>&1 | tail -2 | head -1 | grep -q "0 Failures") || { bad "host suite: $t"; f=$((f+1)); }
  done
  [ "$f" -eq 0 ] && ok "$n suites, 0 failures"
else
  bad "host tests did not build"
fi

step "X32 emulator seam tests"
(cd tests && make run >/dev/null 2>&1) && ok "emulator seam tests" || bad "emulator seam tests"

step "Doc staleness guard"
./test/check_docs.sh >/dev/null 2>&1 && ok "check_docs.sh" || bad "check_docs.sh"

[ "$MODE" = "fast" ] && { printf "\n${DIM}(fast mode: firmware compiles skipped)${RST}\n"; }

# ------------------------------------------------------- arduino board matrix
run_arduino() {
  command -v arduino-cli >/dev/null || { bad "arduino-cli not installed"; return; }

  # Parse the matrix out of the workflow. Never hand-maintain this list.
  local legs
  legs=$(python3 - "$WF" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
m = re.search(r'firmware-compile:.*?strategy:.*?include:\n(.*?)\n    env:', s, re.S)
if not m: sys.exit("PARSE FAILED: could not find the firmware-compile matrix")
legs = re.findall(
    r'-\s*name:\s*(.+?)\n\s*sketch:\s*(\S+)\n\s*fqbn:\s*(\S+)\n\s*flags:\s*"(.*?)"',
    m.group(1))
if not legs: sys.exit("PARSE FAILED: matrix present but no legs parsed")
for name, sketch, fqbn, flags in legs:
    print("\t".join([name.strip(), sketch, fqbn, flags]))
PY
) || { bad "could not parse the CI board matrix (the script would otherwise test NOTHING)"; return; }

  while IFS=$'\t' read -r name sketch fqbn flags; do
    [ -z "$name" ] && continue
    step "Compile $name"
    : > "$sketch/build_opt.h"
    for f in $flags; do echo "$f" >> "$sketch/build_opt.h"; done
    if arduino-cli compile --fqbn "$fqbn" "$sketch" >/dev/null 2>&1; then ok "$name"; else bad "$name"; fi
    git checkout -- "$sketch/build_opt.h" 2>/dev/null || rm -f "$sketch/build_opt.h"
  done <<< "$legs"

  step "Compile KitchenSyncTouch (ESP-025 bench rig, classic ESP32)"
  if tools/build-bench.sh >/dev/null 2>&1; then ok "bench rig"; else bad "bench rig"; fi
  # The CI job also asserts build-bench.sh put HEAD back. So do we.
  grep -q '^#define BOARD_WAVESHARE_S3_TOUCH_LCD_147' KitchenSyncTouch/config.h \
    && head -1 KitchenSyncTouch/partitions.csv | grep -q '16 MB' \
    && ok "build-bench.sh restored HEAD" || bad "build-bench.sh left the board swapped"

  # LoraLink: CI provides lora_secrets.h from the .example (it is gitignored, and unlike
  # KitchenSyncTouch this sketch has no __has_include fallback -- it hard-fails without it),
  # then compiles BOTH roles. The receiver only exists behind LORA_ROLE_OVERRIDE, so without
  # its own compile that half of the firmware is never built.
  local lfq
  lfq=$(awk '/^  loralink-compile:/{f=1} f && /FQBN:/{sub(/.*FQBN: */,""); print; exit}' "$WF")
  [ -z "$lfq" ] && { bad "could not parse LoraLink FQBN from $WF"; return; }

  local made_secrets=0
  if [ ! -f LoraLink/lora_secrets.h ]; then
    cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h
    made_secrets=1        # never clobber a real one; only clean up what we created
  fi

  step "Compile LoraLink (sender role)"
  arduino-cli compile --fqbn "$lfq" LoraLink >/dev/null 2>&1 && ok "LoraLink (sender)" || bad "LoraLink (sender)"

  step "Compile LoraLink (receiver role)"
  echo '-DLORA_ROLE_OVERRIDE=LORA_ROLE_RECEIVER' > LoraLink/build_opt.h
  arduino-cli compile --fqbn "$lfq" LoraLink >/dev/null 2>&1 && ok "LoraLink (receiver)" || bad "LoraLink (receiver)"
  git checkout -- LoraLink/build_opt.h 2>/dev/null || rm -f LoraLink/build_opt.h
  [ "$made_secrets" = "1" ] && rm -f LoraLink/lora_secrets.h
}

# ------------------------------------------------------------- ESP-IDF targets
run_idf() {
  [ -f "$HOME/esp/esp-idf/export.sh" ] || { bad "ESP-IDF not found at ~/esp/esp-idf"; return; }

  # Same source of truth: whatever targets CI compiles, we compile.
  local targets
  targets=$(grep -oE 'target: (esp32[a-z0-9]*)' "$WF" | awk '{print $2}' | sort -u)
  [ -z "$targets" ] && { bad "could not parse the IDF targets out of $WF"; return; }

  # shellcheck disable=SC1090
  source "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1

  for t in $targets; do
    step "Compile KitchenSync for $t"
    # THE BUG THIS SCRIPT EXISTS FOR: idf.py leaves an sdkconfig pinned to the last target,
    # and KitchenSync/sdkconfig is tracked (the P4's). Reuse the directory and CMake reads a
    # config for the wrong chip and dies. CI runs all three back-to-back; so do we.
    rm -rf KitchenSync/build KitchenSync/sdkconfig
    if (cd KitchenSync && idf.py set-target "$t" >/dev/null 2>&1 && idf.py build >/dev/null 2>&1); then
      ok "KitchenSync ($t)"
    else
      bad "KitchenSync ($t)"
    fi
  done
  rm -rf KitchenSync/build KitchenSync/sdkconfig
  git checkout -- KitchenSync/sdkconfig 2>/dev/null || true
}

case "$MODE" in
  fast)    ;;
  arduino) run_arduino ;;
  idf)     run_idf ;;
  all)     run_arduino; run_idf ;;
  *)       echo "usage: $0 [all|fast|arduino|idf]" >&2; exit 2 ;;
esac

# ------------------------------------------------------------------- verdict
printf "\n────────────────────────────────────────\n"
if [ ${#FAILED[@]} -eq 0 ]; then
  printf "${GRN}ALL GREEN${RST} (mode: %s)\n" "$MODE"
  exit 0
fi
printf "${RED}%d FAILED${RST} (mode: %s)\n" "${#FAILED[@]}" "$MODE"
for f in "${FAILED[@]}"; do printf "  - %s\n" "$f"; done
exit 1
