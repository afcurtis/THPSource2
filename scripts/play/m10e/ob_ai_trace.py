"""Milestone 10e probe: Ob_AI 0x00480ff0 (the per-frame object loop) on retail memory.

Same record schema as scripts/play/m10e/powerup_trace.py (c / r / w / cw / x events; scripts/make_trg_interpreter_fixture.py turns it into a fixture). The call record carries the list head's address
in "mode" and 0x00480ff0 in "index". The entry state is the assertion words, the player count and the two player pointers (with the 12 bytes of position behind each), the suspend distance,
the list heads, and for every object of the list the fields the loop reads (+0x00..0x24 vtable, flags, position, next; +0x34; +0x44; +0x64..0x6c; +0xb8) and the first slot of its vtable.
Callees recorded (their return address inside Ob_AI): CItem__FlashUpdate, M3dUtils_InBetween, Animation_InvokeUpdateHooks, Object_SpriteUpdate, Object_SuspendToList and the virtual call
`call [edx]` at 0x00481072 (slot 0, argument 1; logged as a callee at the address the vtable holds, site 0x00481074). Utils_CrapDist is not logged: the Kit computes it (the position regions
are the entry state).

A callee's writes are diffed over the globals, the object being processed, its list neighbours (before and after) and the first suspended object; objects further down the list that a callee
changes are not seen (the replay reports them as a different call sequence).

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (Ob_AI entries), M10_EMPTY (log at most this many calls on an empty list).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/ob_ai.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "60"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))
MAX_EMPTY = int(os.environ.get("M10_EMPTY", "3"))

OB_AI = 0x00480FF0
BODY = (0x00480FF0, 0x00481101)
LOOP_HEAD = 0x00481002
VCALL_INSN = 0x00481072
VCALL_RET = 0x00481074
SUSPENDED_LIST = 0x0056AF44
CALLEES = {
    0x47FFC0: "CItem__FlashUpdate", 0x4011E0: "M3dUtils_InBetween", 0x480FA0: "Animation_InvokeUpdateHooks",
    0x480460: "Object_SpriteUpdate", 0x4802F0: "Object_SuspendToList",
}
MAX_OBJECTS = 6000
OBJECT_BLOCKS = [(0x00, 0x24), (0x34, 0x04), (0x44, 0x02), (0x64, 0x08), (0xB8, 0x04)]

STATIC = {
    "line_tag": (0x00568638, 0x04),
    "file_tag": (0x0056881C, 0x40),
    "players": (0x00533F3C, 0x04),
    "player_one": (0x0056A858, 0x04),
    "player_two": (0x0056A85C, 0x04),
    "distance": (0x00535AF8, 0x04),
    "suspended_head": (SUSPENDED_LIST, 0x04),
    "active_head": (0x0056AF40, 0x04),
    "game_head": (0x0055F6BC, 0x04),
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
        self.regions = {}     # address -> bytes
        self.owner = {}       # address -> object (None for the globals)
        self.pending = []

    def add(self, addr, size, owner=None):
        if addr:
            self.pending.append((addr, size, owner))

    def add_object(self, obj):
        for off, size in OBJECT_BLOCKS:
            self.add(obj + off, size, obj)
        self.add(u32(obj), 4, None)      # slot 0 of its vtable

    def finalize(self):
        merged = []
        for addr, size, owner in sorted(self.pending, key=lambda p: (p[0], -p[1])):
            if merged and addr < merged[-1][0] + merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], addr + size - merged[-1][0])
                if merged[-1][2] != owner:
                    merged[-1][2] = None
            else:
                merged.append([addr, size, owner])
        self.pending = []
        for addr, size, owner in merged:
            if addr in self.regions:
                continue
            data = read(addr, size)
            while data is None and size > 4:
                size //= 2
                data = read(addr, size)
            if data is not None:
                self.regions[addr] = data
                self.owner[addr] = owner

    def diff(self, scope):
        changes = []
        for addr, old in list(self.regions.items()):
            if self.owner[addr] is not None and self.owner[addr] not in scope:
                continue
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


FRAME = []            # [(record, tracker, state)] while Ob_AI runs
STATE = {"seq": 0, "logged": 0, "frames": 0, "empty": 0}
RET = {}
PENDING = {}
CALLEE_BPS = []


def arm(address, fn, esp_min):
    """Run fn when `address` is reached with ESP >= esp_min (the caller's frame: a nested call of the same code returns with a lower ESP)."""
    if address not in RET:
        RET[address] = RetSite(address)
    PENDING.setdefault(address, []).append((fn, esp_min))


class RetSite(TonyBreakpoint):
    def on_hit(self, ctx):
        esp = ctx.register("esp") & 0xFFFFFFFF
        stack = PENDING.get(self.address)
        while stack and esp >= stack[-1][1]:
            stack.pop()[0](ctx)


class Entry(TonyBreakpoint):
    def __init__(self, address, handler, **kw):
        super().__init__(address, **kw)
        self.handler = handler

    def on_hit(self, ctx):
        self.handler(ctx, self.address)


def set_callees(enabled):
    for bp in CALLEE_BPS:
        bp.enabled = enabled


def scope_of(obj, extra=()):
    scope = {obj, u32(obj + 0x20), u32(obj + 0x34), u32(u32(SUSPENDED_LIST))}
    scope.update(extra)
    scope.discard(0)
    return scope


def open_frame(ctx, _addr):
    STATE["seq"] += 1
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] > STOP_FRAMES:
        emit({"t": "stop", "frames": STATE["frames"], "calls": STATE["seq"]})
        gdb.execute("quit")
    head = ctx.arg(0) & 0xFFFFFFFF
    first = u32(head)
    if FRAME or STATE["logged"] >= MAX_CALLS or (first == 0 and STATE["empty"] >= MAX_EMPTY):
        return
    if first == 0:
        STATE["empty"] += 1
    track = Tracker()
    for _, (addr, size) in STATIC.items():
        track.add(addr, size)
    track.add(head, 4)
    for player in (u32(0x0056A858), u32(0x0056A85C)):
        track.add(player + 8 if player else 0, 12)
    objects = []
    obj = first
    while obj and len(objects) < MAX_OBJECTS:
        objects.append(obj)
        track.add_object(obj)
        obj = u32(obj + 0x20)
    first_suspended = u32(u32(SUSPENDED_LIST))
    if first_suspended:
        track.add_object(first_suspended)
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": 0, "index": OB_AI, "mode": head,
        "caller": ctx.return_address(), "stream_bytes": "", "entry": track.dump(), "events": [],
    }
    state = {"objects": objects, "current": 0, "scope": set()}
    FRAME.append((rec, track, state))
    set_callees(True)

    def done(c):
        top_rec, top_track, top_state = FRAME.pop()
        top_rec["events"].append({"e": "x", "w": top_track.diff(top_state["scope"]), "eax": c.register("eax") & 0xFFFFFFFF})
        set_callees(False)
        STATE["logged"] += 1
        emit(top_rec)
        if STATE["logged"] >= MAX_CALLS:
            emit({"t": "stop", "calls": STATE["seq"], "frames": STATE["frames"]})
            gdb.execute("quit")

    arm(ctx.return_address(), done, (ctx.register("esp") & 0xFFFFFFFF) + 4)


