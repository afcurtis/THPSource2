"""Phase 4.10f case generators, part 3: the player-select window (playsel.cpp 0x004a2050..0x004a36e0, 0x004a5f50..0x004a5f60) and the small methods of the
classes it calls (the skater view's selection, the header, the menu window, the load window).

The window is built by hand: fake page vtable, three real Font objects, fake pool objects for the background, the prompts, the dots, the bio and medal windows;
the skater view, the menu window and the load window are slab objects (a vtable VT_ANY whose slot 4 is the view's reported load method, and the few fields the
window's code reads).  The constructors the initialiser calls are seams (stubbed, handing back the arena block just allocated).
"""

from __future__ import annotations

from frontend_specs_menu import coords
from frontend_specs_playsel import (EXIT_CODE, PA, PS_OBJ, REPLAY_STATE, ROWS, START_IN_MENU, PLAYER_COUNT, SELECTION)
from frontend_specs_playsel_cases import obj, pspec
from frontend_specs_screens import VT_ANY
from frontend_specs_screens_cases import Pool, common, font_object, font_ptr, read_u32, window
from frontend_specs_skstat import text_element
from frontend_specs_skwin import VT_WINDOW, item_row, list_object, new_image

S = pspec("sel")
L = pspec("selloop")

# the select window's fields (player_select.hpp)
STATE, BACKGROUND, FONT_A, FONT_B, FONT_C = 0xAC, 0xB0, 0xB4, 0x200, 0x34C
TWO_PLAYER, PLAYERS, VIEW, MEDAL, BIO, HEADER, MENU, NAME, LOAD, PROMPTS, DOTS = 0x498, 0x49C, 0x4A0, 0x4A4, 0x4A8, 0x4AC, 0x4B0, 0x4B4, 0x4B8, 0x4BC, 0x4D8
SIZE = 0x4DC

PAD_LEFT, PAD_RIGHT, PAD_CONFIRM, PAD_BACK, PAD_SPIN_LEFT, PAD_SPIN_RIGHT = 0x0056B079, 0x0056B089, 0x0056B029, 0x0056AFF9, 0x0056B008, 0x0056B018
PAD_STRIDE = 0x16C
MODES = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0]


def select_globals(env, img, rng, *, active=None):
    """The state every select-window case needs: the pads, the skater manager (the active player's slot), the character rows' availability, the globals."""
    player = rng.choice([0, 0, 1, 1]) if active is None else active
    common(env, img, rng, player=player)
    # the player who is choosing is `player` (common made that player's manager slot the active one)
    img.u32(0x0056A888, player)
    available = [rng.choice([0, 1, 1, 1]) for _ in range(20)]
    for forced in rng.sample([i for i in range(20) if i != 0x10], 3):
        available[forced] = 1
    for character in range(20):
        img.u32(ROWS + 0x170 * character + 0x158, available[character])
    img.u32(REPLAY_STATE, rng.choice([0, 1, 2, 3, 5, 7, 10, 7]))
    img.u32(EXIT_CODE, rng.choice([0, 2, 3, 12, 15, 16]))
    img.u32(PLAYER_COUNT, rng.choice([1, 2, 2]))
    img.u32(START_IN_MENU, rng.choice([0, 0, 1]))
    img.u32(SELECTION, rng.choice([0, 1, 2]))
    return player


def pad_edges(img, rng, player, *, only=None):
    """The edges the window listens to (the active player's pad): `only` names the one that is set, the others are set now and then."""
    names = {"left": PAD_LEFT, "right": PAD_RIGHT, "confirm": PAD_CONFIRM, "back": PAD_BACK, "spin_left": PAD_SPIN_LEFT, "spin_right": PAD_SPIN_RIGHT}
    for name, address in names.items():
        value = 1 if name == only else (1 if (only is None and rng.random() < 0.3) or rng.random() < 0.1 else 0)
        img.u8(address + PAD_STRIDE * player, value)


def view_object(env, img, rng, pool, *, selected=None, custom_flag=None):
    base = obj(env, 1)
    img.u32(base, PA(env, VT_ANY))
    img.u32(base + 0xAC, pool.take())
    img.u32(base + 0xB0, rng.getrandbits(32))
    img.u32(base + 0x890, rng.choice([0, 1, 5, 9, 12, 14, 19, 20, 20, 3, 16]) if selected is None else selected)
    img.u32(base + 0x1378, PA(env, 0x24800))
    img.u32(base + 0x138C, rng.choice([0, 0, 1]) if custom_flag is None else custom_flag)
    return base


