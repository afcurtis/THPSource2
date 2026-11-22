"""Phase 4.11c case generators, part 4: the PRK buffer (levelgen.cpp 0x0043c050 parse, 0x0043c480 serialize, 0x0043c6b0 translate): group `lgk`.

The buffers are the 50 shipped parks (build/p45/xtools/out/data/PARK*.PRK; both file formats: a park's bytes with the older magic make the older item layout read the
same bytes), parks with fields mutated, and made-up buffers.  The cells go to a block of the slab, the items to a second, the trailing table to its static.
"""

from __future__ import annotations

import glob
import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from gen_specs_model import CELL_X, GRID_D, GRID_W, ROW_SET
from levelgen_specs import (CLOCK, ITEMS, ITEMS_SOURCE, LG_READS, LG_STATICS, ROWS, ROWS_SOURCE, THEME, TRAILING, VERSION_TABLE, World, lg_stubs, window)
from pspool_specs import Ov

PRK_DIR = (os.path.expanduser("~/THPSource2") + "/build/p45/xtools/out/data")
VERSIONS = [(16, 16), (24, 24), (30, 30), (30, 18), (60, 6)]
K_ROWS = 0x3800
K_CELLS = 0x3900            # up to 900 cells of 16 bytes
K_ITEMS = 0x7300
K_BUF = 0x7600              # a PRK buffer (up to 0x1e00 bytes)
K_OUT = 0x9600
K_ARENA = 0x9700
K_ARENA_SIZE = 0x2100
ROTATIONS = (0x00529E18, 0x2A8)

_files: list = []


def park_files():
    if not _files:
        for path in sorted(glob.glob(os.path.join(PRK_DIR, "PARK*.PRK"))):
            with open(path, "rb") as fh:
                _files.append(fh.read())
    return _files


def lgk_regions(slab):
    return window(slab, (K_ROWS, 0x100), (K_CELLS, 0x3900), (K_ITEMS, 0x200), (K_BUF, 0x2000), (K_OUT, 0x10), (K_ARENA, K_ARENA_SIZE)) + LG_STATICS + [ROTATIONS]


def lgk_seed(env, rng):
    env.alloc_base = K_ARENA
    env.alloc_size = K_ARENA_SIZE
    return {}


SLAB_BYTES["lgk"] = K_ARENA + K_ARENA_SIZE + 0x100
SEEDS["lgk"] = lgk_seed
EXTRA_REGIONS["lgk"] = lgk_regions
READ_ONLY["lgk"] = LG_READS


def prk_state(env, w, version):
    """The statics and the editor's tables for a park of `version`: rows of cells at K_CELLS, ten items at K_ITEMS."""
    ov, rng = w.ov, w.rng
    width, depth = VERSIONS[version]
    ov.u32(GRID_W, width)
    ov.u32(GRID_D, depth)
    ov.u32(ROW_SET, version)
    ov.u32(THEME, rng.randrange(4))
    table = w.a(K_ROWS)
    for z in range(min(depth, 64)):
        ov.u32(table + 4 * z, w.a(K_CELLS) + z * width * 16)
    ov.u32(0x005614CC, table)
    ov.u32(ROWS, table)
    ov.u32(0x005614D0, w.a(K_ITEMS))
    ov.u32(0x005614D8, w.a(K_ITEMS))
    ov.put(w.a(K_ITEMS), rng.randbytes(0x1B8))
    ov.put(TRAILING, rng.randbytes(0x40))
    return width, depth


def made_up_buffer(rng, version, magic):
    """A buffer of the given version with random cells and items (references that are legal ids or empty)."""
    width, depth = VERSIONS[version]
    cells = width * depth
    out = bytearray(struct.pack("<III", magic, version, rng.randrange(0, 4)))
    for _ in range(cells):
        refs = bytes(rng.choice([0xFF, 0xFF, rng.randrange(0, 40)]) for _ in range(5))
        word = rng.randrange(1 << 16)
        out += refs + bytes([0]) + struct.pack("<H", word)
    for _ in range(10):
        rec = bytearray(rng.randbytes(0x24))
        name = bytes(rng.randrange(0x41, 0x5B) for _ in range(rng.randrange(1, 20))) + b"\0"
        off = 0xB if magic == 0x4E25 else 0xA
        rec[off:off + len(name)] = name
        out += rec
    for _ in range(8):
        rec = rng.choice([b"AAA\0", b"ABC\0", b"AB\0\0", b"ABCD", b"\0\0\0\0"]) + rng.randbytes(4)
        out += rec
    return bytes(out)


@spec(0x0043C050, "CustomPark_ParseBuffer", "lgk")
def gen_parse(env, n):
    rng = env.rng
    files = park_files()
    roll = rng.random()
    if files and roll < 0.55:
        data = bytearray(rng.choice(files))
        if rng.random() < 0.15:
            struct.pack_into("<I", data, 0, 0x4E24)
        elif rng.random() < 0.1:
            for _ in range(rng.randrange(1, 6)):
                data[rng.randrange(12, len(data))] = rng.randrange(256)
        data = bytes(data)
    else:
        data = made_up_buffer(rng, rng.randrange(5), rng.choice([0x4E25, 0x4E25, 0x4E24, 0x1234, 0]))
    version = struct.unpack_from("<I", data, 4)[0] if len(data) >= 8 else 0
    w = World(env)
    w.ov = Ov()
    prk_state(env, w, version if version < 5 else 0)
    w.ov.put(w.a(K_BUF), data)
    w.ov.put(w.a(K_OUT), rng.randbytes(8))
    return [w.a(K_BUF), w.a(K_OUT)], w.ov.pokes()


def cells_state(w, version, theme):
    ov, rng = w.ov, w.rng
    width, depth = prk_state(w.env, w, version)
    ov.u32(THEME, theme)
    ov.u32(0x005614C8, theme)
    for c in range(width * depth):
        cell = w.a(K_CELLS) + c * 16
        count = rng.randrange(0, 6)
        for k in range(5):
            ov.u16(cell + 2 * k, rng.choice([0xFFFF, 0xFFFF, rng.randrange(0, 60)]))
            ov.u8(cell + 0xA + k, rng.randrange(4))
        ov.u8(cell + 0xF, count)
    return width, depth


@spec(0x0043C480, "CustomPark_SerializeBuffer", "lgk")
def gen_serialize(env, n):
    rng = env.rng
    w = World(env)
    w.ov = Ov()
    version = rng.choice([0, 0, 1, 2, 3, 4])
    cells_state(w, version, rng.randrange(4))
    for i in range(10):
        at = w.a(K_ITEMS) + 0x2C * i
        w.ov.put(at, rng.randbytes(0x2C))
        w.ov.u8(at + 0xC, rng.choice([0x80, 0x81, 0x00, 0x05]))
        w.ov.put(at + 0xD, bytes(rng.randrange(0x41, 0x5B) for _ in range(rng.randrange(1, 20))) + b"\0")
    w.ov.put(w.a(K_OUT), rng.randbytes(8))
    return [w.a(K_OUT)], w.ov.pokes()


@spec(0x0043C6B0, "LevelGen_TranslateReferences", "lgk")
def gen_translate(env, n):
    rng = env.rng
    w = World(env)
    w.ov = Ov()
    version = rng.choice([0, 3, 4])
    theme = rng.randrange(4)
    cells_state(w, version, theme)
    # (the special ids the piece tables are set up from are poked by EditorPieces_Setup itself)
    return [theme], w.ov.pokes()


GROUP_STUBS: dict = {"lgk": lg_stubs}
