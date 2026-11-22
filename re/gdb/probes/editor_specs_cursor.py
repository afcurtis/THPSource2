"""Phase 4.11d case generators, part 3: the editor's cursor with the placing, removing and rotating of pieces (group `edk`, 0x004377d0..0x00439540).

The cursor object (0x15f0 bytes: four elements of 0x94 bytes, its fields, 0x40 object records) is made up in the scratch slab with a small park around it: the park generator
object's source models (six of 0x24 bytes) and its map of placed pieces, the editor's cell rows, the item records, the pieces themselves and the object records of the
region the models are drawn from.  Everything outside the file is a stub: the generator's operations (4.11c), the piece tables (4.11b), the budgets, the sine and cosine
of the engine, the renderer and the vector constructors.

Slab layout (offsets from the scratch slab):
  0x0000 the cursor (0x1600)   0x1600 the generator object (partly captured)   0x5400 cell rows (0x100) and cells (0x400)   0x5a00 map rows (0x100) and map (0x500)
  0x6000 pieces (12 of 0x240)  0x8100 item records (0x1c0)   0x8300 object records (8 of 0x4c)   0x8600 fog words and model flag words (0x100 each)   0x8800 out slots
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from editor_specs import Img, a
from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem
from pspool_specs import ensure, under_test

CUR = 0x0000
LG = 0x1600
CELL_ROWS, CELLS = 0x5400, 0x5500
MAP_ROWS, MAP = 0x5A00, 0x5B00
PIECES, PIECE_STRIDE = 0x6000, 0x240
ITEMS = 0x8100
OBJECTS = 0x8300
FOGS, FLAGS = 0x8600, 0x8700
OUT = 0x8800
SLAB = 0x8A00
MODELS = 6
SCRATCH_LIMIT = 0x7FF00950

GRID_W, GRID_D, CELL_X, CELL_Z = 0x00528398, 0x0052839C, 0x00561488, 0x0056148C
ROWS_PTR, ITEMS_PTR = 0x005614D4, 0x005614D8
SPECIAL_BASE, SPECIAL_A, SPECIAL_B, SPECIAL_C = 0x005614A8, 0x005614B8, 0x005614A0, 0x00561184
START_PIECE, GAP_PIECE, POLY_FLAG = 0x005611A8, 0x0056116C, 0x00564384
OBJECT_TABLES = 0x0056D438

STATICS = [(GRID_W, 8), (CELL_X, 8), (ROWS_PTR, 8), (0x005614A0, 0x28), (0x00561184, 4), (START_PIECE, 8), (GAP_PIECE, 4), (POLY_FLAG, 4), (OBJECT_TABLES, 0x110), (0x00518910, 4)]
STATE: dict = {}


def reset():
    STATE.clear()


def cursor_regions(slab):
    return ([(slab + CUR, 0x1600), (slab + LG, 0x140), (slab + LG + 0x2340, 0x10), (slab + LG + 0x3690, 0x10), (slab + CELL_ROWS, 0x100), (slab + CELLS, 0x400), (slab + MAP_ROWS, 0x100),
             (slab + MAP, 0x500), (slab + PIECES, 12 * PIECE_STRIDE), (slab + ITEMS, 0x1C0), (slab + OBJECTS, 0x300), (slab + FOGS, 0x200), (slab + OUT, 0x100)] + STATICS)


SLAB_BYTES["edk"] = SLAB
EXTRA_REGIONS["edk"] = cursor_regions
READ_ONLY["edk"] = []
SEEDS["edk"] = lambda env, rng: {}


def world(env, w, pieces=None):
    """A small park under edit: the generator's models, cells and the map of pieces; the cursor over it."""
    rng = env.rng
    W = rng.choice([4, 4, 6, 8])
    D = rng.choice([4, 4, 6, 8])
    w.u32(GRID_W, W)
    w.u32(GRID_D, D)
    w.u32(CELL_X, rng.choice([0x154, 0x154, 0x100, 0x20]))
    w.u32(CELL_Z, rng.choice([0xAA, 0x80]))
    lg = a(env, LG)
    w.garbage(lg, 0x140, rng)
    w.u32(lg + 0x14, rng.randrange(0, 3))
    # the source models: flags, size, a base for the solid ones
    for m in range(MODELS):
        at = lg + 0x18 + 0x24 * m
        w.u32(at + 4, a(env, FLAGS + 0x10 * m))
        w.u8(at + 8, rng.randrange(0, 20))
        w.u8(at + 9, rng.choice([0, 0x10, 0x20, 0x40, 0x50, 0x08, 0x18, rng.randrange(256)]))
        # (the sizes are signed bytes; a negative width or depth makes the footprint negative, and the base indicator then writes before the cursor, so only the heights go below zero)
        w.u8(at + 0xA, rng.choice([1, 1, 2, 3, 0]))
        w.u8(at + 0xB, rng.choice([0, 1, 1, 2, 3, 0xFE]))
        w.u8(at + 0xC, rng.choice([1, 1, 2, 3, 0]))
        w.u8(at + 0xD, rng.choice([1, 1, 2, 3, 4, 0xFD]))
        w.u16(at + 0xE, rng.randrange(1 << 16))
        w.u16(at + 0x12, rng.randrange(1 << 16))
        w.put(a(env, FLAGS + 0x10 * m), rng.randbytes(4))
    # the object records of the region (the slot the generator is in)
    slot = int(w.bytes[lg + 0x14]) if lg + 0x14 in w.bytes else 0
    entry = OBJECT_TABLES + slot * 0x44
    w.u32(entry, a(env, OBJECTS))
    w.garbage(a(env, OBJECTS), 0x300, rng)
    STATE["specials"] = {}
    for name in (SPECIAL_BASE, SPECIAL_A, SPECIAL_B, SPECIAL_C, START_PIECE, GAP_PIECE):
        STATE["specials"][name] = rng.randrange(0, MODELS)
        w.u32(name, STATE["specials"][name])
    # the cell rows and the map
    w.u32(ROWS_PTR, a(env, CELL_ROWS))
    w.u32(lg + 0x2348, a(env, MAP_ROWS))
    for z in range(D):
        w.u32(a(env, CELL_ROWS) + 4 * z, a(env, CELLS) + z * W * 16)
        w.u32(a(env, MAP_ROWS) + 4 * z, a(env, MAP) + z * W * 20)
    w.garbage(a(env, CELLS), W * D * 16, rng)
    for z in range(D):
        for x in range(W):
            cell = a(env, CELLS) + (z * W + x) * 16
            for k in range(5):
                w.u16(cell + 2 * k, rng.choice([0xFFFF, 0xFFFF, rng.randrange(1 << 16)]))       # (a stale reference shows whether the placing empties the layers above a tall piece)
            w.u8(cell + 0xF, rng.choice([0, 1, 1, 2, 3]))
    w.put(a(env, MAP), bytes(W * D * 20))
    # pieces on the map (their cells' layer counts cover them)
    count = rng.randrange(0, 9) if pieces is None else pieces
    placed = []
    for i in range(count):
        z, x = rng.randrange(D), rng.randrange(W)
        layer = rng.randrange(0, 3)
        slot_at = a(env, MAP) + (z * W + x) * 20 + 4 * layer
        if any(p["slot"] == slot_at for p in placed):
            continue
        at = a(env, PIECES + PIECE_STRIDE * i)
        w.garbage(at, 0x240, rng)
        m = rng.randrange(0, MODELS)
        w.u8(at + 5, x)
        w.u8(at + 6, layer)
        w.u8(at + 7, z)
        w.u8(at + 0xC, rng.randrange(0, 4))
        w.u32(at + 0x224, lg + 0x18 + 0x24 * m)
        w.u32(at + 0x228, a(env, FOGS + 0x10 * i))
        w.u32(at + 0x234, rng.choice([0, 0, a(env, ITEMS)]))
        w.put(a(env, FOGS + 0x10 * i), rng.randbytes(4))
        w.u32(slot_at, at)
        cell = a(env, CELLS) + (z * W + x) * 16
        w.u8(cell + 0xF, max(layer + 1, 1) if rng.random() < 0.85 else rng.randrange(0, 4))
        placed.append({"slot": slot_at, "at": at, "x": x, "z": z, "layer": layer})
    # the item records
    items = a(env, ITEMS)
    w.u32(ITEMS_PTR, items)
    w.garbage(items, 0x1B8, rng)
    for i in range(10):
        w.u8(items + 0x2C * i + 0xC, rng.choice([0x80, 0x81, 0, 1, 2]))
        if placed and rng.random() < 0.5:
            p = rng.choice(placed)
            end = rng.randrange(2)
            w.u8(items + 0x2C * i + end, p["x"])
            w.u8(items + 0x2C * i + 2 + end, p["layer"])
            w.u8(items + 0x2C * i + 4 + end, p["z"])
    w.u32(lg + 0x3698, rng.randrange(0, 8))
    w.u32(lg + 0x2348, a(env, MAP_ROWS))
    w.u32(POLY_FLAG, rng.randrange(1 << 32))
    return W, D, placed


