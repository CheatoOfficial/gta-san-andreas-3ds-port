# 3DS port status and measurements

Last updated 2026-11-15. All numbers come from the Azahar emulator (New 3DS mode, JIT, 100% clock, vsync on) on a
Windows PC, using a scripted pad route. **Nothing has been run on real hardware.** Emulator timing of float-heavy
code is probably pessimistic compared with a real ARM11 with VFP.

## What works (emulator)

* The translated game boots, passes the intro, reaches the city and runs its script.
* Citro3D renderer: textured world, HUD, radar, characters, vehicles, fog; lit/skinned geometry on the CPU.
* A scripted pad route moves the player (position changes are logged by the dev-pad probe).
* The IOP sound-driver model runs in lockstep (Azahar reports an NDSP error, so audio output is unverified).

## Setup reproduced from a clean clone (2026-11-15)

A fresh clone of this repository, on a Windows 11 PC with devkitPro and WSL2, ran: `verify-disc`, `convert --use wsl`
(analysis, translation, generated modules, staged data: all steps passed), `build` (release 3DSX, 11.66 MB), and
`azahar`: the release build booted in Azahar and drew frames (for example 34,542 triangles in a city frame) with no
fatal errors in the log. This found and fixed four setup bugs (xargs quoting under WSL, missing PATH for devkitPro's
shell, a missing texture-converter source, a diagnostic branch that broke release builds). Not reproduced: Linux/macOS,
Docker mode on this PC (Docker Desktop was not running), `doctor --fix` on a machine without devkitPro.

## Performance (the blocker)

![Main loop time per iteration in Azahar](img/performance.svg)

| Step | Between presents | Main loop / iteration |
|---|---|---|
| Baseline | ~336 ms (~3 fps) | 330-650 ms |
| Vertex-converter optimisation (strip cache, hoisted lighting maths): 231 -> 128 ms per 3D frame in the hook | ~335 ms | ~347 ms |
| Static-vertex budget 20,000/frame (`--vert-budget 20000`) | 228-246 ms (~4 fps) | 175-275 ms |

* The game targets 25 fps (40 ms per iteration). Game logic is small (~35 ms/iteration).
* Largest remaining cost: two guest zones (`render_scene` and `camera_show`, ~120-320 ms and ~140-240 ms self time per
  iteration). They have no zoned children yet; the cause is not attributed.
* Citro3D submission is cheap (FrameBegin wait ~8-14 ms, submit 2-25 ms, FrameEnd 0.4 ms); the time is between presents.
* CPU vertex conversion: static ~0.8 us/vertex (40-65k vertices/frame uncapped), lit/skinned ~1.6 us/vertex.
  That is ~650 cycles per simple transform, implausibly high for real hardware, which is why we want a hardware log.
* Frame time also worsens over a run (e.g. 124 -> 718 ms between presents); cause unknown.
* The vertex budget skips whole meshes in draw order (not by distance); the one capture checked still looked intact.
* The Azahar-only draw-distance/detail preset (`ps2rc.cfg`) did not improve frame time.

## Not verified

Physical button input, audio output, sustained gameplay, missions, saving, real-hardware speed and memory (the game
is tight: linear heap free dips under 1 MB in Azahar), and the old New 3DS. Nothing here supports "playable".

## Open work (good first contributions)

1. **Get a hardware profile log** ([3DS_HARDWARE_TEST.md](3DS_HARDWARE_TEST.md)). It decides what comes next.
2. Attribute the `render_scene` / `camera_show` self time (add profiler zones around the HLE hooks, flush, state and
   draw logic in `games/SLES-52541_v2.01/engine/render/hle_w3d*.c` and `3ds/`).
3. If hardware is also slow: move the static-mesh transform onto the PICA vertex shader (per-draw uniform matrices,
   cached float vertex buffers, strip draws) and reduce per-batch state work.
4. Distance/priority-aware budget (player and vehicles first, then the map by distance) and a budget for lit geometry.
5. Find why frame time grows during a run (suspects: streaming/emergency memory paths).
6. Verify controls on hardware, then audio (NDSP) and memory headroom.

## Ideas reviewed and not used

* github.com/sammwyy/gtasands (a Nintendo DS demake): different approach, no license; only the idea of an explicit
  per-frame render budget was borrowed.
* REGTA 3DS port: only its button-mapping/texture-byte-order conventions were observed; no code reused.
