# 3DS port: setup guide

Everything is driven by one script: `python 3ds-port/ps2rc3ds.py <command>` (run it with `-h` for options).
The first run of `doctor` tells you exactly what is missing.

## 1. Install the tools (once)

| Tool | Windows | Linux / macOS |
|---|---|---|
| Python 3.11+ and git | python.org, git-scm.com (tick "Add to PATH") | your package manager |
| **devkitPro** (devkitARM, libctru, citro3d) | the official installer from https://devkitpro.org/wiki/Getting_Started - it also installs the MSYS2 bash the build uses | devkitPro's `dkp-pacman` ([guide](https://devkitpro.org/wiki/devkitPro_pacman)) |
| 3DS libraries | `python 3ds-port/ps2rc3ds.py doctor --fix` (runs `dkp-pacman -S 3ds-dev`) | same |
| **PSP toolchain** for the `convert` step: **WSL2 + pspdev** (recommended on Windows), or Docker, or native pspdev | `wsl --install -d Ubuntu`, then `wsl -e bash 3ds-port/scripts/install-pspdev-wsl.sh` (downloads the pinned pspdev release, ~170 MB, into `~/pspdev`) | run `bash 3ds-port/scripts/install-pspdev-wsl.sh`, or Docker, or [pspdev](https://pspdev.github.io/installation.html) |
| Python packages | `doctor --fix` installs `pyelftools pillow pytest` | same |

The PSP toolchain is only needed for the `convert` step, because PS2RC compiles its PSP reference build there; the 3DS
build itself uses only devkitPro.

### Ubuntu / Linux specifics

```bash
python3 -m venv .venv && . .venv/bin/activate && pip install pyelftools pillow pytest   # Python deps in a venv
bash 3ds-port/scripts/install-pspdev-wsl.sh                  # installs ~/pspdev (works on any Debian/Ubuntu x86_64)
export PSPDEV="$HOME/pspdev" PATH="$HOME/pspdev/bin:$PATH"   # needed so the converter finds psp-gcc
```
Then use `convert ... --use native` (the `wsl` mode exists only on Windows) and run `doctor` from the same shell. On
Windows with WSL2, nothing needs exporting: the converter runs `wsl.exe` and finds `~/pspdev` itself.

## 2. Check your disc

```bash
python 3ds-port/ps2rc3ds.py verify-disc "your-disc.iso"
```

Expected: `Serial: SLES-52541`, `Version: 2.01`, PAL, boot program `SLES_525.41` (5,684,848 bytes) with SHA-256
`67458503d86d3000546cbccc5a7b2746b4c5172e7c787c24d74424ea01a8a85c`, matched pack `SLES-52541_v2.01`.
Any other disc (other region, version 1.0x, other game) is not supported by this port.
These identifiers are facts used to recognise a disc; they are not game data.

## 3. Convert (translation + staged data, about 10-30 minutes)

```bash
python 3ds-port/ps2rc3ds.py convert "your-disc.iso" --use wsl      # Linux: --use native; or: --use docker --pull
```

This runs PS2RC's own converter (`python -m ps2rc.convert`) into `3ds-work/work` (analysis, translated C, generated
modules) and `3ds-work/package/PS2RC_SLES52541` (the staged data folder the 3DS runtime reads). With `--use docker`, `--pull` allows it to
download the `pspdev/pspdev` image the first time. `--audio min|none` stages less sound data (default `all`,
about 2.9 GB). Output is large (about 4 GB) and game-derived: keep it private.

## 4. Build

```bash
python 3ds-port/ps2rc3ds.py build                    # release build -> 3ds-work/dist/ps2rc-3ds-release.3dsx
python 3ds-port/ps2rc3ds.py build --mode profile     # timing/diagnostic build (scripted pad route, profiler, captures)
```

The first build compiles about 11,000 translated functions (several minutes at `--jobs 8`); later builds reuse
objects. Options: `--vert-budget N` (cap static vertices converted per frame, 0 = off), `--max-seconds N`
(real-time cap of a session; the release build has none).

## 5. Run it

**Emulator ([Azahar](https://github.com/azahar-emu/azahar)):**
```bash
python 3ds-port/ps2rc3ds.py azahar --azahar path/to/azahar.exe
```
Uses a *portable* profile (a `user/` folder next to the executable, New 3DS mode, JIT, 100% clock), stages the runtime
with hard links where possible, and starts minimized. Use a copy of Azahar that is not your everyday install.
The log is `<azahar>/user/sdmc/3ds/ps2rc-sa/session.log`; profile builds also write `gpu-frame-*.ppm` captures there.
Copy the log right after each run: the next run overwrites it, and old files from other builds mislead.

**Real 3DS:** see [3DS_HARDWARE_TEST.md](3DS_HARDWARE_TEST.md). `stage-sd SD_ROOT` copies everything to
`SD:/3ds/ps2rc-sa/` (dry run unless you add `--yes`; it checks FAT32's 4 GB file limit; files whose size or
modification time differ are replaced, but files that no longer exist after a reconversion are NOT deleted, so
remove `SD:/3ds/ps2rc-sa/` first when you reconvert).

## 6. Tests (no game data needed)

```bash
python 3ds-port/ps2rc3ds.py test
```
Runs the 3DS host unit tests, a randomised equivalence test of the vertex converter, the game-data guard and PS2RC's
quick pytest set. Run it before every commit.

## Troubleshooting

* *`No translation found`*: run `convert` first; it must finish successfully.
* *`python3: command not found` inside the build*: set `--python-bin` to the folder holding your Python.
* *devkitPro not found*: set `DEVKITPRO` (Windows default `C:\devkitPro`, Linux `/opt/devkitpro`).
* *Azahar prints an NDSP error and plays no sound*: known; audio output is unverified.
* *Out of memory on the 3DS*: the game is tight on RAM (linear free dips under 1 MB). Report it with the log.
