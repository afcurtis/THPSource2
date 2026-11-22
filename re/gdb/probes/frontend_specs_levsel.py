"""Phase 4.10f3 case generators: the level select screen (levelsel.cpp, 0x004588c0..0x0045b080), built the way the career select screen's probe is
(frontend_specs_carsel.py: the 4.10e1 slab, fake pool objects whose virtual methods are reported services, the toolkit's real rows).  Groups: levwin (the window and
its callbacks), levloop (the screen loop), levban (the player banner and the name-word helper), levglobe, levname and levmenu (the other classes).

Every class another function constructs or calls on a child is a seam: the window's constructor builds its banners, globe, name window, information windows and
mode menu through stubbed constructors, `change_level` and `update` call the children's functions as stubs; each class is replayed on its own in its group.
The window's loop uses seams for the step (a stub that writes the answer through the pointer and answers done), update, draw and delete.

Objects: the window (0x628 bytes, vtable 0x00518a90) holds the banners (+0xac, +0xb0), five prompts (+0xb8), the name window (+0xcc), the globe (+0xd0), the
information windows (+0xd4, +0xd8), the mode menu (+0xdc), the goal prompt (+0xe0), the background (+0xe4), four fonts, the state (+0x618), the park menu (+0x61c),
done (+0x620) and the answer (+0x624).  The level table (0x0053c1f8, 0x1ac bytes a level) is read as it is in the game.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS
from frontend_specs_menu import FIND_FONTS, FONT_SIZE, VT_REAL as MENU_REAL
from frontend_specs_menu import CHILD_UPDATE, VIRTUAL_DELETE, coords, poke_props, props_at
from frontend_specs_screens import OBJ, TEXTS, TEXT_STRIDE, TICK, install_screen_stubs, regions, register, seed_screens, TEXT_POOL
from frontend_specs_screens_cases import Pool, common, font_object, gspec, window
from frontend_specs_skwin import VT_WINDOW, A, item_row, list_object, new_image

CHOSEN = 0x00561CE4
MODE = 0x00561C74
TWO_SKATERS = 0x00561C70
LOADER_FLAG = 0x005614BC
LOADER_BASE = 0x005614CC
FLAG_BYTE = 0x00561450
EVENT_SELECTOR = 0x00533F38
CURRENT_LEVEL = 0x0056A898
BYTE_FLAG = 0x0055FF90
PLAYER_SPOOL = 0x0056DB4C
REGION_NAMES = 0x0056D428
LEVELS = 0x0053C1F8
LEVEL_COUNT = 14
LEVEL_STRIDE = 0x1AC
SCRATCH_LINE = 0x7FF00400

VT_TFLAG = 0x1A190       # a fake flagged-text element: delete, update, (width at slot 3)
TFLAG_V3 = 0x004ADC70    # an unreached engine entry point: the slot-3 method (answers the text's width)

# the words the screens read and write, and the level table
WORDS = [(0x00561C70, 0x10), (CHOSEN, 4), (LOADER_FLAG, 4), (LOADER_BASE, 4), (FLAG_BYTE, 4), (EVENT_SELECTOR, 4), (CURRENT_LEVEL, 4), (BYTE_FLAG, 4), (PLAYER_SPOOL, 4),
         (REGION_NAMES, 0x220)]
STATIC_EXTRA = [(LEVELS, LEVEL_COUNT * LEVEL_STRIDE + 0x10), (0x00525400, 0x80), (0x005258D0, 0x40), (0x0052DE00, 0x200), (0x00535F00, 0x8B00), (0x0052E8F0, 0x80), (0x005252F0, 0x20), (0x00531780, 0x40), (0x0052CBDC, 0x40)]


def lev_regions(slab):
    from opentony.memory import mem

    # the location strings the level table points to: static data (read-only regions of the groups) or the game's own heap blocks
    strings = []
    for pointer in sorted(p for p in {mem.u32(LEVELS + level * LEVEL_STRIDE + 0x40) for level in range(LEVEL_COUNT)} if p >= 0x600000):
        if strings and pointer < strings[-1][1]:
            strings[-1][1] = pointer + 0x40
        else:
            strings.append([pointer, pointer + 0x40])
    return regions(slab) + WORDS + [(start, end - start) for start, end in strings]


def seed_lev(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_screens(env, rng)
    write_chunks(env.slab + VT_TFLAG, struct.pack("<IIII", VIRTUAL_DELETE, CHILD_UPDATE, 0, TFLAG_V3))
    return writes


GROUPS = ("levwin", "levloop", "levban", "levglobe", "levname", "levmenu")
for _group in GROUPS:
    register(_group)
    EXTRA_REGIONS[_group] = lev_regions
    READ_ONLY[_group] = list(READ_ONLY[_group]) + STATIC_EXTRA
    SEEDS[_group] = seed_lev

WIN = gspec("levwin")
LOOP = gspec("levloop")
BAN = gspec("levban")
GLB = gspec("levglobe")
NAM = gspec("levname")
MNU = gspec("levmenu")

# window fields
W_BANNER_A, W_BANNER_B, W_BANNER_X, W_PROMPTS = 0xAC, 0xB0, 0xB4, 0xB8
W_NAME, W_GLOBE, W_INFO, W_GOALS, W_MENU, W_PERCENT, W_BACKGROUND = 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4
W_FONTS = (0xE8, 0x234, 0x380, 0x4CC)
W_STATE, W_PARK, W_DONE, W_ANSWER = 0x618, 0x61C, 0x620, 0x624
W_SIZE = 0x628
# banner, globe, name and menu fields
B_FACE, B_LINE_A, B_LINE_B, B_FONT, B_BUFFER_A, B_BUFFER_B, B_SIZE = 0xAC, 0xB0, 0xB4, 0xB8, 0x204, 0x218, 0x22C
G_RECORD, G_REGION, G_CROSSHAIR, G_LAT, G_LAT_TARGET, G_LONG, G_LONG_TARGET, G_SIZE = 0x2C, 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C, 0xA4
N_NAME, N_LOCATION, N_FONT, N_SLIDE, N_SHOWN, N_WANTED, N_STATE, N_SIZE = 0xAC, 0xB0, 0xB4, 0x200, 0x204, 0x208, 0x20C, 0x210
M_LIST, M_ROWS, M_STYLE, M_RESULT, M_FLAG, M_PAD, M_SIZE = 0xAC, 0xB0, 0x230, 0x234, 0x238, 0x23C, 0x240

PAD_STRIDE = 0x16C
PADS = {"left": (0x0056B079, 0x0056B1E5), "right": (0x0056B089, 0x0056B1F5), "confirm": (0x0056B029, 0x0056B0D9, 0x0056B195, 0x0056B245),
        "back": (0x0056AFF9, 0x0056B165), "triangle": (0x0056B019,)}


# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------

def stub_t(fc, address, count, **kw):
    fc.stub(address, count, kw.pop("name", "engine_%08x" % address), convention="thiscall", this=True, **kw)


def stub_c(fc, address, count, **kw):
    fc.stub(address, count, kw.pop("name", "engine_%08x" % address), **kw)


def install_lev_base(fc, env):
    """The seams every group shares: the engine's PSX, spool and sprite calls and the pad edge reader."""
    if getattr(fc, "_lev_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._lev_installed = True

    from frontend_specs_playsel import disable

    install_screen_stubs(fc, env)
    # the prompt windows are arena blocks (the constructor of a window hands back what the caller allocated): the screen calls virtual methods of them
    disable(fc, "prompt_window_construct")
    fc.stub(0x004A1B40, 7, "prompt_window_construct", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    stub_c(fc, 0x004E8730, 4, stack_to=SCRATCH_LINE)                      # the camera view rectangle (a local)
    stub_c(fc, 0x004B18A0, 1, result=lambda a: env.rng.randrange(0, 8))   # Skater_SelectManagerSlot
    stub_t(fc, 0x004A1ED0, 7, result=lambda a: env.alloc_log[-1][0])      # a flagged text element
    for address in (0x004B52B0, 0x004B5580, 0x004B5300, 0x004B5350, 0x004B55C0):   # the player spool: reset, force-finalize, process, finalize, update
        stub_t(fc, address, 0)
    # the other windows of the screen and the park menu (seams for the window; the 4.10h ones belong to their own screen)
    stub_c(fc, 0x0045B8A0, 2, result=lambda a: env.rng.choice([0, 1, 1]))
    stub_t(fc, 0x0045B890, 1)
    stub_t(fc, 0x0045C360, 2)
    stub_c(fc, 0x004DE150, 0, result=lambda a: env.rng.choice([0, 0, 0, 1]))
    stub_c(fc, 0x004DE180, 2)
    stub_c(fc, 0x00454E60, 0)
    stub_t(fc, 0x00432C10, 0)
    stub_t(fc, 0x00432C60, 0, result=lambda a: env.rng.choice([0, 1, 2, 2, 3]))
    stub_t(fc, 0x004329D0, 4, result=lambda a: env.alloc_log[-1][0])
    stub_c(fc, 0x00416050, 1, result=lambda a: A(env, OBJ[3]))
    stub_c(fc, 0x00414130, 2, result=lambda a: env.rng.randrange(0, 10))
    stub_t(fc, 0x004A1EB0, 1, stack_to=SCRATCH_LINE, text=True)
    stub_t(fc, 0x004A1670, 5, result=lambda a: env.alloc_log[-1][0])      # the medal window's constructor


def seam_ctor(fc, env, address, count):
    stub_t(fc, address, count, result=lambda a: env.alloc_log[-1][0])


def install_levwin(fc, env):
    install_lev_base(fc, env)
    for address, count in ((0x00459DB0, 5), (0x0045A1B0, 4), (0x0045A6E0, 4), (0x0045B0A0, 6), (0x0045B950, 6), (0x0045ABC0, 3)):
        seam_ctor(fc, env, address, count)
    stub_t(fc, 0x0045A9A0, 1)
    stub_t(fc, 0x0045A6B0, 1)
    stub_t(fc, 0x0045AB50, 0)
    # the mode menu's answer: the pad that confirmed (written through the pointer) and the chosen row
    stub_t(fc, 0x0045B080, 1, keep=[], pokes=[(0, 4, lambda a: env.rng.choice([0, 1]))], result=lambda a: env.rng.choice([0, 1, 2, 3, 4, 5]))


def install_levloop(fc, env):
    from opentony.memory import mem

    rng = env.rng
    install_levwin(fc, env)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    # the step answers done on most frames and writes the answer through its pointer; the window's update, draw and delete are seams
    stub_t(fc, 0x00459D90, 1, keep=[], pokes=[(0, 4, lambda a: rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 3, 7, 11, 13, 0x22B, 0x22B]))],
           result=lambda a: rng.choice([0, 1, 1, 1]))
    stub_c(fc, 0x00466BE0, 0)                      # the poly buffer end calculation of each frame
    stub_t(fc, 0x00459650, 0)
    stub_t(fc, 0x004595E0, 0)
    stub_t(fc, 0x004594E0, 1)
    stub_c(fc, 0x0045D830, 1)
    stub_c(fc, 0x0045DB40, 0)


