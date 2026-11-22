#!/usr/bin/env python3
"""Turn retail trick traces (Phase 4.7a probe build/p47/trick_trace.py: TrickScript_Update
0x00492ea0 and the services under it) into the compact text fixture consumed by
src/runtime/trick_retail_trace_test.cpp.

Fixture layout, one record per line (images are 0x3538-byte skater images written as the
words that differ from the previous image in the file, "index:hex"):

  # comment
  S <sha256 of the TRICKS.BIN the trace ran against>
  T <hex u16>*        the generated player trick key table (when it changes)
  U <seq> <frame> <clock> <time_scale> <step> <event_latch> <tricks_base>
  C <hex bytes>       controller record at skater +0x2ccc (0x100 bytes)
  I <diff>            image at TrickScript_Update entry
  E <diff>            image at the depth-0 TrickScript_Execute entry (only if it ran)
  K <service> <eax> <arg>*   a service call in order (the 6 stack words)
  P <diff>            image at the call entry
  R <diff>            image after the call returned
  O <diff>            image when Execute returned
  X <diff>            image at Update exit
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

WORDS = 0x3538 // 4
NAMES = {
    "anim_start": "anim_start", "anim_start_default_end": "anim_start_default_end",
    "anim_range": "anim_range", "anim_full": "anim_full", "state_transition": "state_transition",
    "point_timer": "point_timer", "bail": "bail", "trick_takeoff": "take_off", "trick_land": "land",
    "gap_apply": "gap_apply", "board_on": "board_on", "board_off": "board_off", "sfx_play": "sfx_play",
    "vibrate": "vibrate", "rail_restart": "rail_restart", "fn_46dd20": "fn_46dd20",
    "fn_431260": "fn_431260", "maybe_manual": "maybe_manual", "assert": "assert", "rnd": "rnd",
}


def diff_tokens(before, after):
    return " ".join(f"{i}:{after[i]:x}" for i in range(WORDS) if before[i] != after[i])


# Words of the trick state (script cursor and flags, history ring, start queue).
TRICK_REGION = range(0x29C0 // 4, 0x2C00 // 4)


def keep_block(block, entry, exit_, seq, idle_every):
    """Keep an update that ran a script, made a call, changed the trick state or carries a
    new key table, plus every `idle_every`-th quiet one. Each update replays from its own
    entry image, so thinning idle frames loses no evidence."""
    if not idle_every or seq % idle_every == 0:
        return True
    if any(line[0] in "KET" for line in block):
        return True
    return any(entry[i] != exit_[i] for i in TRICK_REGION)


def main(trace_paths, tricks_path, out_path, limit=0, idle_every=0):
    dropped = 0
    entry_image = None
    tricks = Path(tricks_path).read_bytes()
    lines = [
        "# retail trick trace (Phase 4.7a); see scripts/make_trick_trace_fixture.py",
        f"S {hashlib.sha256(tricks).hexdigest()}",
    ]
    written = [0] * WORDS  # image state in the fixture
    block = []             # lines of the update in progress; dropped if it never exits
    block_written = None
    table = None
    records = 0
    calls = {}
    for trace_path in trace_paths:
        image = None  # running probe image (per trace file)
        pending = None
        for raw in open(trace_path):
            r = json.loads(raw)
            kind = r["t"]
            if "img" in r:
                if "full" in r["img"]:
                    image = list(struct.unpack(f"<{WORDS}I", bytes.fromhex(r["img"]["full"])))
                else:
                    for index, value in r["img"]["d"]:
                        image[index] = value
            if kind == "entry":
                if limit and records >= limit:
                    break
                if block:
                    written = block_written  # an unfinished update is dropped
                block, block_written = [], list(written)
                entry_image = list(image)
                if r.get("table") is not None and r["table"] != table:
                    table = r["table"]
                    block.append("T " + " ".join(f"{w:x}" for w in table))
                block.append(
                    f"U {r['seq']} {r['frame']} {r['clock']} {r['time_scale']} {r['step']} "
                    f"{r['event_latch']} {r['tricks_base']}")
                block.append("C " + r["ctrl"])
                tag = "I"
            elif kind == "exec_in":
                tag = "E"
            elif kind == "call":
                name = NAMES[r["name"]]
                calls[r["seq"]] = calls.get(r["seq"], 0) + 1
                block.append(f"K {name} 0 " + " ".join(str(a) for a in r["args"]))
                tag = "P"
            elif kind == "ret":
                # patch the eax onto the K line just written
                k = max(i for i, l in enumerate(block) if l.startswith("K "))
                parts = block[k].split(" ")
                parts[2] = str(r["eax"])
                block[k] = " ".join(parts)
                tag = "R"
            elif kind == "exec_out":
                tag = "O"
            elif kind == "exit":
                tag = "X"
            else:
                continue
            block.append(f"{tag} " + diff_tokens(written, image))
            written = list(image)
            if kind == "exit":
                if keep_block(block, entry_image, image, r["seq"], idle_every):
                    lines.extend(block)
                    records += 1
                else:
                    written = block_written  # thinned idle update: the next diff is against what was kept
                    dropped += 1
                block = []
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{records} updates kept ({dropped} idle updates thinned), {sum(calls.values())} service calls "
          f"-> {out_path} ({Path(out_path).stat().st_size} bytes)")


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) < 3:
        raise SystemExit("usage: make_trick_trace_fixture.py TRICKS.BIN OUT.txt TRACE.ndjson... [--max N] [--idle-every N]")
    limit = 0
    idle_every = 0
    if "--idle-every" in args:
        i = args.index("--idle-every")
        idle_every = int(args[i + 1])
        del args[i:i + 2]
    if "--max" in args:
        i = args.index("--max")
        limit = int(args[i + 1])
        del args[i:i + 2]
    main(args[2:], args[0], args[1], limit, idle_every)
