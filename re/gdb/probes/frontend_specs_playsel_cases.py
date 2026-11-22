"""Phase 4.10f case generators, part 2: the generators of each group (see frontend_specs_playsel.py for the slab, the seeds and the stubs).

A function that works on a window's own state (destroy, draw, update) is given a window the generator builds by hand: its vtable is the fake page vtable
(the per-frame hook is a reported service), its children and engine objects are fake objects whose virtual methods are reported services.  A constructor is
called on garbage and builds its objects for real in the arena (the window base and the toolkit are native); everything outside them is a stub.
"""

from __future__ import annotations

from create_specs import spec
from frontend_specs_menu import STRING_POOL, coords, string_at
from frontend_specs_playsel import (PA, PS_OBJ, SELECTION, VT_SPRITE_REAL, VT_TEXT_REAL, real_sprite_element, real_text_element)
from frontend_specs_screens import VT_ANY
from frontend_specs_screens_cases import Pool, common, font_object, font_ptr, window
from frontend_specs_skstat import text_element
from frontend_specs_skwin import VT_WINDOW, new_image


def pspec(group):
    def make(address, name, this=None):
        resolve = (lambda env: env.slab + this) if this is not None else None
        return spec(address, name, group, this=resolve)
    return make


def obj(env, i=0):
    return PA(env, PS_OBJ[i])


def text_ptr_nonempty(env, rng):
    return string_at(env, rng.choice([0, 1, 3, 4, 5, 7, 9, 12, 13, 15]))


def small_int(rng):
    return rng.choice([0, 1, 2, 5, 99, 1000, 999999, 1000000, -1, -5, rng.randrange(0, 2000000), rng.randrange(-100000, 100000)]) & 0xFFFFFFFF


# ---- the window classes (player_ui.cpp) ----------------------------------------------------------------------------------------------------------

W = pspec("uiw")


def counter_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, 0xBC, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    value = rng.choice([0, 1, 2, 100, 5000, 999999, rng.randrange(0, 1000000), rng.randrange(-5, 0) & 0xFFFFFFFF])
    shown = rng.choice([value, value - 1, value + 1, rng.randrange(0, 1000000), 0, rng.randrange(-50, 50)])
    img.u32(base + 0xAC, value & 0xFFFFFFFF)
    img.u32(base + 0xB0, shown & 0xFFFFFFFF)
    img.u32(base + 0xB4, text_element(env, img, rng, 12))
    font = PA(env, 0x3D00 + 0x14C * rng.randrange(4))      # a seeded font (FIND_FONTS)
    img.u32(base + 0xB8, font)
    return font


@W(0x004A1440, "ScoreCounter_Construct", this=PS_OBJ[0])
def gen_counter_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0xBC, rng)
    return [rng.choice([0, 1, 100, 123456, rng.getrandbits(32)]), font_ptr(env, rng), coords(rng), coords(rng)], img.pokes()


@W(0x004A1510, "ScoreCounter_ScalarDelete", this=PS_OBJ[0])
def gen_counter_delete(env, n):
    img, rng = new_image(env)
    counter_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@W(0x004A1530, "ScoreCounter_Destroy", this=PS_OBJ[0])
def gen_counter_destroy(env, n):
    img, rng = new_image(env)
    counter_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


@W(0x004A1540, "ScoreCounter_SetValue", this=PS_OBJ[0])
def gen_counter_set_value(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), 0xBC, rng)
    return [small_int(rng)], img.pokes()


@W(0x004A1560, "ScoreCounter_Draw", this=PS_OBJ[0])
def gen_counter_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    counter_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@W(0x004A15A0, "ScoreCounter_Update", this=PS_OBJ[0])
def gen_counter_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    counter_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


def medal_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, 0xB8, pool, children=3 if not destroy else rng.randrange(0, 4))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for slot in (0xAC, 0xB0, 0xB4):
        img.u32(base + slot, keep(pool.take()))