def install_levban(fc, env):
    install_lev_base(fc, env)


def install_levglobe(fc, env):
    install_lev_base(fc, env)
    # (the camera, fog, model and cleanup calls are the shared engine stubs of the screens' slab)
    stub_t(fc, 0x0047FDB0, 0)
    stub_c(fc, 0x004B37A0, 4, text=True, result=lambda a: env.rng.randrange(0, 8))
    stub_c(fc, 0x004B3DF0, 0)
    stub_t(fc, 0x0047FE20, 0)


def install_levname(fc, env):
    install_lev_base(fc, env)
    # (the flagged text elements' slot-3 method answers the text's width)
    fc.stub(TFLAG_V3, 0, "element_width", convention="thiscall", this=True, result=lambda a: env.rng.randrange(0, 0x180))


def install_levmenu(fc, env):
    install_lev_base(fc, env)


GROUP_STUBS = {"levwin": install_levwin, "levloop": install_levloop, "levban": install_levban, "levglobe": install_levglobe, "levname": install_levname,
               "levmenu": install_levmenu}


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def pad_edges(img, rng, *, only=None, chance=0.12):
    """The edges the window reads on both pads. `only` names the group that is set on some of its addresses (the others are rarely set)."""
    for name, addresses in PADS.items():
        for address in addresses:
            if only == name:
                value = 1 if rng.random() < 0.55 else 0
            else:
                value = 1 if rng.random() < (0.25 if only is None else chance) else 0
            img.u8(address, value)


