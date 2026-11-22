"""Phase 5, milestone 8b: the static data of the player's own THawk2.exe, as a git-ignored build output.

The standalone program needs the initialised `.rdata` / `.data` (tables, strings, constants) at their retail addresses and
must not need the retail executable to run. This step reads them from the executable the player has beside `ALL.PKR`: the
build the natives target, or the original 2000 release's unprotected build, whose data tony/static_variant.py rebuilds at the
canonical addresses (re/xref/original_2000_static.yml). Any other build is refused. It writes `build/static/thawk2_static.img`.
No game bytes are committed (`/build/` is ignored). The format is read by `src/hooks/kits/standalone_spike.cpp`:

    "OTSD" u32 version(1) u32 image_base u32 pe_timestamp u32 size_of_image u32 section_count
    section_count x { u32 va, u32 size }, then the bytes of every section in that order

Code sections are left out (`.text` stays zero in the standalone: there is no retail code in it).
"""

from __future__ import annotations

import struct
from pathlib import Path

import pefile

from . import static_variant
from .common import load_yaml, relative_to_root, resolve, sha256

MAGIC = b"OTSD"
VERSION = 1
IMAGE = "build/static/thawk2_static.img"
_CODE = 0x00000020  # IMAGE_SCN_CNT_CODE


def canonical_exe() -> Path:
    return resolve(load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["path"])


def image_path() -> Path:
    return resolve(IMAGE)


def build_image(exe: Path) -> bytes:
    """The static image of `exe` (the canonical build, or the original 2000 release rebuilt to it); SystemExit with the reason for any other."""
    if not exe.is_file():
        raise SystemExit(f"{exe}: not found (put the player's THawk2.exe beside ALL.PKR)")
    want = load_yaml("re/config/native_hooks.yml")["builds"]["canonical"]
    try:
        pe = pefile.PE(str(exe), fast_load=True)
    except pefile.PEFormatError as exc:
        raise SystemExit(f"rejected: {exe} is not a PE file: {exc}") from exc
    found = sha256(exe)
    original = static_variant.variant_spec()
    if found == original["sha256"]:
        # the original 2000 release: its data rebuilt at the canonical addresses (re/xref/original_2000_static.yml)
        try:
            return static_variant.apply(static_variant.read_executable(exe), static_variant.load_recipe())
        except ValueError as exc:
            raise SystemExit(f"rejected: {exe}: {exc}") from exc
    if found == original["protected_disc_executable"]["sha256"]:
        raise SystemExit(
            f"rejected: {exe} is the copy-protected THawk2.exe of the original 2000 disc (SafeDisc 2: its data is encrypted). "
            f"Use the original release's unprotected THawk2.exe (sha256 {original['sha256']}) or the Activision Value re-release's.")
    if found != want["sha256"] or int(pe.FILE_HEADER.TimeDateStamp) != int(want["pe_timestamp"]) \
            or int(pe.OPTIONAL_HEADER.SizeOfImage) != int(want["size_of_image"]):
        raise SystemExit(
            f"rejected: {exe} is not the retail build the natives target (sha256 {found}, "
            f"timestamp {int(pe.FILE_HEADER.TimeDateStamp):#010x}, size of image {int(pe.OPTIONAL_HEADER.SizeOfImage):#010x}; "
            f"expected sha256 {want['sha256']})")
    base = int(pe.OPTIONAL_HEADER.ImageBase)
    data = exe.read_bytes()
    parts: list[tuple[int, bytes]] = []
    for section in pe.sections:
        if int(section.Characteristics) & _CODE:
            continue
        size = min(int(section.SizeOfRawData), int(section.Misc_VirtualSize))
        offset = int(section.PointerToRawData)
        if offset + size > len(data):
            continue
        parts.append((base + int(section.VirtualAddress), data[offset:offset + size]))
    header = MAGIC + struct.pack("<5I", VERSION, base, int(pe.FILE_HEADER.TimeDateStamp), int(pe.OPTIONAL_HEADER.SizeOfImage), len(parts))
    table = b"".join(struct.pack("<2I", va, len(blob)) for va, blob in parts)
    return header + table + b"".join(blob for _, blob in parts)


def ensure_image(exe: Path | None = None) -> Path:
    """The image path, written first when it is missing or older than the executable. Cheap enough to call before every run."""
    exe = exe or canonical_exe()
    out = image_path()
    if out.is_file() and exe.is_file() and out.stat().st_mtime >= exe.stat().st_mtime:
        return out
    blob = build_image(exe)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    return out


def static_data(args) -> int:
    exe = resolve(args.exe) if args.exe else canonical_exe()
    out = resolve(args.out) if args.out else image_path()
    blob = build_image(exe)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"{relative_to_root(out)}: {len(blob)} bytes from {exe}")
    return 0
