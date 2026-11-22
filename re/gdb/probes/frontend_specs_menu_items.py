"""Phase 4.10c case generators, part 1: the menu items (control, style, text, row, dots, slider, scaled text).  See frontend_specs_menu.py."""

from __future__ import annotations

import struct

from frontend_specs_menu import (CHILDREN, OBJ, Image, ROOT_VTABLE, STRINGS, VT_ITEM, VT_REAL, any_string, children, coords, item_fields, mspec,
                                 poke_props, props_at, small, sprite_at, string_at, world, STRING_POOL)


def flag(rng):
    return rng.choice([0, 1, 2, 3])


def new_image(env):
    return Image(), env.rng


# ---- MenuControl and the small base methods --------------------------------------------------------------


@mspec(0x0046F680, "MenuControl_Construct", this=OBJ)
def gen_control_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [rng.getrandbits(32)], img.pokes()


@mspec(0x0046F6A0, "MenuControl_Delete", this=OBJ)
def gen_control_delete(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    img.u32(env.slab + OBJ, VT_REAL["control"])
    return [flag(rng)], img.pokes()


@mspec(0x0046F6C0, "MenuControl_Destroy", this=OBJ)
def gen_control_destroy(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [], img.pokes()


@mspec(0x0046F6D0, "MenuControl_SetHighlighted", this=OBJ)
def gen_control_set_highlighted(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [rng.getrandbits(32)], img.pokes()


@mspec(0x0046F6E0, "MenuControl_SetValue", this=OBJ)
def gen_control_set_value(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [rng.getrandbits(32)], img.pokes()


@mspec(0x0046F6F0, "MenuControl_LogNavigate", this=OBJ)
def gen_control_log(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [rng.getrandbits(32)], img.pokes()


@mspec(0x0046F710, "MenuControl_Nothing", this=OBJ)
def gen_control_nothing(env, n):
    return [], {}


@mspec(0x00470530, "MenuControl_NoOp2", this=OBJ)
def gen_control_noop2(env, n):
    return [env.rng.getrandbits(32), env.rng.getrandbits(32)], {}


@mspec(0x00470540, "MenuControl_NoOp", this=OBJ)
def gen_control_noop(env, n):
    return [], {}


@mspec(0x0046F720, "MenuControlB_Construct", this=OBJ)
def gen_control_b_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [rng.getrandbits(32)], img.pokes()


@mspec(0x0046F740, "MenuControlB_Delete", this=OBJ)
def gen_control_b_delete(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    img.u32(env.slab + OBJ, VT_REAL["control_b"])
    return [flag(rng)], img.pokes()


@mspec(0x0046F760, "MenuControl_GetId", this=OBJ)
def gen_control_id(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [], img.pokes()


# ---- MenuProperties ---------------------------------------------------------------------------------------


@mspec(0x0046F770, "MenuProperties_Construct", this=OBJ)
def gen_props_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x180, rng)
    return [], img.pokes()


@mspec(0x0046F820, "MenuProperties_Delete", this=OBJ)
def gen_props_delete(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x180, rng)
    img.u32(env.slab + OBJ, VT_REAL["props"])
    return [flag(rng)], img.pokes()


@mspec(0x0046F840, "MenuProperties_Destroy", this=OBJ)
def gen_props_destroy(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x180, rng)
    return [], img.pokes()


# ---- MenuText ---------------------------------------------------------------------------------------------


def text_object(env, img, rng, **kw):
    kw.setdefault("props", rng.randrange(4))
    poke_props(env, img, kw["props"], rng)
    item_fields(env, img, env.slab + OBJ, rng, vtable=VT_REAL["text"], **kw)


@mspec(0x0046F890, "MenuText_Construct", this=OBJ)
def gen_text_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    return [rng.randrange(-5, 40) & 0xFFFFFFFF, any_string(env, rng), props_at(env, props)], img.pokes()


@mspec(0x0046F910, "MenuText_Delete", this=OBJ)
def gen_text_delete(env, n):
    img, rng = new_image(env)
    text_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x0046F930, "MenuText_Destroy", this=OBJ)
def gen_text_destroy(env, n):
    img, rng = new_image(env)
    text_object(env, img, rng)
    return [], img.pokes()


@mspec(0x0046F940, "MenuItem_SetPosition", this=OBJ)
def gen_item_set_position(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF], img.pokes()


@mspec(0x0046F960, "MenuText_SetText", this=OBJ)
def gen_text_set_text(env, n):
    img, rng = new_image(env)
    text_object(env, img, rng)
    return [any_string(env, rng)], img.pokes()


@mspec(0x0046FA60, "MenuText_Draw", this=OBJ)
def gen_text_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    kw = {}
    if rng.random() < 0.08:
        kw["just"] = 2
    if rng.random() < 0.08:
        kw["timer"] = rng.choice([5, 9, -1])
    text_object(env, img, rng, text=rng.choice(STRING_POOL[:15]), **kw)
    return [], img.pokes()


# ---- MenuRow ----------------------------------------------------------------------------------------------


def row_object(env, img, rng, columns=None, **kw):
    kw.setdefault("props", rng.randrange(4))
    columns = rng.randrange(1, 4) if columns is None else columns
    for i in range(4):
        poke_props(env, img, i, rng)
    base = env.slab + OBJ
    item_fields(env, img, base, rng, vtable=VT_REAL["row"], **kw)
    img.garbage(base + 0x60, 0xA0, rng)
    img.u32(base + 0xE4, columns)
    for c in range(3):
        img.put(base + 0x54 + 0x20 * c, rng.choice(STRING_POOL[:15]) + b"\0")
        img.u32(base + 0xD4 + 4 * c, rng.randrange(0, 200))
        img.u32(base + 0xE8 + 4 * c, rng.choice([0, props_at(env, rng.randrange(4))]))
    img.u32(base + 0xF8, rng.choice([0, sprite_at(env, 0)]))
    img.u32(base + 0xFC, rng.choice([0, sprite_at(env, 1)]))
    return columns


@mspec(0x0046FC50, "MenuRow_Construct", this=OBJ)
def gen_row_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x110, rng)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    columns = rng.choice([1, 2, 3, 3, 2, 0, 4, 5, -1])
    return [rng.randrange(0, 40), any_string(env, rng), props_at(env, props), columns & 0xFFFFFFFF, rng.choice([0, sprite_at(env, 2)]),
            rng.choice([0, sprite_at(env, 3)])], img.pokes()


@mspec(0x0046FD70, "MenuRow_Delete", this=OBJ)
def gen_row_delete(env, n):
    img, rng = new_image(env)
    row_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x0046FD90, "MenuRow_SetColumn", this=OBJ)
def gen_row_set_column(env, n):
    img, rng = new_image(env)
    columns = row_object(env, img, rng)
    column = rng.choice([0, 1, 2, columns - 1, columns, -1, 3])
    return [any_string(env, rng, long_ok=True), column & 0xFFFFFFFF, rng.randrange(0, 300), rng.choice([0, props_at(env, rng.randrange(4))])], img.pokes()


@mspec(0x0046FE80, "MenuRow_Destroy", this=OBJ)
def gen_row_destroy(env, n):
    img, rng = new_image(env)
    row_object(env, img, rng)
    return [], img.pokes()


@mspec(0x0046FE90, "MenuRow_Draw", this=OBJ)
def gen_row_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    kw = {}
    if rng.random() < 0.08:
        kw["just"] = 2
    row_object(env, img, rng, **kw)
    return [], img.pokes()


# ---- MenuDots ---------------------------------------------------------------------------------------------


def dots_object(env, img, rng, base=None):
    base = env.slab + OBJ if base is None else base
    img.garbage(base, 0x20, rng)
    img.u32(base, VT_REAL["dots"])
    maximum = rng.randrange(1, 14)
    img.u32(base + 0x0C, maximum)
    img.u32(base + 0x08, rng.randrange(0, maximum + 1))
    img.u32(base + 0x04, rng.randrange(0, maximum + 1))
    img.u32(base + 0x10, sprite_at(env, rng.randrange(8)))
    img.u32(base + 0x14, rng.choice([1, 2, 15, 16, 17, 29, 30, rng.randrange(1, 31), 31, 0, -1]))
    for k in range(8):
        img.put(sprite_at(env, k) + 4, bytes(rng.randrange(256) for _ in range(0x20)))


@mspec(0x004701D0, "MenuDots_Construct", this=OBJ)
def gen_dots_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x30, rng)
    for k in range(8):
        img.put(sprite_at(env, k) + 4, bytes(rng.randrange(256) for _ in range(0x20)))
    return [rng.getrandbits(32), rng.choice([0, sprite_at(env, rng.randrange(8))]), rng.randrange(0, 14), rng.randrange(0, 14)], img.pokes()


@mspec(0x00470280, "MenuDots_Delete", this=OBJ)
def gen_dots_delete(env, n):
    img, rng = new_image(env)
    dots_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x004702A0, "MenuDots_Destroy", this=OBJ)
def gen_dots_destroy(env, n):
    img, rng = new_image(env)
    dots_object(env, img, rng)
    return [], img.pokes()


@mspec(0x004702B0, "MenuDots_Draw", this=OBJ)
def gen_dots_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    dots_object(env, img, rng)
    if rng.random() < 0.05:
        img.u32(env.slab + OBJ + 0x10, 0)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF], img.pokes()


