"""Export per-function data-flow facts from Ghidra for the platform audit.

For every non-thunk function this records, as normalized symbolic
expressions (see `tony.hal.expr`):

- each CALL/CALLIND: call site, direct target or target expression, and the
  argument expressions;
- each STORE and each direct write to a global: destination address and
  stored value;
- each returned value.

Two decompiler passes run inside one transaction that is rolled back, so the
Ghidra project is never modified: the first pass commits the decompiler's
inferred prototypes (which is what lets callers pass ECX to `thiscall`
wrappers), and pins the platform import thunks to their documented stdcall
arities; the second pass exports the facts.
"""

from __future__ import annotations

import hashlib
import json
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

from ..common import ROOT, load_yaml, resolve

FACTS_DIR = "build/hal"
FACTS_VERSION = 17
REFINE_ROUNDS = 4

# Imports whose Ghidra thunks carry no prototype (no type library covers
# them). Stack argument counts come from the DirectX 7 / DirectPlay headers.
THUNK_ARITY = {
    "DirectDrawCreateEx": 4,
    "DirectDrawEnumerateExA": 3,
    "DirectInputCreateA": 4,
    "Ordinal_1": 3,  # DSOUND.DLL #1 DirectSoundCreate(guid, out, outer)
    "Ordinal_4": 5,  # DPLAYX.DLL #4 DirectPlayLobbyCreateA(guid, out, outer, data, size)
}


def facts_path(build: str) -> Path:
    return resolve(FACTS_DIR) / f"facts-{build}.json"


def _signed(value: int) -> int:
    return value - (1 << 32) if value >= (1 << 31) else value


def _describer(program):
    from ghidra.program.model.pcode import PcodeOp

    stack_pointer = program.getCompilerSpec().getStackPointer().getOffset()
    passthrough = {PcodeOp.COPY, PcodeOp.CAST, PcodeOp.INT_ZEXT, PcodeOp.INT_SEXT, PcodeOp.INDIRECT, PcodeOp.SUBPIECE}

    def param_name(varnode) -> str | None:
        high = varnode.getHigh()
        try:
            symbol = high.getSymbol() if high is not None else None
            if symbol is not None and symbol.isParameter():
                return f"arg{symbol.getCategoryIndex()}"
        except Exception:  # noqa: BLE001 - Java exceptions from partial HighSymbols
            return None
        return None

    def describe(varnode, depth: int = 0) -> str:
        if varnode is None:
            return "?"
        if depth > 20:
            return "…"
        if varnode.isConstant():
            return hex(varnode.getOffset())
        definition = varnode.getDef()
        if definition is None:
            name = param_name(varnode)
            if name:
                return name
            if varnode.isAddress():
                return "G" + hex(varnode.getOffset())
            if varnode.getAddress().isStackAddress():
                return f"S{_signed(varnode.getOffset())}"
            if varnode.isRegister() and varnode.getOffset() == stack_pointer:
                return "SP"
            return "in:" + str(varnode)
        if definition.getOpcode() in (PcodeOp.INDIRECT, PcodeOp.MULTIEQUAL):
            # Memory that a call may have written (out-parameters) or a merge
            # of the same memory: name the storage, never the earlier value.
            if varnode.isAddress():
                return "G" + hex(varnode.getOffset())
            if varnode.getAddress().isStackAddress():
                return f"S{_signed(varnode.getOffset())}"
        return describe_op(definition, depth)

    def describe_op(op, depth: int) -> str:
        code = op.getOpcode()
        inputs = [op.getInput(i) for i in range(op.getNumInputs())]
        if code == PcodeOp.LOAD:
            pointer = describe(inputs[1], depth + 1)
            if pointer.startswith("0x"):
                return "G" + pointer
            if pointer.startswith("&S"):
                return pointer[1:]
            return f"*({pointer})"
        if code in passthrough:
            return describe(inputs[0], depth + 1)
        if code in (PcodeOp.INT_ADD, PcodeOp.PTRSUB):
            left, right = describe(inputs[0], depth + 1), describe(inputs[1], depth + 1)
            if left == "SP" and right.startswith("0x"):
                return f"&S{_signed(int(right, 16))}"
            if left.startswith("0x") and right.startswith("0x"):
                return hex((int(left, 16) + int(right, 16)) & 0xFFFFFFFF)
            if left.startswith("0x"):
                left, right = right, left
            return f"({left}+{right})"
        if code == PcodeOp.PTRADD:
            base, index, size = (describe(item, depth + 1) for item in inputs[:3])
            return f"({base}+{index}*{size})"
        if code == PcodeOp.INT_MULT:
            return f"({describe(inputs[0], depth + 1)}*{describe(inputs[1], depth + 1)})"
        if code == PcodeOp.MULTIEQUAL:
            parts = sorted({describe(item, depth + 3) for item in inputs})
            return parts[0] if len(parts) == 1 else "phi{" + "|".join(parts) + "}"
        if code == PcodeOp.CALL:
            return f"ret({hex(inputs[0].getOffset())})"
        if code == PcodeOp.CALLIND:
            return f"reti({hex(op.getSeqnum().getTarget().getOffset())})"
        return op.getMnemonic() + "(" + ",".join(describe(item, depth + 1) for item in inputs) + ")"

    return describe, describe_op


