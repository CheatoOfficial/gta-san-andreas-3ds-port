# 3DS port status and measurements

Last updated 2026-11-15. Unless marked otherwise, numbers come from the Azahar emulator (New 3DS mode, JIT, 100% clock, vsync on) on a
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
is tight: linear heap free dips under 1 MB in Azahar), and the Old 3DS family. Nothing here supports "playable".

## Old-model profile (2026-11-15)

`--model old3ds` selects smaller bounded CPU/GPU buffers, a 2 MiB compact texture cache, an 8 MiB linear heap reservation
and modifier controls for consoles without ZL/ZR or a C-stick. The model has a separate object directory and named
artifacts; staging and Azahar also accept `--model old3ds`.

Both profiles have full-build and emulator observations below. All performance figures above predate this profile
and describe New-model observations. Real-hardware memory headroom, controls, audio and speed remain unverified.
See [3DS_OLD_MODEL.md](3DS_OLD_MODEL.md) for the acceptance plan.

**Baseline full builds and emulator runs (`20bee84`, before compact textures):** both models were built from a clean clone
and private local conversion outputs, and each matching release build was run in Azahar for about 4 minutes.

| | New-model build, New 3DS mode | Old-model build, Old 3DS mode |
|---|---|---|
| Linked image (text / BSS) | 11.57 MB / 13.64 MB | 11.57 MB / 6.58 MB |
| Heap at start (normal / linear) | 69,556 / 32,768 KiB | 72,352 / 8,192 KiB |
| Reached | city frames (1680+), same triangle counts as before the merge | city frames (2160+) |
| Texture omissions per sampled frame | 0 | up to 171; texture decodes refused ~43,000 times, linear heap down to ~0.8 MB |
| Frame pacing (between presents + submit) | ~200-340 ms | ~120-270 ms (fewer textures and vertices drawn) |

The Old-model build therefore boots and renders in the emulator, but with heavy texture loss: its 2 MiB texture cache
and 8 MiB linear heap are too small for this scene. Azahar's Old 3DS mode gave the homebrew 72 MiB of normal heap,
which a real Old 3DS homebrew launch may not, so these numbers do not establish real old-hardware memory headroom.

## Compact textures and draw-budget correction (2026-11-15)

Full New release, Old release and Old profile builds linked from private local conversion outputs. The final Old profile
(scripted input/captures, requested 300-second cap) and New release were run in separate fresh portable Azahar profiles,
with the matching hardware mode, JIT, 100% clock and vsync. They ran concurrently for correctness checks; these are not
comparable speed benchmarks. Tested artifact hashes matched the final local build outputs.

| Observation | Old profile / Old mode | New release / New mode |
|---|---:|---:|
| Last sampled world frame | 2040 | 2280 |
| Maximum texture omissions in sampled reports | 0 | 0 |
| Last reported texture decode/refusal failures | 0 | 11 (all pinned-cache refusals) |
| Last reported cache / peak | 1880 / 2048 KiB | 7670 / 7766 KiB |
| Minimum sampled linear heap free | 219 KiB (capture build) | 13,295 KiB |
| Normal / linear heap at startup | 73,276 / 8192 KiB | 69,548 / 32,768 KiB |
| Fatal log entries observed | 0 | 0 |

The earlier Old release reported up to 171 omissions per sampled frame and roughly 43,000 refused decodes. The new Old
profile keeps its 2 MiB limit but uses 512 metadata slots, bounded smaller images and 16-bit texels. World images are up
to 64 pixels / 8 KiB and HUD copies up to 128 pixels / 32 KiB. Local diagnostic captures showed the player, textured
environment, minimap and readable HUD text; no game captures are published here. Detail/color precision is lower than
New-model RGBA8. Old release BSS is now 5,637,424 bytes (about 5.64 MB); its decode scratch alone shrank by 983,040 bytes.

Raster lifecycle/unlock hooks now invalidate cache copies and defer freeing busy GPU snapshots. Header dimension/format
changes also invalidate a hit. New draws can temporarily be refused while an old snapshot is busy; zero sampled omissions
does not mean no transient omission occurred. Writes bypassing these hooks still need investigation if stale images recur.

A separate static-budget bug reserved all remaining frame vertices before returning for a skipped mesh. The budget check
now runs first, leaving capacity for later immediate/lit draws. A synthetic regression fails against the old production
backend and passes after the fix. Earlier inflated input counts must not be treated as fully drawn geometry. Draw-order
budget omission and real vertex/batch limits remain; the correction does not eliminate every skipped mesh.

A fresh emulator profile also exposed a startup path mismatch: staging places the boot ELF at the runtime root, but the
loader searched inside disc/. The loader now follows the staging layout, and both fresh-profile runs passed startup.

No real hardware was tested. Old-mode Azahar still provides more normal heap than a real Old launch may provide, and the
capture build's linear headroom is small. These runs establish neither whole-game playability nor hardware compatibility.

## Universal binary (2026-11-16, PR #3)

The default `--model auto` produces one binary with runtime-sized renderer storage. Its pre-main allocator uses the
remaining process allowance: 32 MiB linear when at least 88 MiB remains, otherwise 8 MiB. APT hardware detection then
selects compact resources on Old hardware; full resources require New hardware, 56 MiB normal and 32 MiB linear heap.
A low-memory New launch keeps New controls with compact resources. Insufficient normal heap stops startup. Forced
Old/New targets remain for regression checks. See [3DS_SETUP.md](3DS_SETUP.md) for artifact names and hardware selection.

