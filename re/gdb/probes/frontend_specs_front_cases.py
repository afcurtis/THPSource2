"""Phase 4.10i probe, part 2: the case generators of the CMenu class (fmenu) and the small screens (fscr); see frontend_specs_front.py for the groups, regions and stubs.

A case pokes a world (the status, the game mode, the level, the pads, the clock, the menus as real CMenu objects built in the slab) and calls one retail function. The menus are built
by `menu_image` in the layout the constructor and AddEntry leave (the class is replayed on its own by the fmenu group).
"""

from __future__ import annotations

import struct

from create_specs import spec
from frontend_specs_front import MENU_STRIDE, SLAB, STRING_COUNT, STRING_POOL, STRING_STRIDE, TICK, menu_at, string_at
from frontend_specs_menu import Image

VT_CMENU = 0x00518A00
STATUS = 0x00561BFC
GOT_NEW_STATUS = 0x005616D8
GAME_MODE = 0x00533F38
PLAYER_COUNT = 0x00533F3C
PLAYERS = 0x0056A858
CURRENT_LEVEL = 0x0056A898
ANIM_SCALE = 0x0056865C
SWAPPED = 31                       # the string slot that holds a name with its case swapped
HORSE_WORD = 0x0056A478
HORSE_LENGTH = 0x0056A474
NAME_CELL = {"yes": 0x0052E410, "no": 0x0052E414}

# the menu variables (the pointer cells the screens keep their menus in)
MENU_VARS = [0x00561C1C, 0x00561C20, 0x00561C24, 0x00561C28, 0x00561C2C, 0x00561C30, 0x00561C34, 0x00561C38, 0x00561C3C, 0x00561C40, 0x00561C44]


def fmenu(address, name, this=None):
    return spec(address, name, "fmenu", this=(lambda env: menu_at(env, this)) if this is not None else None)


def fscr(address, name, this=None):
    return spec(address, name, "fscr", this=(lambda env: menu_at(env, this)) if this is not None else None)


def new_image(env):
    img = Image()
    clock = env.rng.randrange(0, 100000)
    img.u32(TICK, clock)                       # (the running game moves its clock between the snapshot and the call: every case pokes it)
    img.u32(0x0056E320, clock)
    return img, env.rng


def pool_pointer(env, index):
    return string_at(env, index)


def menu_image(env, img, rng, index, *, count=None, line=None, names=None, hidden=(), unchoosable=(), spacing=None, heights=False):
    """A CMenu in the layout the constructor and AddEntry leave: the header, fifteen entries of scales and colours, `count` names. Returns (base, indices of the names)."""
    base = menu_at(env, index)
    if names is None:
        count = rng.randrange(0, 15) if count is None else count
        names = rng.sample(range(0, 20), min(count, 20))[:count]
    count = len(names)
    line = rng.randrange(0, max(count, 1)) if line is None else line
    sel_scale = rng.choice([0x14F, 0x190, 0x100])
    norm_scale = rng.choice([0x100, 0xF0])
    img.put(base, bytes(0x180))
    img.u32(base, VT_CMENU)
    img.u8(base + 4, rng.choice([0, 1, 2]))
    img.u8(base + 6, line)
    img.u8(base + 9, count)
    img.u32(base + 0xC, rng.choice([0x100, 0x80, 0x180, 0x1E0]))
    img.u32(base + 0x10, rng.randrange(0, 0xA0))
    img.u32(base + 0x14, spacing if spacing is not None else rng.choice([0x16, 0x12, 0x20, 0x18]))
    for e in range(15):
        p = base + 0x18 * (e + 1)
        shown = 0 if e in hidden else 1
        img.u16(p + 8, sel_scale)
        img.u16(p + 0xA, norm_scale)
        cur = sel_scale if e == line else norm_scale
        img.u16(p + 4, rng.choice([cur, cur, cur, rng.randrange(0, 0x200)]))
        img.u16(p + 6, rng.choice([cur, cur, rng.randrange(0, 0x200)]))
        img.u8(p + 0xC, rng.choice([0, 0, 0, 8, 0x10]) if heights else 0)
        img.u8(p + 0xD, shown if e < count else 1)
        img.u8(p + 0xE, 0 if e in unchoosable else 1)
        img.put(p + 0xF, bytes([0xB6, 0x84, 0x24, 0x7A, 0x7A, 0x7A]))
        img.u8(p + 0x15, rng.choice([0x80, 0x40, 0xBC, rng.randrange(256)]))
    for e, n in enumerate(names):
        img.u32(base + 0x18 * (e + 1), pool_pointer(env, n))
    return base, names


