"""Phase 4.11d case generators, part 2: the editor's camera (group `edc`, 0x00437270..0x00437720) and its interface object, the piece library's selection and the HUD
(group `edu`, 0x00436520..0x00437220).

The camera is an object of its own in the scratch slab; the engine's rotation and matrix routines are stubs that write scripted bytes (a matrix of 32 bytes, a position of
three words) through their pointer arguments and report the structure they were handed.  The interface object (0x470 bytes with a font object at +0x184) carries fake panel
elements whose virtual methods are reported; the piece library and category tables at 0x00528820 and 0x00529e38 are poked per case with a small library.

Slab layout (offsets from the scratch slab):
  0x0000 the camera (0x80)   0x0100 the interface object (0x470)   0x0600 ten panel elements of 0x40   0x0900 the vtables   0x0a00 the polygon arena (0x400)
  0x0e00 a renderer (0x90)   0x0f00 the camera view's out buffers (0x100)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from editor_specs import Img, a
from opentony.memory import mem
from pspool_specs import FAKE_SLOTS, ensure, under_test

CAM, UI = 0x0000, 0x0100
ELEMENTS = 0x0600
VT_ELEM, VT_FONT = 0x0900, 0x0940
ARENA_POLY = 0x0A00
RENDERER, HEAD = 0x0E00, 0x0E90
OUT = 0x0F00
ITEMS_V = 0x1000
MSGS = 0x1200
SLAB = 0x1400
FONT_SLOT = 0x004AD890

SCRATCH_ANGLES, SCRATCH_VECTOR, SCRATCH_MATRIX, SCRATCH_POSITION = 0x7FF00A40, 0x7FF00A50, 0x7FF00A60, 0x7FF00A80

PADS = [0x0056B009, 0x0056B078, 0x0056B088, 0x0056B098, 0x0056B0A8, 0x0056B0E8]
CAMERA_STATICS = [(0x0055F7CC, 0x60), (0x00528398, 8), (0x00561488, 8)] + [(p, 4) for p in PADS]
PIECES, CATEGORIES = 0x00528820, 0x00529E38
UI_STATICS = [(0x005613C0, 0x100), (0x005614C8, 4), (0x005614D8, 4), (0x00560FD0, 8), (0x00568628, 4), (0x005611B8, 4), (0x005283A0, 4), (0x005287C0, 0x340), (CATEGORIES, 0xE0),
              (0x00518850, 0x70), (0x00527D50, 8), (0x0052A680, 0x100), (0x00528398, 8), (0x005256D8, 0x10)]
STATE: dict = {}


def reset():
    STATE.clear()
    STATE.update(width=0)


def view_seed(env, rng):
    # the elements share one vtable (delete, update, draw, place), the font has one (slot 1 draws a line)
    vt = struct.pack("<IIII", FAKE_SLOTS[0], FAKE_SLOTS[1], FAKE_SLOTS[2], FAKE_SLOTS[3])
    font = struct.pack("<II", 0, FONT_SLOT)
    return {env.slab + VT_ELEM: vt, env.slab + VT_FONT: font}


def cam_regions(slab):
    return [(slab + CAM, 0x80), (slab + OUT, 0x100)] + CAMERA_STATICS


def ui_regions(slab):
    out = [(slab + UI, 0x470), (slab + ELEMENTS, 0x300), (slab + ARENA_POLY, 0x400), (slab + RENDERER, 0xA0), (slab + ITEMS_V, 0x1C0), (slab + VT_ELEM, 0x80), (slab + MSGS, 0x140)] + UI_STATICS
    for k in range(5):
        out.append((mem.u32(0x0052E8F8 + 4 * k), 0x80))       # the strings the message pointers name
    out.append((0x0052E8F8, 0x14))
    return out


for _group, _regions in (("edc", cam_regions), ("edu", ui_regions)):
    SLAB_BYTES[_group] = SLAB
    SEEDS[_group] = view_seed
    EXTRA_REGIONS[_group] = _regions
    READ_ONLY[_group] = []


# ---- the camera -----------------------------------------------------------------------------------------------------------------
def camera_object(env, w, quarter=None, state=None):
    rng = env.rng
    c = a(env, CAM)
    w.garbage(c, 0x80, rng)
    w.u32(c, rng.randrange(0, 4) if quarter is None else quarter)
    w.u32(c + 4, rng.choice([0, 0x40, 0x380, 0xFC0, rng.randrange(0, 0x1000)]))
    w.u32(c + 8, rng.randrange(0, 0x3000))
    for k in range(4):
        w.u32(c + 0xC + 4 * k, rng.randrange(0, 0x1000))
        w.u32(c + 0x1C + 4 * k, rng.choice([0x80, 0x400, 0x3E0, 0x400 - 0x20, rng.randrange(0x60, 0x420)]))
    w.u32(c + 0x3C, rng.choice([1, 1, 1, 2]) if state is None else state)
    w.u8(c + 0x40, rng.choice([0, 1]))
    w.u8(c + 0x41, rng.choice([0, 1]))
    w.u32(c + 0x44, rng.choice([0, 1, 0xFFFFFFFF, 5, 0xFFFFFFF0]))
    w.u32(c + 0x48, rng.choice([0, 1, 0xFFFFFFFF, 5, 0xFFFFFFF0]))
    for k in range(3):
        w.u32(c + 0x2C + 4 * k, rng.randrange(-0x2000, 0x2000) & 0xFFFFFFFF)
    for pad in PADS:
        w.u8(pad, rng.choice([0, 0, 1]))
    w.u32(0x00528398, rng.choice([2, 4, 6, 16, 30]))
    w.u32(0x0052839C, rng.choice([2, 4, 6, 16, 30]))
    w.u32(0x00561488, rng.choice([0x154, 0x100, 0x180, 0x10]))
    w.u32(0x0056148C, rng.choice([0xAA, 0x80]))
    for off in (0, 8, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30):
        w.u32(0x0055F7CC + off, rng.choice([0, rng.randrange(-40, 40) & 0xFFFFFFFF, rng.randrange(-0x3000, 0x3000) & 0xFFFFFFFF, 0x3FC]))
    # the point the view keeps clear of the park's edges: sometimes a few units from a window the clamp tests (the half extents are the grid times the cell size over two)
    cell, gw, gd = mem_or(w, 0x00561488), mem_or(w, 0x00528398), mem_or(w, 0x0052839C)
    STATE["want"] = None
    if rng.random() < 0.7:
        # (the view's position is worked out from the engine's answer and the target: ask the stub for the answer that lands the position near an edge the clamp tests)
        want = []
        for half in ((cell * gw) >> 1, (gd * cell) >> 1):
            edge, d = rng.choice([0, 0x3FC]), rng.randrange(0, 30)
            want.append(rng.choice([edge + half - d, d - edge - half, edge + half - d + rng.randrange(-3, 3), rng.randrange(-0x3000, 0x3000)]))
        STATE["want"] = [want[0], rng.randrange(-0x3000, 0x3000), want[1]]
        STATE["target"] = [mem_or(w, c + 0x2C + 4 * k) for k in range(3)]
    return c


def mem_or(w, address):
    return int.from_bytes(bytes(w.bytes[address + k] for k in range(4)), "little")


def cspec(address, name, this=True):
    return spec(address, name, "edc", this=(lambda env: env.slab + CAM) if this else None)


@cspec(0x00437270, "EditorCamera_Init")
def gen_cam_init(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [], w.pokes()


@cspec(0x004372C0, "EditorCamera_Nothing", this=False)
def gen_cam_nothing(env, n):
    reset()
    return [], {}


@cspec(0x004372D0, "EditorCamera_SetTarget")
def gen_cam_target(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [env.rng.randrange(1 << 32) for _ in range(3)], w.pokes()


@cspec(0x004372F0, "EditorCamera_NextQuarter")
def gen_cam_next(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [], w.pokes()


@cspec(0x00437310, "EditorCamera_Update")
def gen_cam_update(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [], w.pokes()


@cspec(0x00437580, "EditorCamera_View")
def gen_cam_view(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [env.rng.randrange(0, 1 << 16), a(env, OUT + 0x10), a(env, OUT + 0x60)], w.pokes()


@cspec(0x00437650, "EditorCamera_Direction")
def gen_cam_direction(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    return [env.rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, env.rng.randrange(1 << 32)])], w.pokes()


@cspec(0x00437720, "EditorCamera_Look")
def gen_cam_look(env, n):
    reset()
    w = Img()
    camera_object(env, w)
    w.garbage(a(env, OUT), 8, env.rng)
    return [a(env, OUT)], w.pokes()


# ---- the interface object and the piece library -----------------------------------------------------------------------------------
def make_library(env, w, pieces=None, categories=None):
    """A small piece library: unique first codes, four codes a piece, names, flags; categories that split it; the last-used byte of each category."""
    rng = env.rng
    p = rng.randrange(5, 14) if pieces is None else pieces
    k = rng.randrange(2, 5) if categories is None else categories
    cuts = sorted(rng.sample(range(1, p), k - 1))
    starts = [0] + cuts
    ends = cuts + [p]
    codes0 = rng.sample(range(0x30, 0x7F), p + 1)
    for i in range(p):
        at = PIECES + 0x25 * i
        rec = bytes([codes0[i]] + [rng.randrange(0x30, 0x7F) for _ in range(3)])
        name = bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 12))) + b"\0"
        flags = rng.choice([0, 0x80, 0x81, 0x01, 0x11])
        if i in starts:
            flags &= 0x7F        # (every category keeps a piece that can be chosen, or stepping through it would never end)
        rec += name + bytes(0x20 - len(name)) + bytes([flags])
        w.put(at, rec)
    # the sentinel after the last piece (the end code of the last category)
    w.put(PIECES + 0x25 * p, bytes([codes0[p]]) + bytes(0x24))
    for c in range(k):
        at = CATEGORIES + 0x22 * c
        name = bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 10))) + b"\0"
        w.put(at, bytes([codes0[starts[c]]]) + name + bytes(0x20 - len(name)) + bytes([rng.choice([0, 0x30, 0x31])]))
    w.put(CATEGORIES + 0x22 * k, bytes([codes0[p]]) + bytes(0x21))
    return p, k, starts, ends


def ui_object(env, w, library=True, nulls=False):
    rng = env.rng
    u = a(env, UI)
    w.garbage(u, 0x470, rng)
    w.u32(u, rng.randrange(-300, 300) & 0xFFFFFFFF)
    w.u32(u + 4, rng.randrange(-300, 300) & 0xFFFFFFFF)
    w.u32(u + 8, rng.randrange(0, 300))
    w.u32(u + 0xC, rng.randrange(8, 20))
    w.u32(u + 0x10, rng.randrange(0, 4))
    # the elements
    els = [a(env, ELEMENTS + 0x40 * k) for k in range(10)]
    for e in els:
        w.u32(e, a(env, VT_ELEM))
        w.u32(e + 0xC, rng.randrange(1 << 32))
    for off, e in ((0x134, els[0]), (0x13C, els[1]), (0x15C, els[8]), (0x160, els[9])):
        w.u32(u + off, e if not nulls or rng.random() < 0.9 else 0)
    for k in range(7):
        w.u32(u + 0x144 + 4 * k, els[2 + k] if not nulls or rng.random() < 0.9 else 0)
    # the four lines of text the interface keeps (the set's line and name, the piece's line and name)
    for off, size in ((0x14, 0x28), (0x3C, 0x28), (0x74, 0x28), (0x9C, 0x28)):
        text = bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + b"\0"
        w.put(u + off, text + bytes(rng.randrange(256) for _ in range(size - len(text))))
    w.u32(u + 0x138, rng.randrange(0, 200))
    w.u32(u + 0x140, rng.randrange(0, 300))
    w.u32(u + 0x184, a(env, VT_FONT))
    w.u32(u + 0x188, rng.randrange(0, 3))
    w.u32(u + 0x18C, rng.randrange(0, 3))
    w.u32(a(env, HEAD), rng.randrange(1 << 32))
    w.u32(a(env, RENDERER) + 0x84, a(env, HEAD))
    w.u32(0x00560FD4, a(env, RENDERER))
    if library:
        p, k, starts, ends = make_library(env, w)
        w.u32(u + 0x174, p - 1)
        w.u32(u + 0x178, k)
        w.u32(u + 0x164, rng.randrange(0, 4))
        w.u32(u + 0x168, rng.choice([4, 4, 0xFFFFFFFF, 1]))
        c = rng.randrange(0, k)
        w.u32(u + 0x16C, c)
        w.u32(u + 0x17C, starts[c])
        w.u32(u + 0x180, ends[c] - starts[c])
        w.u32(u + 0x170, rng.randrange(starts[c], ends[c]))
        return u, p, k, starts, ends
    return u, 0, 0, [], []


def uspec(address, name, group="edu"):
    return spec(address, name, group, this=lambda env: env.slab + UI)


@uspec(0x00436E00, "SelectManager__PieceSelectable")
def gen_selectable(env, n):
    reset()
    w = Img()
    u, p, k, starts, ends = ui_object(env, w)
    return [env.rng.randrange(0, p + 1)], w.pokes()


@uspec(0x00436C60, "SelectManager__SetPiece")
def gen_set_piece(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    return [rng.choice([rng.randrange(starts[0], ends[-1])] * 3 + [starts[rng.randrange(0, k)], 0, p])], w.pokes()


@uspec(0x00436DA0, "SelectManager__StepPiece")
def gen_step_piece(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    return [rng.choice([1, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0, 2, 0xFFFFFFFE])], w.pokes()


@uspec(0x00436E30, "SelectManager__SetCategory")
def gen_set_category(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    c = rng.randrange(0, k)
    # the byte the category remembers: a piece code of the shown library (so the search finds it), or something else
    lib = rng.randrange(0, 4)
    w.u32(u + 0x164, lib)
    pick = rng.randrange(starts[c], ends[c])
    code = w_code(w, pick, lib)
    w.u8(CATEGORIES + 0x22 * c + 0x21, code if rng.random() < 0.93 else 0x2F)
    return [c], w.pokes()


def w_code(w, piece, lib):
    return w.bytes[PIECES + 0x25 * piece + lib]


@uspec(0x00436FA0, "SelectManager__ChangeCategory")
def gen_change_category(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    lib = rng.randrange(0, 4)
    w.u32(u + 0x164, lib)
    for c in range(k):
        w.u8(CATEGORIES + 0x22 * c + 0x21, w_code(w, rng.randrange(starts[c], ends[c]), lib))
    return [rng.choice([1, 0xFFFFFFFF, 1, 0xFFFFFFFF, 2, 0, 0xFFFFFFFE])], w.pokes()


@uspec(0x004371D0, "SelectManager__PieceChar")
def gen_piece_char(env, n):
    reset()
    w = Img()
    u, p, k, starts, ends = ui_object(env, w)
    return [], w.pokes()


@uspec(0x004371F0, "SelectManager__SetLibrary")
def gen_set_library(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    return [rng.choice([0xFFFFFFFF, 0, 1, 3]), rng.randrange(0, 5)], w.pokes()


@uspec(0x00437220, "LevelEditor_SetLimits")
def gen_set_limits(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w, library=False)
    w.u32(0x005283A0, rng.randrange(0, 5))
    w.u32(0x005614C8, rng.randrange(0, 4))
    return [], w.pokes()


@uspec(0x00436FD0, "LevelEditor_UiLayout")
def gen_layout(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w, library=False)
    w.u32(0x00527D50, rng.randrange(0, 100))
    w.u32(0x00527D54, rng.randrange(0, 100))
    return [rng.choice([0, 1, 2, 3, 3, 4, 0xFFFFFFFF, w.bytes[u + 0x10]])], w.pokes()


@uspec(0x00436520, "LevelEditor_UiDestruct")
def gen_ui_destruct(env, n):
    reset()
    w = Img()
    ui_object(env, w, library=False, nulls=True)
    return [], w.pokes()


@uspec(0x004369D0, "LevelEditor_UiUpdate")
def gen_ui_update(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w, library=False)
    at = rng.randrange(-300, 300)
    w.u32(u, at & 0xFFFFFFFF)
    w.u32(u + 4, (at + rng.choice([0, 1, -1, 2, 3, -3, 40, -90])) & 0xFFFFFFFF)
    bar = rng.randrange(0, 300)
    w.u32(u + 0x41C, bar)
    w.u32(u + 0x420, (bar + rng.choice([0, 1, -1, 2, -2, 50, -50])) & 0xFFFFFFFF)
    kind = rng.randrange(0, 5)
    w.u32(u + 0x430, kind)
    w.u32(u + 0x434, rng.choice([kind, kind, 0, 1, 2, 3, 4, 3, 5]))
    w.u8(u + 0x438, rng.choice([0, 1, 1]))
    index = rng.randrange(0, 10)
    w.u32(u + 0x43C, rng.choice([index, index, rng.randrange(0, 10)]))
    w.u32(u + 0x454, index)
    w.u32(u + 0x440, rng.choice([0, 0, 1, 1, 2, 2, 2, 3]))
    x = rng.randrange(0, 0x220)
    w.u32(u + 0x444, x)
    w.u32(u + 0x448, rng.choice([rng.randrange(0, 0x220), x, x + 1, x - 1]))
    # the message texts the kinds name, and the item records the fourth kind quotes
    items = a(env, ITEMS_V)
    for k in range(5):
        if rng.random() < 0.7:
            at = a(env, MSGS + 0x40 * k)
            w.put(at, bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 40))) + bytes([0]))
            w.u32(0x0052E8F8 + 4 * k, at)
    w.u32(0x005614D8, items)
    w.garbage(items, 0x1B8, rng)
    for i in range(10):
        name = bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + b"\0"
        w.put(items + 0x2C * i + 0xD, name)
    return [], w.pokes()


@uspec(0x004365E0, "LevelEditor_UiDraw")
def gen_ui_draw(env, n):
    reset()
    w = Img()
    rng = env.rng
    u, p, k, starts, ends = ui_object(env, w)
    w.u32(u + 0x41C, rng.randrange(0, 300))
    w.u32(u + 0x424, rng.choice([0, 0, 5, 12, 3, 2]))
    w.u32(u + 0x428, rng.randrange(50, 600))
    w.u32(u + 0x42C, rng.randrange(50, 4000))
    w.u32(u + 0x430, rng.choice([0, 1, 3, 4]))
    w.u32(u + 0x444, rng.choice([0x208, 0x100, rng.randrange(0, 0x208)]))
    w.u32(u + 0x44C, rng.randrange(0, 200))
    w.u32(u + 0x450, rng.randrange(0, 40))
    w.u8(0x005611B8, rng.choice([0, 1]))
    w.put(0x005613C0, bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 40))) + b"\0")
    w.put(0x0056145C, bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 40))) + b"\0")
    # the polygon arena: a cursor with room, or none
    arena = a(env, ARENA_POLY)
    cursor = rng.choice([arena, arena + 0x100, arena + 0x3C0, arena + 0x3F0, arena + 0x400])
    end = rng.choice([arena + 0x3F0, arena + 0x3F0, arena + 0x3FF, arena + 0x3C0])
    w.u32(0x00560FD0, cursor)
    w.u32(0x00568628, end)
    w.garbage(arena, 0x400, rng)
    limit_p, limit_z = mem_or(w, u + 0x428), mem_or(w, u + 0x42C)
    fills = [rng.choice([0, 0x40, 0x76, 0x77, 0x8E, 0x8F, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, rng.randrange(0, 0xA0)]) for _ in range(3)]
    STATE["pieces"] = max(0, limit_p - (fills[0] * limit_p) // 159) if rng.random() < 0.9 else rng.choice([0, limit_p + 5])
    STATE["nodes"] = max(0, 0x8C0 - (fills[1] * 0x8C0) // 159) if rng.random() < 0.9 else rng.choice([0, 0x8C0 + 5])
    STATE["zone"] = max(0, limit_z - (fills[2] * limit_z) // 159) if rng.random() < 0.9 else rng.choice([0, limit_z + 5])
    return [], w.pokes()


# ---- the stubs -------------------------------------------------------------------------------------------------------------------
def view_stubs(fc, env):
    if getattr(fc, "_view_stubs", False):
        return
    fc._view_stubs = True
    under_test()
    rng = env.rng

    def width(args):
        text = mem.cstring(args[0], 256)
        return sum(6 + (ord(c) % 5) for c in text)

    def extent(args):
        return 8 + len(mem.cstring(args[0], 256)) % 7

    ensure(fc, 0x004AD620, 3)                                        # SFX_PlayX
    def matrix(args):
        data = bytearray(rng.randrange(256) for _ in range(0x20))
        for offset in (4, 0xA, 0x10):         # (the third column: the direction the view looks along: zero, small or any)
            data[offset:offset + 2] = struct.pack("<h", rng.choice([0, 0, 5, -5, rng.randrange(-4096, 4096)]))
        return bytes(data)

    ensure(fc, 0x004E80E0, 2, blob=(0, 6), fill=(1, matrix), stack_to={0: SCRATCH_ANGLES, 1: SCRATCH_MATRIX})
    def offset(args):
        want, target = STATE.get("want"), STATE.get("target")
        if want and target:
            return b"".join(struct.pack("<I", ((w_ << 12) - t) & 0xFFFFFFFF) for w_, t in zip(want, target))
        return bytes(rng.randrange(256) for _ in range(12))

    ensure(fc, 0x004E85A0, 3, blob=(1, 12), fill=(2, offset),
           stack_to={0: SCRATCH_MATRIX, 1: SCRATCH_VECTOR, 2: SCRATCH_POSITION})
    ensure(fc, 0x0044A4D0, 1, convention="thiscall", this=True, text=True, result=width)
    ensure(fc, 0x0044A540, 1, convention="thiscall", this=True, text=True, result=extent)
    ensure(fc, 0x0044B0A0, 1, text=True)
    ensure(fc, 0x0044A060, 0, convention="thiscall", this=True)
    ensure(fc, 0x00435F60, 0, result=lambda a_: STATE.get("pieces", 0))
    ensure(fc, 0x00440170, 0, result=lambda a_: STATE.get("nodes", 0))
    ensure(fc, 0x0043F710, 0, result=lambda a_: STATE.get("zone", 0))
    ensure(fc, 0x004E8940, 5)
    ensure(fc, 0x00456790, 7)
    ensure(fc, 0x00456600, 7)
    ensure(fc, FAKE_SLOTS[0], 1, convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[1], 0, convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[2], 2, convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[3], 3, convention="thiscall", this=True)
    ensure(fc, FONT_SLOT, 4, convention="thiscall", this=True, text=2)


GROUP_STUBS: dict = {"edc": view_stubs, "edu": view_stubs}
reset()
