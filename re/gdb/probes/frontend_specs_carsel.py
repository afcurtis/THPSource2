"""Phase 4.10f3 case generators: the career select screen (carsel.cpp, 0x004173d0..0x00418150), built the way the skater select screen's probe is
(frontend_specs_select.py: the 4.10e1 slab, fake pool objects whose virtual methods are reported services, the toolkit's real text rows and list
object).  Groups: carwin (the window's functions and the two front-end helpers) and carloop (the screen loop and its callbacks; the per-frame calls
are seams).

The window (0x4dc bytes, vtable 0x00518508): +0xac the background, +0xb0 and +0x1fc two Font objects, +0x348 the list, +0x34c its style, +0x350 the
rows (0x60 slots: Continue career id 3, New career id 0, Load career id 1), +0x4d0 and +0x4d4 the arrows, +0x4d8 the state (-1 choosing, 1 loading
from the card, 2 saving to it).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props, props_at
from frontend_specs_screens import OBJ, TICK, install_screen_stubs, regions, register
from frontend_specs_screens_cases import Pool, common, font_object, gspec, window
from frontend_specs_skwin import A, item_row, list_object, new_image

# the pointer variables the rows' texts are read through (0x0052e6ac..) and the PRE file name
STATIC_EXTRA = [(0x0052E6A0, 0x20), (0x005254F0, 0x10), (0x00525400, 0x80)]
# the words the screen writes: the new-career flag and the front end's sound voice (0x0052cb38..0x0052cb48), the sound's scale and pan (0x0052be40..0x0052be50)
# and the pan's current and target positions
WORDS = [(0x0052BE40, 0x10), (0x0052CB38, 0x10), (0x00561C84, 4), (0x00561C94, 4)]


def car_regions(slab):
    return regions(slab) + WORDS


for _group in ("carwin", "carloop"):
    register(_group)
    EXTRA_REGIONS[_group] = car_regions
    READ_ONLY[_group] = list(READ_ONLY[_group]) + STATIC_EXTRA

WIN = gspec("carwin")
LOOP = gspec("carloop")

BACKGROUND, FONT_A, FONT_B = 0xAC, 0xB0, 0x1FC
LIST, STYLE, ROWS, ARROW_A, ARROW_B, STATE = 0x348, 0x34C, 0x350, 0x4D0, 0x4D4, 0x4D8
SIZE = 0x4DC

PAD = {"up": 0x0056B099, "down": 0x0056B0A9, "confirm": 0x0056B029, "back": 0x0056AFF9}

# the career select screen's globals and the front end's sound state
NEW_CAREER_FLAG = 0x0052CB3D
SOUND_VOICE = 0x0052CB38
SOUND_CURRENT = 0x00561C84
SOUND_TARGET = 0x00561C94
SOUND_SCALE = 0x0052BE48
SOUND_PAN = 0x0052BE44


# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------
# (address, stack arguments, thiscall) of the engine functions the screen reaches beyond the 4.10e1 set
CAR_ENGINE = [(0x0047A6C0, 3, False), (0x004138F0, 0, False), (0x004AD9F0, 1, False), (0x004AD950, 3, False)]


def install_car_stubs(fc, env):
    if getattr(fc, "_car_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._car_installed = True
    install_screen_stubs(fc, env)
    for address, count, thiscall in CAR_ENGINE:
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall)


def install_car_loop_stubs(fc, env):
    """The loop: the clock advances at every Game_FrameTick, and the window's step (the poll), update, draw and delete are seams with scripted answers."""
    from opentony.memory import mem

    rng = env.rng
    install_car_stubs(fc, env)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    step, update, draw, delete = 0x00417E30, 0x00417C90, 0x00417BF0, 0x00417AA0
    fc.stub(step, 0, "engine_%08x" % step, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2]))
    fc.stub(update, 0, "engine_%08x" % update, convention="thiscall", this=True)
    fc.stub(draw, 2, "engine_%08x" % draw, convention="thiscall", this=True)
    fc.stub(delete, 1, "engine_%08x" % delete, convention="thiscall", this=True)


def install_car_win_stubs(fc, env):
    from frontend_specs_playsel import disable

    install_car_stubs(fc, env)
    # the front end's sound update (0x00418150) is a seam in every screen's loop but the function under test here; and the sound update stops its voice through
    # 0x004ad9f0, which the menu group stubs as an item's highlight (thiscall): one stub per address, so that one is off (no virtual call of this group reaches it)
    disable(fc, "engine_00418150", "item_highlight")


GROUP_STUBS = {"carwin": install_car_win_stubs, "carloop": install_car_loop_stubs}


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def pad_edges(img, rng, *, only=None):
    """The four pad edges the screen reads (pad 0). `only` names the one that is set (the others stay clear most of the time)."""
    for name, address in PAD.items():
        if only is None:
            value = rng.choice([0, 0, 1])
        elif name == only:
            value = 1
        else:
            value = 1 if rng.random() < 0.15 else 0
        img.u8(address, value)


