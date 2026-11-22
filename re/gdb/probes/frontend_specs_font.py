"""Phase 4.10a case generators: the font code (fonttools.cpp, 0x0044a070..0x0044b0a0).

The functions take a Font object (ECX), so the probe builds three fonts in the scratch slab, one per
character mapping (0 letters, 1 digits and colon, 2 the lower-case alphabet), each with a random glyph table, a
sentinel glyph and one fake sprite object per glyph.  Sprite and effect objects get fake vtables whose slots hold
the entry points of retail functions that nothing in a font call reaches (SFX_Pause and its neighbours); the probe
stubs those entries, so every virtual call the font makes is reported as a service call with the object it was
made on.  (A stub only intercepts while a forced call runs.)

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000 + n * 0x1000   font n (0..2): Font object at +0x000, glyph table at +0x200 (count + 1 records of 8 bytes),
                        sprites at +0x500 (one of 0x20 bytes per glyph); the counts are 0x31, 11 and 0x4c
  0x3000                the sprite vtable (+0 deleting destructor, +0xc draw)
  0x3040                an effect object (vtable pointer), 0x3080 its vtable (+0x14 position, +0x18 colour, +0x20 glyph)
  0x3100, 0x3300        two text buffers (0x200 bytes), an empty guard before each
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec

FONT_STRIDE = 0x1000
GLYPHS_AT = 0x200
SPRITES_AT = 0x500
SPRITE_STRIDE = 0x20
SPRITE_VT = 0x3000
EFFECT = 0x3040
EFFECT_VT = 0x3080
TEXT = 0x3100
TEXT2 = 0x3300
SLAB = 0x3600
GLYPH_COUNT = 0x31          # glyph indices 0..0x30 plus the sentinel at 0x31
FONT_TABLE = 0x0056161C
FONT_NAMES = 0x0056163C

# retail entry points no font call reaches: the fake vtables point at them and the probe stubs them
SPRITE_DELETE = 0x004AD9B0
SPRITE_DRAW = 0x004AD9D0
EFFECT_POSITION = 0x004AD9E0
EFFECT_COLOUR = 0x004AD9F0
EFFECT_GLYPH = 0x004ADA10

ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
PUNCT = "?!:.,-%+'#/$_"
ESCAPES = "[]{}<>|~^"


def font_at(env, n):
    return env.slab + n * FONT_STRIDE


def glyphs_at(env, n):
    return font_at(env, n) + GLYPHS_AT


def seed_fonts(env, rng):
    """Three fonts with random glyph tables, set up the way Font_Parse leaves them, mapping built by the retail
    function itself (validated separately)."""
    from opentony.fcall import ForcedCalls, write_chunks

    for n in range(3):
        font = font_at(env, n)
        count = (0x31, 11, 0x4C)[n]       # the lower-case mapping (2) maps a..z to glyphs 0x31..0x4a
        glyphs = glyphs_at(env, n)
        obj = bytearray(0x14C)
        struct.pack_into("<I", obj, 0x00, 0)                         # vtable: not called by the code under test
        struct.pack_into("<I", obj, 0x04, 0)                         # left aligned
        struct.pack_into("<I", obj, 0x08, 0)
        struct.pack_into("<I", obj, 0x0C, 1)                         # one pixel between glyphs
        struct.pack_into("<III", obj, 0x14, 0xFF, 0xFF, 0xFF)
        struct.pack_into("<I", obj, 0x30, 0x1000)                    # scale 1.0
        struct.pack_into("<I", obj, 0x34, glyphs)
        struct.pack_into("<I", obj, 0x38, count)
        struct.pack_into("<I", obj, 0x3C, 0xFFFFFFFF)
        struct.pack_into("<I", obj, 0x44, n)
        write_chunks(font, bytes(obj))
        table = bytearray()
        for g in range(count + 1):
            advance = rng.randrange(3, 21)
            top = rng.randrange(0, 9)
            bottom = top + rng.randrange(4, 17)
            sprite = font + SPRITES_AT + g * SPRITE_STRIDE
            table += struct.pack("<IBBBB", sprite, advance, bottom, top, 0)
        write_chunks(glyphs, bytes(table))
        for g in range(count + 1):
            sprite = font + SPRITES_AT + g * SPRITE_STRIDE
            write_chunks(sprite, struct.pack("<I", env.slab + SPRITE_VT) + bytes(SPRITE_STRIDE - 4))
        ForcedCalls.call(0x0044ABB0, (n,), this=font)               # Font::setMapping: builds the glyph index table
    vt = bytearray(0x10)
    struct.pack_into("<I", vt, 0x00, SPRITE_DELETE)
    struct.pack_into("<I", vt, 0x0C, SPRITE_DRAW)
    write_chunks(env.slab + SPRITE_VT, bytes(vt))
    evt = bytearray(0x30)
    struct.pack_into("<I", evt, 0x14, EFFECT_POSITION)
    struct.pack_into("<I", evt, 0x18, EFFECT_COLOUR)
    struct.pack_into("<I", evt, 0x20, EFFECT_GLYPH)
    write_chunks(env.slab + EFFECT_VT, bytes(evt))
    write_chunks(env.slab + EFFECT, struct.pack("<I", env.slab + EFFECT_VT))
    return {}


SLAB_BYTES["font"] = SLAB
SEEDS["font"] = seed_fonts
READ_ONLY["font"] = []
EXTRA_REGIONS["font"] = lambda slab: [(slab, SLAB), (FONT_TABLE, 0x20), (FONT_NAMES, 0x80)]


def install_font_stubs(fc, env):
    """Fake services of the font group: the virtual calls of glyph sprites and the effect object."""
    from opentony.memory import mem

    rng = env.rng
    fc.stub(SPRITE_DELETE, 1, "font_sprite_delete", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "font_sprite_draw", convention="thiscall", this=True)
    # the effect may change the position and the colour it is handed by pointer
    fc.stub(EFFECT_POSITION, 2, "font_effect_position", convention="thiscall", this=True, deref=[0, 1],
            pokes=[(0, 4, lambda a: mem.u32(a[0]) + rng.randrange(-0x600, 0x600)),
                   (1, 4, lambda a: mem.u32(a[1]) + rng.randrange(-0x600, 0x600))])
    fc.stub(EFFECT_COLOUR, 3, "font_effect_colour", convention="thiscall", this=True,
            pokes=[(0, 1, lambda a: rng.randrange(256)), (1, 1, lambda a: rng.randrange(256)),
                   (2, 1, lambda a: rng.randrange(256))])
    fc.stub(EFFECT_GLYPH, 4, "font_effect_glyph", convention="thiscall", this=True)
    fc.stub(0x0048A500, 1, "free16_slot")


GROUP_STUBS = {"font": install_font_stubs}


# ---- helpers ------------------------------------------------------------------------------------------


def text_of(rng, length, escapes=True, high=False):
    pool = ALPHABET * 3 + PUNCT + (ESCAPES if escapes else "")
    chars = [rng.choice(pool) for _ in range(length)]
    if high and length:
        chars[rng.randrange(length)] = chr(rng.randrange(0x80, 0x100))
    return "".join(chars).encode("latin-1")


def put_text(env, p, offset, data):
    p.put(env.slab + offset, data + b"\0")


def put_font(env, p, n, **fields):
    """Poke the writable fields of font n (everything but the glyph table pointer, the character mapping table and
    the vtable). Unlisted fields keep their seeded values."""
    base = env.base
    font = font_at(env, n)
    block = bytearray(env.bytes(font + 0x04, 0x30))
    tail = bytearray(env.bytes(font + 0x38, 0x10))
    layout = {
        "halign": (block, 0x00, "<I"), "valign": (block, 0x04, "<I"), "spacing": (block, 0x08, "<I"),
        "red": (block, 0x10, "<I"), "green": (block, 0x14, "<I"), "blue": (block, 0x18, "<I"),
        "shadow": (block, 0x1D, "<B"), "shadow_x": (block, 0x20, "<i"), "shadow_y": (block, 0x24, "<i"),
        "mono": (block, 0x28, "<I"), "scale": (block, 0x2C, "<I"),
        "count": (tail, 0x00, "<I"), "clut": (tail, 0x04, "<I"), "effect": (tail, 0x08, "<I"), "mapping": (tail, 0x0C, "<I"),
    }
    for key, value in fields.items():
        buf, off, fmt = layout[key]
        struct.pack_into(fmt, buf, off, value & 0xFFFFFFFF if fmt == "<I" else value)
    p.put(font + 0x04, bytes(block))
    p.put(font + 0x38, bytes(tail))
    return font


def rgb_fields(rng):
    return {"red": rng.randrange(256), "green": rng.randrange(256), "blue": rng.randrange(256)}


# ---- generators ---------------------------------------------------------------------------------------


@spec(0x0044A860, "Font_EscapeClass", "font")
def gen_escape_class(env, n):
    rng = env.rng
    c = rng.choice([ord(ch) for ch in ESCAPES] + [rng.randrange(256) for _ in range(6)])
    return [c], {}


@spec(0x0044A620, "Font_GlyphIndex", "font", this=lambda env: font_at(env, env.font_n))
def gen_glyph_index(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, mapping=rng.choice([0, 0, 1, 2, 2, 3, 5]), count=rng.choice([0x10, 0x24, 0x25, 0x28, 0x30, 0x31, 0x32, 0x40]))
    c = rng.choice([rng.randrange(256), rng.randrange(0x20, 0x7F), ord(rng.choice(PUNCT)), ord(rng.choice(ESCAPES)), ord(rng.choice(ALPHABET))])
    return [c], p.pokes


@spec(0x0044ABB0, "Font_SetMapping", "font", this=lambda env: font_at(env, env.font_n))
def gen_set_mapping(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, count=rng.choice([0x10, 0x25, 0x28, 0x31, 0x32]))
    return [rng.choice([0, 1, 2, 0, 1, 2, 3])], p.pokes


@spec(0x0044AB70, "Font_SetRgb", "font", this=lambda env: font_at(env, env.font_n))
def gen_set_rgb(env, n):
    rng = env.rng
    env.font_n = rng.randrange(3)
    return [rng.getrandbits(32), rng.randrange(256), rng.getrandbits(32)], {}


@spec(0x0044AB90, "Font_SetEffect", "font", this=lambda env: font_at(env, env.font_n))
def gen_set_effect(env, n):
    env.font_n = env.rng.randrange(3)
    return [env.rng.choice([0, env.slab + EFFECT, env.rng.getrandbits(32)])], {}


@spec(0x0044ABA0, "Font_GetEffect", "font", this=lambda env: font_at(env, env.font_n))
def gen_get_effect(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, effect=rng.choice([0, rng.getrandbits(32)]))
    return [], p.pokes


@spec(0x0044AAA0, "Font_CharAdvance", "font", this=lambda env: font_at(env, env.font_n))
def gen_char_advance(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, mapping=rng.choice([0, 1, 2, 0, 1, 2, 4]), spacing=rng.randrange(-3, 6),
             scale=rng.choice([0x1000, 0x800, 0x1400, 0x1800, rng.randrange(0x400, 0x3000)]))
    c = rng.choice([ord(rng.choice(ALPHABET + PUNCT)), ord(":"), ord(rng.choice(ESCAPES)), rng.randrange(256)])
    return [c], p.pokes


def measure_state(env, rng, p):
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, spacing=rng.randrange(-2, 6),
             scale=rng.choice([0x1000, 0x800, 0x1400, 0x2000, rng.randrange(0x400, 0x3000)]))
    text = text_of(rng, rng.randrange(0, 40), high=rng.random() < 0.2)
    put_text(env, p, TEXT, text)
    return [env.slab + TEXT]


for _addr, _name in ((0x0044A4D0, "Font_TextWidth"), (0x0044A570, "Font_TextTop"), (0x0044A5C0, "Font_TextHeight"),
                     (0x0044A540, "Font_TextExtent")):
    def _gen(env, n):
        p = Pokes()
        args = measure_state(env, env.rng, p)
        return args, p.pokes

    spec(_addr, _name, "font", this=lambda env: font_at(env, env.font_n))(_gen)


@spec(0x0044A8E0, "Font_ApplyEscape", "font", this=lambda env: font_at(env, env.font_n))
def gen_apply_escape(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, **rgb_fields(rng))
    # the saved-colour bytes (+0x48..+0x4a) sit in the map table's neighbourhood: poke them separately
    p.put(font_at(env, env.font_n) + 0x48, bytes([rng.randrange(256) for _ in range(3)]))
    c = rng.choice([ord(ch) for ch in ESCAPES] + [rng.randrange(256) for _ in range(3)])
    return [c], p.pokes


@spec(0x0044ABE0, "Font_Ellipsize", "font", this=lambda env: font_at(env, env.font_n))
def gen_ellipsize(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, spacing=rng.randrange(0, 4), scale=rng.choice([0x1000, 0x1000, 0x800, 0x1800]))
    text = text_of(rng, rng.randrange(8, 100), escapes=False)
    put_text(env, p, TEXT, text)
    # a width no smaller than six dots need (a glyph is at most 23 pixels at scale 1.5): the original loops for ever on
    # a width its own ellipsis cannot meet
    return [env.slab + TEXT, rng.randrange(240, 240 + 6 * len(text))], p.pokes


@spec(0x0044A070, "Font_Draw", "font", this=lambda env: font_at(env, env.font_n))
def gen_draw(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    put_font(env, p, env.font_n, halign=rng.choice([0, 1, 2, 0, 1, 2, 3]), valign=rng.randrange(4), spacing=rng.randrange(-2, 5),
             shadow=rng.choice([0, 0, 1]), shadow_x=rng.randrange(-3, 4), shadow_y=rng.randrange(-3, 4),
             mono=rng.choice([0, 0, 0, 1]), scale=rng.choice([0x1000, 0x1000, 0x800, 0x1800, 0, rng.randrange(0x400, 0x3000)]),
             effect=rng.choice([0, 0, env.slab + EFFECT]), clut=0xFFFFFFFF, **rgb_fields(rng))
    text = text_of(rng, rng.randrange(0, 40), high=rng.random() < 0.2)
    put_text(env, p, TEXT, text)
    return [rng.randrange(-40, 700), rng.randrange(-40, 500), env.slab + TEXT, rng.getrandbits(32)], p.pokes


@spec(0x0044AD30, "Font_Unload", "font", this=lambda env: font_at(env, env.font_n))
def gen_unload(env, n):
    rng = env.rng
    p = Pokes()
    env.font_n = rng.randrange(3)
    count = rng.choice([-1, 0, 1, 3, 5, 0x31, 0x40000000])
    count = min(count, 0x31)                       # the loop walks count + 1 glyph records
    put_font(env, p, env.font_n, count=count & 0xFFFFFFFF, clut=rng.choice([0xFFFFFFFF, rng.randrange(0, 16), 0]))
    # some glyph sprites are missing (a null pointer is skipped)
    glyphs = glyphs_at(env, env.font_n)
    for g in range(0, min(max(count + 1, 0), 0x32)):
        if rng.random() < 0.3:
            p.put(glyphs + g * 8, struct.pack("<I", 0))
    return [], p.pokes


def table_state(env, rng, p, names=None):
    """Random names and pointers for the font table (the game's own, restored after every case)."""
    entries = []
    names = names or ["FONT", "MENU", "TITLE", "SCORE", "TIPS", "NUMS", "SMALL", "BIG"]
    block = b""
    for i in range(8):
        name = rng.choice(names).encode()[:15]
        entries.append(name)
        block += name + b"\0" * (0x10 - len(name))
    p.put(FONT_NAMES, block)
    p.put(FONT_TABLE, b"".join(struct.pack("<I", rng.choice([0, 0, rng.getrandbits(32) | 1])) for _ in range(8)))
    return entries


@spec(0x0044AF90, "Font_IsLoaded", "font")
def gen_is_loaded(env, n):
    rng = env.rng
    p = Pokes()
    entries = table_state(env, rng, p)
    wanted = rng.choice(entries + [b"NOPE", b"font", b"MENU"])
    p.put(env.slab + TEXT, wanted + b"\0")
    return [env.slab + TEXT], p.pokes


@spec(0x0044AD80, "Font_ClearTable", "font")
def gen_clear_table(env, n):
    p = Pokes()
    table_state(env, env.rng, p)
    return [], p.pokes
