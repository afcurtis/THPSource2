"""Milestone 10e probe: TrickTable_BuildForPlayer 0x004bcf00 on retail memory.

Same record schema as scripts/play/m10e/list_trace.py (c / r / w / cw / x events; scripts/make_trg_interpreter_fixture.py turns it into a fixture). The call record carries the function in "index",
the player argument in "mode", the manager (ecx) in "stream" and the table argument as the four little-endian bytes of "stream_bytes". The entry state: the assertion words, the pointer cells
the function reads (0x00568688 section 0 of TRICKS.BIN, 0x0056a894 the loaded file, 0x0056a850 section 6), the alternate binding block, the whole loaded file and the table's 0x400 bytes.
Callees recorded (only those the function itself calls): Skater_GetSlotRecord 0x004b1430, Skater_MapEnabledIndex 0x004b1980, SkaterManager_LoadedBlock 0x004b1570 (with the 0x35-byte block
its result points at), Skater_GetResourceSlot 0x004b0c60, TrickConfig_FindByCharacter 0x00416050 (with the 0x104-byte record), SkaterManager_GetCostume 0x004b0cd0, and the assertion 0x004011e0
(also from the four table builders when it fails: the Kit must report those). The table builders 0x004bc3a0 / 0x004bcb70 / 0x004bcc90 / 0x004bcdd0 are not recorded: their effect is the
table and the assertion words.

Env: M10_OUT (ndjson), M10_MAX (stop after this many logged calls), M10_STOP_FRAMES (frame hook hits).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/tricktable.ndjson")), "w")
MAX_CALLS = int(os.environ.get("M10_MAX", "100"))
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))

ENTRY = 0x004BCF00
BODY = (0x004BCF00, 0x004BD153)
ASSERT = 0x004011E0
IMAGE_POINTER = 0x0056A894
IMAGE_SIZE = 33168
TABLE_BYTES = 0x400

CALLEES = {
    0x4B1430: "Skater_GetSlotRecord", 0x4B1980: "Skater_MapEnabledIndex", 0x4B1570: "SkaterManager_LoadedBlock",
    0x4B0C60: "Skater_GetResourceSlot", 0x416050: "TrickConfig_FindByCharacter", 0x4B0CD0: "SkaterManager_GetCostume",
    ASSERT: "M3dUtils_InBetween",
}
RETURNED = {0x4B1570: 0x35, 0x416050: 0x104}

STATIC = {
    "line_tag": (0x00568638, 0x04),
    "file_tag": (0x0056881C, 0x40),
    "source_file": (0x0054108C, 0x20),
    "section0": (0x00568688, 0x04),
    "image": (IMAGE_POINTER, 0x04),
    "section6": (0x0056A850, 0x04),
    "alternate_block": (0x0056A690, 0x38),
}


def emit(rec):
    OUT.write(json.dumps(rec, sort_keys=True) + "\n")
    OUT.flush()


def read(addr, size):
    try:
        return mem.bytes(addr, size)
    except Exception:
        return None


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

    def resolve(self, table):
        for _, (addr, size) in STATIC.items():
            self.add(addr, size)
        self.add(mem.u32(IMAGE_POINTER), IMAGE_SIZE)
        self.add(table, TABLE_BYTES)

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
    obj = ctx.register("ecx") & 0xFFFFFFFF
    player = ctx.arg(0) & 0xFFFFFFFF
    table = ctx.arg(1) & 0xFFFFFFFF
    track = Tracker()
    track.resolve(table)
    track.finalize()
    rec = {
        "t": "call", "n": STATE["seq"], "depth": 0, "stream": obj, "index": ENTRY, "mode": player,
        "caller": ctx.return_address(), "stream_bytes": table.to_bytes(4, "little").hex(), "entry": track.dump(), "events": [],
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
    in_body = BODY[0] <= ret_to < BODY[1]
    failing_assert = address == ASSERT and (ctx.arg(0) & 0xFF) == 0
    if not in_body and not failing_assert:
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
        eax = c.register("eax") & 0xFFFFFFFF
        ev = {"e": "r", "a": address, "eax": eax, "edx": c.register("edx") & 0xFFFFFFFF}
        if address in RETURNED and eax:
            data = read(eax, RETURNED[address])
            ev["data"] = data.hex() if data is not None else None
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


Entry(ENTRY, h_entry)
for address in CALLEES:
    bp = Entry(address, h_callee)
    bp.enabled = False
    CALLEE_BPS.append(bp)
Entry(0x00480FF0, h_frame)
gdb.write("trick table probe armed\n")
