#!/usr/bin/env python3
"""Turn a retail trick-database trace (Phase 4.7b probe build/p47b/db_trace.py) into the compact
text fixture consumed by src/runtime/trick_database_retail_trace_test.cpp.

Usage: make_trickdb_trace_fixture.py TRACE.ndjson [TRACE2.ndjson ...] TRICKS.BIN OUT.txt

Fixture layout, one record per line:

  # comment
  S <sha256 of the pristine TRICKS.BIN the game loaded>
  M <offset:byte>*     bytes the constructor changed in the loaded buffer (names lowercased in place)
  D <count> <times>    the manager image a Tricks_InitializeManager call left (seen <times> times)
  R <hexname> <source> <cost> <flags> <descriptor>   one record of the preceding D
  K <character> <class_mask> <costs hex> <record hex>   TrickConfig_InitializeCharacter's result
  A <stream> <block hex>   bindings applied to the alternate block from the section-6 stream
  T <character> <stream> <block hex>   a TrickTable_BuildForPlayer call (character -1: the alternate block): the key stream offset and
                                       the binding block it was given ...
  C <six words>        ... one line per return-combo entry ('?' where retail leaves scratch memory)
  W <words>            ... and the finished table
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

SKATER_MANAGER = 0x00568A6C
CONFIG_SIZE = 0x104
BLOCK_OFFSET = 0xCC
ALT_BLOCK = 0x0056A690


def load(paths):
    events = []
    for path in paths:
        for line in Path(path).read_text().splitlines():
            if line.strip():
                events.append(json.loads(line))
    return events


def records_of(hex_records, count):
    raw = bytes.fromhex(hex_records)
    out = []
    for i in range(count):
        chunk = raw[i * 0x28:(i + 1) * 0x28]
        name = chunk[:0x20].split(b"\0", 1)[0]
        source, cost, flags, descriptor = struct.unpack_from("<4H", chunk, 0x20)
        out.append((name, source, cost, flags, descriptor))
    return out


def character_slice(configs_hex, character):
    raw = bytes.fromhex(configs_hex)
    return raw[character * CONFIG_SIZE:(character + 1) * CONFIG_SIZE]


def main(argv):
    *trace_paths, tricks_path, out_path = argv
    pristine = Path(tricks_path).read_bytes()
    events = load(trace_paths)
    lines = [
        "# Retail trick database trace (Phase 4.7b): manager images, character records, key tables.",
        f"S {hashlib.sha256(pristine).hexdigest()}",
    ]

    buffers = [e for e in events if e["t"] == "buffer"]
    if not buffers:
        raise SystemExit("trace has no buffer event")
    loaded = bytes.fromhex(buffers[0]["hex"])
    if len(loaded) != len(pristine):
        raise SystemExit(f"loaded buffer is {len(loaded)} bytes, pristine {len(pristine)}")
    changes = [(i, b) for i, (a, b) in enumerate(zip(pristine, loaded)) if a != b]
    for i, b in changes:
        if not (ord("A") <= pristine[i] <= ord("Z") and b == pristine[i] + 32):
            raise SystemExit(f"byte {i:#x} changed from {pristine[i]:#x} to {b:#x}, not a lowercase pass")
    lines.append("M " + " ".join(f"{i:x}:{b:x}" for i, b in changes))

    seen = {}
    for e in events:
        if e["t"] == "init":
            key = tuple(records_of(e["records"], e["count"]))
            seen[key] = seen.get(key, 0) + 1
    for records, times in seen.items():
        lines.append(f"D {len(records)} {times}")
        for name, source, cost, flags, descriptor in records:
            lines.append(f"R {name.hex()} {source} {cost} {flags} {descriptor}")

    characters = set()
    for e in events:
        if e["t"] != "cfginit":
            continue
        character = e["args"][0]
        record = character_slice(e["configs_out"], character)
        key = (character, record)
        if key in characters:
            continue
        characters.add(key)
        class_mask = struct.unpack_from("<I", record, 0x34)[0]
        lines.append(f"K {character} {class_mask} {record[0x38:0x42].hex()} {record.hex()}")

    alternates = set()
    for e in events:
        if e["t"] == "apply" and e["name"] == "bindings" and e["args"][1] != 0:
            key = (e["alt_keys"] - e["base"], bytes.fromhex(e["alt_out"])[:0x35].hex())
            if key not in alternates:
                alternates.add(key)
                lines.append(f"A {key[0]} {key[1]}")

    tables = set()
    for e in events:
        if e["t"] != "build":
            continue
        block_ptr = e["block_ptr"]
        if block_ptr == ALT_BLOCK:
            character = -1  # the alternate block and the section-6 stream
        else:
            character = (block_ptr - SKATER_MANAGER - BLOCK_OFFSET) // CONFIG_SIZE
            if (block_ptr - SKATER_MANAGER - BLOCK_OFFSET) % CONFIG_SIZE != 0 or not 0 <= character < 21:
                raise SystemExit(f"build block {block_ptr:#x} is not a character record")
        stream = e["keys"] - e["base"]
        combos = bytes.fromhex(e["combos"]["hex"])
        shown_lines = []
        for i in range(e["combos"]["count"]):
            w = list(struct.unpack_from("<6H", combos, i * 12))
            shown = [str(w[0])]
            for slot in range(1, 4):
                shown.append(str(w[slot]) if slot <= w[0] else "?")
            shown.append(str(w[4]))
            shown.append(str(w[5]))
            shown_lines.append("C " + " ".join(shown))
        key = (character, stream, e["block"], tuple(shown_lines), tuple(e["table"]))
        if key in tables:
            continue
        tables.add(key)
        lines.append(f"T {character} {stream} {e['block']}")
        lines.extend(shown_lines)
        lines.append("W " + " ".join(str(x) for x in e["table"]))

    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"wrote {out_path}: {len(lines)} lines, {len(seen)} distinct manager images, "
          f"{len(characters)} character records, {len(tables)} tables")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
