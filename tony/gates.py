"""`tony gates quick|full`: the gate chain of docs/HOOKS.md, tiered and in parallel (Phase 5.1p).

The commands of the serial chain (`tony hooks parity`, `tony session parity`, ...) remain the definition of each
gate. This module runs the same code as a graph of steps:

* a CPU lane (native build, ctest, `verify --all`, pytest, the self-test and the 32-bit replays) beside
* a Wine lane of isolated workers (tony/workers.py), one run (scenario, variant) per step, and
* one compare step per scenario once its runs are in.

Every step logs to `build/gates/logs/NAME.log` and appends one line to `build/gates/status.txt` when it ends
(`NAME exit N SECONDSs`), so a Monitor on that file reports each step as it finishes. A *critical* step (verify,
build, smoke, selftest, native build) that fails stops the run: running steps are interrupted, the rest is skipped.
"""

from __future__ import annotations

import json
import os
import shlex
import signal
import subprocess
import sys
import time
from collections.abc import Callable, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from . import workers
from .common import ROOT, resolve

GATES_ROOT = "build/gates"
STATUS_FILE = "status.txt"
QUICK_SCENARIOS = ("warehouse-idle", "warehouse-rail")
QUICK_SESSIONS = ("boot-menu",)
GIB = 1024**3
BYTES_PER_WORKER = 1.5 * GIB


@dataclass
class Step:
    name: str
    argv: list[str]
    needs: tuple[str, ...] = ()  # must have succeeded
    after: tuple[str, ...] = ()  # must have ended, however
    lane: str = "cpu"  # "cpu" | "wine" (takes a worker slot)
    critical: bool = False
    timeout: float = 1800.0
    priority: int = 0  # among ready steps of a lane, the highest starts first
    env: dict[str, str] = field(default_factory=dict)
    skip: str | None = None  # a reason: the step is recorded as skipped


@dataclass
class Result:
    name: str
    status: str  # "ok" | "fail" | "timeout" | "skipped" | "blocked" | "aborted"
    code: int | None
    seconds: float
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.status in ("ok", "skipped")  # "blocked": a step it needs did not succeed


# ------------------------------------------------------------------------------ machine sizing


def memory_bytes() -> int:
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return 8 * GIB