def menu_object(env, img, rng, pool, *, choice=None):
    base = obj(env, 2)
    img.u32(base, PA(env, VT_ANY))
    rows = [item_row(env, img, rng, i, ident, selectable=1) for i, ident in enumerate([0, 1, 2, 3])]
    img.u32(base + 0xAC, list_object(env, img, rng, 0, rows, highlighted=rng.randrange(0, 4)))
    img.u32(base + 0x234, rng.choice([0, 0, 1, 2, 3, 4, 5, 6, 7]) if choice is None else choice)
    return base


def load_object(env, img, rng, *, state=None):
    base = obj(env, 3)
    img.u32(base, PA(env, VT_ANY))
    img.u32(base + 0x14, rng.choice([0, 1]))
    img.u32(base + 0xAC, rng.choice([0, 1, 2]) if state is None else state)
    return base


def select_window(env, img, rng, pool, *, state=None, destroy=False, header=True, view=None, menu=None, load=None, mode_two=None):
    """A select window in a legal state; returns its address."""
    base = obj(env, 0)
    window(env, img, rng, base, SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for slot in (FONT_A, FONT_B, FONT_C):
        font_object(env, img, rng, base + slot)
    img.u32(base + STATE, rng.choice([0, 0, 1, 2]) if state is None else state)
    img.u32(base + BACKGROUND, keep(pool.take()))
    img.u32(base + TWO_PLAYER, rng.choice([0, 1]) if mode_two is None else mode_two)
    img.u32(base + PLAYERS, rng.choice([1, 2, 2, 3]))
    img.u32(base + VIEW, view if view is not None else view_object(env, img, rng, pool))
    img.u32(base + MEDAL, pool.take())
    img.u32(base + BIO, pool.take())
    img.u32(base + HEADER, pool.take() if header else 0)
    img.u32(base + MENU, menu if menu is not None else menu_object(env, img, rng, pool))
    img.u32(base + NAME, text_element(env, img, rng, 12))
    img.u32(base + LOAD, load if load is not None else load_object(env, img, rng))
    for i in range(7):
        img.u32(base + PROMPTS + 4 * i, pool.take())
    img.u32(base + DOTS, pool.take())
    return base


# ---- the window --------------------------------------------------------------------------------------------------------------------------------

@S(0x004A2050, "PlayerSelectWindow_Construct", this=PS_OBJ[0])
def gen_construct(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    img.garbage(obj(env, 0), SIZE, rng)
    return [rng.choice(MODES)], img.pokes()


@S(0x004A20E0, "PlayerSelectWindow_ScalarDelete", this=PS_OBJ[0])
def gen_delete(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    select_window(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@S(0x004A2100, "PlayerSelectWindow_Init", this=PS_OBJ[0])
def gen_init(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    img.garbage(obj(env, 0), SIZE, rng)
    return [rng.choice(MODES)], img.pokes()


@S(0x004A2BC0, "PlayerSelectWindow_Destroy", this=PS_OBJ[0])
def gen_destroy(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    select_window(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@S(0x004A2CB0, "PlayerSelectWindow_SetPromptsHidden", this=PS_OBJ[0])
def gen_set_prompts_hidden(env, n):
    img, rng = new_image(env)
    select_window(env, img, rng, Pool(env, img, rng))
    return [rng.choice([0, 1, 0x100, 0xFF, rng.getrandbits(32)])], img.pokes()


@S(0x004A2CE0, "PlayerSelectWindow_Draw", this=PS_OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    pool = Pool(env, img, rng)
    state = rng.choice([0, 0, 1, 1, 2, 2, 2, 5])
    window_at = select_window(env, img, rng, pool, state=state, header=rng.random() < 0.6, load=load_object(env, img, rng, state=rng.choice([0, 1, 2, 2])))
    if rng.random() < 0.1:
        img.u32(window_at + HEADER, 0)
    return [], img.pokes()


@S(0x004A3110, "PlayerSelectWindow_UpdateFrame", this=PS_OBJ[0])
def gen_update_frame(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    select_window(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@S(0x004A3230, "PlayerSelectWindow_UpdateSkaters", this=PS_OBJ[0])
def gen_update_skaters(env, n):
    img, rng = new_image(env)
    player = select_globals(env, img, rng)
    pad_edges(img, rng, player, only=rng.choice(["left", "right", "confirm", "back", "confirm", "back", None]))
    select_window(env, img, rng, Pool(env, img, rng), state=0)
    return [], img.pokes()


@S(0x004A34B0, "PlayerSelectWindow_UpdateMenu", this=PS_OBJ[0])
def gen_update_menu(env, n):
    img, rng = new_image(env)
    player = select_globals(env, img, rng)
    pad_edges(img, rng, player, only=rng.choice(["back", None]))
    pool = Pool(env, img, rng)
    select_window(env, img, rng, pool, state=1, menu=menu_object(env, img, rng, pool))
    return [], img.pokes()


@S(0x004A35E0, "PlayerSelectWindow_UpdateLoad", this=PS_OBJ[0])
def gen_update_load(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    select_window(env, img, rng, Pool(env, img, rng), state=2)
    return [], img.pokes()


@S(0x004A35F0, "PlayerSelectWindow_NextPlayer", this=PS_OBJ[0])
def gen_next_player(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    select_window(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@S(0x004A36E0, "PlayerSelectWindow_Update", this=PS_OBJ[0])
def gen_update(env, n):
    img, rng = new_image(env)
    player = select_globals(env, img, rng)
    pad_edges(img, rng, player, only=rng.choice(["left", "right", "confirm", "back", "spin_left", "spin_right", None]))
    state = rng.choice([0, 0, 0, 1, 1, 2, 2, 7])
    select_window(env, img, rng, Pool(env, img, rng), state=state)
    return [], img.pokes()


@S(0x004A5F50, "PlayerMenu_GetChoice", this=PS_OBJ[2])
def gen_menu_choice(env, n):
    img, rng = new_image(env)
    menu_object(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


# ---- the small methods of the classes the window calls --------------------------------------------------------------------------------------------

@S(0x004A5380, "SkaterView_IsCustomSelected", this=PS_OBJ[1])
def gen_is_custom(env, n):
    img, rng = new_image(env)
    view_object(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@S(0x004A44B0, "SkaterView_SetMode", this=PS_OBJ[1])
def gen_set_mode(env, n):
    img, rng = new_image(env)
    view_object(env, img, rng, Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 0x100, 0xFF01, rng.getrandbits(32)])], img.pokes()


@S(0x004A44D0, "SkaterView_Rotate", this=PS_OBJ[1])
def gen_rotate(env, n):
    img, rng = new_image(env)
    view_object(env, img, rng, Pool(env, img, rng))
    return [rng.choice([30, 0xFFFFFFE2, 0x1E, rng.getrandbits(16), rng.getrandbits(32)])], img.pokes()


@S(0x004A5390, "SkaterView_StepSelection", this=PS_OBJ[1])
def gen_step_selection(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    view_object(env, img, rng, Pool(env, img, rng))
    return [rng.choice([1, 0xFFFFFFFF, 1, 0xFFFFFFFF, 2, 0xFFFFFFFE])], img.pokes()


@S(0x004A4890, "SkaterView_Refresh", this=PS_OBJ[1])
def gen_refresh(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    view_object(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@S(0x004A5340, "PlayerSelect_NextCostume")
def gen_next_costume(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    return [], img.pokes()


@S(0x004A5880, "PlayerSelectWindow_SlideIn", this=PS_OBJ[4])
def gen_slide_in(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env, 4), 0x100, rng)
    return [], img.pokes()


@S(0x004A5D70, "PlayerMenu_HighlightFirst", this=PS_OBJ[2])
def gen_highlight_first(env, n):
    img, rng = new_image(env)
    menu_object(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@S(0x004A78F0, "LoadWindow_IsLoading", this=PS_OBJ[3])
def gen_is_loading(env, n):
    img, rng = new_image(env)
    load_object(env, img, rng)
    return [], img.pokes()


# ---- the screen loop -----------------------------------------------------------------------------------------------------------------------------

@L(0x004A5F60, "PlayerSelect_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    return [rng.choice(MODES)], img.pokes()


@L(0x004A2010, "PlayerSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    select_globals(env, img, rng)
    img.u32(obj(env, 5), rng.choice(MODES))
    return [obj(env, 5), obj(env, 5) + 8], img.pokes()
