#!/usr/bin/env bash
# Build the universal or forced-profile 3DS executable (3DSX) from the PS2RC translation made by `ps2rc3ds.py convert`.
# Normally run through:  python 3ds-port/ps2rc3ds.py build [--mode release|profile]
# Runs under devkitPro's MSYS2 bash on Windows, or any bash on Linux/macOS with devkitPro installed.
#
# MODE=release  clean build: real controller input only, no profiler, no diagnostics, no time limit (the default)
# MODE=profile  developer build: PS2RC profiler, per-frame timing logs, scripted pad route, periodic frame captures,
#               real-time cap (MAX_SECONDS, default 420). Use it for performance work and emulator checks only.
# VERT_BUDGET=N cap static (non-skinned) vertices converted per frame (0 = unlimited; release default 0,
#               profile default 20000). Trades distant detail for speed; see docs/3DS_STATUS.md.
set -euo pipefail

REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
WORK="${WORK:-$REPO/3ds-work/work}"
PKG="${PKG:-$REPO/3ds-work/package/PS2RC_SLES52541}"
DIST="${DIST:-$REPO/3ds-work/dist}"
MODE="${MODE:-release}"
MODEL="${MODEL:-auto}"
EXPLICIT_BUDGET="${VERT_BUDGET+x}"
AUTO_MODEL=0
case "$MODEL" in
  auto) AUTO_MODEL=1; OLD_MODEL=0; BUILD_SUFFIX="-auto"; DEFAULT_TEXTURE_SLOTS=512; DEFAULT_TEXTURE_KB=2048 ;;
  new3ds) OLD_MODEL=0; BUILD_SUFFIX=""; DEFAULT_TEXTURE_SLOTS=384; DEFAULT_TEXTURE_KB=20480 ;;
  old3ds) OLD_MODEL=1; BUILD_SUFFIX="-old3ds"; DEFAULT_TEXTURE_SLOTS=512; DEFAULT_TEXTURE_KB=2048 ;;
  *) echo "MODEL must be auto, new3ds or old3ds" >&2; exit 1 ;;
esac
BDIR="${BDIR:-$REPO/3ds-work/build-3ds$BUILD_SUFFIX}"
JOBS="${JOBS:-4}"
PACK=SLES-52541_v2.01

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export CTRULIB="${CTRULIB:-$DEVKITPRO/libctru}"
[ -x "$DEVKITARM/bin/arm-none-eabi-gcc" ] || [ -x "$DEVKITARM/bin/arm-none-eabi-gcc.exe" ] || {
  echo "devkitARM not found under $DEVKITPRO - run: python 3ds-port/ps2rc3ds.py doctor --fix" >&2; exit 1; }

# PS2RC's build.sh calls `python3`; devkitPro's MSYS2 has none, so use whichever Python is on the PATH.
[ -n "${PYTHON_BIN:-}" ] && export PATH="$PYTHON_BIN:$PATH"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:/usr/bin:$PATH"
if ! command -v python3 >/dev/null 2>&1; then
  SHIM="$(mktemp -d)"; trap 'rm -rf "$SHIM"' EXIT
  PYEXE="$(command -v python || command -v py || true)"
  [ -n "$PYEXE" ] || { echo "No Python on PATH (set PYTHON_BIN to its folder)" >&2; exit 1; }
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$PYEXE" > "$SHIM/python3"; chmod +x "$SHIM/python3"
  export PATH="$SHIM:$PATH"
fi
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) export PS2RC_PATH_STYLE=msys ;; esac

[ -f "$WORK/phase6psp/trans/stats.json" ] || { echo "No translation in $WORK - run the convert step first" >&2; exit 1; }

# Where PS2RC finds the converter's results.
export PS2RC_WORK="$WORK" PS2RC_ANALYSIS="$WORK/analysis" PS2RC_PACK="$PACK" PS2RC_LOCAL="$WORK/local.toml"
export PS2RC_DISC="$PKG/disc"
[ -d "$PS2RC_DISC" ] || export PS2RC_DISC="$WORK/disc"
export GEN_DIR="$WORK/modules"
export BDIR PROFILE=public AUDIO=1 HLE_BUILD="${HLE_BUILD:-100}"

# ---- build options ---------------------------------------------------------------------------------------------
TEXTURE_SLOTS="${HLE_3DS_TEXTURE_SLOTS:-$DEFAULT_TEXTURE_SLOTS}"
TEXTURE_BUDGET_KB="${HLE_3DS_TEXTURE_BUDGET_KB:-$DEFAULT_TEXTURE_KB}"
if [ "$MODEL" = old3ds ] && { [ "$TEXTURE_BUDGET_KB" -gt 2048 ] || [ "$TEXTURE_SLOTS" -gt 512 ]; }; then
  echo "Old 3DS profile supports at most 2048 KiB textures and 512 slots" >&2; exit 1
fi
DEFS="-DHLE_3DS_AUTO_MODEL=$AUTO_MODEL -DHLE_3DS_OLD_MODEL=$OLD_MODEL -DHLE_3DS_SKY_PASS=${HLE_3DS_SKY_PASS:-1}"
# Experimental: HLE_3DS_GPU_CLIP=1 lets PICA near-clip world triangles (see hle_3ds_gpu_clip_rows). Off by default.
DEFS="$DEFS -DHLE_3DS_GPU_CLIP=${HLE_3DS_GPU_CLIP:-0}"
if [ "$MODEL" = auto ]; then
  if [ -n "${HLE_3DS_TEXTURE_SLOTS+x}${HLE_3DS_TEXTURE_BUDGET_KB+x}" ]; then
    echo "Per-model texture overrides require --model old3ds or new3ds" >&2; exit 1
  fi