def swapped_case(img, env, name_index):
    """Slot SWAPPED holds the pool string with its letters' case swapped (the lookups ignore case); returns its address."""
    text = STRING_POOL[name_index].decode("latin-1").swapcase().encode("latin-1")
    img.put(string_at(env, SWAPPED), text + b"\0" + bytes(STRING_STRIDE - len(text) - 1))
    return string_at(env, SWAPPED)


def clock_and_pads(env, img, rng):
    img.u32(TICK, rng.choice([0, 15, 30, 31, 45, 59, 60, 61, 90, rng.randrange(0, 100000), rng.randrange(0, 0xFFFFFFF)]))
    for address in (0x0056B098, 0x0056B0A8, 0x0056B204, 0x0056B214):
        img.u8(address, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561C10, rng.choice([0, 0, 1, 5, 15, 16, 19, 20, 24, 100, 0xFFFFFFFF]))


# ---- the CMenu class ----------------------------------------------------------------------------------------------------------------------------------------------

@fmenu(0x0044B640, "CMenu__new", this=11)
def gen_new(env, n):
    img, rng = new_image(env)
    img.put(menu_at(env, 11), bytes(0x180))
    return [rng.choice([0x100, 0, 0x80, -1 & 0xFFFFFFFF]), rng.choice([0, 0x50, 0xA0]), rng.choice([0, 1, 2, 0x101]), rng.choice([0x190, 0x14F, 0x100, 0x12345678]),
            rng.choice([0x100, 0xF0, 0xABCD0100]), rng.choice([0x16, 0x12, 0x20, 0])], img.pokes()


@fmenu(0x0044B6F0, "CMenu__destruct", this=0)
def gen_destruct(env, n):
    img, rng = new_image(env)
    menu_image(env, img, rng, 0)
    return [rng.choice([0, 1, 2, 3, 0xFFFFFF01])], img.pokes()


@fmenu(0x0044B710, "CMenu__delete", this=0)
def gen_delete(env, n):
    img, rng = new_image(env)
    menu_image(env, img, rng, 0)
    return [], img.pokes()


@fmenu(0x0044B720, "CMenu__CentreY", this=0)
def gen_centre(env, n):
    img, rng = new_image(env)
    count = rng.randrange(0, 15)
    hidden = {e for e in range(count) if rng.random() < 0.25}
    menu_image(env, img, rng, 0, count=count, hidden=hidden, heights=True)
    return [], img.pokes()


@fmenu(0x0044B770, "CMenu__SetLine", this=0)
def gen_set_line(env, n):
    img, rng = new_image(env)
    menu_image(env, img, rng, 0)
    return [rng.choice([0, 1, 2, 5, 14, 15, 0x105])], img.pokes()


@fmenu(0x0044B7B0, "CMenu__Reset", this=0)
def gen_reset(env, n):
    img, rng = new_image(env)
    menu_image(env, img, rng, 0)
    return [], img.pokes()


def name_case(env, img, rng, names, *, missing=False):
    """The name a lookup asks for: an entry's name, the same with its case swapped, or (missing) one that no entry has."""
    if missing or not names:
        return string_at(env, 21)
    pick = rng.choice(names)
    return swapped_case(img, env, pick) if rng.random() < 0.5 else string_at(env, pick)


@fmenu(0x0044B800, "CMenu__FindEntry", this=0)
def gen_find(env, n):
    img, rng = new_image(env)
    _, names = menu_image(env, img, rng, 0)
    return [name_case(env, img, rng, names, missing=rng.random() < 0.15)], img.pokes()


@fmenu(0x0044B890, "CMenu__AddEntry", this=0)
def gen_add(env, n):
    img, rng = new_image(env)
    menu_image(env, img, rng, 0, count=rng.randrange(0, 15))
    return [string_at(env, rng.randrange(0, 20))], img.pokes()


def named_entry_spec(address, name):
    @fmenu(address, name, this=0)
    def gen(env, n):
        img, rng = new_image(env)
        count = rng.randrange(1, 15)
        hidden = {e for e in range(count) if rng.random() < 0.3}
        unchoosable = {e for e in range(count) if rng.random() < 0.3}
        _, names = menu_image(env, img, rng, 0, count=count, hidden=hidden, unchoosable=unchoosable)
        return [name_case(env, img, rng, names)], img.pokes()
    return gen


for _address, _name in ((0x0044B920, "CMenu__EntryOn"), (0x0044B940, "CMenu__EntryOff"), (0x0044B960, "CMenu__IsEntryOn"), (0x0044B980, "CMenu__Enable"), (0x0044B9A0, "CMenu__Disable"),
                        (0x0044B9C0, "CMenu__IsEntryShown")):
    named_entry_spec(_address, _name)


@fmenu(0x0044B9E0, "CMenu__SetEntryHeight", this=0)
def gen_height(env, n):
    img, rng = new_image(env)
    _, names = menu_image(env, img, rng, 0, count=rng.randrange(1, 15))
    return [name_case(env, img, rng, names), rng.choice([0, 5, 0x10, 0x1FF, 0xFFFFFF80])], img.pokes()