def _import_tables(program) -> tuple[dict[int, str], dict[int, str]]:
    """Return ({IAT slot: 'DLL!name'}, {thunk function entry: 'DLL!name'})."""

    listing = program.getListing()
    symbols = program.getSymbolTable()
    slots: dict[int, str] = {}
    for reference in program.getReferenceManager().getExternalReferences():
        location = reference.getExternalLocation()
        name = f"{str(location.getLibraryName()).upper()}!{location.getLabel()}"
        slots[int(reference.getFromAddress().getOffset())] = name
    thunks: dict[int, str] = {}
    for function in program.getFunctionManager().getFunctions(True):
        if not function.isThunk():
            continue
        target = function.getThunkedFunction(True)
        if target is not None and target.isExternal():
            location = target.getExternalLocation()
            thunks[int(function.getEntryPoint().getOffset())] = (
                f"{str(location.getLibraryName()).upper()}!{location.getLabel()}"
            )
    del listing, symbols
    return slots, thunks


def _pin_thunks(program, thunks: dict[int, str]) -> None:
    from ghidra.program.model.data import Undefined4DataType
    from ghidra.program.model.listing import Function, ParameterImpl
    from ghidra.program.model.symbol import SourceType
    from java.util import ArrayList

    manager = program.getFunctionManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    for entry, name in thunks.items():
        arity = THUNK_ARITY.get(name.split("!", 1)[1])
        if arity is None:
            continue
        function = manager.getFunctionAt(space.getAddress(entry))
        parameters = ArrayList()
        for index in range(arity):
            parameters.add(ParameterImpl(f"p{index}", Undefined4DataType.dataType, program))
        function.setCallingConvention("__stdcall")
        function.replaceParameters(parameters, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, True,
                                   SourceType.USER_DEFINED)


def _commit_prototypes(program, decompiler, monitor) -> int:
    from ghidra.program.model.pcode import HighFunctionDBUtil
    from ghidra.program.model.symbol import SourceType

    committed = 0
    for function in program.getFunctionManager().getFunctions(True):
        if function.isThunk() or function.isExternal() or function.getSignatureSource() != SourceType.DEFAULT:
            continue
        result = decompiler.decompileFunction(function, 90, monitor)
        high = result.getHighFunction() if result is not None else None
        if high is None:
            continue
        try:
            HighFunctionDBUtil.commitParamsToDatabase(high, True, HighFunctionDBUtil.ReturnCommitOption.COMMIT,
                                                      SourceType.ANALYSIS)
            committed += 1
        except Exception:  # noqa: BLE001, S112 - keep the default prototype
            continue
    return committed


