"""Phase 4.11e case generators: the park editor's object and its loop (leveleditor.cpp, 0x004334d0..0x0043651f): groups `eoc` (the constructors, the destructor, the budget),
`eod` (update and draw) and `eom` (the pad, the gap adjustment, the menus, placing and removing).

The editor object (0x1dc bytes), the interface object (0x470), the camera (0x4c), the cursor (0x15f0) and the park (the generator object, of which only the two fields the
editor reads are real) live in the scratch slab.  Everything the editor calls outside these 14 functions is a stub that reports its call and answers a scripted value: the park
generator (4.11c), the camera, the cursor and the interface (4.11d), the menu toolkit and the memory card.  The menus are fake objects whose virtual methods are unreached
retail entry points; the five kinds of vtable are poked into the slab.

Slab layout (offsets from the scratch slab):
  0x0000 the editor (0x200)  0x0200 the interface (0x480)  0x0700 the camera (0x80)  0x0800 the cursor (0x2c0)  0x0b00 the park (only +0x2348 and +0x23a0 are read)
  0x3000 the layer table  0x3040 the row arrays  0x3240 the cells  0x3400 the item records (10 of 0x2c)  0x3600 the menu wrapper and its inner object  0x3680 the park menu
  0x36c0 the name entry  0x3700 the gap bitmap  0x3740 the object list  0x3800 the vtables  0x3900 a font record  0x3a80 a renderer  0x3b20 the save buffer  0x3b40 a name
  0x3c00 the allocation arena (0x2800)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from editor_specs import Img, a
from editor_specs_view import CATEGORIES, PIECES, make_library
from opentony.memory import mem
from pspool_specs import ensure, under_test

EDT, UI, CAM, CUR, PARK = 0x0000, 0x0200, 0x0700, 0x0800, 0x0B00
LAYERS, ROWS, CELLS, ITEMS = 0x3000, 0x3040, 0x3240, 0x3400
WRAP, INNER, PM, NE, ELEM, OBJLIST = 0x3600, 0x3640, 0x3680, 0x36C0, 0x3700, 0x3740
VT_WRAP, VT_NE, VT_EL, VT_PM, VT_FONT = 0x3800, 0x3820, 0x3840, 0x3860, 0x3880
FREC, RENDER, SAVEBUF, NAME = 0x3900, 0x3A80, 0x3B20, 0x3B40
ARENA, ARENA_SIZE = 0x3C00, 0x2800
SLAB = ARENA + ARENA_SIZE + 0x100

# unreached retail entry points the fake objects' virtual methods are
FS_DELETE, FS_STEP, FS_DONE, FS_CHOICE = 0x004AD9B0, 0x004ADA10, 0x004AD9E0, 0x004ADA40
FS_NAME2, FS_ELEM3 = 0x004AD9C0, 0x004AD9D0
FONT_SLOT = 0x004AD890
EDITOR_SLOT0 = 0x004DD390         # the first slot of the editor's own vtable (0x00518908)

SCRATCH_TEXTS, SCRATCH_IDS, SCRATCH_LOOK, SCRATCH_LINE = 0x7FF00D00, 0x7FF00D40, 0x7FF00E80, 0x7FF00EA0
SCRATCH_CARD, SCRATCH_ADDED = 0x7FF00EE0, 0x7FF00F00

# the statics the editor code reads and writes (the game's own, poked per case)
PADS = (0x0056AFF0, 0x168)
WRITABLE = [(0x005611B8, 4), (0x005613B8, 8), (0x0056116C, 4), (0x005614C8, 4), (0x005614D8, 4), (0x005283A0, 0x30), (0x00528398, 8), PADS, (0x00561C70, 4), (0x00561CA8, 4),
            (0x0056A1FC, 0x44), (0x0056A924, 4), (0x00564368, 4), (0x00563A38, 4), (0x00563B38, 4), (0x00564384, 4), (0x00562108, 4), (0x005620AC, 4), (0x00560FD4, 4),
            (0x0056D438, 0x110), (0x029DA394, 8), (0x00527D50, 8), (0x005287C0, 0x340), (CATEGORIES, 0xE0)]
# read, never written (the executable's data): frozen once
FIXED = [(0x00518908, 0x14), (0x005188B8, 0x50), (0x005286BC, 0x40), (0x005286FC, 0x10), (0x0052E400, 0x510), (0x0052A4E0, 0x1C0), (0x00525440, 0x120), (0x005256D8, 0x10),
         (0x00525760, 8), (0x005258D0, 0x20), (0x00527840, 0x20), (0x005287A0, 0x20), (0x0052EB20, 8), (0x0055FE00, 0x40)]

STATE: dict = {}
# what the menu in a state can answer with (-1 is "back")
CHOICES = {1: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], 6: [-1, 0, 1, 2, 3], 7: [-1, 0, 1, 2, 3, 4], 8: [-1, 0, 1], 9: [0, 1, 2, 3], 12: [0, 1, 2], 13: [-1, 0, 1, 2, 3, 4],
           15: [-1] + list(range(16)), 16: [-1, 0, 1, 2, 3]}
WRAPPER_STATES = (1, 6, 7, 8, 9, 12, 13, 15, 16)


def reset():
    STATE.clear()
    STATE.update(free=0)


class FixedStrings:
    """The static regions plus the strings the pointer tables of the menus name (found when the capture starts, in the game)."""

    def __iter__(self):
        out = list(FIXED)
        for base in range(0x0052E400, 0x0052E910, 4):
            pointer = mem.u32(base)
            if 0x00500000 <= pointer < 0x00560000:
                out.append((pointer, 0x40))
        pointer = mem.u32(0x0052EB24)
        out.append((pointer, 0x20))
        return iter(out)


def obj_regions(slab, arena=False):
    out = [(slab + EDT, 0x200), (slab + UI, 0x480), (slab + CAM, 0x80), (slab + CUR, 0x2C0), (slab + PARK + 0x2348, 8), (slab + PARK + 0x23A0, 4), (slab + LAYERS, 0x40),
           (slab + ROWS, 0x200), (slab + CELLS, 0x100), (slab + ITEMS, 0x1C0), (slab + WRAP, 0x80), (slab + PM, 0x40), (slab + NE, 0x40), (slab + ELEM, 0x40), (slab + OBJLIST, 0x40),
           (slab + VT_WRAP, 0xA0), (slab + FREC, 0x160), (slab + RENDER, 0x90), (slab + SAVEBUF, 0x10), (slab + NAME, 0x40)] + WRITABLE
    if arena:
        out.append((slab + ARENA, ARENA_SIZE))
    return out


def obj_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    vt = {VT_WRAP: [FS_DELETE, FS_STEP, FS_DONE, FS_CHOICE], VT_NE: [FS_DELETE, 0, FS_NAME2, 0], VT_EL: [FS_DELETE, 0, 0, FS_ELEM3], VT_PM: [FS_DELETE, 0, 0, 0],
          VT_FONT: [0, FONT_SLOT, 0, 0]}
    return {env.slab + at: struct.pack("<IIII", *slots) for at, slots in vt.items()}


for _group, _arena in (("eoc", True), ("eod", False), ("eom", False)):
    SLAB_BYTES[_group] = SLAB
    SEEDS[_group] = obj_seed
    EXTRA_REGIONS[_group] = (lambda arena: (lambda slab: obj_regions(slab, arena)))(_arena)
    READ_ONLY[_group] = FixedStrings()


# ---- the world ---------------------------------------------------------------------------------------------------------------------
def gap_types(env):
    return [env.u32(0x005286FC + 4 * k) for k in range(4)]


def build_world(env, w, mode=None, state=None, menu=None, pm=None, ne=None, item=None, sub=None, requests=None, pads=None, delay=None, held=None):
    """A legal editor in the slab: the object, its parts, the park's cells, ten item records and the statics.  Returns a dictionary of what was chosen."""
    rng = env.rng
    e = a(env, EDT)
    w.garbage(e, 0x1DC, rng)
    w.u32(e, 0x00518908)
    w.u32(e + 0x38, a(env, VT_FONT))
    choice = {}
    mode = rng.choice([0, 0, 1, 1, 2, 3, 4, 5]) if mode is None else mode
    if state is None:
        state = rng.choice(list(range(0, 18)))
        if mode == 5 and rng.random() < 0.5:
            state = 0                       # (the pad-out mode closes a menu of a running state: with none running the menu it made is looked at)
    choice.update(mode=mode, state=state)
    STATE["choices"] = CHOICES.get(state)
    STATE["done_p"] = 0.75 if state in CHOICES else 0.5
    w.u32(e + 0x194, mode)
    w.u32(e + 0x198, rng.choice([0, 1, 2, 3, 4]))
    w.u32(e + 0x188, state)
    w.u32(e + 0x18C, rng.choice([0, 1, 6, 7, 9, 10, 0xB, 0xC, 0xD, 0x2A] if state != 8 else [7, 7, 10, 10, 1, 0x2A]))
    item = rng.randrange(0, 10) if item is None else item
    sub = rng.choice([0, 1]) if sub is None else sub
    w.u32(e + 0x1A0, item)
    w.u32(e + 0x1A4, sub)
    w.u32(e + 0x19C, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 10)]))
    w.u32(e + 0x1A8, a(env, UI))
    w.u32(e + 0x1AC, a(env, PARK))
    w.u32(e + 0x1B4, a(env, CAM))
    w.u32(e + 0x1B8, a(env, CUR))
    w.u32(e + 0x184, a(env, ELEM))
    w.u32(e + 0x1D4, rng.randrange(0, 4))
    w.u32(e + 0x1D8, rng.choice([0, 0, 1, 3, 4]) if delay is None else delay)
    w.u32(e + 4, rng.choice([0xFFFFFFFF, 0, 1, 2, 3]))
    w.u32(e + 8, rng.randrange(0, 5))
    for off in (0xC, 0xD, 0xE):
        w.u8(e + off, 0)
    if requests:
        for off in requests:
            w.u8(e + off, 1)
    w.u8(e + 0x2E, rng.choice([0, 1]))
    # the parts the menus hang on
    wrapper = rng.random() < (0.75 if state in WRAPPER_STATES else 0.4) if menu is None else menu
    w.u32(e + 0x30, a(env, WRAP) if wrapper else 0)
    w.u32(e + 0x34, a(env, NE) if (rng.random() < (0.75 if state == 14 else 0.3) if ne is None else ne) else 0)
    w.u32(e + 0x190, a(env, PM) if (rng.random() < (0.75 if state in (3, 4, 10, 11) else 0.3) if pm is None else pm) else 0)
    w.u32(e + 0x18, rng.choice([0, a(env, SAVEBUF)]))
    w.u32(e + 0x1C, rng.choice([0, a(env, SAVEBUF)]))
    choice.update(menu=wrapper)
    # fake objects
    for at in (WRAP, INNER):
        w.garbage(a(env, at), 0x40, rng)
        w.u32(a(env, at), a(env, VT_WRAP))
    w.u32(a(env, WRAP) + 4, a(env, INNER))
    w.garbage(a(env, PM), 0x40, rng)
    w.u32(a(env, PM), a(env, VT_PM))
    w.garbage(a(env, NE), 0x40, rng)
    w.u32(a(env, NE), a(env, VT_NE))
    w.garbage(a(env, ELEM), 0x40, rng)
    w.u32(a(env, ELEM), a(env, VT_EL))
    # the interface (its fields the editor touches)
    u = a(env, UI)
    w.garbage(u + 0x430, 0x30, rng)
    w.u32(u + 0x434, rng.randrange(0, 6))
    w.u32(u + 0x43C, rng.randrange(0, 12))
    # the cursor and the camera
    k = a(env, CUR)
    w.garbage(k + 0x260, 0x60, rng)
    w.garbage(k + 0x2A0, 0x20, rng)
    w.u32(k + 0x8C, rng.randrange(0, 0x4000))
    # the park: layers -> rows -> cells
    p = a(env, PARK)
    w.u32(p + 0x2348, a(env, LAYERS))
    w.u32(p + 0x23A0, rng.choice([0, a(env, OBJLIST)]))
    for z in range(4):
        w.u32(a(env, LAYERS) + 4 * z, a(env, ROWS + 0x80 * z))
        for slot in range(30):
            w.u32(a(env, ROWS + 0x80 * z) + 4 * slot, a(env, CELLS + 0x20 * rng.randrange(0, 8)))
    for c in range(8):
        w.garbage(a(env, CELLS + 0x20 * c), 0x20, rng)
        w.u8(a(env, CELLS + 0x20 * c) + 8, rng.randrange(0, 5) if rng.random() < 0.9 else 0xFE)
        w.u8(a(env, CELLS + 0x20 * c) + 0xA, rng.randrange(0, 5) if rng.random() < 0.9 else 0xFE)
    # the item records
    items = a(env, ITEMS)
    w.u32(0x005614D8, items)
    types = gap_types(env)
    for i in range(10):
        rec = items + 0x2C * i
        w.garbage(rec, 0x2C, rng)
        for end in (0, 1):
            w.u8(rec + end, rng.randrange(0, 6))
            w.u8(rec + 2 + end, rng.randrange(0, 5))
            w.u8(rec + 4 + end, rng.randrange(0, 4))
            w.u8(rec + 6 + end, rng.randrange(0, 4))
            w.u8(rec + 8 + end, rng.randrange(0, 4))
            w.u8(rec + 10 + end, rng.randrange(0, 4))
        w.u8(rec + 0xC, rng.choice(types))
        w.put(rec + 0xD, bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + b"\0")
        w.u32(rec + 0x28, rng.randrange(0, 16))
    # the statics
    w.u32(0x005614C8, rng.randrange(0, 4))
    w.u32(0x005283A0, rng.randrange(0, 5))
    w.u32(0x00528398, rng.choice([3, 4, 6, 8, 30]))
    w.u32(0x0052839C, rng.choice([2, 3, 4, 8, 30]))
    w.u32(0x0056116C, rng.randrange(0x30, 0x7F))
    w.u8(0x005611B8, rng.choice([0, 1]))
    w.u32(0x005613B8, rng.randrange(0, 4))
    for r in range(4):
        w.u32(0x0056D438 + 0x44 * r, a(env, OBJLIST) if rng.random() < 0.95 else 0)
    w.put(PADS[0], bytes(PADS[1]))
    w.u32(0x0056B144, rng.choice([1] * 9 + [0]) if mode != 5 else rng.choice([0, 0, 1]))
    if pads is not None:
        for address in pads:
            w.u8(address, 1)
    for address in (held or []):
        w.u8(address, 1)
    for address in (0x0056B0E8,):
        w.u8(address, rng.choice([0, 0, 0, 1]))
    w.u8(0x00561CA8, rng.randrange(256))
    w.u8(0x00561C70, rng.randrange(256))
    w.put(0x0056A1FC, rng.randbytes(0x44))
    w.u32(0x0056A924, rng.randrange(1 << 32))
    # the statics the screen and the renderer give
    w.u32(0x029DA394, rng.choice([640, 512, 800, 320]))
    w.u32(0x029DA398, rng.choice([480, 384, 600, 240]))
    w.u32(0x00527D50, rng.randrange(0, 60))
    w.u32(0x00527D54, rng.randrange(0, 60))
    w.u32(0x00560FD4, a(env, RENDER))
    w.garbage(a(env, RENDER), 0x90, rng)
    w.garbage(a(env, FREC), 0x160, rng)
    w.put(a(env, NAME), bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + b"\0")
    # the piece library (a small one over the real table)
    pieces, categories, _starts, _ends = make_library(env, w)
    w.u8(PIECES + 0x25 * pieces, 0xFF)                   # (the tables end with a record whose first byte is 0xff: the interface counts up to it)
    w.u8(CATEGORIES + 0x22 * categories, 0xFF)
    return choice


