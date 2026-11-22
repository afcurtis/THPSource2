"""Native replacement hook harness (`tony hooks ...`).

`re/config/native_hooks.yml` lists retail functions that opentony.dll
(src/hooks/) redirects to native implementations. This module validates that
manifest against the executables and the knowledge base, generates the C hook
table, cross-builds the 32-bit DLLs with clang-cl against the pinned VC6 SDK,
stages them beside the no-CD executable, runs the in-process self-test, and
provides the `--hooks` environment used by scenario capture/replay.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import functools
import json
import os
import re
import shutil
import struct
import subprocess
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

from . import kit_hooks, static_data
from .common import ROOT, load_yaml, resolve, sha256, which

MANIFEST_PATH = "re/config/native_hooks.yml"
HEADER_PATH = "src/hooks/native_hooks_generated.h"
NATIVE_SOURCES = "src/hooks/native"
SDK_DIR = "build/toolchain/vc6-sdk"
VC6_DIR = ".tools/vc6/VC98"
TOOLCHAIN_FILE = "cmake/toolchains/win32-clang-cl.cmake"
KITS_TOOLCHAIN_FILE = "cmake/toolchains/win32-llvm-mingw.cmake"
HOOKS_BUILD_DIR = "build/hooks/win32"
KITS_BUILD_DIR = "build/hooks/kits"
KITS64_TOOLCHAIN_FILE = "cmake/toolchains/win64-llvm-mingw.cmake"   # 11b: the 64-bit standalone (the Kits' CMake builds only the standalone then)
KITS64_BUILD_DIR = "build/hooks/kits64"
CAPTURE_BUILD_DIR = "build/capture/win32"
SELFTEST_ROOT = "build/hooks/selftest"
RUNTIME_DIR = "build/hooks/runtime"
STAGED_FILES = ("dinput.dll", "opentony.dll", "opentony_native.dll", "SDL3.dll")
RUNTIME_OUTPUTS = ("opentony.log", "opentony.stats", "opentony.selftest")
DEFAULT_INI = "src/hooks/opentony.ini"
SELFTEST_IMPORTS = ("binkw32.dll",)

ABIS = {
    "thiscall_vec3_vec3_ref": "OT_ABI_THISCALL_VEC3_VEC3_REF",
    "thiscall_vec3_int_ref": "OT_ABI_THISCALL_VEC3_INT_REF",
    "thiscall_vec3_compare": "OT_ABI_THISCALL_VEC3_COMPARE",
    "thiscall_vec3_divisor_ref": "OT_ABI_THISCALL_VEC3_DIVISOR_REF",
    "thiscall_vec3_bytes_ref": "OT_ABI_THISCALL_VEC3_BYTES_REF",
    "fastcall_vec3s": "OT_ABI_FASTCALL_VEC3S",
    "thiscall_vec3s_vec3s_ref": "OT_ABI_THISCALL_VEC3S_VEC3S_REF",
    "thiscall_vec3s_bytes_ref": "OT_ABI_THISCALL_VEC3S_BYTES_REF",
    "thiscall_vec3s_compare": "OT_ABI_THISCALL_VEC3S_COMPARE",
    "cdecl_vec3s_out": "OT_ABI_CDECL_VEC3S_OUT",
    "cdecl_overlay_open": "OT_ABI_CDECL_OVERLAY_OPEN",
}
KIT_ABI = "OT_ABI_KIT"
HOOKABLE_STATUSES = {
    "tested": "OT_NATIVE_TESTED",
    "trace-validated": "OT_NATIVE_TRACE_VALIDATED",
    "integrated": "OT_NATIVE_INTEGRATED",
}
DEFAULT_ON_STATUSES = {"trace-validated", "integrated"}


def default_on(hook: dict[str, Any], status: str) -> bool:
    """Native by default: a validated native status, unless the row says `default: off` (Phase 5.1: no
    parity scenario has reached the hook yet, so it is installed but stays retail until one does)."""

    return status in DEFAULT_ON_STATUSES and hook.get("default") != "off"
CLOCK_SITE_KINDS = {"pump": "OT_CLOCK_SITE_PUMP", "entry": "OT_CLOCK_SITE_ENTRY", "spin": "OT_CLOCK_SITE_SPIN"}
HAL_SITE_ROLES = {
    "startup": "OT_HAL_ROLE_STARTUP",
    "shutdown": "OT_HAL_ROLE_SHUTDOWN",
    "window": "OT_HAL_ROLE_WINDOW",
    "video": "OT_HAL_ROLE_VIDEO",
    "draw_list": "OT_HAL_ROLE_DRAW_LIST",
    "texture": "OT_HAL_ROLE_TEXTURE",
}
HAL_SITE_KINDS = {"before": "OT_HAL_SITE_BEFORE", "after": "OT_HAL_SITE_AFTER", "before_arg": "OT_HAL_SITE_BEFORE_ARG"}
# src/hal/hal.h OtHalService order.
HAL_SERVICES = ("clock", "window", "input", "video", "audio", "movie")
HAL_ADAPTER_SOURCES = "src/hooks/hal"
HAL_BOUNDARY_PATH = "re/config/hal_boundary.yml"
HAL_SYMBOL_KINDS = ("function", "data")
MAX_HAL_SITES = 8
MAX_OVERWRITE = 16
MAX_BUILDS = 4
MAX_CLOCK_SITES = 8
_BRANCH = re.compile(r"^(j[a-z]+|call[lw]?|loop[a-z]*|jecxz|jcxz)$")


class HookError(RuntimeError):
    pass


# --------------------------------------------------------------------- inputs


def load_manifest(path: str | Path = MANIFEST_PATH) -> dict[str, Any]:
    return load_yaml(path)


def _expected_bytes(hook: dict[str, Any]) -> bytes:
    return bytes.fromhex(str(hook.get("expected", "")).replace(" ", ""))


def _native_statuses() -> dict[int, dict[str, Any]]:
    return {
        int(item["address"]): item
        for item in load_yaml("re/native/functions.yml").get("functions", [])
    }


def _symbols() -> dict[int, str]:
    return {
        int(item["address"]): item["name"]
        for item in load_yaml("re/symbols/functions.yml").get("functions", [])
    }


@functools.cache
def _canonical_exe() -> Path:
    return resolve(load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["path"])


@functools.cache
def _variant_exe(build: str) -> Path | None:
    variant = load_yaml("re/xref/config.yml").get("variants", {}).get(build)
    return resolve(variant["path"]) if variant and variant.get("path") else None


def build_executable(build: str) -> Path | None:
    return _canonical_exe() if build == "canonical" else _variant_exe(build)


class PeImage:
    """Minimal PE32 reader: header identity and virtual-address reads."""

    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise HookError(f"{path}: not a PE image")
        sections = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
        optional = pe + 24
        self.image_base = struct.unpack_from("<I", self.data, optional + 28)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, optional + 56)[0]
        self.sections = []
        for index in range(sections):
            header = optional + optional_size + 40 * index
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, header + 8)
            self.sections.append((rva, max(virtual_size, raw_size), raw_offset, raw_size))

    def read(self, address: int, size: int) -> bytes:
        rva = address - self.image_base
        for section_rva, span, raw_offset, raw_size in self.sections:
            if section_rva <= rva and rva + size <= section_rva + min(span, raw_size):
                offset = raw_offset + rva - section_rva
                return self.data[offset:offset + size]
        raise HookError(f"{self.path}: 0x{address:08x}+{size} is not file-backed")


def _variant_map(path: str) -> dict[int, tuple[int, bool]]:
    """canonical address -> (variant address, identical normalized code)."""

    mapping: dict[int, tuple[int, bool]] = {}
    for row in load_yaml(path).get("functions", []):
        variant, canonical, _method, identical = row[0], row[1], row[2], row[3]
        mapping[int(canonical)] = (int(variant), bool(identical))
    return mapping


@functools.lru_cache(maxsize=1)
def _module_ends() -> dict[int, int]:
    return {
        int(module["start_va"]): int(module["end_va"])
        for module in load_yaml("match/manifest.yml").get("modules", [])
    }


def _module_end(address: int) -> int:
    return _module_ends().get(address, address + 0x100)


@functools.cache
def _objdump() -> str | None:
    for name in ("llvm-objdump", "llvm-objdump-21", "llvm-objdump-20", "llvm-objdump-19", "objdump"):
        found = which(name)
        if found:
            return found
    return None


def _disassemble(exe: Path, start: int, stop: int) -> list[tuple[int, str, str]]:
    tool = _objdump()
    if tool is None:
        raise HookError("llvm-objdump not found")
    result = subprocess.run(
        [tool, "-d", "--no-show-raw-insn", f"--start-address=0x{start:x}", f"--stop-address=0x{stop:x}", str(exe)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    rows = []
    for line in result.stdout.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if match:
            rows.append((int(match.group(1), 16), match.group(2), match.group(3)))
    if result.returncode != 0 or not rows:
        raise HookError(f"could not disassemble 0x{start:08x}..0x{stop:08x}: {result.stdout.strip()[:200]}")
    return rows


def _check_boundaries(exe: Path, name: str, address: int, overwrite: int) -> list[str]:
    errors = []
    rows = _disassemble(exe, address, _module_end(address))
    starts = [row[0] for row in rows]
    if starts[0] != address:
        errors.append(f"{name}: disassembly does not start at 0x{address:08x}")
    if address + overwrite not in starts:
        errors.append(f"{name}: overwrite {overwrite} does not end on an instruction boundary")
    for insn_address, mnemonic, operands in rows:
        if not _BRANCH.match(mnemonic):
            continue
        if insn_address < address + overwrite:
            errors.append(f"{name}: relative {mnemonic} at 0x{insn_address:08x} inside the patched prologue")
        target = re.match(r"0x([0-9a-f]+)", operands)
        if target and address < int(target.group(1), 16) < address + overwrite:
            errors.append(f"{name}: {mnemonic} at 0x{insn_address:08x} branches into the patched prologue")
    return errors


# ------------------------------------------------------------------ validation


def validate(
    manifest: dict[str, Any] | None = None,
    *,
    check_executables: bool = True,
    check_disassembly: bool = True,
    check_header: bool = True,
) -> tuple[list[str], list[str]]:
    """Return (errors, notes) for the hook manifest."""

    manifest = manifest if manifest is not None else load_manifest()
    errors: list[str] = []
    notes: list[str] = []
    if manifest.get("version") != 1:
        errors.append(f"{MANIFEST_PATH}: version must be 1")
    builds = manifest.get("builds", {})
    if list(builds)[:1] != ["canonical"]:
        errors.append(f"{MANIFEST_PATH}: builds must start with 'canonical'")
    if len(builds) > MAX_BUILDS:
        errors.append(f"{MANIFEST_PATH}: at most {MAX_BUILDS} builds")
    canonical_sha = load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["sha256"]
    if builds.get("canonical", {}).get("sha256") != canonical_sha:
        errors.append(f"{MANIFEST_PATH}: canonical sha256 differs from executables.thps2_pc")

    images: dict[str, PeImage] = {}
    if check_executables:
        for build, spec in builds.items():
            exe = build_executable(build)
            if exe is None or not exe.is_file():
                notes.append(f"{build}: executable not present; byte checks skipped")
                continue
            if sha256(exe) != spec.get("sha256"):
                errors.append(f"{build}: {exe} does not have the recorded sha256")
                continue
            image = PeImage(exe)
            if image.timestamp != spec.get("pe_timestamp") or image.size_of_image != spec.get("size_of_image"):
                errors.append(f"{build}: PE timestamp/SizeOfImage differ from the manifest identity")
            images[build] = image
    variant_maps = {
        build: _variant_map(spec["address_map"])
        for build, spec in builds.items()
        if build != "canonical" and spec.get("address_map")
    }

    symbols = _symbols()
    natives = _native_statuses()
    registered = _registered_natives()
    seen: set[str] = set()
    for hook in manifest.get("hooks", []):
        name = hook.get("name", "?")
        if name in seen:
            errors.append(f"{name}: duplicate hook")
        seen.add(name)
        overwrite = hook.get("overwrite")
        expected = _expected_bytes(hook)
        if not isinstance(overwrite, int) or not 5 <= overwrite <= MAX_OVERWRITE:
            errors.append(f"{name}: overwrite must be 5..{MAX_OVERWRITE}")
            continue
        if len(expected) != overwrite:
            errors.append(f"{name}: expected has {len(expected)} bytes, overwrite is {overwrite}")
        kit = kit_hooks.is_kit_hook(hook)
        if not kit and hook.get("abi") not in ABIS:
            errors.append(f"{name}: unknown abi {hook.get('abi')!r}")
        address = hook.get("builds", {}).get("canonical")
        if not isinstance(address, int):
            errors.append(f"{name}: missing canonical address")
            continue
        if symbols.get(address) != name:
            errors.append(f"{name}: re/symbols/functions.yml names 0x{address:08x} {symbols.get(address)!r}")
        progress = natives.get(address)
        if progress is None:
            errors.append(f"{name}: no re/native/functions.yml entry for 0x{address:08x}")
        elif progress.get("status") not in HOOKABLE_STATUSES:
            errors.append(f"{name}: native status {progress.get('status')!r} is not hookable (needs tested+)")
        if not kit and registered.get(("native", name)) != hook.get("native"):
            errors.append(f"{name}: no OT_REGISTER_NATIVE({name}, {hook.get('native')}) under {NATIVE_SOURCES}")
        if hook.get("fault") and ("fault", name) not in registered:
            errors.append(f"{name}: fault: true but no OT_REGISTER_FAULT({name}, ...)")
        for build, build_address in hook.get("builds", {}).items():
            if build not in builds:
                errors.append(f"{name}: unknown build {build!r}")
                continue
            if build in variant_maps:
                mapped = variant_maps[build].get(address)
                if mapped is None or mapped[0] != build_address:
                    errors.append(f"{name}: {build} address 0x{build_address:08x} disagrees with {builds[build]['address_map']}")
                elif not mapped[1]:
                    errors.append(f"{name}: {build} function is not byte-identical to canonical")
            image = images.get(build)
            if image is not None:
                actual = image.read(build_address, overwrite)
                if actual != expected:
                    errors.append(f"{name}: {build} bytes at 0x{build_address:08x} are {actual.hex(' ')}")
        if check_disassembly and "canonical" in images:
            if _objdump() is None:
                notes.append("llvm-objdump not found; instruction-boundary checks skipped")
                check_disassembly = False
            else:
                errors.extend(_check_boundaries(images["canonical"].path, name, address, overwrite))
    errors.extend(_validate_services(manifest, images, symbols, check_disassembly))
    errors.extend(_validate_hal_sites(manifest, images, symbols, check_disassembly))
    errors.extend(_validate_hal_replacements(manifest, images, symbols))
    errors.extend(kit_hooks.validate(manifest, symbols, check_disassembly and "canonical" in images))
    if check_header:
        header = resolve(HEADER_PATH)
        if not header.is_file() or header.read_text() != render_header(manifest):
            errors.append(f"{HEADER_PATH} is stale; run `tony hooks generate`")
        for path, standalone in ((kit_hooks.KIT_SOURCE_PATH, False), (kit_hooks.STANDALONE_SOURCE_PATH, True)):
            source = resolve(path)
            if not source.is_file() or source.read_text() != kit_hooks.render_source(manifest, standalone):
                errors.append(f"{path} is stale; run `tony hooks generate`")
    from .hal.shaders import check_staleness as _check_shader_staleness

    errors.extend(_check_shader_staleness())
    return errors, notes


def _validate_services(
    manifest: dict[str, Any],
    images: dict[str, PeImage],
    symbols: dict[int, str],
    check_disassembly: bool,
) -> list[str]:
    errors: list[str] = []
    clock = manifest.get("services", {}).get("lockstep_clock")
    if not clock:
        return errors
    builds = manifest.get("builds", {})
    imported = {
        int(item["address"]): item["name"]
        for item in load_yaml("re/symbols/imported.yml").get("functions", [])
    }
    sites = [("timer_callback", clock.get("timer_callback", {}))]
    sites += [(f"delivery_sites[{index}]", site) for index, site in enumerate(clock.get("delivery_sites", []))]
    if len(sites) - 1 > MAX_CLOCK_SITES:
        errors.append(f"lockstep_clock: at most {MAX_CLOCK_SITES} delivery sites")
    for key, site in sites:
        name, overwrite = site.get("name"), site.get("overwrite")
        if not isinstance(overwrite, int) or not 5 <= overwrite <= MAX_OVERWRITE:
            errors.append(f"lockstep_clock.{key}: overwrite must be 5..{MAX_OVERWRITE}")
            continue
        canonical = site.get("builds", {}).get("canonical", {}).get("address")
        if key != "timer_callback" and site.get("kind") not in CLOCK_SITE_KINDS:
            errors.append(f"lockstep_clock.{key}: kind must be one of {sorted(CLOCK_SITE_KINDS)}")
        elif site.get("kind") != "spin" and name not in (symbols.get(canonical), imported.get(canonical)):
            errors.append(f"lockstep_clock.{key}: 0x{canonical or 0:08x} is not a function named {name}")
        for build, entry in site.get("builds", {}).items():
            if build not in builds:
                errors.append(f"lockstep_clock.{key}: unknown build {build!r}")
                continue
            expected = _expected_bytes(entry)
            if len(expected) != overwrite:
                errors.append(f"lockstep_clock.{key}.{build}: expected has {len(expected)} bytes")
            image = images.get(build)
            if image is not None and image.read(int(entry["address"]), overwrite) != expected:
                errors.append(f"lockstep_clock.{key}.{build}: bytes at 0x{int(entry['address']):08x} differ")
        if check_disassembly and "canonical" in images and isinstance(canonical, int):
            errors.extend(_check_boundaries(images["canonical"].path, f"lockstep_clock.{key}", canonical, overwrite))
    state = clock.get("timer_state", {})
    globals_by_address = {
        int(item["address"]): item["name"]
        for item in load_yaml("re/symbols/globals.yml").get("globals", [])
    }
    canonical_state = state.get("builds", {}).get("canonical")
    if globals_by_address.get(canonical_state) != state.get("name"):
        errors.append(f"lockstep_clock.timer_state: re/symbols/globals.yml does not name it {state.get('name')}")
    for build, spec in builds.items():
        if build == "canonical" or not spec.get("address_map") or build not in state.get("builds", {}):
            continue
        data_map = {int(canonical): int(variant) for variant, canonical in load_yaml(spec["address_map"]).get("data", [])}
        if data_map.get(canonical_state) != state["builds"][build]:
            errors.append(f"lockstep_clock.timer_state: {build} address disagrees with {spec['address_map']}")
    return errors


def _validate_hal_sites(
    manifest: dict[str, Any],
    images: dict[str, PeImage],
    symbols: dict[int, str],
    check_disassembly: bool,
) -> list[str]:
    """services.hal.entry_sites: named function entries, exact bytes, clean boundaries."""

    errors: list[str] = []
    hal = manifest.get("services", {}).get("hal", {})
    onexit = hal.get("onexit")
    if onexit:
        canonical = onexit.get("builds", {}).get("canonical")
        if symbols.get(canonical) != onexit.get("name"):
            errors.append(f"hal.onexit: 0x{canonical or 0:08x} is not the curated function {onexit.get('name')}")
        for build, spec in manifest.get("builds", {}).items():
            if build == "canonical" or not spec.get("address_map") or build not in onexit.get("builds", {}):
                continue
            mapped = _variant_map(spec["address_map"]).get(canonical)
            if mapped is None or mapped[0] != onexit["builds"][build]:
                errors.append(f"hal.onexit: {build} address disagrees with {spec['address_map']}")
    sites = hal.get("entry_sites", [])
    if len(sites) > MAX_HAL_SITES:
        errors.append(f"hal: at most {MAX_HAL_SITES} entry sites")
    builds = manifest.get("builds", {})
    for index, site in enumerate(sites):
        key = f"hal.entry_sites[{index}]"
        name, overwrite = site.get("name"), site.get("overwrite")
        if site.get("role") not in HAL_SITE_ROLES:
            errors.append(f"{key}: role must be one of {sorted(HAL_SITE_ROLES)}")
        if site.get("kind") not in HAL_SITE_KINDS:
            errors.append(f"{key}: kind must be one of {sorted(HAL_SITE_KINDS)}")
        if not isinstance(overwrite, int) or not 5 <= overwrite <= MAX_OVERWRITE:
            errors.append(f"{key}: overwrite must be 5..{MAX_OVERWRITE}")
            continue
        canonical = site.get("builds", {}).get("canonical", {}).get("address")
        if symbols.get(canonical) != name:
            errors.append(f"{key}: 0x{canonical or 0:08x} is not the curated function {name}")
        for build, entry in site.get("builds", {}).items():
            if build not in builds:
                errors.append(f"{key}: unknown build {build!r}")
                continue
            expected = _expected_bytes(entry)
            if len(expected) != overwrite:
                errors.append(f"{key}.{build}: expected has {len(expected)} bytes")
            image = images.get(build)
            if image is not None and image.read(int(entry["address"]), overwrite) != expected:
                errors.append(f"{key}.{build}: bytes at 0x{int(entry['address']):08x} differ")
        if check_disassembly and "canonical" in images and isinstance(canonical, int):
            errors.extend(_check_boundaries(images["canonical"].path, key, canonical, overwrite))
    return errors


def _validate_hal_replacements(
    manifest: dict[str, Any],
    images: dict[str, PeImage],
    symbols: dict[int, str],
) -> list[str]:
    """services.hal.replacements and services.hal.symbols.

    A replacement must be a curated hook point of its service in
    hal_boundary.yml with a registered adapter; its entry bytes are exact per
    build. The retail body never runs, so unlike a trampoline hook the patch
    may end inside an instruction. Symbols must be curated names whose
    per-build addresses agree with the address map.
    """

    errors: list[str] = []
    hal = manifest.get("services", {}).get("hal", {})
    replacements = hal.get("replacements", [])
    builds = manifest.get("builds", {})
    maps = {
        build: (_variant_map(spec["address_map"]),
                {int(canonical): int(variant) for variant, canonical in load_yaml(spec["address_map"]).get("data", [])})
        for build, spec in builds.items()
        if build != "canonical" and spec.get("address_map")
    }
    boundary = {
        (int(point["canonical"]), point["name"]): point.get("service")
        for point in load_yaml(HAL_BOUNDARY_PATH).get("hook_points", [])
    }
    adapters = _registered_hal_adapters()
    seen: set[str] = set()
    for index, entry in enumerate(replacements):
        name, overwrite, service = entry.get("name"), entry.get("overwrite"), entry.get("service")
        key = f"hal.replacements[{index}] {name}"
        if name in seen:
            errors.append(f"{key}: duplicate replacement")
        seen.add(name)
        if service not in HAL_SERVICES:
            errors.append(f"{key}: service must be one of {list(HAL_SERVICES)}")
        if not isinstance(overwrite, int) or not 5 <= overwrite <= MAX_OVERWRITE:
            errors.append(f"{key}: overwrite must be 5..{MAX_OVERWRITE}")
            continue
        canonical = entry.get("builds", {}).get("canonical", {}).get("address")
        if symbols.get(canonical) != name:
            errors.append(f"{key}: 0x{canonical or 0:08x} is not the curated function {name}")
        if boundary.get((canonical, name), "<missing>") != service:
            errors.append(f"{key}: not a {service} hook point in {HAL_BOUNDARY_PATH}")
        if name not in adapters:
            errors.append(f"{key}: no OT_REGISTER_HAL_ADAPTER({name}, ...) under {HAL_ADAPTER_SOURCES}")
        for build, where in entry.get("builds", {}).items():
            if build not in builds:
                errors.append(f"{key}: unknown build {build!r}")
                continue
            address = int(where["address"])
            expected = _expected_bytes(where)
            if len(expected) != overwrite:
                errors.append(f"{key}.{build}: expected has {len(expected)} bytes")
            if build in maps:
                mapped = maps[build][0].get(canonical)
                if mapped is None or mapped[0] != address:
                    errors.append(f"{key}: {build} address 0x{address:08x} disagrees with {builds[build]['address_map']}")
            image = images.get(build)
            if image is not None and image.read(address, overwrite) != expected:
                errors.append(f"{key}.{build}: bytes at 0x{address:08x} are {image.read(address, overwrite).hex(' ')}")
    globals_by_address = {
        int(item["address"]): item["name"]
        for item in load_yaml("re/symbols/globals.yml").get("globals", [])
    }
    names: set[str] = set()
    for index, entry in enumerate(hal.get("symbols", [])):
        name, kind = entry.get("name"), entry.get("kind")
        key = f"hal.symbols[{index}] {name}"
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_]\w*", name):
            errors.append(f"{key}: name must be a C identifier")
            continue
        if name in names:
            errors.append(f"{key}: duplicate symbol")
        names.add(name)
        if kind not in HAL_SYMBOL_KINDS:
            errors.append(f"{key}: kind must be one of {list(HAL_SYMBOL_KINDS)}")
            continue
        canonical = entry.get("builds", {}).get("canonical")
        curated = symbols if kind == "function" else globals_by_address
        if not isinstance(canonical, int) or curated.get(canonical) != name:
            errors.append(f"{key}: 0x{canonical or 0:08x} is not the curated {kind} {name}")
            continue
        for build, address in entry.get("builds", {}).items():
            if build not in builds:
                errors.append(f"{key}: unknown build {build!r}")
            elif build in maps:
                if kind == "function":
                    mapped = maps[build][0].get(canonical)
                    mapped_address = mapped[0] if mapped else None
                else:
                    mapped_address = maps[build][1].get(canonical)
                if mapped_address != address:
                    errors.append(f"{key}: {build} address 0x{int(address):08x} disagrees with "
                                  f"{builds[build]['address_map']}")
    return errors


def _registered_hal_adapters() -> dict[str, str]:
    registered: dict[str, str] = {}
    pattern = re.compile(r"OT_REGISTER_HAL_ADAPTER\((\w+),\s*(\w+)\)")
    for source in sorted(resolve(HAL_ADAPTER_SOURCES).glob("*.cpp")):
        for hook, function in pattern.findall(source.read_text()):
            registered[hook] = function
    return registered


def _registered_natives() -> dict[tuple[str, str], str]:
    registered: dict[tuple[str, str], str] = {}
    pattern = re.compile(r"OT_REGISTER_(NATIVE|FAULT)\((\w+),\s*(\w+)\)")
    for source in sorted(resolve(NATIVE_SOURCES).glob("*.cpp")):
        for kind, hook, function in pattern.findall(source.read_text()):
            registered[(kind.lower(), hook)] = function
    return registered


# -------------------------------------------------------------------- codegen


def render_header(manifest: dict[str, Any] | None = None) -> str:
    manifest = manifest if manifest is not None else load_manifest()
    natives = _native_statuses()
    builds = list(manifest.get("builds", {}).items())
    hooks = manifest.get("hooks", [])
    lines = [
        "/* Generated by `tony hooks generate` from re/config/native_hooks.yml and",
        " * re/native/functions.yml (native status -> default). Do not edit. */",
        "#ifndef OPENTONY_NATIVE_HOOKS_GENERATED_H",
        "#define OPENTONY_NATIVE_HOOKS_GENERATED_H",
        "",
        '#include "hook_types.h"',
        "",
        f"#define OT_BUILD_COUNT {len(builds)}u",
        f"#define OT_HOOK_COUNT {len(hooks)}u",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
    ]
    for hook in hooks:
        if kit_hooks.is_kit_hook(hook):
            continue
        lines.append(f"extern void *const ot_native_{hook['name']};")
        if hook.get("fault"):
            lines.append(f"extern void *const ot_fault_{hook['name']};")
    lines += ["#ifdef __cplusplus", "}", "#endif", ""]
    kit_names = [hook["name"] for hook in hooks if kit_hooks.is_kit_hook(hook)]
    lines += [
        "/* Kit hooks: the entry points come from opentony_native.dll at startup (src/hooks/kit_abi.h). */",
        f"#define OT_KIT_HOOK_COUNT {len(kit_names)}u",
        *[f"static void *ot_kit_{name} = 0;" for name in kit_names],
        "",
        "static const OtBuildIdentity ot_builds[OT_BUILD_COUNT] = {",
    ]
    for build, spec in builds:
        lines.append(f'    {{"{build}", 0x{int(spec["pe_timestamp"]):08x}u, 0x{int(spec["size_of_image"]):08x}u}},')
    lines += ["};", "", "static const OtHookSpec ot_hooks[OT_HOOK_COUNT] = {"]
    for hook in hooks:
        name = hook["name"]
        canonical = int(hook["builds"]["canonical"])
        status = natives.get(canonical, {}).get("status", "tested")
        expected = ", ".join(f"0x{value:02x}" for value in _expected_bytes(hook))
        addresses = ", ".join(f"0x{int(hook['builds'].get(build, 0)):08x}u" for build, _ in builds)
        fault = f"&ot_fault_{name}" if hook.get("fault") else "0"
        default = 1 if default_on(hook, status) else 0
        kit = kit_hooks.is_kit_hook(hook)
        abi = KIT_ABI if kit else ABIS.get(hook.get("abi"), "0")
        native = f"&ot_kit_{name}" if kit else f"&ot_native_{name}"
        lines += [
            (f"    /* {name}: native status {status} -> default {'on' if default else 'off'}"
             f"{' (Kit ' + hook['kit'] + ')' if kit else ''} */"),
            f'    {{"{name}", {abi}, {int(hook["overwrite"])}u,',
            f"     {{{expected}}},",
            f"     {HOOKABLE_STATUSES.get(status, '0')}, {default}u, {native}, {fault},",
            (f"     {{{addresses}}}, {int(hook.get('stack_args', 0)) if kit else 0}u, "
             f"{1 if kit and hook.get('selftest') == 'words' else 0}u, {1 if kit and hook.get('returns') else 0}u}},"),
        ]
    lines += ["};", ""]
    lines += _render_clock(manifest.get("services", {}).get("lockstep_clock"), [build for build, _ in builds])
    lines += [""]
    lines += _render_hal(manifest.get("services", {}).get("hal", {}), [build for build, _ in builds])
    lines += ["", "#endif /* OPENTONY_NATIVE_HOOKS_GENERATED_H */", ""]
    return "\n".join(lines)


def _render_clock(clock: dict[str, Any] | None, builds: list[str]) -> list[str]:
    def site(entry: dict[str, Any] | None) -> str:
        if not entry:
            return "{0u, {0}}"
        expected = ", ".join(f"0x{value:02x}" for value in _expected_bytes(entry))
        return f"{{0x{int(entry['address']):08x}u, {{{expected}}}}}"

    if not clock:
        return ["static const OtLockstepClockSpec ot_lockstep_clock = {0u, {{0u, {0}}}, {0u}, 0u, {{0, 0u, 0u, {{0u, {0}}}}}};"]
    callback, state = clock["timer_callback"], clock["timer_state"]
    sites = clock.get("delivery_sites", [])
    lines = [
        "/* services.lockstep_clock (see re/config/native_hooks.yml) */",
        "static const OtLockstepClockSpec ot_lockstep_clock = {",
        f"    {int(callback['overwrite'])}u,",
        "    {" + ", ".join(site(callback["builds"].get(build)) for build in builds) + "},",
        "    {" + ", ".join(f"0x{int(state['builds'].get(build, 0)):08x}u" for build in builds) + "},",
        f"    {len(sites)}u,",
        "    {",
    ]
    for entry in sites:
        lines += [
            f'        {{"{entry["name"]}", {CLOCK_SITE_KINDS[entry["kind"]]}, {int(entry["overwrite"])}u,',
            "         {" + ", ".join(site(entry["builds"].get(build)) for build in builds) + "}},",
        ]
    return lines + ["    },", "};"]


def _render_hal(hal: dict[str, Any], builds: list[str]) -> list[str]:
    def site(entry: dict[str, Any] | None) -> str:
        if not entry:
            return "{0u, {0}}"
        expected = ", ".join(f"0x{value:02x}" for value in _expected_bytes(entry))
        return f"{{0x{int(entry['address']):08x}u, {{{expected}}}}}"

    sites = hal.get("entry_sites", [])
    replacements = hal.get("replacements", [])
    symbols = hal.get("symbols", [])
    onexit = hal.get("onexit", {}).get("builds", {})
    lines = ["/* services.hal (see re/config/native_hooks.yml and docs/HAL.md) */"]
    if replacements:
        lines += ["#ifdef __cplusplus", 'extern "C" {', "#endif"]
        lines += [f"extern void *const ot_hal_adapter_{entry['name']};" for entry in replacements]
        lines += ["#ifdef __cplusplus", "}", "#endif", ""]
    lines.append("enum OtHalSymbolIndex {")
    lines += [f"    OT_HAL_SYM_{entry['name']} = {index}," for index, entry in enumerate(symbols)]
    lines += [f"    OT_HAL_SYMBOL_COUNT = {len(symbols)}", "};", ""]
    lines.append(f"static const OtHalReplacement ot_hal_replacements[{max(len(replacements), 1)}] = {{")
    for entry in replacements:
        service = HAL_SERVICES.index(entry["service"])
        lines += [
            (f'    {{"{entry["name"]}", {service}u /* {entry["service"]} */, {int(entry["overwrite"])}u, '
             f"&ot_hal_adapter_{entry['name']},"),
            "     {" + ", ".join(site(entry["builds"].get(build)) for build in builds) + "}},",
        ]
    if not replacements:
        lines.append("    {0, 0u, 0u, 0, {{0u, {0}}}}")
    lines += ["};", "", f"static const OtHalSymbol ot_hal_symbols[{max(len(symbols), 1)}] = {{"]
    for entry in symbols:
        addresses = ", ".join(f"0x{int(entry['builds'].get(build, 0)):08x}u" for build in builds)
        lines.append(f'    {{"{entry["name"]}", {{{addresses}}}}},')
    if not symbols:
        lines.append("    {0, {0u}}")
    lines += [
        "};",
        "",
        "static const OtHalSpec ot_hal_spec = {",
        "    {" + ", ".join(f"0x{int(onexit.get(build, 0)):08x}u" for build in builds) + "},",
        f"    {len(sites)}u,",
        "    {",
    ]
    for entry in sites:
        head = (f'        {{"{entry["name"]}", {HAL_SITE_ROLES[entry["role"]]}, {HAL_SITE_KINDS[entry["kind"]]}, '
                f'{int(entry["overwrite"])}u,')
        lines += [head, "         {" + ", ".join(site(entry["builds"].get(build)) for build in builds) + "}},"]
    if not sites:
        lines.append("        {0, 0u, 0u, 0u, {{0u, {0}}}}")
    return lines + [
        "    },",
        f"    {len(replacements)}u, ot_hal_replacements,",
        f"    {len(symbols)}u, ot_hal_symbols,",
        "};",
    ]


def generate(check: bool = False) -> bool:
    manifest = load_manifest()
    outputs = [
        (resolve(HEADER_PATH), render_header(manifest)),
        (resolve(kit_hooks.KIT_SOURCE_PATH), kit_hooks.render_source(manifest)),
        (resolve(kit_hooks.STANDALONE_SOURCE_PATH), kit_hooks.render_source(manifest, True)),
    ]
    if check:
        return all(path.is_file() and path.read_text() == text for path, text in outputs)
    for path, text in outputs:
        path.write_text(text)
    return True


# ---------------------------------------------------------------------- build


def ensure_sdk() -> Path:
    """Publish a lower-case symlink view of the VC6 headers and libraries.

    clang-cl on Linux is case-sensitive while the VC6 SDK uses upper-case file
    names (WINDOWS.H, KERNEL32.LIB) and its headers include lower-case ones.
    """

    source = resolve(VC6_DIR)
    target = resolve(SDK_DIR)
    stamp = target / ".source"
    if (target / "include/windows.h").exists() and stamp.is_file() and stamp.read_text() == str(source):
        return target
    if not (source / "INCLUDE").is_dir():
        raise HookError(f"VC6 toolchain not found at {source}; run `tony setup vc6`")
    if target.exists():
        shutil.rmtree(target)
    for part in ("INCLUDE", "LIB"):
        root = source / part
        for path in sorted(root.rglob("*")):
            link = target / part.lower() / str(path.relative_to(root)).lower()
            if path.is_dir():
                link.mkdir(parents=True, exist_ok=True)
            else:
                link.parent.mkdir(parents=True, exist_ok=True)
                if not link.exists():
                    link.symlink_to(path)
    stamp.write_text(str(source))
    return target


def _cmake_build(source: str, binary: str, sdk: Path, *, clean: bool, options: tuple[str, ...] = ()) -> None:
    build_dir = resolve(binary)
    if clean and build_dir.exists():
        shutil.rmtree(build_dir)
    stamp = build_dir / ".opentony-configure"
    arguments = [
        "cmake", "-S", str(resolve(source)), "-B", str(build_dir), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={resolve(TOOLCHAIN_FILE)}",
        f"-DOPENTONY_WIN32_SDK={sdk}",
        *options,
    ]
    configuration = " ".join(arguments)
    if not (build_dir / "build.ninja").is_file() or not stamp.is_file() or stamp.read_text() != configuration:
        subprocess.run(arguments, cwd=ROOT, check=True)
        stamp.write_text(configuration)
    subprocess.run(["cmake", "--build", str(build_dir)], cwd=ROOT, check=True)


def build(*, capture: bool = False, clean: bool = False) -> None:
    if not generate(check=True):
        raise HookError(f"{HEADER_PATH} is stale; run `tony hooks generate`")
    sdk = ensure_sdk()
    from . import sdl3

    sdl3.ensure()
    _cmake_build("src/hooks", HOOKS_BUILD_DIR, sdk, clean=clean,
                 options=(f"-DOPENTONY_SDL3_INCLUDE={sdl3.include_dir()}",))
    build_kits(clean=clean)
    if capture:
        _cmake_build("src/capture/win32", CAPTURE_BUILD_DIR, sdk, clean=clean)


def build_kits(*, clean: bool = False, arch: int = 32) -> None:
    """opentony_native.dll: the Kits and their generated adapters (llvm-mingw, src/hooks/kits). `arch` 64: only the standalone (DLL, spike, video DLL),
    for x86-64, in build/hooks/kits64 (milestone 11b)."""

    from . import ffmpeg, imgui_source, llvm_mingw, sdl3, zlib_source

    toolchain = llvm_mingw.ensure()
    target = "x86_64" if arch == 64 else "i686"
    ffmpeg.ensure(target)  # the standalone's movie and music decoder (5.3b): headers to build against, DLLs to load at run time
    zlib = zlib_source.source_dir()  # the standalone's zlib (8c): upstream source, checked against the pin
    imgui = imgui_source.source_dir()  # the THPSource2 launcher's UI (docs/LAUNCHER.md): upstream source, checked against the pin
    build_dir = resolve(KITS64_BUILD_DIR if arch == 64 else KITS_BUILD_DIR)
    if clean and build_dir.exists():
        shutil.rmtree(build_dir)
    arguments = [
        "cmake", "-S", str(resolve("src/hooks/kits")), "-B", str(build_dir), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={resolve(KITS64_TOOLCHAIN_FILE if arch == 64 else KITS_TOOLCHAIN_FILE)}",
        f"-DOPENTONY_LLVM_MINGW={toolchain}",
        f"-DOPENTONY_FFMPEG_INCLUDE={ffmpeg.include_dir(target)}",
        f"-DOPENTONY_ZLIB_SOURCE={zlib}",
        f"-DOPENTONY_SDL3_INCLUDE={sdl3.include_dir()}",  # the spike's video (7c): the HAL's SDL GL backend, SDL3.dll loaded at run time
        f"-DOPENTONY_IMGUI_SOURCE={imgui}",
    ]
    stamp = build_dir / ".opentony-configure"
    configuration = " ".join(arguments)
    if not (build_dir / "build.ninja").is_file() or not stamp.is_file() or stamp.read_text() != configuration:
        subprocess.run(arguments, cwd=ROOT, check=True)
        stamp.write_text(configuration)
    subprocess.run(["cmake", "--build", str(build_dir)], cwd=ROOT, check=True)
    check_production_memory(build_dir, toolchain, arch)


# 11d (docs/PORT64_AUDIT.md D3): the production DLLs hold the arena backend of RetailMemory only. retail_memory.hpp puts each flavour in
# its own inline namespace (`flat_memory`, `fixture_memory`), so a DLL's symbol table says which one it was built with.
PRODUCTION_DLLS = {32: ("opentony_native.dll", "opentony_standalone.dll"), 64: ("opentony_standalone.dll",)}
_NOT_FLAT = re.compile(r"(?<!flat_memory::)\bRetailMemory\b")


def memory_backend_problems(symbols: list[str]) -> list[str]:
    """Problems with the demangled symbol lines of a production DLL: a fixture-flavour (sparse) RetailMemory, or none at all to tell by."""

    named = [line for line in symbols if "RetailMemory" in line]
    if not named:
        return ["no RetailMemory symbol: the backend cannot be told"]
    problems = [line for line in named if "fixture_memory" in line or _NOT_FLAT.search(line)]
    problems += [line for line in symbols if re.search(r"RetailMemory::(?:add_region|regions)\b", line) and line not in problems]
    return problems


def check_production_memory(build_dir: Path, toolchain: Path, arch: int) -> None:
    nm = toolchain / "bin" / "llvm-nm"
    for name in PRODUCTION_DLLS[arch]:
        dll = build_dir / name
        if not dll.is_file():
            raise HookError(f"{dll} not built")
        lines = subprocess.run([str(nm), "-C", str(dll)], cwd=ROOT, check=True, capture_output=True, text=True).stdout.splitlines()
        problems = memory_backend_problems(lines)
        if problems:
            raise HookError(f"{dll}: not the arena-only RetailMemory (11d):\n  " + "\n  ".join(problems[:10]))


def _built(name: str) -> Path:
    if name == "SDL3.dll":
        from . import sdl3

        path = sdl3.dll_path()
        if not path.is_file():
            raise HookError(f"{path} missing; run `tony setup sdl3`")
        return path
    path = resolve(KITS_BUILD_DIR if name == "opentony_native.dll" else HOOKS_BUILD_DIR) / name
    if not path.is_file():
        raise HookError(f"{path} not built; run `tony hooks build`")
    return path


# -------------------------------------------------------------------- staging


def default_game_dir() -> Path:
    from .nocd import nocd_executable

    return nocd_executable().parent


def _guard_target(directory: Path) -> None:
    game_root = resolve("game").resolve()
    if directory.resolve() == game_root or game_root in directory.resolve().parents:
        raise HookError(f"refusing to write into user-supplied game inputs: {directory}")


def staged_current(directory: Path) -> bool:
    return all(
        (directory / name).is_file() and (directory / name).read_bytes() == _built(name).read_bytes()
        for name in STAGED_FILES
    )


PROXY_MARKER = b"OpenTony dinput proxy"


def stage(directory: Path | None = None) -> Path:
    directory = directory or default_game_dir()
    _guard_target(directory)
    existing = directory / "dinput.dll"
    if existing.is_file() and PROXY_MARKER not in existing.read_bytes():
        raise HookError(
            f"{existing} belongs to another mod; rename it (e.g. dinput_chain.dll), set "
            "`chain = dinput_chain.dll` under [proxy] in opentony.ini, then stage again"
        )
    for name in STAGED_FILES:
        shutil.copyfile(_built(name), directory / name)
    ini = directory / "opentony.ini"
    if not ini.exists():
        shutil.copyfile(resolve(DEFAULT_INI), ini)
    return directory


def unstage(directory: Path | None = None) -> Path:
    directory = directory or default_game_dir()
    _guard_target(directory)
    for name in STAGED_FILES + RUNTIME_OUTPUTS:
        with contextlib.suppress(FileNotFoundError):
            (directory / name).unlink()
    return directory


def _find_case_insensitive(directory: Path, name: str) -> Path:
    for candidate in directory.iterdir():
        if candidate.name.lower() == name.lower():
            return candidate
    raise HookError(f"{name} not found in {directory}")


def _merge_overrides(current: str | None, override: str) -> str:
    key = override.split("=", 1)[0]
    parts = [part for part in (current or "").split(";") if part and part.split("=", 1)[0] != key]
    return ";".join([*parts, override])


def _wine_z_path(path: Path) -> str:
    """Wine's default Z: drive maps the Unix root."""

    return "Z:" + str(path.resolve()).replace("/", "\\")


