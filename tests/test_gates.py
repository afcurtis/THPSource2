"""Phase 5.1p: worker isolation, the gate scheduler, the status file, fail-fast, the plans."""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import pytest

from tony import display, gates, hooks, workers


def py(code: str) -> list[str]:
    return [sys.executable, "-c", code]


def make_gate(tmp_path: Path, steps, **kwargs) -> gates.Gate:
    kwargs.setdefault("jobs", 2)
    return gates.Gate(steps, directory=tmp_path / "gates", poll=0.02, echo=None, **kwargs)


# ------------------------------------------------------------------------------ sizing


def test_default_jobs_is_half_the_cores_unless_memory_limits():
    assert gates.default_jobs(cores=12, memory=15 * gates.GIB) == 6
    assert gates.default_jobs(cores=12, memory=6 * gates.GIB) == 4
    assert gates.default_jobs(cores=1, memory=1 * gates.GIB) == 1


def test_timeout_scale_only_grows_when_runs_outnumber_cores():
    assert gates.timeout_scale(6, 3, cores=12) == 1.0
    assert gates.timeout_scale(12, 6, cores=12) == 1.5


# ----------------------------------------------------------------------------- workers


def test_worker_id_is_two_digits(monkeypatch):
    monkeypatch.delenv(workers.ENV, raising=False)
    assert workers.worker_id() is None
    monkeypatch.setenv(workers.ENV, "03")
    assert workers.worker_id() == "03"
    monkeypatch.setenv(workers.ENV, "3")
    with pytest.raises(workers.WorkerError):
        workers.worker_id()
    assert workers.worker_ids(3) == ["00", "01", "02"]


def test_worker_resources_are_distinct():
    a, b = "00", "01"
    assert workers.prefix_dir(a) != workers.prefix_dir(b)
    assert workers.disc_dir(a) != workers.disc_dir(b)
    assert workers.runtime_dir(a) != workers.runtime_dir(b)
    assert workers.display_number(a) != workers.display_number(b)
    assert workers.prefix_dir(a).name == "wineprefix-head-w00"


def test_worker_paths_have_the_length_of_the_unisolated_ones():
    """The environment block of a Wine run shifts the Wine heap; the verdicts of the session gate depend on it."""

    from tony.common import load_yaml, resolve

    cfg = load_yaml("re/config/wine.yml")["wine"]
    assert len(str(workers.prefix_dir("07"))) == len(str(resolve(cfg["headless_prefix"])))
    assert len(str(workers.disc_dir("07"))) == len(str(resolve(cfg["headless_disc"])))
    assert len(str(workers.runtime_dir("07"))) == len(str(resolve(hooks.RUNTIME_DIR)))
    assert all(len(f":{workers.display_number(w)}") == len(":99") for w in workers.worker_ids(40))


def test_the_gate_variables_stay_out_of_the_wine_environment(monkeypatch):
    from tony.common import wine_env

    for name in ("OPENTONY_WORKER", "OPENTONY_TIMEOUT_SCALE", "OPENTONY_GATES_NATIVE_ALL"):
        monkeypatch.setenv(name, "1")
    env = wine_env()
    assert not [name for name in env if name in ("OPENTONY_WORKER", "OPENTONY_TIMEOUT_SCALE", "OPENTONY_GATES_NATIVE_ALL")]


def test_runtime_dir_and_display_follow_the_worker(monkeypatch):
    monkeypatch.delenv(workers.ENV, raising=False)
    shared = hooks.runtime_dir()
    assert "-a" in display.xvfb_command({}, {})
    monkeypatch.setenv(workers.ENV, "02")
    assert hooks.runtime_dir() == workers.runtime_dir("02") != shared
    command = display.xvfb_command({}, {})
    assert command[1:3] == ["-n", str(workers.display_number("02"))]
    assert "-a" not in command


def fake_disc(tmp_path: Path) -> Path:
    disc = tmp_path / "disc"
    data = disc / "SETUP" / "data"
    data.mkdir(parents=True)
    (disc / "README.TXT").write_text("readme")
    (disc / "SETUP" / "SETUP.EXE").write_text("setup")
    (data / "ALL.PKR").write_bytes(b"x" * (workers.SMALL_FILE + 1))
    (data / "THawk2.exe").write_text("exe")
    (data / "THawk2.nocd.exe").write_text("nocd")
    (data / "TH2_OPT.CFG").write_text("options")
    (data / "opentony.ini").write_text("[runtime]\n")
    (data / "dinput.dll").write_text("shared dll")
    (data / "SCR00000.bmp").write_text("shot")
    (data / "THPCERR.LOG").write_text("log")
    (data / "Music").mkdir()
    (data / "Music" / "a.mp3").write_text("music")
    return disc


