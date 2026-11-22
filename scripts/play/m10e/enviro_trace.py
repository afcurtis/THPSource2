"""Milestone 10e probe: Spool_FindEnviroItem 0x004b1ef0 on retail memory.

Same record schema as scripts/play/m10e/list_trace.py (the call record carries the function in "index" and the key argument in "mode"; scripts/make_trg_interpreter_fixture.py turns it into a fixture). The entry state:
the list head (0x0056db28), every object of the list (the bytes +0x18..+0x28: the item index word at +0x1a, the region byte at +0x1f, the next pointer at +0x20), the region table cells (0x0056d434 +
0x44 * region) and the key array each points at. Only the first M10_MAX_CALLS calls are recorded.

Env: M10_OUT (ndjson), M10_MAX (calls to log), M10_STOP_FRAMES (frame hook hits).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/enviro.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "400"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

ENTRY = 0x004B1EF0
LIST_HEAD = 0x0056DB28
TABLE = 0x0056D434
STRIDE = 0x44
KEY_ARRAY_BYTES = 0x1000
STATE = {"seq": 0, "logged": 0, "frames": 0}
RET = {}
PENDING = {}


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


def read_halving(addr, size):
    data = read(addr, size)
    while data is None and size > 4:
        size //= 2
        data = read(addr, size)
    return data


def entry_state():
    regions = {}
    regions[LIST_HEAD] = read(LIST_HEAD, 4)
    seen_tables = set()
    obj = u32(LIST_HEAD)
    guard = 0
    while obj and guard < 20000:
        guard += 1
        data = read(obj + 0x18, 0x10)
        if data is None:
            break
        regions[obj + 0x18] = data
        region = data[0x1F - 0x18]
        if region not in seen_tables:
            seen_tables.add(region)
            cell = TABLE + STRIDE * region
            regions[cell] = read(cell, 4)
            array = u32(cell)
            if array:
                keys = read_halving(array, KEY_ARRAY_BYTES)
                if keys is not None:
                    regions[array] = keys
        obj = int.from_bytes(data[0x20 - 0x18:0x24 - 0x18], "little")
    return {f"{a:08x}": d.hex() for a, d in sorted(regions.items()) if d is not None}


class RetSite(TonyBreakpoint):
    def on_hit(self, ctx):
        stack = PENDING.get(self.address)
        if stack:
            stack.pop()(ctx)


class Entry(TonyBreakpoint):
    def __init__(self, address, handler, **kw):
        super().__init__(address, **kw)
        self.handler = handler

    def on_hit(self, ctx):
        self.handler(ctx, self.address)


def arm(address, fn):
    if address not in RET:
        RET[address] = RetSite(address)
    PENDING.setdefault(address, []).append(fn)


def h_entry(ctx, _addr):
    STATE["seq"] += 1
    if STATE["logged"] >= MAX_CALLS:
        return
    key = ctx.arg(0) & 0xFFFFFFFF
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": 0, "index": ENTRY, "mode": key,
        "caller": ctx.return_address(), "stream_bytes": "", "entry": entry_state(), "events": [],
    }

    def done(c):
        rec["events"].append({"e": "x", "w": [], "eax": c.register("eax") & 0xFFFFFFFF})
        STATE["logged"] += 1
        emit(rec)
        if STATE["logged"] >= MAX_CALLS:
            emit({"t": "stop", "calls": STATE["seq"]})
            gdb.execute("quit")

    arm(ctx.return_address(), done)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"]})
        gdb.execute("quit")


Entry(ENTRY, h_entry)
Entry(0x00480FF0, h_frame)
gdb.write("enviro probe armed\n")
