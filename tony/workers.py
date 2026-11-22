"""Isolated Wine workers for parallel gate runs (Phase 5.1p).

A gate run is one Wine process with its own state. Run side by side, two of them must not share a Wine
prefix (one `wineserver` per prefix; `wineserver -k` ends all of its clients), a game directory (the
staged DLLs, `opentony.ini`, `THPCERR.LOG`, the `SCRnnnnn.bmp` screenshots), the runtime directory that
holds `opentony.stats`, or an X display number.

`OPENTONY_WORKER=NN` (two digits) selects a worker. In a process that has it set, every helper that
names one of those resources returns the worker's own: `common.headless_wine_env`, `nocd.nocd_executable`,
`hooks.runtime_dir` and `display.xvfb_command` ask this module. Without the variable nothing changes.

**The environment block of a Wine run must have the length it has without workers.** Its size shifts the Wine
heap, and with it the pointer cells of the D3D/DirectInput wrappers that the session comparison sees (5.1s: "any
single enabled hook shifts the heap"; the verdicts of `native-default`, `native-all-stress` and the fault control
changed when 5.1p first gave the workers longer paths). So the paths a worker puts into that environment have
exactly the length of the unisolated ones (`.tools/wineprefix-head-w00` against `.tools/wineprefix-headless`,
`build/gw/00/disc` against `build/disc/files`, `build/gw/00/runtime` against `build/hooks/runtime`, display `:99`
down to `:94`), and `common.wine_env` keeps the gate's own variables (`OPENTONY_WORKER`, ...) out of it.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path

from .common import load_yaml, resolve

ENV = "OPENTONY_WORKER"
WORKER_ROOT = "build/gw"
LAST_DISPLAY = 99  # X display numbers 99 - id: two digits like the `:99` that `xvfb-run -a` picks
_ID = re.compile(r"\d\d")

# Names in the game directory a worker keeps for itself: the game writes them, or the harness stages them
_PRIVATE_NAMES = {"dinput.dll", "opentony.dll", "opentony_native.dll", "sdl3.dll", "opentony.ini", "thawk2.nocd.exe"}
_PRIVATE_SUFFIXES = (".bmp", ".log", ".stats", ".selftest", ".sav")


class WorkerError(RuntimeError):
    pass


def worker_id() -> str | None:
    """The worker of this process, or None when it runs unisolated."""

    value = os.environ.get(ENV)
    if not value:
        return None
    if not _ID.fullmatch(value):
        raise WorkerError(f"{ENV}={value!r}: expected two digits")
    return value


def worker_ids(count: int) -> list[str]:
    if not 1 <= count <= 40:
        raise WorkerError(f"worker count {count}: expected 1 to 40")
    return [f"{index:02d}" for index in range(count)]


def worker_dir(worker: str) -> Path:
    return resolve(WORKER_ROOT) / worker


def prefix_dir(worker: str) -> Path:
    base = Path(str(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless")))
    # same length as the headless prefix path (see the module docstring)
    return resolve(base.with_name(f"{base.name.removesuffix('headless')}head-w{worker}"))


def disc_dir(worker: str) -> Path:
    return worker_dir(worker) / "disc"


def runtime_dir(worker: str) -> Path:
    return worker_dir(worker) / "runtime"


def display_number(worker: str) -> int:
    return LAST_DISPLAY - int(worker)


def timeout_scale() -> float:
    """Factor on the per-run time limits when more runs go at once than the machine has cores
    (`OPENTONY_TIMEOUT_SCALE`, set by `tony gates`)."""

    try:
        return max(1.0, float(os.environ.get("OPENTONY_TIMEOUT_SCALE", "1")))
    except ValueError:
        return 1.0


def worker_environ(worker: str, base: dict[str, str] | None = None) -> dict[str, str]:
    env = dict(os.environ if base is None else base)
    env[ENV] = worker
    return env


# --------------------------------------------------------------------- the game directory


def _is_private(name: str) -> bool:
    lowered = name.lower()
    return lowered in _PRIVATE_NAMES or lowered.endswith(_PRIVATE_SUFFIXES) or lowered.startswith("scr")


def source_disc() -> Path:
    return resolve(load_yaml("re/config/wine.yml")["wine"].get("headless_disc", "build/disc/files"))


def game_subdirectory() -> Path:
    """The directory of the executable relative to the disc root (`SETUP/data`)."""

    spec = load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]
    return resolve(spec["path"]).parent.relative_to(source_disc())


def _sync_links(source: Path, target: Path, *, real: str | None, skip) -> None:
    """Make `target` a real directory of symlinks to the entries of `source`. The entry `real` becomes a
    real directory (the caller fills it); entries `skip(name)` selects are left to the caller."""

    target.mkdir(parents=True, exist_ok=True)
    wanted = {entry.name for entry in source.iterdir() if entry.name != real and not skip(entry.name)}
    for existing in target.iterdir():
        if existing.is_symlink() and existing.name not in wanted:
            existing.unlink()
    for name in wanted:
        link = target / name
        if not link.is_symlink():
            if link.exists():
                raise WorkerError(f"{link} exists and is not a link")
            link.symlink_to(source / name)


def ensure_disc(worker: str) -> Path:
    """The worker's disc tree: links to the shared files down to the game directory, whose files are links
    too except the ones the game or the harness writes. The executable is a real copy: Wine and `winepath`
    resolve links, which would put the run back on the shared directory. Returns the game directory."""

    source = source_disc()
    target = disc_dir(worker)
    relative = game_subdirectory()
    parts = relative.parts
    for depth in range(len(parts)):
        here = Path(*parts[:depth])
        _sync_links(source / here, target / here, real=parts[depth], skip=lambda _name: False)
    shared = shared_nocd_executable()  # first, so that the listing below includes it
    game_source, game_target = source / relative, target / relative
    _sync_links(game_source, game_target, real=None, skip=lambda name: _is_private(name) or _is_small_file(game_source / name))
    for entry in game_source.iterdir():
        # Small files are copied: the game may write them (TH2_OPT.CFG) and Wine resolves links to the shared
        # path. The staged DLLs come from the build (`stage_worker`), the logs and screenshots are the run's own.
        if _is_small_file(entry) and (entry.name.lower() == "opentony.ini" or not _is_private(entry.name)):
            _copy_if_different(entry, game_target / entry.name)
    _copy_if_different(shared, game_target / shared.name)
    return game_target


SMALL_FILE = 4 * 1024 * 1024


def _is_small_file(path: Path) -> bool:
    return path.is_file() and not path.is_symlink() and path.stat().st_size < SMALL_FILE


def shared_nocd_executable() -> Path:
    """The no-CD executable of the shared game directory (created when missing)."""

    from .nocd import patch_nocd_executable

    return patch_nocd_executable()


def _copy_if_different(source: Path, target: Path) -> None:
    if target.is_file() and not target.is_symlink():
        before, after = source.stat(), target.stat()
        if before.st_size == after.st_size and before.st_mtime_ns == after.st_mtime_ns:
            return
    target.unlink(missing_ok=True)
    shutil.copy2(source, target)
    target.chmod(target.stat().st_mode | 0o200)


def stage_worker(worker: str) -> Path:
    """The worker's game directory with the current harness DLLs and the default `opentony.ini` staged."""

    from . import hooks

    directory = ensure_disc(worker)
    if not hooks.staged_current(directory):
        hooks.stage(directory)
    return directory