def test_ensure_disc_links_big_files_and_copies_the_ones_a_run_writes(tmp_path, monkeypatch):
    disc = fake_disc(tmp_path)
    monkeypatch.setattr(workers, "source_disc", lambda: disc)
    monkeypatch.setattr(workers, "game_subdirectory", lambda: Path("SETUP/data"))
    monkeypatch.setattr(workers, "shared_nocd_executable", lambda: disc / "SETUP/data/THawk2.nocd.exe")
    monkeypatch.setattr(workers, "worker_dir", lambda worker: tmp_path / "workers" / worker)

    game = workers.ensure_disc("00")

    assert game == tmp_path / "workers/00/disc/SETUP/data"
    assert (game / "ALL.PKR").is_symlink()
    assert (game / "Music").is_symlink()
    assert (tmp_path / "workers/00/disc/README.TXT").is_symlink()
    for name in ("THawk2.nocd.exe", "THawk2.exe", "TH2_OPT.CFG", "opentony.ini"):
        assert (game / name).is_file() and not (game / name).is_symlink(), name
    for name in ("SCR00000.bmp", "THPCERR.LOG", "dinput.dll"):
        assert not (game / name).exists(), name
    # a run that rewrites the options file does not reach the shared one
    (game / "TH2_OPT.CFG").write_text("changed")
    assert (disc / "SETUP/data/TH2_OPT.CFG").read_text() == "options"
    # and a second call is a no-op that keeps the worker's own state
    assert workers.ensure_disc("00") == game
    assert (game / "ALL.PKR").is_symlink()


def test_two_workers_share_nothing_writable(tmp_path, monkeypatch):
    disc = fake_disc(tmp_path)
    monkeypatch.setattr(workers, "source_disc", lambda: disc)
    monkeypatch.setattr(workers, "game_subdirectory", lambda: Path("SETUP/data"))
    monkeypatch.setattr(workers, "shared_nocd_executable", lambda: disc / "SETUP/data/THawk2.nocd.exe")
    monkeypatch.setattr(workers, "worker_dir", lambda worker: tmp_path / "workers" / worker)
    a, b = workers.ensure_disc("00"), workers.ensure_disc("01")
    (a / "THPCERR.LOG").write_text("a")
    assert not (b / "THPCERR.LOG").exists()
    assert (a / "THawk2.nocd.exe").resolve() != (b / "THawk2.nocd.exe").resolve()


# --------------------------------------------------------------------------- scheduler


def read_status(gate: gates.Gate) -> list[str]:
    return gate.status_path.read_text().splitlines()


def test_status_file_has_one_line_per_step_and_a_done_line(tmp_path):
    gate = make_gate(tmp_path, [
        gates.Step("a", py("pass")),
        gates.Step("b", py("raise SystemExit(3)"), after=("a",)),
        gates.Step("c", py("pass"), needs=("a",)),
    ], keep_going=True)
    results = gate.run()
    lines = read_status(gate)
    assert results["a"].status == "ok" and results["b"].code == 3 and results["c"].status == "ok"
    assert sorted(line.split()[0] for line in lines if " exit " in line) == ["a", "b", "c"]
    assert any(line.startswith("b exit 3 ") for line in lines)
    assert lines[-1] == "DONE failed=1 b"
    assert (tmp_path / "gates/logs/a.log").is_file()


def test_a_failed_critical_step_stops_the_run(tmp_path):
    gate = make_gate(tmp_path, [
        gates.Step("verify", py("raise SystemExit(1)"), critical=True),
        gates.Step("long", py("import time; time.sleep(60)"), lane="wine"),
        gates.Step("after", py("pass"), needs=("verify",)),
    ], keep_going=True)
    started = time.time()
    results = gate.run()
    assert time.time() - started < 30
    assert results["verify"].status == "fail"
    assert results["long"].status == "aborted"
    assert results["after"].status == "aborted"
    assert not gate.results["long"].ok
    assert "long aborted (verify failed)" in read_status(gate)
    assert read_status(gate)[-1].startswith("DONE failed=")


def test_fail_fast_stops_on_any_failure_keep_going_does_not(tmp_path):
    steps = lambda: [gates.Step("bad", py("raise SystemExit(2)")),
                     gates.Step("slow", py("import time; time.sleep(1)"), after=("bad",))]
    stopped = make_gate(tmp_path / "x", steps(), keep_going=False).run()
    assert stopped["slow"].status == "aborted"
    kept = make_gate(tmp_path / "y", steps(), keep_going=True).run()
    assert kept["slow"].status == "ok"