def lev_world(env, img, rng, *, mode=None, level=None, two=None):
    """The globals of the screen: the chosen level, the game mode and the two-skater byte, the spool, the loader, the config the percentage reads."""
    img.u32(CHOSEN, rng.randrange(LEVEL_COUNT) if level is None else level)
    img.u32(MODE, rng.choice([1, 1, 2, 3]) if mode is None else mode)
    img.u8(TWO_SKATERS, rng.choice([0, 1]) if two is None else two)
    img.u32(PLAYER_SPOOL, rng.randrange(0x1000, 0x100000))
    img.u32(LOADER_BASE, A(env, OBJ[2]) + 0x40)
    img.u32(A(env, OBJ[2]) + 0x40, rng.getrandbits(32))
    img.u8(FLAG_BYTE, rng.choice([0, 1]))
    # the per-level medal bits the percentage reads (a record of 14 halfwords at +0xc of the config the stub hands back)
    config = A(env, OBJ[3])
    for i in range(LEVEL_COUNT):
        img.put(config + 0xC + 2 * i, struct.pack("<H", rng.choice([0, 0, 0x400, 0x800, 0x1000, 0x8000, 0x8400, 0x8800, 0x9000, rng.getrandbits(16)])))


def flagged_text(env, img, rng, pool, *, arrived=None):
    """A flagged text element (fake vtable: delete, update and the width at slot 3): a string, a font, a place and the byte at +0x1c the slide sets on arrival."""
    base = pool.take()
    img.u32(base, A(env, VT_TFLAG))
    img.u32(base + 0x08, rng.randrange(-300, 600) & 0xFFFFFFFF)
    img.u8(base + 0x1C, rng.choice([0, 1]) if arrived is None else arrived)
    img.u32(base + 0x28, A(env, TEXTS + TEXT_STRIDE * rng.randrange(len(TEXT_POOL))))
    img.u32(base + 0x2C, A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)))
    return base


