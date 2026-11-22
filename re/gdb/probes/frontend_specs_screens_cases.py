"""Phase 4.10e1 case generators, part 2: the generators of each group (see frontend_specs_screens.py for the slab, the seeds and the stubs).

A function that works on a window's own state (destroy, draw, update, poll) is given a window the generator builds by hand: the window's vtable is the
fake page vtable (its per-frame hook is a reported service), its children, lists, styles and sprites are fake objects whose virtual methods are reported
services.  A constructor is called on garbage and builds its objects for real in the arena (the toolkit and the window base are native); everything
outside them is a stub that hands back arena blocks.
"""

from __future__ import annotations

import struct

from create_specs import spec
from frontend_specs_menu import FIND_FONTS, FONT_SIZE, STRING_POOL, VT_FONT, coords, sprite_at
from frontend_specs_screens import (ANIM, CACHED_DATABASE, CAMERA, CUSTOM_SKATER_GLOBAL, DATABASE, EDITOR_MODE, FRONT_FLAG, LAST_TICK, OBJ, POOL, POOL_N,
                                    POOL_STRIDE, PRE_MANAGER, RENDERER, RENDERER_POINTER, SKATER, SPIN_LEFT, SPIN_RIGHT, TEXT_POOL, TEXT_STRIDE, TEXTS, TICK, VT_ANY,
                                    VT_REAL, ANIM_SCALE)
from frontend_specs_skstat import text_element
from frontend_specs_skwin import (CUSTOM, MANAGER, TEX, VT_WINDOW, A, item_row, list_object, new_image, skater_state, window_part, world)


def gspec(group):
    def make(address, name, this=None):
        resolve = (lambda env: env.slab + this) if this is not None else None
        return spec(address, name, group, this=resolve)
    return make


class Pool:
    """The fake objects of one case: each is handed out once."""

    def __init__(self, env, img, rng):
        self.env, self.img, self.rng = env, img, rng
        self.next_index = 0

    def take(self, hidden=None):
        index = self.next_index
        self.next_index += 1
        base = A(self.env, POOL + POOL_STRIDE * index)
        self.img.garbage(base + 4, POOL_STRIDE - 4, self.rng)
        self.img.u32(base, A(self.env, VT_ANY))
        self.img.u32(base + 0x14, self.rng.choice([0, 0, 1]) if hidden is None else hidden)
        return base

    def maybe(self, chance=0.15):
        """A fake object, or nothing (the destructors skip what is not there)."""
        return 0 if self.rng.random() < chance else self.take()


def read_u32(img, address):
    return struct.unpack("<I", bytes(img.b[address + i] for i in range(4)))[0]


def skater(env, img, rng, **kw):
    """The skater manager and records of a case (frontend_specs_skwin.skater_state), with the active slot's costume one its character has: retail asserts
    and then reads through a null model name otherwise."""
    slot, chosen = skater_state(env, img, rng, **kw)
    # the screens work on the player's own slot (the pad the active player stands on): make that the active slot, holding the generated entry
    active = read_u32(img, MANAGER + 0x64)
    pad = read_u32(img, 0x0056A888) & 7
    source = MANAGER + 4 + active * 0xC
    entry = MANAGER + 4 + pad * 0xC
    for i in range(3):
        img.u32(entry + 4 * i, read_u32(img, source + 4 * i))
    img.u32(MANAGER + 0x64, pad)
    row = 0x0053A3B8 + 0x170 * read_u32(img, entry)
    flags = env.u32(row + 0x15C)
    count = 0
    for i in range(4):
        if (i == 2 and flags & 8) or (i == 3 and flags & 0x10):
            break
        if env.u32(row + 8 * i) != 0:
            count += 1
    img.u32(entry + 8, rng.randrange(count) if count else 0)
    return slot, chosen