def cursor_object(env, w, W, D, placed, model=None):
    rng = env.rng
    c = a(env, CUR)
    w.garbage(c, 0x1600, rng)
    lg = a(env, LG)
    w.u32(c + 0x2DC, lg)
    w.u32(c + 0x254, rng.randrange(0, 4) if rng.random() < 0.97 else rng.choice([4, 5, 0x101]))       # (a turn past the last quarter is skipped by the switches)
    fw, fd = rng.choice([1, 1, 2, 3]), rng.choice([1, 1, 2, 3])
    w.u32(c + 0x264, fw)
    w.u32(c + 0x26C, fd)
    cx = rng.randrange(0, W) if rng.random() < 0.1 else rng.randrange(0, W - fw + 1)
    cz = rng.randrange(0, D) if rng.random() < 0.1 else rng.randrange(0, D - fd + 1)
    if placed and rng.random() < 0.6:
        p = rng.choice(placed)
        cx, cz = p["x"], p["z"]
    w.u32(c + 0x258, cx)
    w.u32(c + 0x260, cz)
    w.u32(c + 0x270, (cx + rng.choice([0, 1, -1, 2])) & 0xFFFFFFFF)
    w.u32(c + 0x274, rng.randrange(0, 3))
    w.u32(c + 0x278, (cz + rng.choice([0, 1, -1, 2])) & 0xFFFFFFFF)
    w.u32(c + 0x250, rng.randrange(0, MODELS))
    w.u32(c + 0x25C, rng.randrange(0, 3))
    # the elements: the first one holds the model in hand, the others sometimes carry pieces along
    m0 = rng.randrange(0, MODELS) if model is None else model
    for k in range(4):
        e = c + 0x94 * k
        used = k == 0 or rng.random() < 0.3
        w.u32(e + 0x4C, m0 if k == 0 else (rng.randrange(0, MODELS) if used else 0xFFFFFFFF))
        for off in (0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84):
            w.u32(e + off, rng.choice([0, 1, 2, 3, 0xFFFFFFFF, rng.randrange(-4, 6) & 0xFFFFFFFF]))
    for off in (0x27C, 0x280, 0x284, 0x288):
        w.u32(c + off, rng.choice([0, 1, 2, 3, 4]))
    w.u32(c + 0x28C, rng.randrange(0, 0x3000))
    w.u32(c + 0x290, rng.randrange(0, 0x3000))
    w.u32(c + 0x2A4, rng.choice([0, 1, 2, 3, 4]))
    w.u8(c + 0x2A8, rng.choice([0, 1]))
    w.u8(c + 0x2EC, rng.choice([0, 1]))
    for off in (0x294, 0x298, 0x29C, 0x2A0, 0x2AC, 0x2B0, 0x2B4, 0x2B8, 0x2BC, 0x2C0, 0x2CC, 0x2D0, 0x2D8):
        w.u32(c + off, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    w.u32(c + 0xE0, rng.randrange(0, MODELS))
    w.u32(c + 0x8C, rng.randrange(-300, 300) & 0xFFFFFFFF)
    return c


def kspec(address, name, this=True):
    return spec(address, name, "edk", this=(lambda env: env.slab + CUR) if this else None)


def make(env, pieces=None, model=None):
    specials = STATE.get("specials")
    reset()
    w = Img()
    W, D, placed = world(env, w, pieces)
    cursor_object(env, w, W, D, placed, model)
    STATE["budget"] = env.rng.choice([0, 1, 5, 100])
    STATE["spare"] = env.rng.choice([0, 1, 5, 100])
    STATE["room"] = env.rng.choice([0, 1, 5, 100])
    return w, W, D, placed


@kspec(0x004377D0, "EditorCursor_Construct")
def gen_construct(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00437840, "EditorCursor_ElementConstruct")
def gen_element_construct(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00437850, "EditorCursor_ElementDestruct")
def gen_element_destruct(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00437860, "EditorCursor_Destruct")
def gen_destruct(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x004378D0, "EditorCursor_Init")
def gen_init(env, n):
    w, *_ = make(env)
    return [a(env, LG)], w.pokes()


@kspec(0x00437950, "EditorCursor_SetModel")
def gen_set_model(env, n):
    w, W, D, placed = make(env)
    return [env.rng.randrange(0, MODELS)], w.pokes()


@kspec(0x00437C30, "EditorCursor_Rotate")
def gen_rotate(env, n):
    w, *_ = make(env)
    return [env.rng.choice([1, 1, 3, 0xFFFFFFFF, 2, 0])], w.pokes()


@kspec(0x00437CD0, "EditorCursor_MoveTo")
def gen_move_to(env, n):
    w, W, D, placed = make(env)
    rng = env.rng
    return [rng.randrange(-1, W + 1) & 0xFFFFFFFF, rng.randrange(0, 4), rng.randrange(-1, D + 1) & 0xFFFFFFFF], w.pokes()


@kspec(0x00437D30, "EditorCursor_Step")
def gen_step(env, n):
    w, *_ = make(env)
    return [env.rng.randrange(0, 16)], w.pokes()


@kspec(0x00437DC0, "EditorCursor_Update")
def gen_update(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00438320, "EditorCursor_MarkGaps")
def gen_mark_gaps(env, n):
    w, W, D, placed = make(env)
    c = a(env, CUR)
    if env.rng.random() < 0.6:
        w.u32(c + 0x4C, STATE["specials"][GAP_PIECE])
    return [], w.pokes()


@kspec(0x00438360, "EditorCursor_FindItem")
def gen_find_item(env, n):
    w, W, D, placed = make(env)
    return [env.rng.choice([0, a(env, OUT)])], w.pokes()


@kspec(0x00438400, "EditorCursor_Scaled")
def gen_scaled(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00438410, "EditorCursor_Animate")
def gen_animate(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x004385B0, "EditorCursor_Draw")
def gen_draw(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00438610, "EditorCursor_Place")
def gen_place(env, n):
    w, W, D, placed = make(env, pieces=env.rng.randrange(0, 4))
    c = a(env, CUR)
    rng = env.rng
    STATE["budget"] = rng.choice([1, 5, 100, 100, 0])
    STATE["spare"] = rng.choice([1, 5, 100, 100, 0])
    STATE["room"] = rng.choice([1, 5, 100, 100, 0])
    STATE["match"] = [rng.choice([0, 1, 1, 1]) for _ in range(8)]
    w.u32(c + 0x58, rng.choice([0, 0, 0, 1, 0xFFFFFFFF]))
    if rng.random() < 0.75:
        # a level, free footprint (the same number of layers under every cell, nothing on the map at that layer) so that the placing can go through
        cx, cz = mem_or(w, c + 0x258), mem_or(w, c + 0x260)
        fw, fd = mem_or(w, c + 0x264), mem_or(w, c + 0x26C)
        layers = rng.choice([1, 1, 2, 3])
        for z in range(cz, min(cz + fd, D)):
            for x in range(cx, min(cx + fw, W)):
                w.u8(a(env, CELLS) + (z * W + x) * 16 + 0xF, layers)
                for k in range(5):
                    w.u32(a(env, MAP) + (z * W + x) * 20 + 4 * k, 0)
        w.u32(c + 0x4C, rng.randrange(0, MODELS))
        # pieces next to the footprint on the layer the piece would go to, so that the colours of the neighbours are compared
        floor = layers - 1
        y = (mem_or(w, c + 0x58) + floor) & 0xFFFFFFFF
        if y < 5:
            for k, (dx, dz) in enumerate(((-1, 0), (fw, 0), (0, -1), (0, fd))):
                x, z = cx + dx, cz + dz
                if 0 <= x < W and 0 <= z < D and rng.random() < 0.6:
                    at = a(env, PIECES + PIECE_STRIDE * (8 + k))
                    w.garbage(at, 0x240, rng)
                    w.u8(at + 5, x)
                    w.u8(at + 6, y)
                    w.u8(at + 7, z)
                    w.u8(at + 0xC, rng.randrange(0, 4))
                    w.u32(at + 0x224, a(env, LG) + 0x18 + 0x24 * rng.randrange(0, MODELS))
                    w.u32(at + 0x228, a(env, FOGS + 0x10 * (8 + k)))
                    w.u32(at + 0x234, 0)
                    w.u32(a(env, MAP) + (z * W + x) * 20 + 4 * y, at)
    return [], w.pokes()


def mem_or(w, address):
    return int.from_bytes(bytes(w.bytes[address + k] for k in range(4)), "little")


@kspec(0x00438C50, "EditorCursor_TryAddItem")
def gen_try_add(env, n):
    w, W, D, placed = make(env)
    STATE["budget"] = env.rng.choice([0, 1, 5])
    STATE["added"] = env.rng.choice([0, 1, 9, 0xFFFFFFFF])
    return [env.rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 1, 3]), a(env, OUT)], w.pokes()


