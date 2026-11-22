"""Export normalized per-function fingerprints from a Ghidra program."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
from pathlib import Path

from ..common import load_yaml, resolve
from ..ghidra_ops import _exe_path, _require_pyghidra
from .common import BUILD_DIR, CANONICAL, features_path, verified_variant_path

IMAGE_LOW, IMAGE_HIGH = 0x00400000, 0x02B00000
_HEX = re.compile(r"0x([0-9a-fA-F]+)")


def normalize_operand(text: str) -> str:
    """Replace in-image absolute addresses so relocated builds hash identically."""

    def replace(match: re.Match[str]) -> str:
        value = int(match.group(1), 16)
        return "A" if IMAGE_LOW <= value < IMAGE_HIGH else match.group(0)

    return _HEX.sub(replace, text)


def _digest(parts: list[str]) -> str:
    return hashlib.sha1("\n".join(parts).encode()).hexdigest()[:20]


def _export_program(program, pyghidra, exe: Path) -> dict:
    from ghidra.program.model.address import Address
    from ghidra.program.model.block import BasicBlockModel
    from ghidra.program.model.scalar import Scalar

    listing = program.getListing()
    monitor = pyghidra.task_monitor()
    blocks = BasicBlockModel(program)
    functions = []
    for function in program.getFunctionManager().getFunctions(True):
        tokens, mnemonics, callees, absolute, constants, strings = [], [], [], [], [], []
        for instruction in listing.getInstructions(function.getBody(), True):
            mnemonic = str(instruction.getMnemonicString())
            operands = [normalize_operand(str(instruction.getDefaultOperandRepresentation(i)))
                        for i in range(instruction.getNumOperands())]
            tokens.append(mnemonic + " " + ",".join(operands))
            mnemonics.append(mnemonic)
            flow = instruction.getFlowType()
            if flow.isCall():
                callees.extend(int(target.getOffset()) for target in (instruction.getFlows() or []))
            for index in range(instruction.getNumOperands()):
                for item in instruction.getOpObjects(index):
                    if isinstance(item, Scalar):
                        value = item.getUnsignedValue() & 0xFFFFFFFF
                        if IMAGE_LOW <= value < IMAGE_HIGH:
                            absolute.append(value)
                        elif abs(item.getSignedValue()) >= 0x100:
                            constants.append(int(item.getSignedValue()))
                    elif isinstance(item, Address):
                        value = int(item.getOffset())
                        if IMAGE_LOW <= value < IMAGE_HIGH and not flow.isCall() and not flow.isJump():
                            absolute.append(value)
            for reference in instruction.getReferencesFrom():
                if reference.getReferenceType().isData():
                    data = listing.getDataAt(reference.getToAddress())
                    if data is not None and data.hasStringValue():
                        strings.append(str(data.getValue()))
        block_count = 0
        iterator = blocks.getCodeBlocksContaining(function.getBody(), monitor)
        while iterator.hasNext():
            iterator.next()
            block_count += 1
        functions.append({
            "a": int(function.getEntryPoint().getOffset()),
            "name": str(function.getName()),
            "size": int(function.getBody().getNumAddresses()),
            "n": len(tokens),
            "bb": block_count,
            "h": _digest(tokens),
            "hm": _digest(mnemonics),
            "callees": callees,
            "abs": absolute,
            "consts": constants,
            "strs": strings,
            "thunk": bool(function.isThunk()),
        })
    strings = []
    for data in listing.getDefinedData(True):
        if data.hasStringValue():
            strings.append({"a": int(data.getAddress().getOffset()), "s": str(data.getValue())})
    return {"exe": str(exe), "functions": functions, "strings": strings}


def export(build: str = CANONICAL, reanalyze: bool = False) -> Path:
    pyghidra = _require_pyghidra()
    spec = load_yaml("re/config/ghidra.yml")["ghidra"]
    install = resolve(spec["install_dir"])
    output = features_path(build)
    output.parent.mkdir(parents=True, exist_ok=True)
    pyghidra.start(install_dir=install)

    if build == CANONICAL:
        exe = _exe_path()
        project_parent, project_name = resolve(spec["project_dir"]), spec["project_name"]
        if not (project_parent / f"{project_name}.gpr").exists():
            raise SystemExit("canonical Ghidra project missing; run: tony ghidra rebuild")
        from ..ghidra_lock import ghidra_project_lock

        with ghidra_project_lock(), pyghidra.open_project(project_parent, project_name, create=False) as project, \
                pyghidra.program_context(project, f"/{exe.name}") as program:
            result = _export_program(program, pyghidra, exe)
    else:
        exe = verified_variant_path(build)
        project_parent = (BUILD_DIR / f"ghidra-{build}").resolve()
        project_name = f"xref-{build}"
        fresh = reanalyze or not (project_parent / f"{project_name}.gpr").exists()
        if fresh:
            shutil.rmtree(project_parent, ignore_errors=True)
            project_parent.mkdir(parents=True, exist_ok=True)
        with pyghidra.open_project(project_parent, project_name, create=True) as project:
            if fresh:
                with pyghidra.program_loader().project(project).source(str(exe)).load() as results:
                    results.save(pyghidra.task_monitor())
            with pyghidra.program_context(project, f"/{exe.name}") as program:
                if fresh:
                    print(f"Analyzing {build} variant {exe} ...")
                    pyghidra.analyze(program, pyghidra.task_monitor())
                    program.save("OpenTony xref variant analysis", pyghidra.task_monitor())
                result = _export_program(program, pyghidra, exe)

    output.write_text(json.dumps(result), encoding="utf-8")
    print(f"Exported {len(result['functions'])} functions, {len(result['strings'])} strings: {output}")
    return output
