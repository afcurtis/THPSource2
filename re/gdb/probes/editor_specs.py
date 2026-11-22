"""Phase 4.11d case generators, part 1: editorglobal.cpp (the editor's maps, the park menu, the load and save state machine) and the editor loop of leveleditor.cpp
(0x004326a0..0x00433280, 0x004333c0): groups `edg` (maps, shade, park name), `edm` (the park menu) and `edl` (load, discard, save and the editor loop).

The probe (frontend_calls.py with the group name) forces calls of the retail functions on the live process.  The menu object, its two list wrappers (fake objects whose
vtable slots are unreached retail entry points, so a virtual call is a reported service), the park buffer, the editor's cell rows, the item records, the base objects and
the allocation arena are made up in the scratch slab; the statics the code reads and writes are the game's own, poked per case.  Everything outside the file is a stub
that reports its call and answers a scripted value: the menu toolkit, the file layer, the memory card, the park generator (4.11c) and the editor's own object (4.11e).

Slab layout (offsets from the scratch slab):
  0x0000 the menu object (0x180)    0x0200 the delete menu's wrapper   0x0240 the list's wrapper   0x0280, 0x02c0 their inner objects   0x0300 the vtable
  0x0400 the labels' file text (0x800)    0x0c00 the park buffer (0x1e00)    0x2a00 the rows table (0x100), 0x2b00 its block (0x1e00)    0x4900 the item records (0x200)
  0x4b00 file names (0x100)   0x4c00 base objects (8 of 0x4c)   0x4f00 a region's raw image (0x300)   0x5200 models   0x5300 the memory card manager (+0xda0..)   0x6100 the arena
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from pspool_specs import FAKE_SLOTS, Ov, ensure, under_test

# ---- statics -----------------------------------------------------------------------------------------------------------------
MAP_STATE = 0x005614BC
STATIC_BLOCK = 0x005614B8        # 0x30 bytes: the map state, the flash flag, the object count, the theme, the rows, the items
SHADE_CACHE = 0x00561170
SLOT_ROW = 0x005611A4
PARK_IS_NEW = 0x00561450
PARK_NAME = 0x0053D7B4
NAME_DIRTY = 0x0056ACFB
VERSION = 0x005283A0
ACTIVE_PLAYER = 0x0056A888
FADE_FLAG = 0x00561CA9
POLY_FLAG = 0x00564384
LAST_TICK = 0x00568624
TICK = 0x0056E31C
MMU_MANAGER = 0x0056AD00
OBJECT_TABLES = 0x0056D438
SCRATCH_FILE, SCRATCH_OUT, SCRATCH_PARSE, SCRATCH_INDEX, SCRATCH_LIST = 0x7FF00900, 0x7FF00920, 0x7FF00930, 0x7FF00940, 0x7FF00A00

# ---- the slab ------------------------------------------------------------------------------------------------------------------
OBJ = 0x0000
DEL, LST, INNER, INNER2, VT = 0x0200, 0x0240, 0x0280, 0x02C0, 0x0300
TEXT = 0x0400
BUF = 0x0C00
ROWS, BLOCK = 0x2A00, 0x2B00
ITEMS = 0x4900
NAMES = 0x4B00
OBJS = 0x4C00
RAW = 0x4F00
MODELS = 0x5200
MMU = 0x5300
ARENA, ARENA_SIZE = 0x6100, 0x6000
SLAB = ARENA + ARENA_SIZE + 0x100

EDITOR_WINDOWS = [(STATIC_BLOCK, 0x30), (SHADE_CACHE, 4), (SLOT_ROW, 4), (PARK_IS_NEW, 4), (PARK_NAME, 0x100), (NAME_DIRTY, 4), (VERSION, 4), (ACTIVE_PLAYER, 4),
                  (FADE_FLAG, 4), (POLY_FLAG, 4), (LAST_TICK, 4), (MMU_MANAGER, 4), (OBJECT_TABLES, 0x110)]
EDITOR_READS = [(0x005256DC, 8), (0x005287A4, 0xA0), (0x005310AC, 0x20), (0x0052E404, 0x14), (0x0052EB20, 4)]


def edit_regions(slab, *ranges):
    return [(slab + a, n) for a, n in ranges] + EDITOR_WINDOWS


def edit_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    # the fake vtable of the list wrappers: delete (1 argument), then three methods of no arguments
    vt = struct.pack("<IIII", FAKE_SLOTS[0], FAKE_SLOTS[1], FAKE_SLOTS[2], FAKE_SLOTS[3])
    return {env.slab + VT: vt}


class Img(Ov):
    def garbage(self, address, size, rng):
        self.put(address, bytes(rng.randrange(256) for _ in range(size)))


def a(env, offset):
    return env.slab + offset


def register(group, ranges, clock=False):
    SLAB_BYTES[group] = SLAB
    SEEDS[group] = edit_seed
    # (the live clock ticks on its own while a forced call runs: only the loop, which moves it on itself, captures it)
    EXTRA_REGIONS[group] = lambda slab: edit_regions(slab, *ranges) + ([(TICK, 4)] if clock else [])
    READ_ONLY[group] = EDITOR_READS


register("edg", [(ROWS, 0x100), (BLOCK, 0x40), (ITEMS, 0x200), (OBJS, 0x300), (RAW, 0x300), (MODELS, 0x80), (NAMES, 0x100), (ARENA, ARENA_SIZE)])
register("edm", [(OBJ, 0x180), (DEL, 0x180), (TEXT, 0x800), (BUF, 0x1E00), (ROWS, 0x100), (BLOCK, 0x1E00), (NAMES, 0x100), (MMU + 0xDA0, 0x10), (ARENA, ARENA_SIZE)])
register("edl", [(ROWS, 0x100), (BLOCK, 0x1E00), (BUF, 0x80), (NAMES, 0x100), (ARENA, ARENA_SIZE)])
register("edr", [(ROWS, 0x100), (BLOCK, 0x1E00), (BUF, 0x80), (NAMES, 0x100), (ARENA, ARENA_SIZE)], clock=True)

# ---- scripted answers -----------------------------------------------------------------------------------------------------------
STATE: dict = {}


def reset_state():
    STATE.clear()
    STATE.update(size=0, text=b"", ticks=0, frames=1, quit_after=None, update_after=1, frame=0, parse=(0, 0), choice=None, done=None)


# ---- the park menu's world ----------------------------------------------------------------------------------------------------------
def label_text(rng, count=50):
    """The labels' file: each label after a '$', up to the end of its line (some junk before)."""
    out = b""
    for i in range(count):
        out += rng.choice([b"", b"x", b"junk ", b"\n", b"  "]) + b"$" + (b"PARK %d " % i + bytes(rng.randrange(65, 91) for _ in range(rng.randrange(0, 8)))) + b"\r\n"
    return out