@contextlib.contextmanager
def hooks_environment(spec: str | None, clock: str | None = None) -> Iterator[None]:
    """Run Wine children with or without the staged hook harness.

    With a spec, the current DLLs are staged beside the no-CD executable and
    Wine is told to prefer the native dinput.dll proxy; OPENTONY_HOOKS carries
    the selection and OPENTONY_CLOCK the clock mode (see src/hooks/opentony.ini).
    Logs and call statistics go to build/hooks/runtime/. Without a spec the
    builtin dinput is forced so staged files cannot leak into a baseline run.
    """

    names = ("OPENTONY_HOOKS", "OPENTONY_CLOCK", "OPENTONY_LOG_DIR", "WINEDLLOVERRIDES")
    saved = {name: os.environ.get(name) for name in names}
    if spec is None and clock is not None:
        raise HookError("--clock needs --hooks (the clock service lives in opentony.dll)")
    try:
        if spec is None:
            os.environ.pop("OPENTONY_HOOKS", None)
            os.environ["WINEDLLOVERRIDES"] = _merge_overrides(saved["WINEDLLOVERRIDES"], "dinput=b")
        else:
            directory = default_game_dir()
            if not staged_current(directory):
                stage(directory)
            output = runtime_dir()
            output.mkdir(parents=True, exist_ok=True)
            for name in RUNTIME_OUTPUTS:
                with contextlib.suppress(FileNotFoundError):
                    (output / name).unlink()
            os.environ["OPENTONY_HOOKS"] = spec
            os.environ["OPENTONY_LOG_DIR"] = _wine_z_path(output)
            if clock is not None:
                os.environ["OPENTONY_CLOCK"] = clock
            os.environ["WINEDLLOVERRIDES"] = _merge_overrides(saved["WINEDLLOVERRIDES"], "dinput=n,b")
            print(f"hooks: OPENTONY_HOOKS={spec} OPENTONY_CLOCK={os.environ.get('OPENTONY_CLOCK', 'realtime')}")
        yield
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        if spec is not None:
            stats = runtime_dir() / "opentony.stats"
            if stats.is_file():
                print("hooks: " + stats.read_text().strip().replace("\n", "\nhooks: "))


