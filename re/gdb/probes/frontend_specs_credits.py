"""Phase 4.10c case generators: the credits roll (credits.cpp, 0x00426430..0x004270f0).

The roll's manager (0xff4 bytes) and its records are built in the scratch slab.  A text file is poked at the start of the allocation arena (the
first allocation Credits_LoadText makes is the file's buffer, and the stubbed file opener answers with a size), records are constructed from the
arena like the original does, and the records' virtual methods go through fake vtables or, in some cases, their real ones (so the native
dispatch runs the records' own methods).  The loop (Credits_Run) keeps its manager in its own stack frame and runs until the player presses a
button: it is not forced.

Slab layout (offsets from the scratch slab; the whole slab is captured):
  0x0000 fake vtables: 0x00 sprite, 0x10 font, 0x20 record    0x0100 the glyph table    0x0300 two valid fake fonts    0x0600 sixteen strings of 0x40 bytes
  0x0a00 eight sprite objects of 0x60 bytes    0x1000 the manager (0x1000 bytes)    0x2000 sixteen text records (0x44), sixteen bitmap records
  (0x10) and sixteen music markers (0x18)    0x2800 scratch text for the parsers    0x3000 the allocation arena (the file buffer first)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_menu import FONT_SIZE, GLYPHS, Image, build_font

SLAB = 0x8000
VT_SPRITE = 0x0000
VT_FONT = 0x0010
VT_RECORD = 0x0020
FONTS = 0x0300
STRINGS = 0x0600
SPRITES = 0x0A00
SPRITE_STRIDE = 0x60
OBJ = 0x1000
TEXT_RECORDS = 0x2000
BITMAP_RECORDS = 0x2440
MUSIC_RECORDS = 0x2540
SCRATCH = 0x2800
ARENA = 0x3000
ARENA_SIZE = SLAB - ARENA
FILE_BUFFER = 0x400          # what the stubbed file opener answers (the buffer's size)

VT_REAL = {"text": 0x00518790, "bitmap": 0x00518798, "music": 0x005187A0}
FONT_A = 0x00560D08
FONT_B = 0x00560E58

VIRTUAL_DELETE = 0x004AD9B0
SPRITE_DRAW = 0x004AD890
FONT_DRAW = 0x004AD5E0
RECORD_UPDATE = 0x004AD9D0

STRING_POOL = [b"Producer", b"Tony Hawk", b"x", b"A long line of credits text", b"", b"Special thanks", b"S2CRSKW1.BMP", b"credits1.bmp", b"Neversoft", b"Activision",
               b"The Skaters", b"Music by", b"Programming", b"Art", b"Design", b"Thanks to everyone"]


def regions(slab):
    return [(slab, SLAB), (FONT_A, FONT_SIZE), (FONT_B, FONT_SIZE), (0x00560FA4, 4), (0x0056E31C, 4), (0x0056B0A8, 4), (0x0053778C, 4)]


SLAB_BYTES["credits"] = SLAB
EXTRA_REGIONS["credits"] = regions
READ_ONLY["credits"] = [
    (0x00526A70, 0x120),    # file names, messages and the "?" string
    (0x00525440, 0x20),     # s2font2.fnt
]


def seed_credits(env, rng):
    from opentony.fcall import write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    write_chunks(env.slab + VT_SPRITE, struct.pack("<IIII", VIRTUAL_DELETE, 0, 0, SPRITE_DRAW))
    write_chunks(env.slab + VT_FONT, struct.pack("<II", 0, FONT_DRAW))
    write_chunks(env.slab + VT_RECORD, struct.pack("<II", VIRTUAL_DELETE, RECORD_UPDATE))
    table = bytearray()
    for _ in range(0x32):
        top = rng.randrange(0, 9)
        table += struct.pack("<IBBBB", 0, rng.randrange(3, 21), top + rng.randrange(4, 17), top, 0)
    write_chunks(env.slab + GLYPHS, bytes(table))
    for i in range(2):
        build_font(env, rng, env.slab + FONTS + FONT_SIZE * i, VT_FONT)
    for i, text in enumerate(STRING_POOL):
        write_chunks(env.slab + STRINGS + 0x40 * i, text + b"\0")
    for i in range(8):
        write_chunks(env.slab + SPRITES + SPRITE_STRIDE * i, struct.pack("<I", env.slab + VT_SPRITE))
    return {}


SEEDS["credits"] = seed_credits


def install_credits_stubs(fc, env):
    rng = env.rng
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    fc.stub(0x00466B70, 1, "operator_new", result=env.alloc)
    fc.stub(0x00466BB0, 1, "operator_delete")
    fc.stub(0x00449CA0, 0, "font_construct", convention="thiscall", this=True)
    fc.stub(0x0044A060, 0, "font_destroy", convention="thiscall", this=True)
    fc.stub(0x0044B000, 1, "font_find", text=True, result=lambda a: env.slab + FONTS + FONT_SIZE * rng.randrange(2))
    fc.stub(0x00500260, 1, "atexit")
    fc.stub(0x00449030, 1, "engine_00449030", text=True, result=lambda a: FILE_BUFFER)
    fc.stub(0x00449230, 1, "engine_00449230")
    fc.stub(0x00449660, 0, "engine_00449660")
    fc.stub(0x004ABE50, 0, "engine_004abe50")
    fc.stub(0x004AB9C0, 0, "engine_004ab9c0")
    fc.stub(0x004ABC80, 0, "engine_004abc80")
    fc.stub(0x004ABB00, 2, "engine_004abb00")
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00457830, 0, "sprite_height", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    fc.stub(FONT_DRAW, 4, "font_draw", convention="thiscall", this=True, text=2)
    fc.stub(RECORD_UPDATE, 1, "credit_record_update", convention="thiscall", this=True)


GROUP_STUBS = {"credits": install_credits_stubs}


def kspec(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "credits", this=resolve)


def font_ptr(env, rng):
    return env.slab + FONTS + FONT_SIZE * rng.randrange(2)


def string_at(env, i):
    return env.slab + STRINGS + 0x40 * i


def sprite_at(env, i):
    return env.slab + SPRITES + SPRITE_STRIDE * i


def world(env, img, rng):
    img.u32(0x0056E31C, rng.randrange(0, 100000))
    img.u8(0x0056B0A8, rng.choice([0, 1]))
    img.u32(0x0053778C, rng.choice([0, 1]))
    img.u8(0x00560FA5, rng.choice([0, 1]))


# ---- the text file ----------------------------------------------------------------------------------------


def credits_text(rng, lines=None):
    """A credits file: tagged lines ended by '#'."""
    out = bytearray()
    for _ in range(rng.randrange(1, 9) if lines is None else lines):
        kind = rng.choice(["text", "text", "text", "font", "bitmap", "music"])
        if kind == "font":
            out += b"@F  " + rng.choice([b"Heading", b"Name", b""])
        elif kind == "bitmap":
            out += b"@B " + rng.choice([b"s2crskw1.bmp", b"credits1.bmp"])
        elif kind == "music":
            out += b"@M" + bytes([0x30 + rng.randrange(10)]) + b"," + bytes([0x30 + rng.randrange(10)])
        else:
            out += rng.choice([b"Producer", b"Tony Hawk", b"", b"A long line of credits text that stops at forty-eight chars"[:48], b"x"])
        out += b"\r\n"
    out += rng.choice([b"#", b"#\r\n"])
    return bytes(out)


@kspec(0x004266E0, "Credits_LoadText", this=OBJ)
def gen_load_text(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    img.garbage(env.slab + OBJ, 0x1000, rng)
    text = credits_text(rng)
    img.put(env.slab + ARENA, text + b"\0")
    img.put(env.slab + SCRATCH, rng.choice([b"credits.txt", b"music.txt"]) + b"\0")
    return [0x00526A84 if rng.random() < 0.5 else 0x00526A78], img.pokes()


@kspec(0x00426C00, "Credits_ParseLine", this=OBJ)
def gen_parse_line(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + OBJ, 0x1000, rng)
    text = credits_text(rng, lines=1)
    img.put(env.slab + SCRATCH, text + b"\0")
    return [env.slab + SCRATCH], img.pokes()


@kspec(0x00426C80, "Credits_ParseTag", this=OBJ)
def gen_parse_tag(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + OBJ, 0x1000, rng)
    tags = [b"@B ", b"@b ", b"@F  ", b"@f  ", b"@M1,2", b"@m7,3", b"x", b"", b"@Q", b"@Z"]
    text = rng.choice(tags) + b"rest"
    img.put(env.slab + SCRATCH, text + b"\0")
    return [env.slab + SCRATCH], img.pokes()


# ---- the manager ----------------------------------------------------------------------------------------------


def make_records(env, img, rng, count, real):
    """`count` records of random kinds in the pools; returns their addresses."""
    pointers = []
    t = b = m = 0
    for _ in range(count):
        kind = rng.choice(["text", "text", "bitmap", "music"])
        if kind == "text" and t < 16:
            at = env.slab + TEXT_RECORDS + 0x44 * t
            t += 1
            img.garbage(at, 0x44, rng)
            img.put(at + 0x0C, rng.choice(STRING_POOL[:6]) + b"\0")
            img.u32(at + 0x40, font_ptr(env, rng))
            vt = VT_REAL["text"]
        elif kind == "bitmap" and b < 16:
            at = env.slab + BITMAP_RECORDS + 0x10 * b
            b += 1
            img.garbage(at, 0x10, rng)
            img.u32(at + 0x0C, sprite_at(env, rng.randrange(8)))
            vt = VT_REAL["bitmap"]
        elif m < 16:
            at = env.slab + MUSIC_RECORDS + 0x18 * m
            m += 1
            img.garbage(at, 0x18, rng)
            img.u32(at + 0x0C, rng.randrange(0, 10))
            img.u32(at + 0x10, rng.randrange(0, 10))
            img.u32(at + 0x14, rng.choice([0, 1, 2, 3]))
            vt = VT_REAL["music"]
        else:
            continue
        img.u32(at, vt if real else env.slab + VT_RECORD)
        img.u32(at + 0x04, rng.randrange(0, 600))
        img.u32(at + 0x08, rng.randrange(1, 60))
        pointers.append(at)
    return pointers


def manager(env, img, rng, real=False):
    base = env.slab + OBJ
    img.garbage(base, 0x1000, rng)
    records = make_records(env, img, rng, rng.randrange(1, 10), real)
    for i in range(1000):
        img.u32(base + 4 + 4 * i, 0)
    for i, r in enumerate(records):
        img.u32(base + 4 + 4 * i, r)
    img.u32(base + 0xFA4, len(records))
    img.u32(base + 0xFA8, rng.randrange(0, 2000))
    img.u32(base + 0xFAC, rng.randrange(0, 100000))
    img.u32(base, rng.randrange(-1100, 8000) & 0xFFFFFFFF)
    for k in range(8):
        img.garbage(sprite_at(env, k) + 4, 0x20, rng)
    return records


@kspec(0x00426B10, "Credits_Unload", this=OBJ)
def gen_unload(env, n):
    img, rng = Image(), env.rng
    manager(env, img, rng, real=rng.random() < 0.5)
    return [], img.pokes()


@kspec(0x00426B50, "Credits_AdvanceTimeline", this=OBJ)
def gen_advance(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    manager(env, img, rng)
    return [], img.pokes()


@kspec(0x00426BC0, "Credits_UpdateRecords", this=OBJ)
def gen_update_records(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    manager(env, img, rng, real=rng.random() < 0.5)
    return [], img.pokes()


# ---- the records ------------------------------------------------------------------------------------------------


def record(env, img, rng, kind, real=True):
    base = {"text": env.slab + TEXT_RECORDS, "bitmap": env.slab + BITMAP_RECORDS, "music": env.slab + MUSIC_RECORDS}[kind]
    size = {"text": 0x44, "bitmap": 0x10, "music": 0x18}[kind]
    img.garbage(base, size, rng)
    img.u32(base, VT_REAL[kind] if real else env.slab + VT_RECORD)
    img.u32(base + 4, rng.randrange(0, 600))
    img.u32(base + 8, rng.randrange(0, 60))
    if kind == "text":
        img.put(base + 0x0C, rng.choice(STRING_POOL[:6]) + b"\0")
        img.u32(base + 0x40, font_ptr(env, rng))
    elif kind == "bitmap":
        img.u32(base + 0x0C, sprite_at(env, rng.randrange(8)))
    else:
        img.u32(base + 0x0C, rng.randrange(0, 10))
        img.u32(base + 0x10, rng.randrange(0, 10))
        img.u32(base + 0x14, rng.choice([0, 1, 2, 3]))
    for k in range(8):
        img.garbage(sprite_at(env, k) + 4, 0x20, rng)
    return base


@kspec(0x00426D90, "CreditRecord_Destroy", this=TEXT_RECORDS)
def gen_record_destroy(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "text")
    return [], img.pokes()


@kspec(0x00426DA0, "CreditRecord_SetOffset", this=TEXT_RECORDS)
def gen_record_set_offset(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "text")
    return [rng.randrange(0, 5000)], img.pokes()


@kspec(0x00426DB0, "CreditRecord_Nothing", this=TEXT_RECORDS)
def gen_record_nothing(env, n):
    return [env.rng.getrandbits(32)], {}


@kspec(0x00426E60, "CreditText_Construct", this=TEXT_RECORDS)
def gen_text_construct(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + TEXT_RECORDS, 0x44, rng)
    return [string_at(env, rng.randrange(0, 6)), font_ptr(env, rng)], img.pokes()


@kspec(0x00426EE0, "CreditText_Delete", this=TEXT_RECORDS)
def gen_text_delete(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "text")
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@kspec(0x00426F00, "CreditText_Destroy", this=TEXT_RECORDS)
def gen_text_destroy(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "text")
    return [], img.pokes()


@kspec(0x00426F10, "CreditText_Update", this=TEXT_RECORDS)
def gen_text_update(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "text", real=False)
    return [rng.randrange(-100, 700) & 0xFFFFFFFF], img.pokes()


@kspec(0x00426F50, "CreditBitmap_Construct", this=BITMAP_RECORDS)
def gen_bitmap_construct(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + BITMAP_RECORDS, 0x10, rng)
    return [string_at(env, rng.choice([6, 7]))], img.pokes()


@kspec(0x00426FE0, "CreditBitmap_Delete", this=BITMAP_RECORDS)
def gen_bitmap_delete(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "bitmap")
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@kspec(0x00427000, "CreditBitmap_Destroy", this=BITMAP_RECORDS)
def gen_bitmap_destroy(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "bitmap")
    return [], img.pokes()


@kspec(0x00427050, "CreditBitmap_Update", this=BITMAP_RECORDS)
def gen_bitmap_update(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "bitmap", real=False)
    return [rng.randrange(-100, 700) & 0xFFFFFFFF], img.pokes()


@kspec(0x00427080, "CreditMusic_Construct", this=MUSIC_RECORDS)
def gen_music_construct(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + MUSIC_RECORDS, 0x18, rng)
    return [rng.randrange(0, 10), rng.randrange(0, 10)], img.pokes()


@kspec(0x004270B0, "CreditMusic_Delete", this=MUSIC_RECORDS)
def gen_music_delete(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "music")
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@kspec(0x004270D0, "CreditMusic_BaseDestroy", this=MUSIC_RECORDS)
def gen_music_base_destroy(env, n):
    img, rng = Image(), env.rng
    record(env, img, rng, "music")
    return [], img.pokes()


@kspec(0x004270E0, "CreditMusic_Update", this=MUSIC_RECORDS)
def gen_music_update(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    record(env, img, rng, "music")
    return [rng.randrange(-100, 700) & 0xFFFFFFFF], img.pokes()


# ---- the static fonts ----------------------------------------------------------------------------------------------


def flags(env, img, rng):
    img.u8(0x00560FA4, rng.choice([0, 1, 2, 3]))


@kspec(0x00426DC0, "CreditsFontA_Init")
def gen_static_a_init(env, n):
    return [], {}


@kspec(0x00426DD0, "CreditsFontA_Construct")
def gen_static_a_construct(env, n):
    return [], {}


@kspec(0x00426DE0, "CreditsFontA_Atexit")
def gen_static_a_atexit(env, n):
    return [], {}


@kspec(0x00426DF0, "CreditsFontA_Destroy")
def gen_static_a_destroy(env, n):
    img, rng = Image(), env.rng
    flags(env, img, rng)
    return [], img.pokes()


@kspec(0x00426E10, "CreditsFontB_Init")
def gen_static_b_init(env, n):
    return [], {}


@kspec(0x00426E20, "CreditsFontB_Construct")
def gen_static_b_construct(env, n):
    return [], {}


@kspec(0x00426E30, "CreditsFontB_Atexit")
def gen_static_b_atexit(env, n):
    return [], {}


@kspec(0x00426E40, "CreditsFontB_Destroy")
def gen_static_b_destroy(env, n):
    img, rng = Image(), env.rng
    flags(env, img, rng)
    return [], img.pokes()
