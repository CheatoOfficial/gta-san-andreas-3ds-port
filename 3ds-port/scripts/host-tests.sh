#!/usr/bin/env bash
# Host-side (x86) unit tests for the 3DS layer: no 3DS toolchain, no game data, no emulator.
# Needs only gcc (or $CC) and a POSIX shell (Linux, WSL, macOS or devkitPro/MSYS2 on Windows).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${CC:-gcc}"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
R="$REPO/games/SLES-52541_v2.01/engine/render"
T="$REPO/tests/cfg"
CF=(-O1 -std=gnu11 -Wall -Wno-unused-function "-I$R" "-I$R/3ds" "-I$REPO/hle/3ds")
fail=0
run() { # name, sources...
  local name="$1"; shift
  if "$CC" "${CF[@]}" "$@" -lm -o "$OUT/$name" 2>"$OUT/$name.err" && "$OUT/$name" >"$OUT/$name.out" 2>&1; then
    echo "PASS  $name"
  else
    echo "FAIL  $name"; sed -n 1,12p "$OUT/$name.err" "$OUT/$name.out" 2>/dev/null; fail=1
  fi
}
run pad_map        -I"$T/stubs" "$T/pad3ds_main.c" "$REPO/hle/3ds/hle_3ds_padmap.c"
run homogeneous    "$T/homogeneous_3ds_main.c"
run texture_layout "$T/texture_3ds_layout_main.c"
run texture_cache -I"$T/3ds_texture_stubs" -I"$R/psp" "$T/texture_3ds_cache_main.c" "$R/3ds/hle_3ds_texture.c" "$R/psp/texconv.c"
run texture_cache_old -DHLE_3DS_OLD_MODEL=1 -I"$T/3ds_texture_stubs" -I"$R/psp" "$T/texture_3ds_cache_main.c" "$R/3ds/hle_3ds_texture.c" "$R/psp/texconv.c"
run backend_world  -I"$T/3ds_backend_stubs" "$T/backend_3ds_world_main.c" "$R/3ds/w3d_citro3d_backend.c" \
                   "$R/3ds/w3d_citro3d_adapter.c" "$T/3ds_backend_stubs/platform_stubs.c"
run backend_old3ds -DHLE_3DS_OLD_MODEL=1 -I"$T/3ds_backend_stubs" "$T/backend_3ds_world_main.c" "$R/3ds/w3d_citro3d_backend.c" \
                   "$R/3ds/w3d_citro3d_adapter.c" "$T/3ds_backend_stubs/platform_stubs.c"
run backend_budget -DHLE_3DS_OLD_MODEL=1 -DW3D_C3D_STATIC_VERT_BUDGET=3 -I"$T/3ds_backend_stubs" \
                   "$T/backend_3ds_budget_main.c" "$R/3ds/w3d_citro3d_backend.c" "$R/3ds/w3d_citro3d_adapter.c" "$T/3ds_backend_stubs/platform_stubs.c"
exit $fail