def _pushes(program, site: int, limit: int = 8) -> list[str]:
    """Operands pushed before the call at `site`, nearest first.

    Fallback for call sites where the decompiler attached no arguments (for
    example COM calls whose stack cleanup it could not model). Immediates are
    exact; registers are resolved only through a preceding `mov reg, imm` or
    `lea reg, [esp+n]` in the same block, otherwise reported as `?reg`.
    """

    from ghidra.program.model.scalar import Scalar

    listing = program.getListing()
    space = program.getAddressFactory().getDefaultAddressSpace()
    instruction = listing.getInstructionAt(space.getAddress(site))
    pushes: list[str] = []
    steps = 0
    while instruction is not None and len(pushes) < limit and steps < 40:
        instruction = instruction.getPrevious()
        steps += 1
        if instruction is None or instruction.getFlowType().isCall() or instruction.getFlowType().isJump():
            break
        if str(instruction.getMnemonicString()).upper() != "PUSH":
            continue
        objects = instruction.getOpObjects(0)
        value = "?"
        if len(objects) == 1 and isinstance(objects[0], Scalar):
            value = hex(objects[0].getUnsignedValue() & 0xFFFFFFFF)
        elif len(objects) == 1 and hasattr(objects[0], "getName"):
            register = str(objects[0].getName())
            value = f"?{register.lower()}"
            probe = instruction.getPrevious()
            for _ in range(12):
                if probe is None or probe.getFlowType().isCall():
                    break
                text = str(probe)
                if text.upper().startswith(f"MOV {register.upper()},0X"):
                    value = hex(int(text.split(",")[1], 16))
                    break
                if text.upper().startswith((f"MOV {register.upper()},", f"LEA {register.upper()},")):
                    break
                probe = probe.getPrevious()
        pushes.append(value)
    return pushes


def _vtable_evidence(program, site: int) -> list[str] | None:
    """Instruction-level shape of `call [R+off]` / `call [R]`.

    Returns [object register, last pushed operand], or None for any other
    call form (function-pointer tables, `call reg`). R's defining load
    `mov R, [X]` names the object register X. MSVC passes a C++ virtual's
    `this` in ECX and never pushes it; a COM call pushes the object last
    (`this` is the first stack argument). Special values:

    - pushed "stale": X was written after that push (a prologue register
      save), so the push did not push the object;
    - object "ecx" with pushed "none": ECX is (re)loaded before the call and
      not pushed afterwards (`push ecx; mov ecx, ebp; call [eax+0x24]`),
      i.e. a C++ virtual; only reported when the last push is not X.

    The backward window stops at calls, unconditional jumps and returns.
    """

    listing = program.getListing()
    space = program.getAddressFactory().getDefaultAddressSpace()
    call = listing.getInstructionAt(space.getAddress(site))
    if call is None:
        return None
    text = str(call).upper().replace(" ", "")
    if not text.startswith("CALLDWORDPTR["):
        return None
    operand = text[len("CALLDWORDPTR["):].rstrip("]")
    base = operand.split("+")[0]
    if len(base) != 3 or not base.startswith("E") or "*" in operand or operand.count("+") > 1:
        return None
    if "+" in operand and not operand.split("+")[1].startswith("0X"):
        return None
    window = []
    probe = call.getPrevious()
    for _ in range(24):
        if probe is None:
            break
        flow = probe.getFlowType()
        if flow.isCall() or flow.isTerminal() or (flow.isJump() and not flow.isConditional()):
            break
        window.append(str(probe).upper().replace(" ", ""))
        probe = probe.getPrevious()
    obj = None
    for current in window:
        prefix = f"MOV{base},DWORDPTR["
        if current.startswith(prefix):
            value = current[len(prefix):].rstrip("]")
            obj = value.lower() if len(value) == 3 and value.startswith("E") else "?"
            break
    push_index = next((i for i, current in enumerate(window) if current.startswith("PUSH")), None)
    pushed = window[push_index][4:].lower() if push_index is not None else ""
    if obj not in (None, "?") and push_index is not None:
        writes = (f"MOV{obj.upper()},", f"LEA{obj.upper()},", f"POP{obj.upper()}", f"XOR{obj.upper()},")
        write_index = next((i for i, current in enumerate(window) if current.startswith(writes)), None)
        if write_index is not None and write_index < push_index:
            pushed = "stale"
    if base != "ECX" and not (obj not in (None, "?") and pushed == obj):
        ecx_write = next((i for i, current in enumerate(window)
                          if current.startswith(("MOVECX,", "LEAECX,", "ADDECX,", "SUBECX,"))), None)
        if ecx_write is not None and "PUSHECX" not in window[:ecx_write]:
            return ["ecx", "none"]
    return [obj or "?", pushed]


