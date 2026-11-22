#!/usr/bin/env python3
"""Identify statically linked library functions by relocation-masked code bytes.

Usage: python scripts/match_library_signatures.py [--library NAME=PATH ...] [--json PATH] [--min-size N]

Ghidra's Function ID databases need an interactive library import; this does the same job
for a toolchain we already own. Each library (an `ar` archive such as VC6's `LIBC.LIB`, or a
single COFF `.obj`) is split into its code sections (one function each under `/Gy`), the bytes
that a relocation patches (absolute addresses and call displacements) are wildcards, and a
section matches a function in the canonical executable when every other byte is equal. Output
is one record per executable function: the library, the object member and the symbol(s) that
matched. A function that several members match identically lists all of them.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_LNK_COMDAT = 0x1000
RELOCATED_WIDTH = {0x0006: 4, 0x0007: 4, 0x0014: 4, 0x000A: 2, 0x000B: 4}  # DIR32, DIR32NB, REL32, SECTION, SECREL


def parse_archive(data: bytes) -> list[tuple[str, bytes]]:
    """Members of an `ar` archive (the `/` and `//` linker members are consumed, not returned)."""

    if not data.startswith(b"!<arch>\n"):
        raise ValueError("not an ar archive")
    members, position, longnames = [], 8, b""
    while position + 60 <= len(data):
        header = data[position:position + 60]
        name, size = header[:16].decode("latin-1").strip(), int(header[48:58].decode("latin-1").strip())
        body = data[position + 60:position + 60 + size]
        position += 60 + size + (size & 1)
        if name == "/":
            continue
        if name == "//":
            longnames = body
            continue
        if name.startswith("/") and name[1:].isdigit():
            start = int(name[1:])
            stops = [i for i in (longnames.find(b"\0", start), longnames.find(b"\n", start)) if i >= 0]
            name = longnames[start:min(stops, default=len(longnames))].decode("latin-1").rstrip("/")
        members.append((name.rstrip("/"), body))
    return members


def parse_coff(data: bytes) -> list[dict]:
    """Code sections of a COFF object: name, symbols defined in them, bytes and relocated ranges."""

    machine, count, _stamp, symbol_table, symbol_count, optional, _flags = struct.unpack_from("<HHIIIHH", data, 0)
    if machine != 0x14C:
        return []
    string_table = symbol_table + symbol_count * 18
    strings = data[string_table:]

    def symbol_name(raw: bytes) -> str:
        if raw[:4] == b"\0\0\0\0":
            offset = struct.unpack_from("<I", raw, 4)[0]
            return strings[offset:strings.index(b"\0", offset)].decode("latin-1")
        return raw.rstrip(b"\0").decode("latin-1")

    symbols, index = [], 0
    while index < symbol_count:
        entry = data[symbol_table + index * 18:symbol_table + index * 18 + 18]
        value, section, kind, storage, aux = struct.unpack_from("<IhHBB", entry, 8)
        symbols.append({"name": symbol_name(entry[:8]), "value": value, "section": section, "type": kind,
                        "storage": storage})
        symbols.extend({"name": "", "value": 0, "section": 0, "type": 0, "storage": 0xFF} for _ in range(aux))
        index += 1 + aux
    sections = []
    base = 20 + optional
    for number in range(1, count + 1):
        header = data[base + (number - 1) * 40:base + number * 40]
        name = header[:8].rstrip(b"\0").decode("latin-1")
        _size, _va, raw_size, raw_pointer, reloc_pointer, _lines, reloc_count, _lc, flags = struct.unpack_from(
            "<IIIIIIHHI", header, 8)
        if not flags & IMAGE_SCN_CNT_CODE:
            continue
        content = data[raw_pointer:raw_pointer + raw_size]
        wild = bytearray(len(content))
        for r in range(reloc_count):
            offset, _symbol, kind = struct.unpack_from("<IIH", data, reloc_pointer + r * 10)
            width = RELOCATED_WIDTH.get(kind, 4)
            for byte in range(offset, min(offset + width, len(content))):
                wild[byte] = 1
        defined = sorted((s["value"], s["name"]) for s in symbols
                         if s["section"] == number and s["name"] and (
                             s["storage"] == 2 or s["storage"] == 3 and s["type"] == 0x20))
        sections.append({"name": name, "comdat": bool(flags & IMAGE_SCN_LNK_COMDAT), "bytes": content,
                         "wild": bytes(wild), "symbols": defined})
    return sections


def library_functions(path: Path, minimum: int) -> list[dict]:
    """Every code blob a library could contribute: {member, symbol, bytes, wild}."""

    if path.is_dir():
        members = [(item.name, item.read_bytes()) for item in sorted(path.glob("*.obj"))]
    else:
        raw = path.read_bytes()
        members = parse_archive(raw) if raw.startswith(b"!<arch>") else [(path.name, raw)]
    result = []
    for member, body in members:
        try:
            sections = parse_coff(body)
        except (struct.error, ValueError):
            continue
        for section in sections:
            content, wild = section["bytes"], section["wild"]
            symbols = section["symbols"]
            if section["comdat"] or len(symbols) <= 1:
                cuts = [(0, len(content), symbols[0][1] if symbols else section["name"])]
            else:
                # A plain .text with several functions: split at the symbols.
                offsets = sorted({value for value, _ in symbols})
                names = {value: name for value, name in symbols}
                cuts = [(start, end, names[start]) for start, end in zip(offsets, offsets[1:] + [len(content)])]
            for start, end, name in cuts:
                if end - start >= minimum:
                    result.append({"member": member, "symbol": name, "bytes": content[start:end],
                                   "wild": wild[start:end]})
    return result


def masked_equal(code: bytes, blob: dict) -> bool:
    """`code` begins with the blob's bytes at every non-wildcard position."""

    reference, wild = blob["bytes"], blob["wild"]
    if len(code) < len(reference):
        return False
    return all(wild[i] or code[i] == reference[i] for i in range(len(reference)))


