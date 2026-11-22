"""Milestone 11d (docs/PORT64_AUDIT.md D3, section 7): what production code may ask of `RetailMemory`.

The backend split (arena backend for production, sparse region backend for the fixtures) is only possible while production code uses one small
surface of the accessor class. This file pins that surface:

* scalar access goes through `guest::read_scalar` / `guest::write_scalar` or a generated view, never `m_.u32(address)` (any receiver name);
* scratch windows are requested with `ensure_region`; `add_region` and `regions()` belong to the fixture loaders and the replays only;
* nothing in production code compares two memories or constructs one (`RetailMemory g_memory` in `kit_dll.cpp` is the single instance).
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

# the accessor layer itself: the only places that may name the raw accessors
ACCESSOR_LAYER = {"retail_memory.hpp", "guest_view.hpp", "guest_views_generated.hpp"}
# fixture loaders and the replay engine: the only production-tree files that may add regions or walk them
SPARSE_USERS = {"retail_memory.hpp", "career_tables.hpp", "create_tables.hpp", "retail_calls_replay.hpp"}

RAW = re.compile(r"\b(?:m_|m|memory_?|mem|g_memory)\s*(?:\.|->)\s*(?:u8|s8|u16|s16|u32|s32|set8|set16|set32)\s*\(")
SPARSE = re.compile(r"\badd_region\s*\(|\.regions\s*\(\s*\)")
BYVALUE = re.compile(r"^\s*(?:static\s+)?RetailMemory\s+\w+\s*(?:;|\{|=)", re.MULTILINE)

# one live instance per process
SINGLE_INSTANCE = {"kit_dll.cpp": 1}


def production_sources():
    for path in sorted(SRC.rglob("*")):
        if path.suffix in (".cpp", ".hpp", ".h") and not path.name.endswith("_test.cpp"):
            yield path


def code_lines(text: str):
    for number, line in enumerate(text.splitlines(), 1):
        yield number, line.split("//", 1)[0]


def hits(pattern: re.Pattern[str], text: str) -> list[str]:
    return [f"{number}: {line.strip()}" for number, line in code_lines(text) if pattern.search(line)]


def test_no_raw_accessor_in_production_code():
    problems = []
    for path in production_sources():
        if path.name in ACCESSOR_LAYER:
            continue
        for hit in hits(RAW, path.read_text(encoding="utf-8", errors="replace")):
            problems.append(f"{path.relative_to(ROOT)}:{hit}")
    assert not problems, "raw accessor calls (use guest::read_scalar / write_scalar or a view):\n" + "\n".join(problems)


def test_regions_belong_to_the_fixture_loaders():
    problems = []
    for path in production_sources():
        if path.name in SPARSE_USERS:
            continue
        for hit in hits(SPARSE, path.read_text(encoding="utf-8", errors="replace")):
            problems.append(f"{path.relative_to(ROOT)}:{hit}")
    assert not problems, "add_region / regions() outside the fixture loaders (use ensure_region):\n" + "\n".join(problems)


def test_one_memory_instance_in_production():
    found = {}
    for path in production_sources():
        if path.name in SPARSE_USERS | ACCESSOR_LAYER:
            continue
        count = len(BYVALUE.findall(path.read_text(encoding="utf-8", errors="replace")))
        if count:
            found[path.name] = count
    assert found == SINGLE_INSTANCE, found


def test_the_checks_see_what_they_forbid():
    assert hits(RAW, "    const auto v = m.u32(a + 4);\n")
    assert hits(RAW, "    g_memory.set8(a, 1);\n")
    assert not hits(RAW, "    // m_.u32(a)\n    guest::read_scalar<std::uint32_t>(m_, a);\n")
    assert hits(SPARSE, "    m_.add_region(kScratch, 0x100);\n")
    assert hits(SPARSE, "    for (auto& r : after.regions()) {}\n")
    assert not hits(SPARSE, "    m_.ensure_region(kScratch, 0x100);\n")
    assert BYVALUE.findall("RetailMemory g_memory;\n")
    assert not BYVALUE.findall("void f(RetailMemory& m);\n")


# ---------------------------------------------------------------- 11d s2: the backend split (docs/PORT64_AUDIT.md D3, section 7)

KITS_CMAKE = SRC / "hooks" / "kits" / "CMakeLists.txt"
HOST_CMAKE = ROOT / "CMakeLists.txt"
HEADER = SRC / "runtime" / "retail_memory.hpp"
FLAT_DEFINE = "target_compile_definitions(opentony_kits_runtime PUBLIC OPENTONY_GUEST_MEMORY_FLAT=1)"


def test_production_dlls_build_the_arena_backend():
    text = KITS_CMAKE.read_text(encoding="utf-8")
    assert FLAT_DEFINE in text
    links = dict(re.findall(r"target_link_libraries\((\S+) PRIVATE ([^)]*)\)", text))
    for dll in ("opentony_native", "opentony_standalone"):
        assert "opentony_kits_runtime" in links[dll].split() and "opentony_kits_runtime_fixture" not in links[dll], links[dll]
    assert "opentony_kits_runtime_fixture" in links["opentony_${test}"].split()   # the 32-bit replays need the sparse backend
    assert "OPENTONY_GUEST_MEMORY_FLAT" not in HOST_CMAKE.read_text(encoding="utf-8")   # ctest and the layer tests are fixture builds


def test_sparse_members_exist_only_in_the_fixture_build():
    """Every sparse member of RetailMemory sits under `#if !OPENTONY_GUEST_MEMORY_FLAT` (a line-level preprocessor walk)."""

    blocks = []   # a stack of the open #if blocks: "sparse", "flat" or "other"
    outside = []
    for number, line in enumerate(HEADER.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("#if"):
            directive, _, condition = stripped.split("//", 1)[0].partition(" ")
            condition = condition.strip() if directive == "#if" else ""   # #ifdef / #ifndef are "other"
            blocks.append({"!OPENTONY_GUEST_MEMORY_FLAT": "sparse", "OPENTONY_GUEST_MEMORY_FLAT": "flat"}.get(condition, "other"))
        elif stripped.startswith("#else"):
            blocks[-1] = {"sparse": "flat", "flat": "sparse"}.get(blocks[-1], blocks[-1])
        elif stripped.startswith("#endif"):
            blocks.pop()
        elif re.search(r"void add_region\s*\(|\bregions_\b|\bregions\s*\(\s*\)\s*const|operator==|\blive_\b|std::map|<map>", line.split("//", 1)[0]):
            if "sparse" not in blocks:
                outside.append(f"{number}: {stripped}")
    assert not blocks
    assert not outside, "sparse members outside `#if !OPENTONY_GUEST_MEMORY_FLAT`:\n" + "\n".join(outside)


def test_the_dll_check_sees_what_it_forbids():
    from tony.hooks import memory_backend_problems

    flat = [
        "10003260 T opentony::runtime::flat_memory::RetailMemory::alias_live(unsigned int, unsigned int, unsigned char*)",
        "10048b30 T opentony::kitdll::Kits::Kits(opentony::runtime::flat_memory::RetailMemory&, opentony::runtime::RetailServices&)",
    ]
    assert memory_backend_problems(flat) == []
    assert memory_backend_problems(["10001000 T main"])   # nothing to tell by is a failure, not a pass
    # the symbols of a DLL built before the split, and of one built with the fixture flavour
    assert memory_backend_problems(["10003260 T opentony::runtime::RetailMemory::alias_live(unsigned int, unsigned int, unsigned char*)"])
    assert memory_backend_problems(flat + ["10049340 T opentony::runtime::fixture_memory::RetailMemory::add_region(unsigned int, unsigned int)"])
    assert memory_backend_problems(flat + ["1004a000 T f(opentony::runtime::flat_memory::RetailMemory&, opentony::runtime::RetailMemory const&)"])
