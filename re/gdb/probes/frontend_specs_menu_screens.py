"""Phase 4.10c case generators, part 3: the name entry keyboard, the menu wrapper and the toolkit's static initialisers.  See frontend_specs_menu.py."""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_menu import (ARRAYS, CELLS, CHILDREN, CHILD_DRAW, CHILD_UPDATE, ELEM_HOOK, FIND_FONTS, FONT_SIZE, GRIDOBJ, ENTRY_PROPS, OBJ, Image, STRING_POOL, STRINGS, VT_ENTRY_CHILD, VT_ENTRY_DRAW, VT_GRID, VT_ITEM,
                                 VT_REAL, child_at, coords, item_fields, mspec, poke_props, props_at, sprite_at, string_at, world)

NAME_CHARS = [b"", b"A", b"AB", b"HELLO", b"abcdef", b"X Y", b"TONY HAWK", b"  ", b"Z"]
ENTRY_LABELS = {}


def new_image(env):
    return Image(), env.rng


def flag(rng):
    return rng.choice([0, 1, 2, 3])


def fake_font(env, rng):
    return env.slab + FIND_FONTS + FONT_SIZE * rng.randrange(4)


def real_child(env, img, rng, index, **kw):
    kw.setdefault("props", rng.randrange(4))
    item_fields(env, img, child_at(env, index), rng, vtable=VT_REAL["text"], text=rng.choice(STRING_POOL[:15]), **kw)


