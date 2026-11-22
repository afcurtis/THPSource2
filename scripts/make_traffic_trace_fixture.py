#!/usr/bin/env python3
"""Turn a retail traffic-car trace NDJSON (Phase 4.4c probe build/p44c/traffic_trace.py,
TRG_UpdateTrafficObject 0x00412960) into the compact text fixture consumed by
src/trg/traffic_object_retail_trace_test.cpp.

Fixture layout, one record per line:

  # comment
  S <sha256 of the TRG file the trace ran against>
  U <seq> <object> <AnimationClock> <AnimationTimeScale> <player count> <px py pz> <rng a b c>
  I <word:hex>*   entry image (0x1e8 bytes) as words differing from this car's previous entry
  X <word:hex>*   exit image as words differing from the entry
  K <name> <int>* a side-effect call in order: sfx_modify_pos, sfx_play, interaction,
                  proximity, save_position
  R <name> <int>  a world-query result (video_playing)
  N <int>         the next Rnd result
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

SIZE = 0x1E8
WORDS = SIZE // 4
# TRG_CheckTrafficProximity (0x004136e0): the only caller of sounds and Rnd that
# the native car models itself; the interaction helpers (0x00412ef0..0x004136e0)
# are a service.
DIRECT = (0x004136E0, 0x004138E0)


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
        if record["t"] != "meta":
            by_seq.setdefault(record["seq"], []).append(record)
    lines = [
        "# retail traffic car trace (Phase 4.4c); see scripts/make_traffic_trace_fixture.py",
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
        pos = entry["target_pos"] or [0, 0, 0]
        rng = entry["rng"]
        lines.append(
            f"U {seq} {obj} {entry['clock']} {entry['scale']} {entry['players']} "
            f"{pos[0]} {pos[1]} {pos[2]} {rng[0]} {rng[1]} {rng[2]}"
        )
        lines.append("I " + " ".join(diff(previous.get(obj, [0] * WORDS), image_in)))
        previous[obj] = image_in
        lines.append("X " + " ".join(diff(image_in, image_out)))
        for record in group:
            t = record["t"]
            if t == "call":
                if record["name"] == "sfx_modify_pos":
                    values = [record["args"][0], *(record["pos"] or [0, 0, 0])]
                elif record["name"] == "sfx_play":
                    if not DIRECT[0] <= record["caller"] < DIRECT[1]:
                        continue
                    values = record["args"][:1]
                else:
                    values = record["args"]
                lines.append(f"K {record['name']} " + " ".join(str(v) for v in values))
            elif t == "ret":
                if record["name"] == "rnd":
                    if DIRECT[0] <= record["caller"] < DIRECT[1]:
                        lines.append(f"N {s32(record['eax'])}")
                else:
                    lines.append(f"R {record['name']} {s32(record['eax'])}")
        emitted += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{emitted} updates -> {out_path} ({Path(out_path).stat().st_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: make_traffic_trace_fixture.py TRACE.ndjson LEVEL.TRG OUT.txt [MAX_UPDATES]")
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
