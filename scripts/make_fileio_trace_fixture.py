#!/usr/bin/env python3
"""Turn a retail file-I/O trace NDJSON (Phase 4.5 probe build/p45/fileio_trace.py:
FileIO_BackendOpen 0x004e75b0, PKR2_OpenResource 0x004e9940, backend seek/read/close,
the PRE manager load/unload/lookup) into the compact text fixture consumed by
src/assets/resource_retail_trace_test.cpp.

Fixture layout, one record per line, in retail call order:

  # comment
  Z <size of ALL.PKR the trace ran against>
  K <found 0|1> <dir> <name> [<marker:hex> <offset> <stored size> <decoded size>]
                    PKR2_OpenResource lookup as retail normalised it (dir keeps its '/')
  O <owner> <slot|-1> <size|-1> <path>
                    FileIO_BackendOpen(path, mode 0); slot and slot size after the call.
                    owner: r = FileIO_OpenResource (0x00449030), e = FileIO_Exists (0x00449460),
                    x = any other caller (the texture loader, sound loader, ...)
  A <name> <size> <b|p>
                    a FileIO_OpenResource call, hoisted in front of the K/O records it caused:
                    b = went to the backend, p = served by the PRE fast path
  X <name> <0|1>    a FileIO_Exists call, hoisted in front of its K/O records
  D <sub dir>       FileIO_SetSubDir
  I <base length>   FileIO_Init (0x00448f50) returned; the base directory ("data\\", length 5)
                    is empty until then
  S <slot> <offset> <whence> <ret>       FileIO_BackendSeek
  R <slot> <count> <ret> <crc32:hex|->   FileIO_BackendRead, crc of the bytes it copied
  C <slot> <ret>                         FileIO_BackendClose
  L <name> <slot it landed in>           PRE_Load wrapper (0x004a90f0)
  U <name>                               PRE_Unload (0x004a9330)
  Q <name> <size|-1>                     PRE_FindEmbeddedResource (0x004a9410): payload size or -1

Only the backend opens issued with mode 0 are expected (the retail game never uses the
other two modes in a level load); anything else aborts the conversion.
"""

import json
import struct
import sys
from pathlib import Path

INT_MAX = 1 << 31
OPEN_RESOURCE = (0x00449030, 0x00449230)
EXISTS = (0x00449460, 0x004494B0)


def owner_of(caller):
    if OPEN_RESOURCE[0] <= caller < OPEN_RESOURCE[1]:
        return "r"
    if EXISTS[0] <= caller < EXISTS[1]:
        return "e"
    return "x"


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >= INT_MAX else value


def main(trace_path, pkr_path, out_path):
    rows = [json.loads(line) for line in open(trace_path)]
    lines = [
        "# retail file-I/O trace (Phase 4.5); see scripts/make_fileio_trace_fixture.py",
        f"Z {Path(pkr_path).stat().st_size}",
    ]
    counts = {}
    # index in `lines` of the K record of the newest backend open per owner since the last
    # game-owned call; A and X records are hoisted in front of it
    anchor = {"r": None, "e": None}
    for row in rows:
        kind = row["t"]
        if kind == "pkr_lookup":
            found = row["ret"] != 0
            out = bytes.fromhex(row["out"]) if found else b""
            text = f"K {int(found)} {row['dir']} {row['name']}"
            if found:
                marker, offset, size1, size2 = struct.unpack_from("<4I", out, 0x20)
                text += f" {marker:x} {offset} {size1} {size2}"
            lines.append(text)
            pending_k = len(lines) - 1
        elif kind == "backend_open":
            if row["mode"] != 0:
                raise SystemExit(f"unexpected backend mode {row['mode']} for {row['path']}")
            slot = s32(row["slot"]) if row["slot"] > -0x1_0000_0000 else -1
            slot = slot if slot >= 0 else -1
            size = row["after"]["size"][slot] if slot >= 0 else -1
            owner = owner_of(row["caller"])
            lines.append(f"O {owner} {slot} {size} {row['path']}")
            if owner in anchor:
                anchor[owner] = pending_k
        elif kind == "open_resource":
            text = f"A {row['name']} {row['size']}"
            if anchor["r"] is None:
                lines.append(text + " p")
            else:
                lines.insert(anchor["r"], text + " b")
                anchor = {key: (None if value is None else value + (1 if value >= anchor["r"] else 0))
                          for key, value in anchor.items()}
            anchor["r"] = None
        elif kind == "exists":
            if anchor["e"] is None:
                raise SystemExit(f"exists({row['name']}) without a backend open")
            lines.insert(anchor["e"], f"X {row['name']} {row['ret']}")
            anchor = {key: (None if value is None else value + (1 if value >= anchor["e"] else 0))
                      for key, value in anchor.items()}
            anchor["e"] = None
        elif kind == "fileio_init":
            lines.append(f"I {row['base_len']}")
        elif kind == "set_subdir":
            lines.append(f"D {row['name']}")
        elif kind == "backend_seek":
            lines.append(f"S {row['slot']} {row['offset']} {row['whence']} {row['ret']}")
        elif kind == "backend_read":
            crc = f"{row['crc']:x}" if "crc" in row else "-"
            lines.append(f"R {row['slot']} {row['count']} {row['ret']} {crc}")
        elif kind == "backend_close":
            lines.append(f"C {row['slot']} {row['ret']}")
        elif kind == "pre_load":
            slot = row["slots"].index(row["name"]) if row["name"] in row["slots"] else -1
            lines.append(f"L {row['name']} {slot}")
        elif kind == "pre_unload":
            lines.append(f"U {row['name']}")
        elif kind == "pre_lookup":
            lines.append(f"Q {row['name']} {row['size'] if row['payload'] else -1}")
        else:
            continue
        counts[kind] = counts.get(kind, 0) + 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, counts)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: make_fileio_trace_fixture.py TRACE.ndjson ALL.PKR OUT.txt")
    main(*sys.argv[1:])
