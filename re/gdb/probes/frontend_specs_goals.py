"""Phase 4.10h probe, part 1: the groups of the goals, game-mode and competition screens, their regions, seeds and stubs (the generators are in frontend_specs_goals_cases.py).

Groups: gcmp (competition.cpp's object and its logic), gcdisp (its displays), gmdl (the medal spawner and the tape constructor), gtag (tag.cpp), gvwn (the level
information window's methods), glvp (the level picture window), gprl (the TRG type 500 node's record), gled (the light editor), glds (the loading screen) and ggls (the goals
checklist screen).

The competition, medal and tag groups work on the career world (the career records, the skater manager, the level table, the two player objects, a few dozen globals), which the
4.8 probe captured: its regions and its random-state sampler (career_state, career_specs) are reused, and the scratch slab of the screens' probe (frontend_specs_screens.py)
holds the fake objects (the medals, the pictures, the windows). The window groups use the screens' regions and stubs as they are.

Slab additions (the screens' slab is 0x24000 bytes): 0x1a140 a fake medal vtable (delete, -, -, update), 0x1a160 a fake picture vtable (delete, -, load, draw).
"""

from __future__ import annotations

import struct

import career_state as cs
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import ELEM_HOOK, FIND_FONTS, FONT_SIZE, VIRTUAL_DELETE
from frontend_specs_screens import (CHILD_DRAW, OBJ, POOL, RENDERER, SLAB, TICK, VT_ANY, READ_ONLY_COMMON, install_screen_stubs, regions as screens_regions,
                                    seed_screens)

VT_MEDAL = 0x1A140
VT_PIC = 0x1A160
MEDAL_UPDATE = 0x004ADC80       # unreached engine functions: the fake vtables' targets (nothing refers to them)
PICTURE_LOAD = 0x004ADC90
SCRATCH_LINE = 0x7FF00400
FONT_TAIL = 0x21000            # the fake glyph font of the mess font, in the arena's last 0x3000 bytes
CREATED = 0x22000              # three medal-sized fake objects (stride 0x200) that Powerup_Construct hands back: the medals a display creates (their vtable is the fake medal's)
FAKE_PLAYER = 0x24000          # the tag game's second player (the running level has one): 0x3000 bytes, past the screens' slab
TAG_SLAB = 0x28000

COMP_GROUPS = ("gcmp", "gcdisp", "gmdl", "gtag")
WIN_GROUPS = ("gvwn", "glvp", "gprl", "gled", "glds", "ggls")
ALL_GROUPS = COMP_GROUPS + WIN_GROUPS

# the career world's extra mutable words (the competition object, its display flags, the medals' totals, the GTE, the tag game, the loading screen)
GOAL_WORLD = [
    (0x0056A858, 0x10),        # the player pointers
    (0x0056A41C, 0x60),        # a bit per level whose medal screen was seen, for each character
    (0x0056A94C, 4),           # the video the career plays next
    (0x00534110, 4),           # the screen to go on to after it
    (0x00561B00, 0x300),       # the end-of-competition flags
    (0x00518638, 0x20),        # which human sits where
    (0x00568A2C, 0x20),        # the human seats' manager slots
    (0x0056DC80, 0x40),        # the model variants of the medals
    (0x0056DB40, 0x40),        # the tag game
    (0x0056B790, 0x60),        # the tag game's counts
    (0x005685F4, 4), (0x006A3A90, 4), (0x006A18B0, 4), (0x006A3D48, 4), (0x00568628, 4),
    (0x0056E31C, 4),           # the clock
    (0x006A3E00, 0xD0),        # the GTE registers
    (0x005620E0, 4),           # the camera record the medals read
    (0x00560FD0, 8),           # the draw list's cursor and the renderer
    (0x029DA394, 0x10),        # the screen size and the software flag
    (0x00525774, 0x10),        # the score anchors and the final video
    (0x0056A9A0, 0x14C),       # the mess font (its glyph table is faked in the slab)
]
# the loading screen's, the light editor's and the goals screen's statics
GOAL_STATICS = [(0x00561980, 0x20), (0x005618C8, 4), (0x0052E088, 4), (0x005614C0, 8), (0x00562000, 0x0A0), (0x005244CC, 4), (0x0056A960, 4), (0x006A1417, 1), (0x0056B1E0, 0x80), (0x0056B250, 8), (0x0056AFF8, 0x1D0), (0x00533F3C, 4),
                (0x00561C90, 0x20), (0x0056114C, 4), (0x006A3A9C, 4), (0x0056B8D4, 4), (0x0056A8E8, 4), (0x0056A89C, 4)]


