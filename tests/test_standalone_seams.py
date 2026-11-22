"""Milestone 8c (Phase 5.3c): every registered seam has an answer in the standalone, upstream zlib stands in for the retail zlib, and the mod overlay is in front of the files."""

from __future__ import annotations

import json
import os
import re
import struct
import subprocess
from pathlib import Path

import pytest

from tony import static_data, zlib_source
from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
KITS_DIR = ROOT / "src/hooks/kits"
HEADER = ROOT / "src/runtime/seam_addresses_generated.hpp"
NATIVES = KITS_DIR / "standalone_kits_generated.cpp"
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
KITS = ROOT / "build/hooks/kits/opentony_standalone.dll"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
DUMP = ROOT / "build/sessions/runs/spike-boot-career-hangar/session_5000.img"
DATA = ROOT / "build/assets/all-pkr/files"
FEATURES = ROOT / "build/xref/features-canonical.json"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


def _seams() -> list[tuple[str, int]]:
    return [(found[0], int(found[1], 16)) for found in re.findall(r'\{"(\w+\.\w+)", "[\w-]+", 0x([0-9a-f]+)\}', HEADER.read_text())]


def _natives() -> set[int]:
    source = NATIVES.read_text()
    table = source[source.index("const NativeEntry kNatives[]"):]
    return {int(found, 16) for found in re.findall(r"^    \{0x([0-9a-f]{8})u, ", table, re.MULTILINE)}


def _constants() -> dict[str, int]:
    """`seam::<category>::<name>` -> address, from the generated header."""

    names: dict[str, int] = {}
    category = ""
    for line in HEADER.read_text().split("\n"):
        namespace = re.match(r"namespace (\w+) \{", line)
        if namespace:
            category = namespace.group(1)
        constant = re.match(r"inline constexpr std::uint32_t (\w+) = 0x([0-9a-f]+);", line)
        if constant and category:
            names[f"{category}::{constant.group(1)}"] = int(constant.group(2), 16)
    return names


def _standalone_sources() -> str:
    """The code of the standalone's seam files without comments."""

    text = ""
    for path in sorted(KITS_DIR.glob("standalone_*.cpp")):
        if path.name in {"standalone_kits_generated.cpp", "standalone_spike.cpp", "standalone_video.cpp"}:
            continue
        text += "\n".join(line.split("//")[0] for line in path.read_text().split("\n")) + "\n"
    return text


def _implemented_addresses() -> set[int]:
    text = _standalone_sources()
    constants = _constants()
    found = {int(value, 16) for value in re.findall(r"\b0x([0-9a-f]{8})\b", text)}
    for category, name in re.findall(r"\bseam::(\w+)::(\w+)", text):
        found.add(constants[f"{category}::{name}"])
    return found


def test_every_registered_seam_is_a_native_or_is_named_by_an_implementation():
    seams = _seams()
    assert len(seams) == len(load_yaml("re/symbols/seams.yml")["seams"])
    answered = _natives() | _implemented_addresses()
    missing = [f"{name} ({address:#010x})" for name, address in seams if address not in answered]
    assert not missing, "seams with no native and no implementation in src/hooks/kits/standalone_*.cpp: " + ", ".join(missing)


def test_the_draw_handlers_are_natives_of_the_draw_kit():
    natives = {int(row["address"]): row for row in load_yaml("re/config/standalone_natives_manual.yml")["natives"]}
    handlers = {name: address for name, address in _seams() if name.startswith("hal.D3D_Draw")}
    assert len(handlers) == 14
    for name, address in handlers.items():
        assert address in natives and natives[address]["kit"] == "draw", name
    for name in ("hal.D3D_SetViewportRect", "hal.D3D_ClearBuffers"):
        assert dict(_seams())[name] in natives, name


def test_the_table_seams_are_all_in_the_host_table():
    rows = load_yaml("re/config/table_seams.yml")["table_seams"]
    host = (KITS_DIR / "standalone_host.cpp").read_text()
    seams = {address for _, address in _seams()}
    assert len({row["address"] for row in rows}) == len(rows)
    for row in rows:
        assert row["disposition"] in {"answered", "replaced"}, row
        assert row["note"], row
        assert f"{row['address']:#010x}" in host, f"{row['address']:#010x} is not in kHostSeams"
        assert row["address"] not in seams and row["address"] not in _natives(), f"{row['address']:#010x} is a seam or a native already"


