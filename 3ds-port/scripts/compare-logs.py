#!/usr/bin/env python3
"""Compare the timing of two session.log files of the same build and route (e.g. Azahar vs a real 3DS).

Prints numbers only (no raw log lines): per 120-frame window, the whole frame (between presents + FrameBegin wait +
submit), the vertex conversion (static / lit), and per profiler window the main-loop ms per iteration; then the
medians of each. Windows are paired in order (the scripted route is deterministic in virtual time, so window N of
both logs covers the same part of the route as long as neither run was cut short).

    python 3ds-port/scripts/compare-logs.py emulator/session.log hardware/session.log
"""
import re
import statistics
import sys

TIMING = re.compile(r"^Citro3D timing \(\d+ frames, ms/frame\): between-presents ([\d.]+) FrameBegin-wait ([\d.]+) "
                    r"submit ([\d.]+) (?:\(world draw calls ([\d.]+)\) )?FrameEnd ([\d.]+)$")
CONVERT = re.compile(r"^Citro3D convert \(\d+ frames, per frame\): static ([\d.]+) ms (\d+) verts \d+ calls \| "
                     r"lit ([\d.]+) ms (\d+) verts \d+ calls$")
PROF = re.compile(r"^PROF window real [\d.]+-[\d.]+ s: (\d+) iterations, ([\d.]+) ms/it")
RES = re.compile(r"3DS resources: old_profile=([01]) new_hardware=([01]) heap_KiB=(\d+) linear_KiB=(\d+)")
LINEAR = re.compile(r"linear_free=(\d+) KiB")
SUSPECT = re.compile(r"fatal|abort|crash|panic|STALL", re.I)


def read(path):
    out = {"frame": [], "convert": [], "prof": [], "res": None, "linear_min": None, "suspect": 0, "lines": 0}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            out["lines"] += 1
            line = line.rstrip("\r\n")
            m = TIMING.match(line)
            if m:
                gap, wait, submit, draw, end = m.groups()
                out["frame"].append((float(gap) + float(wait) + float(submit) + float(end), float(gap), float(wait),
                                     float(submit), float(draw) if draw else None))
                continue
            m = CONVERT.match(line)
            if m:
                out["convert"].append((float(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4))))
                continue
            m = PROF.match(line)
            if m:
                out["prof"].append((int(m.group(1)), float(m.group(2))))
                continue
            m = RES.search(line)
            if m and out["res"] is None:
                out["res"] = tuple(int(x) for x in m.groups())
            m = LINEAR.search(line)
            if m:
                v = int(m.group(1))
                out["linear_min"] = v if out["linear_min"] is None else min(out["linear_min"], v)
            if SUSPECT.search(line):
                out["suspect"] += 1
    return out


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else float("nan")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = read(sys.argv[1]), read(sys.argv[2])
    for name, d in (("A", a), ("B", b)):
        r = d["res"]
        print(f"{name}: {d['lines']} lines; resources old_profile/new_hw/heap/linear = {r}; min linear free "
              f"{d['linear_min']} KiB; lines matching fatal/abort/crash/panic/STALL: {d['suspect']}")
    print("\nwindow  frame ms A -> B (ratio)   wait A/B   submit A/B   static ms A/B (verts)   lit ms A/B (verts)")
    n = min(len(a["frame"]), len(b["frame"]))
    for i in range(n):
        fa, fb = a["frame"][i], b["frame"][i]
        ca = a["convert"][i] if i < len(a["convert"]) else None
        cb = b["convert"][i] if i < len(b["convert"]) else None
        conv = ""
        if ca and cb:
            same = "" if (ca[1], ca[3]) == (cb[1], cb[3]) else "  (vertex counts differ)"
            conv = f"   {ca[0]:6.1f}/{cb[0]:6.1f} ({ca[1]})   {ca[2]:6.1f}/{cb[2]:6.1f} ({ca[3]}){same}"
        ratio = fb[0] / fa[0] if fa[0] else float("nan")
        print(f"{i:5d}  {fa[0]:7.1f} -> {fb[0]:7.1f} ({ratio:4.2f})   {fa[2]:5.1f}/{fb[2]:5.1f}   "
              f"{fa[3]:5.1f}/{fb[3]:5.1f}{conv}")
    busy = [i for i in range(n) if i < len(a["convert"]) and a["convert"][i][1] > 0]
    if busy:
        print(f"\nmedians over {len(busy)} rendering windows: frame {med([a['frame'][i][0] for i in busy]):.1f} -> "
              f"{med([b['frame'][i][0] for i in busy]):.1f} ms; static "
              f"{med([a['convert'][i][0] for i in busy]):.1f} -> {med([b['convert'][i][0] for i in busy if i < len(b['convert'])]):.1f} ms; lit "
              f"{med([a['convert'][i][2] for i in busy]):.1f} -> {med([b['convert'][i][2] for i in busy if i < len(b['convert'])]):.1f} ms")
    m = min(len(a["prof"]), len(b["prof"]))
    if m:
        print(f"profiler windows ({m}): median main-loop {med([x[1] for x in a['prof'][:m]]):.1f} -> "
              f"{med([x[1] for x in b['prof'][:m]]):.1f} ms per iteration")
    if len(a["frame"]) != len(b["frame"]):
        print(f"note: window counts differ ({len(a['frame'])} vs {len(b['frame'])}); pairs beyond the shorter run are omitted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