def career_read_only():
    """The 4.8 probe's static regions without asking the running game (career_state.read_only_regions reads the trick database pointer)."""
    out = list(cs.READ_ONLY)
    cursor = cs.SKATER_TABLE[0]
    for base, size in cs.SKATER_WINDOWS:
        if base > cursor:
            out.append((cursor, base - cursor))
        cursor = base + size
    if cursor < cs.SKATER_TABLE[1]:
        out.append((cursor, cs.SKATER_TABLE[1] - cursor))
    return out


# the texts the displays format and the pointer cells they read, apart from the mutable words above
GOAL_TEXT = [(0x0052E000, 0x1000), (0x00525760, 0x14), (0x00525784, 0x17C), (0x005252C0, 0x60), (0x00527D54, 4), (0x0052DF00, 0x100), (0x00531400, 0x600), (0x00533400, 0x600), (0x00531A00, 0x1A00), (0x0053E9C0, 0x300),
             (0x00526ACC, 0x10), (0x0055FE10, 0x10)]


def merged(items, gap=0):
    items = sorted(items)
    out = []
    for base, size in items:
        if out and base <= out[-1][0] + out[-1][1] + gap:
            out[-1][1] = max(out[-1][0] + out[-1][1], base + size) - out[-1][0]
        else:
            out.append([base, size])
    return [(b, s) for b, s in out]


def without(static, holes):
    """The read-only ranges with the mutable words cut out of them."""
    out = []
    for base, size in static:
        pieces = [(base, base + size)]
        for hole, length in holes:
            cut = []
            for lo, hi in pieces:
                if hole < hi and lo < hole + length:
                    if lo < hole:
                        cut.append((lo, hole))
                    if hole + length < hi:
                        cut.append((hole + length, hi))
                else:
                    cut.append((lo, hi))
            pieces = cut
        out += [(lo, hi - lo) for lo, hi in pieces]
    return out


def comp_read_only():
    drop = lambda r: not (0x0052E000 <= r[0] < 0x0052F000)         # (the text cells are in GOAL_TEXT's block)
    return merged(without([r for r in career_read_only() if drop(r)] + GOAL_TEXT, GOAL_WORLD + GOAL_STATICS + list(cs.LIVE) + list(cs.STATIC) + list(cs.SKATER_WINDOWS)), gap=0)


def comp_regions(slab):
    """The career world (the 4.8 probe's regions, which read the live player objects), the world above, and the slab's fake-object area."""
    out = list(cs.regions(["goals"]))
    for player in (cs.mem.u32(cs.PLAYERS), cs.mem.u32(cs.PLAYERS + 4)) if hasattr(cs, "mem") else ():
        if player:
            out += [(player + 8, 0x10), (player + 0x29B0, 4), (player + 0x2A8, 4), (player + 0x2CC4, 8), (player + 0x2F64, 4), (player + 0x174, 4)]
    out += GOAL_WORLD + [(slab + 0x18000, SLAB - 0x18000)]
    result = merged(out, gap=0x40)
    check_disjoint(result, merged(READ_ONLY["gcmp"], gap=0))
    return result


def check_disjoint(result, static):
    for base, size in result:
        for sbase, ssize in static:
            if base < sbase + ssize and sbase < base + size:
                raise RuntimeError(f"mutable region {base:#x}+{size:#x} overlaps the read-only {sbase:#x}+{ssize:#x}")


def tag_regions(slab):
    return merged(comp_regions(slab) + [(slab + FAKE_PLAYER, 0x3000)], gap=0)


