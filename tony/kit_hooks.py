"""Kit hooks (Phase 5.0): natives of the src/runtime/ Kits hooked into the running game.

A Kit hook is a row of `hooks:` in re/config/native_hooks.yml with a `kit` instead of a hand-written
`native` adapter:

    - name: CBruce__HandleFlips
      kit: skater                      # kits.<id>: the Kit object the call runs on
      call: handle_flips(self)         # the expression of the replay adapter tables (`self`, `a[i]`, S(), U())
      returns: false                   # whether the retail callers use EAX
      convention: thiscall             # cdecl | stdcall | thiscall (ECX + callee-popped stack arguments)
      stack_args: 0
      reaches: []                      # the services its fixtures observed; each must be a live call
      overwrite: 6
      expected: "56 8b b1 88 2c 00"
      builds:
        canonical: 0x0049e430

`tony hooks generate` writes src/hooks/kits/kit_hooks_generated.cpp from these rows and from
`services.live_calls` (the retail functions the Kits reach by service name, which the live host
calls); `tony hooks row` writes a row from the executable and the fixtures; `tony hooks verify`
checks each row's convention against the `ret` instructions of the retail function.
"""

from __future__ import annotations

import bisect
import functools
import glob
import json
import re
from pathlib import Path
from typing import Any

from .common import ROOT, load_yaml, resolve

KIT_SOURCE_PATH = "src/hooks/kits/kit_hooks_generated.cpp"
STANDALONE_SOURCE_PATH = "src/hooks/kits/standalone_kits_generated.cpp"
CONVENTIONS = {"cdecl": "Cdecl", "stdcall": "Stdcall", "thiscall": "Thiscall"}
MAX_STACK_ARGS = 16
FIXTURES = "re/evidence/fixtures/*calls*.txt"
_RET = re.compile(r"^ret[lw]?$")
_ARG = re.compile(r"\ba\[(\d+)\]")
_SELF = re.compile(r"\bself\b")


def is_kit_hook(hook: dict[str, Any]) -> bool:
    return "kit" in hook


def service_name(call: dict[str, Any]) -> str:
    return call.get("service") or f"engine_{int(call['address']):08x}"


# ------------------------------------------------------------------ the executable


@functools.lru_cache(maxsize=1)
def _function_starts() -> list[int]:
    starts = {int(item["address"]) for item in load_yaml("re/symbols/functions.yml").get("functions", [])}
    exported = resolve("build/ghidra/functions.json")
    if exported.is_file():
        starts |= {int(item["address"]) for item in json.loads(exported.read_text())}
    return sorted(starts)


def function_end(address: int) -> int:
    starts = _function_starts()
    index = bisect.bisect_right(starts, address)
    return starts[index] if index < len(starts) else address + 0x1000


def _reachable(rows: list[tuple[int, str, str]], address: int, end: int) -> list[tuple[int, str, str]]:
    """The instructions control flow can reach from `address` inside [address, end).

    `function_end` is the next function Ghidra lists, so a function whose neighbours are missing from the
    list (a tail-jump stub such as `mov [ecx], vtable; jmp base_destructor`) would otherwise count the `ret`
    of the unlisted functions behind it. An indirect jump (a switch table) makes the walk unreliable: the
    whole range is returned then, up to the first jump table the indirect jumps name (MSVC places the tables
    after the code: their bytes are data, and decoded as code they can read as a `ret`; Phase 5 10g s15).
    """

    tables = [int(m.group(1), 16) for _insn, mnemonic, operands in rows if mnemonic.startswith("jmp")
              for m in [re.match(r"\*0x([0-9a-f]+)\(", operands)] if m and address < int(m.group(1), 16) < end]
    if tables:
        rows = [row for row in rows if row[0] < min(tables)]
    by_address = {row[0]: index for index, row in enumerate(rows)}
    seen: set[int] = set()
    work = [0]
    while work:
        index = work.pop()
        while index < len(rows) and index not in seen:
            seen.add(index)
            _insn, mnemonic, operands = rows[index]
            if _RET.match(mnemonic):
                break
            if mnemonic.startswith("j"):
                match = re.match(r"0x([0-9a-f]+)", operands)
                if not match:
                    return rows
                target = int(match.group(1), 16)
                if address <= target < end and target in by_address:
                    work.append(by_address[target])
                if mnemonic == "jmp":
                    break
            index += 1
    return [row for index, row in enumerate(rows) if index in seen]