def match_functions(functions: list[dict], image: bytes, base: int, text_start: int, libraries: dict[str, list[dict]],
                    slack: int = 24) -> dict[int, list[dict]]:
    """Executable address -> matching blobs. The function may be longer than the blob by padding only."""

    by_size: dict[int, list[tuple[str, dict]]] = defaultdict(list)
    for name, blobs in libraries.items():
        for blob in blobs:
            by_size[len(blob["bytes"])].append((name, blob))
    matches: dict[int, list[dict]] = {}
    for function in functions:
        offset = function["a"] - base - text_start
        if offset < 0 or offset + function["size"] > len(image):
            continue
        code = image[offset:offset + function["size"] + slack]
        found = []
        for size in range(max(1, function["size"] - slack), function["size"] + 1):
            for library, blob in by_size.get(size, ()):
                if masked_equal(code, blob):
                    found.append({"library": library, "member": blob["member"], "symbol": blob["symbol"],
                                  "size": size, "exact_size": size == function["size"]})
        if found:
            matches[function["a"]] = found
    return matches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--library", action="append", default=[], metavar="NAME=PATH",
                        help="library archive or object; default: the VC6 LIBC.LIB and LIBCMT.LIB")
    parser.add_argument("--json", type=Path, default=ROOT / "build/xref/library-matches.json")
    parser.add_argument("--min-size", type=int, default=16)
    parser.add_argument("--write", action="store_true",
                        help="write re/xref/library-matches.yml (the rows `tony xref apply` turns into names)")
    args = parser.parse_args(argv)

    import pefile

    from tony.common import resolve
    from tony.xref.common import CANONICAL, load_features

    libraries_spec = dict(item.split("=", 1) for item in args.library) or {
        "libc": ".tools/vc6/VC98/LIB/LIBC.LIB", "libcmt": ".tools/vc6/VC98/LIB/LIBCMT.LIB"}
    libraries = {name: library_functions(resolve(path), args.min_size) for name, path in libraries_spec.items()}
    exe = pefile.PE(str(resolve("build/disc/files/SETUP/data/THawk2.exe")))
    text = next(s for s in exe.sections if s.Name.startswith(b".text"))
    image = text.get_data()
    base = exe.OPTIONAL_HEADER.ImageBase
    functions = [f for f in load_features(CANONICAL)["functions"] if not f["thunk"] and not f["name"].startswith("Unwind@")]
    matches = match_functions(functions, image, base, text.VirtualAddress, libraries)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps({f"{a:#010x}": m for a, m in sorted(matches.items())}, indent=1) + "\n")
    print(f"{sum(len(b) for b in libraries.values())} library blobs; {len(matches)} executable functions matched")
    if args.write:
        write_matches(matches, libraries_spec)
    return 0


def write_matches(matches: dict[int, list[dict]], libraries_spec: dict[str, str]) -> None:
    from tony.common import resolve, sha256
    from tony.xref.common import write_rows

    rows = []
    for address, found in sorted(matches.items()):
        symbols = sorted({m["symbol"] for m in found})
        libraries = sorted({m["library"] for m in found})
        members = sorted({m["member"].replace("\\", "/").rsplit("/", 1)[-1] for m in found})
        rows.append([address, "|".join(symbols), "|".join(libraries), "|".join(members[:3]), found[0]["size"]])
    header = {
        "version": 1,
        "generated_by": "scripts/match_library_signatures.py --write",
        "libraries": {name: {"path": path, "sha256": sha256(resolve(path))} for name, path in libraries_spec.items()
                      if resolve(path).is_file()},
        "summary": {"matched": len(rows), "unambiguous": sum(1 for r in rows if "|" not in r[1])},
    }
    columns = "[canonical, symbols (| when several objects are byte-identical), libraries, members, size]"
    write_rows("re/xref/library-matches.yml", header, {"functions": (columns, rows)})


if __name__ == "__main__":
    raise SystemExit(main())
