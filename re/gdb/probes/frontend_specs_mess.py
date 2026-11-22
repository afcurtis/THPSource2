"""Phase 4.10a case generators: the message layer (mess.cpp, 0x00473480..0x00473c60).

The message code works on one global font (the mess font, 0x0056a9a0) and a global list of message nodes
(0x0056ab10).  The probe points the mess font's glyph table at fake glyphs in the scratch slab (fake sprites with
stubbed draw and delete methods, as the font group does), so a Mess_Display call runs the retail Font::draw end to
end and every sprite draw it makes is a reported service call.  Message nodes are poked into the slab per case;
nodes the code allocates come from a bump arena in the slab (the allocator is stubbed).

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000  fake glyph font (Font object at +0, glyph table at +0x200, sprites at +0x400), the table the mess font uses
  0x1000  a second fake Font object (the one Mess_LoadFont is told Font_Load returned), its glyph table and sprites
  0x3000  the sprite vtable
  0x3100  eight message programs of 0x20 bytes
  0x3300  eight message strings of 0x40 bytes
  0x3500  the name Mess_LoadFont is given
  0x3600  the allocation arena (0x800 bytes)
  0x3e00  message nodes (0x20 bytes apart)
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec
from frontend_specs_font import (ALPHABET, ESCAPES, GLYPH_COUNT, GLYPHS_AT, PUNCT, SPRITE_DELETE, SPRITE_DRAW, SPRITE_STRIDE,
                                 SPRITES_AT, SPRITE_VT, text_of)

MESS_FONT = 0x0056A9A0
MESS_NAME = 0x0056AAEC
MESS_LOADED = 0x0056AB0C
MESS_LIST = 0x0056AB10
SIM_TIME = 0x0056E320
PROGRAMS = 0x3100
STRINGS = 0x3300
LOAD_NAME = 0x3500
ARENA = 0x3600
ARENA_SIZE = 0x800
NODES = 0x3E00
SLAB = 0x4000
LOADED_FONT = 0x1000
PROGRAM_COUNT = 8
STRING_COUNT = 8


def write_fake_font(env, rng, font, glyph_count):
    """A Font object at `font` with a random glyph table and fake sprites (the glyph table is at +0x200, sprites at
    +0x400), set up the way seed_fonts does it."""
    from opentony.fcall import write_chunks

    glyphs = font + GLYPHS_AT
    table = bytearray()
    for g in range(glyph_count + 1):
        advance = rng.randrange(3, 21)
        top = rng.randrange(0, 9)
        bottom = top + rng.randrange(4, 17)
        sprite = font + SPRITES_AT + g * SPRITE_STRIDE
        table += struct.pack("<IBBBB", sprite, advance, bottom, top, 0)
        write_chunks(sprite, struct.pack("<I", env.slab + SPRITE_VT) + bytes(SPRITE_STRIDE - 4))
    write_chunks(glyphs, bytes(table))
    return glyphs


def seed_mess(env, rng):
    from opentony.fcall import ForcedCalls, write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    # the glyph source of the mess font
    glyphs = write_fake_font(env, rng, env.slab, GLYPH_COUNT)
    obj = bytearray(0x14C)
    struct.pack_into("<I", obj, 0x04, 1)
    struct.pack_into("<I", obj, 0x08, 1)
    struct.pack_into("<I", obj, 0x0C, 1)
    struct.pack_into("<I", obj, 0x10, 1)
    struct.pack_into("<III", obj, 0x14, 0xFF, 0xFF, 0xFF)
    struct.pack_into("<I", obj, 0x30, 0x1000)
    struct.pack_into("<I", obj, 0x34, glyphs)
    struct.pack_into("<I", obj, 0x38, GLYPH_COUNT)
    struct.pack_into("<I", obj, 0x3C, 0xFFFFFFFF)
    # the mess font: the object's own fields (the vtable pointer stays whatever the game put there)
    write_chunks(MESS_FONT + 4, bytes(obj[4:0x4B]))
    ForcedCalls.call(0x0044ABB0, (0,), this=MESS_FONT)             # Font::setMapping: the mess font's glyph index table
    # the font the load call is handed: a different random glyph table, so the copy is visible
    loaded = env.slab + LOADED_FONT
    glyphs2 = write_fake_font(env, rng, loaded, GLYPH_COUNT)
    obj2 = bytearray(obj)
    struct.pack_into("<I", obj2, 0x04, 2)
    struct.pack_into("<I", obj2, 0x30, 0x1400)
    struct.pack_into("<I", obj2, 0x34, glyphs2)
    struct.pack_into("<I", obj2, 0x44, 2)
    write_chunks(loaded, bytes(obj2))
    ForcedCalls.call(0x0044ABB0, (2,), this=loaded)
    # the message programs: random steps chained forwards and backwards, and the strings they show
    programs = bytearray(PROGRAM_COUNT * 0x20)
    for i in range(PROGRAM_COUNT):
        o = i * 0x20
        duration = rng.randrange(1, 31)
        struct.pack_into("<H", programs, o, duration)
        programs[o + 3] = rng.choice([0, 1, 2, 3])
        struct.pack_into("<HH", programs, o + 4, rng.randrange(0x40, 0x200), rng.randrange(0x40, 0x200))
        for k in range(8, 14):
            programs[o + k] = rng.randrange(256)
        struct.pack_into("<HH", programs, o + 0xE, rng.getrandbits(16), rng.getrandbits(16))
        struct.pack_into("<hhhh", programs, o + 0x12, rng.randrange(-300, 300), rng.randrange(-300, 300),
                         rng.randrange(-300, 300), rng.randrange(-300, 300))
        programs[o + 0x1A] = rng.choice([0, 0, 0, 2, 3, 5])
        struct.pack_into("<H", programs, o + 0x1C, rng.choice([0, 0, 1]))
        j = rng.randrange(PROGRAM_COUNT)
        programs[o + 0x1E] = rng.choice([0, 0x7F, (j - i) & 0xFF, (j - i) & 0xFF])
    write_chunks(env.slab + PROGRAMS, bytes(programs))
    for i in range(STRING_COUNT):
        text = text_of(rng, rng.randrange(1, 24), escapes=False)
        write_chunks(env.slab + STRINGS + i * 0x40, text + b"\0")
    write_chunks(env.slab + LOAD_NAME, b"mess.fnt\0")
    vt = bytearray(0x10)
    struct.pack_into("<I", vt, 0x00, SPRITE_DELETE)
    struct.pack_into("<I", vt, 0x0C, SPRITE_DRAW)
    write_chunks(env.slab + SPRITE_VT, bytes(vt))
    # every font-owned virtual call goes to the stubs, so the loaded flag and name start clear
    return {}


SLAB_BYTES["mess"] = SLAB
SEEDS["mess"] = seed_mess
READ_ONLY["mess"] = []
EXTRA_REGIONS["mess"] = lambda slab: [(slab, SLAB), (MESS_FONT, 0x180), (SIM_TIME, 4)]


def install_mess_stubs(fc, env):
    rng = env.rng
    fc.stub(SPRITE_DELETE, 1, "font_sprite_delete", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "font_sprite_draw", convention="thiscall", this=True)
    # Font_Load hands back the second fake font; Font_Unload only reports the name
    fc.stub(0x0044ADA0, 1, "font_load", text=True, result=lambda a: env.slab + LOADED_FONT)
    fc.stub(0x0044B0A0, 1, "font_unload", text=True)
    # the simulation clock's timer callback (another thread) is held off while a case runs
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)


GROUP_STUBS = {"mess": install_mess_stubs}


def mspec(address, name):
    """A mess spec whose case always pokes the simulation clock: it is the game's live timer (a multimedia timer
    thread advances it), so every case starts it at a known value and the callback is held off during the call."""
    def deco(fn):
        def gen(env, n):
            args, pokes = fn(env, n)
            pokes = dict(pokes)
            if SIM_TIME not in pokes:
                pokes[SIM_TIME] = struct.pack("<I", env.rng.randrange(0, 5000))
            return args, pokes
        return spec(address, name, "mess")(gen)
    return deco


def program_at(env, i):
    return env.slab + PROGRAMS + i * 0x20


def string_at(env, i):
    return env.slab + STRINGS + i * 0x40


def put_mess_font(env, p, **fields):
    """Poke the mess font's writable fields (the glyph table pointer, the map and the vtable stay)."""
    block = bytearray(env.bytes(MESS_FONT + 0x04, 0x30))
    tail = bytearray(env.bytes(MESS_FONT + 0x38, 0x10))
    layout = {
        "halign": (block, 0x00, "<I"), "valign": (block, 0x04, "<I"), "spacing": (block, 0x08, "<I"),
        "red": (block, 0x10, "<I"), "green": (block, 0x14, "<I"), "blue": (block, 0x18, "<I"),
        "shadow": (block, 0x1D, "<B"), "shadow_x": (block, 0x20, "<i"), "shadow_y": (block, 0x24, "<i"),
        "mono": (block, 0x28, "<I"), "scale": (block, 0x2C, "<I"),
    }
    for key, value in fields.items():
        buf, off, fmt = layout[key]
        struct.pack_into(fmt, buf, off, value & 0xFFFFFFFF if fmt == "<I" else value)
    p.put(MESS_FONT + 0x04, bytes(block))
    p.put(MESS_FONT + 0x38, bytes(tail))