def entry_object(env, img, rng, *, real_cells=False, fake_cells=True, wide=None, with_digits=None, with_space=None):
    """A NameEntry in a legal state: a grid of 8x5 / 9x5 text cells whose ids follow the keyboard (letters, digits, -3 space, -2 delete, -1 end)."""
    base = env.slab + OBJ
    img.garbage(base, 0x380, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    wide = rng.choice([0, 1]) if wide is None else wide
    cols = 9 if wide else 8
    rows = 5
    ids = [ord("A") + i for i in range(26)] + ([ord("0") + d for d in range(10)] if (with_digits if with_digits is not None else rng.choice([0, 1])) else [])
    if wide:
        ids += [ord(c) for c in ".-'!?"]
    if with_space if with_space is not None else rng.choice([0, 1]):
        ids.append(-3)
    ids += [-2, -1]
    cells = cols * rows
    highlighted = rng.randrange(len(ids))
    for i, ident in enumerate(ids):
        extra = {"highlighted": 1 if i == highlighted else 0}
        if real_cells:
            real_child(env, img, rng, i, id=ident & 0xFFFFFFFF, selectable=1, **extra)
        else:
            item_fields(env, img, child_at(env, i), rng, id=ident & 0xFFFFFFFF, selectable=1, **extra)
    for i in range(cells):
        img.u32(env.slab + CELLS + 4 * i, child_at(env, i) if i < len(ids) else 0)
    grid = env.slab + GRIDOBJ
    img.garbage(grid, 0x40, rng)
    img.u32(grid, env.slab + VT_GRID)      # the fake grid vtable: its engine method (slot 1) is reported
    img.u32(grid + 0x04, cols)
    img.u32(grid + 0x08, rows)
    img.u32(grid + 0x0C, env.slab + CELLS)
    cursor = rng.randrange(len(ids))
    img.u32(grid + 0x10, cursor % cols)
    img.u32(grid + 0x14, cursor // cols)
    img.u32(grid + 0x18, len(ids))
    img.u32(grid + 0x1C, rng.randrange(0, 300))
    img.u32(grid + 0x20, rng.randrange(0, 300))
    img.u32(grid + 0x24, rng.randrange(0, 20))
    img.u32(grid + 0x28, rng.randrange(0, 20))
    img.u32(grid + 0x2C, rng.choice([0, 1]))
    img.u32(grid + 0x30, rng.choice([0, 1]))
    img.u32(grid + 0x34, rng.randrange(0, 5))
    limit = rng.randrange(1, 0x1F)
    img.u32(base, VT_REAL["entry"])
    img.u32(base + 0xAC, 0x14)
    name = rng.choice(NAME_CHARS)[:limit]
    img.put(base + 0xB0, name + b"\0")
    cursor_in_name = rng.choice([len(name), max(0, len(name) - 1), 0, rng.randrange(0, limit)])
    img.u32(base + 0xD0, cursor_in_name)
    img.u32(base + 0xD4, rng.randrange(0, 300))
    img.u32(base + 0xD8, rng.randrange(0, 300))
    img.u32(base + 0xDC, limit + 1)
    img.u32(base + 0xE0, grid)
    for i in range(0x40):
        img.u32(base + 0xE4 + 4 * i, child_at(env, i) if i < len(ids) else 0)
    img.u32(base + 0x1E4, env.slab + ENTRY_PROPS)
    img.u32(base + 0x1E8, fake_font(env, rng))
    img.u32(base + 0x1EC, rng.choice([0, 1, 3, limit]))
    for k in range(3):
        img.u32(base + 0x1F0 + 4 * k, child_at(env, 56 + k))
    img.put(base + 0x348, rng.choice(STRING_POOL[:15])[:0x1F] + b"\0")
    img.u32(base + 0x368, rng.randrange(0, 300))
    img.u32(base + 0x36C, rng.randrange(0, 300))
    img.u32(base + 0x370, rng.randrange(0, 30))
    # the style object the cells draw with
    poke_props(env, img, 0, rng)
    img.garbage(env.slab + ENTRY_PROPS, 0x180, rng)
    img.u32(env.slab + ENTRY_PROPS, VT_REAL["props"])
    # the window part: a few fake children (and the three prompt windows the keyboard holds)
    for k in range(52, 60):
        item_fields(env, img, child_at(env, k), rng, id=0)
    img.u32(base + 0xA8, rng.randrange(0, 4))
    for k in range(32):
        img.u32(base + 0x28 + 4 * k, child_at(env, 52 + k % 4) if k < 3 else 0)
    return base, len(ids), ids, cols, highlighted


# ---- the name entry ---------------------------------------------------------------------------------------


@mspec(0x00471E30, "NameEntry_Construct", this=OBJ)
def gen_entry_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x380, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    limit = rng.choice([1, 2, 5, 8, 12, 15, 20, 30, rng.randrange(1, 0x1F), 31])
    return [fake_font(env, rng), limit, rng.randrange(0, 300), rng.randrange(0, 300), coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF,
            rng.choice([0, 1, 3]), rng.choice([0, 1]), rng.choice([0, 1]), rng.choice([0, 1]), rng.choice([0, 1])], img.pokes()


@mspec(0x004727D0, "NameEntry_Delete", this=OBJ)
def gen_entry_delete(env, n):
    img, rng = new_image(env)
    entry_object(env, img, rng, real_cells=rng.random() < 0.4)
    return [flag(rng)], img.pokes()


@mspec(0x004727F0, "NameEntry_Destroy", this=OBJ)
def gen_entry_destroy(env, n):
    img, rng = new_image(env)
    base, count, ids, cols, highlighted = entry_object(env, img, rng, real_cells=rng.random() < 0.4)
    if rng.random() < 0.08:
        img.u32(base + 0x1E4, 0)
    return [], img.pokes()


@mspec(0x00472920, "NameEntry_GetName", this=OBJ)
def gen_entry_name(env, n):
    img, rng = new_image(env)
    entry_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00472930, "NameEntry_SetName", this=OBJ)
def gen_entry_set_name(env, n):
    img, rng = new_image(env)
    base, count, ids, cols, highlighted = entry_object(env, img, rng, real_cells=rng.random() < 0.4)
    text = rng.choice([b"", b"A", b"HELLO", b"TONY", b"abcdefghijklmnopqrstuvwxyz", b"MAX LENGTH NAME HERE", b"XYZ"])
    img.put(env.slab + STRINGS + 0x40 * 17, text + b"\0")
    return [env.slab + STRINGS + 0x40 * 17], img.pokes()


@mspec(0x00472A70, "NameEntry_SetPrompt", this=OBJ)
def gen_entry_set_prompt(env, n):
    img, rng = new_image(env)
    entry_object(env, img, rng)
    text = rng.choice([b"", b"Enter name", b"x", b"A prompt of exactly 32 characters!!"[:32], b"A prompt that is far too long for the buffer here"[:0x20]])
    img.put(env.slab + STRINGS + 0x40 * 17, text + b"\0")
    return [env.slab + STRINGS + 0x40 * 17, rng.randrange(0, 300), rng.randrange(0, 300)], img.pokes()


@mspec(0x00472CC0, "NameEntry_HandleInput", this=OBJ)
def gen_entry_input(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    base, count, ids, cols, highlighted = entry_object(env, img, rng, real_cells=rng.random() < 0.4)
    # the pad edges: pressing "confirm" is the interesting case
    if rng.random() < 0.75:
        img.u8(0x0056B029, 1)
    return [rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 9])], img.pokes()


# ---- the wrapper ------------------------------------------------------------------------------------------


WRAPPER_SIZE = 0x2A0


def wrapper_arrays(env, img, rng, count):
    """The text and id arrays a wrapper is built from: ids of every kind (>= 0, -1, -2), texts from the pool."""
    ids = []
    for i in range(count):
        ids.append(rng.choice([i, i, i, -1, -2, rng.randrange(0, 50)]))
    for i in range(count):
        img.u32(env.slab + ARRAYS + 4 * i, string_at(env, rng.randrange(15)))
        img.u32(env.slab + ARRAYS + 0x100 + 4 * i, ids[i] & 0xFFFFFFFF)
    return env.slab + ARRAYS, env.slab + ARRAYS + 0x100