PAD_RESET = [0x0056B019, 0x0056B029, 0x0056AFF9, 0x0056B049, 0x0056B039, 0x0056B069, 0x0056B059, 0x0056B079, 0x0056B089, 0x0056B099, 0x0056B0A9, 0x0056B0D9]
HELD = [0x0056B078, 0x0056B088, 0x0056B098, 0x0056B0A8]


def some_pads(rng, count=None):
    count = rng.choice([0, 0, 1, 1, 2]) if count is None else count
    return rng.sample(PAD_RESET, count)


def set_budget(env, w):
    """What Mem_GetFree answers so that the pieces left are near the thresholds (and the nodes and the zone room too)."""
    rng = env.rng
    theme, version = mem_or(w, 0x005614C8), mem_or(w, 0x005283A0)
    need = env.u32(0x005188B8 + 4 * (version + theme * 5))
    left = rng.choice([-0x2000, -0x1518, -0x1517, -1, 0, 1, 0x100, 5000, -0x1000])
    STATE["free"] = (left + 0x6338 + need) & 0xFFFFFFFF
    STATE["nodes"] = rng.choice([-5, 0, 1, 100])
    STATE["zone"] = rng.choice([-5, 0, 1, 100])


def mem_or(w, address):
    return int.from_bytes(bytes(w.bytes.get(address + k, 0) for k in range(4)), "little")