def common(env, img, rng, *, database=None, player=None):
    """The mutable globals every screen reads: the clock and pad edges, the screen size, the skater manager and records, the renderer, the front-end flag.
    Returns the custom slot the active player stands on."""
    world(env, img, rng)
    if player is not None:
        img.u32(0x0056A888, player)
    slot, _chosen = skater(env, img, rng)
    img.u32(RENDERER_POINTER, A(env, RENDERER))
    img.u32(A(env, RENDERER) + 0x88, rng.randrange(1, 0x10000))
    img.u32(FRONT_FLAG, rng.choice([0, 1, 2]))
    # (a loop waits until the clock is two ticks past the last one it saw: start it close, so the wait is a few Game_FrameTick calls)
    tick = rng.randrange(8, 50000)
    img.u32(TICK, tick)
    img.u32(LAST_TICK, tick - rng.choice([0, 1, 2, 3, 5]))
    img.u32(PRE_MANAGER, rng.randrange(0x1000, 0x100000))
    img.u32(CACHED_DATABASE, A(env, TEX))
    img.u32(DATABASE, A(env, TEX) if database is None else database)
    img.u32(EDITOR_MODE, rng.choice([0, 0, 1]))
    img.u32(ANIM_SCALE, rng.randrange(0, 0x1000))
    img.garbage(CAMERA, 0x40, rng)
    img.u32(CUSTOM_SKATER_GLOBAL, rng.choice([0, A(env, SKATER)]))
    img.u8(SPIN_LEFT, rng.choice([0, 0, 1]))
    img.u8(SPIN_RIGHT, rng.choice([0, 0, 1]))
    return slot


def font_ptr(env, rng):
    return A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4))


def text_ptr(env, rng):
    return A(env, TEXTS + TEXT_STRIDE * rng.randrange(len(TEXT_POOL)))


def font_object(env, img, rng, base):
    """A Font object at `base`: a copy of one of the seeded fonts (its glyph table and character map are valid)."""
    img.put(base, env.bytes(A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)), FONT_SIZE))


def window(env, img, rng, base, size, pool, *, hidden=None, children=None):
    """The ScreenElement / Window part of a screen object: fake page vtable, hidden flag, place, children (fake)."""
    window_part(env, img, rng, base, size, hidden=hidden, children=children)
    img.u32(base, A(env, VT_WINDOW))


def real_vtable(img, base, name):
    img.u32(base, VT_REAL[name])


# ---- create_ui.cpp: the text box and the confirm box ---------------------------------------------------------------------------------------------

UI = gspec("crui")


def box_state(env, img, rng, base, *, vtable, lines=None):
    img.garbage(base, 0x428, rng)
    img.u32(base, vtable)
    img.u32(base + 4, rng.choice([0, 1]))
    img.u32(base + 8, coords(rng))
    img.u32(base + 0xC, coords(rng))
    img.u32(base + 0x10, font_ptr(env, rng))
    count = rng.randrange(0, 8) if lines is None else lines
    img.u32(base + 0x414, count)
    for i in range(8):
        img.put(base + 0x14 + 0x80 * i, rng.choice(STRING_POOL[:15] + [b"A line of text for the box", b"Another line", b"W" * 20]) + b"\0")


def confirm_state(env, img, rng, base, pool, *, highlighted=None, complete=True):
    box_state(env, img, rng, base, vtable=VT_REAL["confirm"])
    yes = item_row(env, img, rng, 0, 1, selectable=1)
    no = item_row(env, img, rng, 1, 0, selectable=1)
    pick = rng.choice([0, 1]) if highlighted is None else highlighted
    list_at = list_object(env, img, rng, 2, [yes, no], highlighted=pick)
    keep = (lambda value: value) if complete else (lambda value: 0 if rng.random() < 0.2 else value)
    img.u32(base + 0x418, keep(list_at))
    img.u32(base + 0x41C, keep(yes))
    img.u32(base + 0x420, keep(no))
    img.u32(base + 0x424, keep(pool.take()))
    return base


@UI(0x00425A40, "TextBox_Construct", this=OBJ[0])
def gen_box_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x428, rng)
    return [font_ptr(env, rng), text_ptr(env, rng), coords(rng), coords(rng)], img.pokes()


@UI(0x00425B40, "TextBox_ScalarDelete", this=OBJ[0])
def gen_box_delete(env, n):
    img, rng = new_image(env)
    box_state(env, img, rng, A(env, OBJ[0]), vtable=VT_REAL["textbox"])
    return [rng.choice([0, 1, 2, 3, rng.getrandbits(32)])], img.pokes()


@UI(0x00425B60, "TextBox_Destroy", this=OBJ[0])
def gen_box_destroy(env, n):
    img, rng = new_image(env)
    box_state(env, img, rng, A(env, OBJ[0]), vtable=VT_REAL["textbox"])
    return [], img.pokes()


@UI(0x00425B70, "TextBox_Draw", this=OBJ[0])
def gen_box_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    box_state(env, img, rng, A(env, OBJ[0]), vtable=VT_REAL["textbox"])
    return [], img.pokes()


