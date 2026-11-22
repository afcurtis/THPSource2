"""Phase 4.10d case generators, part 2: what the create-a-skater pages share, and the personal-information page (perwin.cpp, 0x004293b0..0x00429dff).

The pages work on windows the editor loop builds, on the custom skater records, the skater manager, the texture-set manager and the pad.  The probe
lays them out in one scratch slab of 0x18000 bytes: the menu group's layout (frontend_specs_menu.py: fake vtables, fonts, properties, strings, sprites,
the object under test at OBJ, children, the allocation arena) takes the first 0x8000 bytes; this module adds

  0x1c0 a fake item vtable with the row methods (position, draw, highlight, navigate, snap, set column)   0x1f0 a fake window-child vtable
  0x8000 the personal page   0x8400 the appearance page   0x8700 the stat page   0x8d80 the points block   0x8f00 the editor object (palettes)
  0x9240 three lists of 0x1c0 bytes   0x9800 twelve item records of 0x100 bytes   0xb000 two saved palettes   0xd000 engine palettes
  0xd400 window children (0x40 each)   0xd800 fake dialogs   0xe000 out values   0x10000 the texture-set manager (0x6a10 bytes)   0x17000 its entries

The pages call the toolkit natively (retail runs the toolkit's real code on the same objects) and reach lists, rows, dialogs and engine objects through
virtual methods: those objects have fake vtables, so each call is a reported service.  The personal page's age row is a real NumberRow.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from create_specs_color import saved_palette
from frontend_specs_menu import (VT_ENTRY_DRAW, FIND_FONTS, FONT_SIZE, ITEM_DRAW, ITEM_HIGHLIGHT, ITEM_NAVIGATE, ITEM_SET_POSITION, ITEM_SNAP, OBJ, PROPS, STRING_POOL,
                                 VIRTUAL_DELETE, VT_ITEM, Image, build_font, child_at, install_menu_stubs, item_fields, poke_props, props_at, seed_menu,
                                 sprite_at, string_at, CHILD_DRAW, CHILD_UPDATE, ELEM_HOOK)
from frontend_specs_menu_screens import entry_object

SLAB = 0x18000
VT_WITEM = 0x01C0
VT_CHILD = 0x01F0
VT_WINDOW = 0x4640      # a fake page vtable: the per-frame hook (slot 7) of the real one is an engine function, so it is reported
PERSONAL = 0x8000
APPEAR = 0x8400
STAT = 0x8700
POINTS = 0x8D80
EDITOR = 0x8F00
LISTS = 0x9240
LIST_STRIDE = 0x1C0
ROWS = 0x9800
ROW_STRIDE = 0x100
SAVED = 0xB000
PALETTES = 0xD000
ELEMS = 0xD400
ELEM_STRIDE = 0x40
DIALOGS = 0xD800
OUTS = 0xE000
TEX = 0x10000
ENTRIES = 0x17000
NSETS = 24
PRESET_IDS = [19, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 77, 78, 79, 80, 81, 82, 85, 87, 89, 91, 95, 105, 108, 120, 157, 196, 204, 234, 240, 272, 280,
              283, 285, 287, 289, 292, 318, 319, 320, 321, 323, 324, 325, 328, 329, 332, 334, 338, 339, 340, 342, 343, 344, 345, 347, 351, 352, 353, 355, 356,
              357, 363, 364, 366, 367, 368]
SET_IDS = list(range(NSETS)) + PRESET_IDS      # the sets the manager holds (the presets name some by id)
NENTRIES = 32

# unreached engine entry points the fake vtables of this module use (one stub each)
ITEM_SET_COLUMN = 0x004AD020
DIALOG_V1 = 0x004AD180
POINTS_UPDATE = 0x004AD7D0
LIST_V3 = 0x004AD8B0
NAME_ENTRY_DRAW = 0x004AD950
# the list's other methods are the menu group's fake list vtable (VT_LIST at 0xc0: slots 0, 2, 4, 5, 6, 7)

CUSTOM = 0x0056A488
MANAGER = 0x0056C580
CAREER = 0x00568A6C
PADS = 0x0056AFF8
CLOCK = 0x0056E320
TEXTURE_GLOBAL = 0x0056A87C
CURRENT_PAD = 0x0056A888
PRESETS_ON = 0x0056A904
SCREEN = 0x029DA394

PRESET_NAMES = [b"Jeremy Andersen\0", b"Aaron Cammarata\0", b"The Pouncer\0", b"Ralph D'amato\0", b"Edwin Fong\0", b"Noel Hines\0", b"Gary Jesdanun\0",
                b"Joel Jewett\0", b"Ryan McMahon\0", b"Nolan Nelson\0", b"Nggdynln\0", b"Johnny Ow\0", b"Scott Pease\0", b"Silvio\0", b"Chris Rausch\0",
                b"Junki Saita\0", b"Kage\0", b"Aaron Skillman\0", b"Darren Thorne\0", b"Jason Uyeda\0", b"Mick West\0", b"Connor Jewett\0", b"Hudson\0",
                b"Robert Earl\0"]

SKWIN_STUBS = [
    # (address, stack arguments, name, options)
    (ITEM_SET_COLUMN, 4, "item_set_column", {"convention": "thiscall", "this": True, "text": True, "keep": slice(1, 4)}),
    (DIALOG_V1, 0, "dialog_v1", {"convention": "thiscall", "this": True}),
    (POINTS_UPDATE, 0, "points_update", {"convention": "thiscall", "this": True}),
    (LIST_V3, 0, "list_v3", {"convention": "thiscall", "this": True}),
    (NAME_ENTRY_DRAW, 2, "name_entry_draw", {"convention": "thiscall", "this": True}),
    (ELEM_HOOK, 0, "element_hook", {"convention": "thiscall", "this": True}),
    (CHILD_UPDATE, 0, "element_update", {"convention": "thiscall", "this": True}),
    (CHILD_DRAW, 2, "element_draw", {"convention": "thiscall", "this": True}),
    (0x00457420, 4, "sprite_construct", {"convention": "thiscall", "this": True, "text": True}),
    (0x00457120, 0, "sprite_init", {"convention": "thiscall", "this": True}),
    (0x004208A0, 3, "engine_004208a0", {"convention": "thiscall", "this": True}),
    (0x00422D80, 4, "engine_00422d80", {"convention": "thiscall", "this": True}),
    (0x00421410, 0, "engine_00421410", {"convention": "thiscall", "this": True}),
    (0x00426310, 2, "engine_00426310", {"convention": "thiscall", "this": True}),
    (0x00425FA0, 4, "engine_00425fa0", {"convention": "thiscall", "this": True, "text": 1}),
    (0x004A1440, 4, "engine_004a1440", {"convention": "thiscall", "this": True}),
    (0x004A1540, 1, "engine_004a1540", {"convention": "thiscall", "this": True}),
    (0x0048AAD0, 2, "palette_read", {"keep": slice(0, 1)}),
    (0x0048AB00, 2, "palette_store", {"keep": slice(0, 1)}),
]


def regions(slab):
    return [(slab, 0xE400), (slab + TEX, 0x2100), (slab + TEX + 0x6600, 0x410), (slab + ENTRIES, NENTRIES * 0x20 + 0x10), (PADS, 0x3C0), (CLOCK, 4),
            (CUSTOM, 0x400), (CURRENT_PAD, 4), (PRESETS_ON, 4), (MANAGER, 0x6A0), (CAREER, 0x1450), (SCREEN, 8), (0x00561CB4, 4)]


SLAB_BYTES_VALUE = SLAB
READ_ONLY_RANGES = [
    (0x00527D54, 4), (0x00525768, 0x10), (0x0055FE10, 0x10), (0x0052544C, 0x10),
    (0x0053A3B8, 0x1D80),      # the character rows
    (0x00526BB0, 0xC40),       # the preset names (rows of 0x80 bytes ended by an empty row)
    (0x0052E380, 0x640),       # the global text pointers (stat names, row labels, ...)
    (0x00531A00, 0x1A00),      # the texts they point at
    (0x00539DF0, 0x40),        # the stance arrays
    (0x005258F0, 0x30), (0x00527940, 0x30), (0x00527C60, 0x10), (0x0052B780, 0x10),
    (0x00525C00, 0x1B0), (0x00525E10, 0x1F0),
]


def seed_skwin(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_menu(env, rng)
    env.pal_bytes = b""
    env.hit_row = -1
    env.dialog_result = 0
    env.materials = {}
    vt = bytearray(0x28)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ITEM_SET_POSITION), (2, ITEM_DRAW), (3, ITEM_HIGHLIGHT), (5, ITEM_NAVIGATE), (6, ITEM_SNAP), (9, ITEM_SET_COLUMN)):
        struct.pack_into("<I", vt, 4 * slot, entry)
    write_chunks(env.slab + VT_WITEM, bytes(vt))
    write_chunks(env.slab + VT_CHILD, struct.pack("<III", VIRTUAL_DELETE, CHILD_UPDATE, CHILD_DRAW))
    write_chunks(env.slab + VT_WINDOW, struct.pack("<IIIIIIII", VIRTUAL_DELETE, 0, 0, 0, 0, 0, 0, ELEM_HOOK))
    # the list's fake vtable also needs its draw (slot 3) and the dialog / points ones live in objects poked per case
    write_chunks(env.slab + 0x00C0 + 12, struct.pack("<I", LIST_V3))
    # the texture-set manager: the sets, their textures and the database entries
    names = ["noshirt", "skinelbw", "plainbl", "stripes", "camoflg", "redjers", "bluejer", "greenty"]
    entries = b""
    for i in range(NENTRIES):
        stem = "".join(rng.choice("abcdefgh") for _ in range(7))
        tone = rng.choice("1112340")
        entries += struct.pack("<II", i, 0) + (stem + tone).encode().ljust(16, b"\0") + bytes([rng.choice([0, 1]), 0, 0, 0]) + bytes(4)
    write_chunks(env.slab + ENTRIES, entries)
    sets = b""
    for i in range(len(SET_IDS)):
        group = 0 if i < 3 else rng.choice([0, 1, 1, 2, 3, 4, 5, 6])
        textures = [env.slab + ENTRIES + 0x20 * rng.randrange(NENTRIES) for _ in range(4)]
        name = names[i] if i < len(names) else f"set{i}"
        sets += (struct.pack("<ii", SET_IDS[i], -1 if rng.random() < 0.7 else rng.randrange(0, i + 1)) + name.encode().ljust(16, b"\0")
                 + f"Display {i}".encode().ljust(24, b"\0") + b"".join(struct.pack("<I", t) for t in textures)
                 + bytes([rng.randrange(1, 5), rng.choice([0, 1]), rng.choice([0, 0, 0, 1]), group]))
    write_chunks(env.slab + TEX + 4, sets)
    write_chunks(env.slab + TEX + 0x6604, struct.pack("<I", len(SET_IDS)))
    return writes


def install_skwin_stubs(fc, env):
    if getattr(fc, "_skwin_installed", False):      # (three groups share these stubs: install them once per run)
        return
    fc._skwin_installed = True
    install_menu_stubs(fc, env, font_keep=None, frame_masks={4: 0xFF, 5: 0xFF, 6: 0xFF})
    results = {
        0x00426310: lambda a: env.dialog_result,
        0x00425FA0: lambda a: env.alloc_log[-1][0],
        0x004A1440: lambda a: env.alloc_log[-1][0],
        0x00457420: lambda a: env.alloc_log[-1][0],
    }
    from opentony.memory import mem

    for address, count, name, options in SKWIN_STUBS:
        options = dict(options)
        if address in results:
            options["result"] = results[address]
        if address == 0x0048AAD0:
            options["fill"] = (1, lambda a: env.pal_bytes[:32 if mem.u8(a[0] + 3) & 1 else 512])
        if address == 0x0048AB00:
            options["blob"] = (1, lambda a: 32 if mem.u8(a[0] + 3) & 1 else 512)
        fc.stub(address, count, name, **options)


def register(group):
    SLAB_BYTES[group] = SLAB
    EXTRA_REGIONS[group] = regions
    READ_ONLY[group] = READ_ONLY_RANGES
    SEEDS[group] = seed_skwin


for _group in ("skpers", "skapp", "skstat"):
    register(_group)

GROUP_STUBS = {group: install_skwin_stubs for group in ("skpers", "skapp", "skstat")}


# ---- shared state builders ----------------------------------------------------------------------------------


def new_image(env):
    return Image(), env.rng


def A(env, offset):
    return env.slab + offset


def rspec(group):
    def make(address, name, this=None):
        resolve = (lambda env: env.slab + this) if this is not None else None
        return spec(address, name, group, this=resolve)
    return make


def world(env, img, rng, pads=True):
    """The clock, the pad edges and the pad the editor listens to."""
    img.u32(CLOCK, rng.choice([0, 15, 30, 31, 45, 59, 60, 61, 90, rng.randrange(0, 100000)]))
    img.u32(CURRENT_PAD, rng.choice([0, 1]))
    if pads:
        for pad in range(2):
            for off in (0x00, 0x30, 0xA0, 0xB0):
                img.u8(0x0056AFF9 + 0x16C * pad + off, rng.choice([0, 0, 1]))
    img.u32(SCREEN, rng.choice([512, 640, 800, 320]))
    img.u32(SCREEN + 4, rng.choice([480, 240, 600, 448]))


def skater_state(env, img, rng, *, character=None, custom=None, stance_ok=False):
    """The skater manager: the active slot, and for each slot a character, a custom skater and a costume; the custom records and career records."""
    active = rng.randrange(0, 8)
    img.u32(MANAGER + 0x64, active)
    for slot in range(8):
        entry = MANAGER + 4 + slot * 0xC
        img.u32(entry, rng.randrange(0, 20))
        img.u32(entry + 4, rng.randrange(0, 4))
        img.u32(entry + 8, rng.randrange(0, 4))
    entry = MANAGER + 4 + active * 0xC
    allowed = [0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19] if stance_ok else list(range(20))
    chosen = rng.choice(allowed) if character is None else character
    img.u32(entry, chosen)
    slot = rng.randrange(0, 4) if custom is None else custom
    img.u32(entry + 4, slot)
    # the career record of the current character, and the four custom records
    base = CAREER + 0x104 * chosen
    img.garbage(base, 0x104, rng)
    img.u32(base + 8, rng.randrange(0, 30))
    for i in range(10):
        img.u8(base + 0x38 + i, rng.randrange(0, 10))
    for s in range(4):
        record = CUSTOM + 0x80 * s
        img.garbage(record, 0x80, rng)
        name = "".join(rng.choice("ABCDEFGHIJ klm") for _ in range(rng.randrange(1, 15)))
        img.put(record, name.encode() + b"\0")
        img.put(record + 0x10, "".join(rng.choice("abcdefgh XY") for _ in range(rng.randrange(1, 15))).encode() + b"\0")
        img.u8(record + 0x20, rng.randrange(8, 99))
        img.u8(record + 0x21, rng.randrange(0, 8))
        img.u8(record + 0x22, rng.choice([0, 1, 1]))
        img.u8(record + 0x23, rng.randrange(0x40, 0x95))
        img.u8(record + 0x24, rng.randrange(0, 2) if stance_ok else rng.randrange(0, 3))
        img.u8(record + 0x25, rng.randrange(0, 2))
        img.u8(record + 0x26, rng.choice([0, 1]))
        for k in range(4):
            img.u8(record + 0x27 + k, rng.randrange(0, 36))
        for k in range(10):
            img.u32(record + 0x2C + 4 * k, rng.randrange(0, 4))
        img.u32(record + 0x54, rng.choice([0, 1]))
        img.u32(record + 0x58, rng.randrange(0, 50))
        for k in range(7):
            img.u32(record + 0x64 + 4 * k, rng.choice(SET_IDS))
    img.u32(TEXTURE_GLOBAL, env.slab + TEX)
    img.u32(0x00561CB4, env.slab + TEX)
    img.u32(PRESETS_ON, rng.choice([0, 1, 1]))
    if rng.random() < 0.6:
        # the typed name is a preset's (in another case): the compare then answers equal for that row
        img.put(CUSTOM + 0x80 * slot, PRESET_NAMES[rng.randrange(0, 24)].swapcase() if rng.random() < 0.5 else PRESET_NAMES[rng.randrange(0, 24)])
    return slot, chosen


def list_object(env, img, rng, index, items, *, highlighted=None, selectable=None):
    """A list (fake vtable: its methods are reported) holding `items` (addresses of item records); one of them is highlighted."""
    base = A(env, LISTS + LIST_STRIDE * index)
    img.garbage(base, 0x1C0, rng)
    img.u32(base, A(env, 0x00C0))
    for i in range(0x60):
        img.u32(base + 0x14 + 4 * i, items[i] if i < len(items) else 0)
    img.u32(base + 0x194, len(items))
    pick = rng.randrange(-1, len(items)) if highlighted is None else highlighted
    img.u32(base + 0x198, max(pick, 0))
    for i, item in enumerate(items):
        img.u8(item + 0x0E, 1 if i == pick else 0)
    return base


def item_row(env, img, rng, slot, ident, *, real_text=False, selectable=None, vtable=None):
    """An item record of the pages' lists (id at +8, selectable at +0xc, highlighted at +0xe, size and place)."""
    base = A(env, ROWS + ROW_STRIDE * slot)
    item_fields(env, img, base, rng, vtable=A(env, VT_WITEM) if vtable is None else vtable, id=ident & 0xFFFFFFFF, selectable=selectable,
                props=rng.randrange(4), text=rng.choice(STRING_POOL[:15]) if real_text else None)
    return base


