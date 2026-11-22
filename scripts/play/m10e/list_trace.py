"""Milestone 10e probe: the object list functions of ob.cpp on retail memory.

ObjectList_PushHead 0x004801d0, ObjectList_Remove 0x004801f0, Object_SuspendToList 0x004802f0 and CBody__UnSuspend 0x004803b0, every top-level call (the nested ones are the same code and
are covered by the Kit calling itself). Same record schema as build/m10e/create_trace.py (c / r / w / cw / x events; scripts/make_trg_interpreter_fixture.py turns it into a fixture):
the call record carries the function in "index", the list argument in "mode", the object (ecx) in "stream"; the entry state has the object, its neighbours, the heads of the lists
involved and their first objects. Callees recorded: the assertion 0x004011e0 and the object's virtual slot 1 (Suspend only).

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (frame hook hits), M10_PER_FN (log at most this many calls of each function).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/list.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "400"))
PER_FN = int(os.environ.get("M10_PER_FN", "150"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

FUNCTIONS = {0x4801D0: "ObjectList_PushHead", 0x4801F0: "ObjectList_Remove", 0x4802F0: "Object_SuspendToList", 0x4803B0: "CBody__UnSuspend"}
SUSPENDED_LIST = 0x56AF44
ASSERT = 0x4011E0
VCALL_SITE = 0x480383          # the return address of Object_SuspendToList's virtual call
OBJECT = 0xB8
NEIGHBOUR = 0x70

STATIC = {
    "line_tag": (0x00568638, 0x04),
    "file_tag": (0x0056881C, 0x40),
    "suspended_head": (SUSPENDED_LIST, 0x04),
    "game_head": (0x0056AF40, 0x04),
    "baddy_head": (0x0055F6BC, 0x04),
    "powerup_head": (0x0056B830, 0x04),
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

    def resolve(self, obj, lst):
        for _, (addr, size) in STATIC.items():
            self.add(addr, size)
        self.add(obj, OBJECT)
        self.add(u32(obj), 0x20)       # the vtable: Suspend's virtual call reads slot 1
        for pointer in (u32(obj + 0x20), u32(obj + 0x34)):
            self.add(pointer, NEIGHBOUR)
        for head in {lst, SUSPENDED_LIST, u32(obj + 0x64)}:
            if head:
                self.add(head, 4)
                self.add(u32(head), NEIGHBOUR)
        for pointer in (u32(u32(obj + 0x64)) if u32(obj + 0x64) else 0,):
            self.add(pointer, NEIGHBOUR)

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
COUNTS = {}
RET = {}
PENDING = {}
VCALL_BPS = {}


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


def h_entry(ctx, address):
    STATE["seq"] += 1
    if STACK or STATE["logged"] >= MAX_CALLS or COUNTS.get(address, 0) >= PER_FN:
        return
    COUNTS[address] = COUNTS.get(address, 0) + 1
    obj = ctx.register("ecx") & 0xFFFFFFFF
    lst = ctx.arg(0) & 0xFFFFFFFF if address != 0x4803B0 else 0
    track = Tracker()
    track.resolve(obj, lst)
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": obj, "index": address, "mode": lst,
        "caller": ctx.return_address(), "stream_bytes": "", "entry": track.dump(), "events": [],
    }
    STACK.append((rec, track))
    ASSERT_BP.enabled = True
    if address == 0x4802F0:
        target = u32(u32(obj) + 4)
        if target not in VCALL_BPS:
            VCALL_BPS[target] = Entry(target, h_callee)
        VCALL_BPS[target].enabled = True

    def done(c):
        top_rec, top_track = STACK.pop()
        top_rec["events"].append({"e": "x", "w": top_track.diff(), "eax": c.register("eax") & 0xFFFFFFFF})
        ASSERT_BP.enabled = False
        for bp in VCALL_BPS.values():
            bp.enabled = False
        STATE["logged"] += 1
        emit(top_rec)
        if STATE["logged"] >= MAX_CALLS or all(COUNTS.get(a, 0) >= PER_FN for a in FUNCTIONS):
            emit({"t": "stop", "calls": STATE["seq"], "counts": COUNTS})
            gdb.execute("quit")

    arm(ctx.return_address(), done)


def h_callee(ctx, address):
    if not STACK:
        return
    ret_to = ctx.return_address()
    if address != ASSERT and ret_to != VCALL_SITE:
        return
    rec, track = STACK[-1]
    own = track.diff()
    if own:
        rec["events"].append({"e": "w", "w": own})
    rec["events"].append({
        "e": "c", "a": address, "site": ret_to, "ecx": ctx.register("ecx") & 0xFFFFFFFF,
        "edx": ctx.register("edx") & 0xFFFFFFFF, "args": [ctx.arg(i) & 0xFFFFFFFF for i in range(6)],
    })

    def back(c):
        ev = {"e": "r", "a": address, "eax": c.register("eax") & 0xFFFFFFFF, "edx": c.register("edx") & 0xFFFFFFFF}
        theirs = track.diff()
        if theirs:
            rec["events"].append({"e": "cw", "w": theirs})
        rec["events"].append(ev)

    arm(ret_to, back)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"], "counts": COUNTS})
        gdb.execute("quit")


for function in FUNCTIONS:
    Entry(function, h_entry)
ASSERT_BP = Entry(ASSERT, h_callee)
ASSERT_BP.enabled = False
Entry(0x00480FF0, h_frame)
gdb.write("list probe armed\n")
