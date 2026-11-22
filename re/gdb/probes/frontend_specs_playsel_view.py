"""Phase 4.10f case generators, part 5: the skater view and its embedded spool state (playsel.cpp 0x004a3e80..0x004a5390 and 0x004a61f0..0x004a6ea0), and the
load window (0x004a6f50..0x004a78f0).

The view is a 0x13dc-byte slab object whose used fields are poked (the rest is zero): its sprites and the skater object are fake pool objects, the custom
definition holder a slab object whose four skaters are pool objects with a face pointer, the engine's spool and render calls are stubs.  The load window is
built the same way, with a fake list and real text rows (the update sets their text through the toolkit's own method).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props, props_at, sprite_at
from frontend_specs_playsel import (PSLAB, PA, PS_OBJ, STATIC_EXTRA, install_play_stubs, pregions, seed_play, set_result)
from frontend_specs_playsel_cases import obj, pspec
from frontend_specs_playsel_sel import PAD_BACK, PAD_CONFIRM, PAD_STRIDE
from frontend_specs_screens import READ_ONLY_COMMON, VT_ANY
from frontend_specs_screens_cases import Pool, common, font_ptr, read_u32, window
from frontend_specs_skwin import VT_WINDOW, item_row, list_object, new_image

SOFTWARE = 0x029DA3A0
REGION_NAMES = 0x0056D428
REGION_SLOTS = 34
REGION_TABLE_SLOTS = 72
OT_PUSHBACK = 0x00564368
SPOOL_MANAGER = 0x0056DB4C
VIEW_STATIC = [(0x00533EA0, 0xB0), (0x00534170, 0x40), (0x00535C80, 0x300), (0x00525FF0, 0x20), (0x0052E5F0, 0x40)]


def vregions(slab):
    return pregions(slab) + [(SOFTWARE, 4), (REGION_NAMES, 0x44 * REGION_TABLE_SLOTS), (OT_PUSHBACK, 8), (SPOOL_MANAGER, 4)]


CAREER_IMAGE = 0x00568A68      # the career save image (0x1dc4 bytes): the load window copies it whole (the real Mem_Copy runs here)


def lregions(slab):
    # (the skwin layout's career region starts at 0x00568a6c: the image region replaces it)
    return [r for r in vregions(slab) if r[0] != 0x00568A6C] + [(CAREER_IMAGE, 0x1DC4)]


for _group in ("view", "load"):
    SLAB_BYTES[_group] = PSLAB
    EXTRA_REGIONS[_group] = lregions if _group == "load" else vregions
    READ_ONLY[_group] = list(READ_ONLY_COMMON) + STATIC_EXTRA + VIEW_STATIC
    SEEDS[_group] = seed_play


def install_view_stubs(fc, env):
    if getattr(fc, "_view_installed", False):
        return
    fc._view_installed = True
    rng = env.rng
    install_play_stubs(fc, env)
    alloc = lambda a: env.alloc_log[-1][0]
    table = [
        (0x004A63B0, 1, True, lambda a: rng.choice([0, 1, 1])),                  # the spool: is the character's model loaded
        (0x004A6340, 1, True, lambda a: rng.choice([0, sprite_at(env, rng.randrange(8))])),   # the spool: the character's face picture
        (0x004A6440, 2, True, None), (0x004A6720, 2, True, None), (0x004A67C0, 0, True, None), (0x004A62B0, 2, True, None),
        (0x004B52B0, 0, True, None), (0x004A6880, 1, True, None),
        (0x00418200, 1, False, None), (0x00418FC0, 7, True, alloc), (0x0042FA10, 1, True, None),
        (0x004B4640, 0, False, lambda a: rng.randrange(0, 30)),
        (0x004B20F0, 1, False, lambda a: getattr(env, "pkt_source", 0)),       # PSX_FindTrailingTables
        (0x00418330, 1, False, lambda a: getattr(env, "pkt_chunk", 0)),        # CommonFe_FindRgbsChunk
    ]
    for address, count, thiscall, result in table:
        options = {"result": result} if result is not None else {}
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall, **options)


def install_load_stubs(fc, env):
    if getattr(fc, "_load_installed", False):
        return
    fc._load_installed = True
    install_play_stubs(fc, env)
    for address, count, thiscall in ((0x004B5300, 0, True), (0x004B5350, 0, True), (0x0047A6C0, 3, False)):
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall)


GROUP_STUBS = {"view": install_view_stubs, "load": install_load_stubs}
V = pspec("view")
LD = pspec("load")


def current_character(img):
    active = read_u32(img, 0x0056A888)
    from frontend_specs_skwin import MANAGER

    return read_u32(img, MANAGER + 0x64 and (MANAGER + 4 + read_u32(img, MANAGER + 0x64) * 0xC))


def view_state(env, img, rng, pool, *, destroy=False, selected=None, custom_flag=None, multi=None):
    base = obj(env, 0)
    window(env, img, rng, base, 0xB4, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    skater = obj(env, 1)
    img.u32(skater, PA(env, VT_ANY))
    img.garbage(skater + 4, 0x160, rng)
    img.u32(base + 0xAC, keep(skater))
    img.u32(base + 0xB0, rng.getrandbits(8))
    for i in range(8):
        img.u32(base + 0xB4 + 4 * i, pool.take())
    img.u32(base + 0xD4, font_ptr(env, rng))
    img.u32(base + 0xD8, rng.choice([0, 1]) if multi is None else multi)
    img.u32(base + 0xDC, pool.take())
    for slot in (0x878, 0x87C, 0x880):
        img.u32(base + slot, keep(pool.take()))
    img.u32(base + 0x884, rng.choice([0, pool.take()]) if destroy else pool.take())
    img.u32(base + 0x888, pool.take())
    img.u32(base + 0x88C, pool.take())
    img.u32(base + 0x890, rng.choice([0, 1, 5, 9, 12, 14, 19, 20, 3, 8]) if selected is None else selected)
    img.u32(base + 0x894, rng.randrange(0, REGION_SLOTS))
    img.u32(base + 0x898, rng.randrange(0, REGION_SLOTS))
    img.u32(base + 0x89C, pool.take())
    img.u32(base + 0x1374, rng.randrange(0x1000, 0x100000))
    holder = obj(env, 2)
    img.u32(holder, PA(env, VT_ANY))
    for i in range(4):
        custom = pool.take() if rng.random() < 0.7 else 0
        if custom:
            img.u32(custom + 0x6C, pool.take())
        img.u32(holder + 4 + 4 * i, custom)
    img.u32(base + 0x1378, keep(holder))
    img.u32(base + 0x138C, rng.choice([0, 1]) if custom_flag is None else custom_flag)
    for i in range(7):
        img.u16(base + 0x1390 + 2 * i, rng.randrange(0, 0x300))
    for slot in range(REGION_SLOTS):
        if rng.random() < 0.5:
            img.u8(REGION_NAMES + 0x44 * slot + 0xA, rng.choice([0, 1]))
        img.u8(REGION_NAMES + 0x44 * slot, rng.choice([0, 0x47]))
    img.u32(0x0056DB4C, rng.randrange(0x1000, 0x100000))
    img.u32(SOFTWARE, rng.choice([0, 1]))
    return base


def view_world(env, img, rng, **kw):
    player = rng.choice([0, 0, 1, 1])
    common(env, img, rng, player=player)
    img.u32(0x0056A888, player)
    return player


@V(0x004A3E80, "SkaterView_Construct", this=PS_OBJ[0])
def gen_view_construct(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    img.u32(0x0056B834, rng.randrange(0x1000, 0x100000))
    return [rng.choice([0, 1]), font_ptr(env, rng) if rng.random() < 0.95 else 0, rng.choice([0x154, coords(rng)]), rng.choice([0x10, coords(rng)]),
            rng.choice([0x154, -1]), rng.choice([-200 & 0xFFFFFFFF, coords(rng)])], img.pokes()


@V(0x004A4360, "SkaterView_ScalarDelete", this=PS_OBJ[0])
def gen_view_delete(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@V(0x004A4380, "SkaterView_Destroy", this=PS_OBJ[0])
def gen_view_destroy(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@V(0x004A44F0, "SkaterView_BuildSprites", this=PS_OBJ[0])
def gen_view_build_sprites(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    base = obj(env, 0)
    img.u32(base + 0xD4, font_ptr(env, rng))
    img.u32(base + 0xD8, rng.choice([0, 1]))
    img.u32(base + 0x14, 0)
    img.u32(base + 0xA8, rng.randrange(0, 5))
    return [], img.pokes()


@V(0x004A4630, "SkaterView_Draw", this=PS_OBJ[0])
def gen_view_draw(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@V(0x004A48E0, "SkaterView_LoadSkater", this=PS_OBJ[0])
def gen_view_load_skater(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@V(0x004A4BC0, "SkaterView_Update", this=PS_OBJ[0])
def gen_view_update(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@V(0x004A4E40, "SkaterView_CreateObject", this=PS_OBJ[0])
def gen_view_create_object(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    view_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


def spool_state(env, img, rng):
    base = obj(env, 0) + 0xE0
    for i in range(20):
        mem = rng.choice([0, 0, rng.randrange(0x1000, 0x100000)])
        img.u32(base + 4 * i, mem)
        img.u32(base + 0x6F4 + 4 * i, rng.choice([0, 0]) if True else 0)
    img.u32(base + 0x644, rng.choice([0xFFFFFFFF, 0, 3]))
    img.u32(base + 0x648, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 5]))
    img.u32(base + 0x64C, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 7]))
    return base


@V(0x004A61F0, "SkaterView_InitSlots", this=PS_OBJ[0] + 0xE0)
def gen_init_slots(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    spool_state(env, img, rng)
    return [], img.pokes()


@V(0x004A6260, "SkaterView_FreeSlots", this=PS_OBJ[0] + 0xE0)
def gen_free_slots(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    pool = Pool(env, img, rng)
    base = spool_state(env, img, rng)
    for i in range(20):
        if rng.random() < 0.3:
            img.u32(base + 0x6F4 + 4 * i, pool.take())
    return [], img.pokes()


@V(0x004A6E60, "SkaterView_SpoolBusy", this=PS_OBJ[0] + 0xE0)
def gen_spool_busy(env, n):
    img, rng = new_image(env)
    spool_state(env, img, rng)
    return [], img.pokes()


@V(0x004A6E90, "SkaterView_SetSpoolFlag", this=PS_OBJ[0] + 0xE0)
def gen_set_spool_flag(env, n):
    img, rng = new_image(env)
    spool_state(env, img, rng)
    return [rng.getrandbits(32)], img.pokes()


@V(0x004A6EA0, "SkaterView_ResetSpool", this=PS_OBJ[0] + 0xE0)
def gen_reset_spool(env, n):
    img, rng = new_image(env)
    view_world(env, img, rng)
    spool_state(env, img, rng)
    return [], img.pokes()


# ---- the load window ------------------------------------------------------------------------------------------------------------------------------

def load_state(env, img, rng, pool, *, destroy=False, state=None, real_rows=False):
    base = obj(env, 3)
    window(env, img, rng, base, 0x248, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.u32(base + 0xAC, rng.choice([0, 1, 2, 2, 2, 5]) if state is None else state)
    img.u32(base + 0xB0, PA(env, 0x25000))      # the copy of the career image: a 0x1dc4-byte buffer in the slab
    ids = [-2, 0, 1, 2, 3, -1]
    rows = [item_row(env, img, rng, 40 + i, ident & 0xFFFFFFFF, real_text=real_rows, vtable=MENU_REAL["text"] if real_rows else None, selectable=1)
            for i, ident in enumerate(ids)]
    slots = [0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC]
    for i in range(0x60):
        img.u32(base + 0xB8 + 4 * i, 0)
    for slot, row in zip(slots, rows):
        img.u32(base + slot, keep(row) if destroy else row)
    img.u32(base + 0xB4, keep(list_object(env, img, rng, 0, rows, highlighted=rng.randrange(0, len(rows)))))
    img.u32(base + 0x238, keep(pool.take()) if destroy else props_at(env, 0))
    img.u32(base + 0x23C, keep(pool.take()) if destroy else props_at(env, 1))
    img.u32(base + 0x240, keep(pool.take()))
    img.u32(base + 0x244, keep(pool.take()))
    return base


@LD(0x004A6F50, "LoadWindow_Construct", this=PS_OBJ[3])
def gen_load_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    return [rng.choice([0, 0, coords(rng)]), rng.choice([0, 0, coords(rng)])], img.pokes()


@LD(0x004A7460, "LoadWindow_ScalarDelete", this=PS_OBJ[3])
def gen_load_delete(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@LD(0x004A7480, "PlayerSelectWindow_EnterLoad", this=PS_OBJ[3])
def gen_load_enter(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng))
    img.u32(0x0056DB4C, rng.randrange(0x1000, 0x100000))
    return [], img.pokes()


@LD(0x004A74C0, "LoadWindow_Destroy", this=PS_OBJ[3])
def gen_load_destroy(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@LD(0x004A7590, "LoadWindow_Step", this=PS_OBJ[3])
def gen_load_step(env, n):
    img, rng = new_image(env)
    player = view_world(env, img, rng)
    load_state(env, img, rng, Pool(env, img, rng), state=rng.choice([0, 1, 1, 2, 2, 2, 2, 9]))
    edge = rng.choice(["confirm", "back", None, "confirm"])
    for name, address in (("confirm", PAD_CONFIRM), ("back", PAD_BACK)):
        img.u8(address + PAD_STRIDE * player, 1 if name == edge else (1 if rng.random() < 0.1 else 0))
    return [], img.pokes()


@LD(0x004A7730, "LoadWindow_Draw", this=PS_OBJ[3])
def gen_load_draw(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng), state=rng.choice([0, 1, 2, 2, 7]))
    return [coords(rng), coords(rng)], img.pokes()


@LD(0x004A77E0, "LoadWindow_Update", this=PS_OBJ[3])
def gen_load_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    load_state(env, img, rng, Pool(env, img, rng), state=rng.choice([0, 1, 2, 2, 2]), real_rows=True)
    return [], img.pokes()


@LD(0x004A7890, "LoadWindow_SaveImage", this=PS_OBJ[3])
def gen_load_save(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@LD(0x004A78B0, "LoadWindow_RestoreImage", this=PS_OBJ[3])
def gen_load_restore(env, n):
    img, rng = new_image(env)
    load_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@LD(0x004A78D0, "LoadWindow_LoadSkater", this=PS_OBJ[3])
def gen_load_skater(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    load_state(env, img, rng, Pool(env, img, rng))
    return [rng.randrange(0, 4)], img.pokes()


# ---- the packet rewrite (0x004a5080) -------------------------------------------------------------------------------------------------------------

@V(0x004A5080, "PlayerSelect_CopyPacketTables")
def gen_copy_packet(env, n):
    import struct

    img, rng = new_image(env)
    packet = obj(env, 4)
    source_at = PA(env, 0x25000)
    chunk_at = PA(env, 0x27000)
    img.u32(packet + 4, 0x200)
    index = rng.randrange(0, 4)
    img.u32(packet + 8, index)
    count = rng.randrange(0, 20)
    for i in range(4):
        img.u32(packet + 0xC + 0x24 * i, count if i == index else rng.randrange(0, 5))
    # the trailing tables
    words = [count]
    table = []
    table += [rng.getrandbits(32) for _ in range(count)]
    n2 = rng.randrange(0, 8)
    table += [n2] + [rng.getrandbits(32) for _ in range(n2)]
    n3 = rng.randrange(0, 4)
    table += [n3] + [rng.getrandbits(32) for _ in range(9 * n3)]
    n4 = rng.choice([0, 0, 1])
    table += [n4] + [rng.getrandbits(32) for _ in range(0x81 * n4)]
    records = rng.choice([0, 1, 2, 3])
    src_words = list(table)
    base = source_at + 4 * len(src_words) + 4
    src_words.append(records)
    offsets_at = len(src_words)
    for i in range(records):
        src_words.append(0)
    bodies = []
    for i in range(records):
        depth = rng.choice([0x10, 0x100, 0x10, 0x100, rng.choice([8, 0x20])]) if rng.random() < 0.9 else rng.choice([8, 0x20])
        width, height = rng.randrange(1, 12), rng.randrange(1, 12)
        record_at = len(src_words)
        src_words[offsets_at + i] = (source_at + 4 * record_at - (source_at + 4 * (offsets_at + i))) & 0xFFFFFFFF
        src_words += [rng.getrandbits(32), depth, rng.getrandbits(32), rng.getrandbits(32), width | (height << 16)]
        size = (width * height) >> (1 if depth == 0x10 else 0)
        size += 4 - (size & 3)
        data = ((size + ((size >> 31) & 3)) >> 2) - 1
        src_words += [rng.getrandbits(32) for _ in range(max(data, 0))]
    img.u32(source_at - 4, 0xFFFFFFFF if rng.random() < 0.95 else rng.getrandbits(32))
    for i, word in enumerate(src_words):
        img.u32(source_at + 4 * i, word)
    colors = rng.choice([0, 0, 1, 4, 16, 17, 0x100, 0x101 if False else 3])
    env.pkt_source = source_at
    env.pkt_chunk = chunk_at if colors else 0
    if colors:
        img.u32(chunk_at - 8, rng.getrandbits(32))
        img.u32(chunk_at - 4, colors * 4)
        for i in range(colors):
            img.u32(chunk_at + 4 * i, rng.getrandbits(32))
    return [packet], img.pokes()