def element(env, img, rng, index):
    """A window child (fake vtable: update and draw are reported)."""
    base = A(env, ELEMS + ELEM_STRIDE * index)
    img.garbage(base, ELEM_STRIDE, rng)
    img.u32(base, A(env, VT_CHILD))
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    return base


def window_part(env, img, rng, base, size, *, hidden=None, children=None):
    """The ScreenElement / Window fields every page has: hidden flag, place, the child slots."""
    img.garbage(base, size, rng)
    img.u32(base + 0x14, rng.choice([0, 0, 0, 1]) if hidden is None else hidden)
    img.u32(base + 0x20, rng.randrange(-20, 200) & 0xFFFFFFFF)
    img.u32(base + 0x24, rng.randrange(-20, 200) & 0xFFFFFFFF)
    count = rng.randrange(0, 4) if children is None else children
    img.put(base + 0x28, bytes(0x80))
    for i in range(count):
        img.u32(base + 0x28 + 4 * i, element(env, img, rng, 16 + i))
    img.u32(base + 0xA8, count)


def number_row_at(env, img, rng, base, *, value=None):
    """A real NumberRow (age): the fields its methods read, legal."""
    for i in range(4):
        poke_props(env, img, i, rng)
    item_fields(env, img, base, rng, vtable=0x0051856C, props=rng.randrange(4), id=2)
    img.garbage(base + 0x60, 0xB0, rng)
    img.u32(base + 0xE4, 2)
    for c in range(3):
        img.put(base + 0x54 + 0x20 * c, rng.choice(STRING_POOL[:15]) + b"\0")
        img.u32(base + 0xD4 + 4 * c, rng.randrange(0, 200))
        img.u32(base + 0xE8 + 4 * c, props_at(env, rng.randrange(4)))
    low = rng.randrange(6, 12)
    high = low + rng.randrange(20, 90)
    img.u32(base + 0x104, low)
    img.u32(base + 0x108, high)
    img.u32(base + 0x100, rng.randrange(low, high + 1) if value is None else value)
    img.u32(base + 0xD8, rng.randrange(0, 200))
    img.u32(base + 0xEC, props_at(env, rng.randrange(4)))