def espec(address, name, group, this=lambda env: env.slab + EDT):
    return spec(address, name, group, this=this)


# ---- eoc: construct, destruct, budget -----------------------------------------------------------------------------------------------
@espec(0x004334D0, "LevelEditor_Construct", "eoc")
def gen_construct(env, n):
    reset()
    w = Img()
    build_world(env, w, mode=0, state=0)
    w.garbage(a(env, ARENA), 0x2400, env.rng)
    return [], w.pokes()


@espec(0x004338E0, "LevelEditor_Destruct", "eoc")
def gen_destruct(env, n):
    reset()
    w = Img()
    rng = env.rng
    build_world(env, w, ne=rng.random() < 0.5)
    e = a(env, EDT)
    for off, value in ((0x1AC, a(env, PARK)), (0x1A8, a(env, UI)), (0x1B4, a(env, CAM)), (0x1B8, a(env, CUR)), (0x184, a(env, ELEM))):
        w.u32(e + off, value if rng.random() < 0.75 else 0)
    return [], w.pokes()


@espec(0x00434710, "LevelEditor__ResetState", "eoc")
def gen_reset_state(env, n):
    reset()
    w = Img()
    build_world(env, w)
    return [], w.pokes()


@spec(0x00435F60, "LevelEditor_PiecesLeft", "eoc")
def gen_pieces_left(env, n):
    reset()
    w = Img()
    build_world(env, w)
    set_budget(env, w)
    return [], w.pokes()


