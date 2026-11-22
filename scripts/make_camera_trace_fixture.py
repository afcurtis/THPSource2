#!/usr/bin/env python3
"""Turn a retail Camera_Update NDJSON trace into the compact text fixture
consumed by src/camera/camera_retail_trace_test.cpp.

Input is produced by a GDB probe that logs, for every Camera_Update call, the
raw 0x674-byte CCamera before and after the call, the tripod inputs, control
globals, and the return values of the two producers whose results cross the
value-level boundary (0x004f53b0 at 0x0040e5c9 and 0x004ca8f0 at 0x00410872).

Fixture layout (whitespace separated integers, one update per line):
  seq  nE (word_index value)*  nX (word_index value)*  input...
The first line holds only the full entry image as `nE (index value)*`.
Entries after the first are stored as words differing from the previous exit,
exits as words differing from the same update's entry.
"""

import json
import struct
import sys

CAMERA_WORDS = 0x674 // 4
HISTORY_RET = 0x0040E5C9
DIRECTION_RET = 0x00410872


def words(hex_bytes):
    return list(struct.unpack(f"<{CAMERA_WORDS}I", bytes.fromhex(hex_bytes)))


def diff(before, after):
    return [(i, after[i]) for i in range(CAMERA_WORDS) if before[i] != after[i]]


def s32(value):
    return value - (1 << 32) if value & 0x80000000 else value


def flat(pairs):
    out = [str(len(pairs))]
    for index, value in pairs:
        out += [str(index), str(value)]
    return out


def main(trace_path, out_path):
    records = [json.loads(line) for line in open(trace_path)]
    entries = {r["seq"]: r for r in records if r["t"] == "entry"}
    exits = {r["seq"]: r for r in records if r["t"] == "exit"}
    producers = {}
    for r in records:
        if r["t"] == "producer":
            producers.setdefault(r["seq"], []).append(r)

    lines = []
    seqs = sorted(entries)
    first = words(entries[seqs[0]]["camera"])
    lines.append(" ".join(flat([(i, v) for i, v in enumerate(first) if v])))
    previous_exit = first
    for seq in seqs:
        entry, exit_ = entries[seq], exits[seq]
        rec = entry["rec"]
        if rec["secondary_target"] != rec["tripod"]:
            raise SystemExit(f"seq {seq}: secondary target differs from tripod")
        cam_in = words(entry["camera"])
        cam_out = words(exit_["camera"])
        history = [p for p in producers.get(seq, []) if p["ret"] == HISTORY_RET]
        direction = [p for p in producers.get(seq, []) if p["ret"] == DIRECTION_RET]
        g = entry["globals"]
        vc = rec["viewport_control_before"]
        facing = [s32(v) for v in struct.unpack("<3I", bytes.fromhex(entry["tripod"]["30f4"]))]
        inputs = [
            *rec["tripod_position"]["s32"],
            *rec["tripod_follow_offset"]["s32"],
            *rec["tripod_distance_sample_offset"]["s32"],
            rec["tripod_physics_state"],
            rec["tripod_behavior_flag"],
            rec["tripod_effect_gate"],
            rec["tripod_effect_transform_gate"],
            1 if history else 0,
            s32(history[0]["eax"] & 0xFFFFFFFF) if history else 0,
            s32(direction[0]["eax"] & 0xFFFFFFFF) if direction else 0,
            exit_["globals"]["distance_bias"],
            g["delta_q8"],
            g["shake_phase_mult"],
            g["vertical_effect"],
            g["shared_angle"],
            vc["global_override"],
            vc["restore_pending_raw"],
            vc["global_parameter_raw"],
            vc["decrement"],
            vc["increment"],
            vc["reset"],
            vc["restore_axes"],
            vc["rotation_decrement"],
            vc["rotation_increment"],
            vc["direction_input_raw"],
            vc["x_decrement"], vc["x_increment"],
            vc["y_decrement"], vc["y_increment"],
            vc["z_decrement"], vc["z_increment"],
            rec["tripod_unknown_state"],
            exit_["globals"]["vertical_effect"],
            exit_["globals"]["shared_angle"],
            *facing,
            1 if entry["tripod_addr"] in (g["player_a"], g["player_b"]) else 0,
            g["camera_override"],
            g["framing_x"], g["framing_y"], g["framing_z"],
            exit_["globals"]["effect_seed_x"], exit_["globals"]["effect_seed_z"],
            g["retarget_block"], g["refresh_block"],
            *struct.unpack("<9h", bytes.fromhex(entry["tripod"]["2e58"])),
        ]
        line = [str(seq)]
        line += flat(diff(previous_exit, cam_in))
        line += flat(diff(cam_in, cam_out))
        line += [str(int(v)) for v in inputs]
        lines.append(" ".join(line))
        previous_exit = cam_out
    with open(out_path, "w", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    print(f"{len(seqs)} updates -> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
