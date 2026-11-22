"""Phase 4.10f3 case generators: the two-player handicap screen (handicap.cpp, 0x00454e80..0x00455f00), built the way the career select screen's probe is
(frontend_specs_carsel.py: the 4.10e1 slab, fake pool objects whose virtual methods are reported services, the toolkit's real objects where a width or a number
is computed).  Groups: hanpan (one player's panel), hanwin (the window and its callbacks) and hanloop (the screen loop; its window's step, update, draw and delete
are seams).

The panel (0x364 bytes, vtable 0x00518a08): +0xac and +0x1f8 two Font objects, +0x344 / +0x348 the dots sprites, +0x34c / +0x350 two real MenuDots, +0x354 the
face (an element), +0x358 the face sprite, +0x35c ready, +0x360 the player.  The window (0x350 bytes, 0x00518a28): +0xac the background, +0xb0 and +0x1fc two Fonts,
+0x348 and +0x34c the two panels.  The handicaps are the two words at 0x0056a2f0.

The frame of the loop waits for the clock with a bare spin that a forced call cannot satisfy (nothing ticks the clock while the call runs), so the loop is only
replayed on a first frame that ends it: the window's step answers 1 or 2.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY
from frontend_specs_menu import coords
from frontend_specs_menu_items import dots_object
from frontend_specs_screens import OBJ, TICK, install_screen_stubs, regions, register
from frontend_specs_screens_cases import Pool, common, font_object, gspec, window
from frontend_specs_skwin import A, new_image

HANDICAP = 0x0056A2F0
SECOND_PAD = 0x0056B2B0
SCRATCH_LINE = 0x7FF00400      # where the native code builds the strings retail keeps in locals (ScreenKit's kScratchLine)

# the text pointer variables and strings the screen reads, and the words it writes
# (and the skaters' face bitmap names, "s2face%d.bmp", which lie in the data section from 0x00535f04 to 0x0053e8bc)
STATIC_EXTRA = [(0x0052E330, 0x50), (0x0052D840, 0x80), (0x00535F00, 0x8B00)]
WORDS = [(HANDICAP, 8), (0x029DA3A0, 4)]      # (the handicaps; the software-renderer flag the dots' draw reads)


def han_regions(slab):
    return regions(slab) + WORDS


GROUPS = ("hanpan", "hanwin", "hanloop")
for _group in GROUPS:
    register(_group)
    EXTRA_REGIONS[_group] = han_regions
    READ_ONLY[_group] = list(READ_ONLY[_group]) + STATIC_EXTRA

PAN = gspec("hanpan")
WIN = gspec("hanwin")
LOOP = gspec("hanloop")

# panel fields
P_FONT_A, P_FONT_B = 0xAC, 0x1F8
P_DOTS_SPRITE_A, P_DOTS_SPRITE_B, P_DOTS_A, P_DOTS_B, P_FACE, P_FACE_SPRITE, P_READY, P_PLAYER = 0x344, 0x348, 0x34C, 0x350, 0x354, 0x358, 0x35C, 0x360
P_SIZE = 0x364
# window fields
W_BACKGROUND, W_FONT_A, W_FONT_B, W_PANEL_A, W_PANEL_B = 0xAC, 0xB0, 0x1FC, 0x348, 0x34C
W_SIZE = 0x350

PAD_STRIDE = 0x16C
PAD = {"left": 0x0056B079, "right": 0x0056B089, "confirm": 0x0056B029, "back": 0x0056AFF9}


# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------

HAN_ENGINE = [(0x004CA8D0, 3, False), (0x00457810, 0, True)]


def install_han_stubs(fc, env):
    if getattr(fc, "_han_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._han_installed = True
    from frontend_specs_playsel import disable

    install_screen_stubs(fc, env)
    for address, count, thiscall in HAN_ENGINE:
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall)
    # the lines under the dots are formatted into a local buffer: the font draw is handed its stack address (recorded as the native code's scratch line)
    disable(fc, "font_draw")
    fc.stub(0x004AD5E0, 4, "font_draw", convention="thiscall", this=True, text=2, stack_to=SCRATCH_LINE)


def install_han_loop_stubs(fc, env):
    """The loop: the clock advances at every Game_FrameTick; the window's step answers 1 or 2 (the frame that follows a 0 waits on the clock), and its update,
    draw and delete are seams. The loop logs both handicaps with a two-argument printf."""
    from frontend_specs_playsel import disable
    from opentony.memory import mem

    rng = env.rng
    install_han_stubs(fc, env)
    disable(fc, "printf_log")
    fc.stub(0x004CF850, 2, "printf_log")
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    fc.stub(0x00455E90, 0, "engine_00455e90", convention="thiscall", this=True, result=lambda a: rng.choice([1, 2, 2]))
    fc.stub(0x004AC730, 0, "engine_004ac730", convention="thiscall", this=True)
    fc.stub(0x00455E10, 2, "engine_00455e10", convention="thiscall", this=True)
    fc.stub(0x00455D20, 1, "engine_00455d20", convention="thiscall", this=True)


GROUP_STUBS = {"hanpan": install_han_stubs, "hanwin": install_han_stubs, "hanloop": install_han_loop_stubs}


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def pad_edges(img, rng, *, only=None, chance=0.2):
    """The four edges of both pads the panel reads. `only` names the one set on both pads (the others are rarely set)."""
    for player in range(2):
        for name, address in PAD.items():
            if only is None:
                value = 1 if rng.random() < 0.3 else 0
            else:
                value = 1 if name == only else (1 if rng.random() < chance else 0)
            img.u8(address + PAD_STRIDE * player, value)


def panel_state(env, img, rng, base, pool, *, player, ready=None, destroy=False, handicap=None):
    """A panel: the window part with fake children, two fonts, fake sprites, two real rows of dots and a fake face element."""
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, P_SIZE, pool, children=rng.randrange(0, 3))
    for slot in (P_FONT_A, P_FONT_B):
        font_object(env, img, rng, base + slot)
    for slot in (P_DOTS_SPRITE_A, P_DOTS_SPRITE_B, P_FACE_SPRITE):
        img.u32(base + slot, keep(pool.take()))
    for slot in (P_DOTS_A, P_DOTS_B):
        dots = pool.take()
        dots_object(env, img, rng, dots)
        img.u32(base + slot, keep(dots))
    img.u32(base + P_FACE, pool.take())
    ready = rng.choice([0, 1]) if ready is None else ready
    img.u8(base + P_READY, ready)
    img.u32(base + P_PLAYER, player)
    img.u32(HANDICAP + 4 * player, (rng.choice([0, 0, 1, -1, 5, -5, rng.randrange(-6, 7), 4, -4]) if handicap is None else handicap) & 0xFFFFFFFF)
    return ready


def window_state(env, img, rng, base, pool, *, destroy=False, panels=None):
    """A handicap window: the window part, the background and the two fonts; the panels are fake objects, or the two given panel objects."""
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, W_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + W_BACKGROUND, keep(pool.take()))
    for slot in (W_FONT_A, W_FONT_B):
        font_object(env, img, rng, base + slot)
    first, second = panels if panels else (pool.take(), pool.take())
    img.u32(base + W_PANEL_A, first)
    img.u32(base + W_PANEL_B, second)


# ---- the panel -------------------------------------------------------------------------------------------------------------------------------

@PAN(0x00454EC0, "HandicapPlayer_Construct", this=OBJ[0])
def gen_panel_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), P_SIZE, rng)
    player = rng.choice([0, 1])
    img.u32(HANDICAP + 4 * player, rng.choice([0, 1, -1, 5, -5, rng.randrange(-6, 7)]) & 0xFFFFFFFF)
    return [player, rng.choice([0x80, 0x180, 0, rng.randrange(0, 0x200)]), rng.choice([0, 0x10]), 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@PAN(0x00455410, "HandicapPlayer_ScalarDelete", this=OBJ[0])
def gen_panel_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    panel_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), player=rng.choice([0, 1]), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@PAN(0x00455430, "HandicapPlayer_Destroy", this=OBJ[0])
def gen_panel_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    panel_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), player=rng.choice([0, 1]), destroy=True)
    return [], img.pokes()


@PAN(0x004554F0, "HandicapPlayer_Poll", this=OBJ[0])
def gen_panel_poll(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pad_edges(img, rng, only=rng.choice(["confirm", "back", "confirm", "back", None]))
    panel_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), player=rng.choice([0, 1]))
    return [], img.pokes()


@PAN(0x00455590, "HandicapPlayer_Update", this=OBJ[0])
def gen_panel_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pad_edges(img, rng, only=rng.choice(["left", "right", "left", "right", None]))
    panel_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), player=rng.choice([0, 1]), ready=rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


@PAN(0x00455690, "HandicapPlayer_Draw", this=OBJ[0])
def gen_panel_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    panel_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), player=rng.choice([0, 1]))
    return [coords(rng), coords(rng)], img.pokes()


# ---- the window and the callback record --------------------------------------------------------------------------------------------------------

@WIN(0x00454E80, "Handicap_InitCallbacks")
def gen_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@WIN(0x00454EA0, "Handicap_ExitCallback")
def gen_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@WIN(0x004558C0, "HandicapWindow_Construct", this=OBJ[0])
def gen_window_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), W_SIZE, rng)
    for player in range(2):
        img.u32(HANDICAP + 4 * player, rng.choice([0, 1, -1, 5, -5, rng.randrange(-6, 7)]) & 0xFFFFFFFF)
    return [], img.pokes()


@WIN(0x00455D20, "HandicapWindow_ScalarDelete", this=OBJ[0])
def gen_window_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@WIN(0x00455D40, "HandicapWindow_Destroy", this=OBJ[0])
def gen_window_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@WIN(0x00455E10, "HandicapWindow_Draw", this=OBJ[0])
def gen_window_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@WIN(0x00455E90, "HandicapWindow_Step", this=OBJ[0])
def gen_window_step(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(SECOND_PAD, rng.choice([0, 1, 1, 1]))
    pad_edges(img, rng, only=rng.choice(["confirm", "back", None, "confirm"]))
    pool = Pool(env, img, rng)
    first, second = A(env, OBJ[1]), A(env, OBJ[2])
    panel_state(env, img, rng, first, pool, player=0)
    panel_state(env, img, rng, second, pool, player=1)
    window_state(env, img, rng, A(env, OBJ[0]), pool, panels=(first, second))
    return [], img.pokes()


# ---- the screen loop and its callback ---------------------------------------------------------------------------------------------------------

@LOOP(0x00454EB0, "Handicap_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    for player in range(2):
        img.u32(HANDICAP + 4 * player, rng.choice([0, 1, -1, 5, -5, rng.randrange(-6, 7)]) & 0xFFFFFFFF)
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@LOOP(0x00455F00, "Handicap_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    for player in range(2):
        img.u32(HANDICAP + 4 * player, rng.choice([0, 1, -1, 5, -5, rng.randrange(-6, 7)]) & 0xFFFFFFFF)
    return [], img.pokes()
