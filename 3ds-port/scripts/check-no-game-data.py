#!/usr/bin/env python3
"""Guard against committing game data or personal information.

Scans the files this port added or changed relative to the pinned PS2RC base commit (every tracked file if git
history is not available) and fails on:
  * disc images, executables, console binaries, captures, logs, archives and other binary/media files,
  * large files (> 1 MB of text),
  * personal paths (C:\\Users\\<name>, /home/<name>, /mnt/c/Users/...), and e-mail addresses other than noreply ones,
  * long hex/base64 blobs (would be embedded data).
Add --history to also scan every commit made on top of the base (run it before publishing). It is a HEURISTIC safety net,
not proof: it cannot recognise game data it has no pattern for. Run it before every commit; CI runs it too.
"""
import re
import io
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
ALLOW_PII_FILES = {"3ds-port/scripts/check-no-game-data.py", "README.PS2RC.md", "SECURITY.md", "TAKEDOWN.md"}  # the last three are upstream PS2RC's own README (its public contact)


def git(*a):
    return subprocess.run(["git", *a], cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace")


def required_git(*a):
    result = git(*a)
    if result.returncode:
        raise RuntimeError("git " + " ".join(a) + " failed")
    return result.stdout


def split_z(out):
    return [n for n in out.split(chr(0)) if n]


def have_base():
    result = git("cat-file", "-t", BASE)
    return result.returncode == 0 and result.stdout.strip() == "commit"


def files():
    """Files this port added or changed (renames count as additions), NUL-delimited so odd names are safe."""
    if have_base():
        names = set(split_z(git("diff", "-z", "--name-only", "--no-renames", "--diff-filter=AM", BASE).stdout))
        names |= set(split_z(git("ls-files", "-z", "--others", "--exclude-standard").stdout))
    else:
        names = set(split_z(git("ls-files", "-z").stdout))
    return sorted(n for n in names if (REPO / n).is_file())


def scan_text(name, text, problems):
    if name in ALLOW_PII_FILES:
        return
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


def bad_name(name):
    base = name.rsplit("/", 1)[-1]
    return (Path(name).suffix.lower() in BAD_EXT or base in BAD_NAMES or base.startswith("SLES_")
            or base.startswith("gpu-frame-"))


def check_history(problems):
    """Scan actual blobs introduced on every local ref beyond the upstream base, including deleted files."""
    if not have_base():
        problems.append("history: upstream base unavailable; cannot certify history scan")
        return
    objects = {}
    for commit in required_git("rev-list", "--all", "HEAD", "^" + BASE).splitlines():
        entries = split_z(required_git("diff-tree", "--root", "-m", "--no-commit-id", "-r", "-z",
                                       "--raw", "--no-renames", "--diff-filter=AM", commit))
        if len(entries) % 2:
            raise RuntimeError("invalid git history listing")
        for header, name in zip(entries[::2], entries[1::2]):
            oid = header.split()[3]
            objects.setdefault(oid, set()).add(name)
    if not objects:
        return
    # Inspect sizes/types first: never load a committed disc image or oversized blob into memory to reject it.
    metadata = subprocess.run(["git", "cat-file", "--batch-check"], cwd=REPO, capture_output=True,
                              input="".join(oid + "\n" for oid in objects).encode("ascii"), check=True)
    lines = metadata.stdout.splitlines()
    if len(lines) != len(objects):
        raise RuntimeError("incomplete git blob metadata")
    readable = {}
    for (oid, names), line in zip(objects.items(), lines):
        header = line.split()
        if len(header) != 3 or header[0].decode("ascii") != oid or header[1] != b"blob":
            raise RuntimeError("invalid git blob metadata")
        size = int(header[2])
        for name in sorted(names):
            label = f"history {oid[:12]}:{name}"
            if bad_name(name):
                problems.append(f"{label}: binary/media/game-derived file type was committed")
            elif size > 1_000_000:
                problems.append(f"{label}: too large for a source file")
            else:
                readable.setdefault(oid, set()).add(name)
    if not readable:
        return
    batch = subprocess.run(["git", "cat-file", "--batch"], cwd=REPO, capture_output=True,
                           input="".join(oid + "\n" for oid in readable).encode("ascii"), check=True)
    stream = io.BytesIO(batch.stdout)
    for oid, names in readable.items():
        header = stream.readline().split()
        if len(header) != 3 or header[0].decode("ascii") != oid or header[1] != b"blob":
            raise RuntimeError("invalid git blob response")
        size = int(header[2])
        data = stream.read(size)
        if len(data) != size or stream.read(1) != b"\n":
            raise RuntimeError("truncated git blob response")
        for name in sorted(names):
            label = f"history {oid[:12]}:{name}"
            try:
                scan_text(name, data.decode("utf-8", errors="strict"), problems)
            except UnicodeDecodeError:
                problems.append(f"{label}: not UTF-8 text")


def main():
    problems = []
    for name in files():
        p = REPO / name
        if bad_name(name):
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
        scan_text(name, text, problems)
    if "--history" in sys.argv:
        try:
            check_history(problems)
        except (RuntimeError, OSError, subprocess.SubprocessError) as exc:
            problems.append(f"history scan failed: {exc}")
    if problems:
        print("check-no-game-data: FAILED")
        for q in problems:
            print("  -", q)
        return 1
    print(f"check-no-game-data: ok ({len(files())} files checked{', history scanned' if '--history' in sys.argv else ''}; "
          "this is a heuristic, not proof)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