@kspec(0x00438CE0, "EditorCursor_RemoveSelected")
def gen_remove_selected(env, n):
    w, W, D, placed = make(env)
    STATE["removed"] = [env.rng.choice([0, 0, 1]) for _ in range(40)]
    return [], w.pokes()


@kspec(0x00438DC0, "EditorCursor_RemoveItemUnder")
def gen_remove_item(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00438E00, "EditorCursor_TopPiece")
def gen_top_piece(env, n):
    w, *_ = make(env)
    return [], w.pokes()


@kspec(0x00438E60, "EditorCursor_FlagPieces")
def gen_flag_pieces(env, n):
    w, W, D, placed = make(env)
    c = a(env, CUR)
    rng = env.rng
    w.u32(c + 0x258, rng.randrange(-1, W) & 0xFFFFFFFF)
    w.u32(c + 0x260, rng.randrange(-1, D) & 0xFFFFFFFF)
    return [rng.choice([0, 1, 0x100, 0x1])], w.pokes()


@kspec(0x00439110, "EditorCursor_SetLabel")
def gen_set_label(env, n):
    w, *_ = make(env)
    return [env.rng.randrange(1 << 32), env.rng.randrange(1 << 32)], w.pokes()


@kspec(0x00439130, "EditorCursor_Indicator")
def gen_indicator(env, n):
    w, W, D, placed = make(env)
    c = a(env, CUR)
    rng = env.rng
    w.u32(c + 0x250, rng.randrange(0, MODELS))
    for m in range(MODELS):
        at = a(env, LG) + 0x18 + 0x24 * m
        if rng.random() < 0.5:
            w.u8(at + 9, rng.choice([0x40, 0x50, 0x60]))
    fw, fd = rng.choice([1, 2, 3, 4]), rng.choice([1, 2, 3, 4])
    w.u32(c + 0x264, fw)
    w.u32(c + 0x26C, fd)
    # (the footprint stays on the map: a row past the last one is a null pointer)
    w.u32(c + 0x258, rng.randrange(0, W - fw + 1))
    w.u32(c + 0x260, rng.randrange(0, D - fd + 1))
    return [], w.pokes()