@UI(0x00425FA0, "ConfirmBox_Construct", this=OBJ[0])
def gen_confirm_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x428, rng)
    return [font_ptr(env, rng), text_ptr(env, rng), coords(rng), coords(rng)], img.pokes()


@UI(0x004261F0, "ConfirmBox_ScalarDelete", this=OBJ[0])
def gen_confirm_delete(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool, complete=False)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@UI(0x00426210, "ConfirmBox_Destroy", this=OBJ[0])
def gen_confirm_destroy(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool, complete=False)
    return [], img.pokes()


@UI(0x004262A0, "ConfirmBox_Highlight", this=OBJ[0])
def gen_confirm_highlight(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool)
    return [rng.choice([0, 1]) | (rng.choice([0, 0, 1, rng.getrandbits(24)]) << 8)], img.pokes()      # (only the low byte names an item; 0 and 1 exist)


@UI(0x004262C0, "ConfirmBox_Draw", this=OBJ[0])
def gen_confirm_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool)
    return [], img.pokes()


@UI(0x00426300, "ConfirmBox_Update", this=OBJ[0])
def gen_confirm_update(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool)
    return [], img.pokes()


@UI(0x00426310, "ConfirmBox_Poll", this=OBJ[0])
def gen_confirm_poll(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    confirm_state(env, img, rng, A(env, OBJ[0]), pool)
    return [rng.choice([-1, 0, 1, 2, 3, 2, 3, 4, 7]) & 0xFFFFFFFF, rng.choice([-1, -1, 0, 1]) & 0xFFFFFFFF], img.pokes()


# ---- cresave.cpp: the save-skater screen -----------------------------------------------------------------------------------------------------------

SAVE = gspec("crsave")


def save_state(env, img, rng, base, pool, *, state=None, destroy=False, box_ok=True):
    window(env, img, rng, base, 0x378, pool, children=rng.randrange(0, 3))
    background = rng.choice([0, sprite_at(env, rng.randrange(8)), pool.take()]) if rng.random() < 0.2 else pool.take()
    img.u32(base + 0xAC, background)
    font_object(env, img, rng, base + 0xB0)
    font_object(env, img, rng, base + 0x1FC)
    first = item_row(env, img, rng, 2, 0, selectable=1)
    second = item_row(env, img, rng, 3, 0xFF, selectable=1)
    list_at = list_object(env, img, rng, 0, [first, second], highlighted=rng.choice([-1, 0, 1, 1]))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + 0x348, keep(list_at))
    img.u32(base + 0x34C, keep(pool.take()))
    img.u32(base + 0x350, keep(pool.take()))
    img.u32(base + 0x354, keep(first))
    img.u32(base + 0x358, keep(second))
    img.u32(base + 0x35C, keep(pool.take()))
    img.u32(base + 0x360, keep(pool.take()))
    img.u32(base + 0x364, rng.choice([0, 1, 2, 0, 1, 2, 3, 9]) if state is None else state)
    box = A(env, OBJ[1])
    confirm_state(env, img, rng, box, pool, complete=not destroy)
    img.u32(base + 0x368, keep(box) if box_ok else 0)
    for i in range(3):
        img.u32(base + 0x36C + 4 * i, pool.take())
    return base


@SAVE(0x0042BE40, "CreateSave_InitCallbacks")
def gen_save_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@SAVE(0x0042BE60, "CreateSave_ExitCallback")
def gen_save_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@SAVE(0x0042BE80, "CreateSave_Construct", this=OBJ[0])
def gen_save_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x378, rng)
    return [], img.pokes()


@SAVE(0x0042C6F0, "CreateSave_ScalarDelete", this=OBJ[0])
def gen_save_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SAVE(0x0042C710, "CreateSave_Destroy", this=OBJ[0])
def gen_save_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@SAVE(0x0042C860, "CreateSave_Draw", this=OBJ[0])
def gen_save_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@SAVE(0x0042C910, "CreateSave_Update", this=OBJ[0])
def gen_save_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@SAVE(0x0042C940, "CreateSave_Step", this=OBJ[0])
def gen_save_step(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    for offset in (0x56B099, 0x56B0A9):
        img.u8(offset, rng.choice([0, 0, 1]))
    return [], img.pokes()


@SAVE(0x0042CB50, "CreateSave_DrawList", this=OBJ[0])
def gen_save_draw_list(env, n):
    img, rng = new_image(env)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=0)
    return [], img.pokes()


@SAVE(0x0042CB60, "CreateSave_DrawBox", this=OBJ[0])
def gen_save_draw_box(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    save_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=1)
    return [], img.pokes()


