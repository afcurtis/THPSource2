"""Milestone 11a (docs/PORT64_AUDIT.md): every audited retail layout is asserted at compile time in the 32-bit and the 64-bit builds."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import guest_layouts

RUNTIME = ROOT / "src/runtime"
HEADER = RUNTIME / "guest_layouts_generated.hpp"


def _compilers() -> list[tuple[str, list[str]]]:
    found: list[tuple[str, list[str]]] = []
    host = shutil.which("g++") or shutil.which("clang++")
    if host:
        found.append(("host", [host]))
    for root in sorted((ROOT / ".tools/llvm-mingw").glob("*/bin")):
        for triple in ("i686-w64-mingw32", "x86_64-w64-mingw32"):
            compiler = root / f"{triple}-clang++"
            if compiler.exists():
                found.append((triple, [str(compiler)]))
    return found


def _syntax_check(command: list[str], source: Path, include: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [*command, "-std=c++20", "-fsyntax-only", f"-I{include}", str(source)], capture_output=True, text=True, timeout=300, check=False
    )


def test_generated_header_is_current():
    assert guest_layouts.generate() == HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")


def test_every_sized_fixed_layout_is_asserted():
    from tony.recovered_types import load_type_definitions

    text = HEADER.read_text(encoding="utf-8")
    for name, definition in load_type_definitions().items():
        if definition["kind"] != "fixed_layout" or definition.get("size") is None:
            continue
        identifier = guest_layouts.identifier(name)
        assert f"inline constexpr std::size_t k{identifier}Size = {definition['size']};" in text, name
        for field in definition.get("fields") or []:
            assert f"k{identifier}_{guest_layouts.identifier(str(field['name']))} = 0x{int(field['offset']):x};" in text or (
                f"k{identifier}_{guest_layouts.identifier(str(field['name']))}_at_{int(field['offset']):x} = " in text
            ), f"{name}.{field['name']}"


@pytest.mark.parametrize("label,command", _compilers() or [pytest.param("none", [], marks=pytest.mark.skip("no C++ compiler"))])
def test_layouts_compile_in_every_target(label: str, command: list[str], tmp_path: Path):
    source = tmp_path / "layouts.cpp"
    source.write_text('#include "guest_layouts_generated.hpp"\nint main() {}\n')
    result = _syntax_check(command, source, RUNTIME)
    assert result.returncode == 0, result.stderr[-2000:]


def test_a_wrong_layout_fails_to_compile(tmp_path: Path):
    compilers = _compilers()
    if not compilers:
        pytest.skip("no C++ compiler")
    text = HEADER.read_text(encoding="utf-8")
    asserted = next(line for line in text.splitlines() if line.startswith("static_assert(sizeof(") and "Mirror) == k" in line)
    constant = asserted.split("== ")[1].split(",")[0]
    first = next(line for line in text.splitlines() if line.startswith(f"inline constexpr std::size_t {constant} = "))
    name, value = first.split(" = ")
    (tmp_path / "guest_layouts_generated.hpp").write_text(text.replace(first, f"{name} = {int(value.rstrip(';')) + 4};"))
    (tmp_path / "guest_ptr.hpp").write_text((RUNTIME / "guest_ptr.hpp").read_text())
    source = tmp_path / "layouts.cpp"
    source.write_text('#include "guest_layouts_generated.hpp"\nint main() {}\n')
    result = _syntax_check(compilers[0][1], source, tmp_path)
    assert result.returncode != 0
    assert "static assertion" in result.stderr or "static_assert" in result.stderr


def test_inventory_in_the_audit_is_current():
    document = guest_layouts.DOCUMENT.read_text(encoding="utf-8").replace("\r\n", "\n")
    assert guest_layouts.with_inventory(document) == document