def _export_function(program, decompiler, monitor, function, describers) -> dict[str, Any]:
    from ghidra.program.model.pcode import PcodeOp

    describe, describe_op = describers
    references = program.getReferenceManager().getReferencesTo(function.getEntryPoint())
    record: dict[str, Any] = {
        "a": int(function.getEntryPoint().getOffset()),
        "name": str(function.getName()),
        "end": int(function.getBody().getMaxAddress().getOffset()),
        "refs": sorted({int(ref.getFromAddress().getOffset()) for ref in references}),
        "calls": [],
        "stores": [],
        "ret": [],
    }
    result = decompiler.decompileFunction(function, 90, monitor)
    high = result.getHighFunction() if result is not None else None
    if high is None:
        record["fail"] = True
        return record
    symbols = high.getLocalSymbolMap()
    for index in range(symbols.getNumParams()):
        storage = symbols.getParamSymbol(index).getStorage()
        record.setdefault("params", []).append(
            f"S{storage.getStackOffset()}" if storage.isStackStorage() else str(storage))
    ops = high.getPcodeOps()
    while ops.hasNext():
        op = ops.next()
        code = op.getOpcode()
        site = int(op.getSeqnum().getTarget().getOffset())
        if code in (PcodeOp.CALL, PcodeOp.CALLIND):
            target = hex(op.getInput(0).getOffset()) if code == PcodeOp.CALL else describe(op.getInput(0))
            arguments = [describe(op.getInput(i)) for i in range(1, op.getNumInputs())]
            row = [site, "d" if code == PcodeOp.CALL else "i", target, arguments]
            if not arguments:
                row.append(_pushes(program, site))
            record["calls"].append(row)
            if code == PcodeOp.CALLIND:
                evidence = _vtable_evidence(program, site)
                if evidence is not None:
                    record.setdefault("vtable", {})[str(site)] = evidence
        elif code == PcodeOp.STORE:
            record["stores"].append([site, describe(op.getInput(1)), describe(op.getInput(2))])
        elif code == PcodeOp.RETURN and op.getNumInputs() > 1:
            record["ret"].append(describe(op.getInput(1)))
        else:
            output = op.getOutput()
            if output is not None and output.isAddress() and code not in (PcodeOp.INDIRECT, PcodeOp.MULTIEQUAL):
                value = describe_op(op, 0)
                if value != "G" + hex(output.getOffset()):  # skip no-op self copies
                    record["stores"].append([site, f"{output.getOffset():#x}", value])
    return record


def _export_functions(program, decompiler, monitor, only: set[int] | None = None) -> list[dict[str, Any]]:
    describers = _describer(program)
    rows: list[dict[str, Any]] = []
    for function in program.getFunctionManager().getFunctions(True):
        if function.isThunk() or function.isExternal():
            continue
        if only is not None and int(function.getEntryPoint().getOffset()) not in only:
            continue
        rows.append(_export_function(program, decompiler, monitor, function, describers))
    return rows


def _write_overrides(program, overrides: dict[int, tuple[int, int]]) -> None:
    """Pin indirect call sites to stdcall with `this` plus the SDK arity."""

    from ghidra.program.model.data import (
        FunctionDefinitionDataType,
        ParameterDefinitionImpl,
        Undefined4DataType,
    )
    from ghidra.program.model.pcode import HighFunctionDBUtil

    manager = program.getFunctionManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    for site, (entry, stack_args) in sorted(overrides.items()):
        definition = FunctionDefinitionDataType(f"ot_hal_{site:08x}")
        definition.setReturnType(Undefined4DataType.dataType)
        definition.setArguments([ParameterDefinitionImpl(f"a{i}", Undefined4DataType.dataType, None)
                                 for i in range(stack_args)])
        definition.setCallingConvention("__stdcall")
        HighFunctionDBUtil.writeOverride(manager.getFunctionAt(space.getAddress(entry)),
                                         space.getAddress(site), definition)