@spec(0x00435FA0, "LevelEditor_UiConstruct", "eoc", this=lambda env: env.slab + UI)
def gen_ui_construct(env, n):
    reset()
    w = Img()
    build_world(env, w)
    w.garbage(a(env, UI), 0x480, env.rng)
    w.garbage(a(env, ARENA), 0x1200, env.rng)
    return [], w.pokes()


# ---- eod: update and draw -----------------------------------------------------------------------------------------------------------
@espec(0x00433CB0, "LevelEditor__Update", "eod")
def gen_update(env, n):
    reset()
    w = Img()
    rng = env.rng
    requests = [off for off in (0xC, 0xD, 0xE) if rng.random() < 0.25]
    ch = build_world(env, w, requests=requests, pads=some_pads(rng), held=rng.sample(HELD, rng.choice([0, 0, 1, 2])))
    set_budget(env, w)
    STATE["base"] = mem_or(w, 0x0056116C)
    return [], w.pokes()


@espec(0x00433A40, "LevelEditor__Draw", "eod")
def gen_draw(env, n):
    reset()
    w = Img()
    rng = env.rng
    state = rng.choice([0, 1, 2, 2, 0xD, 0xF, 0xF, 0x10, 0x10, 0x11])
    build_world(env, w, state=state, mode=rng.choice([0, 1, 3, 3]), item=rng.randrange(0, 10))
    return [], w.pokes()


