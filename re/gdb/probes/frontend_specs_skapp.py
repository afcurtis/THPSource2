"""Phase 4.10d case generators, part 3: the appearance page (appwin.cpp, 0x00429e00..0x0042be3f).  See frontend_specs_skwin.py for the slab layout.

The page holds a scrolling list of twelve items (rows with fake vtables, four ColourText swatches and a heading text) and works on the editor
object that owns the skater's palettes: the 4.9 palette code runs for real on it (its engine palette reads and writes are stubbed services), and the
texture-set manager answers every set lookup.
"""

from __future__ import annotations

import struct

from create_specs import READ_ONLY, SEEDS, spec
from create_specs_color import saved_palette
from frontend_specs_menu import OBJ, STRING_POOL, VT_ITEM, Image, item_fields, poke_props, props_at, sprite_at
from frontend_specs_skwin import (VT_WINDOW, APPEAR, CUSTOM, EDITOR, ELEMS, NSETS, SET_IDS, OUTS, PALETTES, ROWS, ROW_STRIDE, SAVED, TEX, A, element, item_row, list_object,
                                  new_image, rspec, seed_skwin, skater_state, window_part, world)

Q = rspec("skapp")


def seed_skapp(env, rng):
    writes = seed_skwin(env, rng)
    writes[env.slab + SAVED] = saved_palette(rng, 256)
    writes[env.slab + SAVED + 0x1000] = saved_palette(rng, 256)
    for j in range(8):
        writes[env.slab + PALETTES + j * 0x20 + 3] = bytes([rng.choice([0, 1, 1, 0x0C, 0x0D])])
    return writes


SEEDS["skapp"] = seed_skapp

ITEM_IDS = [(0x0B, "row"), (0x08, "row"), (0x04, "swatch"), (0x09, "row"), (0x01, "row"), (0x05, "swatch"), (0x06, "swatch"), (0x0A, "row"), (0x07, "swatch"),
            (0x02, "row"), (0x03, "row"), (-1, "text")]
TABLE_NAMES = ["crpants0.bmp", "kneepad0.bmp", "sshirta0.bmp", "plbshrt0.bmp"]


def editor_object(env, img, rng):
    base = A(env, EDITOR)
    img.garbage(base, 0x330, rng)
    for k in range(9):
        img.u32(base + 0x8C + 4 * k, A(env, SAVED) + rng.choice([0, 0x1000]))
        img.u32(base + 0x140 + 4 * k, A(env, PALETTES) + 0x20 * rng.randrange(0, 8) if rng.random() < 0.85 else 0)
        shift = (rng.choice([0, 1, -1, 359, rng.randrange(-400, 400)]), rng.choice([0x80, 0, 0x100, rng.randrange(0, 0x200)]),
                 rng.choice([0x80, 0, 0x100, rng.randrange(0, 0x200)]))
        img.put(base + 0x164 + 0x10 * k, b"".join((x & 0xFFFFFFFF).to_bytes(4, "little") for x in shift))
        img.u8(base + 0x28F + 0x10 * k, rng.choice([0x30, 0x30, 0x31, 0x41]))
    img.u32(base + 0x1F4, A(env, PALETTES) + 0x20 * rng.randrange(0, 8))
    img.u32(base + 0x1F8, 0)
    img.u32(base + 0x284, rng.randrange(0, 4))
    img.put(base + 0x318, bytes(rng.choice([0, 1, rng.randrange(0, 36)]) for _ in range(4)))
    img.u32(base + 0x0C, rng.choice([0, 1]))
    env.pal_bytes = b"".join(rng.choice([rng.getrandbits(16), 0x7FFF, 0x8000]).to_bytes(2, "little") for _ in range(256))
    return base


def swatch_at(env, img, rng, base, ident, *, index=None):
    props = rng.randrange(4)
    item_fields(env, img, base, rng, vtable=0x00518528, id=ident & 0xFFFFFFFF, props=props, selectable=rng.choice([0, 1]), text=rng.choice(STRING_POOL[:15]))
    img.garbage(base + 0x54, 0x28, rng)
    img.u32(base + 0x58, rng.randrange(0, 36) if index is None else index)
    img.u32(base + 0x5C, 36)
    img.u32(base + 0x60, 0x00525C00)
    img.u32(base + 0x64, rng.randrange(0, 360))
    img.u32(base + 0x68, rng.choice([0x80, rng.randrange(0, 0x100)]))
    img.u32(base + 0x6C, rng.choice([0x80, rng.randrange(0, 0x100)]))
    img.u32(base + 0x70, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x74, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x78, rng.randrange(1, 31))


