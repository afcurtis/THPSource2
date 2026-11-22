#!/usr/bin/env python3
"""Turn a retail platform (subtype 0x0192) trace NDJSON into the compact text
fixture consumed by src/trg/platform_object_retail_trace_test.cpp.

The trace comes from a GDB probe (build/p44c/platform_trace.py during Phase
4.4c) that logs, for every call of the platform update (0x0049fef0) and of
CPlatform__Bounce (0x0049f4c0):

  entry / exit : the raw 0x218 bytes of the object before and after the call
  cmd / field  : every command word the script runner dispatches, with the
                 cursor after the opcode word was read
  var          : every script variable read (id, result)
  call         : side-effect calls (sounds, shatter, item flash, messages)
  ret          : return values of world queries and every Rnd draw
  query        : each collision query the bounce simulation issued (line and
                 the hit body flag, position and normal it got back)

Fixture layout, one record per line:

  # comment
  B <runtime address of TRG byte 0>
  S <sha256 of the TRG file the trace ran against>
  U <seq> <update|bounce> <object address> <sim time> <tick counter>
    <world vector x y z> <skater context> <skater velocity x y z>
  I <word:hex>*     entry image, as words that differ from this object's
                    previous entry image (the first is relative to zeros)
  X <word:hex>*     exit image, as words that differ from this update's entry
  C <opcode> <cursor after the opcode word>
  V <variable id> <result>
  K <name> <int>*   a direct side-effect call
  R <name> <int>    a world-query return value
  N <int>           the next Rnd result
  Q <sx sy sz ex ey ez> <hit> <px py pz> <nx ny nz>
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

SIZE = 0x218
WORDS = SIZE // 4
# Code range of baddy.cpp plus the platform virtuals: a call whose return
# address lies here was made by script or platform code itself rather than by a
# service it invoked.
DIRECT_CALLER = (0x00401000, 0x004A1600)
SERVICE_CALLS = {
    "sfx_play", "sfx_positional", "stop_sound", "send_pulse", "exec_list",
    "signal_links", "signal_node", "powerup_create", "flash", "anim_cycle",
    "anim_start", "camera_shake", "goal_event", "effect_spawn", "shatter",
    "item_flash", "sfx_modify_pos", "grunt", "vibrate",
}
RETURN_CALLS = {"got_goal_type", "video_playing"}


def words(hex_bytes):
    return list(struct.unpack(f"<{WORDS}I", bytes.fromhex(hex_bytes)))


def diff(before, after):
    return [f"{i}:{after[i]:x}" for i in range(WORDS) if before[i] != after[i]]


def s32(value):
    return value - (1 << 32) if value & 0x80000000 else value


def main(trace_path, trg_path, out_path, limit=0):
    records = [json.loads(line) for line in open(trace_path)]
    trg = Path(trg_path).read_bytes()
    meta = next(r for r in records if r["t"] == "meta")
    node0_offset = struct.unpack_from("<I", trg, 12)[0]
    base = meta["node0"] - node0_offset

    by_seq = {}
    for record in records:
        if record["t"] not in ("meta",):
            by_seq.setdefault(record["seq"], []).append(record)

    lines = [
        "# retail platform object trace (Phase 4.4c); see scripts/make_platform_trace_fixture.py",
        f"B {base}",
        f"S {hashlib.sha256(trg).hexdigest()}",
    ]
    previous_entry = {}
    emitted = 0
    for seq in sorted(by_seq):
        group = by_seq[seq]
        entry = next((r for r in group if r["t"] == "entry"), None)
        exit_ = next((r for r in group if r["t"] == "exit"), None)
        if entry is None or exit_ is None:
            continue  # the trace was cut mid-update
        if limit and emitted >= limit:
            break
        kind = entry.get("fn", "update")
        obj = entry["obj"]
        image_in = words(entry["raw"])
        image_out = words(exit_["raw"])
        vec = entry["vec"] or [0, 0, 0]
        svel = entry.get("skater_vel") or [0, 0, 0]
        lines.append(
            f"U {seq} {kind} {obj} {entry['sim']} {entry['ctr']} {vec[0]} {vec[1]} {vec[2]} "
            f"{entry.get('skater', 0)} {svel[0]} {svel[1]} {svel[2]}"
        )
        lines.append("I " + " ".join(diff(previous_entry.get(obj, [0] * WORDS), image_in)))
        previous_entry[obj] = image_in
        lines.append("X " + " ".join(diff(image_in, image_out)))
        for record in group:
            t = record["t"]
            if t in ("cmd", "field"):
                lines.append(f"C {record['op']} {record['cursor']}")
            elif t == "var":
                lines.append(f"V {record['id']} {record['result']}")
            elif t == "query":
                lines.append(
                    "Q " + " ".join(str(v) for v in record["start"] + record["end"])
                    + f" {record['hit']} " + " ".join(str(v) for v in record["pos"])
                    + " " + " ".join(str(v) for v in record["normal"])
                )
            elif t == "call" and record["name"] in SERVICE_CALLS:
                caller = record["caller"]
                if DIRECT_CALLER[0] <= caller < DIRECT_CALLER[1]:
                    values = list(record["args"])
                    if record["name"] in ("exec_list", "signal_links"):
                        values = record["list"] or []
                    elif record["name"] in ("sfx_positional", "sfx_modify_pos"):
                        values = [values[0], *(record["pos"] or [0, 0, 0])]
                    lines.append(f"K {record['name']} " + " ".join(str(v) for v in values))
            elif t == "ret":
                if record["name"] == "rnd":
                    if DIRECT_CALLER[0] <= record["caller"] < DIRECT_CALLER[1]:
                        lines.append(f"N {s32(record['eax'])}")
                elif record["name"] in RETURN_CALLS and DIRECT_CALLER[0] <= record["caller"] < DIRECT_CALLER[1]:
                    lines.append(f"R {record['name']} {s32(record['eax'])}")
        emitted += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{emitted} updates -> {out_path} ({Path(out_path).stat().st_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: make_platform_trace_fixture.py TRACE.ndjson LEVEL.TRG OUT.txt [MAX_UPDATES]")
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