def career_state(env, img, rng, base, pool, *, state=None, destroy=False, highlighted=None, ids=None):
    """A career select window: the list with its rows (real text rows), fake style, arrows and background, the state. Returns the ids and the pick."""
    for i in range(4):
        poke_props(env, img, i, rng)
    window(env, img, rng, base, SIZE, pool, children=rng.randrange(0, 3))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + BACKGROUND, keep(pool.take()))
    for slot in (FONT_A, FONT_B):
        font_object(env, img, rng, base + slot)
    if ids is None:
        ids = rng.choice([[3, 0, 1], [3, 0, 1], [0, 1], [3], [1, 3, 0], [3, 0, 1, 2], [0, 1, 4]])
    rows = []
    for slot_no, ident in enumerate(ids, start=2):
        rows.append(item_row(env, img, rng, slot_no, ident & 0xFFFFFFFF, real_text=True, vtable=MENU_REAL["text"], selectable=1))
    pick = rng.randrange(len(ids)) if highlighted is None else highlighted
    img.u32(base + LIST, keep(list_object(env, img, rng, 0, rows, highlighted=pick)))
    img.u32(base + STYLE, keep(pool.take()) if destroy else props_at(env, 0))
    for i in range(0x60):
        img.u32(base + ROWS + 4 * i, rows[i] if i < len(rows) else 0)
    img.u32(base + ARROW_A, keep(pool.take()))
    img.u32(base + ARROW_B, keep(pool.take()))
    chosen = rng.choice([-1, -1, -1, 1, 2]) if state is None else state
    img.u32(base + STATE, chosen & 0xFFFFFFFF)
    return {"ids": ids, "pick": pick, "state": chosen}


# ---- the callback record, the window and its methods -----------------------------------------------------------------------------------------

@WIN(0x004173D0, "CareerSelect_InitCallbacks")
def gen_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@WIN(0x004173F0, "CareerSelect_ExitCallback")
def gen_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@WIN(0x00417410, "CareerSelect_Construct", this=OBJ[0])
def gen_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), SIZE, rng)
    img.u8(NEW_CAREER_FLAG, rng.choice([0, 1]))         # (a career in progress greys the first row)
    return [], img.pokes()


@WIN(0x00417AA0, "CareerSelect_ScalarDelete", this=OBJ[0])
def gen_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@WIN(0x00417AC0, "CareerSelect_Destroy", this=OBJ[0])
def gen_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@WIN(0x00417BF0, "CareerSelect_Draw", this=OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=rng.choice([-1, -1, 1, 2, 0, 3]))
    return [coords(rng), coords(rng)], img.pokes()


@WIN(0x00417C90, "CareerSelect_Update", this=OBJ[0])
def gen_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=rng.choice([-1, -1, -1, 1, 2, 0]))
    return [], img.pokes()


@WIN(0x00417CE0, "CareerSelect_UpdateSave", this=OBJ[0])
def gen_update_save(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=2 if rng.random() < 0.85 else rng.choice([-1, 1]))
    return [], img.pokes()


@WIN(0x00417D70, "CareerSelect_UpdateLoad", this=OBJ[0])
def gen_update_load(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=1 if rng.random() < 0.85 else rng.choice([-1, 2]))
    return [], img.pokes()


@WIN(0x00417E30, "CareerSelect_Poll", this=OBJ[0])
def gen_poll(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    state = rng.choice([-1, -1, -1, -1, -1, 1, 2, 5, 0])
    edge = rng.choice(["confirm", "confirm", "confirm", "back", "up", "down", None])
    pad_edges(img, rng, only=edge)
    # (a choice of 0..3 takes its branch; the others, and a list whose highlighted id is not a choice, reach the assertion)
    ids = rng.choice([[3, 0, 1], [3, 0, 1], [3, 0, 1, 2], [2], [1, 2, 0], [0, 1, 4], [5], [1]])
    img.u8(NEW_CAREER_FLAG, rng.choice([0, 1]))
    career_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state, ids=ids)
    return [], img.pokes()


# ---- the front end's shared sound update and the key table -----------------------------------------------------------------------------------

@WIN(0x00418140, "Front_KeyTable")
def gen_key_table(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@WIN(0x00418150, "Front_UpdateMenuSound")
def gen_sound(env, n):
    img, rng = new_image(env)
    voice = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 0x40)])
    img.u32(SOUND_VOICE, voice)
    target = rng.choice([0, 0x88, 0x3FFF, rng.randrange(0, 0x4000)])
    current = rng.choice([target, target, rng.randrange(0, 0x4000), max(0, target - 0x40), target + 0x40, target - 0x88, target + 0x88])
    img.u32(SOUND_TARGET, target & 0xFFFFFFFF)
    img.u32(SOUND_CURRENT, current & 0xFFFFFFFF)
    img.u32(SOUND_SCALE, rng.choice([0, 0x7F, 0xFF, 0x100, rng.randrange(0, 0x10000), 0xFFFFFFFF]))
    img.u32(SOUND_PAN, rng.randrange(0, 0x4000))
    return [], img.pokes()


# ---- the screen loop and its callback ---------------------------------------------------------------------------------------------------------

@LOOP(0x00417400, "CareerSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x00417F90, "CareerSelect_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    return [], img.pokes()