@SAVE(0x0042CB70, "CreateSave_MmuScreenThunk")
def gen_save_mmu(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


# ---- statedit.cpp and the stat window's constructor ----------------------------------------------------------------------------------------------

STAT = gspec("crstat")


def stat_page_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, 0x630, pool, children=rng.randrange(0, 3))
    for slot in (0xAC, 0x1F8, 0x344):
        font_object(env, img, rng, base + slot)
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    list_at = list_object(env, img, rng, 0, [item_row(env, img, rng, 2, 0, selectable=1)], highlighted=0)
    img.u32(base + 0x490, keep(list_at))
    count = rng.randrange(0, 12)
    for i in range(0x60):
        img.u32(base + 0x494 + 4 * i, pool.take() if i < count else 0)
    img.u32(base + 0x614, keep(pool.take()))
    img.u32(base + 0x618, keep(pool.take()))
    img.u32(base + 0x61C, pool.take())
    img.u32(base + 0x620, keep(pool.take()))
    img.u32(base + 0x624, rng.choice([0, 1, 2]))
    img.u32(base + 0x628, keep(pool.take()))
    img.u32(base + 0x62C, keep(pool.take()))
    return base


@STAT(0x004B5BB0, "StatPage_Construct", this=OBJ[0])
def gen_statpage_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x630, rng)
    return [coords(rng), coords(rng)], img.pokes()


@STAT(0x004B6060, "StatPage_ScalarDelete", this=OBJ[0])
def gen_statpage_delete(env, n):
    img, rng = new_image(env)
    stat_page_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@STAT(0x004B6080, "StatPage_Destroy", this=OBJ[0])
def gen_statpage_destroy(env, n):
    img, rng = new_image(env)
    stat_page_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


def custom_character(env, rng, custom):
    """A character whose table row is a custom skater's (flag 2) or not; the row table is static data."""
    rows = [c for c in range(20) if bool(env.u32(0x0053A3B8 + 0x170 * c + 0x15C) & 2) == custom]
    return rng.choice(rows)


def statedit_state(env, img, rng, base, pool, *, destroy=False, custom=None):
    window(env, img, rng, base, 0x3A0, pool, children=rng.randrange(0, 3))
    keep = (lambda value: 0 if rng.random() < 0.2 else value) if destroy else (lambda value: value)
    img.u32(base + 0xAC, keep(pool.take()))
    font_object(env, img, rng, base + 0xB0)
    font_object(env, img, rng, base + 0x1FC)
    img.u32(base + 0x348, rng.choice([-1, 0, 5, 9, 0x3F]) & 0xFFFFFFFF)
    img.u32(base + 0x34C, rng.randrange(0, 0x40))
    anim = A(env, ANIM)
    img.garbage(anim + 4, 0x17C, rng)
    img.u32(anim, A(env, VT_ANY))
    img.u32(base + 0x350, keep(anim))
    img.u32(base + 0x39C, pool.take())
    return base


@STAT(0x004B7430, "StatEdit_Construct", this=OBJ[0])
def gen_stat_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng, player=rng.choice([0, 0, 1]))
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    img.garbage(A(env, OBJ[0]), 0x3A0, rng)
    return [], img.pokes()


@STAT(0x004B77B0, "StatEdit_ScalarDelete", this=OBJ[0])
def gen_stat_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    statedit_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@STAT(0x004B77D0, "StatEdit_Destroy", this=OBJ[0])
def gen_stat_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    statedit_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@STAT(0x004B7960, "StatEdit_LoadFonts", this=OBJ[0])
def gen_stat_fonts(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[0]), 0x3A0, rng)
    return [], img.pokes()


