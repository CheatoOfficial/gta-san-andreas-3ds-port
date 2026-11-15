#!/usr/bin/env bash
# Compile the native 3DS layer for BOTH models without a disc, translation or generated game sources.
# This checks SDK/API compatibility; it does not link or run the game.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:/usr/bin:$PATH"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
R="$REPO/games/SLES-52541_v2.01/engine/render"
S="$REPO/games/SLES-52541_v2.01/engine/sound"
picasso -o "$OUT/vshader.shbin" "$R/3ds/vshader.v.pica"
bin2s -a 4 -H "$OUT/vshader_shbin.h" "$OUT/vshader.shbin" > "$OUT/vshader.s"
FLAGS=(-O2 -std=gnu11 -Wall -Wno-unused-function -Wno-unused-variable -Wno-format
       -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -ffunction-sections -fdata-sections
       -fno-fast-math -frounding-math -ffp-contract=off -fno-strict-aliasing -DHLE_3DS
       -DHLE_3DS_CAPTURE_MAX=0 "-I$DEVKITPRO/libctru/include" "-I$OUT"
       "-I$REPO/hle/core" "-I$REPO/hle/3ds" "-I$REPO/runtime" "-I$REPO/audio"
       "-I$R" "-I$R/3ds" "-I$R/psp" "-I$S" "-I$S/3ds")
for MODEL in 0 1; do
  for SRC in "$REPO"/hle/3ds/*.c "$R"/3ds/*.c "$S"/3ds/*.c; do
    arm-none-eabi-gcc "${FLAGS[@]}" "-DHLE_3DS_OLD_MODEL=$MODEL" -c "$SRC" -o "$OUT/$(basename "$SRC").o"
  done
  arm-none-eabi-gcc -march=armv6k -mfloat-abi=hard -c "$REPO/hle/3ds/hle_ctx.S" -o "$OUT/hle_ctx.o"
  echo "PASS native ARM11 compile: old_model=$MODEL (no game data)"
done