@fmenu(0x0044BA00, "CMenu__ChoiceIs", this=0)
def gen_choice(env, n):
    img, rng = new_image(env)
    _, names = menu_image(env, img, rng, 0, count=rng.randrange(1, 15))
    return [name_case(env, img, rng, names, missing=rng.random() < 0.2)], img.pokes()


@fmenu(0x0044BA20, "CMenu__PositionOf", this=0)
def gen_position(env, n):
    img, rng = new_image(env)
    count = rng.randrange(0, 15)
    hidden = {e for e in range(count) if rng.random() < 0.3}
    _, names = menu_image(env, img, rng, 0, count=count, hidden=hidden, heights=True)
    out = menu_at(env, 10) + 0x100
    img.put(out, bytes(8))
    return [name_case(env, img, rng, names, missing=rng.random() < 0.2), out, out + 4], img.pokes()


@fmenu(0x0044BA90, "CMenu__Display", this=0)
def gen_display(env, n):
    img, rng = new_image(env)
    count = rng.randrange(0, 15)
    hidden = {e for e in range(count) if rng.random() < 0.2}
    unchoosable = {e for e in range(count) if rng.random() < 0.2}
    menu_image(env, img, rng, 0, count=count, hidden=hidden, unchoosable=unchoosable, heights=True)
    clock_and_pads(env, img, rng)
    return [], img.pokes()


@fmenu(0x0044BBC0, "CMenu__Update", this=0)
def gen_update(env, n):
    img, rng = new_image(env)
    count = rng.randrange(1, 15)
    hidden = {e for e in range(count) if rng.random() < 0.3}
    unchoosable = {e for e in range(count) if rng.random() < 0.3}
    line = rng.randrange(0, count)
    # the cursor may stand on a hidden entry (the update skips ahead) but at least one entry must be shown, and can be chosen, for the loops to end
    free = rng.randrange(0, count)
    hidden.discard(free)
    unchoosable.discard(free)
    menu_image(env, img, rng, 0, count=count, line=line, hidden=hidden, unchoosable=unchoosable)
    clock_and_pads(env, img, rng)
    return [], img.pokes()


# ---- the small screens --------------------------------------------------------------------------------------------------------------------------------------------

def player_pointer(env, slot=0):
    return env.u32(PLAYERS + 4 * slot)


def career_cash(env, img, rng, character):
    img.u32(player_pointer(env) + 0x2CC0, character)
    img.u32(0x00568A6C + character * 0x104 + 8, rng.choice([0, 5, 999, 1000, 1234, 99999, 100000, 999999, 1000000, 5000000, 0xFFFFFFFF, 0x80000000, rng.randrange(0, 0xFFFFF)]))


