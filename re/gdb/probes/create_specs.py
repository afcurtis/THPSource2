"""Case generators for the Phase 4.9 forced-call probe (create_calls.py): create-a-skater.

Every spec names one retail function and produces (args, pokes) for each case: the arguments of the
call and the memory writes that put the game in a random legal state first.  The native replay test
(src/runtime/create_retail_calls_test.cpp) maps the same addresses to the ported functions.

The create code keeps its working data in objects the editor allocates.  The probe does the same:
it takes one scratch slab from the game's own allocator and lays fake objects (palettes, saved
palettes, texture databases, part tables) out in it, so every byte a function may touch is inside
one captured region.
"""

from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from typing import Callable

from career_state import u8, u16, u32


class Skip(Exception):
    """This case cannot be built from the current game state."""


@dataclass
class Spec:
    address: int
    name: str
    group: str
    gen: Callable
    this: int | None = None
    edx: object = None          # a fastcall's second register argument (an integer, or callable(env))
    via: object = None          # Phase 4.14a: callable(env) -> the address to call instead (a trampoline that sets the x87 control word first); the record still names `address`


SPECS: list[Spec] = []
# Per group: bytes of scratch slab the group needs, a seed function (env, rng) -> {address: bytes}
# written before the base snapshot, static regions the code reads (captured once), and extra
# mutable regions (callables returning (address, size) pairs once the slab exists).
SLAB_BYTES: dict[str, int] = {}
SEEDS: dict[str, Callable] = {}
READ_ONLY: dict[str, list[tuple[int, int]]] = {}
EXTRA_REGIONS: dict[str, Callable] = {}


def spec(address, name, group, this=None, edx=None, via=None):
    def deco(fn):
        SPECS.append(Spec(address, name, group, fn, this, edx, via))
        return fn
    return deco


def select(groups, only):
    out = [s for s in SPECS if s.group in groups]
    if only:
        out = [s for s in out if s.address in only]
    return out


class Pokes:
    """The writes that put the game in a case's starting state."""

    def __init__(self):
        self.pokes: dict[int, bytes] = {}

    def put(self, address: int, data: bytes) -> None:
        self.pokes[address] = data


def s32(v: int) -> bytes:
    return struct.pack("<i", v)


class Env:
    """What the generators know about the running game and the scratch slab."""

    def __init__(self, rng, slab: int, slab_size: int, base):
        self.rng = rng
        self.slab = slab
        self.slab_size = slab_size
        self.base = base
        self.pal_bytes = b""         # what the next Pal_CopyToMemory hands back
        self.alloc_next = 0          # bump pointer of the allocator stub
        self.alloc_log = []

    # ---- base memory -----------------------------------------------------------------------
    def bytes(self, address, size):
        for base, data in self.base.items():
            if base <= address and address + size <= base + len(data):
                return data[address - base:address - base + size]
        raise KeyError(f"{address:#x}")

    def u8(self, a): return self.bytes(a, 1)[0]
    def u16(self, a): return struct.unpack("<H", self.bytes(a, 2))[0]
    def u32(self, a): return struct.unpack("<I", self.bytes(a, 4))[0]
    def s32(self, a): return struct.unpack("<i", self.bytes(a, 4))[0]

    # ---- per case --------------------------------------------------------------------------
    def begin_case(self):
        self.alloc_next = 0
        self.alloc_log = []

    def rnd(self, args):
        limit = args[0] if args else 0
        limit = limit if limit < 0x7FFFFFFF else 0
        return self.rng.randrange(limit) if limit > 0 else 0

    def alloc(self, args):
        """FileIO_AllocLoadedBuffer stand-in: hand out slab addresses in order."""
        size = max(args[0], 8)
        at = self.slab + self.alloc_base + self.alloc_next
        self.alloc_next += (size + 15) & ~15
        if self.alloc_next > self.alloc_size:
            raise RuntimeError("the allocation arena is exhausted")
        self.alloc_log.append((at, size))
        if os.environ.get("P410_STUBTRACE") == "2":      # (what the block held before the caller's constructor: retail's class new would have zeroed it)
            from opentony.memory import mem
            print(f"ALLOC {at:#x} size {size:#x} first {mem.u32(at):#x} base {self.alloc_base:#x} next {self.alloc_next:#x}", flush=True)
        return at

    alloc_base = 0
    alloc_size = 0
    file_ptr = 0     # what the next FileIO_OpenLoad hands back
    materials: dict = {}     # scene material checksum -> address of its record (PSX_FindSceneMaterial)

    def find_material(self, args):
        return self.materials.get(args[0], 0)

    def file_open(self, args):
        return self.file_ptr

    def text_width(self, args):
        """The font's width of a line, in pixels: a fixed function of the characters (the real one reads glyph
        tables of the font object), so the layout has lines that fit and lines that do not."""
        from opentony.memory import mem
        text = mem.cstring(args[0], 256)
        return sum(6 + (ord(c) % 5) for c in text)


def install_stubs(fc, env):
    from opentony.memory import mem

    fc.stub(0x004011E0, 2, "assert", when=lambda a: (a[0] & 0xFF) == 0, keep=slice(1, 2))
    fc.stub(0x004CF850, 1, "printf_log")
    fc.stub(0x004C9340, 1, "rnd", result=env.rnd)
    fc.stub(0x0040B500, 1, "rand", result=env.rnd)
    # loader allocations come from the slab's arena; the file opener hands back a prepared image
    fc.stub(0x0046F490, 4, "alloc", result=env.alloc)
    fc.stub(0x00448DD0, 2, "file_open", result=env.file_open)
    fc.stub(0x0046F4D0, 1, "mem_delete")
    # the trick database is validated elsewhere (Phase 4.7b): report the call, leave the career record alone
    fc.stub(0x004170D0, 2, "trick_init")
    fc.stub(0x004B2030, 1, "find_material", result=env.find_material)
    # the font's width of a line (a thiscall on the font object; the line is reported as the call's text)
    fc.stub(0x0044A4D0, 1, "text_width", convention="thiscall", keep=slice(0, 0), text=True, result=env.text_width)
    # the engine's palette store: a read hands back 15-bit colours, a write reports them
    fc.stub(0x0048AAD0, 2, "palette_read", keep=slice(0, 1),
            fill=(1, lambda a: env.pal_bytes[:32 if mem.u8(a[0] + 3) & 1 else 512]))
    fc.stub(0x0048AB00, 2, "palette_store", keep=slice(0, 1),
            blob=(1, lambda a: 32 if mem.u8(a[0] + 3) & 1 else 512))
