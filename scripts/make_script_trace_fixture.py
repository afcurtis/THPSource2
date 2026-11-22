#!/usr/bin/env python3
"""Turn a retail level-object script NDJSON trace into the compact text fixture
consumed by src/trg/object_script_retail_trace_test.cpp.

The trace comes from a GDB probe (build/p44b/script_trace.py during Phase 4.4b)
that logs, for every call of the subtype-0xcb object update (0x004031a0):

  entry / exit : the raw 0x200 bytes of the object before and after the call
  cmd / field  : every command word the script runner dispatches, with the
                 cursor value after the opcode word was read
  var          : every script variable read (id, result)
  call         : side-effect calls made directly by the script code
  ret          : return values of world queries (movie playing, goal type)

Fixture layout, one record per line:

  # comment
  B <runtime address of TRG byte 0>
  S <sha256 of the TRG file the trace ran against>
  U <seq> <object address> <frame delta> <sim time> <mode> <target 0|1> <tx> <ty> <tz> <moved 0|1>
                    moved = the probe teleported the skater after the object
                    loop measured its distance, so +0xb8 is stale for this update
  I <word:hex>*     entry image, as words that differ from this object's
                    previous entry image (the first is relative to zeros)
  X <word:hex>*     exit image, as words that differ from this update's entry
  C <opcode> <cursor after the opcode word>
  V <variable id> <result>
  K <name> <int>*   a direct side-effect call
  R <name> <int>    a world-query return value
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

WORDS = 0x200 // 4
# Code range of baddy.cpp plus the subtype-0xcb command handler. A call whose
# return address lies here was made by the script interpreter itself rather
# than by a service it invoked.
DIRECT_CALLER = (0x00401000, 0x00404400)
SERVICE_CALLS = {
    "sfx_play", "sfx_positional", "stop_sound", "send_pulse", "exec_list",
    "signal_links", "signal_node", "powerup_create", "flash", "anim_cycle",
    "anim_start", "camera_shake", "goal_event", "effect_spawn",
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
        if record["t"] != "meta":
            by_seq.setdefault(record["seq"], []).append(record)

    lines = [
        "# retail level-object script trace (Phase 4.4b); see scripts/make_script_trace_fixture.py",
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
        obj = entry["obj"]
        image_in = words(entry["raw"])
        image_out = words(exit_["raw"])
        target = entry["target_pos"] or [0, 0, 0]
        moved = 1 if any(r["t"] == "teleport" for r in group) else 0
        lines.append(
            f"U {seq} {obj} {entry['dt']} {entry['sim']} {entry['mode']} "
            f"{1 if entry['target_pos'] else 0} {target[0]} {target[1]} {target[2]} {moved}"
        )
        lines.append("I " + " ".join(diff(previous_entry.get(obj, [0] * WORDS), image_in)))
        previous_entry[obj] = image_in
        lines.append("X " + " ".join(diff(image_in, image_out)))
        for record in group:
            kind = record["t"]
            if kind in ("cmd", "field"):
                lines.append(f"C {record['op']} {record['cursor']}")
            elif kind == "var":
                lines.append(f"V {record['id']} {record['result']}")
            elif kind == "call" and record["name"] in SERVICE_CALLS:
                caller = record["caller"]
                if DIRECT_CALLER[0] <= caller < DIRECT_CALLER[1]:
                    values = list(record["args"])
                    if record["name"] in ("exec_list", "signal_links"):
                        values = record["list"] or []
                    elif record["name"] == "sfx_positional":
                        values = [values[0], *(record["pos"] or [0, 0, 0])]
                    lines.append(f"K {record['name']} " + " ".join(str(v) for v in values))
            elif kind == "ret" and record["name"] in RETURN_CALLS:
                if DIRECT_CALLER[0] <= record["caller"] < DIRECT_CALLER[1]:
                    lines.append(f"R {record['name']} {s32(record['eax'])}")
        emitted += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{emitted} updates -> {out_path} ({Path(out_path).stat().st_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: make_script_trace_fixture.py TRACE.ndjson LEVEL.TRG OUT.txt [MAX_UPDATES]")
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
