# Codex / Claude Code handoff

Updated 2026-11-16. Read `AGENTS.md` first. This is a source-only handoff.

## Current state

PRs #1-#7 are merged; `main` is `39a9571` (GPU clipping with the PR #7 fog fallback, switch still off by default).
Current continuation: `claude/shader-fog-uv`, based on Codex's local `codex/gpu-lighting-probe` docs commits
(`a1f27e1`..`a34b35d`, never pushed separately). It moves world fog/UV scale into the shader and adds Codex's
game-free GPU lighting probe. See "Claude: shader-side fog and UV scale" at the end.
The user authorized suitable private conversion/build workspaces for local full builds and emulator checks.
The repository remains public; no game data, generated game source, binaries, raw logs or captures belong in git.
LICENSE and NOTICE.md remain identical to upstream `8cd56bb`.
Neither console is established as playable. Hardware, physical controls, audio and sustained gameplay remain unverified.
Saving is disabled by the memory-card configuration and filesystem stubs; this pass does not implement saving.

## Universal runtime (merged PR #3)

- `hle_3ds_heaps.c` overrides libctru's weak pre-main allocator for the auto target. It queries remaining process commit
  allowance without libc or services, reserves 32 MiB linear only when at least 88 MiB remains, otherwise 8 MiB, and
  gives normal memory the remainder (capped at 96 MiB). Failed queries/mappings stop startup. It does not guess hardware.
- After APT identifies hardware, `hle_3ds_profile.c` chooses immutable renderer capacities before allocation. Old hardware
  always uses compact resources. New hardware uses full resources only with at least 56 MiB normal and 32 MiB linear;
  otherwise it uses compact resources while retaining New controls. Startup requires 40 MiB normal for compact resources.
- CPU vertices, batches, overlay arrays, texture slots and decode scratch are allocated for the selected profile. GPU buffers
  and texture policies preserve PR #2's capacities/formats and deferred invalidation. Shutdown waits for queued GPU work
  before releasing images/buffers; partial initialization frees allocated storage. Never change profiles with live storage.
- The default build/staging model is now `auto`, with distinct `ps2rc-3ds-auto-<mode>.3dsx` artifacts and `build-3ds-auto`
  objects. Forced `old3ds`/`new3ds` targets remain for regressions. Old legacy filenames cannot be mistaken for auto artifacts.
- `azahar --hardware old3ds|new3ds` selects hardware independently from the artifact target. Auto defaults to New mode.
  Auto rejects per-model texture budget overrides; use a forced target for those. Explicit static budgets work on all targets.
- Tests exercise heap alignment/limits, production startup mappings and failure handling, generous Old allowances,
  low-memory New fallback/controls, both production texture policies in one executable, and artifact selection.

## Verification

- Eleven host executables, eleven selected Python tests and 6,000 adapter equivalence cases pass.
- ARM11 native-source compilation passes for auto and both forced targets. The existing posix_memalign nonnull warning remains.
- Full translated-game auto profile/release and forced Old/New release builds link in the authorized private workspace.
  Universal release BSS is 2,061,688 bytes; moving arrays out of BSS does not remove their runtime memory cost.
- Final same-binary emulator results are in the newest section of `docs/3DS_STATUS.md`. They are scripted diagnostic runs,
  not normal-controller play or a performance comparison. Both emulator modes were run concurrently on the host.
- The final normal release artifact also booted/rendered in both modes for about 115 seconds; no input was exercised.
- Raw logs and captures remain private. Only reviewed numeric summaries may be shared. Hardware is still untested.

## Next work for Claude

