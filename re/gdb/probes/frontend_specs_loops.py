"""Phase 4.10i3 probe: the screen loops 4.10c left unreplayed (MainMenu_Main with its wrapper, CardScreen_Main with its wrapper, Credits_Run).

The loops wait on the clock and on the pad, and the forced-call stubs could not change either. Since 4.10e a stub can write memory (`writes`): here Game_FrameTick writes the
clock (a few ticks, now and then a big jump) and the input poll writes the six pad edge bytes (zero, except that on the case's chosen frame one of them is 1); the quit tests answer
from a frame counter. A loop therefore runs a few frames and ends the way the case says: a pad press the screen takes, the quit flag, the quit combination, the screen's own state
(the card scan finishing) or the roll's timeline reaching its end; a backstop on the combination keeps every case finite.

Groups: mloop (the title screen and the memory-card scan screen: the windows are built by the retail constructors inside the forced call, so their vtables are the real ones; the prompt
windows the stubbed constructor hands back and the arena blocks the other stubbed constructors fill carry fake vtables) and cloop (the credits roll).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from opentony.memory import mem

import frontend_specs_credits as cr
import frontend_specs_mainmenu as mm

GROUPS = ("mloop", "cloop")

TICK = 0x0056E31C
PAD_EDGES = [0x0056B029, 0x0056B0D9, 0x0056AFF9, 0x0056B195, 0x0056B245, 0x0056B165]
RUN_TEXT = 0x7FF00FB0            # the parse buffer of the roll's manager, also in its stack frame (the bitmap records hand its text to the sprite constructor)
RUN_SPRITE = 0x7FF01000          # the stack object Credits_Run keeps its background sprite in (the native code builds it at this address)

DRIVE = {"frame": 0, "press": None, "pad": 0, "quit": None, "combo": None, "jumps": False}


def drive(rng, *, frames=None, pad_ok=True):
    """The script of one case: how many frames it runs and how it ends."""
    limit = rng.randrange(1, 7) if frames is None else frames
    DRIVE.update(frame=0, press=None, pad=0, quit=None, combo=None, jumps=rng.random() < 0.15)
    mode = rng.choice(["pad", "pad", "quit", "combo"] if pad_ok else ["quit", "combo", "combo"])
    if mode == "pad":
        DRIVE["press"] = rng.randrange(1, limit + 1)
        DRIVE["pad"] = rng.randrange(len(PAD_EDGES))
        DRIVE["combo"] = limit + 3          # (a press the screen ignores must not run for ever)
    elif mode == "quit":
        DRIVE["quit"] = limit
        DRIVE["combo"] = limit + 3
    else:
        DRIVE["combo"] = limit


def pad_write(index):
    def produce(args):
        if index == 0:
            DRIVE["frame"] += 1            # (one input poll a frame: the first write counts it)
        return 1 if DRIVE["press"] == DRIVE["frame"] and DRIVE["pad"] == index else 0

    return produce


def install_loop_stubs(fc, env):
    """The per-frame services of the loops: the clock, the pad and the quit tests are driven, the rest report."""
    rng = env.rng

    def next_tick(args):
        step = rng.choice([1, 2, 3, 3, 4, 7]) if not DRIVE["jumps"] else rng.choice([300, 1000, 5000])
        return (mem.u32(TICK) + step) & 0xFFFFFFFF

    ensure(fc, 0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, next_tick)])
    ensure(fc, 0x004F5FF0, 0, "engine_004f5ff0", writes=[(edge, 1, pad_write(i)) for i, edge in enumerate(PAD_EDGES)])
    ensure(fc, 0x004E41B0, 1, "engine_004e41b0", result=lambda a: 1 if DRIVE["quit"] is not None and DRIVE["frame"] >= DRIVE["quit"] else 0)
    ensure(fc, 0x00452D30, 0, "engine_00452d30", result=lambda a: 1 if DRIVE["combo"] is not None and DRIVE["frame"] >= DRIVE["combo"] else 0)


# ---- the title screen and the card scan screen -------------------------------------------------------------------------------------------------------------------------

# every arena block starts with a vtable that serves the stub-built objects: delete (slot 0), the text box's method (slot 1) and the sprite's draw (slot 3)
VT_ARENA = 0x0100
RESULT = 0x3FC0           # the cell a wrapper stores its answer in (past the prompt windows, which run to 0x3f80)


def seed_mloop(env, rng):
    from opentony.fcall import write_chunks

    mm.seed_mainmenu(env, rng)
    write_chunks(env.slab + VT_ARENA, struct.pack("<IIII", mm.VIRTUAL_DELETE, mm.TEXTBOX_V1, 0, mm.SPRITE_DRAW))
    block = struct.pack("<I", env.slab + VT_ARENA)
    for offset in range(0, mm.ARENA_SIZE, 16):
        write_chunks(env.slab + mm.ARENA + offset, block)
    # the prompt windows the stubbed constructor hands back are elements of the title window
    for k in range(8):
        write_chunks(env.slab + mm.PROMPTS + 0x20 * k, struct.pack("<I", env.slab + mm.VT_ELEMENT))
    return {}


SLAB_BYTES["mloop"] = mm.SLAB
EXTRA_REGIONS["mloop"] = mm.regions
READ_ONLY["mloop"] = mm.READ_ONLY["mainmenu"]
SEEDS["mloop"] = seed_mloop


def install_mloop_stubs(fc, env):
    if getattr(fc, "_mloop_installed", False):
        return
    fc._mloop_installed = True
    mm.install_mainmenu_stubs(fc, env)
    install_loop_stubs(fc, env)
    # the real classes' per-frame hooks (slot 7 of the title window, the wheel, the card window and the real text and sprite elements) are engine functions: reported like the fake vtables' hook
    for hook in (0x004DD140, 0x004DCF80, 0x004DC9F0, 0x004DCA60, 0x004DCB30):
        ensure(fc, hook, 0, "element_hook", convention="thiscall", this=True)


def lspec(address, name):
    return spec(address, name, "mloop")


def set_clock(img, rng):
    """The clock and the last frame's clock: a loop waits until two ticks have passed, so the last clock is close to the clock (a random pair would make it wait for ever)."""
    clock = rng.randrange(1000, 100000)
    img.u32(TICK, clock)
    img.u32(0x00568624, (clock + rng.choice([-5, -1, 0, 1, 2, 3, 10, 40])) & 0xFFFFFFFF)