def menu_object(env, w, mode=None, step=None, saving=None):
    """The park menu object at the slab's start: random bytes, then the fields the code reads in a legal state."""
    rng = env.rng
    ov = w
    ov.garbage(a(env, OBJ), 0x180, rng)
    ov.garbage(a(env, DEL), 0x80, rng)
    ov.u32(a(env, OBJ), 0x00518850)
    ov.u32(a(env, OBJ) + 0x5C, rng.choice([0, 1, 4]) if mode is None else mode)
    ov.u32(a(env, OBJ) + 0x58, rng.choice([0, 1, 2, 2, 3, 0xFFFFFFFF]) if step is None else step)
    ov.u8(a(env, OBJ) + 0x60, rng.choice([0, 1]) if saving is None else saving)
    ov.u32(a(env, OBJ) + 0x64, rng.randrange(0, 4))
    ov.u32(a(env, OBJ) + 0x68, rng.randrange(0, 2))
    ov.u32(a(env, OBJ) + 0x6C, a(env, BUF))
    ov.u32(a(env, OBJ) + 0x70, rng.choice([0x1E00, 0x1000, rng.randrange(0x40, 0x1E00)]))
    ov.u32(a(env, OBJ) + 0x74, a(env, LST))
    ov.u32(a(env, OBJ) + 0x4, a(env, DEL))
    # the wrappers: a vtable, an inner object with the same vtable
    for wrapper, inner in ((LST, INNER), (DEL, INNER2)):
        ov.u32(a(env, wrapper), a(env, VT))
        ov.u32(a(env, wrapper) + 4, a(env, inner))
        ov.u32(a(env, inner), a(env, VT))
    # the labels: 50 pointers into the text
    text = label_text(rng)
    pos = 0
    for i in range(50):
        pos = text.index(b"$", pos) + 1
        ov.u32(a(env, OBJ) + 0x7C + 4 * i, a(env, TEXT) + pos)
        pos = text.index(b"\r", pos)
        text = text[:pos] + b"\0" + text[pos + 1:]      # (the loader ends each label where its line ended)
    ov.put(a(env, TEXT), text + bytes(0x800 - len(text)))
    ov.put(PARK_NAME, bytes(rng.randrange(32, 127) for _ in range(0x20)) + b"\0" * 0x20)
    ov.u32(ACTIVE_PLAYER, rng.randrange(0, 2))
    ov.u32(MMU_MANAGER, a(env, MMU))
    ov.put(a(env, MMU) + 0xDA0, rng.randbytes(0x10))
    ov.put(a(env, ROWS), rng.randbytes(8))
    ov.u32(a(env, ROWS), a(env, BLOCK))
    ov.u32(0x005614CC, a(env, ROWS))
    ov.put(a(env, BLOCK), rng.randbytes(0x1E00))
    ov.u8(NAME_DIRTY, rng.randrange(256))
    return ov


