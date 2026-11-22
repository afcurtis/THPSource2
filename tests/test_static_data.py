"""Phase 5, milestone 8b: the static data image made from the player's THawk2.exe (tony/static_data.py)."""

from __future__ import annotations

import os
import struct
import subprocess
from pathlib import Path

import pytest

from tony import static_data
from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
SPIKE = ROOT / "build/hooks/kits/opentony_spike.exe"
KITS = ROOT / "build/hooks/kits/opentony_standalone.dll"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"

needs_exe = pytest.mark.skipif(not EXE.is_file(), reason="needs the extracted retail executable")


def _sections(blob: bytes) -> list[tuple[int, bytes]]:
    version, base, stamp, size_of_image, count = struct.unpack_from("<5I", blob, 4)
    assert blob[:4] == b"OTSD" and version == 1 and base == 0x00400000
    build = load_yaml("re/config/native_hooks.yml")["builds"]["canonical"]
    assert (stamp, size_of_image) == (int(build["pe_timestamp"]), int(build["size_of_image"]))
    table = [struct.unpack_from("<2I", blob, 24 + 8 * i) for i in range(count)]
    at = 24 + 8 * count
    out = []
    for va, size in table:
        out.append((va, blob[at:at + size]))
        at += size
    assert at == len(blob)
    return out


@needs_exe
def test_the_image_holds_the_data_sections_at_their_retail_addresses_and_no_code():
    import pefile

    blob = static_data.build_image(EXE)
    pe = pefile.PE(str(EXE))
    data = EXE.read_bytes()
    wanted = {0x00400000 + s.VirtualAddress: s for s in pe.sections if not s.Characteristics & 0x20}
    got = _sections(blob)
    assert {va for va, _ in got} == set(wanted)
    for va, bytes_ in got:
        section = wanted[va]
        n = min(section.SizeOfRawData, section.Misc_VirtualSize)
        assert bytes_ == data[section.PointerToRawData:section.PointerToRawData + n]
    assert sum(len(b) for _, b in got) == 297196  # what 5.4a's spike copied from the file


@needs_exe
def test_another_build_is_rejected(tmp_path):
    other = tmp_path / "THawk2.exe"
    data = bytearray(EXE.read_bytes())
    data[-1] ^= 0xFF  # same headers, different bytes: only the checksum of the whole file notices
    other.write_bytes(bytes(data))
    with pytest.raises(SystemExit, match="not the retail build"):
        static_data.build_image(other)
    junk = tmp_path / "junk.exe"
    junk.write_bytes(b"MZ" + b"\0" * 100)
    with pytest.raises(SystemExit, match="rejected"):
        static_data.build_image(junk)
    with pytest.raises(SystemExit, match="not found"):
        static_data.build_image(tmp_path / "missing.exe")


VARIANT_EXE = ROOT / "game/variants/original-2000/THawk2.exe"
PROTECTED_EXE = ROOT / "build/variants/original-2000-disc/files/Setup/data/THawk2.exe"  # the original disc's own (extracted by hand; optional)
needs_both = pytest.mark.skipif(not (EXE.is_file() and VARIANT_EXE.is_file()), reason="needs both the Value re-release's and the original 2000 release's THawk2.exe")


@needs_both
def test_the_original_2000_release_gives_the_canonical_image_with_only_the_cd_key_zeroed():
    from tony import static_variant

    canonical = static_data.build_image(EXE)
    rebuilt = static_data.build_image(VARIANT_EXE)
    assert len(rebuilt) == len(canonical) and rebuilt[:24] == canonical[:24]  # same header: the natives' build, timestamp and size of image
    recipe = static_variant.load_recipe()
    masked = [(int(m["address"]), int(m["size"])) for m in recipe["masked"]]
    assert masked == [(0x00525500, 400)]  # Front_KeyTable's CD-check key, and nothing else
    wanted = bytearray(canonical)
    at = 24 + 8 * len(_sections(canonical))  # where the first section's bytes start
    for va, data in _sections(canonical):
        for address, size in masked:
            if va <= address < va + len(data):
                wanted[at + address - va:at + address - va + size] = bytes(size)
        at += len(data)
    assert any(canonical[k] != wanted[k] for k in range(len(wanted)))  # the key is not zero in the Value re-release
    assert rebuilt == bytes(wanted)


@needs_both
def test_the_committed_recipe_is_what_the_two_executables_give():
    from tony import static_variant

    recipe = static_variant.derive_from_config()
    assert static_variant.recipe_path().read_text() == static_variant.render_recipe(recipe), "run `tony xref static-recipe`"
    assert (ROOT / static_variant.INCLUDE).read_text() == static_variant.render_include(recipe), "run `tony xref static-recipe`"


