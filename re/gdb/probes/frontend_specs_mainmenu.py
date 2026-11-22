"""Phase 4.10c case generators: the title screen and its memory-card scan screen (mainmenu.cpp, 0x0046aee0..0x0046c460).

The windows of the screen are built in the scratch slab the way the engine builds them; every service the screen reaches (the engine
functions without a better name are stubbed under the name `engine_<address>`) and every virtual method of an object the port does not own is a
named service (fake vtables whose slots hold retail functions nothing here reaches).  The loops (MainMenu_Main, the card screen's loop) are not
forced: they run until the player answers, and no stub changes the memory they wait on.

Slab layout (offsets from the scratch slab; the whole slab is captured):
  0x0000 fake vtables: 0x00 sprite, 0x10 element (window children), 0x50 wheel (slot 7 reports), 0x80 text box, 0xc0 card
  0x0200 four valid fake fonts (what Font_Find / Font_Load return)   0x0a00 two sprite palettes of 0x200 bytes   0x0e00 eight sprite objects
  0x1100 a text box (0x418 bytes)       0x2000 the title window (0x368) or the card window (0xd60)      0x3000 the wheel (0xe0)
  0x3100 a card window as the title window's child (0xd60)    0x3e80 prompt windows (0x20 bytes each)    0x3f00 a scratch word
  0x4000 the allocation arena
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_menu import FONT_SIZE, GLYPHS, Image, build_font
from opentony.memory import mem

SLAB = 0x10000
VT_SPRITE = 0x0000
VT_ELEMENT = 0x0010
VT_WHEEL = 0x0050
VT_TEXTBOX = 0x0080
VT_CARD = 0x00C0
FONTS = 0x0200
PALETTES = 0x0A00
SPRITES = 0x0E00
SPRITE_STRIDE = 0x60
TEXTBOX = 0x1100
OBJ = 0x2000
WHEEL = 0x3000
CARD = 0x3100
PROMPTS = 0x3E80
SCRATCH = 0x3F00
ARENA = 0x4000
ARENA_SIZE = SLAB - ARENA

VT_REAL = {"title": 0x00518D78, "wheel": 0x00518D98, "card": 0x00518DB8}

# retail entry points nothing a screen call reaches; one per fake vtable slot
VIRTUAL_DELETE = 0x004AD9B0
ELEM_UPDATE = 0x004AD9D0
ELEM_DRAW = 0x004AD9E0
ELEM_SNAP_X = 0x004AD9F0
ELEM_SNAP_Y = 0x004ADA10
ELEM_HOOK = 0x004AD870
SPRITE_DRAW = 0x004AD890
TEXTBOX_V1 = 0x004AD5B0
CARD_UPDATE = 0x004AD9C0
CARD_DRAW = 0x004ADB90
WHEEL_V7 = 0x0046F470

PAD_REGION = (0x0056AFF8, 0x3C0)
WHEEL_TABLE = (0x00534500, 0x58)
TICK = 0x0056E31C


def regions(slab):
    out = [(slab, SLAB), PAD_REGION, WHEEL_TABLE, (0x00533E98, 4), (0x0052CB3C, 4), (0x006A1414, 4), (0x0056114C, 4), (0x00568624, 4), (TICK, 8),
           (0x0056C574, 8), (0x029DA3A0, 4)]
    for pointer in (0x0052E36C, 0x0052E368, 0x0052E9D0):
        out.append((mem.u32(pointer), 0x40))       # the strings these pointer variables point at
    state = mem.u32(0x00560FD4)
    out.append((state + 0x88, 4))                  # the render state's +0x88 word the screen hands the sprite
    return out


SLAB_BYTES["mainmenu"] = SLAB
EXTRA_REGIONS["mainmenu"] = regions
READ_ONLY["mainmenu"] = [
    (0x00527D54, 4),
    (0x00534558, 0x80),     # file and bitmap names of the title screen
    (0x00525440, 0x30),     # s2font1.fnt and s2font2.fnt
    (0x0052B710, 0x30),
    (0x005254E0, 0x10),
    (0x0052E368, 8),        # pointers to the card screen's texts
    (0x0052E9D0, 4),
    (0x00560FD4, 4),
    (0x0056B834, 4),        # the engine object the title screen hands to two calls
]


def seed_mainmenu(env, rng):
    from opentony.fcall import write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    write_chunks(env.slab + VT_SPRITE, struct.pack("<IIII", VIRTUAL_DELETE, 0, 0, SPRITE_DRAW))
    evt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ELEM_UPDATE), (2, ELEM_DRAW), (5, ELEM_SNAP_X), (6, ELEM_SNAP_Y), (7, ELEM_HOOK)):
        struct.pack_into("<I", evt, 4 * slot, entry)
    write_chunks(env.slab + VT_ELEMENT, bytes(evt))
    wvt = bytearray(0x20)
    struct.pack_into("<I", wvt, 0x00, VIRTUAL_DELETE)
    struct.pack_into("<I", wvt, 0x1C, ELEM_HOOK)
    write_chunks(env.slab + VT_WHEEL, bytes(wvt))
    write_chunks(env.slab + VT_TEXTBOX, struct.pack("<II", VIRTUAL_DELETE, TEXTBOX_V1))
    cvt = bytearray(0x20)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, CARD_UPDATE), (2, CARD_DRAW), (7, ELEM_HOOK)):
        struct.pack_into("<I", cvt, 4 * slot, entry)
    write_chunks(env.slab + VT_CARD, bytes(cvt))
    table = bytearray()
    for _ in range(0x32):
        top = rng.randrange(0, 9)
        table += struct.pack("<IBBBB", 0, rng.randrange(3, 21), top + rng.randrange(4, 17), top, 0)
    write_chunks(env.slab + GLYPHS, bytes(table))
    for i in range(4):
        build_font(env, rng, env.slab + FONTS + FONT_SIZE * i, VT_SPRITE)
    for i in range(8):
        write_chunks(env.slab + SPRITES + SPRITE_STRIDE * i, struct.pack("<I", env.slab + VT_SPRITE))
    return {}


SEEDS["mainmenu"] = seed_mainmenu

# engine functions without a better name: (address, argument count, thiscall, scripted result)
ENGINE = [
    (0x0042FC70, 0, False, None), (0x004B2390, 0, False, None), (0x0048A420, 0, False, None), (0x00489490, 0, False, None),
    (0x004F7CE0, 0, False, None), (0x004F5FF0, 0, False, None), (0x004DAE00, 0, False, None), (0x00418150, 0, False, None),
    (0x004304B0, 0, False, None), (0x00466BE0, 0, False, None), (0x00489A10, 0, False, None), (0x004E8740, 1, False, None),
    (0x004C9300, 1, False, None), (0x00430510, 0, False, None), (0x00466C00, 0, False, None), (0x004C8F30, 0, False, None),
    (0x004138F0, 0, False, None), (0x004165D0, 0, False, None),
    (0x004E41B0, 1, False, "bit"), (0x00452D30, 0, False, "bit"),
    (0x004A90F0, 2, True, None), (0x004A9330, 1, True, None), (0x004D78C0, 0, True, None), (0x004D7990, 3, True, None),
    (0x00473E50, 0, True, None), (0x00473F10, 0, True, None),
    (0x00475620, 0, True, "bit"), (0x00474D70, 1, True, None), (0x004741F0, 1, True, "bit"), (0x00473F30, 0, True, "small"),
    (0x004742E0, 1, True, "small"), (0x00475840, 2, True, None), (0x00476200, 2, True, "bit"),
]


def install_mainmenu_stubs(fc, env):
    rng = env.rng
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    fc.stub(0x00466B70, 1, "operator_new", result=env.alloc)
    fc.stub(0x00466BB0, 1, "operator_delete")
    fc.stub(0x005002FE, 1, "crt_new", result=env.alloc)
    fc.stub(0x005002F3, 1, "crt_delete")
    fc.stub(0x00449CA0, 0, "font_construct", convention="thiscall", this=True)
    fc.stub(0x0044A060, 0, "font_destroy", convention="thiscall", this=True)
    fc.stub(0x0044B000, 1, "font_find", text=True, result=lambda a: env.slab + FONTS + FONT_SIZE * rng.randrange(4))
    fc.stub(0x0044ADA0, 1, "font_load", text=True, result=lambda a: env.slab + FONTS + FONT_SIZE * rng.randrange(4))
    fc.stub(0x0044B0A0, 1, "font_unload", text=True)
    fc.stub(0x004AD620, 3, "sfx")
    fc.stub(0x00489670, 1, "pad_clear")
    fc.stub(0x004AF6B0, 1, "pad_present", result=lambda a: rng.choice([0, 1]))
    fc.stub(0x0046F2E0, 3, "mem_copy")
    fc.stub(0x00457870, 4, "sprite_construct_b", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00457120, 0, "sprite_init", convention="thiscall", this=True)
    fc.stub(0x00456C90, 0, "sprite_get_palette", convention="thiscall", this=True, result=lambda a: env.slab + PALETTES + 0x200 * rng.randrange(2))
    fc.stub(0x00456CB0, 1, "sprite_set_palette", convention="thiscall", this=True)
    fc.stub(0x004A1B40, 7, "prompt_window_construct", convention="thiscall", this=True, text=True,
            result=lambda a: env.slab + PROMPTS + 0x20 * rng.randrange(0, 8))
    fc.stub(0x00425A40, 4, "textbox_construct", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00425CA0, 1, "textbox_set_text", convention="thiscall", this=True, keep=slice(0, 0), text=True)
    fc.stub(0x004D74A0, 3, "engine_004d74a0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00448DD0, 2, "engine_00448dd0", text=True, result=lambda a: 1 + rng.randrange(0x1000))
    # the effect object: it reports its state through the pointer it is handed
    fc.stub(0x00475480, 1, "engine_00475480", convention="thiscall", this=True, pokes=[(0, 4, lambda a: rng.choice([0, 0, 1, 2]))])
    fc.stub(0x005002A1, 3, "sprintf", keep=slice(1, 3), text=1,
            fill=(0, lambda a: b"t" + str(rng.randrange(1000000)).zfill(6).encode() + b"\0"))
    for address, argc, thiscall, result in ENGINE:
        if address == 0x00475480:
            continue
        func = None
        if result == "bit":
            func = lambda a: rng.choice([0, 1])  # noqa: E731
        elif result == "small":
            func = lambda a: rng.randrange(0, 10)  # noqa: E731
        fc.stub(address, argc, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall, result=func)
    # virtual methods
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(ELEM_UPDATE, 0, "element_update", convention="thiscall", this=True)
    fc.stub(ELEM_DRAW, 2, "element_draw", convention="thiscall", this=True)
    fc.stub(ELEM_SNAP_X, 0, "element_snap_x", convention="thiscall", this=True)
    fc.stub(ELEM_SNAP_Y, 0, "element_snap_y", convention="thiscall", this=True)
    fc.stub(ELEM_HOOK, 0, "element_hook", convention="thiscall", this=True)
    fc.stub(SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    fc.stub(TEXTBOX_V1, 0, "textbox_v1", convention="thiscall", this=True)
    fc.stub(CARD_UPDATE, 0, "card_update", convention="thiscall", this=True)
    fc.stub(CARD_DRAW, 2, "card_draw", convention="thiscall", this=True)


GROUP_STUBS = {"mainmenu": install_mainmenu_stubs}


# ---- helpers -----------------------------------------------------------------------------------------------


def aspec(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "mainmenu", this=resolve)


def sprite_at(env, i):
    return env.slab + SPRITES + SPRITE_STRIDE * i


def world(env, img, rng):
    """Globals a screen call reads or writes."""
    img.u32(0x0052CB3C, rng.choice([0, 1]))
    img.u8(0x00533E98, rng.randrange(0, 7))
    img.u32(TICK, rng.randrange(0, 100000))
    img.u32(0x00568624, rng.randrange(0, 100000))
    img.u32(0x0056114C, rng.getrandbits(32))
    img.u32(0x029DA3A0, rng.choice([0, 1]))
    for pad in range(2):
        for off in (0x0, 0x30, 0xE0):
            img.u8(0x0056AFF9 + 0x16C * pad + off, rng.choice([0, 0, 1]))
    for edge in (0x0056B079, 0x0056B1E5, 0x0056B089, 0x0056B1F5, 0x0056B195):
        img.u8(edge, rng.choice([0, 0, 1]))
    img.u8(0x0056B038, rng.choice([0, 1]))
    img.u32(0x0056B144, rng.choice([0, 1]))
    img.u32(0x0056B2B0, rng.choice([0, 1]))
    img.u32(0x0056C574, rng.randrange(0, 5))
    img.u32(0x0056C578, rng.randrange(0, 5))
    img.u8(0x006A1415, rng.choice([0, 0, 1]))
    # the wheel's table: seven rows of (angle, what pad 0 picks, what pad 1 picks)
    angles = sorted(rng.randrange(0, 0x1000) for _ in range(7))
    for i in range(7):
        img.u32(0x00534504 + 12 * i, angles[i])
        img.u32(0x00534508 + 12 * i, rng.choice([-1, 0, 1, 2, 3, 0x27]) & 0xFFFFFFFF)
        img.u32(0x0053450C + 12 * i, rng.choice([-1, 0, 1, 2, 3, 0x27]) & 0xFFFFFFFF)


def window_part(env, img, rng, base, children=3):
    """The Window base of an object at `base`: a position and a few fake children."""
    img.u32(base + 0x04, 0)
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x20, rng.randrange(-50, 50))
    img.u32(base + 0x24, rng.randrange(-50, 50))
    img.u32(base + 0xA8, children)
    for i in range(32):
        img.u32(base + 0x28 + 4 * i, 0)
    for i in range(children):
        child = env.slab + SCRATCH + 0x40 + 0x40 * i
        img.u32(base + 0x28 + 4 * i, child)
        img.garbage(child, 0x30, rng)
        img.u32(child, env.slab + VT_ELEMENT)


def card_object(env, img, rng, base, real_vtable=False, state=None, finishing=False):
    img.garbage(base, 0xD60, rng)
    window_part(env, img, rng, base, rng.randrange(0, 3))
    img.u32(base, VT_REAL["card"] if real_vtable else env.slab + VT_CARD)
    img.u32(base + 0xD48, rng.choice([0, 1, 2]) if state is None else state)
    img.u32(base + 0xD4C, rng.choice([4, 9, 0xB, 9, 0xB, 4, 5, 0]))
    img.u32(base + 0xD50, env.slab + TEXTBOX)
    img.u32(base + 0xD54, 0 if not finishing else rng.choice([0, 1, 2]))
    img.u32(base + 0xD58, rng.choice([0, 0, 1, 5, 3]))
    img.u32(base + 0xD5C, rng.choice([-3, -1, 0, 1, 20, 45]) & 0xFFFFFFFF)
    img.garbage(env.slab + TEXTBOX, 0x40, rng)
    img.u32(env.slab + TEXTBOX, env.slab + VT_TEXTBOX)
    img.u32(base + 0x1FC, 0)


def title_object(env, img, rng, state=None, card=None):
    base = env.slab + OBJ
    img.garbage(base, 0x368, rng)
    window_part(env, img, rng, base, rng.randrange(0, 3))
    img.u32(base, env.slab + VT_ELEMENT)      # fake: the real class's hook is an engine function the probe cannot report
    img.u32(base + 0x1F8, rng.choice([0, 1]) if state is None else state)
    img.u32(base + 0x204, sprite_at(env, 6))
    img.u32(base + 0x208, env.slab + WHEEL)
    for slot, off in ((0x200, 0), (0x360, 1), (0x364, 2)):
        prompt = env.slab + PROMPTS + 0x20 * off
        img.garbage(prompt, 0x20, rng)
        img.u32(base + slot, prompt)
    img.u32(base + 0x35C, env.slab + CARD if card is None else card)
    img.garbage(env.slab + CARD, 0x40, rng)
    img.u32(env.slab + CARD, env.slab + VT_CARD)
    return base


def wheel_object(env, img, rng, real_vtable=False):
    base = env.slab + WHEEL
    img.garbage(base, 0xE0, rng)
    window_part(env, img, rng, base, rng.randrange(0, 3))
    img.u32(base, VT_REAL["wheel"] if real_vtable else env.slab + VT_WHEEL)
    img.u32(base + 0xAC, sprite_at(env, rng.randrange(6)))
    angle = rng.randrange(0, 0x1000)
    img.u32(base + 0xB0, rng.choice([angle, rng.randrange(0, 0x1000)]))
    img.u32(base + 0xB4, rng.choice([angle, rng.randrange(0, 0x1000), (angle + 0x400) & 0xFFF, (angle - 0x400) & 0xFFFFFFFF]))
    img.u32(base + 0xB8, rng.choice([7, 7, 7, rng.randrange(1, 8)]))
    img.u32(base + 0xBC, rng.randrange(0, 7))
    img.u8(base + 0xC0, rng.choice([0, 1]))
    img.u32(base + 0xC4, env.slab + PALETTES)
    img.u32(base + 0xC8, env.slab + PALETTES + 0x200)
    img.u32(base + 0xCC, rng.randrange(0, 7))
    img.put(env.slab + PALETTES, bytes(rng.randrange(256) for _ in range(0x400)))
    for k in range(6):
        img.garbage(sprite_at(env, k) + 4, 0x20, rng)
    return base


# ---- the title window ---------------------------------------------------------------------------------------


@aspec(0x0046B0A0, "MainMenuWindow_Construct", this=OBJ)
def gen_title_construct(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    img.garbage(env.slab + OBJ, 0x368, rng)
    return [], img.pokes()


@aspec(0x0046B4F0, "MainMenuWindow_Delete", this=OBJ)
def gen_title_delete(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    title_object(env, img, rng)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@aspec(0x0046B510, "MainMenuWindow_Destroy", this=OBJ)
def gen_title_destroy(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    title_object(env, img, rng, card=rng.choice([0, env.slab + CARD]))
    return [], img.pokes()


@aspec(0x0046B5D0, "MainMenuWindow_Draw", this=OBJ)
def gen_title_draw(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    title_object(env, img, rng, card=env.slab + CARD)
    return [], img.pokes()


@aspec(0x0046B690, "MainMenuWindow_Update", this=OBJ)
def gen_title_update(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    title_object(env, img, rng, card=env.slab + CARD)
    return [], img.pokes()


@aspec(0x0046B720, "MainMenuWindow_Poll", this=OBJ)
def gen_title_poll(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    title_object(env, img, rng, card=env.slab + CARD)
    wheel_object(env, img, rng)
    card_object(env, img, rng, env.slab + CARD, real_vtable=rng.random() < 0.5, finishing=True)
    return [env.slab + SCRATCH], img.pokes()


# ---- the wheel -----------------------------------------------------------------------------------------------


@aspec(0x0046B870, "MainMenuWheel_Construct", this=WHEEL)
def gen_wheel_construct(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    img.garbage(env.slab + WHEEL, 0xE0, rng)
    return [rng.randrange(-30, 300), rng.randrange(-30, 300), rng.choice([-1, 0, 5]) & 0xFFFFFFFF, rng.choice([-1, 0, 5]) & 0xFFFFFFFF], img.pokes()


@aspec(0x0046B9A0, "MainMenuWheel_Delete", this=WHEEL)
def gen_wheel_delete(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@aspec(0x0046B9C0, "MainMenuWheel_Destroy", this=WHEEL)
def gen_wheel_destroy(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [], img.pokes()


@aspec(0x0046BA50, "MainMenuWheel_Update", this=WHEEL)
def gen_wheel_update(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [], img.pokes()


@aspec(0x0046BCE0, "MainMenuWheel_Draw", this=WHEEL)
def gen_wheel_draw(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [rng.randrange(-50, 300), rng.randrange(-50, 300)], img.pokes()


@aspec(0x0046BD20, "MainMenuWheel_PaletteAlloc", this=WHEEL)
def gen_wheel_palette_alloc(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [], img.pokes()


@aspec(0x0046BD70, "MainMenuWheel_Tint", this=WHEEL)
def gen_wheel_tint(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    wheel_object(env, img, rng)
    return [], img.pokes()


# ---- the memory-card screen ------------------------------------------------------------------------------------


@aspec(0x0046BE60, "CardScreen_Register")
def gen_card_register(env, n):
    img, rng = Image(), env.rng
    img.garbage(env.slab + SCRATCH, 0x20, rng)
    return [env.slab + SCRATCH], img.pokes()


@aspec(0x0046BE80, "CardScreen_Nothing")
def gen_card_nothing(env, n):
    return [], {}


@aspec(0x0046BEA0, "CardWindow_Construct", this=OBJ)
def gen_card_construct(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    img.garbage(env.slab + OBJ, 0xD60, rng)
    return [], img.pokes()


@aspec(0x0046C080, "CardWindow_Delete", this=OBJ)
def gen_card_delete(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    card_object(env, img, rng, env.slab + OBJ)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@aspec(0x0046C0A0, "CardWindow_Destroy", this=OBJ)
def gen_card_destroy(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    card_object(env, img, rng, env.slab + OBJ)
    return [], img.pokes()


@aspec(0x0046C120, "CardWindow_Update", this=OBJ)
def gen_card_update(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    card_object(env, img, rng, env.slab + OBJ, finishing=rng.random() < 0.2)
    return [], img.pokes()


@aspec(0x0046C3D0, "CardWindow_Draw", this=OBJ)
def gen_card_draw(env, n):
    img, rng = Image(), env.rng
    world(env, img, rng)
    card_object(env, img, rng, env.slab + OBJ, finishing=rng.random() < 0.3)
    return [rng.randrange(-50, 300), rng.randrange(-50, 300)], img.pokes()


@aspec(0x0046C450, "CardWindow_Result", this=OBJ)
def gen_card_result(env, n):
    img, rng = Image(), env.rng
    card_object(env, img, rng, env.slab + OBJ, finishing=True)
    return [], img.pokes()
