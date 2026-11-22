#!/usr/bin/env python3
"""Turn a retail ordering-table trace (Phase 4.6 probe build/p46/order_trace.py) into the compact
text fixture consumed by src/trg/render_order_retail_trace_test.cpp.

Fixture layout, one frame per block, in retail order:

  # comment
  D <base> <size> <start>        one D3D_DrawPolygonList call: ordering table base address, entry
                                 count (CurrentOTSize) and the address the caller passed
  L <polygon> <bucket> <next>    a polygon linked by M3D_ClipAndBucketPolygon since the table was
                                 cleared, in link order, with the word 0 it received
  N <node> <is polygon>          the chain D3D_DrawPolygonList walks, in visit order: every node
                                 address and whether its word 1 is non-zero (ordering-table entries
                                 have word 1 == 0 and draw nothing)
  Z                              end of the frame
"""

import json
import sys
from pathlib import Path


def main(trace_path, out_path):
    lines = ["# retail ordering table trace (Phase 4.6); see scripts/make_render_order_fixture.py"]
    frames = 0
    for line in open(trace_path):
        row = json.loads(line)
        if row["t"] != "draw":
            continue
        frames += 1
        lines.append(f"D {row['base']:x} {row['size']} {row['start']:x}")
        for poly, bucket, nxt in row["links"]:
            lines.append(f"L {poly:x} {bucket:x} {nxt:x}")
        for node, _link, is_poly in row["nodes"]:
            lines.append(f"N {node:x} {is_poly}")
        lines.append("Z")
    Path(out_path).write_text("\n".join(lines) + "\n")
    print(out_path, {"frames": frames})


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_render_order_fixture.py TRACE.ndjson OUT.txt")
    main(*sys.argv[1:])