def win_regions(slab):
    out = list(screens_regions(slab)) + list(cs.regions(["goals"]))
    for player in (cs.mem.u32(cs.PLAYERS), cs.mem.u32(cs.PLAYERS + 4)):
        if player:
            out += [(player + 8, 0x10), (player + 0x16C, 0x10), (player + 0x2CC0, 4)]
    out += GOAL_WORLD + GOAL_STATICS
    # (the line and file words an assertion records are cut out: the merge's gap would otherwise take them in, and the port reports only a failed check)
    result = without(merged(out, gap=0x40), [(0x00568638, 4), (0x0056881C, 4)])
    check_disjoint(result, merged(READ_ONLY["gvwn"], gap=0))
    return result


def seed_mess_font(env, rng):
    """The mess font (a game global) gets a fake glyph table in the arena's tail, as the 4.10a message probe does: widths and ellipses are then measured on known glyphs."""
    from frontend_specs_font import GLYPH_COUNT
    from frontend_specs_mess import MESS_FONT, write_fake_font
    from opentony.fcall import ForcedCalls, write_chunks

    glyphs = write_fake_font(env, rng, env.slab + FONT_TAIL, GLYPH_COUNT)
    obj = bytearray(0x14C)
    for offset, value in ((0x04, 1), (0x08, 1), (0x0C, 1), (0x10, 1), (0x14, 0xFF), (0x18, 0xFF), (0x1C, 0xFF), (0x30, 0x1000), (0x34, glyphs), (0x38, GLYPH_COUNT), (0x3C, 0xFFFFFFFF)):
        struct.pack_into("<I", obj, offset, value)
    write_chunks(MESS_FONT + 4, bytes(obj[4:0x4B]))
    ForcedCalls.call(0x0044ABB0, (0,), this=MESS_FONT)
    env.alloc_size = min(env.alloc_size, FONT_TAIL - 0x1C000)       # (the arena stops where the glyphs begin)