def log_callee(ctx, address, site, args, entry_esp):
    rec, track, state = FRAME[-1]
    obj = ctx.register("ebp") & 0xFFFFFFFF
    state["current"] = obj
    state["scope"] |= scope_of(obj)
    scope = state["scope"]
    own = track.diff(scope)
    if own:
        rec["events"].append({"e": "w", "w": own})
    rec["events"].append({
        "e": "c", "a": address, "site": site, "ecx": ctx.register("ecx") & 0xFFFFFFFF,
        "edx": ctx.register("edx") & 0xFFFFFFFF, "args": args,
    })

    def back(c):
        ev = {"e": "r", "a": address, "eax": c.register("eax") & 0xFFFFFFFF, "edx": c.register("edx") & 0xFFFFFFFF}
        state["scope"] |= scope_of(obj, {u32(obj + 0x20)})
        theirs = track.diff(state["scope"])
        if theirs:
            rec["events"].append({"e": "cw", "w": theirs})
        rec["events"].append(ev)

    arm(site, back, entry_esp + 4)


def h_callee(ctx, address):
    if not FRAME:
        return
    ret_to = ctx.return_address()
    if not BODY[0] <= ret_to < BODY[1]:
        return
    log_callee(ctx, address, ret_to, [ctx.arg(i) & 0xFFFFFFFF for i in range(6)], ctx.register("esp") & 0xFFFFFFFF)


def h_vcall(ctx, _addr):
    if not FRAME:
        return
    esp = ctx.register("esp") & 0xFFFFFFFF
    target = u32(ctx.register("edx") & 0xFFFFFFFF)
    log_callee(ctx, target, VCALL_RET, [u32(esp), 0, 0, 0, 0, 0], esp - 4)


Entry(OB_AI, open_frame)
for address in CALLEES:
    bp = Entry(address, h_callee)
    bp.enabled = False
    CALLEE_BPS.append(bp)
bp = Entry(VCALL_INSN, h_vcall)
bp.enabled = False
CALLEE_BPS.append(bp)
gdb.write("ob_ai probe armed\n")
