"""Phase 4.10f3 case generators: the multi-player select screen (multisel.cpp, 0x0047ba30..0x0047c560), built the way the career select screen's probe is
(frontend_specs_carsel.py).  Groups: mulwin (the window's functions and the callback record) and mulloop (the screen loop and its callback; the poll and the
window's methods are seams).

The window (0x238 bytes, vtable 0x00519328): +0xac the background, +0xb0 a Font, +0x1fc the list, +0x200 and +0x204 two styles, +0x208 ten row slots (the scaled
title, then five games: Trick attack 5, Graffiti 8, Horse 7, Tag 9, Free Skate 4), +0x230 and +0x234 the arrows.  The answer goes to the global 0x00561c6c.
The loop's frame waits for the clock with a bare spin that a forced call cannot satisfy, so it is only replayed on a first frame that ends it (the poll answers
1 or 2).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props, props_at
from frontend_specs_screens import OBJ, TICK, install_screen_stubs, regions, register
from frontend_specs_screens_cases import Pool, common, font_object, gspec, window
from frontend_specs_skwin import A, item_row, list_object, new_image

ANSWER = 0x00561C6C
STATIC_EXTRA = [(0x005358B4, 0x20), (0x00525400, 0x80), (0x0052D850, 0x30)]


def mul_regions(slab):
    return regions(slab) + [(ANSWER, 4)]


for _group in ("mulwin", "mulloop"):
    register(_group)
    EXTRA_REGIONS[_group] = mul_regions
    READ_ONLY[_group] = list(READ_ONLY[_group]) + STATIC_EXTRA

WIN = gspec("mulwin")
LOOP = gspec("mulloop")

BACKGROUND, FONT, LIST, STYLE, TITLE_STYLE, ROWS, ARROW_A, ARROW_B = 0xAC, 0xB0, 0x1FC, 0x200, 0x204, 0x208, 0x230, 0x234
SIZE = 0x238

PAD_STRIDE = 0x16C
MOVES = [0x0056B099, 0x0056B0A9]
CONFIRM = 0x0056B029
BACK = 0x0056AFF9


# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------

def install_mul_stubs(fc, env):
    if getattr(fc, "_mul_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._mul_installed = True
    rng = env.rng
    install_screen_stubs(fc, env)
    fc.stub(0x0046F650, 0, "engine_0046f650", result=lambda a: rng.choice([-1, 0, 1, 2, 3]))


def install_mul_loop_stubs(fc, env):
    """The loop: the clock advances at every Game_FrameTick; the poll answers 1 or 2 (the frame that follows a 0 waits on the clock) and the window's selected id,
    update, draw and delete are seams."""
    from opentony.memory import mem

    rng = env.rng
    install_mul_stubs(fc, env)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    fc.stub(0x0047C4A0, 0, "engine_0047c4a0", result=lambda a: rng.choice([1, 1, 2]))
    fc.stub(0x0047C550, 0, "engine_0047c550", convention="thiscall", this=True, result=lambda a: rng.choice([4, 5, 7, 8, 9]))
    fc.stub(0x0047C460, 0, "engine_0047c460", convention="thiscall", this=True)
    fc.stub(0x0047C3E0, 2, "engine_0047c3e0", convention="thiscall", this=True)
    fc.stub(0x0047C290, 1, "engine_0047c290", convention="thiscall", this=True)


GROUP_STUBS = {"mulwin": install_mul_stubs, "mulloop": install_mul_loop_stubs}


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def pad_edges(img, rng, *, only=None):
    """The edges the poll reads, on both pads. `only` names the group that is set ("move", "confirm", "back"); the others stay clear mostly."""
    groups = {"move": MOVES, "confirm": [CONFIRM], "back": [BACK]}
    for name, addresses in groups.items():
        for address in addresses:
            for player in range(2):
                value = 1 if (only == name and rng.random() < 0.6) else (1 if rng.random() < (0.3 if only is None else 0.08) else 0)
                img.u8(address + PAD_STRIDE * player, value)


def multi_state(env, img, rng, base, pool, *, destroy=False, highlighted=None, ids=None):
    """A multi-player select window: the list with its rows (real text rows; the first is the scaled title), fake styles and arrows and background."""
    for i in range(4):
        poke_props(env, img, i, rng)
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + BACKGROUND, keep(pool.take()))
    font_object(env, img, rng, base + FONT)
    ids = [5, 8, 7, 9, 4] if ids is None else ids
    rows = [item_row(env, img, rng, 2, 0xFFFFFFFF, real_text=True, vtable=MENU_REAL["scaled"], selectable=1)]
    for slot_no, ident in enumerate(ids, start=3):
        rows.append(item_row(env, img, rng, slot_no, ident & 0xFFFFFFFF, real_text=True, vtable=MENU_REAL["text"], selectable=1))
    pick = rng.randrange(1, len(rows)) if highlighted is None else highlighted
    img.u32(base + LIST, keep(list_object(env, img, rng, 0, rows, highlighted=pick)))
    for slot in (STYLE, TITLE_STYLE):
        img.u32(base + slot, keep(pool.take()))
    for i in range(10):
        img.u32(base + ROWS + 4 * i, rows[i] if i < len(rows) else 0)
    for slot in (ARROW_A, ARROW_B):
        img.u32(base + slot, keep(pool.take()))
    return {"ids": ids, "pick": pick}


# ---- the callback record and the window -----------------------------------------------------------------------------------------------------

@WIN(0x0047BA30, "MultiSelect_InitCallbacks")
def gen_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@WIN(0x0047BA50, "MultiSelect_ExitCallback")
def gen_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@WIN(0x0047BA70, "MultiSelect_Construct", this=OBJ[0])
def gen_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), SIZE, rng)
    return [], img.pokes()


@WIN(0x0047C290, "MultiSelect_ScalarDelete", this=OBJ[0])
def gen_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    multi_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@WIN(0x0047C2B0, "MultiSelect_Destroy", this=OBJ[0])
def gen_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    multi_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@WIN(0x0047C3E0, "MultiSelect_Draw", this=OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    multi_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@WIN(0x0047C460, "MultiSelect_Update", this=OBJ[0])
def gen_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    multi_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@WIN(0x0047C4A0, "MultiSelect_Poll")
def gen_poll(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pad_edges(img, rng, only=rng.choice(["move", "confirm", "back", "confirm", None]))
    return [], img.pokes()


@WIN(0x0047C550, "MultiSelect_SelectedId", this=OBJ[0])
def gen_selected(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    multi_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), ids=rng.choice([[5, 8, 7, 9, 4], [4, 4, 5, 9, 7], [0, 1, 2, 3, 6]]))
    return [], img.pokes()


# ---- the screen loop and its callback ---------------------------------------------------------------------------------------------------------

@LOOP(0x0047BA60, "MultiSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x0047C560, "MultiSelect_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(ANSWER, rng.getrandbits(32))
    return [], img.pokes()
