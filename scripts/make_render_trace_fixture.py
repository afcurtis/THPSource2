#!/usr/bin/env python3
"""Turn a retail model-submission trace NDJSON (Phase 4.6 probe build/p46/render_trace.py:
M3D_SubmitModelState 0x004d14d0 and the polygon functions under it) into the compact text
fixture consumed by src/trg/render_model_retail_trace_test.cpp.

Fixture layout, one record per line, in retail order:

  # comment
  K <name> <hex>                 a .rdata constant the chain reads (checked against the native
                                 constant table)
  S <n> <flags> <cursor> <end> <vertex count> <normal count> <face count>
                                 start of one M3D_SubmitModelState call: the flags argument, the
                                 polygon arena cursor and exclusive end at entry
  L <level> <soft> <texmode> <pushback>
                                 CurrentLevel, D3DSoftwareRenderer, D3DTextureMode (bits) and the
                                 modelPushbacks entry for the model
  P <packet hex>                 the model packet bytes
  V/A/B/Q/H/M/W <hex>           raw state: viewport record, blocks at 0x563a18 (0x118 bytes),
                                 0x564340 (0x50), 0x562098 (0x18), 0x59d328 (0x18), the matrix
                                 object behind DAT_005620e0 (0x90) and 0x57e878 (0xc)
  F <hex>                        FogYonScale (0x549d98) at entry: a level setting, not a constant
  G <hex|->                      secondary heap (0x58f328 up to DAT_0059d330) at entry, the
                                 records earlier submits left for source-flag-2 vertices
  e build <cursor> <selector> <grey> <colors> <material> <tex|-> <pre1|-> <pre2|-> <lights> <ramp>
                                 M3D_BuildD3DPolygon entry: the four resolved corner colors
                                 (expected output of M3D_ResolveFaceColor), the face material and
                                 its 0x20-byte runtime texture record, the prior content of the
                                 next two arena slots (only when the submit near-clips: the
                                 unwritten color byte of an interpolated vertex is stale arena
                                 data) and the light-array / ramp entries ResolveFaceColor could
                                 read for the face as index=value lists
  e build_end <cursor>
  e clip_bucket <poly> <cursor> <selector> <clip flags> <words>:<hex>
  e clip_bucket_end <poly> <cursor> <words>:<hex>
  e near_clip <poly>
  e near_clip_end <poly> <ret> <words>:<hex>
  e classify <poly> <param3 bits> <param4 bits> <selector>
  e classify_end <poly> <bucket> <previous head> <words>:<hex>
  e video <result>
  X <cursor after> <vertex heap hex> <secondary heap hex|->
                                 exit state: arena cursor, the transformed records and the
                                 secondary heap bytes
  Z                              end of the call

Images are trimmed to the used prefix of the 0xc0-byte arena slot (6 words plus the vertex
stream). The generator also cross-checks the capture: every polygon's classify_end bucket head
must equal the previous link in the same bucket when one was seen in the same call.
"""

import json
import struct
import sys
from pathlib import Path

CONST_NAMES = ["f518484", "f518910", "f518d70", "f51993c", "f549d50", "f549d98"]


def image_prefix(hexstr):
    data = bytes.fromhex(hexstr)
    words = struct.unpack("<48I", data)
    count = words[5]
    if not 0 <= count <= 6:
        count = 6
    stride = 7 if words[4] else 5
    used = 6 + count * stride
    return f"{used}:" + struct.pack(f"<{used}I", *words[:used]).hex()