def window_state(env, img, rng, base, pool, *, destroy=False, state=None, park=None, done=None):
    """A level select window: the window part, the prompts and the children (fake objects), the four fonts and the state."""
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, W_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + W_BANNER_A, rng.choice([0, pool.take(), pool.take()]))
    img.u32(base + W_BANNER_B, rng.choice([0, 0, pool.take()]))
    img.u32(base + W_BANNER_X, rng.choice([0x1AB, 0x1AB, rng.randrange(0, 0x400)]))
    for i in range(5):
        img.u32(base + W_PROMPTS + 4 * i, keep(pool.take()))
    for slot in (W_NAME, W_GLOBE, W_INFO, W_GOALS, W_MENU, W_PERCENT):
        img.u32(base + slot, pool.take())
    img.u32(base + W_BACKGROUND, keep(pool.take()))
    for slot in W_FONTS:
        font_object(env, img, rng, base + slot)
    img.u32(base + W_STATE, rng.choice([0, 0, 1, 2]) if state is None else state)
    if park is None:
        park = rng.choice([0, 0, 1])
    park_object = 0
    if park:
        park_object = pool.take()
        img.u32(park_object + 0x5C, rng.choice([0, 1, 1]))
    img.u32(base + W_PARK, park_object)
    img.u8(base + W_DONE, rng.choice([0, 1]) if done is None else done)
    img.u32(base + W_ANSWER, rng.choice([0xFFFFFFFF, 0, 3, 7, 13, 0x22B]))


