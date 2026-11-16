# Shared instructions for Codex and Claude Code

Read this file first. It is the canonical project policy; `CLAUDE.md` points here.
Read `docs/HANDOFF.md` for the current work and `docs/3DS_STATUS.md` for supported claims.
User instructions for the current task take precedence over these defaults.

## Project and boundaries

- Experimental PS2RC-derived 3DS runtime. Neither model is established as playable.
- Both model profiles have full-build and emulator observations. Real hardware, physical controls, audio and sustained play remain unverified.
- Keep `LICENSE` and `NOTICE.md` identical to pinned upstream `8cd56bb`. Keep the exact PS2RC credit in README and release pages.
- Never commit, push, attach or publish disc data, translated/generated game sources, game executables, captures, or raw session logs.
- Use synthetic fixtures for tests. Share only reviewed output of `3ds-port/scripts/summarize-log.py`.
- Publication reviews must stay in the source checkout. Do not open excluded verification/work directories or disc images to fill an evidence gap; record the gap.
- Build/runtime work with private game-derived inputs needs an explicitly authorized scope. Keep all outputs ignored and local.
- Do not copy code from proprietary SDKs or other ports without establishing compatible licensing and attribution.

## Ownership and collaboration

- Check `git status`, the current branch/head and `docs/HANDOFF.md` before editing. Preserve uncommitted work.
- One agent owns an implementation branch/worktree at a time. Concurrent agents use separate worktrees and explicitly divided files.
- Before a handoff, record changed files, exact checks/results, unresolved issues and the next commands in `docs/HANDOFF.md`.
- Distinguish source inspection, host tests, ARM compilation, emulator execution and hardware validation. Never turn one into a claim about another.
- Keep handoffs free of personal paths, tokens, raw logs and game data. Refer to repo-relative paths or environment variables.
- Never change repository visibility, publish a release, upload generated artifacts or send messages without the user's task-specific authorization.
- Public source does not mean public game builds. This repository must never host converted game binaries.

## Models when using Codex

- Use subscription-native models. Keep the user's lead model.
- Bounded/mechanical research: Luna at low reasoning. Ordinary implementation/tests: Terra at medium reasoning.
- Sol at medium reasoning only for difficult reasoning or a consequential independent review.
- At most two instances including the lead; only one worker at a time. Do not spawn workers unless the current task or applicable instructions authorize delegation.
- No API keys, proxy inference or separately metered fallbacks.

## Build and verification

Run from the repository root; see `docs/3DS_SETUP.md` for prerequisites.

```bash
bash 3ds-port/scripts/host-tests.sh
bash 3ds-port/scripts/adapter-equiv.sh
python -m pytest -q 3ds-port/tests tests/cfg -m "not wsl and not oracle and not psp and not slow and not needs_game"
python 3ds-port/scripts/check-no-game-data.py --history
bash 3ds-port/scripts/native-checks.sh   # requires devkitPro; no game data
```

For an explicitly authorized local game build:

```bash
python 3ds-port/ps2rc3ds.py build --model new3ds
python 3ds-port/ps2rc3ds.py build --model old3ds
```

- Model builds use separate object directories. Do not share their cached objects manually.
- Preserve the 32 MiB guest address window and bounds checks; reducing it requires a separate correctness argument.
- GPU buffers/textures must survive until submitted work completes. CPU conversion and texture eviction must respect that lifetime.
- Stereo NDSP `nsamples` is a count of stereo frames for PCM16, not interleaved halfwords.
- Old 3DS has no built-in C-stick or ZL/ZR. Preserve its modifier controls and never request New 3DS speedup on old hardware.
- Add focused tests for consequential behavior changes. Documentation-only edits need link/claim checks, not redundant tests.

## Navigation

| Topic | Source |
|---|---|
| Build entry point and staging | `3ds-port/ps2rc3ds.py`, `3ds-port/scripts/` |
| Model limits and platform/input | `hle/3ds/` |
| Rendering and texture lifetime | `games/SLES-52541_v2.01/engine/render/3ds/` |
| NDSP output | `games/SLES-52541_v2.01/engine/sound/3ds/` |
| Model validation plan | `docs/3DS_OLD_MODEL.md`, `docs/3DS_HARDWARE_TEST.md` |
| Publication conditions | `docs/3DS_LEGAL.md`, `SECURITY.md`, `TAKEDOWN.md` |
| Compact topic index | `.claude/memory/MEMORY.md` |
