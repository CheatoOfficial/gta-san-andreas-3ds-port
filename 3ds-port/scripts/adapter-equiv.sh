#!/usr/bin/env bash
# Randomised equivalence test: the optimised vertex converter (w3d_citro3d_adapter.c) must match the reference
# implementation kept in 3ds-port/tests/adapter-equiv/old_adapter.c (colours within 1 rounding level).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${CC:-gcc}"
R="$REPO/games/SLES-52541_v2.01/engine/render/3ds"
RR="$REPO/games/SLES-52541_v2.01/engine/render"
T="$REPO/3ds-port/tests/adapter-equiv"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
"$CC" -O1 -I"$R" -I"$RR" -Dw3d_citro3d_convert_mesh_range=old_mesh -Dw3d_citro3d_convert_lit_range=old_lit -Dw3d_citro3d_convert_tris=old_tris \
      -c "$T/old_adapter.c" -o "$OUT/old.o"
"$CC" -O1 -I"$R" -I"$RR" "$T/equiv.c" "$R/w3d_citro3d_adapter.c" "$OUT/old.o" -lm -o "$OUT/equiv"
"$OUT/equiv"