def test_a_step_whose_dependency_failed_is_blocked(tmp_path):
    gate = make_gate(tmp_path, [
        gates.Step("bad", py("raise SystemExit(1)")),
        gates.Step("needs-bad", py("pass"), needs=("bad",)),
        gates.Step("skipped", [], skip="nothing to do"),
    ], keep_going=True)
    results = gate.run()
    assert results["needs-bad"].status == "blocked" and not results["needs-bad"].ok
    assert results["skipped"].status == "skipped" and results["skipped"].ok
    assert "skipped skipped (nothing to do)" in read_status(gate)


def test_wine_steps_get_distinct_workers_and_never_more_than_jobs(tmp_path):
    record = tmp_path / "workers"
    record.mkdir()
    code = (
        f"import os, time, pathlib; w = os.environ['OPENTONY_WORKER']; d = pathlib.Path({str(record)!r}); "
        "(d / f'{w}.start').write_text(str(time.time())); time.sleep(0.4); "
        "(d / f'{w}.end').write_text(str(time.time()))"
    )
    steps = [gates.Step(f"run{i}", py(code), lane="wine") for i in range(6)]
    gate = make_gate(tmp_path, steps, jobs=2, keep_going=True)
    results = gate.run()
    assert all(r.ok for r in results.values())
    assert sorted(p.stem for p in record.glob("*.start")) == ["00", "01"]
    # at most two ran at once: the third started after the first ended
    assert max(results[f"run{i}"].seconds for i in range(6)) < 5


def test_cpu_steps_have_no_worker_and_respect_the_lane_limit(tmp_path):
    steps = [gates.Step(f"c{i}", py("import os; raise SystemExit(0 if 'OPENTONY_WORKER' not in os.environ else 1)"))
             for i in range(4)]
    results = make_gate(tmp_path, steps, cpu_jobs=2, keep_going=True).run()
    assert all(r.ok for r in results.values())


def test_a_step_over_its_time_limit_is_stopped(tmp_path):
    gate = make_gate(tmp_path, [gates.Step("hang", py("import time; time.sleep(60)"), timeout=0.3)], keep_going=True)
    started = time.time()
    results = gate.run()
    assert results["hang"].status == "timeout" and results["hang"].code == 124
    assert time.time() - started < 30


def test_the_scale_stretches_the_time_limit(tmp_path):
    gate = make_gate(tmp_path, [gates.Step("slow", py("import time; time.sleep(1)"), timeout=0.5)],
                     keep_going=True, scale=10.0)
    assert gate.run()["slow"].status == "ok"


def test_unknown_dependencies_and_duplicates_are_rejected(tmp_path):
    with pytest.raises(ValueError):
        make_gate(tmp_path, [gates.Step("a", py("pass"), needs=("nope",))])
    with pytest.raises(ValueError):
        make_gate(tmp_path, [gates.Step("a", py("pass")), gates.Step("a", py("pass"))])


# ------------------------------------------------------------------------------- plans


def test_quick_plan_covers_the_cheap_checks_first():
    steps = {step.name: step for step in gates.plan_quick(jobs=4, tests=["tests/test_gates.py"])}
    for name in ("hooks-verify", "hooks-build", "smoke/session", "smoke/capture"):
        assert steps[name].critical, name
    assert steps["hooks-build"].needs == ("hooks-verify",)
    assert "parity/warehouse-idle" in steps and "parity/warehouse-rail" in steps and "session/boot-menu" in steps
    assert not any(name.startswith("parity/warehouse-turn") for name in steps)
    compare = steps["parity/warehouse-idle"]
    runs = [n for n in steps if n.startswith("parity/warehouse-idle/")]
    assert set(compare.after) == set(runs) and len(runs) == 5
    assert all(steps[n].lane == "wine" for n in runs)
    assert steps["workers"].argv[-2:] == ["--count", "4"]


def test_quick_plan_skips_the_tests_step_without_changed_tests():
    steps = {step.name: step for step in gates.plan_quick(jobs=2, tests=[])}
    assert steps["pytest-changed"].skip


def test_native_hooks_override_reaches_only_the_native_all_runs():
    steps = gates.plan_quick(jobs=2, tests=[], native_hooks="fault:Math_Vector3Add")
    flagged = {s.name for s in steps if s.env.get("OPENTONY_GATES_NATIVE_ALL")}
    assert flagged == {"parity/warehouse-idle/native-all", "parity/warehouse-rail/native-all",
                       "session/boot-menu/native-all"}


