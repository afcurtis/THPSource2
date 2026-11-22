"""Phase 4.10e2 case generators, part 1: the skater select / load / save screen (cresel.cpp, 0x0042cd20..0x0042f2f0).

The screen is built the way the create-a-skater screens are (frontend_specs_screens.py: the slab, the seeds, the stubs, the fake objects; the
generators of the other groups are in frontend_specs_screens_cases.py): a window whose children, prompt windows and dialogs are fake objects whose
virtual methods are reported services, the toolkit's lists and rows either fake (the lists) or real (the rows: their text is set by the toolkit's
own method).  Groups: crsel (the functions) and crselloop (the screen loop and its callbacks; the per-frame calls are seams).
"""

from __future__ import annotations

import os

from create_specs import READ_ONLY
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props, props_at
from frontend_specs_screens import OBJ, TICK, install_screen_stubs, register
from frontend_specs_screens_cases import (Pool, common, confirm_state, font_object, gspec, window)
from frontend_specs_skstat import text_element
from frontend_specs_skwin import A, item_row, list_object, new_image

# the strings the screen reads beyond the 4.10e1 set: the formats and file names around 0x527c10, "%s" at 0x5256dc
STATIC_EXTRA = [(0x00527C10, 0x150), (0x005256D0, 0x20)]
for _group in ("crsel", "crselloop"):
    register(_group)
    READ_ONLY[_group] = list(READ_ONLY[_group]) + STATIC_EXTRA

SEL = gspec("crsel")
LOOP = gspec("crselloop")

# the fields of the select window (see create_screens.hpp)
BACKGROUND, FONT_A, FONT_B, FONT_C = 0xAC, 0xB0, 0x1FC, 0x348
LIST_A, ROWS_A, LIST_B, ROWS_B = 0x494, 0x498, 0x618, 0x61C
STYLE_A, STYLE_B, STYLE_C, ARROW_A, ARROW_B = 0x79C, 0x7A0, 0x7A4, 0x7A8, 0x7AC
STATE, NEXT_STATE, DIALOG, WARNINGS, IMAGE, PROMPTS = 0x7B0, 0x7B4, 0x7B8, 0x7BC, 0x7CC, 0x7D0
SIZE = 0x7E4

PAD = {"up": 0x0056B099, "up2": 0x0056B0A9, "confirm": 0x0056B029, "square": 0x0056B009, "triangle": 0x0056B019, "back": 0x0056AFF9}

# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------
# (address, stack arguments, thiscall) of the engine functions the screen reaches beyond the 4.10e1 set
SELECT_ENGINE = [(0x0047A6C0, 3, False), (0x004C8F10, 2, False), (0x00422F20, 3, False), (0x0042EF30, 0, True), (0x0042EF50, 2, True)]


def install_select_stubs(fc, env):
    if getattr(fc, "_select_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._select_installed = True
    install_screen_stubs(fc, env)
    fc.stub(0x0046F2E0, 3, "mem_copy")
    for address, count, thiscall in SELECT_ENGINE:
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall)


def install_select_loop_stubs(fc, env):
    """The loop: the clock advances at every Game_FrameTick, and the window's step, update, draw and delete are seams with scripted answers."""
    from opentony.memory import mem

    rng = env.rng
    install_select_stubs(fc, env)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    step, update, draw, delete = 0x0042E9C0, 0x0042E180, 0x0042DF60, 0x0042DDA0
    fc.stub(step, 0, "engine_%08x" % step, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2]))
    fc.stub(update, 0, "engine_%08x" % update, convention="thiscall", this=True)
    fc.stub(draw, 2, "engine_%08x" % draw, convention="thiscall", this=True)
    fc.stub(delete, 1, "engine_%08x" % delete, convention="thiscall", this=True)


GROUP_STUBS = {"crsel": install_select_stubs, "crselloop": install_select_loop_stubs}


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def props_ready(env, img, rng):
    """The four MenuProperties the real text rows point at hold legal style words."""
    for i in range(4):
        poke_props(env, img, i, rng)


def pad_edges(img, rng, *, only=None):
    """The six pad edges the screen reads (pad 0). `only` names the one that is set (the others stay clear half the time)."""
    for name, address in PAD.items():
        if only is None:
            value = rng.choice([0, 0, 1])
        elif name == only:
            value = 1
        else:
            value = 1 if rng.random() < 0.15 else 0
        img.u8(address, value)


