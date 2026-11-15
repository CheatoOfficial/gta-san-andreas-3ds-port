"""Game-free regressions for model selection, staging and publication tools."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]


def module(path):
    spec = importlib.util.spec_from_file_location(Path(path).stem.replace("-", "_"), ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


port = module("3ds-port/ps2rc3ds.py")
guard = module("3ds-port/scripts/check-no-game-data.py")
summary = module("3ds-port/scripts/summarize-log.py")


def test_staging_replaces_same_size_same_timestamp_without_writing_through_links(tmp_path):
    src, dst = tmp_path / "src", tmp_path / "dst"
    src.mkdir(); dst.mkdir()
    (src / "asset").write_bytes(b"new!")
    old = tmp_path / "old"
    old.write_bytes(b"old!")
    try:
        os.link(old, dst / "asset")
    except OSError:
        pytest.skip("hard links unavailable")
    stamp = (src / "asset").stat().st_mtime_ns
    os.utime(old, ns=(stamp, stamp))
    port.copy_tree(src, dst)
    assert (dst / "asset").read_bytes() == b"new!"
    assert old.read_bytes() == b"old!"
    assert (src / "asset").read_bytes() == b"new!"


def test_build_models_use_separate_directories_and_quote_the_script(monkeypatch):
    calls = []
    monkeypatch.setattr(port, "REPO", Path("project's directory"))
    monkeypatch.setattr(Path, "exists", lambda _: True)
    monkeypatch.setattr(port, "devkitpro", lambda: None)
    monkeypatch.setattr(port, "find_bash", lambda: "bash")
    monkeypatch.setattr(port, "run", lambda cmd, **kw: calls.append((cmd, kw)) or 0)
    args = dict(mode="release", jobs=2, vert_budget=None, max_seconds=None, python_bin=None)
    for model in ("new3ds", "old3ds"):
        assert port.cmd_build(SimpleNamespace(model=model, **args)) == 0
    assert calls[0][1]["env"]["BDIR"] != calls[1][1]["env"]["BDIR"]
    assert calls[1][1]["env"]["MODEL"] == "old3ds"
    assert shlex.split(calls[1][0][2]) == ["bash", port.msys(port.REPO / "3ds-port/scripts/build-3ds.sh")]


def test_model_selection_never_picks_the_other_models_newer_file(tmp_path, monkeypatch):
    monkeypatch.setattr(port, "DIST", tmp_path)
    old = tmp_path / "ps2rc-3ds-old3ds-release.3dsx"
    new = tmp_path / "ps2rc-3ds-release.3dsx"
    old.write_text("synthetic old model marker")
    new.write_text("synthetic new model marker")
    os.utime(new, ns=(new.stat().st_atime_ns, old.stat().st_mtime_ns + 2_000_000_000))
    assert port.newest_3dsx("old3ds") == old
    assert port.newest_3dsx("new3ds") == new
    old.unlink()
    assert port.newest_3dsx("old3ds") is None


def test_existing_emulator_config_switches_model_and_preserves_other_settings(tmp_path):
    cfg = tmp_path / "qt-config.ini"
    cfg.write_text("[System]\nis_new_3ds=true\n[Other]\nvalue=100%\n")
    port.configure_azahar(cfg, "old3ds")
    assert "is_new_3ds=false" in cfg.read_text()
    assert "value=100%" in cfg.read_text()
    port.configure_azahar(cfg, "new3ds")
    assert "is_new_3ds=true" in cfg.read_text()


def test_summary_rejects_unrestricted_profiler_text_and_trailing_payloads():
    safe = "PROF window real 0.0-10.0 s: 20 iterations, 40.00 ms/it (idle.pace 0.00, io 1.00, instrument ~0.02)"
    assert any(rx.fullmatch(safe) for rx in summary.ALLOW)
    assert not any(rx.fullmatch(safe + " guest payload") for rx in summary.ALLOW)
    assert not any(rx.fullmatch(safe.replace("idle.pace 0.00", "arbitrary guest payload")) for rx in summary.ALLOW)
    frame = ("Citro3D frame 1: 1/1 triangles, batches=1, near-clipped=0, near-rejected=0, W-dropped=0, "
             "capacity-dropped=0, texture omissions=0, state simplifications=0, PSP segments=0, meshes skipped=0, adapter error=0")
    assert any(rx.fullmatch(frame) for rx in summary.ALLOW)
    assert not any(rx.fullmatch(frame + " arbitrary payload") for rx in summary.ALLOW)


def git_at(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], stderr=subprocess.STDOUT).decode().strip()


def history_repo(tmp_path, monkeypatch):
    git_at(tmp_path, "init", "-q")
    git_at(tmp_path, "config", "user.name", "Synthetic test")
    git_at(tmp_path, "config", "user.email", "test@example.invalid")
    (tmp_path / "safe.txt").write_text("synthetic fixture\n")
    git_at(tmp_path, "add", "."); git_at(tmp_path, "commit", "-qm", "fixture base")
    monkeypatch.setattr(guard, "BASE", git_at(tmp_path, "rev-parse", "HEAD"))
    monkeypatch.setattr(guard, "REPO", tmp_path)


def test_history_finds_deleted_renamed_binary_with_spaces(tmp_path, monkeypatch):
    history_repo(tmp_path, monkeypatch)
    git_at(tmp_path, "mv", "safe.txt", "forbidden file.bin")
    git_at(tmp_path, "commit", "-qm", "synthetic rename")
    git_at(tmp_path, "rm", "forbidden file.bin")
    git_at(tmp_path, "commit", "-qm", "synthetic deletion")
    problems = []
    guard.check_history(problems)
    assert any("forbidden file.bin" in p for p in problems)


def test_history_checks_binary_content_under_text_extension_on_other_refs(tmp_path, monkeypatch):
    history_repo(tmp_path, monkeypatch)
    base = guard.BASE
    (tmp_path / "payload.txt").write_bytes(b"\x00\xffsynthetic")
    git_at(tmp_path, "add", "."); git_at(tmp_path, "commit", "-qm", "synthetic binary")
    git_at(tmp_path, "branch", "other-ref")
    git_at(tmp_path, "checkout", "--detach", base)
    problems = []
    guard.check_history(problems)
    assert any("payload.txt" in p and "UTF-8" in p for p in problems)


def test_history_missing_base_fails_closed(monkeypatch):
    monkeypatch.setattr(guard, "BASE", "0" * 40)
    problems = []
    guard.check_history(problems)
    assert problems and "base unavailable" in problems[0]
