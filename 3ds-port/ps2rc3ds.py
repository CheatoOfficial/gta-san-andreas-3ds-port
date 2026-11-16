#!/usr/bin/env python3
"""ps2rc3ds - one entry point for the PS2RC New Nintendo 3DS port.

    python 3ds-port/ps2rc3ds.py doctor [--fix]      check (and optionally install) the toolchain
    python 3ds-port/ps2rc3ds.py verify-disc DISC    check that YOUR disc image is the one the pack expects
    python 3ds-port/ps2rc3ds.py convert DISC        run PS2RC's converter on YOUR disc (translation + staged data)
    python 3ds-port/ps2rc3ds.py build [--mode M]    build the 3DSX with devkitPro (release | profile)
    python 3ds-port/ps2rc3ds.py test                host-side tests (no game data, no emulator)
    python 3ds-port/ps2rc3ds.py stage-sd SD_ROOT    copy the runtime onto YOUR SD card folder (needs --yes)
    python 3ds-port/ps2rc3ds.py azahar --azahar EXE run in a private Azahar profile (minimized)

Everything game-derived stays under ./3ds-work/ (git-ignored). Nothing is ever downloaded for you except what
the tools you choose to run (pip, dkp-pacman, docker pull with --pull) fetch.
"""
import argparse
import configparser
import hashlib
import os
import platform
import shutil
import shlex
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PACK = "SLES-52541_v2.01"
WORKROOT = Path(os.environ.get("PS2RC3DS_WORK", REPO / "3ds-work")).resolve()
WORK = WORKROOT / "work"            # PS2RC converter work folder (analysis, translation, generated modules)
PKG_OUT = WORKROOT / "package"      # PS2RC converter output; the staged data folder lives in PS2RC_<serial>/
PKG = PKG_OUT / "PS2RC_SLES52541"
BUILD = WORKROOT / "build-3ds"
DIST = WORKROOT / "dist"
IS_WIN = platform.system() == "Windows"
RUNTIME_ITEMS = ["disc", "SLES_525.41", "iso_meta.bin", "discmap.txt"]
SD_SUBDIR = Path("3ds") / "ps2rc-sa"


def msys(p):
    """C:\\a\\b -> /c/a/b for devkitPro's MSYS2 bash."""
    p = str(p)
    if IS_WIN and len(p) > 1 and p[1] == ":":
        return "/" + p[0].lower() + p[2:].replace("\\", "/")
    return p.replace("\\", "/")


def devkitpro():
    for c in (os.environ.get("DEVKITPRO"), "C:\\devkitPro", "/opt/devkitpro"):
        if c and Path(c).exists():
            return Path(c)
    return None


def find_bash():
    dk = devkitpro()
    if IS_WIN:
        for c in ([dk / "msys2" / "usr" / "bin" / "bash.exe"] if dk else []) + [Path("C:/devkitPro/msys2/usr/bin/bash.exe")]:
            if c.exists():
                return str(c)
        return None
    return shutil.which("bash")


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.call([str(c) for c in cmd], **kw)