def appearance_page(env, img, rng, *, highlighted_id=None, hidden=None, children=None, real_dtors=False):
    base = A(env, APPEAR)
    window_part(env, img, rng, base, 0x270, hidden=hidden, children=children)
    img.u32(base, A(env, VT_WINDOW))
    img.u32(base + 0xAC, rng.choice(SET_IDS))
    img.u32(base + 0xB0, rng.randrange(0, 4))
    for k in range(7):
        img.u32(base + 0xB4 + 4 * k, rng.choice(SET_IDS))
    for i in range(4):
        poke_props(env, img, i, rng)
    items = []
    for slot, (ident, kind) in enumerate(ITEM_IDS):
        at = A(env, ROWS + ROW_STRIDE * slot)
        if kind == "row":
            item_row(env, img, rng, slot, ident, selectable=rng.choice([0, 1, 1]))
        elif kind == "swatch":
            swatch_at(env, img, rng, at, ident)
        else:
            item_fields(env, img, at, rng, vtable=0x00518E54, id=ident & 0xFFFFFFFF, props=rng.randrange(4), text=rng.choice(STRING_POOL[:15]))
        items.append(at)
        img.u32(base + 0xD4 + 4 * slot, at)
    for slot in range(len(ITEM_IDS), 0x60):
        img.u32(base + 0xD4 + 4 * slot, 0)
    where = None
    if highlighted_id is not None:
        where = next((i for i, (ident, _kind) in enumerate(ITEM_IDS) if ident == highlighted_id), -1)
    img.u32(base + 0xD0, list_object(env, img, rng, 1, items, highlighted=where))
    img.u32(base + 0x254, A(env, 0x0400))
    img.u32(base + 0x258, A(env, 0x0580))
    if real_dtors:
        img.u32(A(env, 0x0400), 0x00518E50)
        img.u32(A(env, 0x0580), 0x00518E50)
    img.u32(base + 0x25C, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x260, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x264, A(env, EDITOR))
    return base


def page_state(env, img, rng, **kw):
    world(env, img, rng)
    skater_state(env, img, rng)
    editor_object(env, img, rng)
    return appearance_page(env, img, rng, **kw)


@Q(0x00429E00, "AppearancePage_Construct", this=APPEAR)
def gen_app_construct(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    editor_object(env, img, rng)
    img.garbage(A(env, APPEAR), 0x270, rng)
    return [A(env, EDITOR), rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@Q(0x0042A0F0, "AppearancePage_Delete", this=APPEAR)
def gen_app_delete(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, real_dtors=True, highlighted_id=rng.choice([None, 1]))
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@Q(0x0042A110, "AppearancePage_Build", this=APPEAR)
def gen_app_build(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    skater_state(env, img, rng)
    editor_object(env, img, rng)
    img.garbage(A(env, APPEAR), 0x270, rng)
    img.u32(A(env, APPEAR) + 0x264, A(env, EDITOR))
    return [], img.pokes()


@Q(0x0042A8C0, "AppearancePage_Destroy", this=APPEAR)
def gen_app_destroy(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, real_dtors=True)
    return [], img.pokes()


@Q(0x0042A9B0, "AppearancePage_Poll", this=APPEAR)
def gen_app_poll(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, highlighted_id=rng.choice([-1, 1, 2, 3, 4, 5, 8, 11, None]))
    return [], img.pokes()


@Q(0x0042AAB0, "AppearancePage_Update", this=APPEAR)
def gen_app_update(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, highlighted_id=rng.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, 12]), hidden=0 if rng.random() < 0.9 else 1)
    return [], img.pokes()


@Q(0x0042AE30, "AppearancePage_Camera", this=APPEAR)
def gen_app_camera(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, highlighted_id=rng.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, 12]))
    img.garbage(A(env, OUTS), 0x80, rng)
    return [A(env, OUTS), A(env, OUTS + 0x40)], img.pokes()


@Q(0x0042B070, "AppearancePage_Draw", this=APPEAR)
def gen_app_draw(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng, highlighted_id=rng.choice([1, 2, 3, 8, 9, 10, 11, None]))
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@Q(0x0042B4D0, "AppearancePage_Refresh", this=APPEAR)
def gen_app_refresh(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [], img.pokes()


@Q(0x0042B880, "AppearancePage_ReadColours", this=APPEAR)
def gen_app_read_colours(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [], img.pokes()


@Q(0x0042B910, "AppearancePage_PartVisible", this=APPEAR)
def gen_app_part_visible(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [rng.choice([1, 4, 7, 1, 4, 7, 2, 3, 5, 6])], img.pokes()


@Q(0x0042B960, "AppearancePage_TextureFlag", this=APPEAR)
def gen_app_texture_flag(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [rng.choice([2, 3, 2, 3, 2, 3, 0, 1, 4])], img.pokes()


@Q(0x0042BA00, "AppearancePage_LoadPreset", this=APPEAR)
def gen_app_load_preset(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [], img.pokes()


@Q(0x0042BCF0, "AppearancePage_Dump", this=APPEAR)
def gen_app_dump(env, n):
    img, rng = new_image(env)
    page_state(env, img, rng)
    return [CUSTOM + 0x80 * rng.randrange(0, 4)], img.pokes()