def callee_pops(address: int, depth: int = 2) -> set[int]:
    """The immediates of the `ret` instructions between `address` and the next known function.

    A function with no `ret` of its own that ends in an unconditional `jmp` out of its range (a tail
    call: `jmp _ftol` after an x87 computation) returns through the target, so the target's `ret`
    immediates count (followed `depth` times).
    """

    from . import hooks

    end = function_end(address)
    rows = _reachable(hooks._disassemble(hooks._canonical_exe(), address, end), address, end)
    pops = set()
    tails = set()
    for _insn, mnemonic, operands in rows:
        if _RET.match(mnemonic):
            match = re.match(r"\$0x([0-9a-f]+)", operands)
            pop = int(match.group(1), 16) if match else 0
            # more than MAX_STACK_ARGS arguments cannot be popped: table bytes decoded as a `ret` (a switch's jump table)
            if pop <= 4 * MAX_STACK_ARGS:
                pops.add(pop)
        elif mnemonic == "jmp":
            match = re.match(r"0x([0-9a-f]+)", operands)
            if match and not address <= int(match.group(1), 16) < end:
                tails.add(int(match.group(1), 16))
    if not pops and depth:
        for target in tails:
            pops |= callee_pops(target, depth - 1)
    return pops


def check_convention(what: str, address: int, convention: str, stack_args: int) -> list[str]:
    if convention not in CONVENTIONS:
        return [f"{what}: convention must be one of {sorted(CONVENTIONS)}"]
    if not isinstance(stack_args, int) or not 0 <= stack_args <= MAX_STACK_ARGS:
        return [f"{what}: stack_args must be 0..{MAX_STACK_ARGS}"]
    pops = callee_pops(address)
    if not pops:
        return [f"{what}: no ret between 0x{address:08x} and the next function (a tail jump?); cannot check the convention"]
    expected = 0 if convention == "cdecl" else 4 * stack_args
    if pops != {expected}:
        listed = ", ".join(f"ret {value}" for value in sorted(pops))
        return [f"{what}: {convention} with {stack_args} stack arguments pops {expected} bytes, the retail function has {listed}"]
    return []


def prologue(address: int) -> tuple[int, bytes, list[str]]:
    """The shortest whole-instruction prologue of at least 5 bytes, its bytes, and why it cannot be patched."""

    from . import hooks

    exe = hooks._canonical_exe()
    rows = hooks._disassemble(exe, address, function_end(address))
    overwrite = next((insn - address for insn, _m, _o in rows if insn - address >= 5), None)
    if overwrite is None:
        end = function_end(address)
        return 0, b"", [f"0x{address:08x}: the function is shorter than 5 bytes ({end - address})"]
    if overwrite > hooks.MAX_OVERWRITE:
        return overwrite, b"", [f"0x{address:08x}: prologue of {overwrite} bytes"]
    data = hooks.PeImage(exe).read(address, overwrite)
    return overwrite, data, hooks._check_boundaries(exe, f"0x{address:08x}", address, overwrite)


# ------------------------------------------------------------------ fixtures


@functools.lru_cache(maxsize=1)
def _fixture_index() -> dict[int, tuple[int, frozenset[str]]]:
    """function address -> (cases, service names) over every forced-call fixture (one pass)."""

    cases: dict[int, int] = {}
    services: dict[int, set[str]] = {}
    for path in sorted(glob.glob(str(ROOT / FIXTURES))):
        with open(path, errors="replace") as lines:
            for line in lines:
                if not line.startswith("C "):
                    continue
                fn = calls = None
                for token in line.split():
                    if token.startswith("fn="):
                        fn = int(token[3:], 16)
                    elif token.startswith("calls="):
                        calls = token[6:]
                if fn is None:
                    continue
                cases[fn] = cases.get(fn, 0) + 1
                names = services.setdefault(fn, set())
                names |= {call.split(",", 1)[0].split("@", 1)[0] for call in (calls or "").split(";") if call}
    return {fn: (count, frozenset(services[fn])) for fn, count in cases.items()}