def seed_goals(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_screens(env, rng)
    seed_mess_font(env, rng)
    medal = bytearray(0x20)
    for slot, entry in ((0, VIRTUAL_DELETE), (3, MEDAL_UPDATE)):
        struct.pack_into("<I", medal, 4 * slot, entry)
    write_chunks(env.slab + VT_MEDAL, bytes(medal))
    picture = bytearray(0x20)
    for slot, entry in ((0, VIRTUAL_DELETE), (2, PICTURE_LOAD), (3, 0x004D7990)):
        struct.pack_into("<I", picture, 4 * slot, entry)
    write_chunks(env.slab + VT_PIC, bytes(picture))
    for k in range(3):
        write_chunks(env.slab + CREATED + 0x200 * k, struct.pack("<I", env.slab + VT_MEDAL) + bytes(0x10C))
    return writes


for _group in COMP_GROUPS:
    SLAB_BYTES[_group] = SLAB
    READ_ONLY[_group] = comp_read_only()
    EXTRA_REGIONS[_group] = comp_regions
    SEEDS[_group] = seed_goals
SLAB_BYTES["gtag"] = TAG_SLAB
EXTRA_REGIONS["gtag"] = tag_regions
for _group in WIN_GROUPS:
    SLAB_BYTES[_group] = SLAB
    READ_ONLY[_group] = merged(without([r for r in READ_ONLY_COMMON if not (0x0052E000 <= r[0] < 0x0052F000)], GOAL_WORLD + GOAL_STATICS + list(cs.LIVE) + list(cs.STATIC)
                                       + list(cs.SKATER_WINDOWS)) + comp_read_only(), gap=0)
    EXTRA_REGIONS[_group] = win_regions
    SEEDS[_group] = seed_goals


# ---- stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------

def present(fc, address):
    return [p for p in fc._probes if getattr(p, "address", None) == address and p.enabled]


def ensure(fc, address, args, name=None, **kw):
    """One stub for an address: an earlier one of the same name is kept, a different one is replaced."""
    name = name or "engine_%08x" % address
    for probe in present(fc, address):
        if probe.name == name and probe.args == args:
            return probe
        probe.enabled = False
    fc.stub(address, args, name, **kw)
    return None


def ensure_engine(fc, address, args, thiscall=False, **kw):
    kw.setdefault("convention", "thiscall" if thiscall else "cdecl")
    if thiscall:
        kw.setdefault("this", True)
    return ensure(fc, address, args, **kw)


MESS = [(0x00473650, 0), (0x004734F0, 1), (0x004734B0, 1)]


def install_mess(fc, env):
    for address, count in MESS:
        ensure_engine(fc, address, count)
    ensure_engine(fc, 0x00473580, 4)                                       # Mess_SetRGB(r, g, b, 0)
    ensure_engine(fc, 0x004739E0, 4, text=2, stack_to=SCRATCH_LINE)        # Mess_DrawText(x, y, text, 0)
    ensure_engine(fc, 0x004736C0, 1, text=True)                            # Mess_DeleteByText(text)
    ensure_engine(fc, 0x004DC0C0, 0)


def install_comp_world(fc, env):
    """The services the competition, the medals and the tags reach."""
    from opentony.memory import mem

    rng = env.rng
    if getattr(fc, "_goals_world", False):
        return
    fc._goals_world = True
    from frontend_specs_menu import install_menu_stubs

    if not getattr(fc, "_screens_installed", False):      # (the screens' set contains the menu stubs)
        install_menu_stubs(fc, env, font_keep=None, frame_masks={4: 0xFF, 5: 0xFF, 6: 0xFF})
    install_mess(fc, env)
    ensure(fc, 0x004AD620, 3, "sfx")
    ensure_engine(fc, 0x00500400, 5, convention="stdcall")
    ensure_engine(fc, 0x0050030C, 4, convention="stdcall")
    ensure_engine(fc, 0x004C9320, 1)
    ensure_engine(fc, 0x004498F0, 0)
    ensure_engine(fc, 0x00449800, 0, result=lambda a: rng.choice([0, 1, 1]))
    ensure_engine(fc, 0x004498A0, 2)
    ensure_engine(fc, 0x00452A80, 1, result=lambda a: rng.choice([0, 1]))
    ensure_engine(fc, 0x0042F370, 0)
    ensure_engine(fc, 0x00454E60, 0)
    ensure_engine(fc, 0x00489610, 0)
    ensure_engine(fc, 0x00489670, 1)
    ensure_engine(fc, 0x00489850, 2)
    ensure_engine(fc, 0x0044B800, 1, thiscall=True, text=True, result=lambda a: rng.randrange(0x1000, 0x2000))
    ensure_engine(fc, 0x0044B770, 1, thiscall=True)
    # the medal objects' construction: the GTE multiply writes its three results, the storage and the power-up constructor hand back arena blocks
    ensure_engine(fc, 0x004E2EF0, 5, writes=[(0x006A3EB8 + 4 * k, 4, lambda a: rng.randrange(-0x40000, 0x40000)) for k in range(3)])
    ensure_engine(fc, 0x0047FD30, 1, result=env.alloc)
    env.created = 0

    def created_medal(a):
        # the constructor's answer is one of the fake medals (the arena's blocks answer a 3-argument draw in the slot the display calls with none)
        env.created = min(env.created + 1, 3)
        return env.slab + CREATED + 0x200 * (env.created - 1)

    ensure_engine(fc, 0x004A7C50, 6, thiscall=True, stack_to=SCRATCH_LINE, blob=(1, 12), result=created_medal)
    fc.stub(MEDAL_UPDATE, 0, "medal_update", convention="thiscall", this=True)
    # the tape constructor
    ensure_engine(fc, 0x00480060, 0, thiscall=True)
    ensure_engine(fc, 0x0047FE30, 1, thiscall=True, keep=slice(0, 0), text=True)
    ensure_engine(fc, 0x004B1DE0, 2, result=lambda a: rng.randrange(0, 0x10000))
    ensure_engine(fc, 0x004801D0, 1, thiscall=True)
    ensure_engine(fc, 0x004B93A0, 0, thiscall=True)
    # the tag game
    ensure_engine(fc, 0x004B83B0, 2, text=1)
    ensure_engine(fc, 0x0046CE60, 0, thiscall=True)
    ensure_engine(fc, 0x004DE7C0, 3)
    ensure_engine(fc, 0x004CABF0, 3, stack_to=SCRATCH_LINE, ret_arg=0, fill=(0, lambda a: struct.pack("<iii", *(rng.randrange(-0x40000, 0x40000) for _ in range(3)))))
    ensure_engine(fc, 0x004CA8F0, 0, thiscall=True, stack_to=SCRATCH_LINE, blob=(-1, 12), result=lambda a: rng.randrange(0, 0x400))
    ensure_engine(fc, 0x004904D0, 2, thiscall=True)
    ensure_engine(fc, 0x004F3980, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))
    ensure_engine(fc, 0x004F39B0, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))
    ensure_engine(fc, 0x0048AB20, 5)
    ensure_engine(fc, 0x00465F60, 1, stack_to=SCRATCH_LINE, blob=(0, 12), fill=(0, lambda a: struct.pack("<iii", *(rng.randrange(-0x1000, 0x1001) for _ in range(3)))))
    ensure_engine(fc, 0x004E8560, 2, result=lambda a: rng.randrange(0, 0x1000))
    ensure_engine(fc, 0x004DE010, 1, result=lambda a: rng.choice([0, 1]))
    ensure_engine(fc, 0x004DE040, 0, result=lambda a: rng.choice([0, 1, 1]))


