"""Milestone 11b (docs/PORT64_AUDIT.md 5.4 item 1): host code reaches game memory only through `arena::`.

A clean x86-64 compile proves nothing about pointers: `static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p))` truncates without a warning, and
`reinterpret_cast<T*>(static_cast<std::uintptr_t>(word))` reaches the wrong byte once guest address A is no longer host address A. So no standalone source
may spell a guest/host conversion by hand: a guest word becomes a host pointer through `arena::host/at/text/chars`, a host pointer becomes a guest word
through `arena::guest` (or is a `HeapBlock`'s address).
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KITS = ROOT / "src/hooks/kits"

# movie_compare.cpp and movie_player.cpp stay 32-bit tools; guest_arena.* is where the conversions live
SOURCES = sorted(
    path
    for path in [*KITS.glob("standalone_*.cpp"), KITS / "kit_dll.cpp"]
    if path.exists()
)

FORBIDDEN = {
    "a guest word cast to a host pointer by hand (use arena::host/at/text/chars)": re.compile(
        r"reinterpret_cast<[^<>]*\*>\(\s*static_cast<std::uintptr_t>\("
    ),
    "a guest constant cast to a host pointer by hand (use arena::host/at/text/chars)": re.compile(
        r"reinterpret_cast<[^<>]*\*>\(\s*(?:0x[0-9a-fA-F]+|k[A-Z]\w*)\s*[+)]"
    ),
    "a host pointer cut to a guest word (use arena::guest or a HeapBlock)": re.compile(
        r"static_cast<std::uint32_t>\(\s*reinterpret_cast<std::uintptr_t>\("
    ),
    "a host pointer cut to a 32-bit integer (use arena::guest)": re.compile(r"\(\s*std::uint32_t\s*\)\s*(?:&|reinterpret_cast)"),
}
# a line that really is a host address (a VirtualQuery walk), said so
ALLOWED_MARKER = "host address, not a guest word"


def test_the_scan_finds_the_standalone_sources() -> None:
    names = {path.name for path in SOURCES}
    assert {"kit_dll.cpp", "standalone_host.cpp", "standalone_spike.cpp", "standalone_boot.cpp", "standalone_seams.cpp"} <= names


def test_no_hand_written_guest_host_conversions() -> None:
    found: list[str] = []
    for path in SOURCES:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for number, line in enumerate(lines, 1):
            if ALLOWED_MARKER in line or (number > 1 and ALLOWED_MARKER in lines[number - 2]):
                continue
            for why, pattern in FORBIDDEN.items():
                if pattern.search(line):
                    found.append(f"{path.relative_to(ROOT)}:{number}: {why}: {line.strip()[:120]}")
    assert not found, "\n" + "\n".join(found)


def test_the_host_printf_is_not_handed_guest_words() -> None:
    """The printf family runs through the guest formatter (src/runtime/guest_format.hpp), never a host call with guest argument words."""
    text = (KITS / "kit_bridge.hpp").read_text(encoding="utf-8", errors="replace")
    assert "call_host_cdecl" not in text
    for path in SOURCES:
        assert "call_host_cdecl" not in path.read_text(encoding="utf-8", errors="replace"), path.name