def observed_services(address: int) -> tuple[int, set[str]]:
    """(cases, service names) the forced-call fixtures recorded for the function at `address`."""

    count, names = _fixture_index().get(address, (0, frozenset()))
    return count, set(names)


def derive_convention(name: str, address: int, call: str) -> tuple[str, int, list[str]]:
    """(convention, stack_args, problems) from the `ret` immediates of the function and what the call expression reads."""

    problems: list[str] = []
    pops = callee_pops(address)
    uses_self = bool(_SELF.search(call))
    used = [int(index) for index in _ARG.findall(call)]
    if len(pops) != 1:
        problems.append(f"0x{address:08x}: ret immediates {sorted(pops)}")
    pop = max(pops) if pops else 0
    if pop:
        convention, stack_args = ("thiscall" if uses_self else "stdcall"), pop // 4
    else:
        convention, stack_args = ("thiscall" if uses_self else "cdecl"), (max(used) + 1 if used else 0)
    if convention == "thiscall" and not pop and used:
        problems.append(f"{name}: uses `self` and stack arguments but pops nothing (a cdecl `this`?)")
    if used and max(used) >= stack_args:
        problems.append(f"{name}: the call reads a[{max(used)}], the function takes {stack_args} stack arguments")
    return convention, stack_args, problems


def make_row(name: str, address: int, kit: str, call: str, returns: bool) -> tuple[dict[str, Any], list[str]]:
    overwrite, data, problems = prologue(address)
    convention, stack_args, more = derive_convention(name, address, call)
    problems += more
    cases, services = observed_services(address)
    if cases == 0:
        problems.append(f"{name}: no fixture case for fn={address:x}")
    row = {
        "name": name,
        "kit": kit,
        "call": call,
        "returns": returns,
        "convention": convention,
        "stack_args": stack_args,
        "reaches": sorted(services),
        "overwrite": overwrite,
        "expected": data.hex(" "),
        "builds": {"canonical": address},
    }
    return row, problems


def render_row(row: dict[str, Any]) -> str:
    reaches = ", ".join(row["reaches"])
    return "\n".join([
        f"  - name: {row['name']}",
        f"    kit: {row['kit']}",
        f"    call: \"{row['call']}\"",
        f"    returns: {'true' if row['returns'] else 'false'}",
        f"    convention: {row['convention']}",
        f"    stack_args: {row['stack_args']}",
        f"    reaches: [{reaches}]",
        f"    overwrite: {row['overwrite']}",
        f"    expected: \"{row['expected']}\"",
        "    builds:",
        f"      canonical: 0x{row['builds']['canonical']:08x}",
        "",
    ])


# ------------------------------------------------------------------ census (Phase 5.1 planning)

PROBES = "re/gdb/probes/*.py"
STUB_HELPERS = {"ensure", "engine", "stub", "stub_site"}
# a stub with any of these recorded something other than the call's own arguments, or scripted the
# service's memory writes as values: the Kit's call has no live form until the Kit is changed
REPLAY_ONLY_OPTIONS = {"pokes", "writes", "fill", "blob", "deref", "strings", "keep", "stack_to", "site"}


@functools.lru_cache(maxsize=1)
def stub_declarations() -> dict[str, list[dict[str, Any]]]:
    """service name -> the probe stubs declared for it (address, options)."""

    import ast

    found: dict[str, list[dict[str, Any]]] = {}
    for path in sorted(glob.glob(str(ROOT / PROBES))):
        tree = ast.parse(Path(path).read_text())
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            helper = node.func.attr if isinstance(node.func, ast.Attribute) else getattr(node.func, "id", None)
            if helper not in STUB_HELPERS:
                continue
            arguments = node.args[1:] if helper in ("ensure", "engine") else node.args
            if len(arguments) < 2 or not isinstance(arguments[0], ast.Constant) or not isinstance(arguments[0].value, int):
                continue
            address = arguments[0].value
            name = f"engine_{address:08x}"
            if len(arguments) > 2 and isinstance(arguments[2], ast.Constant) and isinstance(arguments[2].value, str):
                name = arguments[2].value
            options = {keyword.arg for keyword in node.keywords if keyword.arg}
            if helper == "stub_site":
                options.add("site")
            found.setdefault(name, []).append({"address": address, "options": options})
    return found


