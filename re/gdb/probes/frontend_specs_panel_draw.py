"""Phase 4.10b case generators: the HUD drawing functions of panel.cpp (see frontend_specs_panel.py for the setup).

Everything here is the group "panel": the same slab, stubs and captured regions as the score stack.  The drawing code works on
the live player-one skater: its sprites (a real engine object each, given the fake sprite vtable so a draw is a reported call),
its fonts and text effects (fake vtables), and the polygon arena (the cursor and the bucket heads are live).
"""

from __future__ import annotations

import os
import struct

from frontend_specs_panel import (
    LEVEL_RECORD, NAMES, NAME_STRIDE, PANEL, SCRATCH, SPRITE_POINTERS, SPRITE_VT, TEXTS, TRICK_NAMES, Image, effect_objects,
    entry_at, landing, name_ptr, pspec, put_entries, refresh_state,
)
from opentony.memory import mem

POLY = SCRATCH + 0x000       # a polygon of 0x30 bytes
PLACE_FRAME = SCRATCH + 0x040
ALLOC_FRAME = SCRATCH + 0x080
ALLOC_RECORD = SCRATCH + 0x0C0
OFFSETS = SCRATCH + 0x100
OTHER_SKATER = SCRATCH + 0x200   # a stand-in object for functions that take a skater but touch one field


def arena_cursor():
    return mem.u32(0x00560FD0)


def garbage(rng, size):
    return bytes(rng.randrange(256) for _ in range(size))


def with_noise(rng, value):
    """A 16-bit coordinate, sometimes with junk in the upper half (the original masks it)."""
    return value | rng.choice([0, 0, 0, 0x12340000, 0xFFFF0000])


def hud_world(env, rng, img, count=None, mode=None, players=None, viewport=None):
    """A random legal in-game state for the drawing functions: the panel, its text objects, the sprites' fake vtables, the
    replay and pause flags, the arena."""
    count = rng.choice([0, 0, 1, 2, 3, 5]) if count is None else count
    skater, panel = refresh_state(env, rng, img, count)
    for pointer in SPRITE_POINTERS:
        img.u32(mem.u32(pointer), env.slab + SPRITE_VT)
    img.u32(0x00533F38, rng.choice([1, 1, 2, 3, 4, 7, 8, 9, 0]) if mode is None else mode)
    img.u32(0x00533F3C, rng.choice([1, 1, 2]) if players is None else players)
    img.u32(0x0056A928, rng.choice([0, 0, 1]) if viewport is None else viewport)
    img.u32(0x0056E5D8, rng.choice([0, 0, 0, 0, 2]))
    img.u32(0x0056A8E0, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561C04, rng.choice([0, 0, 0, 1]))
    img.u32(0x006A3B21, rng.choice([0, 0, 0, 1]))
    img.u32(0x006A3D2C, rng.choice([0, 1]))
    img.u32(skater + 0x2CC4, rng.choice([0, 1]))
    img.u32(0x0056DB5C, rng.choice([skater, 0, skater]))
    img.u32(0x0056DB64, rng.choice([0, 1, 2, 2]))
    img.u32(0x00561BFC, rng.choice([0, 1, 0x14, 0x15, 0xF, 0x33]))
    img.u32(0x0056A470, rng.choice([0, 1, 1, 1]))
    img.u32(0x0056E320, rng.randrange(0, 4000))
    img.u32(0x005685F4, rng.randrange(0, 4000))
    img.u32(0x0056865C, rng.choice([0x100, 0x1D5, 0x80, 0x400]))
    img.u32(skater + 0x20, 0)
    return skater, panel


def arena_state(env, rng, img, fail=0.1):
    """Sometimes leave no room in the arena, so the polygon builders return null."""
    if rng.random() < fail:
        img.u32(0x00568628, arena_cursor() + rng.choice([0x08, 0x1B, 0x1C, 0x27, 0x28, 0x2F]))


def xy(rng):
    return with_noise(rng, rng.choice([rng.randrange(0, 0x200), 0x100, 0x19, rng.randrange(0, 0x100)])), with_noise(
        rng, rng.choice([rng.randrange(0, 0xF0), 0x30, 0x2D, 0xE0]))


