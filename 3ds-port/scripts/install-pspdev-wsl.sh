#!/usr/bin/env bash
# Install the pinned pspdev toolchain into ~/pspdev inside WSL2 (or any Linux). PS2RC's converter finds it there
# with `--use wsl`. The version matters: the game pack expects psp-gcc 15.2.0, which is release v20260901.
set -euo pipefail
TAG="${PSPDEV_TAG:-v20260901}"
ASSET="${PSPDEV_ASSET:-pspdev-debian-latest.tar.gz}"
URL="https://github.com/pspdev/pspdev/releases/download/$TAG/$ASSET"
DEST="$HOME/pspdev"
if [ -x "$DEST/bin/psp-gcc" ]; then echo "already installed: $("$DEST/bin/psp-gcc" --version | head -1)"; exit 0; fi
echo "Downloading $URL (about 170 MB) ..."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
curl -fSL --progress-bar --retry 3 -o "$TMP/$ASSET" "$URL"
mkdir -p "$DEST"
tar -xzf "$TMP/$ASSET" -C "$HOME"      # the archive's top directory is pspdev/
[ -x "$DEST/bin/psp-gcc" ] || { echo "unexpected archive layout; expected $DEST/bin/psp-gcc" >&2; exit 1; }
echo "Installed: $("$DEST/bin/psp-gcc" --version | head -1)"
sha256sum "$TMP/$ASSET" | tee "$DEST/.installed-from.sha256"