def loop_world(env, img, rng):
    mm.world(env, img, rng)
    set_clock(img, rng)
    for pad in (0x0056B038,):
        img.u8(pad, rng.choice([0, 0, 1]))
    img.u8(0x006A1415, rng.choice([0, 0, 0, 0, 1]))       # the quit flag the title loop also tests
    drive(rng)


@lspec(0x0046AEF0, "MainMenu_Main")
def gen_main(env, n):
    img, rng = Image(), env.rng
    loop_world(env, img, rng)
    return [], img.pokes()


@lspec(0x0046AEE0, "MainMenu_RunWrapper")
def gen_main_wrapper(env, n):
    img, rng = Image(), env.rng
    loop_world(env, img, rng)
    img.garbage(env.slab + RESULT, 4, rng)
    return [0, env.slab + RESULT], img.pokes()


@lspec(0x0046C460, "CardScreen_Main")
def gen_card(env, n):
    img, rng = Image(), env.rng
    loop_world(env, img, rng)
    return [], img.pokes()


@lspec(0x0046BE90, "CardScreen_RunWrapper")
def gen_card_wrapper(env, n):
    img, rng = Image(), env.rng
    loop_world(env, img, rng)
    img.garbage(env.slab + RESULT, 4, rng)
    return [0, env.slab + RESULT], img.pokes()


# ---- the credits roll ------------------------------------------------------------------------------------------------------------------------------------------------------

def seed_cloop(env, rng):
    from opentony.fcall import write_chunks

    cr.seed_credits(env, rng)
    # the sprites the bitmap records build are arena blocks the stubbed constructor leaves as they are: each block starts with the fake sprite vtable
    block = struct.pack("<I", env.slab + cr.VT_SPRITE)
    for offset in range(0, cr.ARENA_SIZE, 16):
        write_chunks(env.slab + cr.ARENA + offset, block)
    return {}


def cloop_regions(slab):
    out = cr.regions(slab) + [(0x00568624, 4), (mem.u32(0x00560FD4) + 0x88, 4)]
    return out + [(edge, 1) for edge in PAD_EDGES]


SLAB_BYTES["cloop"] = cr.SLAB
EXTRA_REGIONS["cloop"] = cloop_regions
READ_ONLY["cloop"] = cr.READ_ONLY["credits"] + [(0x00560FD4, 4), (0x0052E61C, 4)]
SEEDS["cloop"] = seed_cloop


def install_cloop_stubs(fc, env):
    if getattr(fc, "_cloop_installed", False):
        return
    fc._cloop_installed = True
    rng = env.rng
    cr.install_credits_stubs(fc, env)
    install_loop_stubs(fc, env)
    for address, argc in ((0x004B2390, 0), (0x0048A420, 0), (0x00489490, 0), (0x004DAE00, 0), (0x00418150, 0), (0x004304B0, 0), (0x00466BE0, 0), (0x00489A10, 0),
                          (0x004E8740, 1), (0x004C9300, 1), (0x00430510, 0), (0x00466C00, 0), (0x0046A600, 2)):
        ensure(fc, address, argc, "engine_%08x" % address)
    ensure(fc, 0x00489670, 1, "pad_clear")
    ensure(fc, 0x0044ADA0, 1, "font_load", text=True, result=lambda a: cr.font_ptr(env, rng))
    ensure(fc, 0x0044B0A0, 1, "font_unload", text=True)
    ensure(fc, 0x00448DD0, 2, "engine_00448dd0", text=True, result=lambda a: rng.randrange(1, 0x1000))
    ensure(fc, 0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, stack_to=RUN_TEXT, result=lambda a: env.alloc_log[-1][0])
    ensure(fc, 0x004D74A0, 3, "engine_004d74a0", convention="thiscall", this=True, stack_to=RUN_SPRITE)
    ensure(fc, 0x004D7990, 3, "engine_004d7990", convention="thiscall", this=True, stack_to=RUN_SPRITE)
    ensure(fc, 0x004D78C0, 0, "engine_004d78c0", convention="thiscall", this=True, stack_to=RUN_SPRITE)


def cspec(address, name):
    return spec(address, name, "cloop")


@cspec(0x00426430, "Credits_Run")
def gen_credits_run(env, n):
    img, rng = Image(), env.rng
    cr.world(env, img, rng)
    set_clock(img, rng)
    # the roll's two static fonts are real Font objects whose draw is an engine function: give them the fake font vtable, whose draw is a reported service
    img.u32(cr.FONT_A, env.slab + cr.VT_FONT)
    img.u32(cr.FONT_B, env.slab + cr.VT_FONT)
    mode = rng.choice([0, 1])            # (any other mode fails the check and then indexes the two-name local array past its end: a stack leftover)
    img.u32(env.slab + cr.SCRATCH + 0x40, mode)
    img.put(env.slab + cr.ARENA, cr.credits_text(rng) + b"\0")
    drive(rng, pad_ok=True)
    return [env.slab + cr.SCRATCH + 0x40], img.pokes()


GROUP_STUBS = {"mloop": install_mloop_stubs, "cloop": install_cloop_stubs}
