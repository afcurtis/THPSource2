"""Phase 4.10f case generators, part 1: what the player-select screen's groups share (playsel.cpp and the window classes of player_ui.cpp).
Part 2 (frontend_specs_playsel_cases.py) holds the generators of each group.

The slab is the 4.10e1 one (frontend_specs_screens.py: the menu group's fake vtables, fonts and strings, the pages and lists of 4.10d, the fake object pool
and VT_ANY, the real ConfirmBox state builders) with more room behind it for the select screen's larger objects:

  0x1c000  six object slots of 0x1400 bytes (the object under test, a skater view needs 0x13dc)    0x23800  sixteen real element records of 0x40 bytes
  0x24000  four fake lists of 0x1c0 bytes     0x24800  out buffers (0x800)     0x25000  sixty-four fake rows of 0x100 bytes    0x29000  the allocation arena
  (every arena block starts with VT_ANY, the fake vtable whose methods are reported services; slot 4 is the skater view's load method here)

Groups: uiw (the window classes of player_ui.cpp: the score counter, the medal window, the prompt window, the flagged text element, the callbacks),
sel (the player-select window's functions and the small methods of the classes it calls), selloop (the screen loop).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import ELEM_HOOK, CHILD_DRAW, CHILD_UPDATE, FIND_FONTS, FONT_SIZE, STRING_POOL, VIRTUAL_DELETE, string_at
from frontend_specs_screens import (VT_ANY, READ_ONLY_COMMON, install_screen_stubs, regions as screens_regions, seed_screens)

PSLAB = 0x38000
PS_OBJ = [0x1C000 + 0x1400 * i for i in range(6)]
PS_ELEM = 0x23800
PS_ELEM_STRIDE = 0x40
PS_LISTS = 0x24000
PS_LIST_STRIDE = 0x1C0
PS_OUT = 0x24800
PS_ROWS = 0x25000
PS_ROW_STRIDE = 0x100
PS_ARENA = 0x29000
PS_ARENA_SIZE = PSLAB - PS_ARENA

VT_TEXT_REAL = 0x005196E8
VT_SPRITE_REAL = 0x00519708
SLOT4 = 0x004ADC70            # an unreached engine entry point: slot 4 of VT_ANY (the skater view's load method)
LEVEL_TABLE = 0x0053C1F8      # the static level table (the medal window reads each row's contest flag at +0x48)
ROWS = 0x0053A3B8             # the character rows (0x170 bytes each): mutable in the select groups (Init writes the custom skaters' availability)
ROWS_SIZE = 0x1D80

# the retail globals this family reads or writes beyond the screens' set
SELECTION = 0x0056A884        # the player-select state kept between visits
START_IN_MENU = 0x00561CA4
PLAYER_COUNT = 0x00561C60
REPLAY_STATE = 0x00561C74
EXIT_CODE = 0x00561C78
RESOURCE_SLOTS = 0x00568A2C
SPOOL_FLAGS = 0x0056A2F0
STATIC_EXTRA = [(LEVEL_TABLE, 0x1AC * 13), (0x00536FF0, 0x60), (0x00537000, 0x240), (0x0052E5F0, 0x40)]
SELECT_STATIC = [r for r in list(READ_ONLY_COMMON) if r[0] != ROWS] + [(0x00537000, 0x240), (0x0052E5F0, 0x40), (0x005252C0, 0x40)]


def PA(env, offset):
    return env.slab + offset


def pregions(slab):
    base = [r for r in screens_regions(slab) if r[0] != slab + 0x18000]
    return base + [(slab + 0x18000, PSLAB - 0x18000), (SELECTION, 4)]


def sregions(slab):
    return pregions(slab) + [(ROWS, ROWS_SIZE), (START_IN_MENU, 4), (PLAYER_COUNT, 4), (REPLAY_STATE, 8), (RESOURCE_SLOTS, 8), (SPOOL_FLAGS, 8)]


def seed_play(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_screens(env, rng)
    # (the 4.10e1 seed filled 0x1c000..0x24000 with fake-vtable pointers for its own arena: this slab keeps its object slots there, zeroed)
    write_chunks(env.slab + PS_OBJ[0], bytes(PS_ARENA - PS_OBJ[0]))
    env.alloc_base = PS_ARENA
    env.alloc_size = PS_ARENA_SIZE
    block = struct.pack("<I", env.slab + VT_ANY)
    for offset in range(0, PS_ARENA_SIZE, 16):
        write_chunks(env.slab + PS_ARENA + offset, block)
    write_chunks(env.slab + VT_ANY + 0x10, struct.pack("<I", SLOT4))     # slot 4
    return writes


def register(group, *, select=False):
    SLAB_BYTES[group] = PSLAB
    EXTRA_REGIONS[group] = sregions if select else pregions
    READ_ONLY[group] = (SELECT_STATIC if select else list(READ_ONLY_COMMON) + STATIC_EXTRA)
    SEEDS[group] = seed_play


register("uiw")
for _group in ("sel", "selloop"):
    register(_group, select=True)


# ---- stubs -------------------------------------------------------------------------------------------------------------------------------------

def disable(fc, *names):
    for probe in fc._probes:
        if getattr(probe, "name", "") in names:
            probe.enabled = False


def set_result(fc, name, result):
    for probe in fc._probes:
        if getattr(probe, "name", "") == name:
            probe.result = result


def install_play_stubs(fc, env, *, real_prompt=False):
    if getattr(fc, "_play_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._play_installed = True
    rng = env.rng
    install_screen_stubs(fc, env)
    if real_prompt:
        # the prompt window's own constructor is a function under test here: the menu group's seam for it must not intercept it
        disable(fc, "prompt_window_construct")
    fc.stub(SLOT4, 0, "element_load", convention="thiscall", this=True)
    # the real per-frame hook of the window-base classes (slot 7 of a real vtable) is an engine function: a reported service like the fake vtables' slot 7
    for hook in (0x004DC9F0, 0x004DCA60, 0x004DCB30):      # the window's, the text element's and the sprite element's
        fc.stub(hook, 0, "element_hook", convention="thiscall", this=True)
    # the engine sprite object's size methods (the sprite element's width and height are real code that calls them)
    fc.stub(0x00457820, 0, "sprite_width", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    fc.stub(0x00457830, 0, "sprite_height", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    # the prompt window's button art and caption lookups, and its registration with the engine
    fc.stub(0x004DC050, 1, "engine_004dc050", result=lambda a: string_at(env, rng.choice([0, 1, 3, 4, 5, 7, 9, 12, 13])))
    fc.stub(0x004DC0B0, 1, "engine_004dc0b0", result=lambda a: string_at(env, rng.choice([0, 1, 3, 4, 5, 7, 9, 12, 13, 15])))
    fc.stub(0x004DCBC0, 1, "engine_004dcbc0", convention="thiscall", this=True)


SELECT_ENGINE = [
    (0x004DE150, 0, False, lambda env: (lambda a: env.rng.choice([0, 0, 0, 1]))),                   # Pack_NewFree
    (0x004A9510, 1, True, lambda env: (lambda a: env.rng.choice([0, 1]))),                           # the pre-file manager: is it loaded
    (0x004A3E80, 6, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the skater view's constructor
    (0x004A6F50, 2, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the load window's
    (0x004A5450, 4, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the page dots'
    (0x004A56B0, 5, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the header's
    (0x004A1670, 5, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the medal window's
    (0x004A3830, 4, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the bio window's
    (0x004A5890, 4, True, lambda env: (lambda a: env.alloc_log[-1][0])),                              # the main menu window's
    (0x004A7590, 0, True, lambda env: (lambda a: env.rng.choice([0, 0, 1, 2]))),                      # the load window's step
    (0x004A7480, 0, True, None),                                                                      # entering the load state
    (0x004A6880, 1, True, None),                                                                      # the spool's per-character refresh
    (0x004E02D0, 0, False, None),
    (0x0048A320, 0, False, None),                                                                     # Pal_Cleanup
    (0x0048A0B0, 2, False, None),                                                                     # Pal_Init
]


def install_select_stubs(fc, env):
    if getattr(fc, "_select_installed", False):
        return
    fc._select_installed = True
    from opentony.memory import mem
    from frontend_specs_screens import TICK

    rng = env.rng
    install_play_stubs(fc, env)
    for address, count, thiscall, result in SELECT_ENGINE:
        options = {"result": result(env)} if result is not None else {}
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall, **options)
    # (the 4.10e1 stub of the custom skaters' free-all gives no result: the view reads it)
    set_result(fc, "engine_00423b50", lambda a: rng.choice([0, 0, 1]))
    fc.stub(0x004C8F10, 2, "engine_004c8f10")
    # Game_FrameTick moves the clock on
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])


def install_select_loop_stubs(fc, env):
    """The loop: the window's constructor, step, update, draw and delete are seams with scripted answers."""
    rng = env.rng
    install_select_stubs(fc, env)
    fc.stub(0x004A2050, 1, "engine_004a2050", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x004A36E0, 0, "engine_004a36e0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2]))
    fc.stub(0x004A3110, 0, "engine_004a3110", convention="thiscall", this=True)
    fc.stub(0x004A2CE0, 0, "engine_004a2ce0", convention="thiscall", this=True)
    fc.stub(0x004A20E0, 1, "engine_004a20e0", convention="thiscall", this=True)


GROUP_STUBS = {"uiw": lambda fc, env: install_play_stubs(fc, env, real_prompt=True), "sel": install_select_stubs, "selloop": install_select_loop_stubs}


# ---- builders ----------------------------------------------------------------------------------------------------------------------------------

def element_at(env, index):
    return PA(env, PS_ELEM + PS_ELEM_STRIDE * index)


def real_text_element(env, img, rng, index, *, string=None, font=None):
    """A TextScreenElement with its real vtable, a legal string and a real font (the width is computed from them)."""
    base = element_at(env, index)
    img.garbage(base, PS_ELEM_STRIDE, rng)
    img.u32(base, VT_TEXT_REAL)
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x28, string_at(env, rng.randrange(len(STRING_POOL))) if string is None else string)
    img.u32(base + 0x2C, PA(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)) if font is None else font)
    return base


def real_sprite_element(env, img, rng, index):
    """A SpriteScreenElement with its real vtable and a fake engine sprite object."""
    from frontend_specs_menu import sprite_at

    base = element_at(env, index)
    img.garbage(base, PS_ELEM_STRIDE, rng)
    img.u32(base, VT_SPRITE_REAL)
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x28, sprite_at(env, rng.randrange(8)))
    return base