def test_the_recipe_carries_addresses_and_only_a_few_bytes_of_its_own():
    """Every table and string the game reads comes from the player's executable: the literal bytes are the linker's import tables and a few hundred others."""
    from tony import static_variant

    recipe = static_variant.load_recipe()
    spec = static_variant.variant_spec()
    assert recipe["variant_sha256"] == spec["sha256"]
    assert recipe["canonical_sha256"] == load_yaml("re/config/native_hooks.yml")["builds"]["canonical"]["sha256"]
    kinds = static_variant.classify(recipe)
    assert kinds["content"] <= 600 and kinds["imports"] <= 2600, kinds
    assert sum(len(text) // 2 for _, text in recipe["bytes"]) == kinds["content"] + kinds["imports"]
    for address, size in ((int(m["address"]), int(m["size"])) for m in recipe["masked"]):
        for c, text in recipe["bytes"]:
            assert c + len(text) // 2 <= address or c >= address + size, "a masked range is never written"
    # the launcher's copy is the same recipe
    assert (ROOT / static_variant.INCLUDE).read_text() == static_variant.render_include(recipe), "run `tony xref static-recipe`"


def test_the_launchers_pin_the_same_original_2000_builds():
    import re as regex

    spec = load_yaml("re/xref/config.yml")["variants"]["original_2000"]
    core = (ROOT / "src/launcher/thpsource2/core/game_files.hpp").read_text()
    launcher = (ROOT / "src/launcher/opentony_launcher.cpp").read_text()
    include = (ROOT / "src/launcher/original_2000_static.inc").read_text()
    assert regex.search(r'kOriginal2000Sha256 = "([0-9a-f]{64})"', core).group(1) == spec["sha256"]
    assert regex.search(r'kVariantSha256 = "([0-9a-f]{64})"', include).group(1) == spec["sha256"]
    protected = spec["protected_disc_executable"]["sha256"]
    assert regex.search(r'kProtectedDiscSha256 = "([0-9a-f]{64})"', core).group(1) == protected
    assert regex.search(r'kProtectedDiscSha256 = "([0-9a-f]{64})"', launcher).group(1) == protected
    assert load_yaml("re/config/native_hooks.yml")["builds"]["original_2000"]["sha256"] == spec["sha256"]


@pytest.mark.skipif(not PROTECTED_EXE.is_file(), reason="needs the original 2000 disc's THawk2.exe")
def test_the_original_discs_protected_executable_is_refused_with_the_reason():
    with pytest.raises(SystemExit, match="copy-protected"):
        static_data.build_image(PROTECTED_EXE)


@needs_both
def test_a_changed_original_2000_executable_is_rejected(tmp_path):
    other = tmp_path / "THawk2.exe"
    data = bytearray(VARIANT_EXE.read_bytes())
    data[-1] ^= 0xFF
    other.write_bytes(bytes(data))
    with pytest.raises(SystemExit, match="rejected"):
        static_data.build_image(other)


def test_the_image_is_a_git_ignored_build_output():
    assert static_data.IMAGE.startswith("build/")
    assert "/build/" in (ROOT / ".gitignore").read_text().splitlines()


def _wine(*arguments: str) -> subprocess.CompletedProcess:
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env["WINEDEBUG"] = "-all"
    return subprocess.run(["wine", str(SPIKE), *arguments], cwd=ROOT, env=env, capture_output=True, text=True, timeout=300, check=False)


@pytest.mark.skipif(not WINE or not SPIKE.is_file() or not KITS.is_file() or not EXE.is_file(), reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` done")
def test_the_spike_starts_from_the_image_with_the_same_sections_as_from_the_executable(tmp_path):
    image = static_data.ensure_image(EXE)
    empty = tmp_path / "empty.tsv"
    empty.write_text("")
    from_image = _wine(str(image), str(KITS), "-", str(empty))
    from_exe = _wine(str(EXE), str(KITS), "-", str(empty))
    assert from_image.returncode == from_exe.returncode == 1, from_image.stdout + from_image.stderr  # (1: no getter was asked for, nothing checked)
    line = lambda r: next(x for x in r.stdout.splitlines() if x.startswith("regions:"))
    assert "static-data image" in line(from_image)
    assert line(from_image).split(" from ")[0] == line(from_exe).split(" from ")[0]  # same byte count
    assert line(from_image).split("; ")[1] == line(from_exe).split("; ")[1]  # same first words of .text (zero)


@pytest.mark.skipif(not WINE or not SPIKE.is_file() or not KITS.is_file() or not EXE.is_file(), reason="set OPENTONY_WINE_TESTS=1 with `tony hooks build` done")
def test_the_spike_rejects_an_image_of_another_build(tmp_path):
    blob = bytearray(static_data.ensure_image(EXE).read_bytes())
    struct.pack_into("<I", blob, 12, 0x39ea002F)  # the 2000 retail build's timestamp
    bad = tmp_path / "bad.img"
    bad.write_bytes(bytes(blob))
    empty = tmp_path / "empty.tsv"
    empty.write_text("")
    result = _wine(str(bad), str(KITS), "-", str(empty))
    assert result.returncode == 2 and "not the retail build" in result.stderr
