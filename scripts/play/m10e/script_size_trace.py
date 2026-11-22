"""Milestone 10e probe: TrickScript_CommandSize 0x004bf6c0 and TrickScript_ReadWord 0x004be3c0 on retail memory.

Distinct calls only (the walk of a script table makes tens of thousands). One ndjson line per distinct call:
  {"f": "size", "window": hex of 0x40 bytes at the command, "delta": returned address - command, "count": calls seen so far}
  {"f": "word", "window": hex of the 4 bytes at *cursor, "eax": ax, "advance": *cursor after - before}
Written as they are first seen; a final {"t": "stop"} line says how many calls of each were made.

Env: M10_OUT (ndjson), M10_STOP_FRAMES (frame hook hits), M10_MAX_CALLS (stop after this many calls of either function).
"""

import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10e"), exist_ok=True)  # outputs stay in build/
import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT = open(os.environ.get("M10_OUT", (os.path.expanduser("~/THPSource2") + "/build/m10e/script_size.ndjson")), "w")
STOP_FRAMES = int(os.environ.get("M10_STOP_FRAMES", "0"))
MAX_CALLS = int(os.environ.get("M10_MAX_CALLS", "200000"))
SIZE, WORD = 0x004BF6C0, 0x004BE3C0
WINDOW = 0x40

SEEN = set()
COUNTS = {"size": 0, "word": 0}
STATE = {"frames": 0}
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


def finish():
    emit({"t": "stop", "counts": COUNTS})
    gdb.execute("quit")


def h_size(ctx, _addr):
    command = ctx.arg(0) & 0xFFFFFFFF
    window = read(command, WINDOW)
    if window is None:
        window = read(command, 8) or b""

    def done(c):
        COUNTS["size"] += 1
        eax = c.register("eax") & 0xFFFFFFFF
        key = ("size", window.hex(), (eax - command) & 0xFFFFFFFF)
        if key not in SEEN:
            SEEN.add(key)
            emit({"f": "size", "window": window.hex(), "delta": (eax - command) & 0xFFFFFFFF})
        if COUNTS["size"] + COUNTS["word"] >= MAX_CALLS:
            finish()

    arm(ctx.return_address(), done)


def h_word(ctx, _addr):
    cursor = ctx.arg(0) & 0xFFFFFFFF
    stream = int.from_bytes(read(cursor, 4) or b"\0\0\0\0", "little")
    window = read(stream, 4) or b""

    def done(c):
        COUNTS["word"] += 1
        after = int.from_bytes(read(cursor, 4) or b"\0\0\0\0", "little")
        eax = c.register("eax") & 0xFFFF
        key = ("word", window.hex(), eax, (after - stream) & 0xFFFFFFFF)
        if key not in SEEN:
            SEEN.add(key)
            emit({"f": "word", "window": window.hex(), "eax": eax, "advance": (after - stream) & 0xFFFFFFFF})

    arm(ctx.return_address(), done)


def h_frame(ctx, _addr):
    STATE["frames"] += 1
    if STOP_FRAMES and STATE["frames"] >= STOP_FRAMES:
        finish()


Entry(SIZE, h_size)
Entry(WORD, h_word)
Entry(0x00480FF0, h_frame)
gdb.write("script size probe armed\n")