1. (Done: #4-#7 landed.) Next renderer step is zero-copy world vertices; see the last Claude section.
2. Test real Old/New memory and normal release controls. Old-mode Azahar grants roughly 75 MiB normal heap here; a real
   Old launch may grant much less. The synthetic 64 MiB total-budget allocator test is not a hardware-fit test. Compact
   capture runs can have only a few hundred KiB linear headroom; test sustained texture streaming and allocation failures.
3. Attribute render_scene/camera_show guest self time and optimize measured conversion bottlenecks. Preserve New quality.
   Static mesh omission is still draw-order based; add a distance/priority policy separately with comparable scene evidence.
4. Implement and test memory-card save/load support, then validate audio and longer play/streaming. Check texture writes
   that bypass raster lifecycle/unlock hooks if stale images appear. Keep the full 32 MiB guest address window.

## Claude update (2026-11-16)

PR #3 was reviewed and merged as `6fed0a1` after a clean-clone universal release ran in both Azahar modes (Old chose
compact, New chose full; 0 texture omissions, no fatal entries). Then `claude/present-fast-path` cut the CPU present
pass by about 33% with identical output; numbers are in `docs/3DS_STATUS.md` ("Present-pass fast path").

Recommended next performance step (larger, not started): move the world transform to the PICA vertex shader. Today the
CPU computes world x projection, near clip, perspective divide and fog for every triangle vertex, and expands strips to
three vertices per triangle. Sending object-space positions with a per-batch matrix uniform lets the GPU transform and
clip (the PICA clips in hardware), and an index buffer avoids strip expansion. Risks to check: GE reversed depth and the
current OrthoTilt/DepthMap mapping, fog in view space, and per-batch uniform/command-buffer cost for 700+ batches.
Do it behind a build switch first and compare captures privately with the CPU path. Lit colour could follow later.

Smaller leads from the same profile: `hle.memtrap` sampled up to ~20 ms per iteration in busy windows; lit conversion
(~1.5 us per output vertex in Azahar) recomputes per-call lighting constants per vertex.

## Codex continuation after the present fast path

Independent PR #4 source review found no confirmed defect. A private synthetic harness compiled its actual helpers
with strict float flags: 100,000 old/new finite classifications agreed, and 11,034 fully-inside triangles preserved
the clipper's output ordering/attributes. This is host evidence, not PICA/hardware validation.

PR #5 prepares invariant lit-material terms once per conversion call. It preserves the directional arithmetic order,
all skinning/transform work and all draw/detail policies. No cross-call cache is introduced: every material/light-state
change gets a fresh prepared value. See `docs/3DS_STATUS.md` for the New-mode comparison (about 10% less lit-conversion
time, identical workloads, four byte-identical captures). Do not describe this as a 10% total-frame speedup.

Verification: thirteen host executables (including normal/debug exact lighting tests), eleven selected Python tests,
6,000 existing adapter equivalence cases, and 6,000 extra bitwise comparisons against the immediate pre-change converter.
ARM11 compilation passes for auto/forced Old/forced New, with the existing posix_memalign nonnull warning. The full
universal profile and release builds link; the release was not runtime-tested in this pass. All game-derived evidence stays local and private.

The same candidate profile binary ran sequentially in New and Old modes. Each recorded 28 periodic frame lines through
1,920, zero sampled texture omissions/adapter errors, and no fatal/abort/crash/panic guest-log entries. Old retains its
existing geometry budget (up to 153 skipped meshes sampled); the fresh frame-1,720 city/HUD capture was inspected.
Old is a regression run, not a before/after speed comparison. Only the recorded, identity-checked private emulator PIDs
were stopped. No regular emulator profile or real SD was changed.

Claude: independently review the prepared lighting state/rounding and run the tests, then review both PRs before merging.
The next larger performance step remains a switchable GPU transform/clip prototype with independent depth/fog probes;
no production GPU transform was attempted here. Material constants are now prepared, but per-vertex normalisation,
skinning, transforms, clip checks and strip expansion remain on the CPU. Saving, audio, normal controls, sustained play
and hardware memory/performance still need work. Preserve private process identity checks during any emulator reruns.

## Claude: GPU near-clip prototype (PR #6, stacked on PR #5)

Claude reviewed PR #5 (same expression order, debug/null-light paths unchanged; full tests pass; the adapter
equivalence rounding count is identical to PR #4's: 537,190). Merging #4/#5 was blocked by Claude's permission mode,
so the user merges them.