# ---- eom: the pad, the gap, the menus ------------------------------------------------------------------------------------------------
@espec(0x00434090, "LevelEditor__HandleInput", "eom")
def gen_input(env, n):
    reset()
    w = Img()
    rng = env.rng
    build_world(env, w, mode=rng.choice([0, 1]), pads=some_pads(rng, rng.choice([0, 1, 2, 3, 5])), held=rng.sample(HELD, rng.choice([0, 1, 2, 3, 4])))
    set_budget(env, w)
    STATE["base"] = mem_or(w, 0x0056116C)
    return [], w.pokes()


@espec(0x004343E0, "LevelEditor__AdjustGap", "eom")
def gen_adjust(env, n):
    reset()
    w = Img()
    rng = env.rng
    pads = rng.sample([0x0056B019, 0x0056B039, 0x0056B059], rng.choice([0, 0, 1, 1, 2]))
    build_world(env, w, mode=4, pads=pads)
    return [], w.pokes()


@espec(0x00434750, "LevelEditor__RunMenus", "eom")
def gen_run_menus(env, n):
    reset()
    w = Img()
    rng = env.rng
    state = rng.choice(list(range(0, 18)) + [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16])
    build_world(env, w, state=state, mode=rng.choice([2, 3, 2, 3, 5]))
    set_budget(env, w)
    return [], w.pokes()