def _service_state(name: str, extra: bool) -> str:
    """live (a plain call of one retail function), replay-only, or unknown (no stub declaration)."""

    declarations = stub_declarations().get(name, [])
    if not declarations:
        return "unknown"
    if extra or any(item["options"] & REPLAY_ONLY_OPTIONS for item in declarations):
        return "replay-only"
    if len({item["address"] for item in declarations}) > 1:
        return "unknown"
    return "live"


@functools.lru_cache(maxsize=1)
def _extra_values() -> dict[int, frozenset[str]]:
    """function -> services for which a case scripted more values than it made calls (replayed writes)."""

    extra: dict[int, set[str]] = {}
    for path in sorted(glob.glob(str(ROOT / FIXTURES))):
        with open(path, errors="replace") as lines:
            for line in lines:
                if not line.startswith("C ") or " values=" not in line or line.rstrip().endswith("values="):
                    continue
                fields = dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)
                calls = [call.split(",", 1)[0].split("@", 1)[0] for call in fields.get("calls", "").split(";") if call]
                for item in [value for value in fields.get("values", "").split(";") if value]:
                    service, _, values = item.partition("=")
                    if len([value for value in values.split(",") if value]) > calls.count(service):
                        extra.setdefault(int(fields["fn"], 16), set()).add(service)
    return {fn: frozenset(services) for fn, services in extra.items()}


@functools.lru_cache(maxsize=1)
def _patchable() -> dict[int, str]:
    """function start -> why its entry cannot take a 5-byte jump ('' when it can), from one disassembly of .text."""

    from . import hooks

    starts = _function_starts()
    rows = hooks._disassemble(hooks._canonical_exe(), starts[0], starts[-1] + 0x100)
    boundaries = {insn for insn, _m, _o in rows}
    targets: set[int] = set()
    branch_at: set[int] = set()
    for insn, mnemonic, operands in rows:
        if hooks._BRANCH.match(mnemonic):
            branch_at.add(insn)
            match = re.match(r"0x([0-9a-f]+)", operands)
            if match:
                targets.add(int(match.group(1), 16))
    ordered = sorted(boundaries)
    reasons: dict[int, str] = {}
    for start in starts:
        if start not in boundaries:
            reasons[start] = "not an instruction boundary"
            continue
        index = bisect.bisect_left(ordered, start)
        end = function_end(start)
        patch = next((insn for insn in ordered[index:index + 8] if insn - start >= 5), None)
        if patch is None or patch > end:
            reasons[start] = "shorter than 5 bytes"
        elif patch - start > hooks.MAX_OVERWRITE:
            reasons[start] = "prologue longer than 16 bytes"
        elif any(start <= insn < patch for insn in branch_at):
            reasons[start] = "relative branch in the prologue"
        elif any(start < target < patch for target in targets):
            reasons[start] = "branch target inside the prologue"
        else:
            reasons[start] = ""
    return reasons