`claude/gpu-clip-prototype` adds `HLE_3DS_GPU_CLIP` (default 0): world vertices go to PICA as raw clip coordinates and
the GPU does the near clip. In Azahar it cut present-pass time ~54% in both modes with near-identical captures,
including a frame with 11 near-plane crossings (numbers in `docs/3DS_STATUS.md`). Before making it the default:
review the depth/clip derivation in `hle_3ds_homogeneous.h`, and accept that beyond-far geometry is clipped, not
clamped. Real PICA clipping behaviour still needs hardware evidence.

Next steps, in order of measured cost on the busy window (~337 ms/frame with the switch on):
1. Lit conversion (~125 ms, ~1.4 us per output vertex): normalisation, skinning and strip expansion on the CPU.
   Candidates: compute lighting in the vertex shader from object-space normals, or cache per-input-vertex results.
2. Present pass (~67 ms): make it zero-copy. The adapter could write `Hle3dsVertex` straight into a double-buffered
   linear buffer, with fog and UV scale as per-batch shader uniforms. Linear memory is tight on Old (8 MiB).
3. Game-side time between presents not explained by conversion (~120 ms).

## Codex: fogged near crossings (PR #7, stacked on #6)

Found a confirmed source/math mismatch: PR #6 computes fog before clipping, but the reference computes it at generated
vertices. Because the shader builds projective coordinates from visibility(view_z)*W, interpolation can give roughly
half visibility at a near-plane edge where the reference gives full visibility. The new synthetic regression uses
positive W and non-degenerate side-plane-inside geometry; no game input is required to reproduce the calculation.
See `docs/3DS_STATUS.md` for the formula, limits and emulator observations.

Fogged crossing triangles now share the reference CPU clipper, recompute fog and pass raw clip coordinates through
the GPU projection. Non-crossing and unfogged triangles stay on the GPU path. No default switch, depth mapping, shader,
texture policy or buffer budget changed. The prototype's far-plane/clamp difference remains open. Its clipping counters
now describe CPU fallback work; GPU submissions are not a count of final post-hardware-clipping triangles.

Verification: fourteen host executables, eleven selected Python tests, 6,000 adapter cases; native ARM compilation
with off/on GPU paths for all targets; full on/off profile and default release links. The same on binary ran in both
isolated emulator modes concurrently, with zero sampled texture omissions/adapter errors, no fatal/crash entries,
and 11 CPU fallback crossings at frame 1,920 in each mode. Fresh captures target 1,900..1,930 using an ignored build
script copy; public capture defaults are unchanged. No hardware, release controls, audio or saving was tested.
The old performance table predates this fix. Run a separate matched benchmark before asserting its exact speedup.

A fresh New-mode CPU-reference run also completed the four near captures. They match the previous CPU reference
byte-for-byte. Fixed prototype vs fresh reference: 3/0/8/1 changed pixels at 1,900/1,910/1,920/1,930; maximum channel
delta one. The near-camera wall at 1,920 was inspected privately. The reference log has zero sampled texture
omissions/adapter errors and no fatal/crash entries. These are limited scene checks; the extreme fog-gradient issue
is established by the synthetic shader-coordinate case. All owned emulator processes were stopped after verifying
recorded PID/path/start time. No normal emulator profile or physical SD was changed.

Claude: review the crossing predicate, the unchanged reference clipping/interpolation, fan capacity checks, and raw
coordinates after CPU fog generation. Keep the prototype off by default. Repeat a game-free synthetic GPU fog probe
on real hardware if possible, then boundary/depth/far-geometry tests before enabling it. GPU lighting/caching and
zero-copy work are still next performance candidates; this pass focused on correctness of the proposed clipping path.
All private processes must be stopped by verified recorded identity, not by executable name.

## Reproduction and ownership

Set PS2RC3DS_WORK to an explicitly authorized private conversion workspace. Do not share cached objects across targets.

