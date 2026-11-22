"""Phase 4.10a case generators: the window base (screentools.cpp, 0x004ac430..0x004acf20).

The classes (ScreenElement, Window, TextScreenElement, SpriteScreenElement) work on objects the screens allocate;
the probe builds them in the scratch slab.  Every virtual call they make goes through a fake vtable whose slots hold
the entry points of retail functions nothing here reaches (SFX_Pause and its neighbours), stubbed per slot, so each
call is a reported service call carrying the object.  Services the constructors reach (operator new, the sprite
constructor, the allocator) are stubbed too and hand back slab addresses.

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000  the element vtable (slots 0 delete, 1 update, 2 draw, 5 snap x, 6 snap y, 7 per-frame hook)
  0x0040  the sprite vtable (slot 0 delete, slot 3 draw)      0x0080  the font vtable (slot 1 draw)
  0x0100  a window (0xb0 bytes)      0x0200  35 child element objects of 0x40 bytes
  0x0b00  an element    0x0b80  a text element    0x0bc0  a sprite element
  0x0c00  a sprite object (0x20 bytes, vtable = the sprite vtable)
  0x1800  a font object (0x14c bytes, vtable = the font vtable), its glyph table at 0x1980 (0x32 records)
  0x1b20  four strings of 0x60 bytes     0x1cc0  a sprite file name     0x1d00  the allocation arena (0x300 bytes)
  0x2000  a text window (0x60 bytes)     0x2080  its line colour bytes (0x40)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec
from frontend_specs_font import text_of

ELEM_VT = 0x0000
SPRITE_VT = 0x0040
FONT_VT = 0x0080
WINDOW = 0x0100
CHILDREN = 0x0200
ELEMENT = 0x0B00
TEXT_ELEM = 0x0B80
SPRITE_ELEM = 0x0BC0
SPRITE_OBJ = 0x0C00
FONT = 0x1800
GLYPHS = 0x1980
STRINGS = 0x1B20
FILE_NAME = 0x1CC0
ARENA = 0x1D00
ARENA_SIZE = 0x300
TEXT_WINDOW = 0x2000
LINE_COLOURS = 0x2080
SLAB = 0x2200
EMPTY_STRING = 0x0055FE10
CHILD_COUNT = 35

# retail entry points nothing in a window call reaches; each fake vtable slot gets its own
VIRTUAL_DELETE = 0x004AD9B0
ELEM_UPDATE = 0x004AD9D0
ELEM_DRAW = 0x004AD9E0
ELEM_SNAP_X = 0x004AD9F0
ELEM_SNAP_Y = 0x004ADA10
ELEM_HOOK = 0x004AD870
SPRITE_DRAW = 0x004AD890
FONT_DRAW = 0x004AD5E0


def seed_window(env, rng):
    from opentony.fcall import ForcedCalls, write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    vt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ELEM_UPDATE), (2, ELEM_DRAW), (5, ELEM_SNAP_X), (6, ELEM_SNAP_Y), (7, ELEM_HOOK)):
        struct.pack_into("<I", vt, 4 * slot, entry)
    write_chunks(env.slab + ELEM_VT, bytes(vt))
    svt = bytearray(0x10)
    struct.pack_into("<I", svt, 0x00, VIRTUAL_DELETE)
    struct.pack_into("<I", svt, 0x0C, SPRITE_DRAW)
    write_chunks(env.slab + SPRITE_VT, bytes(svt))
    fvt = bytearray(0x10)
    struct.pack_into("<I", fvt, 0x04, FONT_DRAW)
    write_chunks(env.slab + FONT_VT, bytes(fvt))
    for i in range(CHILD_COUNT):
        write_chunks(env.slab + CHILDREN + 0x40 * i, struct.pack("<I", env.slab + ELEM_VT))
    write_chunks(env.slab + SPRITE_OBJ, struct.pack("<I", env.slab + SPRITE_VT))
    # a font for the text element's width and draw: random glyph table, map built by the retail function
    obj = bytearray(0x14C)
    struct.pack_into("<I", obj, 0x00, env.slab + FONT_VT)
    struct.pack_into("<I", obj, 0x0C, 1)
    struct.pack_into("<I", obj, 0x30, 0x1000)
    struct.pack_into("<I", obj, 0x34, env.slab + GLYPHS)
    struct.pack_into("<I", obj, 0x38, 0x31)
    struct.pack_into("<I", obj, 0x3C, 0xFFFFFFFF)
    write_chunks(env.slab + FONT, bytes(obj))
    table = bytearray()
    for _ in range(0x32):
        top = rng.randrange(0, 9)
        table += struct.pack("<IBBBB", 0, rng.randrange(3, 21), top + rng.randrange(4, 17), top, 0)
    write_chunks(env.slab + GLYPHS, bytes(table))
    ForcedCalls.call(0x0044ABB0, (0,), this=env.slab + FONT)
    strings = [b"", b"PLAY GAME", b"x", text_of(rng, 30, escapes=False)]
    for i, text in enumerate(strings):
        write_chunks(env.slab + STRINGS + 0x60 * i, text + b"\0")
    write_chunks(env.slab + FILE_NAME, b"menu.bmp\0")
    return {}


SLAB_BYTES["window"] = SLAB
SEEDS["window"] = seed_window
READ_ONLY["window"] = [(EMPTY_STRING, 0x10)]
EXTRA_REGIONS["window"] = lambda slab: [(slab, SLAB)]


def install_window_stubs(fc, env):
    rng = env.rng
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(ELEM_UPDATE, 0, "element_update", convention="thiscall", this=True)
    fc.stub(ELEM_DRAW, 2, "element_draw", convention="thiscall", this=True)
    fc.stub(ELEM_SNAP_X, 0, "element_snap_x", convention="thiscall", this=True)
    fc.stub(ELEM_SNAP_Y, 0, "element_snap_y", convention="thiscall", this=True)
    fc.stub(ELEM_HOOK, 0, "element_hook", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    fc.stub(FONT_DRAW, 4, "font_draw", convention="thiscall", this=True, text=2)
    # the sprite is built from `operator new` memory by the engine's sprite constructor (which returns it)
    fc.stub(0x00466B70, 1, "operator_new", result=lambda a: env.slab + SPRITE_OBJ)
    fc.stub(0x00466BB0, 1, "operator_delete")
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.slab + SPRITE_OBJ)
    fc.stub(0x00457120, 0, "sprite_init", convention="thiscall", this=True)
    fc.stub(0x00457820, 0, "sprite_width", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    fc.stub(0x00457830, 0, "sprite_height", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))


GROUP_STUBS = {"window": install_window_stubs}


# ---- helpers ------------------------------------------------------------------------------------------


def wspec(address, name, this=None):
    return spec(address, name, "window", this=(lambda env: env.slab + this) if this is not None else None)


def garbage(rng, size):
    return bytes(rng.randrange(256) for _ in range(size))


def element_state(env, rng, p, base, vtable=True, extra=None, **fields):
    """Poke an element at `base` (offset): random garbage first (so a field the code forgets to write shows), then
    the fields.  The vtable word is the fake element vtable unless told otherwise."""
    data = bytearray(garbage(rng, 0x30))
    struct.pack_into("<I", data, 0, env.slab + ELEM_VT if vtable else rng.getrandbits(32))
    layout = {"pad": (4, "<H"), "tx": (8, "<i"), "ty": (0xC, "<i"), "arg": (0x10, "<I"), "hidden": (0x14, "<I"),
              "delay": (0x18, "<i"), "arrived": (0x1C, "<B"), "x": (0x20, "<i"), "y": (0x24, "<i")}
    for key, value in fields.items():
        off, fmt = layout[key]
        struct.pack_into(fmt, data, off, value if fmt != "<I" else value & 0xFFFFFFFF)
    for off, raw in (extra or {}).items():
        data[off:off + len(raw)] = raw
    p.put(env.slab + base, bytes(data))


def coords(rng):
    return rng.choice([rng.randrange(-300, 700), rng.randrange(-3, 4), 0, rng.randrange(-100000, 100000)])


def slide_state(env, rng, p, base):
    target = coords(rng)
    return dict(tx=target, ty=coords(rng), x=target + rng.choice([0, 1, -1, 2, -2, 3, rng.randrange(-500, 500)]),
                y=rng.choice([coords(rng), 0, 1]), delay=rng.choice([0, 0, 0, 1, 5, -1, -7]), arg=rng.getrandbits(32),
                hidden=rng.choice([0, 0, 1]), arrived=rng.randrange(256))


def window_state(env, rng, p, count=None, fill=None):
    """A window at WINDOW with `count` child pointers in random slots (some slots empty)."""
    count = rng.randrange(0, 33) if count is None else count
    slots = [0] * 32
    chosen = rng.sample(range(32), min(count, 32)) if fill is None else range(min(count, 32))
    for i, slot in enumerate(chosen):
        slots[slot] = env.slab + CHILDREN + 0x40 * i
    state = slide_state(env, rng, p, WINDOW)
    data = bytearray(garbage(rng, 0xB0))
    struct.pack_into("<I", data, 0, env.slab + ELEM_VT)
    for key, off in {"tx": 8, "ty": 0xC, "arg": 0x10, "hidden": 0x14, "delay": 0x18, "x": 0x20, "y": 0x24}.items():
        struct.pack_into("<I", data, off, state[key] & 0xFFFFFFFF)
    data[0x1C] = state["arrived"]
    struct.pack_into("<32I", data, 0x28, *slots)
    struct.pack_into("<I", data, 0xA8, sum(1 for s in slots if s))
    p.put(env.slab + WINDOW, bytes(data))
    return slots


# ---- ScreenElement -------------------------------------------------------------------------------------


@wspec(0x004AC430, "ScreenElement_Construct", this=ELEMENT)
def gen_element_construct(env, n):
    rng = env.rng
    p = Pokes()
    p.put(env.slab + ELEMENT, garbage(rng, 0x40))
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], p.pokes


@wspec(0x004AC490, "ScreenElement_Delete", this=ELEMENT)
def gen_element_delete(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT)
    return [rng.choice([0, 1, 2, 3])], p.pokes


@wspec(0x004AC4B0, "ScreenElement_Destroy", this=ELEMENT)
def gen_element_destroy(env, n):
    p = Pokes()
    element_state(env, env.rng, p, ELEMENT)
    return [], p.pokes


@wspec(0x004AC4C0, "ScreenElement_NoResult")
def gen_no_result(env, n):
    return [], {}


@wspec(0x004AC4D0, "ScreenElement_Update", this=ELEMENT)
def gen_element_update(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT, **slide_state(env, rng, p, ELEMENT))
    return [], p.pokes


@wspec(0x004AC560, "ScreenElement_Nothing", this=ELEMENT)
def gen_nothing(env, n):
    return [], {}


@wspec(0x004AC570, "ScreenElement_SnapX", this=ELEMENT)
def gen_element_snap_x(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT, **slide_state(env, rng, p, ELEMENT))
    return [], p.pokes


@wspec(0x004AC580, "ScreenElement_SnapY", this=ELEMENT)
def gen_element_snap_y(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT, **slide_state(env, rng, p, ELEMENT))
    return [], p.pokes


@wspec(0x004AC590, "ScreenElement_GetX", this=ELEMENT)
def gen_element_x(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT, x=coords(rng))
    return [], p.pokes


@wspec(0x004AC5A0, "ScreenElement_GetY", this=ELEMENT)
def gen_element_y(env, n):
    rng = env.rng
    p = Pokes()
    element_state(env, rng, p, ELEMENT, y=coords(rng))
    return [], p.pokes


# ---- Window --------------------------------------------------------------------------------------------


@wspec(0x004AC5B0, "Window_Construct", this=WINDOW)
def gen_window_construct(env, n):
    rng = env.rng
    p = Pokes()
    p.put(env.slab + WINDOW, garbage(rng, 0xB0))
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], p.pokes


@wspec(0x004AC5F0, "Window_Delete", this=WINDOW)
def gen_window_delete(env, n):
    rng = env.rng
    p = Pokes()
    window_state(env, rng, p)
    return [rng.choice([0, 1, 2, 3])], p.pokes


@wspec(0x004AC610, "Window_Destroy", this=WINDOW)
def gen_window_destroy(env, n):
    p = Pokes()
    window_state(env, env.rng, p)
    return [], p.pokes


@wspec(0x004AC670, "Window_AddElement", this=WINDOW)
def gen_window_add(env, n):
    rng = env.rng
    p = Pokes()
    window_state(env, rng, p, count=rng.choice([0, 1, 5, 31, 32, rng.randrange(0, 33)]))
    child = rng.choice([env.slab + CHILDREN + 0x40 * 34, env.slab + CHILDREN + 0x40 * 33, 0])
    return [child], p.pokes


@wspec(0x004AC730, "Window_Update", this=WINDOW)
def gen_window_update(env, n):
    p = Pokes()
    window_state(env, env.rng, p, fill=True, count=env.rng.choice([0, 1, 2, 5, 32]))
    return [], p.pokes


@wspec(0x0047F1D0, "NetMenu_UpdateJump", this=WINDOW)
def gen_netmenu_update_jump(env, n):
    return gen_window_update(env, n)


@wspec(0x0047FA00, "NetSummary_UpdateJump", this=WINDOW)
def gen_netsummary_update_jump(env, n):
    return gen_window_update(env, n)


@wspec(0x004AC770, "Window_Draw", this=WINDOW)
def gen_window_draw(env, n):
    rng = env.rng
    p = Pokes()
    window_state(env, rng, p, fill=True, count=rng.choice([0, 1, 2, 5, 32]))
    return [coords(rng), coords(rng)], p.pokes


@wspec(0x004AC7C0, "Window_SnapX", this=WINDOW)
def gen_window_snap_x(env, n):
    p = Pokes()
    window_state(env, env.rng, p, fill=True, count=env.rng.choice([0, 1, 2, 5, 32]))
    return [], p.pokes


@wspec(0x004AC800, "Window_SnapY", this=WINDOW)
def gen_window_snap_y(env, n):
    p = Pokes()
    window_state(env, env.rng, p, fill=True, count=env.rng.choice([0, 1, 2, 5, 32]))
    return [], p.pokes


# ---- TextScreenElement ---------------------------------------------------------------------------------


def string_at(env, i):
    return env.slab + STRINGS + 0x60 * i


def text_state(env, rng, p, **fields):
    # +0x28 the element's string (a slab string or an arena block), +0x2c the font
    string = rng.choice([string_at(env, i) for i in range(4)] + [env.slab + ARENA + 0x20])
    element_state(env, rng, p, TEXT_ELEM, extra={0x28: struct.pack("<II", string, env.slab + FONT)}, **fields)


@wspec(0x004AC840, "TextScreenElement_Construct", this=TEXT_ELEM)
def gen_text_construct(env, n):
    rng = env.rng
    p = Pokes()
    p.put(env.slab + TEXT_ELEM, garbage(rng, 0x40))
    return [string_at(env, rng.randrange(1, 4)), env.slab + FONT, coords(rng), coords(rng), rng.choice([-1, coords(rng)]),
            rng.choice([-1, coords(rng)])], p.pokes


@wspec(0x004AC970, "TextScreenElement_Delete", this=TEXT_ELEM)
def gen_text_delete(env, n):
    rng = env.rng
    p = Pokes()
    text_state(env, rng, p)
    return [rng.choice([0, 1, 2, 3])], p.pokes


@wspec(0x004AC990, "TextScreenElement_Destroy", this=TEXT_ELEM)
def gen_text_destroy(env, n):
    p = Pokes()
    text_state(env, env.rng, p)
    return [], p.pokes


@wspec(0x004ACA30, "TextScreenElement_SetString", this=TEXT_ELEM)
def gen_text_set_string(env, n):
    rng = env.rng
    p = Pokes()
    text_state(env, rng, p)
    return [string_at(env, rng.randrange(1, 4))], p.pokes


@wspec(0x004ACB50, "TextScreenElement_Draw", this=TEXT_ELEM)
def gen_text_draw(env, n):
    rng = env.rng
    p = Pokes()
    text_state(env, rng, p, **slide_state(env, rng, p, TEXT_ELEM))
    return [coords(rng), coords(rng)], p.pokes


@wspec(0x004ACBD0, "TextScreenElement_Width", this=TEXT_ELEM)
def gen_text_width(env, n):
    rng = env.rng
    p = Pokes()
    text_state(env, rng, p)
    p.put(env.slab + FONT + 0x30, struct.pack("<I", rng.choice([0x1000, 0x800, 0x1800])))
    return [], p.pokes


# ---- SpriteScreenElement -------------------------------------------------------------------------------


def sprite_state(env, rng, p, **fields):
    element_state(env, rng, p, SPRITE_ELEM, extra={0x28: struct.pack("<I", env.slab + SPRITE_OBJ)}, **fields)


@wspec(0x004ACBE0, "SpriteScreenElement_Construct", this=SPRITE_ELEM)
def gen_sprite_construct(env, n):
    rng = env.rng
    p = Pokes()
    p.put(env.slab + SPRITE_ELEM, garbage(rng, 0x40))
    p.put(env.slab + SPRITE_OBJ + 4, garbage(rng, 0x1C))
    return [env.slab + FILE_NAME, coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)]),
            rng.getrandbits(32), rng.getrandbits(32)], p.pokes


@wspec(0x004ACD60, "SpriteScreenElement_Delete", this=SPRITE_ELEM)
def gen_sprite_delete(env, n):
    rng = env.rng
    p = Pokes()
    sprite_state(env, rng, p)
    return [rng.choice([0, 1, 2, 3])], p.pokes


@wspec(0x004ACD80, "SpriteScreenElement_Destroy", this=SPRITE_ELEM)
def gen_sprite_destroy(env, n):
    p = Pokes()
    sprite_state(env, env.rng, p)
    return [], p.pokes


@wspec(0x004ACE20, "SpriteScreenElement_Draw", this=SPRITE_ELEM)
def gen_sprite_draw(env, n):
    rng = env.rng
    p = Pokes()
    sprite_state(env, rng, p, **slide_state(env, rng, p, SPRITE_ELEM))
    return [coords(rng), coords(rng)], p.pokes


@wspec(0x004ACE60, "SpriteScreenElement_Width", this=SPRITE_ELEM)
def gen_sprite_width(env, n):
    p = Pokes()
    sprite_state(env, env.rng, p)
    return [], p.pokes


@wspec(0x004ACEC0, "SpriteScreenElement_Height", this=SPRITE_ELEM)
def gen_sprite_height(env, n):
    p = Pokes()
    sprite_state(env, env.rng, p)
    return [], p.pokes


# ---- TextWindow (textwin.cpp) --------------------------------------------------------------------------


@wspec(0x004BAD00, "TextWindow_SetLineColour", this=TEXT_WINDOW)
def gen_text_window_set_line_colour(env, n):
    rng = env.rng
    p = Pokes()
    lines = rng.randrange(1, 17)
    data = bytearray(garbage(rng, 0x60))
    struct.pack_into("<I", data, 0x34, env.slab + LINE_COLOURS)
    struct.pack_into("<I", data, 0x50, lines)
    p.put(env.slab + TEXT_WINDOW, bytes(data))
    p.put(env.slab + LINE_COLOURS, garbage(rng, 0x40))
    # lines and colours inside and just outside the legal range (the original only asserts; a bad line writes anyway)
    line = rng.choice([rng.randrange(0, lines), rng.randrange(0, lines), lines, -1, lines + 1][:5 if rng.random() < 0.3 else 2])
    colour = rng.choice([0, 1, 2, 3, 3, 4, -1, 7])
    return [line & 0xFFFFFFFF, colour & 0xFFFFFFFF], p.pokes