def with_hooks(handler: Callable[[Any], int]) -> Callable[[Any], int]:
    @functools.wraps(handler)
    def wrapper(args) -> int:
        with hooks_environment(getattr(args, "hooks", None), getattr(args, "clock", None)):
            return handler(args)

    return wrapper


def runtime_dir() -> Path:
    from . import workers

    worker = workers.worker_id()
    return workers.runtime_dir(worker) if worker is not None else resolve(RUNTIME_DIR)


def read_stats(directory: Path | None = None) -> dict[str, dict[str, int]]:
    stats: dict[str, dict[str, int]] = {}
    path = (directory or runtime_dir()) / "opentony.stats"
    if not path.is_file():
        return stats
    for line in path.read_text().splitlines():
        fields = line.split()
        if fields[:1] == ["hook"] and len(fields) > 2:
            stats[fields[1]] = {
                key: int(value) for key, value in (item.split("=", 1) for item in fields[2:])
            }
    return stats


# --------------------------------------------------------------------- parity

PARITY_ROOT = "build/hooks/parity"


@dataclasses.dataclass(frozen=True)
class ParityVariant:
    label: str
    hooks: str
    expect_equal: bool = True
    stress_ms: int | None = None
    hal: str | None = None  # OPENTONY_HAL for this capture (docs/HAL.md)