# ---- the personal page --------------------------------------------------------------------------------------

P = rspec("skpers")


def personal_page(env, img, rng, *, state=None, hidden=None, highlighted=None, typing_entry=True):
    base = A(env, PERSONAL)
    window_part(env, img, rng, base, 0x3B0, hidden=hidden)
    img.u32(base, A(env, VT_WINDOW))
    state = rng.choice([0, 0, 0, 1]) if state is None else state
    img.u32(base + 0x394, state)
    # eight rows: name, hometown, age (a real NumberRow), stance, style, height, "next", weight; ids as the page's list numbers them
    ids = [0, 1, 2, 3, 4, 5, -1, 6]
    rows = []
    for i, ident in enumerate(ids):
        if i == 2:
            number_row_at(env, img, rng, A(env, ROWS + ROW_STRIDE * i))
            rows.append(A(env, ROWS + ROW_STRIDE * i))
        else:
            rows.append(item_row(env, img, rng, i, ident, selectable=rng.choice([0, 1, 1])))
    for i in range(0x60):
        img.u32(base + 0x200 + 4 * i, rows[i] if i < len(rows) else 0)
    items = rows[:7]
    where = None
    if highlighted is not None:
        where = {-1: 6, 6: -1}.get(highlighted, highlighted if 0 <= highlighted <= 5 else -1)
    listing = list_object(env, img, rng, 0, items, highlighted=where)
    img.u32(base + 0x1FC, listing)
    img.u32(base + 0x384, props_at(env, rng.randrange(4)))
    for i in range(4):
        poke_props(env, img, i, rng)
    img.u32(base + 0x388, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x38C, sprite_at(env, rng.randrange(8)))
    entry_object(env, img, rng, real_cells=False)
    img.u32(base + 0x390, A(env, OBJ))
    for i in range(5):
        img.u32(base + 0x398 + 4 * i, element(env, img, rng, i))
    return base