def default_jobs(cores: int | None = None, memory: int | None = None) -> int:
    """Wine workers: `min(cores / 2, RAM / 1.5 GB)`, at least one."""

    cores = cores or os.cpu_count() or 2
    memory = memory or memory_bytes()
    return max(1, min(cores // 2, int(memory // BYTES_PER_WORKER)))


def timeout_scale(jobs: int, cpu_jobs: int, cores: int | None = None) -> float:
    """Factor on per-run limits: runs that outnumber the cores take longer each."""

    cores = cores or os.cpu_count() or 2
    return max(1.0, (jobs + cpu_jobs) / cores)


# ------------------------------------------------------------------------------------- scheduler


class Gate:
    """Run `steps` with a CPU lane and `jobs` isolated Wine workers."""

    def __init__(self, steps: Sequence[Step], *, jobs: int, cpu_jobs: int = 3, keep_going: bool = False,
                 directory: Path | None = None, scale: float = 1.0, poll: float = 0.2,
                 echo: Callable[[str], None] | None = print):
        names = [step.name for step in steps]
        if len(set(names)) != len(names):
            raise ValueError("duplicate step names")
        for step in steps:
            for dep in (*step.needs, *step.after):
                if dep not in names:
                    raise ValueError(f"{step.name}: unknown dependency {dep}")
        self.steps = list(steps)
        self.jobs = jobs
        self.cpu_jobs = cpu_jobs
        self.keep_going = keep_going
        self.directory = directory or resolve(GATES_ROOT)
        self.scale = scale
        self.poll = poll
        self.echo = echo or (lambda _line: None)
        self.results: dict[str, Result] = {}
        self.status_path = self.directory / STATUS_FILE
        self.aborted: str | None = None

    # -- status
    def _status(self, line: str) -> None:
        with self.status_path.open("a") as stream:
            stream.write(line + "\n")
        self.echo(line)

    def _finish(self, step: Step, status: str, code: int | None, seconds: float, detail: str = "") -> None:
        result = Result(step.name, status, code, seconds, detail)
        self.results[step.name] = result
        if status in ("skipped", "blocked"):
            self._status(f"{step.name} {status} ({detail})")
        elif status == "aborted":
            self._status(f"{step.name} aborted ({detail})")
        else:
            suffix = f" {detail}" if detail else ""
            self._status(f"{step.name} exit {code if code is not None else '-'} {seconds:.0f}s{suffix}")

    # -- processes
    def _start(self, step: Step, worker: str | None) -> subprocess.Popen:
        logs = self.directory / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ)
        env.update(step.env)
        env["OPENTONY_TIMEOUT_SCALE"] = f"{self.scale:g}"
        env.pop(workers.ENV, None)
        if worker is not None:
            env[workers.ENV] = worker
        log = (logs / f"{step.name.replace('/', '_')}.log").open("w")
        log.write(f"$ {shlex.join(step.argv)}\n")
        if worker is not None:
            log.write(f"# worker {worker}\n")
        log.flush()
        process = subprocess.Popen(step.argv, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        log.close()
        return process

    @staticmethod
    def _stop(process: subprocess.Popen, worker: str | None) -> None:
        """Interrupt first (the run's `finally` blocks end Wine and Xvfb), then kill the group."""

        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGINT)
                process.wait(timeout=8)
            except (OSError, subprocess.TimeoutExpired):
                pass
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError:
                process.kill()
            process.wait()
        if worker is not None:
            workers.kill_prefix(worker)
            subprocess.run(["pkill", "-f", f"Xvfb :{workers.display_number(worker)}"], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # -- the loop
    def run(self) -> dict[str, Result]:
        self.directory.mkdir(parents=True, exist_ok=True)
        self.status_path.write_text("")
        pending = {step.name: step for step in self.steps}
        running: dict[str, tuple[Step, subprocess.Popen, str | None, float]] = {}
        free = workers.worker_ids(self.jobs)
        try:
            while pending or running:
                self._reap(running, free)
                if self.aborted:
                    break
                self._skip_blocked(pending)
                self._launch(pending, running, free)
                if pending or running:
                    time.sleep(self.poll)
        except KeyboardInterrupt:
            self.aborted = "interrupted"
        if self.aborted:
            for name, (step, process, worker, started) in list(running.items()):
                self._stop(process, worker)
                self._finish(step, "aborted", None, time.time() - started, self.aborted)
            running.clear()
            for step in list(pending.values()):
                self._finish(step, "aborted", None, 0.0, self.aborted)
            pending.clear()
        failed = sorted(r.name for r in self.results.values() if not r.ok)
        self._status(f"DONE failed={len(failed)}" + (f" {' '.join(failed)}" if failed else ""))
        return self.results

    def _reap(self, running, free: list[str]) -> None:
        for name, (step, process, worker, started) in list(running.items()):
            elapsed = time.time() - started
            code = process.poll()
            if code is None and elapsed > step.timeout * self.scale:
                self._stop(process, worker)
                code = 124
                status, detail = "timeout", f"after {elapsed:.0f}s"
            elif code is None:
                continue
            else:
                status, detail = ("ok" if code == 0 else "fail"), ""
            del running[name]
            if worker is not None:
                free.append(worker)
                free.sort()
            self._finish(step, status, code, elapsed, detail)
            if status != "ok" and (step.critical or not self.keep_going):
                self.aborted = f"{step.name} failed"

    def _skip_blocked(self, pending: dict[str, Step]) -> None:
        for step in list(pending.values()):
            if step.skip is not None:
                del pending[step.name]
                self._finish(step, "skipped", None, 0.0, step.skip)
                continue
            broken = [d for d in step.needs if d in self.results and not self.results[d].ok]
            if broken:
                del pending[step.name]
                self._finish(step, "blocked", None, 0.0, f"needs {broken[0]}")

    def _launch(self, pending: dict[str, Step], running, free: list[str]) -> None:
        cpu_running = sum(1 for step, *_ in running.values() if step.lane == "cpu")
        ready = [
            step for step in pending.values()
            if all(d in self.results for d in (*step.needs, *step.after))
        ]
        for step in sorted(ready, key=lambda s: (-s.priority, self.steps.index(s))):
            if step.lane == "wine":
                if not free:
                    continue
                worker = free.pop(0)
            else:
                if cpu_running >= self.cpu_jobs:
                    continue
                worker = None
                cpu_running += 1
            del pending[step.name]
            running[step.name] = (step, self._start(step, worker), worker, time.time())


# --------------------------------------------------------------------------------------- plans


def tony(*args: str) -> list[str]:
    return [sys.executable, "-m", "tony", *args]


def _parity_steps(scenario: str, *, needs: tuple[str, ...], priority: int, hal: str | None = None) -> list[Step]:
    from . import hooks

    steps = [
        Step(f"parity/{scenario}/{variant.label}", tony("gates", "unit", "capture", scenario, variant.label),
             needs=needs, lane="wine", priority=priority, timeout=900)
        for variant in hooks.parity_variants(hal)
    ]
    runs = tuple(step.name for step in steps)
    steps.append(Step(f"parity/{scenario}", tony("gates", "unit", "compare", scenario), after=runs, priority=priority,
                      timeout=600))
    return steps


def _session_steps(name: str, *, needs: tuple[str, ...], priority: int) -> list[Step]:
    from . import session_scenarios

    steps = [
        Step(f"session/{name}/{variant.label}", tony("gates", "unit", "session", name, variant.label), needs=needs,
             lane="wine", priority=priority, timeout=900)
        for variant in session_scenarios.session_variants()
    ]
    runs = tuple(step.name for step in steps)
    steps.append(Step(f"session/{name}", tony("gates", "unit", "session-compare", name), after=runs, priority=priority,
                      timeout=600))
    return steps


def changed_test_files(root: Path = ROOT) -> list[str]:
    """Test files that changed (git), plus the test file named after each changed `tony/` module."""

    result = subprocess.run(["git", "status", "--porcelain", "--untracked-files=all"], cwd=root, text=True,
                            stdout=subprocess.PIPE, check=False)
    tests: set[str] = set()
    for line in result.stdout.splitlines():
        path = line[3:].split(" -> ")[-1].strip().strip('"')
        if path.startswith("tests/test_") and path.endswith(".py"):
            tests.add(path)
        elif path.startswith("tony/") and path.endswith(".py"):
            candidate = f"tests/test_{Path(path).stem}.py"
            if (root / candidate).is_file():
                tests.add(candidate)
    return sorted(test for test in tests if (root / test).is_file())


def plan_quick(*, jobs: int, tests: Sequence[str] | None = None, native_hooks: str | None = None) -> list[Step]:
    """`hooks verify`, build, smoke, two Warehouse scenarios, `boot-menu`, the tests of what changed."""

    tests = changed_test_files() if tests is None else list(tests)
    steps = [
        Step("workers", tony("gates", "unit", "prepare", "--count", str(jobs)), critical=True, timeout=900),
        Step("hooks-verify", tony("hooks", "verify"), critical=True, timeout=300),
        Step("hooks-build", tony("hooks", "build"), needs=("hooks-verify",), critical=True, timeout=1200),
        Step("hooks-stage", tony("hooks", "stage"), needs=("hooks-build",), critical=True, timeout=120),
        Step("smoke/session", tony("gates", "unit", "smoke-session"), needs=("hooks-stage", "workers"), lane="wine",
             critical=True, priority=100, timeout=300),
        Step("smoke/capture", tony("gates", "unit", "smoke-capture"), needs=("hooks-stage", "workers"), lane="wine",
             critical=True, priority=100, timeout=300),
    ]
    base = ("hooks-stage", "workers")
    for name in QUICK_SESSIONS:
        steps += _session_steps(name, needs=base, priority=20)
    for scenario in QUICK_SCENARIOS:
        steps += _parity_steps(scenario, needs=base, priority=10)
    steps.append(Step("pytest-changed", tony("gates", "unit", "pytest", *tests) if tests else [], lane="cpu",
                      skip=None if tests else "no changed test files", timeout=900))
    if native_hooks:
        for step in steps:
            if step.name.endswith("/native-all"):
                step.env["OPENTONY_GATES_NATIVE_ALL"] = native_hooks
    return steps


def plan_full(*, jobs: int, tests: Sequence[str] | None = None, native_hooks: str | None = None) -> list[Step]:
    """Everything in docs/HOOKS.md "Verification gates": the CPU lane beside the Wine lane."""

    from . import session_scenarios
    from .scenarios import scenario_files
    from .standalone_boot import GATED_SESSIONS, STANDALONE_ONLY_SESSIONS
    from .standalone_warehouse import GATED_WAREHOUSE

    scenarios = [path.stem for path in scenario_files() if path.stem.startswith("warehouse-")]
    sessions = [path.stem for path in session_scenarios.scenario_files() if path.stem not in STANDALONE_ONLY_SESSIONS]
    steps = [
        Step("workers", tony("gates", "unit", "prepare", "--count", str(jobs)), critical=True, timeout=900),
        # CPU lane
        Step("native-build", ["cmake", "--build", "build/native", "-j", str(os.cpu_count() or 4)], critical=True,
             timeout=2400),
        Step("ctest", ["ctest", "--test-dir", "build/native", "-j", "4", "--output-on-failure"], needs=("native-build",),
             timeout=2400),
        Step("verify-all", tony("verify", "--all"), timeout=2400),
        Step("pytest", [sys.executable, "-m", "pytest", "-q", "-n", "3", "-p", "no:cacheprovider"], timeout=2400),
        Step("hooks-verify", tony("hooks", "verify"), critical=True, timeout=300),
        Step("hooks-build", tony("hooks", "build"), needs=("hooks-verify",), critical=True, timeout=1200),
        Step("hooks-stage", tony("hooks", "stage"), needs=("hooks-build",), critical=True, timeout=120),
        Step("selftest", tony("hooks", "selftest", "--build", "all"), needs=("hooks-stage",), critical=True,
             timeout=3600),
        Step("replay32", tony("hooks", "replay32"), needs=("hooks-stage", "native-build"), timeout=3600),
        # 11b: the x86-64 standalone (build/hooks/kits64), beside the 32-bit one
        Step("hooks-build-64", tony("hooks", "build", "--arch", "64"), needs=("hooks-verify",), timeout=2400),
        # Wine lane
        Step("smoke/session", tony("gates", "unit", "smoke-session"), needs=("hooks-stage", "workers"), lane="wine",
             critical=True, priority=1000, timeout=300),
        Step("smoke/capture", tony("gates", "unit", "smoke-capture"), needs=("hooks-stage", "workers"), lane="wine",
             critical=True, priority=1000, timeout=300),
    ]
    base = ("hooks-stage", "workers", "smoke/session", "smoke/capture")
    for name in sessions:
        steps += _session_steps(name, needs=base, priority=50)
    for name in GATED_SESSIONS:
        steps.append(Step(f"standalone/{name}", tony("standalone", "parity", name), needs=base, lane="wine", priority=30, timeout=1200))
    for name in GATED_WAREHOUSE:
        steps.append(Step(f"standalone-warehouse/{name}", tony("standalone", "warehouse", name), needs=base, lane="wine", priority=30, timeout=1800))
    # 11b: the same scenarios on the 64-bit build. Each runs after its 32-bit twin and reuses the retail reference that step just made (`--keep-retail`: two steps
    # on one retail label would overwrite each other's run directory)
    for name in GATED_SESSIONS:
        steps.append(Step(f"standalone64/{name}", tony("standalone", "parity", name, "--keep-retail", "--arch", "64"),
                          needs=(*base, "hooks-build-64", f"standalone/{name}"), lane="wine", priority=20, timeout=1200))
    for name in GATED_WAREHOUSE:
        steps.append(Step(f"standalone64-warehouse/{name}", tony("standalone", "warehouse", name, "--keep-retail", "--arch", "64"),
                          needs=(*base, "hooks-build-64", f"standalone-warehouse/{name}"), lane="wine", priority=20, timeout=1800))
    for scenario in scenarios:
        steps += _parity_steps(scenario, needs=base, priority=10)
    return steps


def select_steps(steps: Sequence[Step], prefixes: Sequence[str]) -> list[Step]:
    """The steps whose name starts with one of `prefixes`, and everything they depend on (`--only`)."""

    by_name = {step.name: step for step in steps}
    wanted: set[str] = set()
    work = [step.name for step in steps if any(step.name.startswith(prefix) for prefix in prefixes)]
    while work:
        name = work.pop()
        if name in wanted:
            continue
        wanted.add(name)
        work.extend((*by_name[name].needs, *by_name[name].after))
    return [step for step in steps if step.name in wanted]


# -------------------------------------------------------------------------- unit commands


def unit_prepare(count: int | None) -> int:
    ids = workers.worker_ids(count or default_jobs())
    with ThreadPoolExecutor(max_workers=len(ids)) as pool:
        for worker, error in zip(ids, pool.map(_prepare_one, ids), strict=True):
            if error:
                print(f"FAIL worker {worker}: {error}")
                return 1
    print(f"workers ready: {' '.join(ids)}")
    return 0


def _prepare_one(worker: str) -> str:
    try:
        workers.prepare(worker)
        if not workers.is_warm(worker):
            # in a process of its own: the worker is chosen by the environment
            done = subprocess.run([sys.executable, "-m", "tony", "gates", "unit", "warmup"], cwd=ROOT, check=False,
                                  env=workers.worker_environ(worker), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True)
            if done.returncode:
                return f"warm-up failed: {done.stdout[-600:]}"
    except Exception as exc:  # noqa: BLE001 - reported per worker
        return str(exc)
    return ""


def unit_warmup() -> int:
    """Two throwaway `boot-menu` runs in this worker's fresh prefix (see `workers.ensure_prefix`)."""

    from . import hooks, session_scenarios

    worker = workers.worker_id()
    if worker is None:
        raise SystemExit("warmup needs OPENTONY_WORKER")
    document = session_scenarios.load_session("boot-menu")
    for run in range(workers.WARMUP_RUNS):
        if not session_scenarios.session_run(f"warmup-{worker}", document, hooks.ParityVariant("warmup", "none")):
            print(f"warm-up run {run} of worker {worker} did not finish")
            return 1
    workers.mark_warm(worker)
    print(f"worker {worker} warm")
    return 0


def unit_capture(scenario: str, label: str, attempts: int, clock: str) -> int:
    from . import hooks

    variant = _variant(hooks.parity_variants(), label)
    override = os.environ.get("OPENTONY_GATES_NATIVE_ALL")
    if override and variant.label == "native-all":
        variant = hooks.ParityVariant(variant.label, override, variant.expect_equal, variant.stress_ms, variant.hal)
    row = hooks.parity_capture(scenario, variant, clock=clock, attempts=attempts)
    print(f"{scenario}/{label}: {'ok' if row['code'] == 0 else 'capture failed'}")
    return 0 if row["code"] == 0 else 1


def unit_compare(scenario: str, clock: str) -> int:
    from . import hooks

    return 0 if hooks.print_parity(scenario, hooks.parity_compare(scenario), clock=clock) else 1


def unit_session(name: str, label: str, attempts: int) -> int:
    from . import session_scenarios

    variant = _variant(session_scenarios.session_variants(), label)
    override = os.environ.get("OPENTONY_GATES_NATIVE_ALL")
    if override and variant.label == "native-all":
        variant = session_scenarios.hooks.ParityVariant(variant.label, override, variant.expect_equal,
                                                        variant.stress_ms, variant.hal)
    finished = session_scenarios.session_variant_run(name, variant, attempts=attempts)
    print(f"{name}/{label}: {'done' if finished else 'no session.done (crash or hang)'}")
    return 0 if finished else 1


def unit_session_compare(name: str) -> int:
    from . import session_scenarios

    rows, ok = session_scenarios.session_compare(name)
    session_scenarios.print_session_rows(name, rows)
    return 0 if ok else 1


def unit_pytest(tests: Sequence[str]) -> int:
    return subprocess.run([sys.executable, "-m", "pytest", "-q", "-n", "2", "-p", "no:cacheprovider", *tests],
                          cwd=ROOT, check=False).returncode


def _variant(variants, label: str):
    for variant in variants:
        if variant.label == label:
            return variant
    raise SystemExit(f"unknown variant {label!r}: {', '.join(v.label for v in variants)}")


def unit_command(args) -> int:
    from . import hooks

    kind = args.unit
    if kind == "prepare":
        return unit_prepare(args.count)
    if kind == "warmup":
        return unit_warmup()
    if kind == "capture":
        return unit_capture(args.scenario, args.variant, args.attempts, args.clock)
    if kind == "compare":
        return unit_compare(args.scenario, args.clock)
    if kind == "session":
        return unit_session(args.name, args.variant, args.attempts)
    if kind == "session-compare":
        return unit_session_compare(args.name)
    if kind == "smoke-session":
        return 0 if hooks.smoke_session() else 1
    if kind == "smoke-capture":
        return 0 if hooks.smoke_capture(args.scenario) else 1
    if kind == "pytest":
        return unit_pytest(args.tests)
    raise SystemExit(f"unknown unit {kind}")


# ------------------------------------------------------------------------------ the commands


def print_summary(results: dict[str, Result], seconds: float) -> None:
    width = max((len(name) for name in results), default=10)
    print(f"\n{'Step':<{width}}  {'Result':<8} {'Exit':>5} {'Time':>7}")
    for result in results.values():
        code = "-" if result.code is None else str(result.code)
        print(f"{result.name:<{width}}  {result.status:<8} {code:>5} {result.seconds:>6.0f}s"
              + (f"  {result.detail}" if result.detail else ""))
    failed = [r.name for r in results.values() if not r.ok]
    print(f"\n{'FAIL' if failed else 'PASS'}: {len(results) - len(failed)}/{len(results)} steps in {seconds / 60:.1f} min"
          + (f"; failed: {', '.join(failed)}" if failed else ""))


def run_tier(tier: str, args) -> int:
    jobs = args.jobs or default_jobs()
    cpu_jobs = args.cpu_jobs
    tests = args.tests if getattr(args, "tests", None) else None
    steps = (plan_quick if tier == "quick" else plan_full)(jobs=jobs, tests=tests, native_hooks=args.native_hooks)
    if args.only:
        steps = select_steps(steps, args.only)
    if args.dry_run:
        for step in steps:
            print(f"{step.name:<44} {step.lane:<5} {'critical ' if step.critical else ''}{shlex.join(step.argv)}")
        return 0
    keep_going = args.keep_going if args.keep_going is not None else tier == "full"
    gate = Gate(steps, jobs=jobs, cpu_jobs=cpu_jobs, keep_going=keep_going,
                scale=timeout_scale(jobs, cpu_jobs))
    print(f"tony gates {tier}: {len(steps)} steps, {jobs} Wine workers, {cpu_jobs} CPU steps at once, "
          f"{'keep going' if keep_going else 'stop at the first failure'}; status: {gate.status_path}")
    started = time.time()
    results = gate.run()
    print_summary(results, time.time() - started)
    summary = {name: {"status": r.status, "code": r.code, "seconds": round(r.seconds, 1)} for name, r in results.items()}
    (gate.directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if all(r.ok for r in results.values()) and not gate.aborted else 1


def register(subparsers) -> None:
    gates = subparsers.add_parser("gates", help="the gate chain of docs/HOOKS.md, tiered and parallel (5.1p)")
    sub = gates.add_subparsers(dest="gates_command", required=True)
    for tier, text in (("quick", "hooks verify, build, smoke, two Warehouse scenarios, boot-menu, changed tests (minutes)"),
                       ("full", "every gate before a commit")):
        p = sub.add_parser(tier, help=text)
        p.add_argument("--jobs", type=int, help="Wine workers (default min(cores / 2, RAM / 1.5 GB))")
        p.add_argument("--cpu-jobs", type=int, default=3, help="CPU steps at once (default 3)")
        p.add_argument("--keep-going", action="store_true", dest="keep_going", default=None,
                       help="do not stop at the first failure (default for full)")
        p.add_argument("--fail-fast", action="store_false", dest="keep_going", default=None,
                       help="stop at the first failure (default for quick)")
        p.add_argument("--tests", nargs="*", help="quick: test files to run (default: the changed ones)")
        p.add_argument("--native-hooks", help="test the gate: the OPENTONY_HOOKS selection of the native-all variant "
                       "(e.g. fault:NAME must be caught)")
        p.add_argument("--only", nargs="+", metavar="PREFIX",
                       help="only the steps whose name starts with PREFIX (and what they need), e.g. session/boot-menu")
        p.add_argument("--dry-run", action="store_true", help="print the steps and stop")
        p.set_defaults(func=lambda args, tier=tier: run_tier(tier, args))
    unit = sub.add_parser("unit", help="one step of a gate run (used by `gates quick|full`)")
    usub = unit.add_subparsers(dest="unit", required=True)
    p = usub.add_parser("prepare")
    p.add_argument("--count", type=int)
    usub.add_parser("warmup")
    p = usub.add_parser("capture")
    p.add_argument("scenario")
    p.add_argument("variant")
    p.add_argument("--attempts", type=int, default=3)
    p.add_argument("--clock", default="lockstep-fast")
    p = usub.add_parser("compare")
    p.add_argument("scenario")
    p.add_argument("--clock", default="lockstep-fast")
    p = usub.add_parser("session")
    p.add_argument("name")
    p.add_argument("variant")
    p.add_argument("--attempts", type=int, default=2)
    p = usub.add_parser("session-compare")
    p.add_argument("name")
    usub.add_parser("smoke-session")
    p = usub.add_parser("smoke-capture")
    p.add_argument("--scenario", default="warehouse-idle")
    p = usub.add_parser("pytest")
    p.add_argument("tests", nargs="*")
    unit.set_defaults(func=unit_command)