def default_parity_variants(manifest: dict[str, Any] | None = None) -> list[ParityVariant]:
    """The A/B matrix every hook change must pass.

    `none` loads the harness with every hook routed back to retail (the
    reference, captured twice to prove the clock makes runs reproducible);
    `all` runs every hook native; `stress` flips the master switch every few
    milliseconds mid-run; each fault variant must diverge (negative control).
    """

    manifest = manifest if manifest is not None else load_manifest()
    variants = [
        ParityVariant("retail-a", "none"),
        ParityVariant("retail-b", "none"),
        ParityVariant("native-all", "all"),
        ParityVariant("native-all-stress", "all", stress_ms=3),
    ]
    variants += [
        ParityVariant(f"fault-{hook['name']}", f"fault:{hook['name']}", expect_equal=False)
        for hook in manifest.get("hooks", [])
        if hook.get("fault")
    ][:1]
    return variants


def hal_parity_variants(hal: str) -> list[ParityVariant]:
    """The HAL gate: services on SDL must reproduce the all-retail run exactly.

    The reference is the harness with every hook and service retail; the HAL
    selection is captured twice (reproducibility) and once more with every
    native hook on and the master switch flipping (services and leaf hooks
    are independent).
    """

    return [
        ParityVariant("retail-a", "none", hal="none"),
        ParityVariant("hal-a", "none", hal=hal),
        ParityVariant("hal-b", "none", hal=hal),
        ParityVariant("hal-native-all-stress", "all", stress_ms=3, hal=hal),
    ]