@kspec(0x00439540, "EditorCursor_DrawBase")
def gen_draw_base(env, n):
    w, *_ = make(env)
    c = a(env, CUR)
    w.u8(c + 0x2EC, env.rng.choice([0, 1, 1]))
    return [], w.pokes()


# ---- the stubs -------------------------------------------------------------------------------------------------------------------
class FloatResult(TonyBreakpoint):
    """EditorCursor_Scaled leaves its result on the x87 stack: at its `ret` the top of the stack is turned into the bits of a float and left in eax, which the forced call returns."""

    def __init__(self, pop_ret):
        super().__init__(0x0043840C, internal=True)
        self.pop_ret = pop_ret

    def stop(self):
        import gdb
        try:
            value = float(gdb.parse_and_eval("$st0"))
        except Exception:      # noqa: BLE001
            value = float("nan")
        gdb.execute("set $eax = %d" % struct.unpack("<I", struct.pack("<f", value))[0])
        # (the caller would pop the value: a forced call must leave the x87 stack empty, or the ninth call overflows it and every result is the indefinite NaN: the return
        # goes through `fstp st(0); ret` written into the slab, where the debugger's stub cannot be asked to reset the stack itself)
        gdb.execute("set $eip = %d" % self.pop_ret)
        return False


def cursor_stubs(fc, env):
    if getattr(fc, "_cursor_stubs", False):
        return
    fc._cursor_stubs = True
    under_test()
    from opentony.fcall import write_chunks
    pop_ret = env.slab + 0x8980
    write_chunks(pop_ret, bytes([0xDD, 0xD8, 0xC3]))
    FloatResult(pop_ret)
    rng = env.rng
    ensure(fc, 0x00500400, 5, convention="stdcall")
    ensure(fc, 0x0050030C, 4, convention="stdcall")
    ensure(fc, 0x0047FDB0, 0, convention="thiscall", this=True)
    ensure(fc, 0x0047FE20, 0, convention="thiscall", this=True)
    ensure(fc, 0x0043F5A0, 1, convention="thiscall", this=True)
    ensure(fc, 0x0043F1B0, 2, convention="thiscall", this=True)
    ensure(fc, 0x0043F400, 2, convention="thiscall", this=True)
    ensure(fc, 0x0043FDA0, 1, convention="thiscall", this=True)
    ensure(fc, 0x0045F530, 1)
    ensure(fc, 0x004F39B0, 1, result=lambda a_: rng.choice([0, 4096, rng.randrange(-4096, 4096) & 0xFFFFFFFF]))
    ensure(fc, 0x004F3980, 1, result=lambda a_: rng.choice([0, 4096, rng.randrange(-4096, 4096) & 0xFFFFFFFF]))
    ensure(fc, 0x00435F60, 0, result=lambda a_: STATE.get("budget", 1))
    ensure(fc, 0x00440170, 0, result=lambda a_: STATE.get("spare", 1))
    ensure(fc, 0x0043F710, 0, result=lambda a_: STATE.get("room", 1))
    ensure(fc, 0x0043CC30, 4, convention="stdcall", result=lambda a_: STATE.get("added", 0))
    ensure(fc, 0x0043CF20, 4, convention="thiscall", this=True, stack_to={2: SCRATCH_LIMIT}, masks={3: 0xFF},
           result=lambda a_: STATE["removed"].pop(0) if STATE.get("removed") else 0)
    ensure(fc, 0x0043CE00, 3, convention="stdcall")
    ensure(fc, 0x00444450, 3, result=lambda a_: rng.randrange(0, 1000))

    def matches(args):
        values = STATE.get("match")
        return values.pop(0) if values else 1

    ensure(fc, 0x00444510, 2, result=matches)
    ensure(fc, 0x0043C770, 5, convention="thiscall", this=True)


GROUP_STUBS: dict = {"edk": cursor_stubs}
reset()