```bash
python 3ds-port/ps2rc3ds.py test
bash 3ds-port/scripts/native-checks.sh
python 3ds-port/ps2rc3ds.py build
python 3ds-port/ps2rc3ds.py build --mode profile --max-seconds 300
python 3ds-port/ps2rc3ds.py azahar --hardware old3ds --azahar <PRIVATE_OLD_COPY>
python 3ds-port/ps2rc3ds.py azahar --hardware new3ds --azahar <PRIVATE_NEW_COPY>
python 3ds-port/ps2rc3ds.py build --model old3ds
python 3ds-port/ps2rc3ds.py build --model new3ds
```

Staging selects the newest auto artifact, including profile builds; use an explicit release `--3dsx` for controller tests.
Use separate portable emulator copies/profiles. Record executable path, PID, start time and artifact hash; verify identity
before stopping only that owned PID. Never stop all Azahar processes or change the everyday profile. Do not restage a
running process's data. Archive old captures and check timestamps. No real SD staging was performed in this pass.


## Codex: landed-main normal-control hardware build (2026-11-16)

Separate worktree/branch codex/hardware-main is based on landed main 39a9571, excluding the parked
long-strip candidate. A fresh private auto/release build linked in 448 s (two jobs), with GPU clipping
at its default off, scripted pad/captures disabled, and no short runtime cap. No scripted pad object
was present. Artifact size 11,663,216 bytes, SHA-256
a34e6d9e7652d49a870d72c93766bb7b4b9071967cef3ad5e55a6a30cfc4f888.

The exact release artifact booted in isolated New-mode Azahar without supplied input: 25 periodic
frame samples through 1560, five with nonzero submitted geometry, zero sampled texture omissions/
adapter errors, no fatal/abort/crash/panic lines and zero scripted-pad lines. This is log-based boot/
submission evidence, not visual capture or physical-controller validation; release captures are off.
NDSP again returned 0xd880a7fa and audio state ran silently. The owned emulator was stopped after
verifying recorded PID, path and start time. Raw evidence and manifests stay private.

The private package preflight found all required runtime items, 287 files totalling 3,823,573,102 bytes,
largest file 478,615,552 bytes, no FAT32-size violation, and supported boot ELF hash match. A private
PowerShell staging helper was parsed and dry-run against a private PC folder: exit 0, destination not
created. It defaults to dry-run, accepts an explicit SD root, and only copies with its explicit Copy switch.
No physical card or existing installation was accessed. Console availability/SD path was requested;
staging and hardware execution still await the user's identified target and physical action.

The private release package has READ-ME-FIRST.txt covering Homebrew Launcher startup, physical controls,
log collection and limits. START is game pause during normal operation, not an unconditional quit key.
Saving is unimplemented. Need real New 3DS LL memory/performance, controls, sound and sustained gameplay
before any playable/validated claim. Further renderer work can proceed independently from this hardware gate.


## Codex: rejected ambient-only lighting micro-optimization (2026-11-16)

Candidate 2bf848b precomputed flat ambient colour and skipped unused normal decode/skin/transform when
there were no directional lights. Fourteen host executables, 6000 adapter cases, 11 selected Python tests
and all three native targets passed, and the full profile build linked. Tests alone did not justify it.
The isolated New-mode same-route measurement was slower at matching vertex/call counts:
53131/243: 68.0 -> 68.7 ms; 89706/501: 124.9 -> 127.0 ms; 51071/267: 73.6 -> 75.2 ms.
Baseline is the previous uninstrumented long-strip candidate run, whose long path was never used in the
measured route. These are single-run stage measurements, not a broad statistical performance claim.
Three fresh captures (1360/1480/1600) matched baseline byte-for-byte. The run was deliberately stopped
after the busy windows, by verified PID/path/start time; raw evidence and diagnostic builds stay private.

Reverted the candidate as 3adcf06. Runtime source (games/hle/mk) again matches origin/main 39a9571 exactly.
Do not merge 2bf848b. The original hardware-test release hash/package remain unchanged. Neither this
candidate nor the parked long-strip change solves the measured lighting bottleneck. Prefer a larger,
switchable GPU transform/lighting approach with independent correctness probes over additional speculative
normal-loop tweaks. Hardware results and physical controls/audio are still pending the user's console.