def select_state(env, img, rng, base, pool, *, state=None, next_state=None, destroy=False, dialog="none", scaled_title=True):
    """A select window: two lists with their rows (real text rows; the first row of each list is its title), fake styles, arrows, warning lines, prompt
    windows, a dialog (none, a real confirm box, or a fake object) and the state.  Returns the dict of the rows and the highlighted ids."""
    props_ready(env, img, rng)
    chosen = rng.choice([0, 0, 0, 3, 3, 3, 1, 2, 4, 5, 6, 0, 3, 9]) if state is None else state
    # the states that follow a choice of slot have one highlighted (the back row is not a slot)
    need_slot = chosen in (1, 3, 4, 5)
    window(env, img, rng, base, SIZE, pool, children=rng.randrange(0, 3))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + BACKGROUND, keep(pool.take()))
    for slot in (FONT_A, FONT_B, FONT_C):
        font_object(env, img, rng, base + slot)
    info = {}
    slot_no = 2
    for name, list_index, list_slot, rows_slot in (("a", 0, LIST_A, ROWS_A), ("b", 1, LIST_B, ROWS_B)):
        ids = [-1] + [rng.choice([0, 1, 2, 3]) for _ in range(rng.randrange(1, 4))] + [-3]
        if rng.random() < 0.3:
            ids = [rng.choice([0, 1, 2, 3] if need_slot and name == "a" else [0, 1, 2, 3, -3])]
        rows = []
        for ident in ids:
            vtable = MENU_REAL["scaled"] if ident == -1 and scaled_title else MENU_REAL["text"]
            rows.append(item_row(env, img, rng, slot_no, ident & 0xFFFFFFFF, real_text=True, vtable=vtable, selectable=1))
            slot_no += 1
        pickable = [i for i, ident in enumerate(ids) if ident in ((0, 1, 2, 3) if need_slot and name == "a" else (0, 1, 2, 3, -3))]
        pick = rng.choice(pickable) if pickable else 0
        list_at = list_object(env, img, rng, list_index, rows, highlighted=pick)
        img.u32(base + list_slot, keep(list_at))
        for i in range(0x60):
            img.u32(base + rows_slot + 4 * i, rows[i] if i < len(rows) else 0)
        info[name] = {"ids": ids, "pick": pick}
    # the styles are real MenuProperties (the update ellipsizes a row's text with the first one's font); the arrows are fake objects
    for i, slot in enumerate((STYLE_A, STYLE_B, STYLE_C)):
        img.u32(base + slot, keep(pool.take()) if destroy else props_at(env, i))
    for slot in (ARROW_A, ARROW_B):
        img.u32(base + slot, keep(pool.take()))
    for i in range(3):
        img.u32(base + WARNINGS + 4 * i, pool.take())
    img.u32(base + WARNINGS + 12, text_element(env, img, rng, 12))
    for i in range(5):
        img.u32(base + PROMPTS + 4 * i, pool.take())
    img.u32(base + IMAGE, pool.take())
    img.u32(base + STATE, chosen)
    img.u32(base + NEXT_STATE, chosen if next_state is None else next_state)
    box = A(env, OBJ[1])
    if dialog == "real":
        confirm_state(env, img, rng, box, pool, complete=not destroy)
        img.u32(base + DIALOG, box)
    elif dialog == "fake":
        img.u32(base + DIALOG, pool.take())
    else:
        img.u32(base + DIALOG, 0)
    info["state"] = chosen
    return info


# ---- the callback record and the window ------------------------------------------------------------------------------------------------------

@SEL(0x0042CD20, "CreateSelect_InitCallbacks")
def gen_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@SEL(0x0042CD40, "CreateSelect_ExitCallback")
def gen_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@SEL(0x0042CD60, "CreateSelect_Construct", this=OBJ[0])
def gen_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), SIZE, rng)
    return [], img.pokes()


@SEL(0x0042DDA0, "CreateSelect_ScalarDelete", this=OBJ[0])
def gen_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True, dialog=rng.choice(["none", "real", "fake"]))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SEL(0x0042DDC0, "CreateSelect_Destroy", this=OBJ[0])
def gen_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True, dialog=rng.choice(["none", "real", "fake"]))
    return [], img.pokes()


@SEL(0x0042DF60, "CreateSelect_Draw", this=OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    state = rng.choice([0, 1, 2, 3, 4, 5, 6, 9, 1, 4])
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state, dialog="real" if state in (1, 4, 5) or rng.random() < 0.2 else "none")
    return [coords(rng), coords(rng)], img.pokes()


@SEL(0x0042E180, "CreateSelect_UpdateFrame", this=OBJ[0])
def gen_update_frame(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    state = rng.choice([0, 0, 3, 3, 1, 2, 4, 5, 6, 9, 4, 5])
    nxt = state if rng.random() < 0.7 else rng.choice([0, 1, 2, 3, 4, 5, 6])
    # the dialog states need a dialog (retail reads through it); the state the frame enters decides
    entered = nxt
    pad_edges(img, rng)
    info = select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state, next_state=nxt,
                        dialog="real" if entered in (1, 4, 5) else rng.choice(["none", "none", "real"]))
    if os.environ.get("P410_DEBUG"):
        with open(os.environ["P410_DEBUG"], "a") as fh:
            fh.write("update_frame #%d state=%d next=%d info=%r" % (n, state, nxt, info) + chr(10))
    return [], img.pokes()


@SEL(0x0042E7D0, "CreateSelect_UpdateLoad", this=OBJ[0])
def gen_update_load(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=2 if rng.random() < 0.85 else rng.choice([0, 6]), dialog="none")
    return [], img.pokes()


@SEL(0x0042E860, "CreateSelect_UpdateSave", this=OBJ[0])
def gen_update_save(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=6 if rng.random() < 0.85 else rng.choice([0, 2]),
                 dialog=rng.choice(["none", "none", "none", "real"]))
    return [], img.pokes()


@SEL(0x0042E9C0, "CreateSelect_Update", this=OBJ[0])
def gen_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    state = rng.choice([0, 0, 0, 0, 3, 3, 3, 2, 6, 1, 4, 5, 9])
    edge = rng.choice(["confirm", "square", "triangle", "back", "confirm", "square", "triangle", None])
    pad_edges(img, rng, only=edge)
    select_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state, dialog="real" if rng.random() < 0.1 else "none")
    return [], img.pokes()


# ---- the screen loop and its callback ---------------------------------------------------------------------------------------------------------

@LOOP(0x0042CD50, "CreateSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x0042F100, "CreateSelect_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    return [], img.pokes()