# ---- polygons -------------------------------------------------------------------------------------------


@pspec(0x0048B2C0, "Panel_AllocFramePrimitive")
def gen_alloc_frame(env, n):
    rng = env.rng
    img = Image()
    arena_state(env, rng, img, 0.2)
    img.put(env.slab + ALLOC_FRAME, struct.pack("<II", rng.getrandbits(32), env.slab + ALLOC_RECORD))
    img.put(env.slab + ALLOC_RECORD, garbage(rng, 0x10))
    return [env.slab + ALLOC_FRAME if n % 12 != 11 else 0], img.pokes()


def place_state(env, rng, img):
    img.put(env.slab + POLY, garbage(rng, 0x30))
    img.put(env.slab + PLACE_FRAME, bytes([rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(256)]))


@pspec(0x0048B430, "Panel_PlaceFrameQuadWide")
def gen_place_wide(env, n):
    rng = env.rng
    img = Image()
    place_state(env, rng, img)
    return [rng.choice([0, 0x100, rng.randrange(-300, 700)]) & 0xFFFFFFFF, rng.choice([0, 0x30, rng.randrange(-100, 300)]) & 0xFFFFFFFF,
            env.slab + POLY if n % 15 != 14 else 0, env.slab + PLACE_FRAME if n % 13 != 12 else 0], img.pokes()


@pspec(0x0048B540, "Panel_PlaceFrameQuad")
def gen_place(env, n):
    rng = env.rng
    img = Image()
    place_state(env, rng, img)
    return [rng.choice([0, 0x100, rng.randrange(-300, 70000)]) & 0xFFFFFFFF, rng.choice([0, 0x30, rng.randrange(-100, 70000)]) & 0xFFFFFFFF,
            env.slab + POLY, env.slab + PLACE_FRAME], img.pokes()


@pspec(0x0048B620, "Panel_DrawFlatShadedPoly")
def gen_flat(env, n):
    rng = env.rng
    img = Image()
    arena_state(env, rng, img, 0.15)
    img.u32(0x0056E320, rng.randrange(0, 100))
    return [rng.choice([0, 0x100, rng.randrange(-300, 700)]) & 0xFFFFFFFF, rng.choice([0, 0x20, rng.randrange(-100, 300)]) & 0xFFFFFFFF,
            rng.choice([2, 0x101, 0x200, rng.randrange(0, 300)]), rng.choice([2, 0x78, rng.randrange(0, 200)]), rng.getrandbits(8),
            rng.getrandbits(8), rng.getrandbits(8), rng.choice([0, 1])], img.pokes()


@pspec(0x0048B760, "Panel_DrawGouraudPoly")
def gen_gouraud(env, n):
    rng = env.rng
    img = Image()
    arena_state(env, rng, img, 0.15)
    return [rng.choice([0, 0x19, rng.randrange(-300, 700)]) & 0xFFFFFFFF, rng.choice([0, 0x2D, rng.randrange(-100, 300)]) & 0xFFFFFFFF,
            rng.choice([3, 80, rng.randrange(0, 300)]), rng.choice([3, 10]), rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8),
            rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8), rng.choice([0, 1])], img.pokes()


# ---- the tapes, graffiti colours, score, icons ---------------------------------------------------------------