def main(trace_path, out_path, limit=None):
    rows = [json.loads(line) for line in open(trace_path)]
    lines = ["# retail model submission trace (Phase 4.6); see scripts/make_render_trace_fixture.py"]
    counts = {"submits": 0, "polygons": 0, "near": 0, "events": 0}
    for row in rows:
        if row["t"] == "const":
            for name in CONST_NAMES:
                lines.append(f"K {name} {row[name]}")
            continue
        if row["t"] != "submit":
            continue
        if limit and counts["submits"] >= limit:
            break
        counts["submits"] += 1
        glob = row["globals"]
        lines.append(
            f"S {row['n']} {row['flags']:x} {row['cursor']:x} {row['end']:x} "
            f"{row['vcount']} {row['ncount']} {row['fcount']}")
        lines.append(f"L {glob['level']} {glob['soft']} {glob['texmode']:x} {row['pushback']}")
        lines.append(f"P {row['packet']}")
        lines.append(f"V {row['viewport_obj']}")
        lines.append(f"A {row['blocks']['viewport']}")
        lines.append(f"B {row['blocks']['b564']}")
        lines.append(f"Q {row['blocks']['b5620']}")
        lines.append(f"H {row['blocks']['b59d']}")
        lines.append(f"M {row['matrix_obj']}")
        lines.append(f"W {row['b57e878']}")
        lines.append(f"G {row.get('heap2_in') or '-'}")
        lines.append(f"F {row.get('fog') or '-'}")
        events = row["events"]
        near = any(e["e"] == "near_clip" for e in events)
        counts["near"] += near
        heads = {}
        for ev in events:
            kind = ev["e"]
            counts["events"] += 1
            if kind == "build":
                colors = bytes.fromhex(ev["colors"]).hex()
                tex = ev["tex"] if ev["tex"] else "-"
                pre1 = ev["pre1"] if near and ev["pre1"] else "-"
                pre2 = ev["pre2"] if near and ev["pre2"] else "-"
                res = ev.get("res") or {"lights": {}, "ramp": {}}
                lights = ",".join(f"{i}={v:x}" for i, v in res["lights"].items()) or "-"
                ramp = ",".join(f"{i}={v:x}" for i, v in res["ramp"].items()) or "-"
                lines.append(
                    f"e build {ev['cursor']:x} {ev['sel']:x} {ev['d59d32c']:x} {colors} "
                    f"{ev['material']:x} {tex} {pre1} {pre2} {lights} {ramp}")
                counts["polygons"] += 1
            elif kind == "build_end":
                lines.append(f"e build_end {ev['cursor']:x}")
            elif kind == "clip_bucket":
                lines.append(
                    f"e clip_bucket {ev['poly']:x} {ev['cursor']:x} {ev['sel']:x} {ev['p3']:x} "
                    f"{image_prefix(ev['img'])}")
            elif kind == "clip_bucket_end":
                lines.append(f"e clip_bucket_end {ev['poly']:x} {ev['cursor']:x} {image_prefix(ev['img'])}")
            elif kind == "near_clip":
                lines.append(f"e near_clip {ev['poly']:x}")
            elif kind == "near_clip_end":
                lines.append(f"e near_clip_end {ev['poly']:x} {ev['ret']:x} {image_prefix(ev['img'])}")
            elif kind == "classify":
                lines.append(
                    f"e classify {ev['poly']:x} {ev['p3']:x} {ev['p4']:x} {ev['sel']:x}")
            elif kind == "classify_end":
                bucket, head = ev["bucket"], ev["head"]
                if bucket in heads:
                    assert heads[bucket] == head, (row["n"], bucket, hex(heads[bucket]), hex(head))
                heads[bucket] = ev["poly"]
                lines.append(
                    f"e classify_end {ev['poly']:x} {bucket:x} {head:x} {image_prefix(ev['img'])}")
            elif kind == "video":
                lines.append(f"e video {ev['ret']:x}")
            else:
                raise SystemExit(f"unknown event {kind}")
        lines.append(f"X {row['cursor_out']:x} {row['heap'] or '-'} {row['heap2'] or '-'}")
        lines.append("Z")
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, counts)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: make_render_trace_fixture.py TRACE.ndjson OUT.txt [MAX_SUBMITS]")
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) == 4 else None)