else
  DEFS="$DEFS -DHLE_3DS_TEX_SLOTS=$TEXTURE_SLOTS -DHLE_3DS_TEX_MAX_BYTES=$((TEXTURE_BUDGET_KB * 1024))u"
fi
DEFS="$DEFS -DHLE_3DS_LIGHTING_DEBUG=0 -DHLE_3DS_DISABLE_DEPTH_DEBUG=0 -DHLE_3DS_DISABLE_CULL_DEBUG=0"
[ "$MODEL" = old3ds ] && VERT_BUDGET="${VERT_BUDGET:-10000}"
case "$MODE" in
  release)
    DEV_PAD=0
    DEFS="$DEFS -DHLE_DEV_PAD_SCRIPTED=0 -DHLE_3DS_CULL_DIAG=0 -DHLE_3DS_RENDER_DIAG=0 -DHLE_3DS_CAPTURE_MAX=0u"
    # the runtime ends a session after HLE_DEFAULT_MAX_SECONDS of real time (default 600): effectively no limit here
    DEFS="$DEFS -DHLE_DEFAULT_MAX_SECONDS=${MAX_SECONDS:-2000000000}"
    VERT_BUDGET="${VERT_BUDGET:-0}" ;;
  profile)
    DEV_PAD=1
    DEFS="$DEFS -DHLE_3DS_PROF=1 -DHLE_DEV_PAD_SCRIPTED=1 -DHLE_3DS_CULL_DIAG=1 -DHLE_3DS_RENDER_DIAG=1"
    DEFS="$DEFS -DHLE_DEFAULT_MAX_SECONDS=${MAX_SECONDS:-420} -DHLE_3DS_CAPTURE_START_FRAME=${CAPTURE_START:-1360} -DHLE_3DS_CAPTURE_MAX=${CAPTURE_MAX:-4} -DHLE_3DS_CAPTURE_INTERVAL=${CAPTURE_INTERVAL:-120}"
    VERT_BUDGET="${VERT_BUDGET:-20000}" ;;
  *) echo "MODE must be release or profile" >&2; exit 1 ;;
esac
if [ "$MODEL" != auto ] || [ -n "$EXPLICIT_BUDGET" ]; then
  DEFS="$DEFS -DW3D_C3D_STATIC_VERT_BUDGET=${VERT_BUDGET}u"
fi
export HLE_DEFS_EXTRA="$DEFS"
echo "build-3ds: MODEL=$MODEL MODE=$MODE VERT_BUDGET=$VERT_BUDGET JOBS=$JOBS BDIR=$BDIR"

mkdir -p "$BDIR/hle" "$DIST"
if [ "$DEV_PAD" = 1 ]; then   # the scripted-pad probe exists only in profile builds
  cp -p "$REPO/3ds-port/tools/hle_dev_pad_probe.c" "$BDIR/hle/hle_dev_pad_probe.c"; touch "$BDIR/hle/hle_dev_pad_probe.c"
else
  rm -f "$BDIR/hle/hle_dev_pad_probe.c" "$BDIR/obj/hle/hle_dev_pad_probe.o"
fi

# The make rules do not track command-line -D flags: drop the objects that depend on them so a mode/option change
# takes effect while the large translated-function object set stays cached.
find "$BDIR/obj" -type f \( -name hle_3ds_main.o -o -name hle_3ds_profile.o -o -name hle_3ds_heaps.o -o -name hle_main.o -o -name hle_sce.o -o -name hle_prof.o \
  -o -name hle_3ds_gfx.o -o -name hle_3ds_texture.o -o -name hle_w3d.o -o -name w3d_citro3d_backend.o \
  -o -name w3d_citro3d_adapter.o -o -name hle_3ds_platform.o -o -name hle_iop_servers.o -o -name audio_3ds.o -o -name hle_dev_pad_probe.o \) \
  -delete 2>/dev/null || true

# The 3DS texture cache reuses PS2RC's PS2-texture converter, which lives in the pack's psp/ source folder
# (mk/build.sh only copies a pack source folder's own 3ds/ subfolder for this target).
for f in texconv.c texconv.h; do
  cp -p "$REPO/games/$PACK/engine/render/psp/$f" "$BDIR/hle/$f"
done

cd "$REPO"
bash mk/build.sh --pack "$PACK" 3ds "$JOBS"

OUT3DSX="$BDIR/ps2rc-sa.3dsx"
[ -f "$OUT3DSX" ] || { echo "build finished but $OUT3DSX is missing" >&2; exit 1; }
NAME="ps2rc-3ds$BUILD_SUFFIX-$MODE.3dsx"
cp -p "$OUT3DSX" "$DIST/$NAME"
( cd "$DIST" && { sha256sum "$NAME" 2>/dev/null || shasum -a 256 "$NAME"; } | tee "$NAME.sha256" )
echo "Built $DIST/$NAME"
