#!/usr/bin/env python3
"""Make a shareable summary of a session.log.

A raw session.log can contain guest memory addresses, instruction words, raster/texture details and other material
derived from the game, so NEVER attach or paste a raw log. This tool prints only whitelisted timing/statistics lines
(frame pacing, vertex-conversion cost, profiler windows, triangle counts) plus a count of lines that look like errors.
Read its output before sharing it.

    python 3ds-port/scripts/summarize-log.py path/to/session.log > summary.txt
"""
import re
import sys

ALLOW = [re.compile(p) for p in (
    r"^Citro3D timing \(\d+ frames, ms/frame\): between-presents [\d.]+ FrameBegin-wait [\d.]+ submit [\d.]+ "
    r"(?:\(world draw calls [\d.]+\) )?FrameEnd [\d.]+$",
    r"^Citro3D lit cache \(\d+ frames, per frame\): \d+ hits \d+ verts \| \d+ misses \d+ verts \| skinned \d+ verts \| "
    r"evicted \d+, no memory \d+, \d+ KiB held$",
    r"^Citro3D GPU lighting \(\d+ frames, per frame\): \d+ batches, \d+ CPU fallbacks \| GPU static \d+ batches, \d+ CPU "
    r"fallbacks, cache \d+ hits \d+ verts / \d+ misses \d+ verts$",
    r"^Citro3D texture decode time: [\d.]+ ms since the last pressure line$",
    r"^Citro3D convert \(\d+ frames, per frame\): static [\d.]+ ms \d+ verts \d+ calls \| lit [\d.]+ ms \d+ verts \d+ calls$",
    r"^Citro3D budget skips \(\d+ frames, per frame\): \d+ static meshes$",
    r"^Citro3D frame \d+: \d+/\d+ triangles, batches=\d+, near-clipped=\d+, near-rejected=\d+, W-dropped=\d+, "
    r"capacity-dropped=\d+, texture omissions=\d+, state simplifications=\d+, PSP segments=\d+, meshes skipped=\d+, adapter error=-?\d+",
    r"^PROF window real [\d.]+-[\d.]+ s: \d+ iterations, [\d.]+ ms/it "
    r"\(idle\.pace [\d.]+, io [\d.]+, instrument ~[\d.]+\)$",
    r"3DS resources: old_profile=[01] new_hardware=[01] heap_KiB=\d+ linear_KiB=\d+",
    r"Citro3D texture pressure frame \d+: slots=\d+/\d+ pinned=\d+ cache=\d+/\d+ KiB peak=\d+ KiB linear_free=\d+ KiB "
    r"decoded=\d+ failures=\d+ badptr=\d+ badpacket=\d+ oversize=\d+ pinned_fail=\d+ noslot=\d+ texinit=\d+ convert=\d+"
    r"(?: scaled=\d+ invalidated=\d+)?",
)]
SUSPECT = re.compile(r"fatal|error|abort|crash|panic|unsupported|BREAK|TRAP", re.I)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    kept, suspects, total = [], 0, 0
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for line in f:
            total += 1
            line = line.rstrip("\r\n")
            if any(rx.fullmatch(line) for rx in ALLOW):
                kept.append(line)
            elif SUSPECT.search(line):
                suspects += 1
    print(f"# summary of {total} log lines: {len(kept)} whitelisted statistics lines")
    print(f"# lines that look like errors or warnings (NOT included, look at your own log): {suspects}")
    print("\n".join(kept))
    return 0


if __name__ == "__main__":
    sys.exit(main())