def random_mess_font(env, rng, p):
    put_mess_font(env, p, halign=rng.randrange(3), valign=rng.randrange(4), spacing=rng.randrange(-1, 4),
                  red=rng.randrange(256), green=rng.randrange(256), blue=rng.randrange(256),
                  shadow=rng.choice([0, 1]), shadow_x=rng.randrange(-2, 3), shadow_y=rng.randrange(-2, 3),
                  mono=rng.choice([0, 0, 1]), scale=rng.choice([0x1000, 0x800, 0x1800, rng.randrange(0x400, 0x3000)]))


def list_state(env, rng, p, count=None, ages=True):
    """A message list of `count` nodes in the slab: random programs, ages (some past the step's end), offsets and
    stacking.  Returns the node addresses, head first."""
    count = rng.randrange(0, 7) if count is None else count
    nodes = [env.slab + NODES + 0x20 * k for k in range(count)]
    for k, node in enumerate(nodes):
        program = rng.randrange(PROGRAM_COUNT)
        duration = struct.unpack("<H", env.bytes(program_at(env, program), 2))[0]
        age = rng.randrange(0, duration + 3) if ages else 0
        data = struct.pack("<IHHhhB3xIII", string_at(env, rng.randrange(STRING_COUNT)), age, rng.randrange(0, 200),
                           rng.randrange(-100, 100), rng.randrange(-100, 100), rng.choice([0, 0, 1]),
                           program_at(env, program), nodes[k + 1] if k + 1 < count else 0, nodes[k - 1] if k else 0)
        p.put(node, data)
    p.put(MESS_LIST, struct.pack("<I", nodes[0] if nodes else 0))
    return nodes