@STAT(0x004B7B50, "StatEdit_SetupScene", this=OBJ[0])
def gen_stat_scene(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    img.garbage(A(env, OBJ[0]), 0x3A0, rng)
    return [], img.pokes()


@STAT(0x004B7F40, "StatEdit_Draw", this=OBJ[0])
def gen_stat_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    statedit_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@STAT(0x004B8020, "StatEdit_UpdateSkater", this=OBJ[0])
def gen_stat_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    statedit_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@STAT(0x004B8080, "StatEdit_Step", this=OBJ[0])
def gen_stat_step(env, n):
    img, rng = new_image(env)
    statedit_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


# ---- creedit.cpp: the editor and its personal-information page -----------------------------------------------------------------------------------

EDIT = gspec("credit")


def personal_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, 0x3B0, pool, children=rng.randrange(0, 3))
    font_object(env, img, rng, base + 0xB0)
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    first = item_row(env, img, rng, 4, 0, selectable=1)
    list_at = list_object(env, img, rng, 1, [first], highlighted=0)
    img.u32(base + 0x1FC, keep(list_at))
    count = rng.randrange(0, 9)
    for i in range(0x60):
        img.u32(base + 0x200 + 4 * i, pool.take() if i < count else 0)
    for slot in (0x380, 0x384, 0x388, 0x38C, 0x390):
        img.u32(base + slot, keep(pool.take()))
    for i in range(5):
        img.u32(base + 0x398 + 4 * i, pool.take())
    img.u32(base + 0x394, rng.choice([0, 1]))
    img.u8(base + 0xAC, rng.choice([0, 1]))
    return base


def editor_state(env, img, rng, base, pool, *, destroy=False, page=None, mode=None):
    window(env, img, rng, base, 0x3E4, pool, children=rng.randrange(0, 3))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + 0xAC, keep(pool.take()))
    font_object(env, img, rng, base + 0xB0)
    font_object(env, img, rng, base + 0x1FC)
    for i in range(6):
        img.u32(base + 0x348 + 4 * i, pool.take())
    skater = A(env, SKATER)
    img.garbage(skater + 4, 0x33C, rng)
    img.u32(skater, A(env, VT_ANY))
    img.u32(skater + 4, rng.choice([0, 1]))
    img.u32(skater + 8, rng.choice([0, 1, 5]))
    img.u32(skater + 0x6C, rng.choice([0, A(env, 0x0F00)]))
    img.u32(base + 0x360, keep(skater))
    anim = A(env, ANIM)
    img.garbage(anim + 4, 0x17C, rng)
    img.u32(anim, A(env, VT_ANY))
    img.u32(base + 0x364, keep(anim))
    img.u32(base + 0x368, rng.choice([-1, 0, 7, 0x3F]) & 0xFFFFFFFF)
    img.u32(base + 0x36C, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 9]) if page is None else page)
    img.u32(base + 0x370, rng.choice([0, 0, 1]) if mode is None else mode)
    for i in range(7):
        img.u16(base + 0x374 + 2 * i, rng.randrange(0, 0x10000))
    img.u32(base + 0x3BC, rng.choice([0, 1]))
    img.u32(base + 0x3C0, rng.choice([0, 1]))
    img.u32(base + 0x3C4, pool.take())
    img.u32(base + 0x3C8, text_element(env, img, rng, 12))
    img.u32(base + 0x3CC, pool.take())
    personal = personal_state(env, img, rng, A(env, OBJ[1]), pool)
    img.u32(base + 0x3D0, personal)
    img.u32(base + 0x3D4, pool.take())
    img.u32(base + 0x3D8, A(env, OBJ[2]))
    appearance = A(env, OBJ[2])
    img.garbage(appearance, 0x268, rng)
    img.u32(appearance, A(env, VT_ANY))
    img.u32(appearance + 0x14, rng.choice([0, 1]))
    for slot in (0xB0, 0xB4, 0xBC, 0xC4, 0xC8, 0xCC):
        img.u32(appearance + slot, rng.randrange(0, 400))
    pick = item_row(env, img, rng, 5, 0, selectable=1)
    img.u32(appearance + 0xD0, list_object(env, img, rng, 2, [pick], highlighted=0))    # the list SetPage sends to its first item
    img.u32(base + 0x3DC, rng.randrange(0, 256))
    img.u32(base + 0x3E0, rng.choice([0, 1, 2, 3, 0xFFFFFFFF]))
    return base


@EDIT(0x004271C0, "CreateEditor_Construct", this=OBJ[0])
def gen_editor_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x3E4, rng)
    return [rng.choice([0, 1, 2, 3, 0, 1]), rng.choice([0, 0, 1])], img.pokes()


@EDIT(0x004277E0, "CreateEditor_ScalarDelete", this=OBJ[0])
def gen_editor_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@EDIT(0x00427800, "CreateEditor_Destroy", this=OBJ[0])
def gen_editor_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@EDIT(0x00427920, "CreateEditor_SetupScene", this=OBJ[0])
def gen_editor_scene(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    if rng.random() < 0.1:
        img.u32(A(env, OBJ[0]) + 0x3D8, 0)       # the appearance window is not set yet
    return [], img.pokes()


@EDIT(0x00427BE0, "CreateEditor_LoadFonts", this=OBJ[0])
def gen_editor_fonts(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[0]), 0x3E4, rng)
    return [], img.pokes()