@fscr(0x0044B180, "Front_DemoFont_Construct")
def gen_demo_font(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@fscr(0x0044B1F0, "Front_SetStatus")
def gen_set_status(env, n):
    img, rng = new_image(env)
    current = rng.randrange(0, 0x38)
    img.u32(STATUS, current)
    img.u32(GOT_NEW_STATUS, rng.choice([0, 1]))
    return [rng.randrange(0, 0x1500), rng.choice([current, rng.randrange(0, 0x38)])], img.pokes()


@fscr(0x0044B240, "Front_PrintDemo")
def gen_print_demo(env, n):
    img, rng = new_image(env)
    img.u32(GAME_MODE, rng.choice([1, 1, 1, 2, 3, 7]))
    img.u32(STATUS, rng.choice([0xE, 0xE, 0xE, 0xD, 0]))
    career_cash(env, img, rng, rng.randrange(0, 21))
    return [], img.pokes()


@fscr(0x0044B320, "Front_DemoModeBlink")
def gen_blink(env, n):
    img, rng = new_image(env)
    img.u32(0x0052BF3C, rng.choice([1, 2, 0xD, 0xE, 0x30, 0x20, 0, 0xFFFFFFFF]))
    img.u32(PLAYER_COUNT, rng.choice([1, 2, 2]))
    return [], img.pokes()


@fscr(0x0044B3A0, "Front_DisplayScreen")
def gen_display_screen(env, n):
    img, rng = new_image(env)
    img.u32(TICK, rng.randrange(1, 100000))
    return [string_at(env, rng.randrange(0, 20))], img.pokes()


def horse_world(env, img, rng, *, restarts=True):
    count = rng.choice([0, 1, 2, 3, 5, 8]) if restarts else rng.randrange(1, 6)
    img.u32(0x0056E208, count)
    for k in range(20):
        img.u32(0x0056E1B8 + 4 * k, string_at(env, rng.randrange(0, 20)))
    img.u32(0x0056867C, rng.randrange(0, 8))
    word = rng.choice([b"HORSE", b"A", b"SKATE", b"BIG", b"ELEPHANT"])
    img.put(HORSE_WORD, word + b"\0" + bytes(8 - len(word) % 8))
    img.u32(HORSE_LENGTH, len(word))
    return word


@fscr(0x0044B470, "RestartCompFrig")
def gen_restart(env, n):
    img, rng = new_image(env)
    img.u32(GAME_MODE, rng.choice([7, 7, 1, 2, 3]))
    horse_world(env, img, rng)
    for address in (0x00561730, 0x0056A854, 0x00568680, 0x00568660, 0x00568630, 0x00568A60, 0x00568A64, 0x00568A20, 0x0056A8C0):
        img.u32(address, rng.choice([0, 1, 5, 0x123456]))
    img.u8(0x00561BE4, rng.choice([0, 1]))
    img.u32(0x00568814, rng.choice([0, 1]))
    return [], img.pokes()


@fscr(0x0044B570, "Horse_DisplayWords")
def gen_horse_words(env, n):
    img, rng = new_image(env)
    word = horse_world(env, img, rng)
    counts = menu_at(env, 10) + 0x100
    img.u32(counts, rng.randrange(0, len(word) + 2))
    img.u32(counts + 4, rng.randrange(0, len(word) + 2))
    return [counts], img.pokes()


@fscr(0x0044BDE0, "Front_ClearScreen")
def gen_clear(env, n):
    img, rng = new_image(env)
    img.u32(0x0056E310, rng.choice([0, 1, 7]))
    return [], img.pokes()


def world_for_init(env, img, rng):
    img.u32(GAME_MODE, rng.choice([1, 1, 2, 3, 4, 7, 9, 10]))
    img.u32(CURRENT_LEVEL, rng.randrange(0, 14))
    img.u32(0x00561C90, rng.randrange(0, 14))
    img.u8(0x00560080, rng.choice([0, 0, 1]))
    img.u8(0x00561CA8, rng.choice([0, 1]))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u8(0x006A3B21, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A2E8, rng.randrange(0, 12))
    img.u32(0x0056A2EC, rng.randrange(0, 12))
    for address in MENU_VARS:
        img.u32(address, rng.choice([0, 0, 0x123456]))


@fscr(0x0044BE50, "Front_Init")
def gen_init(env, n):
    img, rng = new_image(env)
    world_for_init(env, img, rng)
    return [], img.pokes()


def delete_world(env, img, rng):
    """Several menus (real CMenu objects) or nothing in the nine variables Front_DeleteMenus clears."""
    taken = 0
    for variable in (0x00561C1C, 0x00561C38, 0x00561C20, 0x00561C24, 0x00561C28, 0x00561C2C, 0x00561C30, 0x00561C34, 0x00561C3C):
        if rng.random() < 0.6:
            base, _ = menu_image(env, img, rng, taken, count=rng.randrange(0, 6))
            img.u32(variable, base)
            taken += 1
        else:
            img.u32(variable, 0)


@fscr(0x0044C650, "Front_DeleteMenu")
def gen_delete_menu(env, n):
    img, rng = new_image(env)
    delete_world(env, img, rng)
    return [rng.choice([0x00561C1C, 0x00561C20, 0x00561C3C, 0x00561C38])], img.pokes()


@fscr(0x0044C670, "Front_DeleteMenus")
def gen_delete_menus(env, n):
    img, rng = new_image(env)
    delete_world(env, img, rng)
    return [], img.pokes()


@fscr(0x0044C6E0, "Front_HintNone")
def gen_hint_none(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@fscr(0x0044C6F0, "Front_HintOne")
def gen_hint_one(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@fscr(0x0044C700, "Front_HintText")
def gen_hint_text(env, n):
    img, rng = new_image(env)
    return [string_at(env, rng.randrange(0, 20)), rng.choice([0, 1])], img.pokes()


@fscr(0x0044C710, "Front_PrintGoBack")
def gen_go_back(env, n):
    img, rng = new_image(env)
    player = player_pointer(env)
    img.u32(TICK, rng.randrange(0, 100000))
    img.u32(player + 0x2CC4, rng.randrange(0, 4))
    img.u32(player + 0x2CDC, rng.randrange(0, 100))
    img.u32(player + 0x2CE0, rng.randrange(0, 100))
    return [player, rng.choice([0x1E, 0x11E, 0x20, -4 & 0xFFFFFFFF]), rng.choice([0x56, 0x80, 0x100]), rng.randrange(0, 256), rng.randrange(0, 256), rng.randrange(0, 256),
            rng.randrange(0, 50), rng.randrange(0, 50), rng.randrange(0, 50), rng.randrange(0, 50), rng.choice([1, 0, 1, 0, -1 & 0xFFFFFFFF, 2])], img.pokes()