@espec(0x00435830, "LevelEditor__UpdateMenu", "eom")
def gen_update_menu(env, n):
    reset()
    w = Img()
    rng = env.rng
    ch = build_world(env, w, pads=some_pads(rng), held=rng.sample(HELD, rng.choice([0, 0, 1])))
    set_budget(env, w)
    STATE["base"] = mem_or(w, 0x0056116C)
    return [], w.pokes()


@espec(0x00435BE0, "LevelEditor__PlaceItem", "eom")
def gen_place(env, n):
    reset()
    w = Img()
    rng = env.rng
    build_world(env, w, mode=rng.choice([0, 0, 1, 1, 1, 2]))
    set_budget(env, w)
    return [], w.pokes()


@espec(0x00435E50, "LevelEditor__RemoveItem", "eom")
def gen_remove(env, n):
    reset()
    w = Img()
    rng = env.rng
    build_world(env, w, mode=rng.choice([0, 1, 2]))
    set_budget(env, w)
    return [], w.pokes()


@espec(0x00435F20, "LevelEditor__RefreshBudget", "eom")
def gen_refresh(env, n):
    reset()
    w = Img()
    build_world(env, w)
    set_budget(env, w)
    return [], w.pokes()


# ---- the stubs -----------------------------------------------------------------------------------------------------------------------
def obj_stubs(fc, env):
    if getattr(fc, "_obj_stubs", False):
        return
    fc._obj_stubs = True
    under_test()
    rng = env.rng
    last = lambda: env.alloc_log[-1][0] if env.alloc_log else 0      # noqa: E731
    slab = env.slab

    def tc(address, argc, **kw):
        ensure(fc, address, argc, convention="thiscall", this=True, **kw)

    def cd(address, argc, **kw):
        ensure(fc, address, argc, **kw)

    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    cd(0x004436A0, 1, result=env.alloc)
    cd(0x004436D0, 1)
    # fonts
    tc(0x00449CA0, 0)
    tc(0x0044A060, 0)
    cd(0x0044ADA0, 1, text=True, result=lambda v: slab + FREC)
    cd(0x0044B000, 1, text=True, result=lambda v: slab + FREC)
    cd(0x0044B0A0, 1, text=True)
    tc(0x0044AB70, 3)
    # bitmaps, windows and text elements
    tc(0x00457420, 4, text=True, result=lambda v: last())
    tc(0x00457120, 0)
    tc(0x00457820, 0, result=lambda v: rng.randrange(8, 120))
    tc(0x004A1B40, 7, text=True, result=lambda v: last())
    tc(0x004AC840, 6, text=True, result=lambda v: last())
    for address in (0x004B2390, 0x0048A420, 0x00489490):
        cd(address, 0)
    cd(0x004E8960, 1, result=lambda v: rng.randrange(1 << 32))
    cd(0x00489670, 1)
    # the interface (4.11d)
    tc(0x00436FA0, 1)
    tc(0x004371F0, 2)
    tc(0x00437220, 0)
    tc(0x004371D0, 0, result=lambda v: mem.u32(0x0056116C) if rng.random() < 0.35 else rng.randrange(0x30, 0x7F))
    tc(0x00436DA0, 1)
    tc(0x00436FD0, 1)
    tc(0x004365E0, 0)
    tc(0x004369D0, 0)
    tc(0x00436520, 0)
    # the cursor (4.11d)
    tc(0x004377D0, 0, result=lambda v: last())
    tc(0x00437860, 0)
    tc(0x004378D0, 1)
    tc(0x00437950, 1)
    tc(0x00439110, 2, masks={1: 0xFFFF})
    tc(0x00437CD0, 3)
    tc(0x00437C30, 1)
    tc(0x00437D30, 1)
    tc(0x00438410, 0)
    tc(0x004385B0, 0)
    tc(0x00438610, 0, result=lambda v: rng.choice([0, 1]))
    tc(0x00438360, 1, pokes=[(0, 4, lambda v: rng.choice([0, 1]))], result=lambda v: rng.choice([0xFFFFFFFF, 0xFFFFFFFF] + list(range(10))))
    tc(0x00438C50, 2, pokes=[(1, 4, lambda v: rng.randrange(0, 10))], stack_to={1: SCRATCH_ADDED}, result=lambda v: rng.choice([0, 1, 1]))
    tc(0x00438320, 0)
    tc(0x00438CE0, 0, result=lambda v: rng.choice([0, 1]))
    tc(0x00438DC0, 0, result=lambda v: rng.choice([0, 1]))
    # the camera (4.11d)
    tc(0x00437270, 0, result=lambda v: last())
    tc(0x004372C0, 0)
    tc(0x00437720, 1, stack_to={0: SCRATCH_LOOK}, fill=(0, lambda v: struct.pack("<hhh", rng.choice([0, 5, -5, rng.randrange(-4096, 4096)]), rng.randrange(-100, 100),
                                                                                  rng.choice([0, 5, -5, rng.randrange(-4096, 4096)]))),
       result=lambda v: rng.choice([0, 1]))
    tc(0x004372D0, 3)
    tc(0x00437310, 0)
    tc(0x00437650, 1, result=lambda v: rng.choice([0xFFFFFFFF] + list(range(16))))
    # the park generator (4.11c) and the rest of the editor's own code (4.11d)
    cd(0x00433280, 2, pokes=[(0, 4, lambda v: slab + PARK)])
    tc(0x0043A280, 3)
    tc(0x0043D400, 2, pokes=[(0, 4, lambda v: rng.randrange(0, 4))])
    tc(0x0043F6C0, 0)
    tc(0x0043CE60, 1, masks={0: 0xFF})
    tc(0x0043F5E0, 1)
    cd(0x00466DE0, 0)
    tc(0x00439900, 0)
    cd(0x0043BF80, 1)
    tc(0x0043CD90, 2)
    cd(0x0043C050, 2, pokes=[(1, None, lambda v: struct.pack("<II", rng.randrange(0, 5), rng.randrange(0, 4)))])
    cd(0x0043C480, 1, pokes=[(0, 4, lambda v: slab + SAVEBUF)], result=lambda v: rng.randrange(0x40, 0x2000))
    cd(0x00432980, 2, text=True, stack_to=SCRATCH_CARD)
    tc(0x00432C10, 0)
    tc(0x00432C60, 0, result=lambda v: rng.choice([0, 0, 1, 2]))
    tc(0x004329D0, 4, result=lambda v: slab + PM)
    tc(0x00433090, 1)
    # the memory card, the menus
    cd(0x0047ACE0, 0)
    cd(0x0047A480, 4)
    cd(0x0047AC60, 0, result=lambda v: rng.choice([0, 0, 1, 2]))
    cd(0x0047B850, 1, stack_to=SCRATCH_CARD, fill=(0, lambda v: bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + b"\0" + bytes(0x20))) if False else None
    cd(0x0047B850, 1, stack_to=SCRATCH_CARD, fill=(0, lambda v: (bytes(rng.randrange(65, 91) for _ in range(rng.randrange(1, 20))) + bytes(0x20))[:0x20]))
    cd(0x0047AE10, 1)
    tc(0x00472EF0, 5, stack_to={0: SCRATCH_TEXTS, 1: SCRATCH_IDS}, strings=(0, lambda v: v[2]), blob=(1, lambda v: 4 * v[2]), result=lambda v: slab + WRAP)
    tc(0x00470B20, 1)
    cd(0x0046F550, 3, result=lambda v: rng.choice([0xFFFFFFFF, 0, 1, 2, 3, 4, 5]))
    tc(0x00471E30, 11, result=lambda v: slab + NE)
    tc(0x00472A70, 3, text=True)
    tc(0x00472930, 1, text=True)
    tc(0x00472CC0, 1, result=lambda v: rng.choice([0, 0, 1, 2]))
    tc(0x00472920, 0, result=lambda v: slab + NAME)
    # drawing, sound, the budget
    cd(0x0045E8E0, 3)
    cd(0x0045F530, 1)
    cd(0x004AD620, 3)
    cd(0x0046F2C0, 1, result=lambda v: STATE["free"])
    cd(0x00440170, 0, result=lambda v: STATE.get("nodes", rng.choice([-5, 0, 1, 100])) & 0xFFFFFFFF)
    cd(0x0043F710, 0, result=lambda v: STATE.get("zone", rng.choice([-5, 0, 1, 100])) & 0xFFFFFFFF)
    # the fake objects' virtual methods
    tc(FS_DELETE, 1)
    tc(FS_STEP, 0)
    tc(FS_DONE, 0, result=lambda v: 1 if rng.random() < STATE.get("done_p", 0.66) else 0)
    tc(FS_CHOICE, 0, result=lambda v: rng.choice(STATE.get("choices") or [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20]) & 0xFFFFFFFF)
    tc(FS_NAME2, 2)
    tc(FS_ELEM3, 3)
    tc(FONT_SLOT, 4, text=2, stack_to={2: SCRATCH_LINE})
    tc(EDITOR_SLOT0, 0)


GROUP_STUBS: dict = {"eoc": obj_stubs, "eod": obj_stubs, "eom": obj_stubs}
reset()