@pspec(0x0048B8E0, "DisplayTapesSkate")
def gen_tapes(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    img.u32(skater + 0x20, rng.choice([0, 0, 0, 0x1000]))
    img.u32(0x0056B75C, rng.choice([0, 1, 5, 9, 10, 11, 20, 0x28]))
    img.u32(0x0056B758, rng.choice([0, 1, 5, 9, 10, 11, 20, 0x28]))
    img.u32(0x005361D8, rng.choice([0x214, 0x1E2, 0x1E3, 0x1E0, 0x200, 0x100]))
    img.u32(0x005361D4, rng.choice([0x214, 0x1E1, 0x1E2, 0x1E0, 0x200, 0x100]))
    for i in range(5):
        img.u8(skater + 0x2994 + i, rng.choice([0, 1]))
    # the function reads the player-one pointer: it is the live skater
    return [], img.pokes()


@pspec(0x0048BA70, "GraffitiColor")
def gen_graffiti(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    return [skater], img.pokes()


def score_state(env, rng, img, skater):
    img.u32(skater + 0x2F6C, rng.choice([0xFFFFFFFF, 0, 0x2EE000, 0x1000, 0x500000, 0xBB8000, 0x12345, 0x3000]))
    img.u32(skater + 0x2F70, rng.choice([0, 0, 0x2EE000, 0x100000, 0x500, 0x1FFF, 0x300000]))
    img.u32(skater + 0x2A8, rng.choice([0, 5, 0x19, 0x1A, 100, 0x3E8, 0x3E9, 0x2710, 0x2711, 5000, 0x186A0, 0x186A1, 200000]))
    img.u32(skater + 0x29AC, rng.choice([0, 0, 0, 1]))
    img.u32(skater + 0x16C, rng.choice([0, 1234, 0x6FFFFFFF, 0x70000000 - 5, 99999]))
    img.u32(skater + 0x2CDC, rng.randrange(0, 99999))
    img.u8(LEVEL_RECORD, rng.choice([0, 1]))


@pspec(0x0048BAE0, "DisplayScore")
def gen_score(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    score_state(env, rng, img, skater)
    arena_state(env, rng, img, 0.1)
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048BEF0, "Panel_DrawSwitchIcon")
def gen_switch_icon(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    img.u32(skater + 0x30B8, rng.choice([0, 1, 2, 3, 4]))
    img.u32(skater + 0x2D98, rng.randrange(0, 4000))
    img.u32(skater + 0x314C, rng.choice([0, 1, 2]))
    img.u32(skater + 0x31F4, rng.choice([0, 0, 1]))
    img.u8(skater + 0x163, rng.getrandbits(1))
    img.u32(skater + 0xD8, rng.getrandbits(32))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048C0B0, "Panel_DrawBalanceIcon")
def gen_balance_icon(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    img.u32(skater + 0x30B8, rng.choice([1, 1, 1, 0, 2]))
    img.u32(skater + 0x30C4, rng.choice([0, 0, 1]))
    img.u32(0x0056B7EC, rng.choice([0, 0, 1]))
    img.u32(skater + 0x2F38, rng.randrange(0, 0x40000))
    img.u32(skater + 0x2F40, rng.randrange(0, 0x40000))
    img.u32(skater + 0x2F3C, rng.randrange(0, 0x80))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048C1A0, "Panel_DisplayTrickPoints")
def gen_trick_points(env, n):
    rng = env.rng
    img = Image()
    count = rng.choice([0, 1, 2, 3, 6, 7, 8, 9])
    skater, panel = hud_world(env, rng, img, count=count)
    img.u32(skater + 0x29AC, rng.choice([0, 0, 0, 0, 3]))
    img.u32(skater + 0x2A8, rng.choice([0, 0, 0, 900, 12000]))
    img.u32(skater + 0x2C4, rng.choice([0, 0, 1]))
    img.u32(skater + 0x7F0, rng.choice([skater + 0x6B0, 0, 0x1234]))
    img.u32(skater + 0x2BC, rng.randrange(0, 5000))
    img.u32(skater + 0x2988, rng.choice([0, 1, 2, 3]))
    landing(env, rng, img, skater, quality=rng.choice([0, 1, 2, 3, 4]))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048C520, "DisplayHolding")
def gen_holding(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img, mode=rng.choice([1, 2, 4, 7, 9, 3, 9, 7]))
    img.u32(skater + 0x2CC8, rng.choice([0, 100, 590, 599, 600, 601, 3500, 7300, 0x1A000, -50 & 0xFFFFFFFF, rng.randrange(0, 20000)]))
    img.u32(0x0056B740, rng.choice([0, 100, 300, 600, 599]))
    img.u8(0x029DA3A4, rng.choice([0, 0, 1]))
    img.u32(0x0056E320, rng.randrange(0, 4000))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048C760, "Panel_ViewportOffset")
