"""Phase 4.10f case generators, part 4: the bio window, the page dots, the header and the main menu window (playsel.cpp 0x004a3830..0x004a3e80 and
0x004a5450..0x004a5d80).

Constructors run for real in the arena (the window base and the toolkit are native); a window under test is built by hand: fake page vtable, fake pool objects for
the lines and children, real Font objects, and for the menu window a fake list and a real text row (the draw sets its text through the toolkit's own method).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props, props_at
from frontend_specs_playsel import (PSLAB, PA, PS_OBJ, REPLAY_STATE, STATIC_EXTRA, pregions, seed_play, install_play_stubs, real_text_element)
from frontend_specs_playsel_cases import obj, pspec
from frontend_specs_screens import READ_ONLY_COMMON
from frontend_specs_screens_cases import Pool, common, font_object, font_ptr, read_u32, window
from frontend_specs_skstat import text_element
from frontend_specs_skwin import MANAGER, VT_WINDOW, item_row, list_object, new_image

SOFTWARE = 0x029DA3A0
BIO_STATIC = [(0x0052EB00, 0x440), (0x0052FE80, 0x1210), (0x005252F0, 0x10), (0x0052E5F0, 0x40), (0x00525B00, 0x10), (0x0052D840, 0x20)]

for _group in ("bio",):
    SLAB_BYTES[_group] = PSLAB
    EXTRA_REGIONS[_group] = lambda slab: pregions(slab) + [(SOFTWARE, 4), (REPLAY_STATE, 8)]
    READ_ONLY[_group] = list(READ_ONLY_COMMON) + STATIC_EXTRA + BIO_STATIC
    SEEDS[_group] = seed_play

GROUP_STUBS = {"bio": lambda fc, env: install_play_stubs(fc, env)}

B = pspec("bio")


def current_character(img):
    active = read_u32(img, MANAGER + 0x64)
    return read_u32(img, MANAGER + 4 + active * 0xC)


def pads(img, rng, *, scroll=None):
    active = read_u32(img, 0x0056A888)
    for address in (0x0056B0A8, 0x0056B098, 0x0056B099, 0x0056B0A9, 0x0056B029):
        img.u8(address + 0x16C * active, rng.choice([0, 0, 1]))
    if scroll == "up":
        img.u8(0x0056B0A8 + 0x16C * active, 1)
    elif scroll == "down":
        img.u8(0x0056B0A8 + 0x16C * active, 0)
        img.u8(0x0056B098 + 0x16C * active, 1)


def bio_state(env, img, rng, base, pool, *, lines=None, destroy=False, built_for=None):
    window(env, img, rng, base, 0x244, pool, children=0)
    img.u32(base, PA(env, VT_WINDOW))
    img.u32(base + 0x28, pool.take())
    img.u32(base + 0xA8, 1)
    font_object(env, img, rng, base + 0xF4)
    count = rng.randrange(1, 15) if lines is None else lines
    for i in range(16):
        if i < count:
            line = pool.take()
            img.u32(line + 0xC, rng.choice([rng.randrange(-30, 140), 0x14, 0x15, 0x13, 0x48, 0x49, 0x47]) & 0xFFFFFFFF)
            img.u32(base + 0xB0 + 4 * i, line)
        else:
            img.u32(base + 0xB0 + 4 * i, 0 if destroy or i >= count else 0)
    img.u32(base + 0xF0, count)
    img.u32(base + 0xAC, rng.choice([1, 5, 40, 120, 300, rng.randrange(1, 400)]))
    img.u32(base + 0x240, rng.choice([built_for, 0xFFFFFFFF, rng.randrange(0, 20)]) if built_for is not None else rng.randrange(0, 20))
    img.u32(SOFTWARE, rng.choice([0, 1]))


@B(0x004A3830, "BioWindow_Construct", this=PS_OBJ[0])
def gen_bio_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0x244, rng)
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@B(0x004A3A40, "BioWindow_ScalarDelete", this=PS_OBJ[0])
def gen_bio_delete(env, n):
    img, rng = new_image(env)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@B(0x004A3A60, "BioWindow_Destroy", this=PS_OBJ[0])
def gen_bio_destroy(env, n):
    img, rng = new_image(env)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@B(0x004A3AF0, "BioWindow_Draw", this=PS_OBJ[0])
def gen_bio_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@B(0x004A3BD0, "BioWindow_Update", this=PS_OBJ[0])
def gen_bio_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    character = current_character(img)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng), built_for=character)
    # half the cases have the lines built for the current character (no rebuild), half do not
    if rng.random() < 0.5:
        img.u32(obj(env) + 0x240, character)
    else:
        img.u32(obj(env) + 0x240, rng.choice([0xFFFFFFFF, (character + 1) % 20, 99]))
    pads(img, rng, scroll=rng.choice(["up", "down", None, None]))
    return [], img.pokes()


@B(0x004A3E00, "BioWindow_ScrollUp", this=PS_OBJ[0])
def gen_bio_scroll_up(env, n):
    img, rng = new_image(env)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


@B(0x004A3E40, "BioWindow_ScrollDown", this=PS_OBJ[0])
def gen_bio_scroll_down(env, n):
    img, rng = new_image(env)
    bio_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


# ---- the page dots -----------------------------------------------------------------------------------------------------------------------------

def dots_state(env, img, rng, base, pool):
    window(env, img, rng, base, 0xB0, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    img.u32(base + 0xAC, rng.choice([pool.take(), pool.take(), 0]) if False else pool.take())
    img.u32(SOFTWARE, rng.choice([0, 1]))


@B(0x004A5450, "PageDots_Construct", this=PS_OBJ[0])
def gen_dots_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0xB0, rng)
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@B(0x004A54F0, "PageDots_ScalarDelete", this=PS_OBJ[0])
def gen_dots_delete(env, n):
    img, rng = new_image(env)
    dots_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@B(0x004A5510, "PageDots_Destroy", this=PS_OBJ[0])
def gen_dots_destroy(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    dots_state(env, img, rng, obj(env), pool)
    if rng.random() < 0.15:
        img.u32(obj(env) + 0xAC, 0)
    return [], img.pokes()


@B(0x004A5690, "PageDots_Update", this=PS_OBJ[0])
def gen_dots_update(env, n):
    img, rng = new_image(env)
    dots_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


@B(0x004A55C0, "PageDots_Draw", this=PS_OBJ[0])
def gen_dots_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    dots_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


# ---- the header --------------------------------------------------------------------------------------------------------------------------------

def header_state(env, img, rng, base, pool):
    window(env, img, rng, base, 0xB0, pool, children=0)
    img.u32(base, PA(env, VT_WINDOW))
    # (real text elements: the window's update runs their own update natively, as retail does; a fake text element's vtable slot 1 is the items' set-position)
    img.u32(base + 0x28, real_text_element(env, img, rng, 0))
    img.u32(base + 0x2C, real_text_element(env, img, rng, 1))
    img.u32(base + 0xA8, 2)
    img.u32(base + 0xAC, font_ptr(env, rng))


@B(0x004A56B0, "PlayerHeader_Construct", this=PS_OBJ[0])
def gen_header_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng, player=rng.choice([0, 1, 2, 3]))
    img.garbage(obj(env), 0xB0, rng)
    return [font_ptr(env, rng), coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@B(0x004A57B0, "PlayerHeader_ScalarDelete", this=PS_OBJ[0])
def gen_header_delete(env, n):
    img, rng = new_image(env)
    header_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@B(0x004A57D0, "PlayerHeader_Destroy", this=PS_OBJ[0])
def gen_header_destroy(env, n):
    img, rng = new_image(env)
    header_state(env, img, rng, obj(env), Pool(env, img, rng))
    return [], img.pokes()


@B(0x004A57E0, "PlayerHeader_Update", this=PS_OBJ[0])
def gen_header_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng, player=rng.choice([0, 1, 2, 3, 7, 7]))
    header_state(env, img, rng, obj(env), Pool(env, img, rng))
    if rng.random() < 0.1:
        img.u32(0x0056A888, rng.choice([8, 9, 0xFFFFFFFF]))
    return [], img.pokes()


# ---- the main menu window -----------------------------------------------------------------------------------------------------------------------

def menu_state(env, img, rng, base, pool, *, destroy=False, draw=False):
    window(env, img, rng, base, 0x240, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for i in range(4):
        poke_props(env, img, i, rng)
    rows = []
    for i in range(5):
        if draw and i == 1:
            rows.append(item_row(env, img, rng, i, i + 1, real_text=True, vtable=MENU_REAL["text"], selectable=1))
        else:
            rows.append(item_row(env, img, rng, i, i + 1, selectable=1))
    for i in range(0x60):
        img.u32(base + 0xB0 + 4 * i, (rows[i] if i < len(rows) and rng.random() < 0.9 else 0) if destroy else (rows[i] if i < len(rows) else 0))
    img.u32(base + 0xAC, keep(list_object(env, img, rng, 0, rows, highlighted=rng.randrange(0, 5))))
    for i in range(4):
        img.u32(props_at(env, i), MENU_REAL["props"])      # (the destructor deletes the style through its virtual destructor)
    img.u32(base + 0x230, keep(props_at(env, rng.randrange(4))) if destroy else props_at(env, 0))
    img.u32(base + 0x234, rng.randrange(0, 6))
    img.u32(base + 0x238, keep(pool.take()))
    img.u32(base + 0x23C, keep(pool.take()))
    img.u32(REPLAY_STATE, rng.choice([0, 1, 1, 7, 2]))


@B(0x004A5890, "PlayerMenu_Construct", this=PS_OBJ[0])
def gen_menu_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0x240, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.u32(REPLAY_STATE, rng.choice([0, 1, 1, 7, 2]))
    return [rng.choice([0, 1]), font_ptr(env, rng), coords(rng), coords(rng)], img.pokes()


@B(0x004A5CA0, "PlayerMenu_ScalarDelete", this=PS_OBJ[0])
def gen_menu_delete(env, n):
    img, rng = new_image(env)
    menu_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@B(0x004A5CC0, "PlayerMenu_Destroy", this=PS_OBJ[0])
def gen_menu_destroy(env, n):
    img, rng = new_image(env)
    menu_state(env, img, rng, obj(env), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@B(0x004A5D80, "PlayerMenu_Draw", this=PS_OBJ[0])
def gen_menu_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    menu_state(env, img, rng, obj(env), Pool(env, img, rng), draw=True)
    pads(img, rng)
    return [coords(rng), coords(rng)], img.pokes()
