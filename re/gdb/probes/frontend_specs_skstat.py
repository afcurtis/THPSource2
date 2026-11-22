"""Phase 4.10d case generators, part 4: the stat page and its points block (statwin.cpp, 0x004b6180..0x004b742f).  See frontend_specs_skwin.py.

The stat page is a Window holding a list of ten sliders (the real toolkit objects: the dots sit 0x18 bytes before the text part the page keeps) and a
points block (a Window of two counters and two texts).  Cases that only need the points block to answer a virtual call give it a fake vtable (the call
is reported); the block's own functions work on one with the real vtable.
"""

from __future__ import annotations

import struct

from create_specs import SEEDS, spec
from frontend_specs_menu import FIND_FONTS, FONT_SIZE, STRING_POOL, VIRTUAL_DELETE, build_font, item_fields, poke_props, props_at, sprite_at
from frontend_specs_skwin import (VT_WINDOW, CAREER, CUSTOM, DIALOG_V1, ELEMS, ELEM_STRIDE, MANAGER, POINTS, POINTS_UPDATE, ROWS, ROW_STRIDE, STAT, A, element, item_row,
                                  list_object, new_image, rspec, seed_skwin, skater_state, window_part, world)

S = rspec("skstat")
VT_POINTS = 0x4600
VT_DIALOG = 0x4620
VT_POINTS_REAL = 0x0051975C


