"""Phase 4.10d case generators, part 1: the skater windows' shared helpers (commonfe.cpp, 0x00418200..0x0041945f).

The helpers are menu items (ColourText, StepperRow, NumberRow), a model view and an initials window, plus a few plain functions.  They work in
the menu group's scratch slab (frontend_specs_menu.py: fake vtables, fonts, properties, strings, sprites, the object under test at OBJ, the
allocation arena); the first 0x8000 bytes are that layout, 0x8000..0xc000 hold what the helpers add:

  0x8000 the colour presets (36 rows of hue, saturation, value)   0x8200 an array of word pointers   0x8300 an RGBs chunk list
  0x8400 a model table (the count at -4)   0x8500 the skater entry busiest_material reads   0x8700 object records   0x8a00 a light rig
  0x8b00 out values the colour code writes   0x8c00 the texture-set entry table and its names   0x9000 three text elements
  0x9400 the view table the model view draws with

Virtual calls go through the menu group's fake vtables (every call a reported service) or, where a case needs the helper's own methods, through the
real vtables (the native dispatch runs them).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_menu import (CHILDREN, FIND_FONTS, FONT_SIZE, GLYPHS, OBJ, STRING_POOL, VT_ITEM, VT_SPRITE, Image, any_string, coords, item_fields,
                                 install_menu_stubs, poke_props, props_at, seed_menu, small, sprite_at, string_at, world, GROUP_STUBS as MENU_STUBS)

SLAB = 0xC000
PRESETS = 0x8000
WORDS = 0x8200
CHUNKS = 0x8300
MODELS = 0x8400
ENTRY = 0x8500
RECORDS = 0x8700
LIGHT = 0x8A00
OUTS = 0x8B00
ENTRIES = 0x8C00
TEXTS = 0x9000
VIEW = 0x9400

VT_COLOUR = 0x00518528
VT_STEPPER = 0x0051854C
VT_NUMBER = 0x0051856C
VT_MODEL = 0x00518594
VT_INITIALS = 0x005185B4
VT_ELEMENT = 0x005196A8

ENGINE_STUBS = [
    # (address, stack arguments, name, options)
    (0x004B4AD0, 4, "engine_004b4ad0", {}),
    (0x004B4E00, 1, "engine_004b4e00", {}),
    (0x004547F0, 2, "engine_004547f0", {}),
    (0x004B5300, 0, "engine_004b5300", {"convention": "thiscall", "this": True}),
    (0x004B5350, 0, "engine_004b5350", {"convention": "thiscall", "this": True}),
    (0x004674D0, 2, "engine_004674d0", {"keep": slice(0, 1), "text": 1}),
    (0x004B37A0, 4, "engine_004b37a0", {}),
    (0x004B3DF0, 0, "engine_004b3df0", {}),
    (0x0047FD30, 1, "engine_0047fd30", {}),
    (0x0047FDB0, 0, "engine_0047fdb0", {"convention": "thiscall", "this": True}),
    (0x00480610, 0, "engine_00480610", {"convention": "thiscall", "this": True}),
    (0x0047FE30, 1, "engine_0047fe30", {"convention": "thiscall", "this": True, "text": True}),
    (0x00480890, 2, "engine_00480890", {"convention": "thiscall", "this": True}),
    (0x004B32F0, 3, "engine_004b32f0", {}),
    (0x0045E8E0, 3, "engine_0045e8e0", {}),
    (0x00461BF0, 1, "engine_00461bf0", {}),
    (0x0045F530, 1, "engine_0045f530", {}),
    (0x0045F3F0, 0, "engine_0045f3f0", {}),
    (0x00457420, 4, "sprite_construct", {"convention": "thiscall", "this": True, "text": True}),
    (0x00457120, 0, "sprite_init", {"convention": "thiscall", "this": True}),
]


def regions(slab):
    return [(slab, SLAB), (0x0055FAF8, 0x60), (0x0056DB4C, 4), (0x0056D428, 0x220), (0x0056865C, 4), (0x00560080, 4), (0x00560FD4, 4),
            (0x00561C90, 4), (0x00533F38, 4), (0x0053C240, 0x15BC)]


SLAB_BYTES["skhelp"] = SLAB
EXTRA_REGIONS["skhelp"] = regions
READ_ONLY["skhelp"] = [
    (0x00527D54, 4), (0x00525768, 0x10), (0x0055FE10, 0x10), (0x0052544C, 0x10),
    (0x0053DC1C, 0x34),        # the light template
    (0x0053A3B8, 0x1D80),      # the character rows (the custom flag byte of each)
    (0x00525C00, 0x1B0),       # the colour presets
]


def seed_skhelp(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_menu(env, rng)
    env.scores = {}
    env.materials_out = []
    env.entry = 0
    env.region = 0
    # the colour presets: hue 0..359, saturation and value 0..255
    rows = b"".join(struct.pack("<III", rng.randrange(0, 360), rng.randrange(0, 256), rng.randrange(0, 256)) for _ in range(36))
    write_chunks(env.slab + PRESETS, rows)
    for i in range(8):
        write_chunks(env.slab + WORDS + 4 * i, struct.pack("<I", env.slab + 0x0A00 + 0x40 * rng.randrange(0, 15)))
    for i in range(16):
        name = "".join(rng.choice("abcdefghij") for _ in range(rng.randrange(3, 8)))
        write_chunks(env.slab + ENTRIES + 0x80 + 0x10 * i, name.encode() + b"\0")
        write_chunks(env.slab + ENTRIES + 4 * i, struct.pack("<I", env.slab + ENTRIES + 0x80 + 0x10 * i))
    for k in range(3):
        write_chunks(env.slab + TEXTS + 0x30 * k, struct.pack("<I", env.slab + VT_ITEM) + bytes(0x2C))
    return writes


SEEDS["skhelp"] = seed_skhelp


def install_skhelp_stubs(fc, env):
    rng = env.rng
    install_menu_stubs(fc, env, frame_masks={4: 0xFF, 5: 0xFF, 6: 0xFF})
    results = {
        0x004B4E00: lambda a: env.scores.get(a[0], 0),
        0x004547F0: lambda a: env.entry,
        0x004B37A0: lambda a: env.region,
        0x0047FD30: env.alloc,
        0x0047FDB0: lambda a: env.alloc_log[-1][0],
        0x00480610: lambda a: env.alloc_log[-1][0],
        0x00457420: lambda a: env.alloc_log[-1][0],
    }
    for address, count, name, options in ENGINE_STUBS:
        options = dict(options)
        if address == 0x004B4AD0:
            options["keep"] = slice(0, 2)
            options["pokes"] = [(3, 4, lambda a: len(env.materials_out)), (2, None, lambda a: b"".join(struct.pack("<I", m) for m in env.materials_out))]
        if address in results:
            options["result"] = results[address]
        fc.stub(address, count, name, **options)


GROUP_STUBS = {"skhelp": install_skhelp_stubs}


def kspec(address, name, this=OBJ):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "skhelp", this=resolve)


def new_image(env):
    return Image(), env.rng


def abs_(env, offset):
    return env.slab + offset


def sprite_or_none(env, rng):
    return sprite_at(env, rng.randrange(8)) if rng.random() < 0.8 else 0


# ---- plain helpers ------------------------------------------------------------------------------------------


@kspec(0x00418200, "CommonFe_LightDefaults", this=None)
def gen_light_defaults(env, n):
    img, rng = new_image(env)
    img.garbage(abs_(env, LIGHT), 0x60, rng)
    return [abs_(env, LIGHT)], img.pokes()


def poke_colour_tables(img, rng):
    img.garbage(0x0055FAF8, 0x60, rng)
    for i in range(0x14):                      # the colour indices stay small
        img.u8(0x0055FAFA + i, rng.randrange(0, 12))
        img.u8(0x0055FB0E + i, rng.randrange(0, 12))


@kspec(0x004182E0, "CommonFe_ColourIndices", this=None)
def gen_colour_indices(env, n):
    img, rng = new_image(env)
    poke_colour_tables(img, rng)
    a = rng.randrange(0, 20)
    b = rng.randrange(0, 0x10)
    if rng.random() < 0.5:
        img.u8(0x0055FAF8 + b, a)               # the alternate pair applies
    img.garbage(abs_(env, OUTS), 0x10, rng)
    return [a, b, abs_(env, OUTS), abs_(env, OUTS + 8)], img.pokes()


@kspec(0x00418330, "CommonFe_FindRgbsChunk", this=None)
def gen_find_rgbs(env, n):
    img, rng = new_image(env)
    at = abs_(env, CHUNKS)
    img.garbage(at, 0x100, rng)
    chunks = rng.randrange(0, 5)
    place_rgbs = rng.choice([None, None, rng.randrange(0, chunks + 1)])
    cursor = at
    for i in range(chunks + 1):
        if i == chunks:
            tag = 0xFFFFFFFF if place_rgbs is None else 0x73424752
            if place_rgbs is not None and place_rgbs != i:
                tag = 0xFFFFFFFF
            img.u32(cursor, tag)
            if tag != 0xFFFFFFFF:
                img.u32(cursor + 4, 8)
            break
        size = 4 * rng.randrange(0, 8)
        tag = 0x73424752 if place_rgbs == i else rng.choice([0x11111111, 0x22222222, 0x73424753, rng.getrandbits(32) | 1])
        if tag == 0xFFFFFFFF:
            tag = 0x5A5A5A5A
        img.u32(cursor, tag)
        img.u32(cursor + 4, size)
        cursor += 8 + size
    if place_rgbs is not None:
        pass
    return [at], img.pokes()


@kspec(0x00418360, "CommonFe_BusiestMaterial", this=None)
def gen_busiest_material(env, n):
    img, rng = new_image(env)
    entry = abs_(env, ENTRY)
    img.garbage(entry, 0x170, rng)
    region = rng.randrange(0, 8)
    img.u8(entry + 0x1F, region)
    models = rng.randrange(1, 8)
    for i in range(0x13):
        img.u8(entry + 0x150 + i, rng.choice([0x10, 0x00, 0x05, 0x10]) if rng.random() < 0.3 else rng.randrange(0, 0x20))
    if rng.random() < 0.8:
        img.u8(entry + 0x150 + rng.randrange(0, min(models, 0x13)), 0x10)
    # the region record: the model table pointer at +0x14 (the count lives in front of the table)
    img.garbage(0x0056D428, 0x220, rng)
    img.u32(0x0056D428 + region * 0x44 + 0x14, abs_(env, MODELS + 4))
    img.u32(abs_(env, MODELS), models + rng.choice([0, 0, 1, 3]))
    for i in range(8):
        img.u32(abs_(env, MODELS + 4 + 4 * i), 0x00600000 + 0x100 * i)
    env.materials_out = [rng.randrange(0x1000, 0x2000) for _ in range(rng.choice([0, 1, 2, 3, 6, 12]))]
    env.scores = {m: rng.randrange(-3, 40) for m in env.materials_out}
    character = rng.randrange(0, 20)
    return [character, entry], img.pokes()


@kspec(0x00418480, "CommonFe_LoadFaceBitmap", this=None)
def gen_load_face_bitmap(env, n):
    img, rng = new_image(env)
    poke_colour_tables(img, rng)
    a = rng.randrange(0, 20)
    b = rng.randrange(0, 0x10)
    if rng.random() < 0.5:
        img.u8(0x0055FAF8 + b, a)
    img.u32(0x0056DB4C, 0x00700000 + rng.randrange(0, 0x1000) * 16)
    env.entry = abs_(env, ENTRIES + 4 * rng.randrange(0, 16))
    bitmap = abs_(env, RECORDS) if rng.random() < 0.85 else 0
    return [bitmap, a, b], img.pokes()


@kspec(0x00419420, "CommonFe_ReplayLevelFlag", this=None)
def gen_replay_level_flag(env, n):
    img, rng = new_image(env)
    img.u8(0x00560080, rng.choice([0, 0, 1, 2]))
    level = rng.randrange(0, 13)
    img.u32(0x00561C90, level)
    img.garbage(0x0053C240, 0x15BC, rng)
    img.u8(0x0053C240 + level * 0x1AC, rng.choice([0, 1, 5]))
    img.u32(0x00533F38, rng.choice([0, 1, 1, 2, 3]))
    return [], img.pokes()


# ---- ColourText ---------------------------------------------------------------------------------------------


def colour_object(env, img, rng, *, count=None, index=None, selectable=None, highlighted=None, vtable=None, timer=None, sprites=True, table=None):
    base = abs_(env, OBJ)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    item_fields(env, img, base, rng, vtable=VT_COLOUR if vtable is None else vtable, props=props, selectable=selectable, highlighted=highlighted,
                text=rng.choice(STRING_POOL[:15]))
    img.garbage(base + 0x54, 0x28, rng)
    count = rng.choice([1, 2, 5, 12, 36]) if count is None else count
    img.u32(base + 0x58, rng.randrange(0, count) if index is None else index)
    img.u32(base + 0x5C, count)
    img.u32(base + 0x60, abs_(env, PRESETS) if table is None else table)
    img.u32(base + 0x64, rng.randrange(0, 360))
    img.u32(base + 0x68, rng.choice([0x80, rng.randrange(0, 0x100), 0x100]))
    img.u32(base + 0x6C, rng.choice([0x80, rng.randrange(0, 0x100), 0x100]))
    img.u32(base + 0x70, sprite_or_none(env, rng) if sprites else 0)
    img.u32(base + 0x74, sprite_or_none(env, rng) if sprites else 0)
    img.u32(base + 0x78, rng.choice([1, 2, 15, 16, 20, 30]) if timer is None else timer)
    return base


@kspec(0x00418540, "ColourText_Construct")
def gen_colour_construct(env, n):
    img, rng = new_image(env)
    base = abs_(env, OBJ)
    img.garbage(base, 0x80, rng)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    table = abs_(env, PRESETS) if rng.random() < 0.7 else 0x00525C00
    return [rng.randrange(-5, 40) & 0xFFFFFFFF, any_string(env, rng), props_at(env, props), table, rng.choice([1, 12, 36]), sprite_or_none(env, rng),
            sprite_or_none(env, rng)], img.pokes()


@kspec(0x004185E0, "ColourText_Delete")
def gen_colour_delete(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@kspec(0x00418600, "ColourText_Destroy")
def gen_colour_destroy(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418610, "ColourText_Draw")
def gen_colour_draw(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng, selectable=rng.choice([0, 1, 1]), highlighted=rng.choice([0, 1, 1]))
    return [], img.pokes()


@kspec(0x004186F0, "ColourText_SetCustom")
def gen_colour_set_custom(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng)
    return [rng.randrange(0, 360), rng.choice([0x80, rng.randrange(0, 0x100)]), rng.choice([0x80, rng.randrange(0, 0x100)])], img.pokes()


@kspec(0x00418720, "ColourText_Swatch")
def gen_colour_swatch(env, n):
    img, rng = new_image(env)
    obj = colour_object(env, img, rng)
    img.garbage(abs_(env, OUTS), 0x10, rng)
    count = rng.choice([1, 12, 36])
    img.u32(obj + 0x5C, count)
    return [rng.randrange(0, count), abs_(env, OUTS), abs_(env, OUTS + 1), abs_(env, OUTS + 2)], img.pokes()


@kspec(0x004187D0, "ColourText_Colour")
def gen_colour_colour(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng)
    img.garbage(abs_(env, OUTS), 0x20, rng)
    return [abs_(env, OUTS), abs_(env, OUTS + 4), abs_(env, OUTS + 8)], img.pokes()


@kspec(0x00418900, "ColourText_SetIndex")
def gen_colour_set_index(env, n):
    img, rng = new_image(env)
    obj = colour_object(env, img, rng)
    count = rng.choice([1, 12, 36])
    img.u32(obj + 0x5C, count)
    return [rng.choice([rng.randrange(0, count), rng.randrange(-3, count + 3)]) & 0xFFFFFFFF], img.pokes()


@kspec(0x00418970, "ColourText_Index")
def gen_colour_index(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418980, "ColourText_Step")
def gen_colour_step(env, n):
    img, rng = new_image(env)
    colour_object(env, img, rng, count=rng.choice([1, 2, 5, 36]))
    return [rng.choice([0, 1, 1, 0, 2, 3])], img.pokes()


# ---- StepperRow ---------------------------------------------------------------------------------------------


def stepper_object(env, img, rng, *, count=None, vtable=VT_STEPPER, highlighted=None, selectable=None, just=None):
    base = abs_(env, OBJ)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    item_fields(env, img, base, rng, vtable=vtable, props=0, selectable=selectable, highlighted=highlighted, just=just)
    img.garbage(base + 0x60, 0xA0, rng)
    count = rng.randrange(1, 7) if count is None else count
    img.u32(base + 0xF4, count)
    img.u32(base + 0xFC, props_at(env, props))
    for i in range(6):
        img.put(base + 0x31 + 0x20 * i, rng.choice(STRING_POOL[:14])[:0x1C] + b"\0")
    img.u32(base + 0x10, rng.randrange(0, count + 2))
    img.u8(base + 0x30, rng.choice([0, 1, 0]))
    img.u32(base + 0x14, rng.randrange(1, 200))
    img.u32(base + 0x2C, rng.choice([0, 1, 1]) if just is None else just)
    return base


@kspec(0x004189C0, "StepperRow_Construct")
def gen_stepper_construct(env, n):
    img, rng = new_image(env)
    base = abs_(env, OBJ)
    img.garbage(base, 0x100, rng)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    count = rng.randrange(0, 7)
    return [rng.randrange(-5, 40) & 0xFFFFFFFF, props_at(env, props), abs_(env, WORDS), count, rng.randrange(0, 8)], img.pokes()


@kspec(0x00418B20, "StepperRow_Delete")
def gen_stepper_delete(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@kspec(0x00418B40, "StepperRow_DestroyThunk")
def gen_stepper_thunk(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418B50, "StepperRow_Destroy")
def gen_stepper_destroy(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418B60, "StepperRow_SetPosition")
def gen_stepper_position(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF], img.pokes()


@kspec(0x00418B80, "StepperRow_Draw")
def gen_stepper_draw(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng, just=rng.choice([0, 1, 1, 1]) if rng.random() < 0.95 else 2)
    return [], img.pokes()


@kspec(0x00418D30, "StepperRow_Step")
def gen_stepper_step(env, n):
    img, rng = new_image(env)
    stepper_object(env, img, rng)
    return [rng.choice([0, 1, 0, 1, 2])], img.pokes()


# ---- NumberRow ----------------------------------------------------------------------------------------------


def number_object(env, img, rng, *, vtable=VT_NUMBER, value=None):
    base = abs_(env, OBJ)
    for i in range(4):
        poke_props(env, img, i, rng)
    item_fields(env, img, base, rng, vtable=vtable, props=rng.randrange(4))
    img.garbage(base + 0x60, 0xB0, rng)
    img.u32(base + 0xE4, 2)
    for c in range(3):
        img.put(base + 0x54 + 0x20 * c, rng.choice(STRING_POOL[:15]) + b"\0")
        img.u32(base + 0xD4 + 4 * c, rng.randrange(0, 200))
        img.u32(base + 0xE8 + 4 * c, props_at(env, rng.randrange(4)))
    low = rng.randrange(-5, 10)
    high = low + rng.randrange(0, 90)
    img.u32(base + 0x104, low & 0xFFFFFFFF)
    img.u32(base + 0x108, high & 0xFFFFFFFF)
    img.u32(base + 0x100, (rng.randrange(low, high + 1) if value is None else value) & 0xFFFFFFFF)
    img.u32(base + 0xD8, rng.randrange(0, 200))
    img.u32(base + 0xEC, props_at(env, rng.randrange(4)))
    return base


@kspec(0x00418D80, "NumberRow_Construct")
def gen_number_construct(env, n):
    img, rng = new_image(env)
    base = abs_(env, OBJ)
    img.garbage(base, 0x110, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    low = rng.randrange(-5, 10)
    high = low + rng.randrange(0, 90)
    value = rng.choice([rng.randrange(low, high + 1), rng.randrange(-20, 120)])
    return [rng.randrange(-5, 40) & 0xFFFFFFFF, any_string(env, rng), value & 0xFFFFFFFF, low & 0xFFFFFFFF, high & 0xFFFFFFFF, rng.randrange(0, 200),
            props_at(env, rng.randrange(4)), props_at(env, rng.randrange(4)), sprite_or_none(env, rng), sprite_or_none(env, rng)], img.pokes()


@kspec(0x00418E40, "NumberRow_Delete")
def gen_number_delete(env, n):
    img, rng = new_image(env)
    number_object(env, img, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@kspec(0x00418E60, "NumberRow_Destroy")
def gen_number_destroy(env, n):
    img, rng = new_image(env)
    number_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418E70, "NumberRow_Value")
def gen_number_value(env, n):
    img, rng = new_image(env)
    number_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00418E80, "NumberRow_SetValue")
def gen_number_set_value(env, n):
    img, rng = new_image(env)
    base = number_object(env, img, rng)
    low = rng.randrange(-5, 10)
    high = low + rng.randrange(0, 90)
    img.u32(base + 0x104, low & 0xFFFFFFFF)
    img.u32(base + 0x108, high & 0xFFFFFFFF)
    return [rng.choice([rng.randrange(low, high + 1), rng.randrange(-20, 120)]) & 0xFFFFFFFF], img.pokes()


@kspec(0x00418F20, "NumberRow_Step")
def gen_number_step(env, n):
    img, rng = new_image(env)
    base = number_object(env, img, rng)
    if rng.random() < 0.3:
        img.u32(base + 0x100, rng.choice([-9, 200, 1000]) & 0xFFFFFFFF)
    return [rng.choice([0, 1, 0, 1, 2])], img.pokes()


# ---- ModelView ----------------------------------------------------------------------------------------------


def model_object(env, img, rng, *, animated=None):
    base = abs_(env, OBJ)
    item_fields(env, img, base, rng, vtable=VT_MODEL)
    img.garbage(base, 0x44, rng)
    img.u32(base, VT_MODEL)
    animated = rng.random() < 0.5 if animated is None else animated
    record_a = abs_(env, RECORDS)
    record_b = abs_(env, RECORDS + 0x180)
    img.garbage(record_a, 0x60, rng)
    img.garbage(record_b, 0x170, rng)
    img.u32(record_a, abs_(env, VT_ITEM))
    img.u32(record_b, abs_(env, VT_ITEM))
    img.u32(base + 0x2C, 0 if animated else record_a)
    img.u32(base + 0x30, record_b if animated else 0)
    img.u32(base + 0x34, rng.randrange(0, 8))
    img.u32(base + 0x38, rng.randrange(0, 600))
    img.u32(base + 0x3C, rng.randrange(0, 400))
    img.u32(base + 0x28, rng.randrange(0, 0x100))
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x18, rng.choice([0, 0, 1, 5, -3 & 0xFFFFFFFF]))
    return base, animated


@kspec(0x00418FC0, "ModelView_Construct")
def gen_model_construct(env, n):
    img, rng = new_image(env)
    base = abs_(env, OBJ)
    img.garbage(base, 0x44, rng)
    env.region = rng.randrange(0, 8)
    img.garbage(0x0056D428, 0x220, rng)
    for r in range(8):
        img.u8(0x0056D428 + r * 0x44 + 9, rng.choice([0, 1]))      # (0x0056d431 is +9 of the record)
    return [string_at(env, rng.randrange(0, 15)), rng.randrange(0, 600), rng.randrange(0, 400), rng.randrange(0, 0x100),
            rng.randrange(-100, 100) & 0xFFFFFFFF, rng.randrange(-100, 100) & 0xFFFFFFFF, rng.randrange(-100, 100) & 0xFFFFFFFF], img.pokes()


@kspec(0x00419140, "ModelView_Delete")
def gen_model_delete(env, n):
    img, rng = new_image(env)
    model_object(env, img, rng)
    img.garbage(0x0056D428, 0x220, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@kspec(0x00419160, "ModelView_Destroy")
def gen_model_destroy(env, n):
    img, rng = new_image(env)
    model_object(env, img, rng)
    img.garbage(0x0056D428, 0x220, rng)
    return [], img.pokes()


@kspec(0x004191F0, "ModelView_Update")
def gen_model_update(env, n):
    img, rng = new_image(env)
    model_object(env, img, rng)
    return [], img.pokes()


@kspec(0x00419220, "ModelView_Draw")
def gen_model_draw(env, n):
    img, rng = new_image(env)
    model_object(env, img, rng)
    img.u32(0x00560FD4, abs_(env, VIEW))
    img.garbage(abs_(env, VIEW), 0x100, rng)
    return [rng.randrange(0, 600), rng.randrange(0, 400)], img.pokes()


# ---- InitialsWindow -----------------------------------------------------------------------------------------


@kspec(0x00419290, "InitialsWindow_Construct")
def gen_initials_construct(env, n):
    img, rng = new_image(env)
    base = abs_(env, OBJ)
    img.garbage(base, 0x100, rng)
    font = abs_(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)) if rng.random() < 0.9 else 0
    return [font, rng.randrange(-50, 300) & 0xFFFFFFFF, rng.randrange(-50, 300) & 0xFFFFFFFF, rng.choice([-1, 0, 20]) & 0xFFFFFFFF,
            rng.choice([-1, 0, 20]) & 0xFFFFFFFF], img.pokes()


def initials_object(env, img, rng):
    base = abs_(env, OBJ)
    img.garbage(base, 0x100, rng)
    img.put(base + 0x28, bytes(0x80))            # the window's 32 child slots: empty, then the three letters
    img.u32(base, VT_INITIALS)
    img.u32(base + 0xA8, 3)
    for k in range(3):
        element = abs_(env, TEXTS + 0x30 * k)
        img.garbage(element, 0x30, rng)
        img.u32(element, abs_(env, VT_ITEM))      # (a fake vtable: the window's virtual deletes are reported)
        img.u32(element + 0x28, abs_(env, 0x0A00 + 0x40 * (k + 1)))   # a string the element owns (freed and replaced)
        img.u32(element + 0x2C, abs_(env, FIND_FONTS))
        img.u32(base + 0xAC + 4 * k, element)
        img.u32(base + 0x28 + 4 * k, element)
    return base


@kspec(0x00419390, "InitialsWindow_Delete")
def gen_initials_delete(env, n):
    img, rng = new_image(env)
    initials_object(env, img, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@kspec(0x004193B0, "InitialsWindow_Destroy")
def gen_initials_destroy(env, n):
    img, rng = new_image(env)
    initials_object(env, img, rng)
    return [], img.pokes()


@kspec(0x004193C0, "InitialsWindow_SetLetters")
def gen_initials_letters(env, n):
    img, rng = new_image(env)
    initials_object(env, img, rng)
    letter = lambda: rng.choice([ord(c) for c in "ABCXYZ019 "] + [0 if rng.random() < 0.05 else 65])
    return [letter(), letter(), letter()], img.pokes()