@mspec(0x00470550, "MenuDots_SetRange", this=OBJ)
def gen_dots_set_range(env, n):
    img, rng = new_image(env)
    dots_object(env, img, rng)
    maximum = rng.randrange(0, 14)
    img.u32(env.slab + OBJ + 0x0C, maximum)
    return [rng.choice([0, maximum, maximum + 1, -1, rng.randrange(0, maximum + 1)]) & 0xFFFFFFFF,
            rng.choice([0, maximum, maximum + 1, -1, rng.randrange(-2, maximum + 3)]) & 0xFFFFFFFF], img.pokes()


# ---- MenuSlider -------------------------------------------------------------------------------------------


def slider_object(env, img, rng, **kw):
    base = env.slab + OBJ
    for i in range(4):
        poke_props(env, img, i, rng)
    img.garbage(base, 0x80, rng)
    dots_object(env, img, rng, base)
    img.u32(base, VT_REAL["slider"])
    kw.setdefault("props", rng.randrange(4))
    kw.setdefault("text", rng.choice(STRING_POOL[:15]))
    item_fields(env, img, base + 0x18, rng, vtable=VT_REAL["slider_dots"], **kw)
    img.u32(base + 0x6C, rng.choice([0, sprite_at(env, 4)]))
    img.u32(base + 0x70, rng.choice([0, sprite_at(env, 5)]))
    return base