def gen_viewport_offset(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + OFFSETS, garbage(rng, 8))
    img.u32(0x0056E320, rng.randrange(0, 100))
    return [rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]), env.slab + OFFSETS, env.slab + OFFSETS + 4], img.pokes()


@pspec(0x0048C8D0, "Panel_DisplayLevelMessage")
def gen_level_message(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    img.u32(skater + 0x2C8, rng.choice([0, 1, 2, 3, 4]))
    img.u32(LEVEL_RECORD + 0x160, env.slab + TEXTS)
    img.u32(0x0056A8E0, rng.choice([0, 0, 0, 1]))
    img.u32(skater + 0x170, rng.choice([0, 0, 1]))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048CA00, "Panel_ReplayInstructions")
def gen_replay_instructions(env, n):
    rng = env.rng
    img = Image()
    skater, _ = hud_world(env, rng, img)
    img.u32(0x0056E5D8, rng.choice([2, 2, 2, 0]))
    img.u8(0x00561990, rng.choice([0, 0, 1]))
    img.u32(0x005361D0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 20, 100]))
    img.u32(0x0056E628, rng.choice([0, 0, 1, 2, -1 & 0xFFFFFFFF]))
    img.u32(0x0056E5F8, rng.randrange(0, 500))
    img.u32(0x0056E5FC, rng.randrange(0, 500))
    return [], img.pokes()


@pspec(0x0048CD80, "Panel_DisplayTrickNames", this="panel")
def gen_trick_names(env, n):
    rng = env.rng
    img = Image()
    skater, panel = hud_world(env, rng, img, count=rng.choice([1, 2, 3]))
    for i in range(3):
        text = rng.choice([b"", rng.choice(TRICK_NAMES), b"Kickflip + Heelflip"])
        img.put(panel + 0x26F4 + 0x64 * i, text + b"\0")
    img.u32(panel + 0x688, rng.choice([panel + 0x66C, panel + 0x548, panel + 0x66C, panel + 0x548, 0, 0x1234]))
    img.u32(panel + 0x15C, rng.choice([0, 1]))
    img.u32(panel + 0x2E0, rng.choice([0x1000, 0x940]))
    img.u32(skater + 0x170, rng.choice([0, 0, 1]))
    x, y = xy(rng)
    return [skater, x, y], img.pokes()


@pspec(0x0048D090, "Panel_Display")
def gen_display(env, n):
    rng = env.rng
    img = Image()
    skater, panel = hud_world(env, rng, img)
    img.u32(0x00533F9C, rng.choice([1, 1, 1, 1, 0]))
    player = rng.choice([0, 0, 0, 1, 1, 2])
    viewport = rng.choice([0, 0, 1, 1, 2, 3, 2, 3, 4])
    img.u32(0x0056A928, viewport)
    img.u32(skater + 0x170, player)
    # a selector no layout has (an obsolete mode): the level message would read its offsets from stack garbage, so skip it
    img.u32(skater + 0x2C8, rng.choice([0, 1, 2]) if player + viewport * 2 < 8 else 0)
    img.u32(LEVEL_RECORD + 0x160, env.slab + TEXTS)
    img.u32(panel + 0x688, rng.choice([0, panel + 0x66C, panel + 0x548]))
    for i in range(3):
        img.put(panel + 0x26F4 + 0x64 * i, rng.choice([b"", rng.choice(TRICK_NAMES)]) + b"\0")
    img.u32(skater + 0x2CC8, rng.randrange(0, 20000))
    img.u32(skater + 0x20, 0)
    score_state(env, rng, img, skater)
    img.u32(skater + 0x2F6C, rng.choice([0xFFFFFFFF, 0, 0x2EE000]))
    arena_state(env, rng, img, 0.05)
    return [], img.pokes()


@pspec(0x0048D040, "Panel_ObsoleteViewportMode")
def gen_obsolete(env, n):
    return [], {}


@pspec(0x0048D030, "Panel_NoOp2")
def gen_noop2(env, n):
    return [], {}


@pspec(0x0048B2B0, "Panel_NoOp")
def gen_noop(env, n):
    return [], {}


