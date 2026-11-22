"""Phase 4.10c case generators: the menu widget toolkit (menu.cpp, 0x0046f680..0x00473500).

The widgets work on objects the screens allocate; the probe builds them in the scratch slab.  A virtual call goes through a
fake vtable whose slots hold the entry points of retail functions nothing here reaches, stubbed one per slot, so each call is
a named service that carries the object (the native replay reports the same calls).  Services the constructors reach
(operator new, the allocator, the font constructors, the engine's rectangle fills and sounds) are stubbed too and hand back
slab addresses.  Some cases give the objects the REAL vtables instead, so the native virtual dispatch runs the toolkit's own
methods the way the original does.

Slab layout (offsets from the scratch slab; the whole slab is captured):
  0x0000  fake item vtable   0x0040 fake sprite vtable   0x0080 fake font vtable   0x00c0 fake list vtable   0x0100 fake grid vtable
  0x0200  the glyph table shared by every fake font (0x32 records)
  0x0400  four MenuProperties objects of 0x180 bytes (a font embedded at +4)      0x0a00  sixteen strings of 0x40 bytes
  0x0f00  eight sprite objects of 0x60 bytes     0x1200  the object under test (0x400 bytes)
  0x1600  sixty-four children of 0x60 bytes      0x3a00  the grid's cell array      0x3b00  id and text arrays
  0x3d00  four fake fonts for Font_Find   0x4240  fake prompt windows   0x4440  the name entry's grid   0x4480  its style   0x4800  the arena
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec

SLAB = 0x8000
VT_ITEM = 0x0000
VT_SPRITE = 0x0040
VT_FONT = 0x0080
VT_LIST = 0x00C0
VT_GRID = 0x0100
GLYPHS = 0x0200
PROPS = [0x0400, 0x0580, 0x0700, 0x0880]
PROPS_SIZE = 0x180
STRINGS = 0x0A00
STRING_STRIDE = 0x40
SPRITES = 0x0F00
SPRITE_STRIDE = 0x60
OBJ = 0x1200
CHILDREN = 0x1600
CHILD_STRIDE = 0x60
CELLS = 0x3A00
ARRAYS = 0x3B00
FIND_FONTS = 0x3D00
FONT_SIZE = 0x14C
GRIDOBJ = 0x4440
ENTRY_PROPS = 0x4480
ARENA = 0x4800
ARENA_SIZE = SLAB - ARENA
FAKE_PROMPTS = 0x4240      # window objects handed back by the prompt window constructor (0x40 bytes each)

ROOT_VTABLE = 0x005184B4
ELEM_HOOK = 0x004ADA20      # an unreached engine function (0x004AD870 is the items' snap stub of this group)
CHILD_UPDATE = 0x004ADBA0   # the window children's update and draw (not the items' set-position and draw: other arguments)
CHILD_DRAW = 0x004ADC30
VT_ENTRY_CHILD = 0x0180
VT_ENTRY_DRAW = 0x0140      # fake NameEntry vtable of the draw cases (slot 7 = ELEM_HOOK)
VT_REAL = {"control": 0x00518E10, "control_b": 0x00518E30, "props": 0x00518E50, "text": 0x00518E54, "row": 0x00518E78,
           "dots": 0x00518EA0, "slider": 0x00518EA8, "slider_dots": 0x00518EB0, "scaled": 0x00518ED4, "list": 0x00518EF8,
           "scroll": 0x00518F18, "grid": 0x00518F38, "entry": 0x00518F40, "wrapper": 0x00518F60}

# retail entry points nothing in a toolkit call reaches; each fake vtable slot gets its own
VIRTUAL_DELETE = 0x004AD9B0
ITEM_SET_POSITION = 0x004AD9D0
ITEM_DRAW = 0x004AD9E0
ITEM_HIGHLIGHT = 0x004AD9F0
ITEM_NAVIGATE = 0x004ADA10
ITEM_SNAP = 0x004AD870
SPRITE_DRAW = 0x004AD890
FONT_DRAW = 0x004AD5E0
LIST_V2 = 0x004AD5B0
LIST_V4 = 0x004AD9C0
LIST_V5 = 0x004ADB90
LIST_V6 = 0x0046F460
LIST_V7 = 0x0046F470
GRID_V1 = 0x0046F480

# the globals a toolkit call reads or writes (mutable: poked per case and restored)
CLOCK = 0x0056E320
SOFTWARE = 0x029DA3A0
PADS = (0x0056AFF8, 0x3C0)

STRING_POOL = [b"PLAY", b"OPTIONS", b"x", b"A longer menu entry", b"Sound", b"MUSIC VOLUME", b"", b"Done", b"CONTROLS",
               b"Select a level to play", b"On", b"Off", b"Level 12", b"SAVE", b"Back", b"abcdefghijklmnopqrstuvwxyz01234"]
LONG_STRING = b"This string is far too long for a menu item"


def mutable_regions(slab):
    return [(slab, SLAB), (CLOCK, 4), (SOFTWARE, 4), PADS]


EXTRA_REGIONS["menu"] = mutable_regions
SLAB_BYTES["menu"] = SLAB
READ_ONLY["menu"] = [
    (0x00527D54, 4),      # the full-screen fill height
    (0x00525768, 0x10),   # the string the menu text's extent is measured with
    (0x00534DAC, 0x30),   # the name entry's labels and punctuation
    (0x0052F65C, 0x30),   # DEL and END labels
    (0x005287A4, 0x10),   # ingame1.fnt
    (0x0055FE10, 0x10),   # the empty string
    (0x0052544C, 0x10),   # s2font2.fnt
    (0x00527840, 0x10),   # prompt window bitmaps
    (0x00534D9C, 0x10),
    (0x00525400, 0x10),
    (0x00525C60, 0x10),
]


def build_font(env, rng, base, vtable_offset=VT_FONT):
    """A valid Font object at `base` (an absolute address): the glyph table, the mapping the retail routine builds, a scale."""
    from opentony.fcall import ForcedCalls, write_chunks

    obj = bytearray(FONT_SIZE)
    struct.pack_into("<I", obj, 0x00, env.slab + vtable_offset)
    struct.pack_into("<I", obj, 0x0C, 1)
    struct.pack_into("<I", obj, 0x30, 0x1000)
    struct.pack_into("<I", obj, 0x34, env.slab + GLYPHS)
    struct.pack_into("<I", obj, 0x38, 0x31)
    struct.pack_into("<I", obj, 0x3C, 0xFFFFFFFF)
    write_chunks(base, bytes(obj))
    ForcedCalls.call(0x0044ABB0, (0,), this=base)


def seed_menu(env, rng):
    from opentony.fcall import write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    vt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ITEM_SET_POSITION), (2, ITEM_DRAW), (3, ITEM_HIGHLIGHT), (5, ITEM_NAVIGATE), (6, ITEM_SNAP)):
        struct.pack_into("<I", vt, 4 * slot, entry)
    write_chunks(env.slab + VT_ITEM, bytes(vt))
    write_chunks(env.slab + VT_SPRITE, struct.pack("<IIII", VIRTUAL_DELETE, 0, 0, SPRITE_DRAW))
    write_chunks(env.slab + VT_FONT, struct.pack("<II", 0, FONT_DRAW))
    lvt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (2, LIST_V2), (4, LIST_V4), (5, LIST_V5), (6, LIST_V6), (7, LIST_V7)):
        struct.pack_into("<I", lvt, 4 * slot, entry)
    write_chunks(env.slab + VT_LIST, bytes(lvt))
    write_chunks(env.slab + VT_GRID, struct.pack("<II", VIRTUAL_DELETE, GRID_V1))
    table = bytearray()
    for _ in range(0x32):
        top = rng.randrange(0, 9)
        table += struct.pack("<IBBBB", 0, rng.randrange(3, 21), top + rng.randrange(4, 17), top, 0)
    write_chunks(env.slab + GLYPHS, bytes(table))
    for base in PROPS:
        build_font(env, rng, env.slab + base + 4)
    for i in range(4):
        build_font(env, rng, env.slab + FIND_FONTS + FONT_SIZE * i)
    for i, text in enumerate(STRING_POOL):
        write_chunks(env.slab + STRINGS + STRING_STRIDE * i, text + b"\0")
    write_chunks(env.slab + STRINGS + STRING_STRIDE * 16, LONG_STRING + b"\0")
    for i in range(8):
        write_chunks(env.slab + SPRITES + SPRITE_STRIDE * i, struct.pack("<I", env.slab + VT_SPRITE))
    return {}


SEEDS["menu"] = seed_menu


def install_menu_stubs(fc, env, font_keep=None, frame_masks=None):
    rng = env.rng
    # (the assertion, printf, allocator and Mem_Delete stubs are frontend_specs_common's)
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)   # the clock callback must not tick
    # allocation: operator new and the loaded-buffer allocator both hand out arena memory
    fc.stub(0x00466B70, 1, "operator_new", result=env.alloc)
    fc.stub(0x00466BB0, 1, "operator_delete")
    # fonts
    fc.stub(0x00449CA0, 0, "font_construct", convention="thiscall", this=True)
    fc.stub(0x0044A060, 0, "font_destroy", convention="thiscall", this=True)
    fc.stub(0x0044B000, 1, "font_find", text=True, result=lambda a: env.slab + FIND_FONTS + FONT_SIZE * rng.randrange(4))
    # the engine's rectangle fills, sound and the other services the widgets reach
    fc.stub(0x004568D0, 5, "fill_rect")
    fc.stub(0x00456600, 7, "selection_box")
    fc.stub(0x00456790, 7, "frame_box", masks=frame_masks)   # (a colour read from a byte has the caller's register bits above it: mask it)
    fc.stub(0x004AD620, 3, "sfx")
    fc.stub(0x004156F0, 1, "option_decrease")
    fc.stub(0x00415820, 0, "option_limit", result=lambda a: rng.randrange(1, 11))
    fc.stub(0x004155A0, 2, "option_can_increase", result=lambda a: rng.choice([0, 1]))
    fc.stub(0x004DC7B0, 0, "slider_refresh", convention="thiscall", this=True)
    fc.stub(0x004A1B40, 7, "prompt_window_construct", convention="thiscall", this=True, text=True,
            result=lambda a: env.slab + FAKE_PROMPTS + 0x40 * rng.randrange(0, 8))
    fc.stub(0x0046F550, 3, "pad_edges", masks={0: 0xFF, 1: 0xFF}, result=lambda a: rng.choice([-1, 0, 1, 2, 3]))
    fc.stub(0x00500260, 1, "atexit")
    # virtual methods
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(ITEM_SET_POSITION, 2, "item_set_position", convention="thiscall", this=True)
    fc.stub(ITEM_DRAW, 0, "item_draw", convention="thiscall", this=True)
    fc.stub(ITEM_HIGHLIGHT, 1, "item_highlight", convention="thiscall", this=True)
    fc.stub(ITEM_NAVIGATE, 1, "item_navigate", convention="thiscall", this=True)
    fc.stub(ITEM_SNAP, 0, "item_snap", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    fc.stub(FONT_DRAW, 4, "font_draw", convention="thiscall", this=True, text=2, keep=font_keep)
    fc.stub(LIST_V2, 0, "list_v2", convention="thiscall", this=True)
    fc.stub(LIST_V4, 0, "list_v4", convention="thiscall", this=True)
    fc.stub(LIST_V5, 1, "list_v5", convention="thiscall", this=True)
    fc.stub(LIST_V6, 1, "list_v6", convention="thiscall", this=True)
    fc.stub(LIST_V7, 0, "list_v7", convention="thiscall", this=True)
    fc.stub(GRID_V1, 0, "grid_v1", convention="thiscall", this=True)


GROUP_STUBS = {"menu": install_menu_stubs,
               # the name entry's font draws reach the font with stack strings: only the font, x and y are recorded (and the text)
               "entry_draw": lambda fc, env: (install_menu_stubs(fc, env, font_keep=slice(0, 2)),
                                              # the window base's per-frame hook (slot 7; the real one is an engine function) is a service
                                              fc.stub(ELEM_HOOK, 0, "element_hook", convention="thiscall", this=True),
                                              fc.stub(CHILD_UPDATE, 0, "element_update", convention="thiscall", this=True),
                                              fc.stub(CHILD_DRAW, 2, "element_draw", convention="thiscall", this=True))}
for _group in ("entry_draw",):
    EXTRA_REGIONS[_group] = mutable_regions
    SLAB_BYTES[_group] = SLAB
    READ_ONLY[_group] = READ_ONLY["menu"]
    SEEDS[_group] = seed_menu


# ---- helpers ------------------------------------------------------------------------------------------


class Image:
    """Bytes to poke, merged into runs of consecutive addresses (so overlapping puts never raise)."""

    def __init__(self):
        self.b: dict[int, int] = {}

    def put(self, address, data):
        for i, value in enumerate(data):
            self.b[address + i] = value

    def u32(self, address, value):
        self.put(address, struct.pack("<I", value & 0xFFFFFFFF))

    def u16(self, address, value):
        self.put(address, struct.pack("<H", value & 0xFFFF))

    def u8(self, address, value):
        self.put(address, bytes([value & 0xFF]))

    def garbage(self, address, size, rng):
        self.put(address, bytes(rng.randrange(256) for _ in range(size)))

    def pokes(self):
        out = Pokes()
        run_start = None
        run = bytearray()
        for address in sorted(self.b):
            if run_start is not None and address == run_start + len(run):
                run.append(self.b[address])
            else:
                if run_start is not None:
                    out.put(run_start, bytes(run))
                run_start = address
                run = bytearray([self.b[address]])
        if run_start is not None:
            out.put(run_start, bytes(run))
        return out.pokes


def mspec(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "menu", this=resolve)


def world(env, img, rng):
    """The mutable globals of a case: the clock, the renderer flag and the pad edges."""
    img.u32(CLOCK, rng.choice([0, 15, 30, 31, 45, 59, 60, 61, 90, rng.randrange(0, 100000)]))
    img.u32(SOFTWARE, rng.choice([0, 1]))
    for pad in range(2):
        for off in (0x0, 0x30, 0xE0):          # the edges at 0x56aff9, 0x56b029 and 0x56b0d9 (+0x16c for pad two)
            img.u8(0x0056AFF9 + 0x16C * pad + off, rng.choice([0, 0, 1]))
    img.u8(0x0056B038, rng.choice([0, 1]))


def string_at(env, index):
    return env.slab + STRINGS + STRING_STRIDE * index


def any_string(env, rng, long_ok=False):
    if long_ok and rng.random() < 0.12:
        return string_at(env, 16)
    return string_at(env, rng.randrange(len(STRING_POOL)))


def props_at(env, i):
    return env.slab + PROPS[i]


def poke_props(env, img, i, rng):
    """The style words of a MenuProperties: legal random values (the font inside keeps its glyph table)."""
    base = env.slab + PROPS[i]
    img.u32(base + 0x08, rng.choice([0, 1]))
    img.u32(base + 0x0C, rng.choice([0, 1, 2]))
    for off in (0x18, 0x1C, 0x20):
        img.u32(base + off, rng.randrange(256))
    img.u32(base + 0x34, rng.choice([0x800, 0xC00, 0x1000, 0x1400, 0x1800]))
    img.u32(base + 0x150, rng.choice([0x1000, 0x1324, 0x1800, 0xDAC, 0xE5B]))
    img.u32(base + 0x154, rng.choice([0x800, 0x1000, 0xC00, 0xBB8]))
    for off in (0x158, 0x15C, 0x160, 0x164, 0x168, 0x16C, 0x170, 0x174, 0x178):
        img.u32(base + off, rng.randrange(256))
    return base


def coords(rng):
    return rng.choice([rng.randrange(-300, 700), rng.randrange(-3, 4), 0, rng.randrange(-100000, 100000)])


def small(rng):
    return rng.choice([rng.randrange(0, 300), rng.randrange(0, 20), 0, 1])


def sprite_at(env, i):
    return env.slab + SPRITES + SPRITE_STRIDE * i


def child_at(env, i):
    return env.slab + CHILDREN + CHILD_STRIDE * i


def item_fields(env, img, base, rng, *, vtable=None, id=None, selectable=None, highlighted=None, props=0, width=None, height=None,
                x=None, y=None, timer=None, pulse=None, just=None, text=None, value=None):
    """A menu item at the absolute address `base`: random garbage first (so a field the code forgets to write shows), then the fields."""
    img.garbage(base, 0x60, rng)
    img.u32(base, env.slab + VT_ITEM if vtable is None else vtable)
    img.u32(base + 0x08, rng.randrange(0, 40) if id is None else id)
    img.u8(base + 0x0C, rng.choice([0, 1, 1]) if selectable is None else selectable)
    img.u8(base + 0x0D, rng.choice([0, 1]))
    img.u8(base + 0x0E, rng.choice([0, 1]) if highlighted is None else highlighted)
    img.u32(base + 0x10, rng.randrange(0, 20) if value is None else value)
    img.u32(base + 0x14, rng.randrange(1, 200) if width is None else width)
    img.u32(base + 0x18, rng.randrange(1, 40) if height is None else height)
    img.u32(base + 0x1C, coords(rng) if x is None else x)
    img.u32(base + 0x20, coords(rng) if y is None else y)
    img.u32(base + 0x24, rng.choice([0, 1, 2, 3, 4, 4, 9]) if timer is None else timer)
    img.u32(base + 0x28, rng.choice([0, 1]) if pulse is None else pulse)
    img.u32(base + 0x2C, rng.choice([0, 1, 1]) if just is None else just)
    img.u32(base + 0x50, props_at(env, props))
    if text is not None:
        img.put(base + 0x30, text + b"\0")


def children(env, img, rng, count, **kw):
    """`count` fake child items (random sizes and states) in the child slots."""
    for i in range(count):
        item_fields(env, img, child_at(env, i), rng, **kw)
