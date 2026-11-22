#!/usr/bin/env python3
"""Turn retail trick-tip traces (Phase 4.7b probe build/p47b/tips_trace.py: the monitor 0x004c4620,
the tip selector and TrickTips_Display 0x004c4420) into the compact text fixture consumed by
src/runtime/trick_tips_retail_trace_test.cpp.

Usage: make_trick_tips_fixture.py TRACE.ndjson [TRACE2.ndjson ...] OUT.txt [--idle-every N]

The 0x114-byte monitor object is written as the words that differ from the previous image in the
file ("index:hex"). One record per line:

  # comment
  R <diff> | <diff>                   FUN_004c4570 (level start): image before | after
  M <call> <gate_a> <video> <replay> <mode> <state> <b098> <b204> <enabled> <level> <player> <lm0> <lm1>
  S <22 tip state dwords>             (inputs of the monitor call that follows the M line)
  K <9 skater counters>
  I <diff> <m0> <m1> <m2> <m3>        monitor object and the four tip masks at entry
  O <diff> <m0> <m1> <m2> <m3>        the same at exit (diff against the entry image)
  D <diff> | <diff> | <calls>         TrickTips_Display: image before | after | Mess calls
                                      (name:a0:a1:a2:a3:return ...)
"""

import json
import struct
import sys
from pathlib import Path

WORDS = 0x114 // 4
COUNTERS = ("0x302c", "0x3030", "0x3038", "0x303c", "0x3040", "0x3048", "0x3050", "0x3054", "0x3058")


def words(hex_image):
    return struct.unpack(f"<{WORDS}I", bytes.fromhex(hex_image))


def diff(before, after):
    return " ".join(f"{i}:{after[i]:x}" for i in range(WORDS) if before is None or before[i] != after[i])


# Stack words each Mess_* call really takes; the probe records four, the rest is scratch.
ARG_COUNTS = {"set_text_justify": 1, "set_scale": 1, "get_scale": 2, "set_rgb": 4, "draw_panel_text": 4}


def trimmed(call):
    count = ARG_COUNTS[call["name"]]
    return [*call["args"][:count], *[0] * (4 - count)]


def masks_of(hex_masks):
    return " ".join(f"{w:x}" for w in struct.unpack("<4I", bytes.fromhex(hex_masks)))


def main(argv):
    idle_every = 0
    if "--idle-every" in argv:
        at = argv.index("--idle-every")
        idle_every = int(argv[at + 1])
        del argv[at:at + 2]
    *trace_paths, out_path = argv
    events = []
    for path in trace_paths:
        for line in Path(path).read_text().splitlines():
            if not line.strip():
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                break  # a capture stopped by its timeout can end in a torn line

    lines = ["# Retail trick-tip trace (Phase 4.7b): monitor, selector and display."]
    prev = [None]

    def emit_image(hex_image):
        image = words(hex_image)
        text = diff(prev[0], image)
        prev[0] = image
        return text, image

    kept = dropped = displays = 0
    for e in events:
        if e["t"] == "reset":
            before = e.get("obj_in")
            if before is None:
                continue
            text_in, image_in = emit_image(before)
            after = words(e["obj_out"])
            lines.append(f"R {text_in} | {diff(image_in, after)}")
            prev[0] = after
        elif e["t"] == "monitor":
            before, after = words(e["obj_in"]), words(e["obj_out"])
            moved = [i for i in range(WORDS) if before[i] != after[i] and i not in (0x58 // 4, 0x60 // 4)]
            quiet = (not moved and e["masks_in"] == e["masks_out"] and not e["selects"])
            if quiet and idle_every and e["call"] % idle_every != 0:
                dropped += 1
                continue
            lm = e["level_masks"] or "0000000000000000"
            lm0, lm1 = struct.unpack("<2I", bytes.fromhex(lm))
            lines.append(
                "M {call} {a} {v} {r} {mode} {state} {b98} {b204} {en} {lvl} {player} {lm0:x} {lm1:x}".format(
                    call=e["call"], a=int(e["seam"].get("gate_a", 0)), v=int(e["seam"].get("video_playing", 0)),
                    r=e["replay_restart"] & 0xFF, mode=e["mode"], state=e["level_state"],
                    b98=e["flag_b098"] & 0xFF, b204=e["flag_b204"] & 0xFF, en=e["enabled"], lvl=e["level"],
                    player=e["player"], lm0=lm0, lm1=lm1))
            lines.append("S " + " ".join(str(x) for x in e["tips"]))
            lines.append("K " + " ".join(str(e["counters"][c]) for c in COUNTERS))
            text_in, image_in = emit_image(e["obj_in"])
            lines.append(f"I {text_in} {masks_of(e['masks_in'])}")
            lines.append(f"O {diff(image_in, after)} {masks_of(e['masks_out'])}")
            prev[0] = after
            kept += 1
        elif e["t"] == "display":
            if not e["mess"]:
                continue
            text_in, image_in = emit_image(e["obj_in"])
            after = words(e["obj_out"])
            calls = " ".join(
                ":".join([m["name"], *[str(a) for a in trimmed(m)], str(m.get("eax", 0))]) for m in e["mess"])
            lines.append(f"D {text_in} | {diff(image_in, after)} | {calls}")
            prev[0] = after
            displays += 1
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"wrote {out_path}: {kept} monitor calls kept ({dropped} quiet ones dropped), {displays} displays")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