# ---- the screen callbacks and the window -----------------------------------------------------------------------------------------------------

@WIN(0x004588C0, "LevelSelect_InitCallbacks")
def gen_init(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[1]), 0x20, rng)
    return [A(env, OBJ[1])], img.pokes()


@WIN(0x004588E0, "LevelSelect_ExitCallback")
def gen_exit(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@WIN(0x00458B90, "LevelSelect_Construct", this=OBJ[0])
def gen_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), W_SIZE, rng)
    return [], img.pokes()


@WIN(0x004594E0, "LevelSelect_ScalarDelete", this=OBJ[0])
def gen_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@WIN(0x00459500, "LevelSelect_Destroy", this=OBJ[0])
def gen_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@WIN(0x004595E0, "LevelSelect_Draw", this=OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@WIN(0x00459650, "LevelSelect_Update", this=OBJ[0])
def gen_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    state = rng.choice([0, 0, 0, 0, 1, 1, 1, 2, 2, 5])
    only = {0: rng.choice(["left", "right", "confirm", "confirm", "back", "triangle", None]), 1: rng.choice(["back", None, None]), 2: None}.get(state)
    pad_edges(img, rng, only=only)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state, park=1 if state == 2 or (state == 1 and rng.random() < 0.2) else 0)
    return [], img.pokes()


@WIN(0x00459BE0, "LevelSelect_ChangeLevel", this=OBJ[0])
def gen_change_level(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0xFFFFFFFF, 1, 1, 0xFFFFFFFF, 0])], img.pokes()


@WIN(0x00459D90, "LevelSelect_Step", this=OBJ[0])
def gen_step(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    window_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [A(env, OBJ[1]) + 0x40], img.pokes()


# ---- the loop and its callback ---------------------------------------------------------------------------------------------------------------

@LOOP(0x004588F0, "LevelSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng, mode=1)
    img.u32(A(env, OBJ[1]), rng.getrandbits(32))
    return [A(env, OBJ[1]) + 8, A(env, OBJ[1])], img.pokes()


@LOOP(0x00458900, "LevelSelect_Main")
def gen_main(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng, mode=1)
    return [], img.pokes()


# ---- the banner ------------------------------------------------------------------------------------------------------------------------------

def banner_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, B_SIZE, pool, children=rng.randrange(0, 3))
    font_object(env, img, rng, base + B_FONT)
    for slot in (B_FACE, B_LINE_A, B_LINE_B):
        img.u32(base + slot, pool.take())


@BAN(0x00459DB0, "LevelBanner_Construct", this=OBJ[0])
def gen_banner_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), B_SIZE, rng)
    return [rng.choice([0, 1]), rng.choice([0x1AB, 0x1AB, 0, rng.randrange(0, 0x300)]), rng.choice([0x77, 0xB4, 0x76, 0]), rng.choice([0x280, 0xFFFFFFFF]), 0xFFFFFFFF], img.pokes()


@BAN(0x0045A0A0, "LevelBanner_ScalarDelete", this=OBJ[0])
def gen_banner_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    banner_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@BAN(0x0045A0C0, "LevelBanner_Destroy", this=OBJ[0])
def gen_banner_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    banner_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


NAME_WORDS = [b"Tony Hawk", b"  Bob  Burnquist", b"Elissa Steamer", b"Jamie-Thomas", b"Supercalifragilistic Name", b"A", b"", b"   ", b"Rodney Mullen Jr", b"Eric Koston-Smith",
              b"Private Carrera", b"Chad Muska", b"Bucky Lasek", b"Steve Caballero", b"Kareem Campbell", b"Geoff Rowley", b"x y z", b"Andrew Reynolds Sr III",
              b"Spider-Man", b"verylongwordwithoutanyspacesatall here", b"ab cd-ef gh", b"-hyphen first", b"nine char"]