def _ground_truth(program, slots: dict[int, str], thunks: dict[int, str], monitor) -> dict[str, Any]:
    """Instruction-level sets that do not depend on decompilation.

    - `imports`: every instruction that calls or jumps through an IAT slot or
      calls an import thunk: [site, function, api, call|jump];
    - `indirect`: every computed CALL: [site, function, vtable evidence];
    - `orphans`: {address: subroutine entry} for code that Ghidra left
      outside every function (dialog procedures, tails after calls it
      thinks never return); `function` is that entry for such sites.
    """

    from ghidra.program.model.block import IsolatedEntrySubModel

    listing = program.getListing()
    functions = program.getFunctionManager()
    symbols = program.getSymbolTable()
    model = IsolatedEntrySubModel(program)
    orphans: dict[int, int] = {}

    references = program.getReferenceManager()

    def entered(start) -> bool:
        """True when something other than a jump enters `start` (a call, or a
        stored code pointer such as a dialog procedure)."""

        return any(not reference.getReferenceType().isJump() for reference in references.getReferencesTo(start))

    def entry_of(address) -> int:
        owner = functions.getFunctionContaining(address)
        if owner is not None:
            return int(owner.getEntryPoint().getOffset())
        key = int(address.getOffset())
        if key not in orphans:
            # VC6 emits each function contiguously: code Ghidra left outside
            # every body (after a call it wrongly thinks never returns) belongs
            # to the preceding function, unless the island is itself entered
            # by a call or a stored pointer.
            blocks = model.getCodeBlocksContaining(address, monitor)
            starts = [start for block in blocks for start in block.getStartAddresses()]
            island = min(starts, key=lambda start: start.getOffset()) if starts else None
            backward = functions.getFunctions(address, False)
            previous = backward.next() if backward.hasNext() else None
            if island is not None and (previous is None or entered(island)):
                orphans[key] = int(island.getOffset())
            elif previous is not None:
                orphans[key] = int(previous.getEntryPoint().getOffset())
            else:
                orphans[key] = 0
        return orphans[key]

    imports: list[list[Any]] = []
    indirect: list[list[Any]] = []
    for instruction in listing.getInstructions(True):
        flow = instruction.getFlowType()
        if not (flow.isCall() or flow.isJump()):
            continue
        site = int(instruction.getAddress().getOffset())
        api = None
        for reference in instruction.getReferencesFrom():
            to = reference.getToAddress()
            if to.isExternalAddress():
                symbol = symbols.getPrimarySymbol(to)
                if symbol is not None:
                    api = f"{str(symbol.getParentNamespace().getName()).upper()}!{symbol.getName()}"
                continue
            target = int(to.getOffset())
            api = slots.get(target) or (thunks.get(target) if flow.isCall() else None) or api
        if api is None and reference_is_external(instruction):
            api = "?external"
        if api is None and flow.isCall() and flow.isComputed():
            api = _register_import(instruction, slots)
        if api is not None:
            entry = entry_of(instruction.getAddress())
            if entry not in thunks:
                imports.append([site, entry, api, "call" if flow.isCall() else "jump"])
        elif flow.isCall() and flow.isComputed():
            indirect.append([site, entry_of(instruction.getAddress()), _vtable_evidence(program, site)])
    # reference sources outside every function (function pointers stored by orphan code)
    references = program.getReferenceManager()
    for function in functions.getFunctions(True):
        for reference in references.getReferencesTo(function.getEntryPoint()):
            source = reference.getFromAddress()
            if source.isMemoryAddress() and functions.getFunctionContaining(source) is None                     and listing.getInstructionAt(source) is not None:
                entry_of(source)
    return {"imports": imports, "indirect": indirect, "orphans": {hex(k): v for k, v in sorted(orphans.items())}}


def _register_import(call, slots: dict[int, str]) -> str | None:
    """`call reg` whose register was last loaded from an import slot.

    MSVC caches an import in a register (`mov edi, [__imp__mciSendCommandA]`)
    before repeated calls. Ghidra usually resolves these; this instruction
    scan makes sure an unresolved one is not mistaken for a function pointer.
    """

    text = str(call).upper().replace(" ", "")
    if not text.startswith("CALLE") or len(text) != 7:
        return None
    register = text[4:]
    probe = call.getPrevious()
    for _ in range(400):
        if probe is None:
            return None
        current = str(probe).upper().replace(" ", "")
        if current.startswith((f"MOV{register},", f"LEA{register},", f"POP{register}", f"XOR{register},")):
            if current.startswith(f"MOV{register},DWORDPTR[0X"):
                address = int(current[len(f"MOV{register},DWORDPTR["):].rstrip("]"), 16)
                return slots.get(address)
            return None
        if probe.getFlowType().isTerminal():
            return None
        probe = probe.getPrevious()
    return None


def reference_is_external(instruction) -> bool:
    return any(reference.getToAddress().isExternalAddress() for reference in instruction.getReferencesFrom())


def _project(build: str) -> tuple[Path, str, str]:
    spec = load_yaml("re/config/ghidra.yml")["ghidra"]
    if build == "canonical":
        from ..ghidra_ops import _exe_path

        return resolve(spec["project_dir"]), spec["project_name"], _exe_path().name
    from ..xref.common import BUILD_DIR, verified_variant_path

    return (BUILD_DIR / f"ghidra-{build}").resolve(), f"xref-{build}", verified_variant_path(build).name


