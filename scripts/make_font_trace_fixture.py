#!/usr/bin/env python3
"""Turn a retail font trace NDJSON (Phase 4.5 probe build/p45/font_trace.py: Font_Create
0x0044aea0 with Font_Parse's FontGlyph_ConstructImage 0x00456d10 calls, Font_Find 0x0044b000,
Font_Unload 0x0044b0a0) into the compact text fixture consumed by
src/assets/fnt_retail_trace_test.cpp.

Fixture layout, one record per line, in retail call order:

  # comment
  Z <size of ALL.PKR the trace ran against>
  F <name> <size> <crc32:hex> <count> <atlas word:hex> <slot> <pal16x> <pal16y> <GetFree16Slot>
                    a Font_Create call: the file bytes it received (size, CRC-32), the
                    record count, the word stored at font +0x3c, the slot it landed in and the
                    palette-allocator inputs that word is derived from
  E <byte4> <byte5> <byte6>            runtime entry table (count+1 rows) bytes +4/+5/+6
  G <offset|-> <width> <height>        FontGlyph_ConstructImage arguments in order (count+1
                                       rows): pixel data offset inside the file buffer ('-'
                                       for the sentinel's separate blank buffer), width, height
  N find <name> <found 0|1>            Font_Find: whether it returned a font
  N unload <name>                      Font_Unload

The generator also checks that every glyph image object's width/height fields (+0x40, +0x42)
equal the entry bytes 4 and 5, and that the +0x3c word equals the allocator formula.
"""

import json
import struct
import sys
from pathlib import Path


def main(trace_path, pkr_path, out_path):
    rows = [json.loads(line) for line in open(trace_path)]
    lines = [
        "# retail font trace (Phase 4.5); see scripts/make_font_trace_fixture.py",
        f"Z {Path(pkr_path).stat().st_size}",
    ]
    counts = {"F": 0, "N": 0}
    pending = None
    for row in rows:
        kind = row["t"]
        if kind == "font_create":
            pending = row
        elif kind == "font_created":
            count = row["count"]
            word = row["word"]
            x, y, slot16 = pending["pal16x"], pending["pal16y"], row["slot16"]
            expected = ((x >> 4) & 0x3F) | (((slot16 + y) * 0x40) & 0xFFFF)
            assert word == expected & 0xFFFF, (row["name"], hex(word), hex(expected))
            lines.append(
                f"F {row['name']} {pending['size']} {pending['crc']:x} {count} {word:x} "
                f"{row['slot']} {x} {y} {slot16}"
            )
            assert len(row["entries"]) == count + 1 and len(row["glyphs"]) == count + 1
            for index, entry in enumerate(row["entries"]):
                assert entry[0] == 1, "glyph image missing"
                image = bytes.fromhex(row["images"][index])
                width, height = struct.unpack_from("<HH", image, 0x40)
                assert (width, height) == (entry[1], entry[2]), (row["name"], index)
                lines.append(f"E {entry[1]} {entry[2]} {entry[3]}")
            for index, glyph in enumerate(row["glyphs"]):
                offset = "-" if index == count else str(glyph["offset"])
                lines.append(f"G {offset} {glyph['w']} {glyph['h']}")
            counts["F"] += 1
        elif kind == "font_find":
            lines.append(f"N find {row['name']} {int(row['font'] != 0)}")
            counts["N"] += 1
        elif kind == "font_unload":
            lines.append(f"N unload {row['name']}")
            counts["N"] += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, counts)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: make_font_trace_fixture.py TRACE.ndjson ALL.PKR OUT.txt")
    main(*sys.argv[1:])