def _capture_variant(scenario: str, output: Path, variant: ParityVariant, clock: str) -> int:
    from .scenarios import scenario_capture

    with contextlib.suppress(FileNotFoundError):
        output.with_suffix(".otcap").unlink()  # never decode a stale transport
    saved = {name: os.environ.get(name) for name in ("OPENTONY_HOOK_STRESS", "OPENTONY_HAL")}
    try:
        if variant.stress_ms:
            os.environ["OPENTONY_HOOK_STRESS"] = str(variant.stress_ms)
        else:
            os.environ.pop("OPENTONY_HOOK_STRESS", None)
        if variant.hal:
            os.environ["OPENTONY_HAL"] = variant.hal
        else:
            os.environ.pop("OPENTONY_HAL", None)
        with hooks_environment(variant.hooks, clock):
            return scenario_capture(
                argparse.Namespace(name=scenario, output=str(output), frames=None, backend="inproc", force=True)
            )
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def read_hal_services(directory: Path | None = None) -> str:
    path = (directory or runtime_dir()) / "opentony.stats"
    if not path.is_file():
        return ""
    for line in path.read_text().splitlines():
        if line.startswith("hal_services "):
            return line[len("hal_services "):].strip()
    return ""


def _hooked_ranges() -> list[tuple[int, int]]:
    """The canonical address range of every hooked function."""

    starts = [int(hook["builds"]["canonical"]) for hook in load_manifest().get("hooks", [])]
    return [(start, kit_hooks.function_end(start)) for start in starts]