## Claude: shader-side fog and UV scale (2026-11-16, branch claude/shader-fog-uv)

Continued while Codex was out of usage. Codex's GPU lighting probe (`3ds-port/probes/gpu-lighting`, game-free,
committed here unchanged) ran in the private Azahar: 64 of 64 cases exact (`max_channel_delta=0`). Azahar only.

Measured before choosing the next step (private, deterministic route, New mode, GPU clip on, busiest window):
- Skipping directional lighting on lit vertices only: lit conversion 124.6 -> 94.5 ms. So GPU lighting saves at most
  ~30 ms of a ~337 ms frame; the other ~94 ms is decode, skinning and transform.
- Present submit (72.1 ms) splits into world vertex loop 55.5, world state/draws for ~600 batches 14.3, rest small.
  A plain-memcpy experiment still cost ~44 ms in the loop: the copy itself is the cost.
- `mul255` division-free form: ~0.3 ms (most draws skip it). Not committed.
- `hle.memtrap` (~22 ms/iteration): all hardware-register class; it is D8 (scratchpad->RAM) DMA registers and D_STAT,
  ~730 kicks per iteration. Likely includes interrupt work at those safe points. No cheap fix found.

Committed: world fog and UV scale in the vertex shader (numbers in `docs/3DS_STATUS.md`, "Shader-side fog and UV scale").
The GPU vertex layout now equals `W3dC3dVertex` (compile-time asserts), which is the precondition for:

Then committed: screened batch copy (`HLE_3DS_GPU_CLIP=1` only; `docs/3DS_STATUS.md`, "Screened batch copy").
Conversion screens every vertex (finite, in front of near plane); clean batches are copied with one memcpy instead of
per-triangle checks. Busiest window submit 58.4 -> 32.5 ms New, 35.6 -> 20.1 ms Old; captures byte-identical,
including the fogged-crossing frames. With GPU clipping, that window is now ~305 ms/frame vs ~343 ms on `main`.