def census() -> dict[str, Any]:
    from . import hooks

    natives = hooks._native_statuses()
    hookable = [address for address, item in natives.items() if item.get("status") in hooks.HOOKABLE_STATUSES]
    index = _fixture_index()
    extra = _extra_values()
    patchable = _patchable()
    # a service with a live_calls row is live whatever its probe stub recorded (the assertion routine's
    # stub keeps only the message; its row passes the condition as a lead argument)
    rows = {service_name(call) for call in hooks.load_manifest().get("services", {}).get("live_calls", [])}
    categories: dict[str, list[int]] = {}
    blockers: dict[str, set[int]] = {}
    named: dict[str, set[int]] = {}
    for address in sorted(hookable):
        cases, services = index.get(address, (0, frozenset()))
        states = {service: "live" if service in rows else _service_state(service, service in extra.get(address, frozenset()))
                  for service in services}
        if patchable.get(address, "not a known function start"):
            category = "entry cannot be patched"
        elif cases == 0:
            category = "no fixture"
        elif not services:
            category = "pure"
        elif all(state == "live" for state in states.values()):
            category = "live calls only"
        elif any(state == "replay-only" for state in states.values()):
            category = "needs a Kit change (replay-only service)"
        else:
            category = "needs a stub declaration"
        categories.setdefault(category, []).append(address)
        for service, state in states.items():
            if state != "live":
                blockers.setdefault(service, set()).add(address)
            if not service.startswith("engine_") and state == "live":
                named.setdefault(service, set()).add(address)
    reasons: dict[str, int] = {}
    for address in categories.get("entry cannot be patched", []):
        reason = patchable.get(address, "not a known function start")
        reasons[reason] = reasons.get(reason, 0) + 1
    return {
        "hookable": len(hookable),
        "categories": {name: len(items) for name, items in sorted(categories.items(), key=lambda item: -len(item[1]))},
        "unpatchable": reasons,
        "blockers": sorted(((service, len(fns)) for service, fns in blockers.items()), key=lambda item: -item[1]),
        "named_live": sorted(((service, len(fns)) for service, fns in named.items()), key=lambda item: -item[1]),
        "addresses": {name: [f"0x{address:08x}" for address in items] for name, items in categories.items()},
    }


# ------------------------------------------------------------------ validation

def recorder_seam_errors(manifest: dict[str, Any]) -> list[str]:
    """A hook whose patched bytes overlap one of the in-process recorder's patches crashes every capture
    (`tony hooks parity`, `tony scenario capture`); the session probe has no recorder and does not see it."""
    document = load_yaml("re/config/capture_hooks.yml")
    base = int(document["build"]["image_base"])
    seams = [(base + int(seam["rva"]), int(seam["overwrite"]), name) for name, seam in document["hooks"].items()
             if "rva" in seam and "overwrite" in seam]
    errors: list[str] = []
    for hook in manifest.get("hooks", []):
        address = hook.get("builds", {}).get("canonical")
        if not isinstance(address, int) or not isinstance(hook.get("overwrite"), int):
            continue
        for start, length, name in seams:
            if address < start + length and start < address + hook["overwrite"]:
                errors.append(f"{hook.get('name', '?')}: its patched bytes overlap the recorder seam {name} "
                              f"(re/config/capture_hooks.yml, 0x{start:08x}); two patches on one entry crash every capture")
    return errors


NATIVES_PATH = "re/config/standalone_natives.yml"
# the rows written by hand: natives on a value-owned model that a Kit re-implements on the retail memory model (src/runtime/resource_kit.*);
# scripts/standalone_rows.py cannot read them from a replay test because no test builds that Kit
MANUAL_NATIVES_PATH = "re/config/standalone_natives_manual.yml"
NO_KIT = "none"  # a native row whose call is a constant (`0`, `(void)0`): no Kit object


def load_natives() -> list[dict[str, Any]]:
    """The natives that have no hook row: the generated rows (scripts/standalone_rows.py), then the hand-written ones. Called by address in a
    standalone program, never patched in."""

    rows: list[dict[str, Any]] = []
    for path in (NATIVES_PATH, MANUAL_NATIVES_PATH):
        if resolve(path).is_file():
            rows.extend(load_yaml(path).get("natives", []))
    return rows