def parity_directory(scenario: str, hal: str | None = None) -> Path:
    directory = resolve(PARITY_ROOT) / scenario
    if hal:
        directory = directory / ("hal-" + re.sub(r"[^A-Za-z0-9]+", "-", hal).strip("-"))
    return directory


def parity_variants(hal: str | None = None) -> list[ParityVariant]:
    return hal_parity_variants(hal) if hal else default_parity_variants()


def parity_capture(
    scenario: str, variant: ParityVariant, *, clock: str = "lockstep-fast", attempts: int = 3, hal: str | None = None
) -> dict[str, Any]:
    """Capture one variant of `scenario` (the unit of work of a gate run) and keep what the comparison needs:
    the recording, and the call statistics of this run in `<variant>.row.json` beside it. Returns that row."""

    directory = parity_directory(scenario, hal)
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / f"{variant.label}.otrec"
    code = 1
    for _attempt in range(attempts):
        code = _capture_variant(scenario, output, variant, clock)
        if code == 0:
            break
    stats = read_stats()
    row: dict[str, Any] = {
        "variant": variant.label,
        "hooks": variant.hooks,
        "native_calls": sum(item.get("native_calls", 0) for item in stats.values()),
        "retail_calls": sum(item.get("retail_calls", 0) for item in stats.values()),
        "per_hook_native": {name: item.get("native_calls", 0) for name, item in stats.items()},
        "hal": variant.hal or "",
        "hal_services": read_hal_services(),
        "code": code,
        "attempts": attempts,
    }
    (directory / f"{variant.label}.row.json").write_text(json.dumps(row, indent=2) + "\n")
    return row


def parity_compare(scenario: str, *, hal: str | None = None, rows: list[dict[str, Any]] | None = None) -> list[dict[str, Any]]:
    """Compare the captured variants of `scenario` against the reference (the first that was captured).
    `rows` are the results of `parity_capture` in variant order; by default they are read back from disk."""

    from .capture import compare_recordings

    directory = parity_directory(scenario, hal)
    variants = parity_variants(hal)
    if rows is None:
        rows = []
        for variant in variants:
            path = directory / f"{variant.label}.row.json"
            rows.append(json.loads(path.read_text()) if path.is_file() else {
                "variant": variant.label, "hooks": variant.hooks, "native_calls": 0, "retail_calls": 0,
                "per_hook_native": {}, "hal": variant.hal or "", "hal_services": "", "code": 1, "attempts": 0})
    results: list[dict[str, Any]] = []
    reference: Path | None = None
    for variant, captured in zip(variants, rows, strict=True):
        output = directory / f"{variant.label}.otrec"
        code = int(captured["code"])
        row = {key: value for key, value in captured.items() if key not in ("code", "attempts")}
        if code != 0:
            row.update(result="ERROR", ok=False, detail=f"capture failed after {captured.get('attempts', '?')} attempts")
        elif reference is None:
            reference = output
            row.update(result="REFERENCE", ok=True, detail="")
        else:
            # a native variant: the call-observation events of the retail call sites inside replaced
            # functions do not exist, and the never-initialized half-word follows the stack (PHASE_5_SUMMARY.md, 5.0)
            native = variant.hooks != "none"
            comparison = compare_recordings(
                reference, output, scope="portable" if variant.hal else "all",
                native_callers=_hooked_ranges() if native else (),
                function_end=kit_hooks.function_end, mask_uninitialized=native,
            )
            equal = bool(comparison["equal"])
            detail = "" if equal else json.dumps(comparison.get("difference"))[:160]
            unobservable = comparison.get("unobservable_events") or [0, 0]
            if any(unobservable):
                detail = (detail + "; " if detail else "") + (
                    f"set aside {unobservable[0]}/{unobservable[1]} observations of calls from replaced functions")
            if equal and variant.hal:
                exact = compare_recordings(reference, output, scope="all")
                if not exact["equal"]:
                    difference = exact.get("difference") or {}
                    detail = (f"process identity differs ({'/'.join(str(p) for p in difference.get('path', [])[2:])}: "
                              f"{difference.get('left')} -> {difference.get('right')}); every other field exact")
            row.update(
                result="MATCH" if equal else "DIVERGE",
                ok=equal == variant.expect_equal,
                detail=detail,
            )
        results.append(row)
    return results