def pick_choice(rng):
    return rng.choice([rng.randrange(0, 50)] * 4 + [0xFFFFFFFF, 50, 0x33, 49, 0])


@spec(0x00432C60, "CustomPark_ProcessState", "edm", this=lambda env: env.slab + OBJ)
def gen_process(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    step = rng.choice([0, 0, 1, 1, 2, 2, 3])
    menu_object(env, w, step=step)
    if rng.random() < 0.06:
        w.u32(a(env, OBJ) + 0x5C, rng.choice([2, 3, 5]))       # (a mode the machine does not know leaves the result alone)
    # (the delete step takes the list's choice without a range test: a legal state has a real park chosen)
    STATE["choice"] = pick_choice(rng) if step != 2 else rng.choice([0, 0, rng.randrange(0, 50)])
    STATE["done"] = rng.choice([0, 1, 1, 1])
    STATE["card"] = rng.choice([0, 1, 1, 2])
    STATE["size"] = rng.randrange(0x40, 0x1E00)
    return [], w.pokes()


@spec(0x00432C10, "ParkMenu_Update", "edm", this=lambda env: env.slab + OBJ)
def gen_update(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w, step=rng.choice([0, 1, 2, 3, 0xFFFFFFFF, 5]))
    w.u32(a(env, OBJ) + 0x5C, rng.choice([0, 1, 2, 3, 4, 4, 5]))
    return [], w.pokes()


@spec(0x004329D0, "ParkMenu_Construct", "edm", this=lambda env: env.slab + OBJ)
def gen_construct(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w)
    STATE["text"] = label_text(rng)
    STATE["size"] = len(STATE["text"])
    mode = rng.choice([0, 1, 2, 3, 4, 5])
    # (the labels are read from the file the stub loads into the arena)
    return [mode, a(env, BUF), rng.choice([0x1E00, 0x100]), rng.randrange(0, 2)], w.pokes()


@spec(0x00432B80, "ParkMenu_Destroy", "edm", this=lambda env: env.slab + OBJ)
def gen_destroy(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w)
    w.u32(a(env, OBJ) + 0x5C, rng.choice([0, 1, 4, 2]))
    w.u32(a(env, OBJ) + 0x74, rng.choice([a(env, LST), a(env, LST), 0]))
    STATE["card"] = rng.choice([0, 1])
    return [], w.pokes()


@spec(0x00432B60, "ParkMenu_Delete", "edm", this=lambda env: env.slab + OBJ)
def gen_delete(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w)
    w.u32(a(env, OBJ) + 0x5C, rng.choice([0, 1, 4, 2]))
    STATE["card"] = rng.choice([0, 1])
    return [rng.choice([0, 1, 0x100, 3])], w.pokes()


@spec(0x00433090, "ParkMenu_SetMode", "edm", this=lambda env: env.slab + OBJ)
def gen_set_mode(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w)
    w.u32(a(env, OBJ) + 0x74, a(env, LST))
    w.u32(a(env, OBJ) + 4, rng.choice([a(env, DEL), a(env, DEL), 0]))
    STATE["choice"] = rng.choice([rng.randrange(0, 50)] * 3 + [0, 49])
    return [rng.choice([0, 1, 2, 2, 2, 3])], w.pokes()


@spec(0x00432F60, "ParkMenu_LoadByName", "edm", this=lambda env: env.slab + OBJ)
def gen_load_by_name(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w, saving=rng.choice([0, 0, 0, 1]))
    index = rng.randrange(0, 50)
    name = b"park%d.prk\0" % index
    w.put(a(env, NAMES), name)
    STATE["size"] = rng.randrange(0x40, 0x1E00)
    STATE["card"] = rng.choice([0, 0, 1, 2, 0xFFFFFFFF])
    return [a(env, NAMES)], w.pokes()


@spec(0x00433160, "CDParks_LoadLabels", "edm", this=lambda env: env.slab + OBJ)
def gen_load_labels(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    menu_object(env, w)
    STATE["text"] = label_text(rng)
    STATE["size"] = len(STATE["text"])
    return [], w.pokes()


# ---- edg: the maps, the shade, the park name ---------------------------------------------------------------------------------
@spec(0x004326A0, "EditorMaps_Alloc", "edg")
def gen_alloc_maps(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    w.u32(MAP_STATE, rng.choice([0, 0, 0, 1, 2, 3]))
    w.u32(0x005614CC, rng.choice([0, 0, 0, a(env, ROWS)]))
    w.u32(0x005614D0, rng.choice([0, 0, a(env, ITEMS)]))
    w.put(PARK_NAME, rng.randbytes(0x40))
    w.u8(PARK_IS_NEW, rng.randrange(256))
    w.u8(NAME_DIRTY, rng.randrange(256))
    return [], w.pokes()


@spec(0x004327A0, "EditorMaps_Free", "edg")
def gen_free_maps(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    w.u32(MAP_STATE, rng.choice([0, 1, 2, 3]))
    rows = rng.choice([0, a(env, ROWS), a(env, ROWS)])
    w.u32(0x005614CC, rows)
    w.u32(a(env, ROWS), a(env, BLOCK))
    w.u32(0x005614D0, rng.choice([0, a(env, ITEMS)]))
    return [], w.pokes()


@spec(0x004328C0, "EditorObjects_SetShade", "edg")
def gen_set_shade(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    slot = rng.randrange(0, 4)
    count = rng.randrange(0, 7)
    row = rng.randrange(0, 3)
    w.u8(0x005614C4, rng.choice([0, 1, 1, 1]))
    w.u8(SHADE_CACHE, rng.choice([0, 1, 2]))
    w.u32(SLOT_ROW, row)
    w.u32(0x005614C0, count)
    entry = OBJECT_TABLES + slot * 0x44
    w.u32(entry, a(env, OBJS))
    w.u32(entry + 8, a(env, RAW))
    w.garbage(a(env, OBJS), 8 * 0x4C, rng)
    w.garbage(a(env, MODELS), 0x80, rng)
    w.garbage(a(env, RAW), 0x300, rng)
    models = [a(env, MODELS) + 0x10 * k for k in range(8)]
    for k in range(8):
        w.u8(models[k] + 1, rng.randrange(256))
        w.u16(a(env, OBJS) + 0x4C * k + 0x1A, rng.randrange(0, 8))
    base = a(env, RAW) + 0x10 + row * 0x24
    for k in range(8):
        w.u32(base + 4 * k, rng.choice(models))
    return [rng.choice([0, 1, 1, 0x100, 0x7F]), slot], w.pokes()


@spec(0x00432840, "EditorGlobal_NothingA", "edg")
def gen_nothing_a(env, n):
    reset_state()
    return [], {}


@spec(0x00432850, "EditorGlobal_NothingB", "edg")
def gen_nothing_b(env, n):
    reset_state()
    return [], {}


@spec(0x004331D0, "EditorGlobal_InitCommands", "edg")
def gen_init_commands(env, n):
    reset_state()
    w = Img()
    w.garbage(a(env, NAMES), 0x40, env.rng)
    return [a(env, NAMES)], w.pokes()


@spec(0x00432960, "EditorBuffer_Alloc", "edg")
def gen_buffer_alloc(env, n):
    reset_state()
    return [env.rng.choice([0x10, 0x100, 0x600, 0x3000])], {}


@spec(0x00432980, "EditorGlobal_SetParkName", "edg")
def gen_set_park_name(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    name = bytes(rng.randrange(32, 127) for _ in range(rng.randrange(0, 0x20))) + b"\0"
    w.put(PARK_NAME, name + rng.randbytes(0x40 - len(name)))
    w.put(a(env, NAMES), bytes(rng.randrange(32, 127) for _ in range(rng.randrange(0, 0x30))) + b"\0")
    label = rng.choice([0, a(env, NAMES), PARK_NAME, a(env, NAMES) + 1])
    w.u8(PARK_IS_NEW, rng.randrange(256))
    w.u8(NAME_DIRTY, rng.randrange(256))
    return [label, rng.choice([0, 1, 0x100, 3])], w.pokes()


# ---- edl: load, discard, save, the loop ----------------------------------------------------------------------------------------
def load_world(env, w, state=None):
    rng = env.rng
    w.u32(MAP_STATE, rng.choice([0, 1, 2, 3, 3]) if state is None else state)
    w.u32(0x005614CC, a(env, ROWS))
    w.u32(a(env, ROWS), a(env, BLOCK))
    w.put(a(env, BLOCK), rng.randbytes(0x1E00))
    w.u32(0x005614C8, rng.randrange(0, 4))
    w.u32(VERSION, rng.randrange(0, 5))
    STATE["parse"] = (rng.randrange(0, 5), rng.randrange(0, 4))


@spec(0x00433280, "CustomPark_LoadBuffer", "edl")
def gen_load_buffer(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    load_world(env, w)
    return [a(env, NAMES), rng.choice([0, 1, 1, 0x100])], w.pokes()


@spec(0x00433220, "CustomPark_Discard", "edl")
def gen_discard(env, n):
    reset_state()
    w = Img()
    load_world(env, w)
    return [], w.pokes()


@spec(0x00433250, "CustomPark_UnloadPsx", "edl")
def gen_unload_psx(env, n):
    reset_state()
    return [], {}


@spec(0x00433260, "CustomPark_Save", "edl")
def gen_save(env, n):
    reset_state()
    w = Img()
    w.put(a(env, NAMES), env.rng.randbytes(0x40))
    return [0, a(env, NAMES)], w.pokes()


@spec(0x00432860, "EditorGlobal_Command", "edr")
def gen_command(env, n):
    reset_state()
    w = Img()
    rng = env.rng
    load_world(env, w)
    w.put(a(env, NAMES), rng.randbytes(0x40))
    start_loop(rng, w)
    return [rng.choice([0, 1, 2, 3, 3, 4, 9]), a(env, NAMES)], w.pokes()


def start_loop(rng, w):
    STATE["frame"] = 0
    STATE["frames"] = rng.randrange(1, 5)
    STATE["end"] = rng.choice(["update", "update", "quit", "quit"])
    w.u32(TICK, rng.randrange(1000, 100000))
    w.u32(LAST_TICK, rng.randrange(0, 100000))
    w.u8(FADE_FLAG, rng.randrange(256))
    w.u32(POLY_FLAG, rng.randrange(1 << 32))


@spec(0x00433200, "EditorGlobal_CommandNothing", "edl")
def gen_command_nothing(env, n):
    reset_state()
    return [], {}


@spec(0x00433210, "EditorGlobal_CommandRun", "edr")
def gen_command_run(env, n):
    reset_state()
    w = Img()
    start_loop(env.rng, w)
    return [], w.pokes()


@spec(0x004333C0, "LevelEditor_Run", "edr")
def gen_run(env, n):
    reset_state()
    w = Img()
    start_loop(env.rng, w)
    return [], w.pokes()


# ---- the stubs -------------------------------------------------------------------------------------------------------------------
def edit_stubs(fc, env):
    if getattr(fc, "_editor_stubs", False):
        return
    fc._editor_stubs = True
    under_test()
    rng = env.rng
    last = lambda: env.alloc_log[-1][0] if env.alloc_log else 0      # noqa: E731

    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1)
    ensure(fc, 0x00472EF0, 5, convention="thiscall", this=True, stack_to=SCRATCH_LIST, result=lambda a_: last())
    ensure(fc, 0x004DC3A0, 2, convention="thiscall", this=True, stack_to=SCRATCH_LIST)
    ensure(fc, 0x00449030, 1, text=True, stack_to=SCRATCH_FILE, result=lambda a_: STATE["size"] or rng.randrange(0, 0x10000))
    ensure(fc, 0x00449230, 1, fill=(0, lambda a_: STATE["text"].ljust(STATE["size"], b"\0")[:STATE["size"]]))
    ensure(fc, 0x00449660, 0)
    ensure(fc, 0x004DE180, 2, text=1, stack_to=SCRATCH_FILE)
    ensure(fc, 0x004E73F0, 0)
    ensure(fc, 0x004E7900, 2, text=True, stack_to=SCRATCH_FILE, result=lambda a_: rng.randrange(1 << 32))
    ensure(fc, 0x004E79F0, 3)
    ensure(fc, 0x004E7B60, 3)
    ensure(fc, 0x004E7BC0, 1)
    ensure(fc, 0x0047AA50, 2)
    ensure(fc, 0x0047AC40, 0, result=lambda a_: STATE.get("card", 0))
    ensure(fc, 0x0047AE10, 1)
    ensure(fc, 0x0047AC60, 0, result=lambda a_: STATE.get("card", 0))
    ensure(fc, 0x0047ACE0, 0, result=lambda a_: rng.randrange(0, 3))
    ensure(fc, 0x0047B850, 1)
    ensure(fc, 0x004746D0, 1, convention="thiscall", this=True, text=True, result=lambda a_: STATE.get("card", 0) if STATE.get("card") is not None else 0)
    ensure(fc, 0x00476200, 2, convention="thiscall", this=True)
    ensure(fc, 0x004F6140, 4, text=True)
    ensure(fc, 0x004F6400, 2, text=True)
    # the fake vtable of the wrappers
    ensure(fc, FAKE_SLOTS[0], 1, convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[1], 0, convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[2], 0, convention="thiscall", this=True, result=lambda a_: STATE["done"] if STATE.get("done") is not None else rng.choice([0, 1]))
    ensure(fc, FAKE_SLOTS[3], 0, convention="thiscall", this=True, result=lambda a_: STATE["choice"] if STATE.get("choice") is not None else rng.randrange(0, 50))
    # the generator (4.11c), the generator objects (4.11b) and the editor's own object (4.11e)
    ensure(fc, 0x004436A0, 1, result=env.alloc)
    ensure(fc, 0x004436D0, 1)
    ensure(fc, 0x004397D0, 1, convention="thiscall", this=True, result=lambda a_: last())
    ensure(fc, 0x00439900, 0, convention="thiscall", this=True)
    ensure(fc, 0x004399A0, 1)
    ensure(fc, 0x0043C050, 2, stack_to=SCRATCH_PARSE, pokes=[(1, None, lambda a_: struct.pack("<II", *STATE["parse"]))])
    ensure(fc, 0x0043A280, 3, convention="thiscall", this=True)
    ensure(fc, 0x0043D400, 2, convention="thiscall", this=True)
    ensure(fc, 0x0043E980, 0, convention="thiscall", this=True)
    ensure(fc, 0x0043BF80, 1)
    ensure(fc, 0x0043C480, 1, result=lambda a_: rng.randrange(0, 0x2000))
    ensure(fc, 0x004334D0, 0, convention="thiscall", this=True, result=lambda a_: last())
    ensure(fc, 0x00433CB0, 0, convention="thiscall", this=True,
           result=lambda a_: 1 if STATE.get("end") == "update" and STATE["frame"] >= STATE["frames"] else 0)
    ensure(fc, 0x00433A40, 0, convention="thiscall", this=True)
    ensure(fc, 0x004338E0, 0, convention="thiscall", this=True)
    for address, argc in ((0x0045DB40, 0), (0x004C8F30, 0), (0x004E3F80, 1), (0x004DBD90, 0), (0x004F5FF0, 0), (0x004DAE00, 0), (0x004304B0, 0), (0x00466BE0, 0),
                          (0x00489A10, 0), (0x00418150, 0), (0x0045F3F0, 0), (0x004E8740, 1), (0x004C9300, 1), (0x00430510, 0), (0x00466C00, 0), (0x004DBE60, 0)):
        ensure(fc, address, argc)
    from opentony.memory import mem

    def tick(a_):
        STATE["frame"] += 1
        return (mem.u32(TICK) + rng.choice([1, 2, 3, 7])) & 0xFFFFFFFF

    ensure(fc, 0x004F7CE0, 0, writes=[(TICK, 4, tick)])
    ensure(fc, 0x00452D30, 0, result=lambda a_: 1 if STATE.get("end") == "quit" and STATE["frame"] >= STATE["frames"] else 0)


GROUP_STUBS: dict = {"edg": edit_stubs, "edm": edit_stubs, "edl": edit_stubs, "edr": edit_stubs}
reset_state()
