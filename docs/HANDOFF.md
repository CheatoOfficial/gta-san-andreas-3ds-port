# Codex / Claude Code handoff

Updated 2026-11-15. Read `AGENTS.md` first. This document contains source-only status and no private run evidence.

## User objective and publication decision

The user requested checks of the pre-publication fixes, Old 3DS compatibility and shared Codex/Claude project instructions.
They authorized making the repository public once ready, or handing back to Claude if it is not ready.

**Keep the repository private for now.** An experimental Old-model profile is implemented, but a full translated-game
Old-model build and run remain unverified. Source/SDK compilation must not be represented as demonstrated compatibility.
The previous publication review's excluded directories and disc images were not opened during this work.

## Starting point and changes

Started from `5038f6b` on main (Claude's review fixes and measured-effects documentation).
Source inspection confirms the audio frame count, ELF file/memory-size check, palette bounds, GPU wait ordering,
previous-frame texture protection and removal of raw instruction/packet dumps.

Added or changed:

- `AGENTS.md`, `CLAUDE.md` and `.claude/`: shared policy, navigation and coordination rules.
- `hle/3ds/hle_3ds_profile.h`: reduced old-model vertex/batch/cache defaults.
- `hle/3ds/hle_3ds_main.c`: old-profile linear heap reservation, model/heap preflight and conditional New-model speedup.
- `hle/3ds/hle_3ds_padmap.*`, `hle_3ds_platform.c`: Old-model camera/shoulder modifier controls without IR/C-stick dependency.
- Renderer headers and source: selected capacities and no capture-buffer allocation when captures are disabled.
- Build/CLI: `--model old3ds`, separate objects and artifact names; matching model selection for staging and Azahar.
- Staging: SHA-256 comparison, because size plus timestamp can still match changed contents; replacement unlinks hard links first.
- Summary filter: full-line numeric formats; removed the unrestricted profiler-text wildcard and trailing-payload acceptance.
- History guard: scans actual blobs across local refs beyond upstream, including renamed/deleted files and disguised binary content;
  missing base or a Git failure fails the history check.
- `3ds-port/tests/test_port_tools.py`, expanded native host tests, CI tooling regressions and `native-checks.sh`.
- Setup/status/Old-model docs: limits, controls and explicit evidence boundaries.

## Verification performed

- Eight Python tooling regressions passed: model/artifact selection, portable config switching, shell quoting,
  same-size/same-time staging without write-through, restrictive summaries, renamed/deleted history, other refs and absent base.
- Five host executables passed: input mapping (including old-model chords), homogeneous clipping, texture layout,
  New-model backend and Old-model backend.
- Native ARM11 source compilation passed for both models with installed devkitPro headers and the real shader.
  This compiles native source units and assembly only; it does not link the full game.
- One existing SDK/compiler warning remains: `posix_memalign` compares an argument declared nonnull to null.
- The full `ps2rc3ds.py test` command passed: five host executables, 6,000 converter equivalence cases,
  11 selected Python tests (20 platform-dependent tests deselected), and the source/history guard.
- Python syntax, shell syntax, documentation links and `git diff --check` passed. License/notices match the upstream base.

## Claude's next steps

Use an explicitly authorized private build environment. Do not add outputs to this source checkout's tracked files.

```bash
python 3ds-port/ps2rc3ds.py test
bash 3ds-port/scripts/native-checks.sh
python 3ds-port/ps2rc3ds.py build --model new3ds
python 3ds-port/ps2rc3ds.py build --model old3ds
python 3ds-port/ps2rc3ds.py build --model old3ds --mode profile --max-seconds 1500
```

1. Check the full linked Old-model text/data/BSS size and both heap sizes. Its 36 MiB normal-heap preflight is an initial
   floor only; verify guest RAM, audio and coroutine stack allocations throughout boot and streaming.
2. Test the matching old-model artifact in Old-model Azahar, then actual Old 3DS/XL/original 2DS with a documented
   homebrew launch memory allowance. Follow `docs/3DS_OLD_MODEL.md`. Do not infer memory availability from physical RAM.
3. Confirm New-model behavior after the profile/header refactor and compare bounded geometry/texture omissions.
4. Fix or explicitly retain the known texture invalidation issue: in-place updates and recycled raster addresses can return stale images.
   Preserve GPU lifetime when adding invalidation. Old model's smaller cache can increase omissions.
5. Review numeric summaries only before sharing; no raw logs, game builds, captures or translated code in GitHub issues/PRs.
6. GitHub private vulnerability reporting is a feature of public repositories, so it cannot be required as a pre-publication
   setting for this private fork. Enable and verify it immediately after an otherwise-approved visibility change and before
   advertising that route. The private repository's endpoint returned 404; both CLI and connected integration have admin access.
   See [GitHub's feature documentation](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository).
7. Recheck source/history, required PS2RC credit and unchanged license/notices, and public-facing claims. Publish only after
   resolving the above validation gaps or obtaining a user-approved narrower experimental-source publication scope.

## Final validation / delivery

Game-free checks passed as recorded above. Full translated-game build/link, emulator execution and hardware tests were
not performed here. Review [draft PR #1](https://github.com/CheatoOfficial/gta-san-andreas-3ds-port/pull/1) on the private
`codex/old3ds-compatibility` branch; its changes are intended for Claude's
full-build/runtime validation before merging or publication. No repository visibility change was made.