@BAN(0x0045A120, "Level_CopyNameWord")
def gen_copy_word(env, n):
    img, rng = new_image(env)
    source = A(env, OBJ[2])
    name = rng.choice(NAME_WORDS)
    img.put(source, b" " * rng.choice([0, 0, 1, 3]) + name + b"\0" + bytes(rng.randrange(256) for _ in range(8)))
    return [A(env, OBJ[1]) + 0x20, source, rng.choice([0, 1])], img.pokes()


# ---- the globe -------------------------------------------------------------------------------------------------------------------------------

def globe_state(env, img, rng, base, pool, *, free_run=None):
    img.garbage(base, G_SIZE, rng)
    img.u32(base, A(env, VT_WINDOW))      # (the fake vtable whose per-frame hook is a reported service)
    img.u32(base + G_CROSSHAIR, pool.take())
    img.u32(base + G_REGION, rng.randrange(0, 8))
    long_now = rng.choice([0, 100, 0xFFF, 0x800, 0x7FF, rng.randrange(0, 0x1000), 0xFFFFFFF0, 0x1000])
    img.u32(base + G_LONG, long_now & 0xFFFFFFFF)
    img.u32(base + G_LONG_TARGET, rng.choice([long_now, long_now + 2, long_now - 3, long_now + 0x900, long_now - 0x900, rng.randrange(0, 0x1000), 0x7FF, 0x1000]) & 0xFFFFFFFF)
    lat = rng.choice([0, 5, 100, -100, rng.randrange(-300, 300)])
    img.u32(base + G_LAT, lat & 0xFFFFFFFF)
    target = 0xFFFFFFFF if (free_run if free_run is not None else rng.random() < 0.15) else rng.choice([lat, lat + 2, lat - 3, lat + 50, lat - 50, 0]) & 0xFFFFFFFF
    img.u32(base + G_LAT_TARGET, target)


@GLB(0x0045A1B0, "LevelGlobe_Construct", this=OBJ[0])
def gen_globe_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), G_SIZE, rng)
    return [rng.choice([0x16F, 0x16F, 0, rng.randrange(0, 0x200)]), rng.choice([0x46, 0x46, 0, 0x10]), 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@GLB(0x0045A3E0, "LevelGlobe_ScalarDelete", this=OBJ[0])
def gen_globe_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    globe_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@GLB(0x0045A400, "LevelGlobe_Destroy", this=OBJ[0])
def gen_globe_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    globe_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@GLB(0x0045A490, "LevelGlobe_Update", this=OBJ[0])
def gen_globe_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    globe_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@GLB(0x0045A5A0, "LevelGlobe_Draw", this=OBJ[0])
def gen_globe_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    globe_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 0x100, 0xFFFFFF00, rng.randrange(-100, 700) & 0xFFFFFFFF]), rng.choice([0, 0x40, rng.randrange(-50, 400) & 0xFFFFFFFF])], img.pokes()


@GLB(0x0045A6B0, "LevelGlobe_SetLevel", this=OBJ[0])
def gen_globe_set_level(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    globe_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.randrange(LEVEL_COUNT)], img.pokes()


# ---- the level name window -------------------------------------------------------------------------------------------------------------------

def name_state(env, img, rng, base, pool, *, destroy=False, state=None):
    window(env, img, rng, base, N_SIZE, pool, children=rng.randrange(0, 3))
    font_object(env, img, rng, base + N_FONT)
    img.u32(base + N_NAME, flagged_text(env, img, rng, pool))
    img.u32(base + N_LOCATION, flagged_text(env, img, rng, pool))
    img.u32(base + N_SLIDE, rng.choice([0x14, 0x14, 0, 0x40]))
    img.u32(base + N_SHOWN, rng.randrange(LEVEL_COUNT))
    img.u32(base + N_WANTED, rng.randrange(LEVEL_COUNT))
    img.u32(base + N_STATE, rng.choice([0, 1, 2, 2, 0]) if state is None else state)