Zero-copy (drawing straight from conversion output) was dropped for now: it needs a second 8.25 MiB linear vertex
buffer and the full profile only had 6.4 MiB linear free at the route's texture peak. It would need smaller vertices
(e.g. RGBA8 colour, 32 bytes) or a smaller texture budget first. Separately, nothing flushes the data cache for
`vertex_buffer` before the GPU reads it (Citro3D's frame end flushes only its command list, per its headers); Azahar
does not model caches. If hardware shows torn or stale world geometry, add `GSPGPU_FlushDataCache` on the written range.

Suggested next steps, in order:
1. Hardware run of the GPU-clip build (it is now ~11% faster per frame in Azahar than default `main`). If clipping,
   fog and depth look right on a real console, consider making `HLE_3DS_GPU_CLIP=1` the default.
2. Remaining CPU renderer cost in the busiest window: lit conversion ~128 ms (directional lighting ~30 ms of it; the
   GPU lighting probe is ready), static ~17 ms, world state/draws ~14 ms for ~600 batches, game side ~130 ms.

## Claude: static budget distance priority (2026-11-16, branch claude/distance-cut, stacked on #8)

The user pointed at github.com/sammwyy/gtasands (a DS reimplementation, no license: ideas only, clean-room).
Measured which of its ideas apply (`docs/3DS_STATUS.md`, "Distance priority"): a distance cap saves little here
(the PS2 game already limits draw distance; lit cost is all within 50 m), but distance-ordered dropping fixes the
compact profile's holes. Implemented in `w3d_citro3d_depth.h` + `w3d_citro3d_backend.c`; Old captures improved.

Other gtasands ideas not done yet:
- Bottom screen HUD: route the 2D overlay layer (radar, money, clock, wanted level) to the bottom screen, which now
  shows only the debug console. Menus/subtitles share that layer: needs a screen-region split or a toggle.
- Hide tiny props by projected size (their wires/clutter filter), and GPU colour multiply for prelit geometry.

## Claude: dual-screen HUD (2026-11-17, branch claude/bottom-hud, stacked on #9)

Required by the user: bottom-screen HUD like the reference video, reversible. Implemented through the existing HUD
call sites (semantic, not screen-position guesses); details and evidence in `docs/3DS_STATUS.md`, "Dual-screen HUD".
Files: `src/hle_hud.c/.h` (groups, element placement table, bounds log), `sites.def` (hud_sub_0..31),
`engine/render/3ds/hle_3ds_gfx.c` (bottom target, batch screen tag, scissor, capture), `hle/3ds/hle_3ds_platform.c`
(console hand-back), settings entries. Captures: `CAPTURE_START/CAPTURE_MAX/CAPTURE_INTERVAL` build variables write
`gpu-frame-*` and `gpu-bottom-*` PPMs.

Open: confirm ammo/armour/breath/wanted placement when those appear (read `hud bounds ... element N` lines and adjust
`sub_place` / the G_TR box); a loading-screen map on the bottom screen (the video shows one); Old mode check.


## Claude: GPU lighting (2026-11-17, branch claude/gpu-light, stacked on claude/bottom-hud)

`HLE_3DS_GPU_LIGHT=1` (with `HLE_3DS_GPU_CLIP=1`) moves the lit meshes' camera/world transform and lighting into the
vertex shader; -33% lit conversion, -13% busiest frame, captures within 2 levels. Evidence in `docs/3DS_STATUS.md`.
Both switches stay off by default until a hardware run. Next: Old mode check; consider enabling both after hardware.

Audio (Claude, 2026-11-17): with a placeholder `dspfirm.cdc` in the private emulator profile NDSP works and the full
SPU2 path costs ~11 ms/frame; output drops ~60% during the unpaced intro and will underrun in gameplay (details in
`docs/3DS_STATUS.md`, "Audio output path in the emulator"). Next: pace or resample audio to real time, or decide the
audio policy for slow frames. Old mode: the gothic zone-name font is garbled in every compact-profile build checked
(predates the HUD work; likely the 128x128 compact decode limit on a larger font texture).

Pacing (Claude, 2026-11-17): 3DS release builds now default to x_bootpace=1 and x_pace=2 (real time), profile builds
keep the deterministic defaults; NDSP queue 96 buffers. See `docs/3DS_STATUS.md`, "Real-time pacing and the audio
queue". Performance comparisons must use profile builds (or set x_pace=0).

Soak stall (Claude, 2026-11-17, branch claude/soak-stall): deterministic main-loop stall at virtual 315.76 s in the
world-sector line walk 0x27E180 (start vector 1.62e29); bounded by `src/hle_world_guard.c`. Root cause: 0x50DAA0 can return 0 without writing its output and callers 0x40DAB0/0x40E2F4 use it anyway
(uninitialised stack); fixed by pre-filling the output (hook in the same file). See `docs/3DS_STATUS.md`, "Soak stall".

Lit / static geometry on the GPU (Claude, 2026-11-17, branch claude/perf-attrib, PR #13): with GPU lighting, converted
lit ranges are cached across frames (content-hashed key), skinned ranges are blended in the shader (up to three bone
weights per vertex, `HLE_3DS_GPU_SKIN`), and static ranges are sent as object-space vertices for the shader's
`objmode` (cached too; 5 MiB on the full profile, no cache on compact). Azahar, same frames: lit conversion roughly
-30..-36%, static -52%, frame -6..-10% each step; Old mode frame -14..-17%; captures pixel-identical or <= 21 pixels one
level apart. Host tests in `adapter-equiv` emulate the shader arithmetic. `x_texup=0` (skip the driver's GS texture
upload) is an unvalidated opt-in experiment. Next candidates: drop the double copy of cached vertices (draw from the
cache), indexed strips instead of expanded lists, then attribute the remaining guest self time (render_scene,
entity_render, camera_show). Nothing here has been measured on hardware.