@P(0x004293B0, "PersonalPage_Poll", this=PERSONAL)
def gen_personal_poll(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    personal_page(env, img, rng, highlighted=rng.choice([-1, 0, 0, 1, 1, 2, 3, 4, 5, 6]) if rng.random() < 0.9 else None)
    img.u32(A(env, PERSONAL) + 0x394, rng.choice([0, 0, 1, 1, 2]))
    if rng.random() < 0.6:
        img.u8(0x0056B029, 1)
    return [], img.pokes()


@P(0x00429680, "PersonalPage_Update", this=PERSONAL)
def gen_personal_update(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    personal_page(env, img, rng, highlighted=rng.choice([-1, 0, 1, 2, 3, 4, 5, 6]), state=rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


@P(0x00429910, "PersonalPage_Draw", this=PERSONAL)
def gen_personal_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    page = personal_page(env, img, rng, highlighted=rng.choice([-1, 0, 1, 2, 3, 4, 5, 6]), state=rng.choice([0, 0, 0, 0, 1]))
    if img.b.get(page + 0x394) == 1:
        # typing: the keyboard draws itself through a fake vtable whose draw method is reported
        img.u32(A(env, OBJ), A(env, VT_ENTRY_DRAW))
        img.u32(A(env, VT_ENTRY_DRAW) + 8, NAME_ENTRY_DRAW)
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@P(0x00429CD0, "PersonalPage_HighlightFirst", this=PERSONAL)
def gen_personal_highlight_first(env, n):
    img, rng = new_image(env)
    skater_state(env, img, rng)
    personal_page(env, img, rng, highlighted=rng.choice([-1, 0, 3]), state=0)
    return [], img.pokes()


@P(0x00429CE0, "PersonalPage_IsTyping", this=PERSONAL)
def gen_personal_is_typing(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, PERSONAL), 0x3B0, rng)
    img.u32(A(env, PERSONAL) + 0x394, rng.choice([0, 1, 1, 2, rng.getrandbits(32)]))
    return [], img.pokes()


@P(0x00429CF0, "PersonalPage_ApplyPresetName", this=PERSONAL)
def gen_personal_apply_preset(env, n):
    img, rng = new_image(env)
    skater_state(env, img, rng)
    personal_page(env, img, rng, state=0)
    if rng.random() < 0.5:
        # the typed name is a preset's: the stub answers "equal" for that row
        pass
    return [], img.pokes()