def validate_natives(manifest: dict[str, Any], check_disassembly: bool) -> list[str]:
    errors: list[str] = []
    kits = manifest.get("kits", {})
    hooked = {hook["builds"]["canonical"] for hook in manifest.get("hooks", []) if is_kit_hook(hook)}
    seen: dict[int, str] = {}
    names: set[str] = {hook["name"] for hook in manifest.get("hooks", []) if is_kit_hook(hook)}
    for row in load_natives():
        name, address = row.get("name", "?"), int(row.get("address", 0))
        if row.get("kit") not in kits and row.get("kit") != NO_KIT:
            errors.append(f"natives.{name}: unknown kit {row.get('kit')!r}")
        if address in hooked:
            errors.append(f"natives.{name}: 0x{address:08x} has a hook row already")
        if address in seen:
            errors.append(f"natives.{name}: 0x{address:08x} is also {seen[address]}")
        if name in names:
            errors.append(f"natives.{name}: duplicate name")
        seen[address] = name
        names.add(name)
        text = str(row.get("call", row.get("expr", "")))
        if ("call" in row) == ("expr" in row):
            errors.append(f"natives.{name}: exactly one of call and expr")
        used = [int(index) for index in _ARG.findall(text)]
        if used and isinstance(row.get("stack_args"), int) and max(used) >= row["stack_args"]:
            errors.append(f"natives.{name}: the call reads a[{max(used)}], stack_args is {row['stack_args']}")
        if _SELF.search(text) and row.get("convention") != "thiscall":
            errors.append(f"natives.{name}: the call uses `self` but the convention is {row.get('convention')}")
        if check_disassembly and not row.get("unchecked"):
            errors += check_convention(f"natives.{name}", address, row.get("convention"), row.get("stack_args"))
    return errors


def validate(manifest: dict[str, Any], symbols: dict[int, str], check_disassembly: bool) -> list[str]:
    errors: list[str] = recorder_seam_errors(manifest) + validate_natives(manifest, check_disassembly)
    kits = manifest.get("kits", {})
    for kit, spec in kits.items():
        if not resolve(f"src/runtime/{spec.get('header', '')}").is_file():
            errors.append(f"kits.{kit}: src/runtime/{spec.get('header')} does not exist")
        if not spec.get("type"):
            errors.append(f"kits.{kit}: no type")
    calls = manifest.get("services", {}).get("live_calls", [])
    services: set[str] = set()
    seam_names = {int(seam["address"]): seam["name"] for seam in load_yaml("re/symbols/seams.yml").get("seams", [])}
    for call in calls:
        address = int(call.get("address", 0))
        what = f"live_calls.{call.get('name')}"
        # out-of-scope functions are named by re/symbols/seams.yml where Ghidra has no name
        if call.get("name") not in (symbols.get(address), seam_names.get(address)):
            errors.append(f"{what}: re/symbols/functions.yml names 0x{address:08x} {symbols.get(address)!r}, "
                          f"re/symbols/seams.yml {seam_names.get(address)!r}")
        name = service_name(call)
        if name in services:
            errors.append(f"{what}: duplicate service {name}")
        services.add(name)
        if call.get("variadic") and call.get("convention") != "cdecl":
            errors.append(f"{what}: only a cdecl function can be variadic")
        lead = call.get("lead", [])
        if len(lead) > 1 or any(value != lead[0] for value in lead):
            errors.append(f"{what}: lead must be at most one constant")
        if check_disassembly:
            errors += check_convention(what, address, call.get("convention"), call.get("stack_args"))
    for hook in manifest.get("hooks", []):
        if not is_kit_hook(hook):
            continue
        name = hook.get("name", "?")
        address = hook.get("builds", {}).get("canonical")
        if hook.get("kit") not in kits:
            errors.append(f"{name}: unknown kit {hook.get('kit')!r}")
        if set(hook.get("builds", {})) != {"canonical"}:
            errors.append(f"{name}: a Kit hook runs on the canonical build only (the Kits use its data addresses)")
        if "abi" in hook or "native" in hook:
            errors.append(f"{name}: a Kit hook has no abi/native (its adapter is generated)")
        if not isinstance(hook.get("returns"), bool):
            errors.append(f"{name}: returns must be true or false")
        call = str(hook.get("call", ""))
        return_value = hook.get("return_value")
        if return_value is not None:
            if hook.get("returns") is not False:
                errors.append(f"{name}: return_value goes with returns: false")
            call_check = f"{call} {return_value}"
        else:
            call_check = call
        used = [int(index) for index in _ARG.findall(call_check)]
        stack_args = hook.get("stack_args")
        if used and isinstance(stack_args, int) and max(used) >= stack_args:
            errors.append(f"{name}: the call reads a[{max(used)}], stack_args is {stack_args}")
        if _SELF.search(call) and hook.get("convention") != "thiscall":
            errors.append(f"{name}: the call uses `self` but the convention is {hook.get('convention')}")
        for service in hook.get("reaches", []):
            if service not in services:
                errors.append(f"{name}: reaches {service}, which is not a live call")
        if hook.get("default") not in (None, "off"):
            errors.append(f"{name}: default must be `off` or absent")
        if hook.get("selftest") not in (None, "words"):
            errors.append(f"{name}: selftest must be `words` or absent")
        elif hook.get("selftest") == "words" and hook.get("convention") != "cdecl":
            errors.append(f"{name}: selftest words needs a cdecl function")
        if check_disassembly and isinstance(address, int):
            errors += check_convention(name, address, hook.get("convention"), stack_args)
    return errors