@W(0x004A1670, "MedalWindow_Construct", this=PS_OBJ[0])
def gen_medal_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0xB8, rng)
    return [font_ptr(env, rng) if rng.random() < 0.9 else 0, coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@W(0x004A1880, "MedalWindow_ScalarDelete", this=PS_OBJ[0])
def gen_medal_delete(env, n):
    img, rng = new_image(env)
    medal_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@W(0x004A18A0, "MedalWindow_Destroy", this=PS_OBJ[0])
def gen_medal_destroy(env, n):
    img, rng = new_image(env)
    medal_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@W(0x004A19F0, "MedalWindow_Draw", this=PS_OBJ[0])
def gen_medal_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    medal_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@W(0x004A1B00, "MedalWindow_Update", this=PS_OBJ[0])
def gen_medal_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    medal_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


def prompt_state(env, img, rng, base, pool, *, destroy=False, real_children=False):
    window(env, img, rng, base, 0xB0, pool, children=0)
    img.u32(base, PA(env, VT_WINDOW))
    if real_children:
        sprite = real_sprite_element(env, img, rng, 0)
        text = real_text_element(env, img, rng, 1)
        img.u32(base + 0x28, sprite)
        img.u32(base + 0x2C, text)
        img.u32(base + 0xA8, 2)
        img.u32(base + 0xAC, text)
    else:
        text = text_element(env, img, rng, 12)
        img.u32(base + 0xAC, text)
        img.u32(base + 0x28, pool.take())
        img.u32(base + 0x2C, text)
        img.u32(base + 0xA8, 2)


@W(0x004A1B40, "PromptWindow_Construct", this=PS_OBJ[0])
def gen_prompt_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0xB0, rng)
    file = string_at(env, rng.choice([3, 4, 5, 7, 9, 12, 13, 15, 3, 4])) if rng.random() < 0.9 else string_at(env, 6)       # (index 6 is the empty string)
    text = string_at(env, rng.choice([3, 4, 5, 7, 9, 12, 13, 15])) if rng.random() < 0.9 else string_at(env, 6)
    font = font_ptr(env, rng) if rng.random() < 0.92 else 0
    return [file, text, font, coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@W(0x004A1DD0, "PromptWindow_ScalarDelete", this=PS_OBJ[0])
def gen_prompt_delete(env, n):
    img, rng = new_image(env)
    prompt_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@W(0x004A1DF0, "PromptWindow_Destroy", this=PS_OBJ[0])
def gen_prompt_destroy(env, n):
    img, rng = new_image(env)
    prompt_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


@W(0x004A1E00, "PromptWindow_GetWidth", this=PS_OBJ[0])
def gen_prompt_width(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    prompt_state(env, img, rng, obj(env), Pool(env, img, rng), real_children=True)
    if rng.random() < 0.08:
        img.u32(obj(env) + 0x28, 0)
    return [], img.pokes()


@W(0x004A1EB0, "PromptWindow_SetString", this=PS_OBJ[0])
def gen_prompt_set_string(env, n):
    img, rng = new_image(env)
    prompt_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [text_ptr_nonempty(env, rng)], img.pokes()


def flag_state(env, img, rng, base, *, slide=False):
    img.garbage(base, 0x40, rng)
    img.u32(base, VT_TEXT_REAL)
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x28, string_at(env, rng.randrange(len(STRING_POOL))))
    img.u32(base + 0x2C, PA(env, 0x3D00 + 0x14C * rng.randrange(4)))
    img.u32(base + 0x30, rng.choice([1, 2, 1, 2, 3, 4, 31, rng.getrandbits(32)]))
    if slide:
        tx, ty = coords(rng), coords(rng)
        near = rng.choice([0, 0, 1, -1, 2, -2, 5, 100])
        img.u32(base + 0x08, tx & 0xFFFFFFFF)
        img.u32(base + 0x0C, ty & 0xFFFFFFFF)
        img.u32(base + 0x20, rng.choice([tx, tx + near, rng.randrange(-300, 700)]) & 0xFFFFFFFF)
        img.u32(base + 0x24, rng.choice([ty, ty + near, rng.randrange(-300, 700)]) & 0xFFFFFFFF)
        img.u32(base + 0x18, rng.choice([0, 0, 0, 1, 2, 5, -1 & 0xFFFFFFFF]))
        img.u8(base + 0x1C, rng.choice([0, 1]))


@W(0x004A1ED0, "TextFlagElement_Construct", this=PS_OBJ[0])
def gen_flag_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0x40, rng)
    return [text_ptr_nonempty(env, rng), font_ptr(env, rng), rng.choice([0, 1, 2, 0x100, 0xFF00, rng.getrandbits(32)]), coords(rng), coords(rng),
            rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@W(0x004A1F20, "TextFlagElement_ScalarDelete", this=PS_OBJ[0])
def gen_flag_delete(env, n):
    img, rng = new_image(env)
    flag_state(env, img, rng, obj(env))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@W(0x004A1F40, "TextFlagElement_Destroy", this=PS_OBJ[0])
def gen_flag_destroy(env, n):
    img, rng = new_image(env)
    flag_state(env, img, rng, obj(env))
    return [], img.pokes()


@W(0x004A1F50, "TextFlagElement_Update", this=PS_OBJ[0])
def gen_flag_update(env, n):
    img, rng = new_image(env)
    flag_state(env, img, rng, obj(env), slide=True)
    return [], img.pokes()


@W(0x004A1FE0, "PlayerSelect_InitCallbacks")
def gen_init_callbacks(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env, 1), 0x20, rng)
    return [obj(env, 1)], img.pokes()


@W(0x004A2000, "PlayerSelect_ExitCallback")
def gen_exit_callback(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@W(0x004A2030, "PlayerSelect_SaveSelection", this=PS_OBJ[0])
def gen_save_selection(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), 0xB8, rng)
    img.u32(SELECTION, rng.getrandbits(32))
    return [], img.pokes()


@W(0x004A2040, "PlayerSelect_RestoreSelection", this=PS_OBJ[0])
def gen_restore_selection(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), 0xB8, rng)
    img.u32(SELECTION, rng.getrandbits(32))
    return [], img.pokes()