def test_full_plan_has_every_gate_of_the_hooks_document():
    steps = {step.name: step for step in gates.plan_full(jobs=6)}
    for name in ("native-build", "ctest", "verify-all", "pytest", "hooks-verify", "hooks-build", "selftest",
                 "replay32", "smoke/session", "smoke/capture"):
        assert name in steps, name
    parity = [n for n in steps if n.startswith("parity/") and n.count("/") == 1]
    sessions = [n for n in steps if n.startswith("session/") and n.count("/") == 1]
    assert len(parity) == 9 and len(sessions) == 5
    assert steps["ctest"].needs == ("native-build",)
    # nothing in the Wine lane starts before the smoke checks pass
    assert {"smoke/session", "smoke/capture"} <= set(steps["parity/warehouse-idle/retail-a"].needs)
    # the CPU lane does not wait for them
    assert steps["pytest"].needs == () and steps["verify-all"].needs == ()
    # 9a: the cold boot of each matching session scenario, in the Wine lane behind the same smoke checks
    standalone = [n for n in steps if n.startswith("standalone/")]
    assert standalone == ["standalone/boot-menu", "standalone/boot-options", "standalone/boot-create-skater", "standalone/boot-career-hangar",
                          "standalone/boot-career-end-run", "standalone/boot-park-editor", "standalone/boot-saved-data"]
    assert all(steps[n].lane == "wine" and {"smoke/session", "smoke/capture"} <= set(steps[n].needs) for n in standalone)
    # 10g s25: the Warehouse recordings the standalone matches
    warehouse = [n for n in steps if n.startswith("standalone-warehouse/")]
    assert warehouse == [f"standalone-warehouse/warehouse-{n}" for n in ("idle", "rail", "combo", "manual", "ollie-land", "spin", "straight", "turn", "turn-ollie")]
    assert all(steps[n].lane == "wine" and {"smoke/session", "smoke/capture"} <= set(steps[n].needs) for n in warehouse)
    # 11b: the 64-bit build runs the same scenarios after their 32-bit twins, on the retail run those just made
    standalone64 = [n for n in steps if n.startswith("standalone64/")]
    assert standalone64 == [n.replace("standalone/", "standalone64/") for n in standalone]
    warehouse64 = [n for n in steps if n.startswith("standalone64-warehouse/")]
    assert warehouse64 == [n.replace("standalone-warehouse/", "standalone64-warehouse/") for n in warehouse]
    for n in (*standalone64, *warehouse64):
        twin = n.replace("standalone64", "standalone")
        assert steps[n].lane == "wine" and {"hooks-build-64", twin} <= set(steps[n].needs) and "--keep-retail" in steps[n].argv and "64" in steps[n].argv


def test_changed_test_files_maps_modules_to_tests(tmp_path):
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    (tmp_path / "tony").mkdir()
    (tmp_path / "tests").mkdir()
    (tmp_path / "tony/gates.py").write_text("x = 1\n")
    (tmp_path / "tony/other.py").write_text("x = 1\n")
    (tmp_path / "tests/test_gates.py").write_text("")
    (tmp_path / "tests/test_extra.py").write_text("")
    assert gates.changed_test_files(tmp_path) == ["tests/test_extra.py", "tests/test_gates.py"]


def test_parity_compare_reports_a_missing_run_as_an_error(tmp_path, monkeypatch):
    monkeypatch.setattr(hooks, "PARITY_ROOT", str(tmp_path))
    rows = hooks.parity_compare("warehouse-idle")
    assert [row["result"] for row in rows] == ["ERROR"] * len(rows)
    assert not any(row["ok"] for row in rows)


def test_a_fresh_worker_prefix_is_cold_until_marked_warm(tmp_path, monkeypatch):
    monkeypatch.setattr(workers, "prefix_dir", lambda worker: tmp_path / f"prefix-{worker}")
    (tmp_path / "prefix-00").mkdir()
    assert not workers.is_warm("00")
    workers.mark_warm("00")
    assert workers.is_warm("00") and not workers.is_warm("01")


def test_only_keeps_the_matching_steps_and_what_they_need():
    steps = gates.plan_quick(jobs=2, tests=[])
    chosen = {step.name for step in gates.select_steps(steps, ["session/boot-menu"])}
    assert "session/boot-menu" in chosen and "session/boot-menu/native-all" in chosen
    assert {"workers", "hooks-verify", "hooks-build", "hooks-stage"} <= chosen  # the dependencies come along
    assert not any(name.startswith("parity/") for name in chosen)
    assert "smoke/session" not in chosen