# ------------------------------------------------------------------ codegen


def _adapter(hook: dict[str, Any], prefix: str = "ot_kit_") -> list[str]:
    name, kit = hook["name"], hook["kit"]
    # `call` is a member call on the Kit object; `expr` (standalone natives) is a whole C++ expression that names `kit.` itself
    call = hook.get("call", hook.get("expr"))
    stack = [f"std::uint32_t a{k}" for k in range(int(hook["stack_args"]))]
    convention = hook["convention"]
    if convention == "thiscall":
        signature = ", ".join(["std::uint32_t self", "std::uint32_t /*edx*/", *stack])
        head = f"std::uint32_t __fastcall {prefix}{name}({signature})"
    else:
        keyword = "__cdecl" if convention == "cdecl" else "__stdcall"
        head = f"std::uint32_t {keyword} {prefix}{name}({', '.join(stack) or 'void'})"
    arguments = ", ".join(f"a{k}" for k in range(int(hook["stack_args"])))
    lines = [f"{head} {{"]
    if hook["stack_args"]:
        lines.append(f"    const std::uint32_t a[] = {{{arguments}}};")
    lines.append(f'    return run("{name}", [&](Precision precision) -> std::uint32_t {{')
    lines.append("        g_kits->set_precision(precision);")
    if kit == NO_KIT:
        # a constant: the retail function returns it or does nothing
        body = [f"        return U({call});"] if hook["returns"] else [f"        {call};", "        return 0;"]
        lines += ["        (void)precision;", *body, "    });", "}", ""]
        return lines
    lines.append(f"        auto& kit = g_kits->{kit};")
    if "expr" in hook:
        body = [f"        return U({call});"] if hook["returns"] else [f"        {call};", "        return 0;"]
        lines += [*body, "    });", "}", ""]
        return lines
    if hook["returns"]:
        lines.append(f"        return U(kit.{call});")
    elif hook.get("return_value"):
        # a struct returned by value: the Kit fills the hidden output pointer and retail hands it back in EAX
        lines += [f"        kit.{call};", f"        return U({hook['return_value']});"]
    else:
        lines += [f"        kit.{call};", "        return 0;"]
    lines += ["    });", "}", ""]
    return lines


