"""Milestone 11c (docs/PORT64_AUDIT.md D4): the generated typed views read and write exactly the bytes the corpus says.

The check is independent of the generator's own arithmetic: this file derives every field's offset and width from `re/types` and emits a C++ program that sets
each field through its view accessor, then reads the raw bytes at the corpus offset (and the bytes around it, which must not move), and reads the field back.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import guest_layouts

from tony.recovered_types import _expression_size, load_type_definitions, parse_type_expression

RUNTIME = ROOT / "src/runtime"
VIEWS = RUNTIME / "guest_views_generated.hpp"

PRELUDE = r"""
#include "guest_views_generated.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

using namespace opentony::runtime;
using namespace opentony::runtime::guest;

static int g_failures = 0;
static void fail(const char* label, const char* what) {
    if (g_failures++ < 20) std::printf("FAIL %s: %s\n", label, what);
}

template <class T> static std::array<std::uint8_t, sizeof(T)> to_bytes(T v) {
    std::array<std::uint8_t, sizeof(T)> out;
    std::memcpy(out.data(), &v, sizeof(T));
    return out;
}
template <class T> static T from_bytes(std::initializer_list<unsigned> b) {
    std::array<std::uint8_t, sizeof(T)> raw{};
    std::size_t i = 0;
    for (unsigned x : b) raw[i++] = static_cast<std::uint8_t>(x);
    T v;
    std::memcpy(&v, raw.data(), sizeof(T));
    return v;
}
static void scribble(RetailMemory& m, std::uint32_t base, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) m.set8(base + static_cast<std::uint32_t>(i), 0xa5);
}
// `width` bytes at `at` equal `expected`, and every other byte of [base, base + size) is still the scribble
template <class T>
static void check(const char* label, RetailMemory& m, std::uint32_t base, std::size_t size, std::uint32_t at, std::size_t width, T got,
                  std::initializer_list<unsigned> expected) {
    if (sizeof(T) != width) { fail(label, "accessor width differs from the corpus width"); return; }
    const auto bytes = to_bytes(got);
    std::size_t i = 0;
    for (unsigned x : expected) {
        if (bytes[i] != x) { fail(label, "getter does not return what the setter stored"); return; }
        if (m.u8(at + static_cast<std::uint32_t>(i)) != x) { fail(label, "raw byte at the corpus offset is wrong"); return; }
        ++i;
    }
    for (std::uint32_t a = base; a < base + size; ++a) {
        if (a >= at && a < at + width) continue;
        if (m.u8(a) != 0xa5) { fail(label, "a neighbouring byte moved"); return; }
    }
}
template <class F> static void faults(const char* label, F f) {
    try { f(); } catch (const MemoryFault&) { return; }
    fail(label, "an index past the end did not fault");
}
"""


def _pattern(seed: int, width: int) -> list[int]:
    """Distinct non-0xa5 bytes, different for every field."""
    return [((seed * 37 + 11 * k + 5) % 251) if ((seed * 37 + 11 * k + 5) % 251) != 0xA5 else 0x11 for k in range(width)]


def _initializer(values: list[int]) -> str:
    return "{" + ", ".join(f"0x{value:02x}" for value in values) + "}"


def generate_program() -> tuple[str, int]:
    definitions = load_type_definitions()
    sizes = {name: definition.get("size") for name, definition in definitions.items()}

    def resolve(name: str) -> str:
        while definitions.get(name, {}).get("kind") == "alias":
            name = str(definitions[name]["target"])
        return name

    lines: list[str] = [PRELUDE]
    calls: list[str] = []
    seed = 0
    checks = 0
    for name in sorted(definitions):
        definition = definitions[name]
        if definition["kind"] != "fixed_layout" or name != resolve(name):
            continue
        tid = guest_layouts.identifier(name)
        fields = sorted(definition.get("fields") or [], key=lambda field: int(field["offset"]))
        guest_layouts.assign_ids(fields)
        body: list[str] = []
        extent = int(definition.get("size") or 0)
        for field in fields:
            expression = parse_type_expression(field["type"])
            width = _expression_size(expression, sizes, guest_layouts.POINTER_SIZE)
            if width is not None:
                extent = max(extent, int(field["offset"]) + width)
        size = extent + 16
        for field in fields:
            member = guest_layouts.member_name(field["_id"])
            offset = int(field["offset"])
            expression = parse_type_expression(field["type"])
            label = f'"{name}.{field["name"]}"'
            scalar = guest_layouts.view_scalar(expression)
            pointer = guest_layouts.view_pointer(expression)
            element = expression.element if expression.kind == "array" else None
            element_cpp = (guest_layouts.view_scalar(element) or guest_layouts.view_pointer(element)) if element is not None else None
            named = resolve(expression.name or "") if expression.kind == "named" else None
            body.append(f"    if (v.{member}_address() != at + {offset}) fail({label}, \"address accessor\");")
            checks += 1
            if scalar is not None or pointer is not None:
                cpp = scalar or pointer
                width = _expression_size(expression, sizes, guest_layouts.POINTER_SIZE)
                seed += 1
                data = _pattern(seed, width)
                body.append(
                    f"    scribble(m, base, size); v.set_{member}(from_bytes<{cpp}>({_initializer(data)})); "
                    f"check<{cpp}>({label}, m, base, size, at + {offset}, {width}, v.{member}(), {_initializer(data)});"
                )
                checks += 1
            elif element is not None and element_cpp is not None:
                width = _expression_size(element, sizes, guest_layouts.POINTER_SIZE)
                count = expression.count
                for index in sorted({0, count - 1}):
                    seed += 1
                    data = _pattern(seed, width)
                    body.append(
                        f"    scribble(m, base, size); v.set_{member}({index}, from_bytes<{element_cpp}>({_initializer(data)})); "
                        f"check<{element_cpp}>({label}, m, base, size, at + {offset + index * width}, {width}, v.{member}({index}), {_initializer(data)});"
                    )
                    checks += 1
                body.append(f"    faults({label}, [&] {{ (void)v.{member}({count}); }});")
                body.append(f"    faults({label}, [&] {{ v.set_{member}({count}, {{}}); }});")
                checks += 2
            elif named is not None and named in definitions and definitions[named]["kind"] == "fixed_layout":
                body.append(f"    if (v.{member}().address() != at + {offset}) fail({label}, \"nested view address\");")
                checks += 1
            elif element is not None and element.kind == "named" and resolve(element.name or "") in definitions:
                inner = resolve(element.name or "")
                inner_size = sizes.get(inner)
                if definitions[inner]["kind"] == "fixed_layout" and inner_size is not None:
                    body.append(f"    if (v.{member}(1).address() != at + {offset + inner_size}) fail({label}, \"nested array view address\");")
                    body.append(f"    faults({label}, [&] {{ (void)v.{member}({expression.count}); }});")
                    checks += 2
        calls.append(f"    test_{tid}();")
        lines.append(f"static void test_{tid}() {{")
        lines.append(f"    RetailMemory m; const std::uint32_t base = 0x20000, at = base + 3; const std::size_t size = {size};")
        lines.append("    m.add_region(base, size + 8);")
        lines.append(f"    {tid}View v(m, at); (void)v; (void)at;")
        lines.extend(body)
        lines.append("}")
    lines.append("int main() {")
    lines.extend(calls)
    lines.append('    std::printf("%d failures\\n", g_failures);')
    lines.append("    return g_failures == 0 ? 0 : 1;")
    lines.append("}")
    return "\n".join(lines), checks


def _host_compiler() -> list[str] | None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    return [compiler] if compiler else None


def _compilers() -> list[tuple[str, list[str]]]:
    found: list[tuple[str, list[str]]] = []
    host = _host_compiler()
    if host:
        found.append(("host", host))
    for root in sorted((ROOT / ".tools/llvm-mingw").glob("*/bin")):
        for triple in ("i686-w64-mingw32", "x86_64-w64-mingw32"):
            compiler = root / f"{triple}-clang++"
            if compiler.exists():
                found.append((triple, [str(compiler)]))
    return found


def test_generated_views_are_current():
    assert guest_layouts.generate_views() == VIEWS.read_text(encoding="utf-8").replace("\r\n", "\n")


@pytest.mark.parametrize("label,command", _compilers() or [pytest.param("none", [], marks=pytest.mark.skip("no C++ compiler"))])
def test_views_compile_in_every_target(label: str, command: list[str], tmp_path: Path):
    source = tmp_path / "views.cpp"
    source.write_text('#include "guest_views_generated.hpp"\nint main() {}\n')
    result = subprocess.run([*command, "-std=c++20", "-fsyntax-only", f"-I{RUNTIME}", str(source)], capture_output=True, text=True, timeout=300, check=False)
    assert result.returncode == 0, result.stderr[-2000:]


@pytest.fixture(scope="module")
def view_program(tmp_path_factory):
    compiler = _host_compiler()
    if compiler is None:
        pytest.skip("no host C++ compiler")
    directory = tmp_path_factory.mktemp("views")
    source = directory / "views_check.cpp"
    text, checks = generate_program()
    source.write_text(text)
    binary = directory / "views_check"
    build = subprocess.run([*compiler, "-std=c++20", "-O0", f"-I{RUNTIME}", str(source), "-o", str(binary)], capture_output=True, text=True, timeout=900, check=False)
    assert build.returncode == 0, build.stderr[-3000:]
    return binary, checks


def test_every_view_field_reads_and_writes_the_corpus_bytes(view_program):
    binary, checks = view_program
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, check=False)
    assert run.returncode == 0, run.stdout[-3000:]
    assert checks > 2000


def test_a_wrong_view_offset_is_caught(view_program, tmp_path: Path):
    """Mutation: move one field of the register file by two bytes in the view header; the program must fail."""
    compiler = _host_compiler()
    text = VIEWS.read_text(encoding="utf-8")
    marker = "kRuntimeGteRegisterFile_otz"
    broken = text.replace(f"address_ + {marker}", f"address_ + {marker} + 2", 1)
    assert broken != text
    for name in ("guest_views_generated.hpp",):
        (tmp_path / name).write_text(broken)
    for name in ("guest_layouts_generated.hpp", "guest_ptr.hpp", "guest_view.hpp", "retail_memory.hpp"):
        (tmp_path / name).write_text((RUNTIME / name).read_text(encoding="utf-8"))
    source = tmp_path / "views_check.cpp"
    source.write_text(generate_program()[0])
    binary = tmp_path / "views_check"
    build = subprocess.run([*compiler, "-std=c++20", "-O0", f"-I{tmp_path}", str(source), "-o", str(binary)], capture_output=True, text=True, timeout=900, check=False)
    assert build.returncode == 0, build.stderr[-3000:]
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, check=False)
    assert run.returncode != 0 and "RuntimeGteRegisterFile.otz" in run.stdout