Auto profile/release and forced Old/New release builds link. Universal release BSS is 2,061,688 bytes; dynamic renderer
allocations still consume memory after startup. Eleven host executables, eleven selected Python tests, 6,000 adapter
cases and ARM11 compilation for all three targets pass. Startup tests exercise the production allocator with fake SVCs,
including a 64 MiB total / 14 MiB used allowance, query/mapping errors and conservative New fallback.

The exact same final auto diagnostic artifact (SHA-256
`7635f4c1f2c4a898d81e03ce4a093f94d21dab92ff5d3dd1c37b254c879c4e17`) ran for about 291 seconds in each isolated Azahar
hardware mode. These concurrent host runs use scripted input and captures; they are correctness observations, not
comparable performance benchmarks or normal-controller tests.

| Observation | Old mode | New mode |
|---|---:|---:|
| Selected resources | compact | full |
| Normal / linear heap (KiB) | 76,768 / 8,192 | 80,864 / 32,768 |
| Periodic frame records / last sampled frame | 28 / 1,920 | 26 / 1,680 |
| Maximum sampled texture omissions | 0 | 0 |
| Maximum sampled skipped meshes | 153 | 0 |
| Last sampled cumulative decode failures | 0 | 0 |
| Last sampled cache / peak (KiB) | 1,986 / 2,048 | 13,341 / 13,341 |
| Minimum sampled free linear memory (KiB) | 219 | 7,342 |
| Fatal/abort/crash/panic entries in guest log | 0 | 0 |

The normal release artifact (SHA-256
`b3d0b52a121034e2897b29371b7ddd4ba2a47bcac6aa02c5408936e0733da3da`) also booted and rendered in both modes for
about 115 seconds: Old had 24 periodic records through frame 1,440, New 23 through frame 1,320. Neither guest log
had fatal/abort/crash/panic entries. No physical controls or normal-input gameplay was exercised.

Fresh captures through frame 1,720 were inspected privately in both modes: the player, city, minimap and HUD render.
Old geometry omission and detail loss remain visible. Its smaller static draw budget is preserved; zero texture omissions
does not mean full scene coverage. Old-mode Azahar provides a generous normal heap; the allocator fixture and emulator
runs do not prove the complete runtime fits a real Old launch. Compact capture runs have very little linear headroom.
No game captures, raw logs, generated sources or executables are included here. Real hardware, controls, audio, saving
and sustained gameplay remain unfinished.

## Present-pass fast path (2026-11-16)

Each frame, `hle_3ds_gfx_present()` re-checks, near-clips, divides and copies every converted world triangle into the
GPU vertex buffer on the CPU. On the profile route this cost a steady ~2.0 ms per 1,000 world vertices (the "submit"
timer), as much as the whole vertex conversion. Triangles entirely in front of the near plane (almost all of them)
now skip the copy-and-clip step; the finite check is one exact IEEE sum instead of 36 `isfinite()` calls; the
texture UV scale is applied while writing instead of in a second pass over GPU memory. Behaviour is unchanged.

Universal profile build, scripted route, private Azahar profile, New mode, before (`6fed0a1`) and after. The route is
deterministic: matching windows had identical vertex counts.

| Window (static + lit vertices) | Submit before | Submit after |
|---|---:|---:|
| 73,288 | 147.8 ms | 99.3 ms |
| 109,796 | 217.5 ms | 146.0 ms |
| 71,207 | 143.1 ms | 96.4 ms |
| 65,201 | 131.9 ms | 88.6 ms |
| 45,060 | 92.0 ms | 62.0 ms |

That is about 33% less present-pass time (1.35 ms per 1,000 vertices). Old mode on the same binary measured the same
per-vertex cost, 59 sampled frames, 0 texture omissions. Neither mode logged fatal entries. Frame time is still
dominated by conversion plus this pass (roughly 150-450 ms per frame in busy scenes), so the game is still not playable.

## Known issues from the pre-publication review (2026-11-15)

* Fixed: vertex buffers are now written only after the previous frame's GPU work has finished; texture slots used by a
  submitted frame stay protected until that frame completes; stereo audio buffers report frames, not halfwords; the ELF
  loader rejects segments with file size above memory size; palette reads are bounds-checked; raw guest bytes are no
  longer logged. None of these fixes could be validated on real hardware; Azahar does not establish physical GPU timing
  or audible hardware output, so treat GPU/audio behaviour as unproven.
  Measured effect in Azahar (same scripted run, release build): identical triangle and batch counts per frame, same total
  frame time, but 5 texture decodes were refused early in the run (`pinned_fail` 0 -> 5) because the slot was still in
  use by the previous frame, and the CPU vertex build no longer overlaps the GPU's previous frame (it now waits first).
* Raster lifecycle/unlock invalidation is now connected to the 3DS cache with deferred retirement. Writes outside those
  hooks and more complex streaming/lifetime cases still need investigation; the short runs do not prove complete coverage.
* Saving is currently disabled on 3DS: the platform memory-card configuration always reports no card and its filesystem
  operations are stubs. It needs implementation and end-to-end save/load testing, not just a hardware check.
* Open: the converter's `ps2rc/convert/toolchain.py` batch runner is inherited from upstream and was only hardened
  for paths with spaces by quoting; unusual path characters are untested.

## Open work (good first contributions)

Complete the Old-model acceptance checks and current review in [HANDOFF.md](HANDOFF.md) before claiming compatibility.

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