@mspec(0x00473510, "Mess_SetSortMode")
def gen_sort(env, n):
    return [env.rng.choice([0, 1, 0, 1, 2, -1, 7])], {}


@mspec(0x004734B0, "Mess_SetTextJustify")
def gen_justify(env, n):
    p = Pokes()
    put_mess_font(env, p, halign=env.rng.randrange(5))
    return [env.rng.choice([0, 1, 2, 3, 0x41])], p.pokes


@mspec(0x004734F0, "Mess_SetScale")
def gen_set_scale(env, n):
    return [env.rng.choice([0x100, 0x80, env.rng.randrange(-0x800, 0x800), env.rng.getrandbits(32)])], {}


@mspec(0x00473500, "Mess_GetScale")
def gen_get_scale(env, n):
    p = Pokes()
    put_mess_font(env, p, scale=env.rng.choice([0x1000, env.rng.getrandbits(32), env.rng.randrange(0, 0x10000), 0xFFFFFFF1]))
    return [], p.pokes


@mspec(0x00473580, "Mess_SetRgb")
def gen_set_rgb(env, n):
    rng = env.rng
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], {}


@mspec(0x004739E0, "Mess_DrawText")
def gen_draw_text(env, n):
    rng = env.rng
    p = Pokes()
    random_mess_font(env, rng, p)
    return [rng.randrange(-30, 700), rng.randrange(-30, 500), string_at(env, rng.randrange(STRING_COUNT))], p.pokes