def render_source(manifest: dict[str, Any], standalone: bool = False) -> str:
    """The Kit DLL's generated source. `standalone` is opentony_standalone.dll's: every native by retail address (the hooks' and the
    rows of re/config/standalone_natives.yml), the resolver's tables and every Kit those need. Without it, the hooked build's
    opentony_native.dll: the Kit hooks only, exactly as before the standalone existed (the hooked build is the differential
    reference and a bigger DLL in the retail process changed the stress-clock session scenarios, 5.4a)."""

    kits = manifest.get("kits", {})
    hooks = [hook for hook in manifest.get("hooks", []) if is_kit_hook(hook)]
    natives = load_natives() if standalone else []
    used = [kit for kit in kits if any(hook["kit"] == kit for hook in hooks + natives)]
    calls = manifest.get("services", {}).get("live_calls", [])
    lines = [
        "// Generated by `tony hooks generate` from re/config/native_hooks.yml (the hooks with a `kit`, and",
        "// services.live_calls) and re/config/standalone_natives.yml. Do not edit." if standalone else "// services.live_calls). Do not edit.",
        '#include "../kit_abi.h"',
        '#include "kit_bridge.hpp"',
        "",
    ]
    lines += [f'#include "{kits[kit]["header"]}"' for kit in used]
    lines += ["", "#include <cstddef>", "#include <cstdint>", "", "namespace opentony::kitdll {", ""]
    lines.append("struct Kits {")
    initializers = ", ".join(f"{kit}(memory, services)" for kit in used)
    lines.append(f"    Kits(RetailMemory& memory, RetailServices& services){' : ' + initializers if used else ''} {{" + ("(void)memory; (void)services; " if not used else "") + "}")
    lines.append("    void set_precision(Precision precision) {")
    lines += [f"        set_kit_precision({kit}, precision);" for kit in used] or ["        (void)precision;"]
    lines.append("    }")
    lines += [f"    {kits[kit]['type']} {kit};" for kit in used]
    lines += ["};", "", "namespace {", "", "Kits* g_kits = nullptr;", ""]
    for hook in hooks:
        lines += _adapter(hook)
    for row in natives:
        lines += _adapter(row, "ot_native_")
    lines += ["}  // namespace", "", "Kits* make_kits(RetailMemory& memory, RetailServices& services) {",
              "    g_kits = new Kits(memory, services);", "    return g_kits;", "}", ""]
    lines.append("const LiveCall kLiveCalls[] = {")
    for call in calls:
        lead = call.get("lead", [])
        lines.append(
            f'    {{"{service_name(call)}", 0x{int(call["address"]):08x}u, Convention::{CONVENTIONS[call["convention"]]}, '
            f'{int(call["stack_args"])}, {len(lead)}, {int(lead[0]) if lead else 0}u, {"true" if call.get("variadic") else "false"}}},  // {call["name"]}')
    if not calls:
        lines.append('    {"", 0u, Convention::Cdecl, 0, 0, 0u, false},')
    lines += ["};", f"const std::size_t kLiveCallCount = {len(calls)};", ""]
    lines.append("extern const OtKitEntry kEntries[] = {")
    lines += [f'    {{"{hook["name"]}", reinterpret_cast<void*>(&ot_kit_{hook["name"]})}},' for hook in hooks]
    if not hooks:
        lines.append("    {nullptr, nullptr},")
    lines += ["};", f"extern const std::size_t kEntryCount = {len(hooks)};", ""]
    if standalone:
        # the named services the probes stubbed at one function entry and no live call names: the resolver finds them by name
        live_names = {service_name(call) for call in calls}
        named = []
        for service, declarations in sorted(stub_declarations().items()):
            addresses = {item["address"] for item in declarations}
            if service.startswith("engine_") or service in live_names or len(addresses) != 1 or any("site" in item["options"] for item in declarations):
                continue
            named.append((service, next(iter(addresses))))
        lines.append("const ServiceAddress kServices[] = {")
        lines += [f'    {{"{service}", 0x{address:08x}u}},' for service, address in named]
        if not named:
            lines.append('    {"", 0u},')
        lines += ["};", f"const std::size_t kServiceCount = {len(named)};", ""]
        # every native by retail address, sorted: the resolver's table (hooked or not, patched or not)
        table = [(int(hook["builds"]["canonical"]), hook, "ot_kit_") for hook in hooks] + [(int(row["address"]), row, "ot_native_") for row in natives]
        lines.append("const NativeEntry kNatives[] = {")
        for address, row, prefix in sorted(table, key=lambda item: item[0]):
            lines.append(f'    {{0x{address:08x}u, "{row["name"]}", Convention::{CONVENTIONS[row["convention"]]}, {int(row["stack_args"])}, '
                         f'reinterpret_cast<void*>(&{prefix}{row["name"]})}},')
        lines += ["};", f"const std::size_t kNativeCount = {len(table)};", ""]
    lines += ["}  // namespace opentony::kitdll", ""]
    return "\n".join(lines)