@NAM(0x0045A6E0, "LevelName_Construct", this=OBJ[0])
def gen_name_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    lev_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), N_SIZE, rng)
    # (the constructor never sets the shown level, and its first set-level reads the width of that level's name: the block holds a leftover level number,
    # as a recycled arena block of the game does; a wild number would read wild memory)
    img.u32(A(env, OBJ[0]) + N_SHOWN, rng.randrange(LEVEL_COUNT))
    return [rng.choice([0x38, 0, 0x100]), rng.choice([0x23, 0, 0x40]), 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@NAM(0x0045A920, "LevelName_ScalarDelete", this=OBJ[0])
def gen_name_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    name_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@NAM(0x0045A940, "LevelName_Destroy", this=OBJ[0])
def gen_name_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    name_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@NAM(0x0045A9A0, "LevelName_SetLevel", this=OBJ[0])
def gen_name_set_level(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    name_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.randrange(LEVEL_COUNT)], img.pokes()


@NAM(0x0045AA40, "LevelName_Update", this=OBJ[0])
def gen_name_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    state = rng.choice([0, 0, 1, 2, 2, 2])
    name_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), state=state)
    return [], img.pokes()


@NAM(0x0045AB50, "LevelName_Refresh", this=OBJ[0])
def gen_name_refresh(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    name_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


# ---- the mode menu ---------------------------------------------------------------------------------------------------------------------------

def menu_state(env, img, rng, base, pool, *, destroy=False, hidden=None, highlighted=None):
    """A mode menu: the window part, a fake list over four real text rows, a fake style and the result words."""
    for i in range(4):
        poke_props(env, img, i, rng)
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, M_SIZE, pool, hidden=hidden, children=rng.randrange(0, 3))
    rows = []
    for slot_no, ident in enumerate([1, 2, 3, 4], start=2):
        rows.append(item_row(env, img, rng, slot_no, ident, real_text=True, vtable=MENU_REAL["text"], selectable=1))
    pick = rng.randrange(len(rows)) if highlighted is None else highlighted
    img.u32(base + M_LIST, keep(list_object(env, img, rng, 0, rows, highlighted=pick)))
    for i in range(0x60):
        img.u32(base + M_ROWS + 4 * i, rows[i] if i < len(rows) else 0)
    img.u32(base + M_STYLE, keep(pool.take()))
    img.u32(base + M_RESULT, rng.choice([0, 1, 2, 3, 4]))
    img.u8(base + M_FLAG, rng.choice([0, 1]))
    img.u32(base + M_PAD, rng.choice([0, 1]))
    return pick


@MNU(0x0045ABC0, "ModeMenu_Construct", this=OBJ[0])
def gen_menu_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), M_SIZE, rng)
    return [A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)), rng.choice([0x100, 0, 0x80]), rng.choice([0x78, 0, 0x20])], img.pokes()


@MNU(0x0045AEB0, "ModeMenu_ScalarDelete", this=OBJ[0])
def gen_menu_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    menu_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@MNU(0x0045AED0, "ModeMenu_Destroy", this=OBJ[0])
def gen_menu_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    menu_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@MNU(0x0045AF60, "ModeMenu_HighlightPlay", this=OBJ[0])
def gen_menu_highlight(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    menu_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@MNU(0x0045AF70, "ModeMenu_Draw", this=OBJ[0])
def gen_menu_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u8(FLAG_BYTE, rng.choice([0, 1]))
    for address in (0x0056B029, 0x0056B195):
        img.u8(address, rng.choice([0, 0, 1]))
    menu_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), hidden=rng.choice([0, 0, 0, 1]))
    return [coords(rng), coords(rng)], img.pokes()


@MNU(0x0045B080, "ModeMenu_Result", this=OBJ[0])
def gen_menu_result(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    menu_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [A(env, OBJ[1]) + 0x40], img.pokes()
