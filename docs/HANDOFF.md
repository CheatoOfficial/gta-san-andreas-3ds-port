# Codex / Claude Code handoff

Updated 2026-11-16. Read `AGENTS.md` first. This is a source-only handoff.

## Current state

Claude merged PR #2 as `e056963` after independent release builds and Old/New Azahar city runs.
This pass implements the next requested feature on `codex/universal-3ds-runtime`, PR #3: one binary for both consoles.
The user authorized suitable private conversion/build workspaces for local full builds and emulator checks.
The repository remains public; no game data, generated game source, binaries, raw logs or captures belong in git.
LICENSE and NOTICE.md remain identical to upstream `8cd56bb`.
Neither console is established as playable. Hardware, physical controls, audio and sustained gameplay remain unverified.
Saving is disabled by the memory-card configuration and filesystem stubs; this pass does not implement saving.

## Changes for review

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

1. Review PR #3, especially the pre-main allocation policy, immutable-profile assumption, allocation rollback and GPU teardown.
   Repeat one auto artifact in both hardware modes and check CI before merging. Keep the forced targets for regression checks.
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