# ------------------------------------------------------------------------- the Wine prefix


def ensure_prefix(worker: str) -> Path:
    """Clone the headless prefix once (1.8 GB; reflinks where the file system has them), then keep it.
    `headless_wine_command` repoints its `d:` at the worker's disc on every run. A fresh clone must be warmed up
    (`tony gates unit warmup`) before it is used: its first run is sometimes in another Wine heap state, which
    moves the pointer cells of the session comparison (5.1p)."""

    target = prefix_dir(worker)
    if (target / "system.reg").is_file():
        return target
    template = resolve(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    if not (template / "system.reg").is_file():
        raise WorkerError(f"{template}: no headless prefix to clone; run any headless command once")
    partial = target.with_name(target.name + ".partial")
    shutil.rmtree(partial, ignore_errors=True)
    subprocess.run(["cp", "-a", "--reflink=auto", str(template), str(partial)], check=True)
    (partial / ".opentony-prefix.lock").unlink(missing_ok=True)
    shutil.rmtree(target, ignore_errors=True)
    partial.rename(target)
    return target


WARM_MARKER = ".opentony-warm"
WARMUP_RUNS = 2


def is_warm(worker: str) -> bool:
    return (prefix_dir(worker) / WARM_MARKER).is_file()


def mark_warm(worker: str) -> None:
    (prefix_dir(worker) / WARM_MARKER).write_text(f"{WARMUP_RUNS} runs\n")


def prepare(worker: str) -> None:
    ensure_prefix(worker)
    stage_worker(worker)
    runtime_dir(worker).mkdir(parents=True, exist_ok=True)


def kill_prefix(worker: str) -> None:
    """End whatever still runs in the worker's prefix: prefix-scoped, never touches a neighbour."""

    env = dict(os.environ)
    env["WINEPREFIX"] = str(prefix_dir(worker))
    subprocess.run(["wineserver", "-k"], env=env, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