def seed_skstat(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_skwin(env, rng)
    write_chunks(env.slab + VT_POINTS, struct.pack("<IIIII", VIRTUAL_DELETE, 0, 0, 0, POINTS_UPDATE))
    write_chunks(env.slab + VT_DIALOG, struct.pack("<II", VIRTUAL_DELETE, DIALOG_V1))
    build_font(env, rng, env.slab + STAT + 0xAC)      # the font the stat page copies its style from (built once: the cases leave it alone)
    return writes


SEEDS["skstat"] = seed_skstat


def slider_at(env, img, rng, index):
    """A MenuSlider (0x74 bytes): the dots at +0, the text part at +0x18 whose address the page keeps; returns that address."""
    base = A(env, ROWS + ROW_STRIDE * index)
    img.garbage(base, 0x80, rng)
    img.u32(base, 0x00518EA8)
    img.u32(base + 4, rng.randrange(0, 11))
    low = rng.randrange(0, 11)
    img.u32(base + 8, low)
    img.u32(base + 0x0C, rng.randrange(low, 11))
    img.u32(base + 0x10, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x14, rng.randrange(0, 31))
    item_fields(env, img, base + 0x18, rng, vtable=0x00518EB0, id=index, props=rng.randrange(4), selectable=rng.choice([0, 1, 1]), text=rng.choice(STRING_POOL[:15]))
    return base + 0x18


def text_element(env, img, rng, index):
    """A TextScreenElement (fake vtable) that owns a string."""
    base = A(env, ELEMS + ELEM_STRIDE * index)
    img.garbage(base, ELEM_STRIDE, rng)
    img.u32(base, A(env, 0x0000))
    img.u32(base + 0x14, rng.choice([0, 1]))
    img.u32(base + 0x28, A(env, 0x0A00 + 0x40 * rng.randrange(0, 15)))
    img.u32(base + 0x2C, A(env, FIND_FONTS))
    return base


def points_object(env, img, rng, *, real=False, valid=True):
    base = A(env, POINTS)
    window_part(env, img, rng, base, 0x180, children=0)
    img.u32(base, VT_POINTS_REAL if real else A(env, VT_POINTS))
    img.u32(base + 0xAC, element(env, img, rng, 8))
    img.u32(base + 0xB0, element(env, img, rng, 9))
    img.u32(base + 0xB4, text_element(env, img, rng, 10))
    img.u32(base + 0xB8, text_element(env, img, rng, 11))
    img.u32(base + 0xC8, rng.randrange(0, 30))
    for i in range(10):
        img.u8(base + 0xBC + i, rng.randrange(0, 10))
    return base


def own_below_record(env, img, rng, character, base, exact=False):
    """The block's stat bytes never exceed the record's (spent points are only added to the record)."""
    for i in range(10):
        img.u8(base + 0xBC + i, rng.randrange(0, 10))
        img.u8(CAREER + 0x104 * character + 0x38 + i, 0)
    # fix up after the fact: the record gets the block's bytes plus a few
    for i in range(10):
        own = img.b[base + 0xBC + i]
        img.u8(CAREER + 0x104 * character + 0x38 + i, min(own + (0 if exact else rng.choice([0, 0, 1, 2])), 120))


def stat_page(env, img, rng, *, highlighted_id=None, hidden=None, dialog=None, points_real=False):
    base = A(env, STAT)
    window_part(env, img, rng, base, 0x680, hidden=hidden, children=rng.randrange(0, 3))
    img.u32(base, A(env, VT_WINDOW))
    items = [slider_at(env, img, rng, k) for k in range(10)]
    heading = A(env, ROWS + ROW_STRIDE * 10)
    item_fields(env, img, heading, rng, vtable=0x00518E54, id=0xFFFFFFFF, props=rng.randrange(4), text=rng.choice(STRING_POOL[:15]))
    items.append(heading)
    for i in range(0x60):
        img.u32(base + 0x494 + 4 * i, items[i] if i < len(items) else 0)
    where = None
    if highlighted_id is not None:
        where = -1 if highlighted_id not in range(-1, 10) else (10 if highlighted_id == -1 else highlighted_id)
    img.u32(base + 0x490, list_object(env, img, rng, 2, items, highlighted=where))
    for i in range(4):
        poke_props(env, img, i, rng)
    img.u32(base + 0x614, A(env, 0x0400))
    img.u32(base + 0x618, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x628, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x62C, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x61C, points_object(env, img, rng, real=points_real))
    img.u32(base + 0x624, rng.choice([1, 2]))
    have_dialog = rng.random() < 0.3 if dialog is None else dialog
    if have_dialog:
        at = A(env, 0xDC00)
        img.garbage(at, 0x40, rng)
        img.u32(at, A(env, VT_DIALOG))
        img.u32(base + 0x620, at)
    else:
        img.u32(base + 0x620, 0)
    return base


def stat_state(env, img, rng, **kw):
    world(env, img, rng)
    skater_state(env, img, rng)
    env.dialog_result = rng.choice([0, 0, 1, 2])
    return stat_page(env, img, rng, **kw)


@S(0x004B6180, "StatPage_Poll", this=STAT)
def gen_stat_poll(env, n):
    img, rng = new_image(env)
    mode = rng.choice(["confirm_same", "confirm_changed", "back_same", "back_changed", "dialog", "random", "random"])
    world(env, img, rng)
    slot, character = skater_state(env, img, rng)
    env.dialog_result = rng.choice([0, 1, 2])
    page = stat_page(env, img, rng, highlighted_id=-1 if mode.startswith("confirm") else rng.choice([-1, 0, 3, 9, None]), dialog=(mode == "dialog"))
    points = A(env, POINTS)
    if mode.endswith("same") or mode == "dialog":
        own_below_record(env, img, rng, character, points, exact=True)
    elif mode.endswith("changed"):
        own_below_record(env, img, rng, character, points)
    img.u8(0x0056B029, 1 if mode.startswith("confirm") or (mode == "random" and rng.random() < 0.6) else 0)
    img.u8(0x0056AFF9, 1 if mode.startswith("back") or (mode == "random" and rng.random() < 0.3) else 0)
    return [], img.pokes()


@S(0x004B6490, "StatPage_Draw", this=STAT)
def gen_stat_draw(env, n):
    img, rng = new_image(env)
    stat_state(env, img, rng, highlighted_id=rng.choice([-1, 0, 5, None]))
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@S(0x004B64E0, "StatPage_Update", this=STAT)
def gen_stat_update(env, n):
    img, rng = new_image(env)
    stat_state(env, img, rng, highlighted_id=rng.choice([-1, 0, 5, None]))
    return [], img.pokes()


@S(0x004B6560, "StatPage_Build", this=STAT)
def gen_stat_build(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    base = A(env, STAT)
    img.garbage(base, 0xAC, rng)
    img.garbage(base + 0x1F0, 0x680 - 0x1F0, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.u32(base + 0x618, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x628, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x62C, sprite_at(env, rng.randrange(8)))
    return [], img.pokes()


@S(0x004B6BF0, "StatPage_HighlightFirst", this=STAT)
def gen_stat_highlight_first(env, n):
    img, rng = new_image(env)
    stat_state(env, img, rng, highlighted_id=rng.choice([-1, 3, None]))
    return [], img.pokes()


@S(0x004B6C00, "StatPage_ResetSliders", this=STAT)
def gen_stat_reset(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng, stance_ok=True)
    stat_page(env, img, rng)
    return [rng.choice([0, 1])], img.pokes()


@S(0x004B6D20, "StatPage_Apply", this=STAT)
def gen_stat_apply(env, n):
    img, rng = new_image(env)
    stat_state(env, img, rng)
    return [], img.pokes()


@S(0x004B6D90, "StatPoints_Construct", this=POINTS)
def gen_points_construct(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    img.garbage(A(env, POINTS), 0x180, rng)
    fonts = [A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)) for _ in range(2)]
    if rng.random() < 0.05:
        fonts[rng.randrange(2)] = 0
    return [fonts[0], fonts[1], rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF, rng.choice([-1, 0, 20]) & 0xFFFFFFFF,
            rng.choice([-1, 0, 20]) & 0xFFFFFFFF], img.pokes()


@S(0x004B7040, "StatPoints_Delete", this=POINTS)
def gen_points_delete(env, n):
    img, rng = new_image(env)
    points_object(env, img, rng, real=True)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@S(0x004B7060, "StatPoints_SetCharacter", this=POINTS)
def gen_points_set_character(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    points_object(env, img, rng, real=rng.random() < 0.5)
    return [rng.randrange(0, 20)], img.pokes()


@S(0x004B70A0, "StatPoints_Destroy", this=POINTS)
def gen_points_destroy(env, n):
    img, rng = new_image(env)
    points_object(env, img, rng, real=True)
    return [], img.pokes()


@S(0x004B70B0, "StatPoints_Update", this=POINTS)
def gen_points_update(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    points_object(env, img, rng, real=rng.random() < 0.5)
    return [], img.pokes()


@S(0x004B71C0, "StatPoints_Adjust", this=POINTS)
def gen_points_adjust(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng, stance_ok=True)
    points_object(env, img, rng, real=rng.random() < 0.5)
    return [rng.choice([0, 1])], img.pokes()


@S(0x004B7290, "StatPoints_Changed", this=POINTS)
def gen_points_changed(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    slot, character = skater_state(env, img, rng)
    base = points_object(env, img, rng, real=rng.random() < 0.5)
    if rng.random() < 0.85:
        own_below_record(env, img, rng, character, base)
    return [], img.pokes()


@S(0x004B7320, "StatPoints_Text", this=POINTS)
def gen_points_text(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    slot, character = skater_state(env, img, rng)
    base = points_object(env, img, rng, real=rng.random() < 0.5)
    own_below_record(env, img, rng, character, base)
    return [], img.pokes()
