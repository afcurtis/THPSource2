#!/usr/bin/env python3
"""Turn a retail power-up trace NDJSON (Phase 4.4c probe build/p44c/powerup_trace.py:
Powerup_Update 0x004a8ac0 and CPowerUp__TakeEffect 0x004a86f0) into the compact text
fixture consumed by src/trg/powerup_object_retail_trace_test.cpp.

Fixture layout, one record per line:

  # comment
  S <sha256 of the TRG file the trace ran against>
  U <seq> <update|take> <object> <frame counter> <AnimationTimeScale> <player count>
    <draw limit> <game mode> <skater> <primary 0|1> <skater word +0x2f64> <skater word +0x30b8>
  I <word:hex>*   entry image (0x100 bytes) as words differing from this object's previous entry
  X <word:hex>*   exit image as words differing from the entry
  K <name> <int>* a side-effect call in order (sfx_play, sfx_pitch, give_money,
                  give_goal_type, message, delete_messages, trick_takeoff, trick_land, exec_list)
  G <name> <int>  a world-query result in order (ground_height, goal_stat)
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

SIZE = 0x100
WORDS = SIZE // 4
INTERNAL = {"hide", "die"}  # the vtable slots the native object calls itself


def words(hex_bytes):
    return list(struct.unpack(f"<{WORDS}I", bytes.fromhex(hex_bytes)))


def diff(before, after):
    return [f"{i}:{after[i]:x}" for i in range(WORDS) if before[i] != after[i]]


def s32(value):
    return value - (1 << 32) if value & 0x80000000 else value


def main(trace_path, trg_path, out_path, limit=0):
    records = [json.loads(line) for line in open(trace_path)]
    trg = Path(trg_path).read_bytes()
    by_seq = {}
    for record in records:
        if record["t"] in ("entry", "exit", "call", "ret"):
            by_seq.setdefault(record["seq"], []).append(record)
    lines = [
        "# retail power-up trace (Phase 4.4c); see scripts/make_powerup_trace_fixture.py",
        f"S {hashlib.sha256(trg).hexdigest()}",
    ]
    previous = {}
    emitted = 0
    for seq in sorted(by_seq):
        group = by_seq[seq]
        entry = next((r for r in group if r["t"] == "entry"), None)
        exit_ = next((r for r in group if r["t"] == "exit"), None)
        if entry is None or exit_ is None:
            continue
        if limit and emitted >= limit:
            break
        obj = entry["obj"]
        image_in = words(entry["raw"])
        image_out = words(exit_["raw"])
        primary = 1 if entry.get("skater") and entry.get("skater") == entry.get("target") else 0
        lines.append(
            f"U {seq} {entry['fn']} {obj} {entry['ctr']} {entry['scale']} {entry['players']} {entry['limit']} "
            f"{entry['mode']} {entry.get('skater', 0)} {primary} {entry.get('skater_f64') or 0} {entry.get('skater_b8') or 0}"
        )
        lines.append("I " + " ".join(diff(previous.get(obj, [0] * WORDS), image_in)))
        previous[obj] = image_in
        lines.append("X " + " ".join(diff(image_in, image_out)))
        for record in group:
            if record["t"] == "call" and record["name"] not in INTERNAL:
                lines.append(f"K {record['name']} " + " ".join(str(v) for v in record["args"]))
            elif record["t"] == "ret" and record["name"] in ("ground_height", "goal_stat"):
                lines.append(f"G {record['name']} {s32(record['eax'])}")
        emitted += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{emitted} records -> {out_path} ({Path(out_path).stat().st_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: make_powerup_trace_fixture.py TRACE.ndjson LEVEL.TRG OUT.txt [MAX]")
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