def _hexkeys(table: dict[int, str]) -> dict[str, str]:
    return {hex(key): value for key, value in sorted(table.items())}


def _fingerprint(build: str, salt: str = "") -> str:
    """Changes whenever the Ghidra project, this exporter or `salt` changes."""

    parent, name, _exe = _project(build)
    digest = hashlib.sha256(f"v{FACTS_VERSION}:{salt}".encode())
    digest.update(Path(__file__).read_bytes())
    # Only the program databases: opening a project rewrites its index and
    # property files even when nothing is saved.
    for path in sorted((parent / f"{name}.rep").rglob("*.gbf")):
        if path.is_file():
            stat = path.stat()
            digest.update(f"{path.relative_to(parent)}:{stat.st_size}:{stat.st_mtime_ns}".encode())
    return digest.hexdigest()


Refine = Callable[[dict[str, Any]], dict[int, tuple[int, int]]]


def export(build: str = "canonical", *, force: bool = False, refine: Refine | None = None,
           salt: str = "") -> dict[str, Any]:
    """Return the facts for `build`, re-exporting when the project changed.

    `refine(facts)` returns {call site: (function entry, stack arguments)} for
    indirect calls known to be stdcall COM methods; those sites get call-site
    overrides and their functions are decompiled again, until no new site
    appears. `salt` must change whenever `refine`'s answers can change.
    """

    path = facts_path(build)
    fingerprint = _fingerprint(build, salt)
    if path.is_file() and not force:
        cached = json.loads(path.read_text(encoding="utf-8"))
        if cached.get("fingerprint") == fingerprint:
            return cached

    from ..ghidra_ops import _require_pyghidra

    pyghidra = _require_pyghidra()
    spec = load_yaml("re/config/ghidra.yml")["ghidra"]
    pyghidra.start(install_dir=resolve(spec["install_dir"]))
    from ghidra.app.decompiler import DecompInterface

    parent, name, exe_name = _project(build)
    if not (parent / f"{name}.gpr").is_file():
        raise SystemExit(f"Ghidra project {parent / name} missing (canonical: tony ghidra rebuild; "
                         f"{build}: tony xref features --build {build})")
    started = time.time()
    lock = None
    if build == "canonical":
        from ..ghidra_lock import ghidra_project_lock

        lock = ghidra_project_lock()
        lock.__enter__()
    try:
        with pyghidra.open_project(parent, name, create=False) as project, \
                pyghidra.program_context(project, f"/{exe_name}") as program:
            monitor = pyghidra.task_monitor()
            slots, thunks = _import_tables(program)
            ground = _ground_truth(program, slots, thunks, monitor)
            transaction = program.startTransaction("OpenTony hal facts (rolled back)")
            try:
                _pin_thunks(program, thunks)
                decompiler = DecompInterface()
                decompiler.openProgram(program)
                committed = _commit_prototypes(program, decompiler, monitor)
                decompiler.dispose()
                decompiler = DecompInterface()
                decompiler.openProgram(program)
                functions = _export_functions(program, decompiler, monitor)
                overrides: dict[int, tuple[int, int]] = {}
                for _round in range(REFINE_ROUNDS):
                    if refine is None:
                        break
                    wanted = refine({"iat": _hexkeys(slots), "thunks": _hexkeys(thunks), "functions": functions})
                    new = {site: value for site, value in wanted.items() if overrides.get(site) != value}
                    if not new:
                        break
                    _write_overrides(program, new)
                    overrides.update(new)
                    decompiler.dispose()
                    decompiler = DecompInterface()
                    decompiler.openProgram(program)
                    again = {record["a"]: record for record in _export_functions(
                        program, decompiler, monitor, {entry for entry, _ in new.values()})}
                    functions = [again.get(record["a"], record) for record in functions]
                decompiler.dispose()
            finally:
                program.endTransaction(transaction, False)
    finally:
        if lock is not None:
            lock.__exit__(None, None, None)
    facts = {
        "version": FACTS_VERSION,
        "build": build,
        "fingerprint": fingerprint,
        "iat": _hexkeys(slots),
        "thunks": _hexkeys(thunks),
        "committed_prototypes": committed,
        "com_overrides": len(overrides),
        "seconds": round(time.time() - started, 1),
        "ground": ground,
        "functions": functions,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(facts), encoding="utf-8")
    return facts


__all__ = ["ROOT", "export", "facts_path"]
