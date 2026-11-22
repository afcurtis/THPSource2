"""Milestone 10e probe: Powerup_CreateFromTrigger 0x004a8e50 and Powerup_Construct 0x004a7c50 on retail memory.

Same record schema as scripts/play/m10e/construct_trace.py (c / r / w / cw / x events; scripts/make_trg_interpreter_fixture.py turns it into a fixture). Each call record carries the function in
"index" (0x004a8e50 or 0x004a7c50), the storage (ecx of Powerup_Construct) in "stream" and every pushed argument as little-endian dwords in "stream_bytes" (five for CreateFromTrigger: type,
position, flag, word, node; six for Powerup_Construct: type, position, velocity, flags, word, node). Powerup_Construct called from CreateFromTrigger is logged both as a callee of it and as a
call of its own. Tracked at Powerup_Construct's entry: the storage (0x100 bytes), the 12 bytes behind the position and the velocity pointers, the level globals it reads, the career goal row
of the current level and the head of the power-up list with the object behind it (ObjectList_PushHead writes its back link). CreateFromTrigger: the assertion words and the position bytes.

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (frame hook hits).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/powerup.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "400"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

CREATE = 0x004A8E50
CONSTRUCT = 0x004A7C50
FUNCS = {
    CREATE: {"body": (0x004A8E50, 0x004A8ED7), "args": 5},
    CONSTRUCT: {"body": (0x004A7C50, 0x004A814B), "args": 6},
}
STORAGE = 0x100
NEIGHBOUR = 0x70
GOAL_ROW = 0x1AC
GOAL_TABLE = 0x0053C2B4

CALLEES = {
    0x480060: "Object_Construct", 0x4C9340: "Rnd", 0x4CA140: "Utils_GetGroundHeight", 0x480420: "Object_WantSprite",
    0x47FE30: "Object_SelectNamedPsxRegion", 0x4B1DE0: "PSX_FindModelIndexByChecksum", 0x414540: "Career_GoalIndex",
    0x415060: "Career_GotMoney", 0x4801D0: "ObjectList_PushHead", 0x4A8250: "maybe_destroy_model", 0x4011E0: "M3dUtils_InBetween",
    0x47FD30: "TRG_AllocObjectStorage",
}
# bytes behind pointer arguments at the call: callee -> [(argument index, size)]
ARGDATA = {CONSTRUCT: [(1, 12), (2, 12)], 0x4CA140: [(0, 12)]}

STATIC = {
    "line_tag": (0x00568638, 0x04),
    "file_tag": (0x0056881C, 0x40),
    "items_region": (0x0056DCAC, 0x04),
    "goal_region": (0x0056DCB4, 0x04),
    "level": (0x0056A898, 0x04),
    "mode": (0x00533F38, 0x04),
    "flags": (0x00568A24, 0x04),
    "power_head": (0x0056B830, 0x04),
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

    def resolve(self, func, storage, args):
        for _, (addr, size) in STATIC.items():
            self.add(addr, size)
        if func == CONSTRUCT:
            self.add(storage, STORAGE)
            self.add(args[1], 12)
            self.add(args[2], 12)
            self.add(GOAL_TABLE + u32(0x0056A898) * GOAL_ROW, GOAL_ROW)
            self.add(u32(0x0056B830), NEIGHBOUR)
        else:
            self.add(args[1], 12)

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


FRAMES = {}   # function -> (record, tracker) while it runs
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


def open_frame(ctx, func):
    STATE["seq"] += 1
    if func in FUNCS and (func in FRAMES or STATE["logged"] >= MAX_CALLS):
        return
    n_args = FUNCS[func]["args"]
    args = [ctx.arg(i) & 0xFFFFFFFF for i in range(n_args)]
    storage = ctx.register("ecx") & 0xFFFFFFFF if func == CONSTRUCT else 0
    track = Tracker()
    track.resolve(func, storage, args)
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": storage, "index": func, "mode": 0,
        "caller": ctx.return_address(), "stream_bytes": b"".join(a.to_bytes(4, "little") for a in args).hex(),
        "entry": track.dump(), "events": [],
    }
    FRAMES[func] = (rec, track)
    set_callees(True)

    def done(c):
        top_rec, top_track = FRAMES.pop(func)
        top_rec["events"].append({"e": "x", "w": top_track.diff(), "eax": c.register("eax") & 0xFFFFFFFF})
        if not FRAMES:
            set_callees(False)
        STATE["logged"] += 1
        emit(top_rec)
        if STATE["logged"] >= MAX_CALLS and not FRAMES:
            emit({"t": "stop", "calls": STATE["seq"]})
            gdb.execute("quit")

    arm(ctx.return_address(), done)


def h_any(ctx, address):
    if address in FUNCS:
        open_frame(ctx, address)
    if not FRAMES:
        return
    ret_to = ctx.return_address()
    for func, (rec, track) in list(FRAMES.items()):
        lo, hi = FUNCS[func]["body"]
        if not lo <= ret_to < hi or address not in CALLEES:
            continue
        log_callee(ctx, address, ret_to, rec, track)


def log_callee(ctx, address, ret_to, rec, track):
    own = track.diff()
    if own:
        rec["events"].append({"e": "w", "w": own})
    call = {
        "e": "c", "a": address, "site": ret_to, "ecx": ctx.register("ecx") & 0xFFFFFFFF,
        "edx": ctx.register("edx") & 0xFFFFFFFF, "args": [ctx.arg(i) & 0xFFFFFFFF for i in range(6)],
    }
    if address in ARGDATA:
        call["pointees"] = [(read(call["args"][i], size) or b"").hex() for i, size in ARGDATA[address]]
    rec["events"].append(call)

    def back(c):
        eax = c.register("eax") & 0xFFFFFFFF
        ev = {"e": "r", "a": address, "eax": eax, "edx": c.register("edx") & 0xFFFFFFFF}
        theirs = track.diff()
        if theirs:
            rec["events"].append({"e": "cw", "w": theirs})
        rec["events"].append(ev)

    arm(ret_to, back)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"]})
        gdb.execute("quit")


for address in FUNCS:
    Entry(address, h_any)
for address in CALLEES:
    if address in FUNCS:
        continue
    bp = Entry(address, h_any)
    bp.enabled = False
    CALLEE_BPS.append(bp)
Entry(0x00480FF0, h_frame)
gdb.write("powerup probe armed\n")