@mspec(0x00472EF0, "MenuWrapper_Construct", this=OBJ)
def gen_wrapper_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, WRAPPER_SIZE, rng)
    count = rng.choice([1, 3, 5, 8, 12, 20, rng.randrange(1, 25)])
    texts, ids = wrapper_arrays(env, img, rng, count)
    font_name = rng.choice([0x005287A4, 0x0052544C])
    return [texts, ids, count, font_name, rng.choice([0, 1])], img.pokes()


def wrapper_object(env, img, rng, real=False, list_kind="list"):
    base = env.slab + OBJ
    img.garbage(base, WRAPPER_SIZE, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    count = rng.randrange(1, 10)
    for i in range(count):
        item_fields(env, img, child_at(env, i), rng, vtable=VT_REAL["text"] if real else None, id=rng.randrange(-2, 30) & 0xFFFFFFFF,
                    text=rng.choice(STRING_POOL[:15]), props=rng.randrange(4))
    img.u32(base, VT_REAL["wrapper"])
    # the list: a fake list object in the child slab (its vtable reports the calls)
    list_at = child_at(env, 30)
    img.garbage(list_at, 0x60, rng)
    img.u32(list_at, env.slab + 0x00C0)
    img.u32(base + 0x04, list_at)
    img.u8(base + 0x0C, rng.randrange(0, 2))
    img.u8(base + 0x0D, rng.randrange(0, 2))
    img.u32(base + 0x08, rng.randrange(-1, 20) & 0xFFFFFFFF)
    img.u32(base + 0x10, VT_REAL["props"])
    img.u32(base + 0x28C, count)
    for i in range(0x40):
        img.u32(base + 0x18C + 4 * i, child_at(env, i) if i < count else 0)
    return base, count


@mspec(0x00473240, "MenuWrapper_Delete", this=OBJ)
def gen_wrapper_delete(env, n):
    img, rng = new_image(env)
    wrapper_object(env, img, rng, real=rng.random() < 0.4)
    return [flag(rng)], img.pokes()


@mspec(0x00473260, "MenuWrapper_DestroyRoot", this=OBJ)
def gen_wrapper_destroy_root(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x40, rng)
    return [], img.pokes()


@mspec(0x00473270, "MenuWrapper_SubDelete", this=OBJ)
def gen_wrapper_sub_delete(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x40, rng)
    return [flag(rng)], img.pokes()


@mspec(0x00473290, "MenuWrapper_Destroy", this=OBJ)
def gen_wrapper_destroy(env, n):
    img, rng = new_image(env)
    wrapper_object(env, img, rng, real=rng.random() < 0.4)
    return [], img.pokes()


@mspec(0x00473330, "MenuWrapper_TestComplete", this=OBJ)
def gen_wrapper_test_complete(env, n):
    img, rng = new_image(env)
    wrapper_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00473380, "MenuWrapper_Poll", this=OBJ)
def gen_wrapper_poll(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    base, count = wrapper_object(env, img, rng)
    # at most one item is highlighted
    for i in range(count):
        img.u8(child_at(env, i) + 0x0E, 1 if rng.random() < 0.3 else 0)
    img.u8(base + 0x0C, rng.randrange(0, 2))
    return [], img.pokes()


@mspec(0x00473220, "MenuWrapper_Nothing")
def gen_wrapper_nothing(env, n):
    return [], {}


@mspec(0x00473230, "MenuWrapper_False")
def gen_wrapper_false(env, n):
    return [], {}


# ---- the statics ------------------------------------------------------------------------------------------


@mspec(0x00473470, "Mess_StaticInit")
def gen_statics_init(env, n):
    return [], {}


@mspec(0x00473480, "Mess_FontConstruct")
def gen_statics_construct(env, n):
    return [], {}


@mspec(0x00473490, "Mess_FontAtexit")
def gen_statics_atexit(env, n):
    return [], {}


@mspec(0x004734A0, "Mess_FontDestroy")
def gen_statics_destroy(env, n):
    return [], {}


# ---- the name entry's draw (its own group: the font is handed stack strings) -------------------------------------


@spec(0x00472B50, "NameEntry_Draw", "entry_draw", this=lambda env: env.slab + OBJ)
def gen_entry_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    base, count, ids, cols, highlighted = entry_object(env, img, rng, real_cells=False)
    img.u32(base, env.slab + VT_ENTRY_DRAW)
    img.u32(env.slab + VT_ENTRY_DRAW + 0x1C, ELEM_HOOK)
    img.u32(env.slab + VT_ENTRY_CHILD + 4, CHILD_UPDATE)
    img.u32(env.slab + VT_ENTRY_CHILD + 8, CHILD_DRAW)
    for k in range(52, 56):
        img.u32(child_at(env, k), env.slab + VT_ENTRY_CHILD)
    name = rng.choice(NAME_CHARS)[:0x1E]
    # the draw reads the buffer past the terminator too (up to the limit): printable bytes there, so each drawn character is recorded as text
    img.put(base + 0xB0, bytes(rng.randrange(0x41, 0x5B) for _ in range(0x20)))
    img.put(base + 0xB0, name + b"\0")
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()