@mspec(0x004735B0, "Mess_DrawPanelText")
def gen_draw_panel(env, n):
    rng = env.rng
    p = Pokes()
    random_mess_font(env, rng, p)
    return [rng.randrange(-30, 700), rng.randrange(-30, 500), string_at(env, rng.randrange(STRING_COUNT)), rng.getrandbits(32)], p.pokes


@mspec(0x004739D0, "Mess_TextWidth")
def gen_text_width(env, n):
    rng = env.rng
    p = Pokes()
    random_mess_font(env, rng, p)
    return [string_at(env, rng.randrange(STRING_COUNT))], p.pokes


@mspec(0x004735D0, "Mess_DeleteMessage")
def gen_delete_message(env, n):
    rng = env.rng
    p = Pokes()
    nodes = list_state(env, rng, p, count=rng.randrange(1, 7))
    return [rng.choice(nodes)], p.pokes


@mspec(0x00473650, "Mess_DeleteAll")
def gen_delete_all(env, n):
    p = Pokes()
    list_state(env, env.rng, p)
    return [], p.pokes


@mspec(0x00473690, "Mess_Exists")
def gen_exists(env, n):
    rng = env.rng
    p = Pokes()
    nodes = list_state(env, rng, p)
    return [rng.choice(nodes + [0x1234, env.slab + NODES + 0x20 * 9])], p.pokes


@mspec(0x004736C0, "Mess_DeleteByText")
def gen_delete_by_text(env, n):
    rng = env.rng
    p = Pokes()
    nodes = list_state(env, rng, p)
    strings = [string_at(env, i) for i in range(STRING_COUNT)]
    return [rng.choice(strings)], p.pokes


@mspec(0x00473730, "Mess_AllocNode")
def gen_alloc_node(env, n):
    p = Pokes()
    list_state(env, env.rng, p)
    return [], p.pokes


@mspec(0x004736F0, "Mess_AddMessage")
def gen_add_message(env, n):
    rng = env.rng
    p = Pokes()
    list_state(env, rng, p)
    return [string_at(env, rng.randrange(STRING_COUNT)), program_at(env, rng.randrange(PROGRAM_COUNT)), rng.choice([0, 1]),
            rng.getrandbits(16), rng.getrandbits(16)], p.pokes


@mspec(0x00473760, "Mess_Update")
def gen_update(env, n):
    p = Pokes()
    list_state(env, env.rng, p)
    return [], p.pokes


@mspec(0x004737C0, "Mess_Display")
def gen_display(env, n):
    rng = env.rng
    p = Pokes()
    random_mess_font(env, rng, p)
    p.put(SIM_TIME, struct.pack("<I", rng.getrandbits(32) if rng.random() < 0.5 else rng.randrange(0, 5000)))
    list_state(env, rng, p, count=rng.randrange(0, 5))
    return [], p.pokes


@mspec(0x00473A00, "Mess_LoadFont")
def gen_load_font(env, n):
    rng = env.rng
    p = Pokes()
    p.put(MESS_LOADED, bytes([rng.choice([0, 0, 1])]))
    return [env.slab + LOAD_NAME, rng.randrange(0, 6), rng.randrange(-3, 4)], p.pokes


@mspec(0x00473B80, "Mess_UnloadFont")
def gen_unload_font(env, n):
    rng = env.rng
    p = Pokes()
    p.put(MESS_LOADED, bytes([rng.choice([0, 1, 1])]))
    p.put(MESS_NAME, b"mess.fnt\0")
    return [], p.pokes


@mspec(0x00473BE0, "Mess_SetCurrentFont")
def gen_notice_a(env, n):
    return [], {}


@mspec(0x00473BF0, "Mess_RetiredNotice")
def gen_notice_b(env, n):
    return [], {}