@pspec(0x0048D660, "Panel_SetPlayerNumber")
def gen_set_player(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + OTHER_SKATER, garbage(rng, 0x200))
    other = env.slab + OTHER_SKATER
    return [rng.choice([0, 1, 2, 5, 0x10000, 0x10001]), rng.choice([env.skater, other])], img.pokes()


@pspec(0x0048D6D0, "Trick_ResetScoreState")
def gen_reset_state(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    img.u32(skater + 0x2DD4, rng.choice([0, 0, 1]))
    img.u32(skater + 0x30B8, rng.choice([0, 1, 4, 5]))
    img.u32(skater + 0x3024, rng.choice([0, 0x1234]))
    img.u32(skater + 0x3028, rng.choice([0, 0x5678]))
    img.u32(skater + 0x2988, rng.choice([0, 3]))
    return [skater], img.pokes()


@pspec(0x0048D7C0, "Panel_SkateLetterCollected")
def gen_letter(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    for i in range(5):
        img.u8(skater + 0x2994 + i, rng.choice([0, 1, 1, 1]))
    img.u32(0x0056B75C, rng.choice([0, 5]))
    return [rng.choice([-1, 0, 1, 2, 3, 4, 5, 9]) & 0xFFFFFFFF], img.pokes()


@pspec(0x0048D830, "Panel_TapeCollected")
def gen_tape(env, n):
    rng = env.rng
    img = Image()
    img.u32(0x0056B758, rng.choice([0, 7]))
    return [rng.choice([-1, 0, 1, 2, 3, 4, 5]) & 0xFFFFFFFF], img.pokes()


@pspec(0x0048D8A0, "Panel_CollectibleCounted")
def gen_collectible(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    img.u32(0x00533F38, rng.choice([1, 1, 1, 2]))
    img.u32(skater + 0x299C, rng.choice([0, 1, 2, 3, 98]))
    for g in range(4):
        base = LEVEL_RECORD + 0x74 + 0x18 * g
        img.u32(base, rng.choice([1, 2, 3, 4, 5]))
        img.u32(base + 4, name_ptr(env, rng))
    return [], img.pokes()


@pspec(0x0048E950, "Panel_ShowStealsMessage", this="panel")
def gen_steals(env, n):
    rng = env.rng
    img = Image()
    skater, panel = hud_world(env, rng, img)
    img.u32(skater + 0x170, rng.choice([0, 1]))
    img.u32(0x0056A928, rng.choice([0, 1, 2, 3]))
    return [], img.pokes()


@pspec(0x0048E9C0, "CPanelData__OutOfBounds", this="panel")
def gen_out_of_bounds(env, n):
    rng = env.rng
    img = Image()
    skater, panel = hud_world(env, rng, img)
    img.u32(LEVEL_RECORD + 0x160, env.slab + TEXTS)
    img.u32(0x005614C4, rng.choice([0, 1]))
    return [], img.pokes()


# ---- construction ---------------------------------------------------------------------------------------------

FAKE_FONTS = 0x1400           # three fake font objects of 0x150 bytes (time, score, tricks)
FAKE_MEMORY = 0x1800          # four blocks the stubbed operator new hands out
FAKE_SPRITES = 0x1A00         # four sprite objects the stubbed constructor hands back
FONT_NAMES = {0x00536254: 0, 0x00536248: 1, 0x005254E0: 2}


def seed_fonts(env, rng, img):
    for i in range(3):
        img.put(env.slab + FAKE_FONTS + 0x150 * i, garbage(rng, 0x14C))


def install_life_stubs(fc, env):
    rng = env.rng
    fc.stub(0x00449CA0, 0, "font_construct", convention="thiscall", this=True)
    fc.stub(0x004B9730, 0, "falling_construct", convention="thiscall", this=True)
    fc.stub(0x004BA020, 0, "zoom_construct", convention="thiscall", this=True)
    fc.stub(0x0044B000, 1, "font_find", text=True, result=lambda a: env.slab + FAKE_FONTS + 0x150 * FONT_NAMES.get(a[0], 0))
    fc.stub(0x0044ABB0, 1, "font_set_mapping", convention="thiscall", this=True)
    fc.stub(0x0044ADA0, 1, "font_load", text=True)
    fc.stub(0x0044B0A0, 1, "font_unload", text=True)
    fc.stub(0x00473A00, 4, "mess_load_font", text=True, result=lambda a: rng.randrange(1, 9))
    fc.stub(0x00473B80, 0, "mess_unload_font")
    fc.stub(0x00473BE0, 1, "mess_set_current_font")
    fc.stub(0x00466B70, 1, "operator_new", result=lambda a: rng.choice([env.slab + FAKE_MEMORY, env.slab + FAKE_MEMORY, 0]))
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.slab + FAKE_SPRITES)
    fc.stub(0x00457120, 0, "sprite_init", convention="thiscall", this=True)
    fc.stub(0x004B9EF0, 0, "falling_create_sprite")
    fc.stub(0x004B9FB0, 0, "falling_destroy_sprite")
    fc.stub(0x00466A00, 0, "panel_prepare", this=True)
    if "48f690" not in os.environ.get("P410_ONLY", "").lower().split(","):      # (the function under test itself: capture 10g s23 runs it live)
        fc.stub(0x0048F690, 4, "gap_apply_definition_state", convention="thiscall", this=True, text=True)
    fc.stub(0x004BD980, 3, "trick_object_activate")
    fc.stub(0x0042F540, 1, "career_score_landed")


GROUP_STUBS = {"panel": install_life_stubs}


@pspec(0x0048D990, "Panel_Construct", this="panel")
def gen_construct(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    seed_fonts(env, rng, img)
    img.u32(0x00533F3C, rng.choice([1, 2]))
    img.u32(0x00533F38, rng.choice([1, 7, 2]))
    return [skater], img.pokes()


@pspec(0x0048AD50, "Panel_LoadRuntimeSprites", this=lambda env: 0x12345678)
def gen_load(env, n):
    rng = env.rng
    img = Image()
    seed_fonts(env, rng, img)
    for pointer in (0x0056B744, 0x0056B748, 0x0056B74C, 0x0056B750):
        img.u32(pointer, 0 if rng.random() < 0.8 else 0x1234)
    img.u32(0x0056A89C, rng.choice([0, 1]))
    return [], img.pokes()


@pspec(0x0048B100, "Panel_Uninit")
def gen_uninit(env, n):
    rng = env.rng
    img = Image()
    for pointer in SPRITE_POINTERS:
        img.u32(mem.u32(pointer) + 0, env.slab + SPRITE_VT)
    img.u32(0x0056B738, rng.randrange(0, 9))
    return [], img.pokes()


# ---- the skater side of the score (physics.cpp) --------------------------------------------------------------


@pspec(0x0048F5F0, "Trick_TakeOff", this="skater")
def gen_take_off(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    img.u32(skater + 0x30B8, rng.choice([0, 1, 2, 3, 5]))
    img.u32(skater + 0x2CC8, rng.choice([0, 100, 9000]))
    for off in (0x3074, 0x3078, 0x307C, 0x3064, 0x2CBC, 0x2D90, 0x2C80, 0x2C84, 0x2988):
        img.u32(skater + off, rng.getrandbits(8))
    img.u32(skater + 0x2DD4, rng.choice([0, 1]))
    img.u32(skater + 0x3024, rng.choice([0, 0x1234]))
    img.u32(skater + 0x3028, rng.choice([0, 0x5678]))
    img.u32(0x00533F38, rng.choice([1, 2, 3, 4, 7]))
    return [], img.pokes()


def land_state(env, rng, img):
    from frontend_specs_panel import commit_state

    skater, panel = commit_state(env, rng, img, rng.choice([0, 1, 2, 3, 5]))
    img.u32(skater + 0x2C88, rng.getrandbits(8))
    img.u32(skater + 0x3048, rng.randrange(0, 50))
    img.u32(skater + 0x2DD4, rng.choice([0, 0, 1]))
    img.u32(skater + 0x2C84, rng.choice([0, 0, 1]))
    img.u32(skater + 0x3068, rng.choice([0, 3000, -3000 & 0xFFFFFFFF, 100]))
    img.u32(skater + 0x30C0, rng.choice([0, 0, 2]))
    img.u32(skater + 0x2C70, rng.choice([0, 1, 1]))
    img.u32(skater + 0x2D88, rng.choice([0, 0, 1, 3, 40, 41, 100]))
    for i in range(0x28):
        img.u32(skater + 0x2CE8 + 4 * i, rng.getrandbits(32))
    img.u32(skater + 0x2AC, rng.randrange(0, 90000))
    img.u32(skater + 0x2A8, rng.choice([0, 7, 500, 5000, 300000]))
    img.u32(skater + 0x2F6C, rng.choice([0, 0x1000, 0x500000, 0xB00000, 0x2EE000]))
    img.u32(skater + 0x2F70, rng.choice([0, 0, 0x100000, 0x2ED000, 0x2EE000, 0x400000]))
    img.u32(skater + 0x2CC8, rng.choice([0, 100]))
    # the open entry the OLLIE test looks at: the newest entry's kind
    return skater, panel


@pspec(0x0048F8F0, "Trick_Land", this="skater")
def gen_land(env, n):
    rng = env.rng
    img = Image()
    land_state(env, rng, img)
    return [], img.pokes()


@pspec(0x0048F800, "Trick_RegisterObject", this="skater")
def gen_register(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    count = rng.choice([0, 1, 5, 39, 40, 41, 77])
    img.u32(skater + 0x2D88, count)
    pool = [0x1000 + 4 * rng.randrange(64) for _ in range(6)]
    for i in range(0x28):
        img.u32(skater + 0x2CE8 + 4 * i, rng.choice(pool))
    return [rng.choice(pool + [0x2000])], img.pokes()


@pspec(0x0048F7C0, "Trick_LandPending", this="skater")
def gen_land_pending(env, n):
    rng = env.rng
    img = Image()
    skater, _ = land_state(env, rng, img)
    img.u32(skater + 0x2DD4, rng.choice([0, 1, 1]))
    img.u32(skater + 0x29E0, rng.getrandbits(8))
    for i in range(0x20):
        img.u8(skater + 0x2A14 + 8 * i, rng.getrandbits(8))
    return [], img.pokes()


# ---- the live state, untouched ---------------------------------------------------------------------------------
# The same functions on the state the running game has (no random pokes): only the vtable words are redirected, so a
# virtual call is a reported service instead of a real draw.  Run at several frames (P410_FRAME) for several states.


def redirect_vtables(env, img):
    skater = env.skater
    panel = skater + PANEL
    for pointer in SPRITE_POINTERS:
        img.u32(mem.u32(pointer), env.slab + SPRITE_VT)
    for off in (0x548, 0x66C, 0x68C):
        img.u32(panel + off, env.slab + 0x0000)
    for off in (0x164, 0x3FC, 0x2B0):
        img.u32(panel + off, env.slab + 0x0040)


def natural(address, name, args, this=None):
    @pspec(address, name, this=this)
    def gen(env, n):
        img = Image()
        redirect_vtables(env, img)
        values = [env.skater if a == "skater" else a for a in args]
        return values, img.pokes()
    return gen


natural(0x0048D090, "Panel_Display_natural", [])
natural(0x0048BAE0, "DisplayScore_natural", ["skater", 0x19, 0x2D])
natural(0x0048C1A0, "Panel_DisplayTrickPoints_natural", ["skater", 0x100, 0xE0])
natural(0x0048C520, "DisplayHolding_natural", ["skater", 0x100, 0x30])
natural(0x0048CD80, "Panel_DisplayTrickNames_natural", ["skater", 0x100, 0xCD], this="panel")
natural(0x0048B8E0, "DisplayTapesSkate_natural", [])
natural(0x0048CA00, "Panel_ReplayInstructions_natural", [])
natural(0x0048C8D0, "Panel_DisplayLevelMessage_natural", ["skater", 0x100, 0x50])
natural(0x0048BEF0, "Panel_DrawSwitchIcon_natural", ["skater", 0x1AE, 5])
natural(0x0048C0B0, "Panel_DrawBalanceIcon_natural", ["skater", 200, 0x50])