@mspec(0x00470630, "MenuSlider_Construct", this=OBJ)
def gen_slider_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x80, rng)
    for k in range(8):
        img.put(sprite_at(env, k) + 4, bytes(rng.randrange(256) for _ in range(0x20)))
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    return [rng.randrange(0, 40), any_string(env, rng), props_at(env, props), sprite_at(env, rng.randrange(8)), rng.randrange(0, 14),
            rng.choice([0, sprite_at(env, 4)]), rng.choice([0, sprite_at(env, 5)])], img.pokes()


@mspec(0x004706C0, "MenuSlider_Delete", this=OBJ)
def gen_slider_delete(env, n):
    img, rng = new_image(env)
    slider_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x004706E0, "MenuSlider_Destroy", this=OBJ)
def gen_slider_destroy(env, n):
    img, rng = new_image(env)
    slider_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00470710, "MenuAlloc_Thunk")
def gen_alloc_thunk(env, n):
    return [env.rng.choice([0x10, 0x54, 0x58, 0x1c, 0x350])], {}


@mspec(0x00470720, "MenuFree_Thunk")
def gen_free_thunk(env, n):
    return [env.slab + OBJ], {}


@mspec(0x00470730, "MenuSlider_Draw", this=OBJ + 0x18)
def gen_slider_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    slider_object(env, img, rng, highlighted=rng.choice([0, 1, 1]))
    return [], img.pokes()


@mspec(0x004707E0, "MenuSlider_Adjust", this=OBJ + 0x18)
def gen_slider_adjust(env, n):
    img, rng = new_image(env)
    slider_object(env, img, rng)
    return [rng.choice([0, 1, 0, 1, 2, 3, 7])], img.pokes()


@mspec(0x00473450, "MenuSlider_SubDelete", this=OBJ + 0x18)
def gen_slider_sub_delete(env, n):
    img, rng = new_image(env)
    slider_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x00473460, "MenuSlider_SubRefresh", this=OBJ + 0x18)
def gen_slider_sub_refresh(env, n):
    img, rng = new_image(env)
    slider_object(env, img, rng)
    return [], img.pokes()


# ---- MenuTextScaled ---------------------------------------------------------------------------------------


@mspec(0x004708D0, "MenuTextScaled_Construct", this=OBJ)
def gen_scaled_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, 0x60, rng)
    props = rng.randrange(4)
    poke_props(env, img, props, rng)
    return [rng.randrange(-3, 40) & 0xFFFFFFFF, any_string(env, rng), props_at(env, props), rng.choice([0, 1, 7])], img.pokes()


def scaled_object(env, img, rng, **kw):
    kw.setdefault("props", rng.randrange(4))
    poke_props(env, img, kw["props"], rng)
    item_fields(env, img, env.slab + OBJ, rng, vtable=VT_REAL["scaled"], **kw)
    img.u32(env.slab + OBJ + 0x54, rng.randrange(1, 40))


@mspec(0x00470910, "MenuTextScaled_Delete", this=OBJ)
def gen_scaled_delete(env, n):
    img, rng = new_image(env)
    scaled_object(env, img, rng)
    return [flag(rng)], img.pokes()


@mspec(0x00470930, "MenuTextScaled_Destroy", this=OBJ)
def gen_scaled_destroy(env, n):
    img, rng = new_image(env)
    scaled_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00470940, "MenuTextScaled_Draw", this=OBJ)
def gen_scaled_draw(env, n):
    img, rng = new_image(env)
    kw = {}
    if rng.random() < 0.08:
        kw["just"] = 2
    scaled_object(env, img, rng, text=rng.choice(STRING_POOL[:15]), **kw)
    return [], img.pokes()