def parity(
    scenario: str, *, clock: str = "lockstep-fast", attempts: int = 3, hal: str | None = None
) -> list[dict[str, Any]]:
    """Capture `scenario` under each variant and compare against the reference (the serial gate; `tony gates`
    runs the same two halves, `parity_capture` per variant and `parity_compare`, side by side)."""

    rows = [parity_capture(scenario, variant, clock=clock, attempts=attempts, hal=hal) for variant in parity_variants(hal)]
    return parity_compare(scenario, hal=hal, rows=rows)


# ------------------------------------------------------------------- selftest


def selftest(build: str, spec: str = "all") -> tuple[int, str]:
    """Run opentony.dll's in-process differential self-test for one build.

    The executable is copied with the DLLs into build/hooks/selftest/<build>;
    the test runs in DllMain and terminates the process before WinMain, so no
    game data or display output is needed.
    """

    from .common import wine_env

    source = build_executable(build)
    if source is None or not source.is_file():
        raise HookError(f"{build}: executable not present")
    directory = resolve(SELFTEST_ROOT) / build
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, directory / "THawk2.exe")
    # Static imports must resolve before any DllMain runs.
    for name in SELFTEST_IMPORTS:
        shutil.copyfile(_find_case_insensitive(default_game_dir(), name), directory / name)
    for name in STAGED_FILES:
        shutil.copyfile(_built(name), directory / name)
    for name in RUNTIME_OUTPUTS:
        with contextlib.suppress(FileNotFoundError):
            (directory / name).unlink()
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env.update(
        OPENTONY_SELFTEST="1",
        OPENTONY_HOOKS=spec,
        WINEDLLOVERRIDES=_merge_overrides(env.get("WINEDLLOVERRIDES"), "dinput=n,b"),
    )
    env.pop("DISPLAY", None)
    completed = subprocess.run(
        ["wine", str(directory / "THawk2.exe")],
        cwd=directory,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=300,
        check=False,
    )
    report = directory / "opentony.selftest"
    log = directory / "opentony.log"
    text = report.read_text() if report.is_file() else ""
    if log.is_file():
        text += "\n--- opentony.log ---\n" + log.read_text()
    passed = completed.returncode == 0 and "RESULT PASS" in text
    return (0 if passed else 1), text or completed.stdout


# ------------------------------------------------------------------- commands


def _print_findings(errors: list[str], notes: list[str]) -> None:
    for note in notes:
        print(f"NOTE {note}")
    for error in errors:
        print(f"FAIL {error}")


def hooks_verify(_args=None, *, quiet: bool = False) -> int:
    errors, notes = validate()
    if errors or not quiet:
        _print_findings(errors, notes)
    count = len(load_manifest().get("hooks", []))
    print(f"{'FAIL' if errors else 'OK  '} native hooks: {count} hooks, {len(errors)} errors ({MANIFEST_PATH})")
    return 1 if errors else 0


def hooks_generate(args) -> int:
    if getattr(args, "check", False):
        current = generate(check=True)
        print(f"{HEADER_PATH}: {'current' if current else 'STALE'}")
        return 0 if current else 1
    generate()
    print(f"wrote {HEADER_PATH}, {kit_hooks.KIT_SOURCE_PATH} and {kit_hooks.STANDALONE_SOURCE_PATH}")
    return 0


def replay32(only: list[str] | None = None) -> list[dict[str, Any]]:
    """Run the 32-bit builds of the forced-call replays under Wine, on the native ctest's fixtures."""

    from .common import wine_env

    build_kits()
    kits = resolve(KITS_BUILD_DIR)
    subprocess.run(["cmake", "--build", str(kits), "--target", "kit_replay_tests"], cwd=ROOT, check=True)
    listing = subprocess.run(
        ["ctest", "--test-dir", str(resolve("build/native")), "--show-only=json-v1"],
        cwd=ROOT, check=True, stdout=subprocess.PIPE, text=True,
    )
    env = wine_env(load_yaml("re/config/wine.yml")["wine"].get("headless_prefix", ".tools/wineprefix-headless"))
    env.pop("DISPLAY", None)
    env["WINEDEBUG"] = "-all"
    rows = []
    for test in json.loads(listing.stdout).get("tests", []):
        name = test["name"]
        if not name.endswith("_retail_calls_test") or (only and not any(part in name for part in only)):
            continue
        exe = kits / f"{name}.exe"
        if not exe.is_file():
            rows.append({"test": name, "ok": False, "detail": "no 32-bit build"})
            continue
        arguments = [os.path.relpath(arg, ROOT) if os.path.isabs(arg) else arg for arg in test["command"][1:]]
        completed = subprocess.run(
            ["wine", str(exe), *arguments], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, errors="replace", check=False, timeout=3600,
        )
        lines = [line for line in completed.stdout.splitlines() if line.strip()]
        summary = next((line for line in lines if "cases replayed" in line), lines[-1] if lines else "")
        rows.append({"test": name, "ok": completed.returncode == 0, "code": completed.returncode,
                     "fixtures": len(arguments), "detail": summary.strip()})
    return rows


def hooks_replay32(args) -> int:
    try:
        rows = replay32(args.only)
    except (HookError, subprocess.CalledProcessError) as exc:
        print(f"FAIL {exc}")
        return 1
    for row in rows:
        print(f"{'OK  ' if row['ok'] else 'FAIL'} {row['test']:<52} {row.get('fixtures', 0):>3} fixtures  {row['detail'][:110]}")
    failed = sum(1 for row in rows if not row["ok"])
    print(f"{'FAIL' if failed else 'OK  '} 32-bit replays: {len(rows) - failed}/{len(rows)} match their fixtures")
    out = resolve(KITS_BUILD_DIR) / "replay32.json"
    out.write_text(json.dumps(rows, indent=2) + "\n")
    return 1 if failed or not rows else 0


