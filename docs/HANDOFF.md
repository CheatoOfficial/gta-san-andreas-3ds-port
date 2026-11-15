# Codex / Claude Code handoff

Updated 2026-11-15. Read `AGENTS.md` first. This is a source-only handoff; no game-derived files belong in git.

## Current objective and repository state

The user handed the published project back to Codex to fix the game, then return a concrete handoff to Claude.
The user authorized any suitable latest private conversion/build workspace for local full builds and Azahar runs.
The repository is public and private vulnerability reporting was verified enabled. This pass changes neither setting.
Neither model is established as playable. Real hardware, physical controls, audio, saving and sustained gameplay remain unverified.

Work branch: `codex/texture-cache-recovery`, based on `20bee84` (the full-build/Azahar update after PR #1).
The next owner should read this branch's changes and the latest section in `docs/3DS_STATUS.md`, rather than the superseded
pre-publication handoff. LICENSE and NOTICE.md must remain identical to pinned upstream `8cd56bb`.

## Changes in this pass

- Old textures now use RGB565 for opaque, RGBA5551 for cutout and RGBA4 for blended images. World images are bounded
  to 64 pixels / 8 KiB; HUD copies use a separate tier up to 128 pixels / 32 KiB. Stored smaller levels or point decimation
  reduce residency before admission. The 2 MiB cache and 8 MiB linear reservation remain unchanged; metadata has 512 slots.
- New textures retain RGBA8 and their existing level policy. Old decode scratch shrank from 1,052,672 to 69,632 bytes,
  reducing the tested Old release BSS to about 5.64 MB (the published baseline reported about 6.58 MB).
- Raster create, destroy, unlock and texture-read hooks now invalidate all matching 3DS cache copies. Dirty snapshots
  remain alive while current/submitted batches can reference them. New prepares cannot silently reuse a dirty snapshot.
  After GPU completion, dirty unpinned copies are retired. Header dimensions/format are also checked on cache hits.
- Fixed the static-budget rejection path in `w3d_citro3d_backend.c`: it formerly reserved the entire remaining vertex
  buffer before returning for a budget skip, blocking later immediate/lit draws and inflating input counters. It now checks
  the budget first. Whole-mesh budget omission and real buffer-capacity limits still exist.
- Fixed fresh-profile startup: staging puts the boot ELF beside discmap.txt at the runtime root, but the loader opened
  disc/<boot>. An isolated fresh profile reproduced the failure. The loader now opens the staged root file; no manual
  duplicate boot file in disc/ is needed.
- Removed first-texel/mean-color diagnostics. The numeric-only log summarizer now accepts strict texture-pressure lines.
- Updated tests and stale project guidance; no game images, builds, translated sources or raw logs are included.

## Verification

- Eight host executables cover controls, clipping, texture layout, both production texture-cache profiles, both backend
  capacities and static-budget exhaustion. The production cache tests use synthetic packets and a fake GPU allocator:
  packing/alpha formats, separate HUD detail, 300 mixed-size resident images under 2 MiB, graceful refusal when all slots
  are pinned, and deferred invalidation through GPU completion. The budget test fails against the pre-fix production source.
- Eleven selected Python tests and 6,000 adapter equivalence cases pass; the source/history guard and diff checks pass.
- Full translated-game New release, Old release and Old profile builds link using ignored private conversion outputs.
- Final emulator results and remaining limitations are recorded in `docs/3DS_STATUS.md`. The final Old profile includes
  scripted input and captures; the New check uses a release build. They are correctness checks, not comparable speed benchmarks.
- Final Old diagnostic captures were inspected locally: player, environment, minimap and HUD text were recognisable.
  All raw logs/captures remain private. Never paste them into this PR or an issue; use the reviewed numeric summary only.
- The existing SDK warning in posix_memalign (nonnull argument compared with null) remains; it is unrelated to this pass.

## Next work for Claude

1. Review and merge this source branch after its CI and latest runtime evidence pass review. Keep the texture/capacity tests.
   Do not infer whole-game correctness or physical-hardware support from these short emulator runs.
2. Work toward one binary without regressing New-model quality: renderer arrays/capacities and texture policy currently
   depend on compile-time model flags, and Old linear-heap size is set before main. A unified build needs runtime-sized
   renderer storage and a startup allocation policy based on the actual application memory allowance. Old-mode Azahar
   still grants much more normal heap than a real Old 3DS launch may provide; do not infer hardware model from that alone.
3. Attribute the slow render_scene/camera_show guest self time, then optimize measured bottlenecks. Do not present
   reduced drawing or smaller textures as proof of faster full-detail rendering. Skipped meshes still need a distance/priority
   policy; the static budget is currently draw-order based.
4. Test real New/Old hardware memory, release-build controls, audio and sustained gameplay. Saving is disabled: the 3DS
   memory-card configuration returns no card and filesystem operations are stubs, so implement and test save/load support.
   Track any stale images
   caused by writes that bypass the raster lifecycle/unlock hooks. The hooks do not prove all guest write paths are covered.
5. Repeat longer scenes/streaming transitions and inspect HUD/alpha quality with the compact formats. Old cache can reach
   its 2 MiB cap and capture builds leave little linear headroom. Temporary safe refusals remain possible while textures are busy.

## Reproduction and process ownership

Use your explicitly authorized local conversion outputs, selected via PS2RC3DS_WORK. Keep outputs ignored and private.

```bash
python 3ds-port/ps2rc3ds.py test
bash 3ds-port/scripts/native-checks.sh
python 3ds-port/ps2rc3ds.py build --model new3ds
python 3ds-port/ps2rc3ds.py build --model old3ds
python 3ds-port/ps2rc3ds.py build --model old3ds --mode profile --max-seconds 300
```

Use a separate portable emulator copy/profile for each concurrently tested model. Record the exact executable path,
PID, start time and artifact hash; stop only that owned PID after verifying its identity. Never stop all Azahar processes.
Clear or archive prior captures and verify capture timestamps against the run start before using them as evidence.
Do not restage a running process's data. Real SD staging is outside this pass.