@pytest.mark.skipif(not EXE.is_file() or not FEATURES.is_file(), reason="needs the extracted retail executable and `tony xref`")
def test_the_table_seams_are_what_the_executable_holds():
    from tony.native_progress import load_native_progress
    from tony.xref import coverage

    document = json.loads(FEATURES.read_text())
    functions = document["functions"] if isinstance(document, dict) and "functions" in document else document
    scope = coverage.scope_map(coverage._library_functions(functions), native=set(load_native_progress()))
    image = EXE.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    sections = struct.unpack_from("<H", image, pe + 6)[0]
    optional = struct.unpack_from("<H", image, pe + 20)[0]
    held: set[int] = set()
    excluded = {address for _, address in _seams()} | _natives()
    for index in range(sections):
        offset = pe + 24 + optional + 40 * index
        name = image[offset:offset + 8].rstrip(b"\0").decode()
        virtual_size, _, raw_size, raw_pointer = struct.unpack_from("<IIII", image, offset + 8)
        if name not in {".data", ".rdata"}:
            continue
        for at in range(0, min(virtual_size, raw_size) - 3, 4):
            value = struct.unpack_from("<I", image, raw_pointer + at)[0]
            if value in scope and value not in excluded:
                held.add(value)
    rows = {row["address"] for row in load_yaml("re/config/table_seams.yml")["table_seams"]}
    assert held == rows, f"table-held out-of-scope functions not in table_seams.yml: {sorted(map(hex, held - rows))}; stale rows: {sorted(map(hex, rows - held))}"


def test_zlib_is_the_pinned_upstream_release():
    item = zlib_source.spec()
    assert item["release"] == "1.3.1"
    assert item["url"].startswith("https://github.com/madler/zlib/releases/download/v1.3.1/")
    assert re.fullmatch(r"[0-9a-f]{64}", item["sha256"])
    assert {"deflate.c", "inflate.c", "compress.c", "uncompr.c"} <= set(item["sources"])
    notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text()
    assert "zlib" in notices.lower() and "1.3.1" in notices


def test_pkr_asset_is_in_the_kit_library_and_the_retail_zlib_range_is_not_linked():
    cmake = (KITS_DIR / "CMakeLists.txt").read_text()
    assert "EXCLUDE REGEX \"src/assets/pkr_asset" not in cmake
    assert "opentony_zlib" in cmake and "OPENTONY_ZLIB_SOURCE" in cmake


def test_the_overlay_is_in_front_of_the_file_lookup():
    files = (KITS_DIR / "standalone_files.cpp").read_text()
    assert files.index("ot_overlay_lookup") < files.index('std::fopen(full.c_str(), "rb")')
    assert "mod_overlay.cpp" in (KITS_DIR / "CMakeLists.txt").read_text()