def hooks_census(_args) -> int:
    """How many natives a Kit hook row could carry today, and what blocks the rest (Phase 5.1 planning)."""

    result = kit_hooks.census()
    print(f"{result['hookable']} natives with status tested or later")
    for category, count in result["categories"].items():
        print(f"  {count:>5}  {category}")
    print("entries that cannot be patched: " + ", ".join(f"{reason} {count}" for reason, count in result["unpatchable"].items()))
    print("services that block the most natives (replay-only or no stub declaration):")
    for service, count in result["blockers"][:25]:
        print(f"  {count:>5}  {service}")
    print("named services with a plain stub (each needs a live_calls row): " + ", ".join(
        f"{service} {count}" for service, count in result["named_live"][:20]))
    out = resolve(KITS_BUILD_DIR)
    out.mkdir(parents=True, exist_ok=True)
    (out / "census.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"wrote {out / 'census.json'}")
    return 0


def hooks_row(args) -> int:
    """Print the manifest row of a Kit hook, from the executable and the fixtures."""

    symbols = _symbols()
    by_name = {name: address for address, name in symbols.items()}
    address = int(args.function, 16) if args.function.lower().startswith("0x") else by_name.get(args.function)
    if address is None or address not in symbols:
        print(f"FAIL {args.function}: not a named function in re/symbols/functions.yml")
        return 1
    row, problems = kit_hooks.make_row(symbols[address], address, args.kit, args.call, args.returns)
    for problem in problems:
        print(f"# PROBLEM {problem}")
    print(kit_hooks.render_row(row))
    return 1 if problems else 0


def hooks_sdk(_args) -> int:
    print(f"VC6 SDK view: {ensure_sdk()}")
    return 0


def hooks_build(args) -> int:
    if str(getattr(args, "arch", "32")) == "64":
        # 11b: the x86-64 standalone alone; the hooked build lives in the 32-bit retail process
        try:
            if not generate(check=True):
                raise HookError(f"{HEADER_PATH} is stale; run `tony hooks generate`")
            from . import sdl3

            sdl3.ensure()
            build_kits(clean=bool(getattr(args, "clean", False)), arch=64)
        except (HookError, subprocess.CalledProcessError) as exc:
            print(f"FAIL {exc}")
            return 1
        for name in ("opentony.exe", "opentony_standalone.dll", "opentony_spike.exe", "opentony_spike_video.dll"):
            print(f"built {resolve(KITS64_BUILD_DIR) / name}")
        return 0
    try:
        build(capture=bool(getattr(args, "capture", False)), clean=bool(getattr(args, "clean", False)))
    except (HookError, subprocess.CalledProcessError) as exc:
        print(f"FAIL {exc}")
        return 1
    for name in STAGED_FILES:
        print(f"{'built' if name != 'SDL3.dll' else 'SDL3 '} {_built(name)}")
    return 0


def hooks_stage(args) -> int:
    directory = resolve(args.game_dir) if getattr(args, "game_dir", None) else None
    try:
        if getattr(args, "remove", False):
            print(f"removed hook harness from {unstage(directory)}")
        else:
            target = stage(directory)
            print(f"staged {', '.join(STAGED_FILES)} in {target}")
            print("Wine: WINEDLLOVERRIDES=dinput=n,b; Windows: nothing else needed")
    except HookError as exc:
        print(f"FAIL {exc}")
        return 1
    return 0


def hooks_selftest(args) -> int:
    builds = list(load_manifest()["builds"]) if args.build == "all" else [args.build]
    failed = False
    for build in builds:
        exe = build_executable(build)
        if exe is None or not exe.is_file():
            print(f"SKIP {build}: executable not present")
            continue
        try:
            code, text = selftest(build, args.hooks)
        except HookError as exc:
            print(f"FAIL {build}: {exc}")
            failed = True
            continue
        print(f"== {build} ==")
        print(text.strip())
        failed = failed or code != 0
    return 1 if failed else 0


def smoke_session() -> bool:
    """`boot-menu` with every row native and no recorder: a hook that crashes the front end shows here."""

    from . import session_scenarios

    started = time.time()
    document = session_scenarios.load_session("boot-menu")
    ok = session_scenarios.session_run("smoke-boot-menu", document, ParityVariant("smoke", "all"))
    print(f"{'ok  ' if ok else 'FAIL'} boot-menu, every row native: "
          f"{'finished' if ok else 'no session.done (crash or hang): bisect with build/p51t/crashbis.py'} ({time.time() - started:.0f}s)")
    return ok


def smoke_capture(scenario: str) -> bool:
    """One Warehouse capture with the in-process recorder and every hook installed (none enabled): a hook on a
    recorder seam, or one that crashes at startup, shows here."""

    output = resolve(PARITY_ROOT) / "smoke" / "retail.otrec"
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    code = _capture_variant(scenario, output, ParityVariant("smoke", "none"), "lockstep-fast")
    print(f"{'ok  ' if code == 0 else 'FAIL'} {scenario} capture with the recorder, every hook installed "
          f"({time.time() - started:.0f}s)" + ("" if code == 0 else ": a hook probably sits on a recorder seam or crashes at startup"))
    return code == 0


def hooks_smoke(args) -> int:
    """Two quick runs that catch what a gate chain would only report an hour in (5.1t): `smoke_session` and
    `smoke_capture`. `tony gates` runs them side by side."""

    session_ok = smoke_session()
    capture_ok = smoke_capture(args.scenario)
    return 0 if session_ok and capture_ok else 1


def print_parity(scenario: str, rows: list[dict[str, Any]], *, clock: str = "lockstep-fast", hal: str | None = None) -> bool:
    """Print the table of `tony hooks parity` for one scenario, write its `parity.json`; True when every row is ok."""

    print(f"\n{scenario} (clock {clock}{', hal ' + hal if hal else ''})")
    print(f"{'Variant':<24} {'Hooks':<26} {'Result':<10} {'OK':<4} {'native calls':>13} {'retail calls':>13}")
    for row in rows:
        print(
            f"{row['variant']:<24} {row['hooks']:<26} {row['result']:<10} {'yes' if row['ok'] else 'NO':<4} "
            f"{row['native_calls']:>13} {row['retail_calls']:>13}"
        )
        if row["detail"]:
            print(f"    {row['detail']}")
        if row.get("hal") and row.get("hal_services"):
            print(f"    hal: {row['hal_services']}")
    native = next((row for row in rows if row["variant"] == "native-all"), None)
    if native:
        print("native calls per hook: " + ", ".join(f"{k}={v}" for k, v in native["per_hook_native"].items()))
    out = parity_directory(scenario, hal)
    out.mkdir(parents=True, exist_ok=True)
    (out / "parity.json").write_text(json.dumps(rows, indent=2) + "\n")
    return all(row["ok"] for row in rows)


def hooks_parity(args) -> int:
    failed = False
    for scenario in args.scenarios:
        try:
            rows = parity(scenario, clock=args.clock, attempts=args.attempts, hal=getattr(args, "hal", None))
        except HookError as exc:
            print(f"FAIL {scenario}: {exc}")
            failed = True
            continue
        failed = not print_parity(scenario, rows, clock=args.clock, hal=getattr(args, "hal", None)) or failed
    return 1 if failed else 0


def hooks_status(_args) -> int:
    manifest = load_manifest()
    natives = _native_statuses()
    builds = list(manifest["builds"])
    print(f"{'Hook':<26} {'Status':<16} {'Default':<8} " + " ".join(f"{b:<14}" for b in builds))
    for hook in manifest.get("hooks", []):
        status = natives.get(int(hook["builds"]["canonical"]), {}).get("status", "?")
        default = "native" if default_on(hook, status) else "retail"
        addresses = " ".join(f"0x{int(hook['builds'].get(b, 0)):08x}    " for b in builds)
        print(f"{hook['name']:<26} {status:<16} {default:<8} {addresses}")
    return 0


def register(subparsers) -> None:
    hooks = subparsers.add_parser("hooks", help="native replacement hook harness (opentony.dll)")
    sub = hooks.add_subparsers(dest="hooks_command", required=True)
    p = sub.add_parser("status", help="list hooks, native status and per-build addresses")
    p.set_defaults(func=hooks_status)
    p = sub.add_parser("verify", help="validate the hook manifest against executables and knowledge")
    p.set_defaults(func=hooks_verify)
    p = sub.add_parser("generate", help=f"regenerate {HEADER_PATH}")
    p.add_argument("--check", action="store_true", help="only report whether the header is current")
    p.set_defaults(func=hooks_generate)
    p = sub.add_parser("row", help="print the manifest row of a Kit hook (prologue, convention, observed services)")
    p.add_argument("function", help="curated name or 0x address")
    p.add_argument("--kit", required=True, help="kits.<id> of re/config/native_hooks.yml")
    p.add_argument("--call", required=True, help="the Kit call, as in the replay tables (e.g. 'handle_flips(self)')")
    p.add_argument("--returns", action="store_true", help="the retail callers use EAX")
    p.set_defaults(func=hooks_row)
    p = sub.add_parser("smoke", help="two quick runs (boot-menu with every row native; one recorder capture) to run before any gate chain")
    p.add_argument("--scenario", default="warehouse-idle")
    p.set_defaults(func=hooks_smoke)
    p = sub.add_parser("census", help="classify every native for live hooking (services, prologue); Phase 5.1 planning")
    p.set_defaults(func=hooks_census)
    p = sub.add_parser("replay32", help="run the forced-call replays built 32-bit (llvm-mingw) under Wine")
    p.add_argument("only", nargs="*", help="substrings of the test names to run (default: all)")
    p.set_defaults(func=hooks_replay32)
    p = sub.add_parser("sdk", help=f"publish the lower-case VC6 SDK view under {SDK_DIR}")
    p.set_defaults(func=hooks_sdk)
    p = sub.add_parser("build", help="cross-build opentony.dll and the dinput.dll proxy (clang-cl)")
    p.add_argument("--capture", action="store_true", help="also build the in-process capture recorder")
    p.add_argument("--clean", action="store_true", help="reconfigure from scratch")
    p.add_argument("--arch", choices=("32", "64"), default="32", help="64: only the x86-64 standalone, into build/hooks/kits64 (milestone 11b)")
    p.set_defaults(func=hooks_build)
    p = sub.add_parser("static-data", help="8b: write the git-ignored static-data image from the player's THawk2.exe (rejects any other build)")
    p.add_argument("--exe", help="the executable (default: executables.thps2_pc of re/config/binaries.yml)")
    p.add_argument("--out", help="the image (default: build/static/thawk2_static.img)")
    p.set_defaults(func=static_data.static_data)
    p = sub.add_parser("stage", help="copy the harness next to the no-CD executable (or --game-dir)")
    p.add_argument("--game-dir", help="install directory containing THawk2.exe")
    p.add_argument("--remove", action="store_true", help="remove staged files instead")
    p.set_defaults(func=hooks_stage)
    p = sub.add_parser("parity", help="capture scenarios with hooks retail/native/stress/fault and compare exactly")
    p.add_argument("scenarios", nargs="+", help="scenario names from re/scenarios/")
    p.add_argument("--clock", default="lockstep-fast", choices=("lockstep", "lockstep-fast"))
    p.add_argument("--attempts", type=int, default=3, help="retries per capture (Wine startup is flaky)")
    p.add_argument("--hal", help="HAL gate: OPENTONY_HAL selection (e.g. input=sdl,clock=sdl) against all-retail")
    p.set_defaults(func=hooks_parity)
    p = sub.add_parser("selftest", help="run the in-process retail/native differential self-test")
    p.add_argument("--build", default="all", help="canonical, original_2000 or all")
    p.add_argument("--hooks", default="all", help="OPENTONY_HOOKS selection (default: all)")
    p.set_defaults(func=hooks_selftest)
