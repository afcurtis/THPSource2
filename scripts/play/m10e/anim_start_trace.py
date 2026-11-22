"""Milestone 10e probe: Animation_Start 0x00480730 (thiscall; animation, from, to, end mode) on retail memory.

Same record schema as scripts/play/m10e/ob_ai_trace.py (c / r / w / x events). The call record carries the object (ecx) in "stream", 0x00480730 in "index" and the four stack arguments as dwords in
"stream_bytes". The entry state is the assertion words, the object's bytes 0x1f and 0xf4..0x120, the region's cell of AnimationTablesByRegion (0x44 bytes) and the first 0x400 bytes of the
animation table it points at. The only callee is the assertion M3dUtils_InBetween.

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (Ob_AI entries).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/anim_start.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "150"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

START = 0x00480730
ASSERT = 0x004011E0
BODY = (0x00480730, 0x00480884)
TABLES = 0x0056D444


def emit(rec):
    OUT.write(json.dumps(rec, sort_keys=True) + "\n")
    OUT.flush()


def read(addr, size):
    try:
        return mem.bytes(addr, size)
    except Exception:
        return None


def u32(addr):
    try:
        return mem.u32(addr)
    except Exception:
        return 0


class Tracker:
    def __init__(self):
        self.regions = {}
        self.pending = []

    def add(self, addr, size):
        if addr:
            self.pending.append((addr, size))

    def finalize(self):
        merged = []
        for addr, size in sorted(self.pending, key=lambda p: (p[0], -p[1])):
            if merged and addr < merged[-1][0] + merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], addr + size - merged[-1][0])
            else:
                merged.append([addr, size])
        self.pending = []
        for addr, size in merged:
            data = read(addr, size)
            while data is None and size > 4:
                size //= 2
                data = read(addr, size)
            if data is not None:
                self.regions[addr] = data

    def diff(self):
        changes = []
        for addr, old in list(self.regions.items()):
            new = read(addr, len(old))
            if new is None or new == old:
                continue
            for off in range(0, len(old) - len(old) % 4, 4):
                if old[off:off + 4] != new[off:off + 4]:
                    changes.append([addr + off, old[off:off + 4].hex(), new[off:off + 4].hex()])
            for off in range(len(old) - len(old) % 4, len(old)):
                if old[off:off + 1] != new[off:off + 1]:
                    changes.append([addr + off, old[off:off + 1].hex(), new[off:off + 1].hex()])
            self.regions[addr] = new
        return changes

    def dump(self):
        return {f"{a:08x}": d.hex() for a, d in sorted(self.regions.items())}


FRAME = []
STATE = {"seq": 0, "logged": 0, "frames": 0}
RET = {}
PENDING = {}


def arm(address, fn):
    if address not in RET:
        RET[address] = RetSite(address)
    PENDING.setdefault(address, []).append(fn)


class RetSite(TonyBreakpoint):
    def on_hit(self, ctx):
        stack = PENDING.get(self.address)
        while stack:
            stack.pop()(ctx)


class Entry(TonyBreakpoint):
    def __init__(self, address, handler, **kw):
        super().__init__(address, **kw)
        self.handler = handler

    def on_hit(self, ctx):
        self.handler(ctx, self.address)


def open_frame(ctx, _addr):
    STATE["seq"] += 1
    if FRAME or STATE["logged"] >= MAX_CALLS:
        return
    obj = ctx.register("ecx") & 0xFFFFFFFF
    args = [ctx.arg(i) & 0xFFFFFFFF for i in range(4)]
    track = Tracker()
    track.add(0x00568638, 4)
    track.add(0x0056881C, 0x40)
    track.add(obj + 0x1F, 1)
    track.add(obj + 0xF4, 0x2C)
    region_byte = read(obj + 0x1F, 1)
    region = region_byte[0] if region_byte else 0
    cell = TABLES + region * 0x44
    track.add(cell, 0x44)
    table = u32(cell)
    track.add(table, 0x10 + 8 * min(max(args[0], 0x2F), 0x2000))
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": obj, "index": START, "mode": 0,
        "caller": ctx.return_address(), "stream_bytes": b"".join(a.to_bytes(4, "little") for a in args).hex(),
        "entry": track.dump(), "events": [],
    }
    FRAME.append((rec, track))
    ASSERT_BP.enabled = True

    def done(c):
        top_rec, top_track = FRAME.pop()
        top_rec["events"].append({"e": "x", "w": top_track.diff(), "eax": c.register("eax") & 0xFFFFFFFF})
        ASSERT_BP.enabled = False
        STATE["logged"] += 1
        emit(top_rec)
        if STATE["logged"] >= MAX_CALLS:
            emit({"t": "stop", "calls": STATE["seq"], "frames": STATE["frames"]})
            gdb.execute("quit")

    arm(ctx.return_address(), done)


def h_assert(ctx, address):
    if not FRAME:
        return
    ret_to = ctx.return_address()
    if not BODY[0] <= ret_to < BODY[1]:
        return
    rec, track = FRAME[-1]
    own = track.diff()
    if own:
        rec["events"].append({"e": "w", "w": own})
    rec["events"].append({
        "e": "c", "a": address, "site": ret_to, "ecx": ctx.register("ecx") & 0xFFFFFFFF,
        "edx": ctx.register("edx") & 0xFFFFFFFF, "args": [ctx.arg(i) & 0xFFFFFFFF for i in range(6)],
    })

    def back(c):
        rec["events"].append({"e": "r", "a": address, "eax": c.register("eax") & 0xFFFFFFFF, "edx": c.register("edx") & 0xFFFFFFFF})

    arm(ret_to, back)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"]})
        gdb.execute("quit")


Entry(START, open_frame)
ASSERT_BP = Entry(ASSERT, h_assert)
ASSERT_BP.enabled = False
Entry(0x00480FF0, h_frame)
gdb.write("anim_start probe armed\n")