needs_wine = pytest.mark.skipif(
    not WINE or not SPIKE.is_file() or not KITS.is_file() or not EXE.is_file() or not DUMP.is_file() or not DATA.is_dir(),
    reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` and `scripts/standalone_spike.py run` done (runs under Wine)",
)


def _spike(calls: str, **environment: str) -> subprocess.CompletedProcess:
    runs = DUMP.parent
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env.update({"WINEDEBUG": "-all", "OT_SPIKE_CALL": calls, "OT_SPIKE_FRAMES": "30", "OPENTONY_DATA_DIR": str(DATA), **environment})
    return subprocess.run(
        ["wine", str(SPIKE), str(static_data.ensure_image(EXE)), str(KITS), str(DUMP), str(ROOT / "build/standalone-spike/expected.tsv"), str(runs / "session_5000.heap")],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=False,
    )


@needs_wine
def test_the_program_answers_every_registered_seam():
    result = _spike("0x004a8ee0", OT_SEAMS_REPORT="1")
    output = result.stdout + result.stderr
    found = re.search(r"seams: (\d+) of (\d+) answered", output)
    assert found, output[-800:]
    assert found.group(1) == found.group(2) == str(len(_seams())), output[-800:]
    assert "unanswered:" not in output


@needs_wine
def test_the_host_seams_do_what_their_retail_functions_do():
    settings = ROOT / "build/standalone-spike/hostcheck.ini"
    settings.unlink(missing_ok=True)
    result = _spike("hostcheck", OPENTONY_SETTINGS="build/standalone-spike/hostcheck.ini")
    lines = [line for line in result.stdout.split("\n") if line.startswith("hostcheck: ")]
    failed = [line for line in lines if " FAIL" in line]
    assert not failed, failed
    names = {line.split()[1] for line in lines}
    assert {"zlib-compress", "zlib-uncompress", "zlib-buffer-error", "zlib-data-error", "isspace", "stat", "stat-missing", "package-stream", "find-files",
            "find-files-none", "settings", "directplay", "display", "platform-answers"} <= names
    assert "hostcheck: 0 failures" in result.stdout


PACKAGE = Path(os.environ.get("OPENTONY_TEST_PKR", str(ROOT / "game/installed/Setup/data/ALL.PKR")))


@needs_wine
@pytest.mark.skipif(not PACKAGE.is_file(), reason="needs the player's ALL.PKR (OPENTONY_TEST_PKR)")
def test_pcinit_loads_the_package_tables_through_the_stream_seams():
    # PCinit builds "<game dir>\all.pkr" and PKR2_Preload reads the header and the two tables through PKR2_OpenPackageStream / PKR2_ReadResource / ResourceStream_Close
    # (the host streams of standalone_host.cpp); OPENTONY_PKR points the stream at the real package, which is not in the working directory
    header = PACKAGE.open("rb").read(0x10 + 0x28 + 0x30)
    assert header[:4] == b"PKR2"
    entries, files = struct.unpack_from("<II", header, 8)
    assert entries > 0 and files > 0
    first_entry_word = struct.unpack_from("<I", header, 0x10)[0]
    # (the session image carries the name of the package the retail process had loaded, and PKR2_Preload would unload it, freeing the retail heap's tables with the host's
    # free: the name is blanked first, which is the state of a program that has loaded no package)
    result = _spike("poke:0x006a7578=0,0x004e73f0,peekp:0x006a7678,peekstr:0x006a7578", OPENTONY_PKR="Z:" + str(PACKAGE), OT_FILES_LOG="1")
    output = result.stdout + result.stderr
    assert "FATAL" not in output, output[-800:]
    assert re.search(r"files: package Z:.*ALL\.PKR -> ok", output), output[-800:]   # (the path has spaces)
    # kPackageEntries (0x006a7678) points at a table whose first word is the first word of the package's first 0x28-byte entry, read from the file
    table = re.search(r"peekp 006a7678 -> ([0-9a-f]{8}) = ([0-9a-f]{8})", output)
    assert table and int(table.group(1), 16) != 0, output[-800:]
    assert int(table.group(2), 16) == first_entry_word, output[-800:]
    assert re.search(r"peekstr 006a7578 = \S*all\.pkr", output), output[-800:]


@needs_wine
def test_a_mod_file_answers_the_game_open_before_the_data_does(tmp_path):
    # the overlay's keys and folders are the hooked build's (docs/HOOKS.md, "Mods"): <mod>\Data\options.pre replaces data\options.pre
    mods = tmp_path / "Mods" / "swap" / "Data"
    mods.mkdir(parents=True)
    (mods.parent / "mod.ini").write_text("[Mod]\nName = swap\n")
    original = next(path for path in (DATA / "data").iterdir() if path.name.lower() == "options.pre")   # upper case in the extracted package; Wine does not care
    (mods / "options.pre").write_bytes(original.read_bytes())
    result = _spike("0x004a8ee0,0x00483ef0", OPENTONY_MODS="Z:" + str(tmp_path / "Mods"), OPENTONY_MOD_TRACE="1", OT_FILES_LOG="1")   # Wine: the Unix root is drive Z:
    output = result.stdout + result.stderr
    assert "mods: loaded 'swap'" in output, output[-1200:]
    assert re.search(r"files: open \S*swap\S*options\.pre -> ok", output), output[-1200:]
    assert "frame limit: 30 frames of Game_FrameTick" in result.stdout
