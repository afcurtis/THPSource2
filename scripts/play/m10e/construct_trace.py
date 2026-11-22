"""Milestone 10e probe: TRG_ConstructSubtype0x0192 0x0049f250 on retail memory.

Same record schema as build/m10e/create_trace.py (c / r / w / cw / x events; scripts/make_trg_interpreter_fixture.py turns it into a fixture). The call record carries the storage
(ecx) in "stream", the payload end (first argument) in "index" and the node (second argument) in "mode". Tracked at entry: the whole storage (0x218 bytes), the three level globals the
constructor reads, the head of the game object list and the object behind it (ObjectList_PushHead writes its back link). Callees: Baddy_Construct, Object_SelectNamedPsxRegion,
ObjectList_PushHead, TRG_ReadFixedPosition, TRG_ReadObjectParameters (each logged with its registers and arguments, what it wrote into the tracked regions is the k line).

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (frame hook hits).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/construct.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "400"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

ENTRY = 0x0049F250
BODY = (0x0049F250, 0x0049F33B)
STORAGE = 0x218
NEIGHBOUR = 0x70

CALLEES = {
    0x401270: "Baddy_Construct", 0x47FE30: "Object_SelectNamedPsxRegion", 0x4801D0: "ObjectList_PushHead",
    0x480240: "TRG_ReadFixedPosition", 0x4802C0: "TRG_ReadObjectParameters",
}

STATIC = {
    "line_tag": (0x00568638, 0x04),
    "file_tag": (0x0056881C, 0x40),
    "d56a954": (0x0056A954, 0x04),
    "d56e218": (0x0056E218, 0x04),
    "game_head": (0x0056AF40, 0x04),
}


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
        for addr, size in sorted(self.pending):
            if merged and addr <= merged[-1][0] + merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], addr + size - merged[-1][0])
            else:
                merged.append([addr, size])
        self.pending = []
        for addr, size in merged:
            if addr in self.regions:
                continue
            data = read(addr, size)
            while data is None and size > 4:
                size //= 2
                data = read(addr, size)
            if data is not None:
                self.regions[addr] = data

    def resolve(self, storage):
        for _, (addr, size) in STATIC.items():
            self.add(addr, size)
        self.add(storage, STORAGE)
        self.add(u32(0x0056AF40), NEIGHBOUR)

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


STACK = []
STATE = {"seq": 0, "logged": 0, "frames": 0}
RET = {}
PENDING = {}
CALLEE_BPS = []


def arm(address, fn):
    if address not in RET:
        RET[address] = RetSite(address)
    PENDING.setdefault(address, []).append(fn)


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


def set_callees(enabled):
    for bp in CALLEE_BPS:
        bp.enabled = enabled


def h_entry(ctx, _addr):
    STATE["seq"] += 1
    if STACK or STATE["logged"] >= MAX_CALLS:
        return
    storage = ctx.register("ecx") & 0xFFFFFFFF
    track = Tracker()
    track.resolve(storage)
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": storage, "index": ctx.arg(0) & 0xFFFFFFFF, "mode": ctx.arg(1) & 0xFFFFFFFF,
        "caller": ctx.return_address(), "stream_bytes": "", "entry": track.dump(), "events": [],
    }
    STACK.append((rec, track))
    set_callees(True)

    def done(c):
        top_rec, top_track = STACK.pop()
        top_rec["events"].append({"e": "x", "w": top_track.diff(), "eax": c.register("eax") & 0xFFFFFFFF})
        set_callees(False)
        STATE["logged"] += 1
        emit(top_rec)
        if STATE["logged"] >= MAX_CALLS:
            emit({"t": "stop", "calls": STATE["seq"]})
            gdb.execute("quit")

    arm(ctx.return_address(), done)


def h_callee(ctx, address):
    if not STACK:
        return
    ret_to = ctx.return_address()
    if not BODY[0] <= ret_to < BODY[1]:
        return
    rec, track = STACK[-1]
    own = track.diff()
    if own:
        rec["events"].append({"e": "w", "w": own})
    call = {
        "e": "c", "a": address, "site": ret_to, "ecx": ctx.register("ecx") & 0xFFFFFFFF,
        "edx": ctx.register("edx") & 0xFFFFFFFF, "args": [ctx.arg(i) & 0xFFFFFFFF for i in range(6)],
    }
    rec["events"].append(call)

    def back(c):
        eax = c.register("eax") & 0xFFFFFFFF
        ev = {"e": "r", "a": address, "eax": eax, "edx": c.register("edx") & 0xFFFFFFFF}
        theirs = track.diff()
        if theirs:
            rec["events"].append({"e": "cw", "w": theirs})
        rec["events"].append(ev)

    arm(ctx.return_address(), back)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"]})
        gdb.execute("quit")


Entry(ENTRY, h_entry)
for address in CALLEES:
    bp = Entry(address, h_callee)
    bp.enabled = False
    CALLEE_BPS.append(bp)
Entry(0x00480FF0, h_frame)
gdb.write("construct probe armed\n")