def py_has(mod):
    return subprocess.call([sys.executable, "-c", f"import {mod}"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) == 0


# ---------------------------------------------------------------- doctor
def cmd_doctor(a):
    ok = True

    def line(good, what, hint=""):
        nonlocal ok
        print(("  [ok]   " if good else "  [FAIL] ") + what + ("" if good else ("  -> " + hint if hint else "")))
        ok = ok and good

    print("Checking the toolchain for the GTA San Andreas 3DS port")
    line(sys.version_info >= (3, 11), f"Python {platform.python_version()} (need 3.11+)", "install Python 3.11 or newer")
    line(shutil.which("git") is not None, "git", "install git")
    for mod, pipname in (("elftools", "pyelftools"), ("PIL", "pillow"), ("pytest", "pytest")):
        have = py_has(mod)
        if not have and a.fix:
            run([sys.executable, "-m", "pip", "install", pipname])
            have = py_has(mod)
        line(have, f"Python package {pipname}", f"python -m pip install {pipname}   (or: doctor --fix)")
    docker_up = shutil.which("docker") is not None and subprocess.call(
        ["docker", "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0
    wsl_pspdev = IS_WIN and shutil.which("wsl.exe") is not None and subprocess.call(
        ["wsl.exe", "-e", "bash", "-lc", "test -x ~/pspdev/bin/psp-gcc"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0
    line(wsl_pspdev or docker_up or shutil.which("psp-gcc") is not None,
         "pspdev for the `convert` step (WSL2 ~/pspdev, a running Docker daemon, or psp-gcc on PATH)",
         "WSL2: wsl -e bash 3ds-port/scripts/install-pspdev-wsl.sh   (or start Docker Desktop, or see https://pspdev.github.io/installation.html)")
    dk = devkitpro()
    line(dk is not None, f"devkitPro at {dk}" if dk else "devkitPro", "install from https://devkitpro.org/wiki/Getting_Started")
    bash = find_bash()
    line(bash is not None, "bash for devkitPro" + (f" ({bash})" if bash else ""),
         "install devkitPro with its MSYS2 (Windows) or use Linux/macOS bash")
    if dk and bash:
        if a.fix:
            pacman = dk / "msys2" / "usr" / "bin" / "dkp-pacman"
            pm = shutil.which("dkp-pacman") or (str(pacman) if pacman.exists() else None)
            if pm:
                run([bash, "-lc", f"dkp-pacman -S --needed --noconfirm 3ds-dev"])
        need = [("libctru/include/3ds.h", "libctru"), ("libctru/include/citro3d.h", "citro3d"),
                ("devkitARM/bin/arm-none-eabi-gcc" + (".exe" if IS_WIN else ""), "devkitARM")]
        for rel, name in need:
            line((dk / rel).exists(), name, "dkp-pacman -S 3ds-dev   (or: doctor --fix)")
        tools = dk / "tools" / "bin"
        for t in ("picasso", "3dsxtool", "smdhtool", "bannertool"):
            if t == "bannertool":
                continue
            line((tools / (t + (".exe" if IS_WIN else ""))).exists(), t, "dkp-pacman -S 3ds-dev")
    print("\nAll good." if ok else "\nFix the [FAIL] items above, then run doctor again.")
    return 0 if ok else 1


# ---------------------------------------------------------------- disc
def cmd_verify(a):
    sys.path.insert(0, str(REPO))
    from ps2rc.convert.disc import IsoDisc
    from ps2rc.convert.identify import IdentifyError, identify
    disc = IsoDisc(a.disc)
    try:
        ident = identify(disc, str(REPO))
    except IdentifyError as exc:
        print(f"NOT SUPPORTED: {exc.message}", file=sys.stderr)
        if exc.hint:
            print(exc.hint, file=sys.stderr)
        return 1
    finally:
        disc.close()
    print(f"Serial: {ident.serial}   Version: {ident.version}   Video: {ident.region}")
    print(f"Boot program: {ident.boot} ({ident.elf_size} bytes)")
    print(f"Boot program SHA-256: {ident.elf_sha256}")
    print(f"Matched game pack: {ident.pack.name}")
    if ident.pack.name != PACK:
        print(f"This port targets {PACK}; this disc matched a different pack.", file=sys.stderr)
        return 1
    print("OK: this is the disc image this port targets.")
    return 0


def cmd_convert(a):
    WORK.mkdir(parents=True, exist_ok=True)
    PKG_OUT.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, "-m", "ps2rc.convert", a.disc, "--out", PKG_OUT, "--work", WORK, "--pack", PACK,
           "--movies", "skip", "--audio", a.audio, "--no-splash"]
    if a.use:
        cmd += ["--use", a.use]
    if a.pull:
        cmd += ["--allow-docker-pull"]
    if a.jobs:
        cmd += ["--jobs", str(a.jobs)]
    rc = run(cmd, cwd=REPO)
    if rc == 0:
        print(f"\nConverter finished. Package folder: {PKG}\nNext: python 3ds-port/ps2rc3ds.py build")
    return rc


# ---------------------------------------------------------------- build / test
def cmd_build(a):
    bash = find_bash()
    if not bash:
        print("No bash found; run `doctor` first.", file=sys.stderr)
        return 1
    if not (WORK / "phase6psp" / "trans" / "stats.json").exists():
        print(f"No translation found in {WORK}. Run `convert` first.", file=sys.stderr)
        return 1
    env = dict(os.environ)
    env.update({
        "REPO": msys(REPO), "WORK": msys(WORK), "PKG": msys(PKG),
        "BDIR": msys(BUILD if a.model == "new3ds" else WORKROOT / ("build-3ds-" + a.model)), "DIST": msys(DIST),
        "MODEL": a.model, "MODE": a.mode, "JOBS": str(a.jobs or os.cpu_count() or 4),
    })
    if IS_WIN:   # devkitPro's login shell resets PATH: hand it the Python running this script
        env.setdefault("PYTHON_BIN", msys(Path(sys.executable).parent))
    if devkitpro():
        env["DEVKITPRO"] = msys(devkitpro()) if not IS_WIN else "/opt/devkitpro"
    for k in ("VERT_BUDGET", "MAX_SECONDS", "PYTHON_BIN"):
        v = getattr(a, k.lower(), None)
        if v is not None:
            env[k] = str(v)
    script = msys(REPO / "3ds-port" / "scripts" / "build-3ds.sh")
    return run([bash, "-lc", f"bash {shlex.quote(script)}"], env=env)


def cmd_test(a):
    bash = find_bash() or shutil.which("bash")
    rc = 0
    for s in ("host-tests.sh", "adapter-equiv.sh"):
        rc |= run([bash, "-lc", "PATH=/usr/bin:$PATH bash " + shlex.quote(msys(REPO / '3ds-port' / 'scripts' / s))])
    rc |= run([sys.executable, "3ds-port/scripts/check-no-game-data.py", "--history"], cwd=REPO)
    rc |= run([sys.executable, "-m", "pytest", "-q", "tests/cfg", "3ds-port/tests", "-m", "not wsl and not oracle and not psp and not slow and not needs_game"],
              cwd=REPO)
    print("\nALL TESTS PASSED" if rc == 0 else "\nSOME TESTS FAILED")
    return 1 if rc else 0


# ---------------------------------------------------------------- staging
def newest_3dsx(model="auto"):
    prefix = "ps2rc-3ds" if model == "new3ds" else "ps2rc-3ds-" + model
    # Exact names prevent staging the other model's newest build by accident.
    cands = [DIST / f"{prefix}-{mode}.3dsx" for mode in ("release", "profile")]
    cands = sorted((p for p in cands if p.is_file()), key=lambda p: p.stat().st_mtime, reverse=True)
    return cands[0] if cands else None


def same_file(s, d):
    """Compare content: extracted files can retain the same size AND timestamp after a change."""
    try:
        a, b = s.stat(), d.stat()
    except OSError:
        return False
    if a.st_size != b.st_size:
        return False
    if os.path.samefile(s, d):
        return True
    def digest(p):
        with p.open("rb") as f:
            return hashlib.file_digest(f, "sha256").digest()
    return digest(s) == digest(d)


def copy_tree(src, dst, link=False):
    for root, _dirs, files in os.walk(src):
        rel = Path(root).relative_to(src)
        (dst / rel).mkdir(parents=True, exist_ok=True)
        for f in files:
            s, d = Path(root) / f, dst / rel / f
            if same_file(s, d):
                continue
            if d.exists() or d.is_symlink():
                d.unlink()          # never write through a hard link into the source file
            if link:
                try:
                    os.link(s, d)
                    continue
                except OSError:
                    pass
            shutil.copy2(s, d)


def stage_runtime(dest, threedsx, link=False):
    dest.mkdir(parents=True, exist_ok=True)
    for item in RUNTIME_ITEMS:
        s = PKG / item
        if not s.exists():
            raise SystemExit(f"missing {s}; run `convert` first")
        if s.is_dir():
            copy_tree(s, dest / item, link)
        else:
            t = dest / item
            if t.exists() or t.is_symlink():
                t.unlink()
            shutil.copy2(s, t)
    if threedsx:
        t = dest / threedsx.name
        if t.exists() or t.is_symlink():
            t.unlink()
        shutil.copy2(threedsx, t)


def cmd_stage_sd(a):
    sd = Path(a.sd_root)
    if not sd.is_dir():
        print(f"{sd} is not an existing folder. Mount/insert the SD card first.", file=sys.stderr)
        return 1
    big = [p for p in (PKG / "disc").rglob("*") if p.is_file() and p.stat().st_size >= 4 * 1024**3 - 1]
    if big:
        print("Files too large for FAT32:", *big, sep="\n  ", file=sys.stderr)
        return 1
    total = sum(p.stat().st_size for item in RUNTIME_ITEMS for p in ([PKG / item] if (PKG / item).is_file() else (PKG / item).rglob("*")) if p.is_file())
    dest = sd / SD_SUBDIR
    tdsx = Path(a.threedsx) if a.threedsx else newest_3dsx(a.model)
    if tdsx is None:
        print(f"No {a.model} 3DSX found; build that model first.", file=sys.stderr)
        return 1
    print(f"Will copy {total / 1e9:.2f} GB + {tdsx} into {dest}")
    if not a.yes:
        print("Dry run only. Re-run with --yes to copy.")
        return 0
    stage_runtime(dest, tdsx)
    print("Done. Launch the .3dsx from the Homebrew Launcher.")
    return 0


def configure_azahar(cfg, model):
    """Set the requested hardware model even when a portable config already exists."""
    ini = configparser.ConfigParser(interpolation=None, strict=False)
    ini.optionxform = str
    if cfg.exists():
        ini.read(cfg, encoding="utf-8-sig")
    else:
        ini.read_dict({"Core": {"use_cpu_jit\\default": "false", "use_cpu_jit": "true",
                                "cpu_clock_percentage\\default": "false", "cpu_clock_percentage": "100"},
                       "Renderer": {"use_vsync\\default": "false", "use_vsync": "true"},
                       "Audio": {"audio_emulation\\default": "true"}})
    if not ini.has_section("System"):
        ini.add_section("System")
    ini["System"]["is_new_3ds\\default"] = "false"
    ini["System"]["is_new_3ds"] = "false" if model == "old3ds" else "true"
    with cfg.open("w", encoding="utf-8") as f:
        ini.write(f, space_around_delimiters=False)


def cmd_azahar(a):
    exe = Path(a.azahar).resolve()
    if not exe.exists():
        print(f"{exe} not found", file=sys.stderr)
        return 1
    user = exe.parent / "user"            # Azahar portable mode: a `user` folder next to the executable
    cfgdir = user / "config"
    cfgdir.mkdir(parents=True, exist_ok=True)
    cfg = cfgdir / "qt-config.ini"
    tdsx = Path(a.threedsx) if a.threedsx else newest_3dsx(a.model)
    if tdsx is None:
        print("No 3DSX found; run `build` first.", file=sys.stderr)
        return 1
    hardware = a.hardware or (a.model if a.model != "auto" else "new3ds")
    configure_azahar(cfg, hardware)
    dest = user / "sdmc" / SD_SUBDIR
    for stale in list(dest.glob("session.log")) + list(dest.glob("gpu-frame-*")):
        stale.unlink()                       # old logs/captures from other builds would contaminate this run
    stage_runtime(dest, tdsx, link=True)
    print(f"Staged into {dest}. Launching {tdsx.name}; the log appears in {dest / 'session.log'}")
    flags = 0
    if IS_WIN and not a.visible:
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 6  # SW_MINIMIZE
        subprocess.Popen([str(exe), str(dest / tdsx.name)], cwd=exe.parent, startupinfo=si)
    else:
        subprocess.Popen([str(exe), str(dest / tdsx.name)], cwd=exe.parent)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("doctor"); p.add_argument("--fix", action="store_true"); p.set_defaults(f=cmd_doctor)
    p = sub.add_parser("verify-disc"); p.add_argument("disc"); p.set_defaults(f=cmd_verify)
    p = sub.add_parser("convert"); p.add_argument("disc")
    p.add_argument("--audio", choices=["all", "min", "none"], default="all")
    p.add_argument("--use", choices=["native", "wsl", "docker"])
    p.add_argument("--pull", action="store_true", help="allow downloading the pspdev Docker image")
    p.add_argument("--jobs", type=int); p.set_defaults(f=cmd_convert)
    p = sub.add_parser("build"); p.add_argument("--mode", choices=["release", "profile"], default="release")
    p.add_argument("--model", choices=["auto", "new3ds", "old3ds"], default="auto")
    p.add_argument("--vert-budget", dest="vert_budget", type=int); p.add_argument("--max-seconds", dest="max_seconds", type=int)
    p.add_argument("--python-bin", dest="python_bin"); p.add_argument("--jobs", type=int); p.set_defaults(f=cmd_build)
    p = sub.add_parser("test"); p.set_defaults(f=cmd_test)
    p = sub.add_parser("stage-sd"); p.add_argument("sd_root"); p.add_argument("--3dsx", dest="threedsx")
    p.add_argument("--model", choices=["auto", "new3ds", "old3ds"], default="auto")
    p.add_argument("--yes", action="store_true"); p.set_defaults(f=cmd_stage_sd)
    p = sub.add_parser("azahar"); p.add_argument("--azahar", required=True); p.add_argument("--3dsx", dest="threedsx")
    p.add_argument("--model", choices=["auto", "new3ds", "old3ds"], default="auto")
    p.add_argument("--hardware", choices=["new3ds", "old3ds"], help="emulated console, independent of the binary target")
    p.add_argument("--visible", action="store_true"); p.set_defaults(f=cmd_azahar)
    a = ap.parse_args()
    return a.f(a)


if __name__ == "__main__":
    sys.exit(main())