@EDIT(0x00427DC0, "CreateEditor_SetPage", this=OBJ[0])
def gen_editor_set_page(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    base = A(env, OBJ[0])
    editor_state(env, img, rng, base, pool)
    return [rng.choice([0, 1, 2, 3, 2, 2])], img.pokes()


@EDIT(0x00427E40, "CreateEditor_UpdateFrame", this=OBJ[0])
def gen_editor_update_frame(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    for pad in range(2):
        img.u8(SPIN_LEFT + 0x16C * pad, rng.choice([0, 0, 1]))
        img.u8(SPIN_RIGHT + 0x16C * pad, rng.choice([0, 0, 1]))
    return [], img.pokes()


@EDIT(0x004280D0, "CreateEditor_ChangeBuild", this=OBJ[0])
def gen_editor_change_build(env, n):
    img, rng = new_image(env)
    slot = common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    # the build the stats were set for against the custom skater's: equal now and then
    if rng.random() < 0.3:
        img.u32(A(env, OBJ[0]) + 0x3E0, img.b[CUSTOM + 0x80 * slot + 0x24] | (0xFFFFFF00 if img.b[CUSTOM + 0x80 * slot + 0x24] & 0x80 else 0))
    return [], img.pokes()


@EDIT(0x004281E0, "CreateEditor_Draw", this=OBJ[0])
def gen_editor_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@EDIT(0x004282D0, "CreateEditor_Update", this=OBJ[0])
def gen_editor_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@EDIT(0x004283F0, "CreateEditor_ShowButtons", this=OBJ[0])
def gen_editor_buttons(env, n):
    img, rng = new_image(env)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 0x100, 0x101, rng.getrandbits(32)])], img.pokes()


@EDIT(0x004288A0, "PersonalPage_Construct", this=OBJ[0])
def gen_personal_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x3B0, rng)
    return [coords(rng), coords(rng)], img.pokes()


@EDIT(0x00428C80, "PersonalPage_Delete", this=OBJ[0])
def gen_personal_delete(env, n):
    img, rng = new_image(env)
    personal_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@EDIT(0x00428CA0, "PersonalPage_BuildList", this=OBJ[0])
def gen_personal_build(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    base = A(env, OBJ[0])
    img.garbage(base, 0x3B0, rng)
    img.u32(base + 0x388, pool.take())
    img.u32(base + 0x38C, pool.take())
    return [], img.pokes()


@EDIT(0x004292D0, "PersonalPage_Destroy", this=OBJ[0])
def gen_personal_destroy(env, n):
    img, rng = new_image(env)
    personal_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@EDIT(0x0042B4C0, "AppearancePage_HighlightFirst", this=OBJ[2])
def gen_appearance_first(env, n):
    img, rng = new_image(env)
    base = A(env, OBJ[2])
    img.garbage(base, 0x268, rng)
    items = [item_row(env, img, rng, i, i, selectable=rng.choice([0, 1, 1])) for i in range(rng.randrange(1, 6))]
    img.u32(base + 0xD0, list_object(env, img, rng, 2, items, highlighted=rng.randrange(-1, len(items))))
    return [], img.pokes()


# ---- the screen loops and their callbacks ------------------------------------------------------------------------------------------------------------

LOOP = gspec("crloop")


@LOOP(0x0042BE70, "CreateSave_ScreenCall")
def gen_save_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x0042CB80, "CreateSave_Run")
def gen_save_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    return [], img.pokes()


@LOOP(0x004B5BA0, "StatEdit_ScreenCall")
def gen_stat_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng, player=rng.choice([0, 0, 0, 1]))
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x004B8090, "StatEdit_Run")
def gen_stat_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng, player=rng.choice([0, 0, 0, 1]))
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    return [], img.pokes()


@LOOP(0x004271A0, "CreateEditor_ScreenCall")
def gen_editor_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng, database=0)
    img.u32(A(env, OBJ[3]), rng.choice([0, 1, 2, 3]))
    return [A(env, OBJ[3]), A(env, OBJ[3]) + 8], img.pokes()


@LOOP(0x00428420, "CreateEditor_Run")
def gen_editor_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng, database=0 if rng.random() < 0.9 else A(env, TEX))
    return [rng.choice([0, 1, 2, 3]), rng.choice([0, 0, 1])], img.pokes()