def install_win_world(fc, env):
    """The window groups' services: the screens' set, the mess calls and the engine functions of the windows and the loading screen."""
    from opentony.memory import mem

    rng = env.rng
    if getattr(fc, "_goals_win", False):
        return
    fc._goals_win = True
    install_screen_stubs(fc, env)
    install_mess(fc, env)
    # the level information window
    ensure_engine(fc, 0x00489450, 2, masks={0: 0xFFFF, 1: 0xFFFF})
    ensure_engine(fc, 0x004E6F00, 1)
    ensure_engine(fc, 0x004E8760, 3, stack_to=SCRATCH_LINE, blob=(0, 8))
    ensure_engine(fc, 0x00454D30, 1)
    ensure_engine(fc, 0x00454E50, 1)
    ensure_engine(fc, 0x00454E60, 0)
    ensure_engine(fc, 0x00454E70, 1)
    ensure_engine(fc, 0x00457150, 0, thiscall=True)
    ensure_engine(fc, 0x00457130, 0, thiscall=True)
    ensure_engine(fc, 0x00457810, 0, thiscall=True)
    ensure_engine(fc, 0x00457820, 0, thiscall=True, result=lambda a: rng.choice([0x40, 0x80, 0x100]))
    ensure_engine(fc, 0x00457830, 0, thiscall=True, result=lambda a: rng.choice([0x20, 0x40, 0x80]))
    # the level picture window
    ensure_engine(fc, 0x00449030, 1, text=True, result=lambda a: 1 + rng.randrange(0x1000))
    ensure_engine(fc, 0x00449230, 1)
    ensure_engine(fc, 0x00449660, 0)
    ensure_engine(fc, 0x00457540, 4, thiscall=True, result=lambda a: env.alloc_log[-1][0])
    ensure_engine(fc, 0x004B51B0, 3, stack_to=SCRATCH_LINE,
                  pokes=[(0, None, lambda a: struct.pack("<14I", *rng.sample(range(14), 14)))])
    ensure_engine(fc, 0x004B5370, 4, thiscall=True, text=True, stack_to=SCRATCH_LINE, result=lambda a: rng.randrange(0, 0x100))
    ensure_engine(fc, 0x004B52B0, 0, thiscall=True)
    ensure_engine(fc, 0x004B58A0, 1, thiscall=True, result=lambda a: rng.choice([0, 0, 0, 1]))
    ensure_engine(fc, 0x004B5990, 1, thiscall=True, result=lambda a: env.slab + OBJ[3] + 0x400 * rng.randrange(1, 2))
    fc.stub(PICTURE_LOAD, 1, "picture_load", convention="thiscall", this=True)
    # the type 500 node's record
    ensure_engine(fc, 0x00404020, 0, thiscall=True)
    ensure_engine(fc, 0x00404060, 0, thiscall=True)
    ensure_engine(fc, 0x00403DC0, 1)
    ensure_engine(fc, 0x00403D30, 1, result=env.alloc)
    ensure_engine(fc, 0x00405D60, 10, thiscall=True, masks={4: 0xFF, 5: 0xFF, 6: 0xFF, 7: 0xFF, 8: 0xFF, 9: 0xFF}, result=lambda a: env.alloc_log[-1][0])
    # the light editor
    ensure(fc, 0x004AD020, 4, name="item_set_column", convention="thiscall", this=True, text=True, keep=slice(1, 4))      # (the rows' value column)
    ensure(fc, 0x004CF850, 1, name="printf_log")                        # (the editor's debug log: only the format is recorded)
    ensure_engine(fc, 0x004F3980, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))
    ensure_engine(fc, 0x004F39B0, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))
    # the loading screen and the goals screen
    ensure_engine(fc, 0x0044AF90, 1, text=True, result=lambda a: rng.choice([0, 1]))
    ensure_engine(fc, 0x0044A070, 4, thiscall=True, text=2, stack_to=SCRATCH_LINE)
    ensure_engine(fc, 0x00466BE0, 0)
    ensure_engine(fc, 0x004E8730, 4, stack_to=SCRATCH_LINE, blob=(0, 8))
    ensure_engine(fc, 0x004E8750, 2, stack_to=SCRATCH_LINE, blob=(0, 8))
    ensure_engine(fc, 0x004E8770, 2, stack_to=SCRATCH_LINE, blob=(0, 8))
    ensure_engine(fc, 0x00415B30, 1)
    ensure_engine(fc, 0x0044ABB0, 1, thiscall=True, stack_to=SCRATCH_LINE)
    ensure_engine(fc, 0x0047FB20, 0)
    ensure_engine(fc, 0x004DBD90, 0)
    ensure_engine(fc, 0x0047FC00, 0)
    ensure_engine(fc, 0x004DE150, 0, result=lambda a: rng.choice([0, 0, 0, 1]))
    ensure_engine(fc, 0x004305B0, 0)
    ensure_engine(fc, 0x004E02D0, 0, writes=[(0x0056B028, 1, lambda a: 0), (0x0056B194, 1, lambda a: 0)])
    ensure_engine(fc, 0x0047FAC0, 0, result=lambda a: rng.choice([0, 1]))
    ensure_engine(fc, 0x0047FAD0, 0, result=lambda a: rng.choice([0, 0, 1, 1]))
    ensure_engine(fc, 0x00452430, 1, result=lambda a: rng.choice([0, 0, 1]))
    ensure_engine(fc, 0x0047FC20, 1)
    ensure_engine(fc, 0x004DBE60, 0)
    ensure_engine(fc, 0x004C8F10, 2)
    ensure_engine(fc, 0x004D78C0, 0, thiscall=True)
    ensure_engine(fc, 0x004D7990, 3, thiscall=True)
    # the clock moves on at every frame tick (the loops wait for it)
    ensure_engine(fc, 0x004F7CE0, 0, writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    # prompt windows are blocks the caller allocated
    for probe in fc._probes:
        if probe.name == "prompt_window_construct":
            probe.result = lambda a: env.alloc_log[-1][0]
        if probe.name in ("prompt_window_construct", "font_construct", "font_destroy"):
            # the screens build their Font objects in their frames (the stack address is not repeatable): the scratch address stands for each of them
            probe.stack_to = SCRATCH_LINE


GROUP_STUBS = {group: install_comp_world for group in COMP_GROUPS}
GROUP_STUBS.update({group: install_win_world for group in WIN_GROUPS})
