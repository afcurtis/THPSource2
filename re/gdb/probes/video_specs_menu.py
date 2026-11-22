"""Phase 4.15b case generators: the video options screen (vidmenu.cpp, 0x004cdf60..0x004ce2a0 and 0x004cea50).

The screen's window works on the options slab of frontend_specs_opt.py (the 4.10g layout: four objects of 0xc00 bytes at 0x24000, the arena at 0x1c000) plus the video
menu's globals (0x0056e780 ..: the mode, the option records, the gamma step) and the renderer words it clears.  Groups:

  vmn     the window's constructor and destructor, the start-up tables, the gamma call and the small stubs
  vmnrun  the screen loop (0x004ce030) and its wrapper (0x004ce020): the clock advances at every Game_FrameTick, the window's constructor, update and delete are seams;
          the window's update (a seam) sets its done flag, the pad poll (0x004cefb0) answers a button and stores -1, 0 or +1 in the caller's local
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_opt import (OPT_OBJ, SCRATCH, install_opt_common, opt_image, opt_regions, opt_world, register_opt)
from frontend_specs_screens import ARENA2, TICK, TEXTS, TEXT_STRIDE, TEXT_POOL
from frontend_specs_screens_cases import Pool, font_object, window
from frontend_specs_skwin import A

# ---- retail globals ----------------------------------------------------------------------------------------------------------------------------------------
MODE_BLOCK = 0x0056E780       # the mode, its column and the gamma step (0x56e7ac..0x56e7b4), the menu flag (0x56e7b8), the renderer (0x56e7c0), the option records (0x56e7c4..0x56e7f4)
MODE_BLOCK_SIZE = 0x80
SOFTWARE_WORD = 0x00548560    # 0x548564 and 0x548570 are the addresses the option records point at; 0x548568 is the software-renderer flag the screen clears
FRONT_FLAG = 0x0056114C
GAMMA = 0x0056E7D8
OPTION_RECORDS = 0x0056E7C4
RENDERER = 0x0056E7C0
RENDERER_CHOICE = 0x0056E7E4
DONE_OFFSET = 0xE4

VMN_STATIC = [
    (0x00518900, 0x20),       # the gamma scale and offset (0x51890c, 0x519910)
    (0x00519900, 0x20),
    (0x0052EA20, 0x20),       # the default mode and the option texts (0x52ea20, 0x52ea2c..0x52ea34)
    (0x0052EB10, 0x10),
    (0x005485F0, 0xB0),       # the log lines, the picture names (0x548638..0x548688)
]


def vmn_regions(slab):
    return opt_regions(slab) + [(MODE_BLOCK, MODE_BLOCK_SIZE), (SOFTWARE_WORD, 0x20)]


def register_vmn(group):
    register_opt(group, arena_items=False)
    EXTRA_REGIONS[group] = vmn_regions
    READ_ONLY[group] = list(READ_ONLY[group]) + VMN_STATIC


for _group in ("vmn", "vmnrun"):
    register_vmn(_group)


def vmn_spec(address, name, this=None, group="vmn"):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, group, this=resolve)


def vmn_world(env, img, rng):
    """The video menu's globals: random state for everything the functions read or write."""
    opt_world(env, img, rng)
    img.u32(MODE_BLOCK + 0x2C, rng.getrandbits(32))             # 0x56e7ac: the current mode
    img.u32(MODE_BLOCK + 0x30, rng.getrandbits(32))
    img.u32(MODE_BLOCK + 0x34, rng.choice([0, 1, rng.getrandbits(32)]))
    img.u8(MODE_BLOCK + 0x38, rng.choice([0, 1]))               # 0x56e7b8
    img.u32(RENDERER, rng.choice([0, 1, 2]))
    img.u32(RENDERER_CHOICE, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]))
    img.u32(GAMMA, rng.choice([0, 1, 2, 5, 10, 20, 0xFFFFFFFF, 0xFFFFFFF0, rng.randrange(-100, 100) & 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(SOFTWARE_WORD + 8, rng.choice([0, 1]))
    img.u32(FRONT_FLAG, rng.choice([0, 1, rng.getrandbits(32)]))


# ---- the stubs ---------------------------------------------------------------------------------------------------------------------------------------------

def install_vmn(fc, env):
    """The seams of the window's constructor: the engine object, the picture elements, the window's own methods (PcSystem's)."""
    install_opt_common(fc, env)
    if getattr(fc, "_vmn_installed", False):
        return
    fc._vmn_installed = True
    block = lambda a: env.alloc_log[-1][0]
    fc.stub(0x00454930, 0, "engine_00454930", convention="thiscall", this=True)
    fc.stub(0x004ACBE0, 7, "engine_004acbe0", convention="thiscall", this=True, text=True, result=block)
    fc.stub(0x004CEBB0, 3, "engine_004cebb0", convention="thiscall", this=True)
    fc.stub(0x004CED80, 1, "engine_004ced80", convention="thiscall", this=True)
    fc.stub(0x004D4120, 1, "engine_004d4120")


def install_vmnrun(fc, env):
    from frontend_specs_playsel import disable
    from opentony.memory import mem

    install_opt_common(fc, env)
    install_vmn(fc, env)
    if getattr(fc, "_vmnrun_installed", False):
        return
    fc._vmnrun_installed = True
    rng = env.rng
    done = lambda a: rng.choice([0, 0, 0, 1])
    menu = env.slab + ARENA2          # (the first block the screen allocates: the window)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    fc.stub(0x004CE2A0, 0, "engine_004ce2a0", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    # the window's update (a seam) sets its done flag; the pad poll stores the pad's direction through its argument and answers whether a button was pressed
    disable(fc, "element_update")
    from frontend_specs_menu import CHILD_UPDATE
    fc.stub(CHILD_UPDATE, 0, "element_update", convention="thiscall", this=True, writes=[(menu + DONE_OFFSET, 1, done)])
    fc.stub(0x004CEFB0, 1, "engine_004cefb0", convention="thiscall", this=True, stack_to=SCRATCH, pokes=[(0, 4, lambda a: rng.choice([0xFFFFFFFF, 0, 1]))],
            result=lambda a: rng.choice([0, 0, 1]))
    fc.stub(0x004CEC70, 0, "engine_004cec70", convention="thiscall", this=True)
    fc.stub(0x004CE280, 1, "engine_004ce280", convention="thiscall", this=True)
    fc.stub(0x004F2700, 0, "engine_004f2700", result=lambda a: rng.choice([0, 1]))
    for address, count in ((0x004CF030, 0), (0x00466BE0, 0), (0x004CF300, 0), (0x004DBE60, 0), (0x004DBD90, 0), (0x004F6D20, 2)):
        fc.stub(address, count, "engine_%08x" % address)      # (the log line is the shared printf_log stub)


GROUP_STUBS = {"vmn": install_vmn, "vmnrun": install_vmnrun}


# ---- the small functions -----------------------------------------------------------------------------------------------------------------------------------

def gen_init_state(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    return [], img.pokes()


def gen_init_options(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    for offset in range(0, 0x30, 4):
        img.u32(OPTION_RECORDS + offset, rng.getrandbits(32))
    return [], img.pokes()


# (each table's initialiser is also reached through a one-instruction jump thunk)
for _address, _name, _gen in ((0x004CDF60, "VideoMenu_InitStateThunk", gen_init_state), (0x004CDF70, "VideoMenu_InitState", gen_init_state),
                              (0x004CDFA0, "VideoMenu_InitOptionsThunk", gen_init_options), (0x004CDFB0, "VideoMenu_InitOptions", gen_init_options)):
    vmn_spec(_address, _name)(_gen)


@vmn_spec(0x004CE260, "VideoMenu_ApplyGamma")
def gen_apply_gamma(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    return [], img.pokes()


@vmn_spec(0x004CE280, "VideoMenu_NoOp", this=OPT_OBJ[0])
def gen_nothing(env, n):
    img, rng = opt_image(env)
    return [rng.getrandbits(32)], img.pokes()


@vmn_spec(0x004CE290, "VideoMenu_IsDone", this=OPT_OBJ[0])
def gen_is_done(env, n):
    img, rng = opt_image(env)
    img.u8(A(env, OPT_OBJ[0]) + DONE_OFFSET, rng.choice([0, 1, 2, 0x80, 0xFF, rng.getrandbits(8)]))
    return [], img.pokes()


# ---- the window --------------------------------------------------------------------------------------------------------------------------------------------

@vmn_spec(0x004CE2A0, "VideoMenu_Construct", this=OPT_OBJ[0])
def gen_construct(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    img.put(A(env, OPT_OBJ[0]), bytes(0x3CC))       # (retail's class new zeroes the block)
    # the four option records the rows are made from: a pointer to a text each (a row keeps its text in a 32-byte name: longer ones overflow it in retail)
    short = [k for k, text in enumerate(TEXT_POOL) if len(text) < 0x20]
    for i in range(4):
        img.u32(OPTION_RECORDS + 0xC * i, A(env, TEXTS + TEXT_STRIDE * rng.choice(short)))
    return [], img.pokes()


@vmn_spec(0x004CEA50, "VideoMenu_ScalarDelete", this=OPT_OBJ[0])
def gen_delete(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    base = A(env, OPT_OBJ[0])
    pool = Pool(env, img, rng)
    window(env, img, rng, base, 0x3C0, pool, children=rng.randrange(0, 3))
    keep = lambda value: 0 if rng.random() < 0.2 else value
    img.u32(base + 0x238, keep(pool.take()))
    img.u32(base + 0x388, keep(pool.take()))
    img.u32(base + 0x3B0, keep(pool.take()))
    for i in range(4):
        img.u32(base + 0x38C + 4 * i, keep(pool.take()))
    font_object(env, img, rng, base + 0x23C)
    font_object(env, img, rng, base + 0xE8)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


# ---- the loop ----------------------------------------------------------------------------------------------------------------------------------------------

@vmn_spec(0x004CE030, "VideoMenu_Run", group="vmnrun")
def gen_run(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    return [], img.pokes()


@vmn_spec(0x004CE020, "VideoMenu_RunInto", group="vmnrun")
def gen_run_into(env, n):
    img, rng = opt_image(env)
    vmn_world(env, img, rng)
    img.u32(A(env, OPT_OBJ[3]), rng.getrandbits(32))
    return [rng.getrandbits(32), A(env, OPT_OBJ[3])], img.pokes()
