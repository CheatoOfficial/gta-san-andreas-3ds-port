#!/usr/bin/env python3
"""Guard against committing game data or personal information.

Scans the files this port added or changed relative to the pinned PS2RC base commit (every tracked file if git
history is not available) and fails on:
  * disc images, executables, console binaries, captures, logs, archives and other binary/media files,
  * large files (> 1 MB of text),
  * personal paths (C:\\Users\\<name>, /home/<name>, /mnt/c/Users/...), and e-mail addresses other than noreply ones,
  * long hex/base64 blobs (would be embedded data).
Run it before every commit; CI runs it on every push and pull request.
"""
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BASE = "8cd56bb"  # PS2RC 0.2.1 - the upstream commit this port is based on
BAD_EXT = {".iso", ".bin", ".elf", ".3dsx", ".smdh", ".cia", ".nds", ".pbp", ".prx", ".o", ".a", ".so", ".dll", ".exe",
           ".ppm", ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".wav", ".mp3", ".ogg", ".at3", ".vag", ".pmf", ".pss",
           ".mp4", ".zip", ".7z", ".rar", ".gz", ".xz", ".tar", ".img", ".dat", ".pkl", ".log", ".cfg.bak"}
BAD_NAMES = {"session.log", "discmap.txt", "iso_meta.bin"}
PII = [re.compile(p, re.I) for p in (
    r"[A-Za-z]:\\+Users\\+[^\\\s\"']+", r"/mnt/[a-z]/Users/", r"/c/Users/[^/\s\"']+", r"/home/[a-z][a-z0-9_-]+/",
    r"/Users/[A-Za-z][^/\s\"']+/")]
EMAIL = re.compile(r"[\w.+-]+@[\w-]+\.[\w.-]+")
BLOB = re.compile(r"[A-Za-z0-9+/=]{200,}|(?:0x[0-9a-fA-F]{2},\s*){64,}")
ALLOW_PII_FILES = {"3ds-port/scripts/check-no-game-data.py"}


def files():
    def git(*a):
        return subprocess.run(["git", *a], cwd=REPO, capture_output=True, text=True)
    if git("rev-parse", "--verify", "-q", BASE).returncode == 0:
        names = set(git("diff", "--name-only", "--diff-filter=AM", BASE).stdout.split())
        names |= set(git("ls-files", "--others", "--exclude-standard").stdout.split())
    else:
        names = set(git("ls-files").stdout.split())
    return sorted(n for n in names if (REPO / n).is_file())


def main():
    problems = []
    for name in files():
        p = REPO / name
        if p.suffix.lower() in BAD_EXT or p.name in BAD_NAMES or p.name.startswith("SLES_") or p.name.startswith("gpu-frame-"):
            problems.append(f"{name}: not allowed in this repository (binary/media/game-derived file type)")
            continue
        if p.stat().st_size > 1_000_000:
            problems.append(f"{name}: {p.stat().st_size} bytes - too large for a source file")
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="strict")
        except UnicodeDecodeError:
            problems.append(f"{name}: not UTF-8 text")
            continue
        if name in ALLOW_PII_FILES:
            continue
        for rx in PII:
            m = rx.search(text)
            if m:
                problems.append(f"{name}: personal path '{m.group(0)}'")
                break
        for m in EMAIL.finditer(text):
            if "noreply" not in m.group(0).lower() and "example" not in m.group(0).lower():
                problems.append(f"{name}: e-mail address '{m.group(0)}'")
                break
        if BLOB.search(text):
            problems.append(f"{name}: long encoded/hex blob (embedded data?)")
    if problems:
        print("check-no-game-data: FAILED")
        for q in problems:
            print("  -", q)
        return 1
    print(f"check-no-game-data: ok ({len(files())} files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
