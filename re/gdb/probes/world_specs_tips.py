"""Phase 5 / 10g s17 probe: the trick tip monitor (groups tpu: 0x004c4620 TrickTips_Update, tps: 0x004c4950 TrickTips_Show).

The monitor is the static object at 0x0056db98 (0x114 bytes); the player is a slab whose first 0x30 bytes stand for the skater's counters (+0x302c .. +0x3058: the pointer is the slab minus 0x302c).
Update's callees are services (the pause check, Video_Playing, the show method); Show is its own group, its only callee the assertion routine (stubbed by the common stubs when the condition fails).
The tip table (0x00518b98, 22 rows of 0x14 bytes, read past the 22 for the assertion cases), the heading pointers and the file name the assertion macro copies are retail's own, read-only.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import noise

MONITOR, MASKS, OPTION, LEVEL, PLAYER = 0x0056DB98, 0x0056A40C, 0x0056A46C, 0x0056A898, 0x0056A960
REPLAY_BYTE, MODE, STATE, HELD_A, HELD_B = 0x00561990, 0x00533F38, 0x0055FD78, 0x0056B098, 0x0056B204
LEVEL_MASKS, LINE, FILE = 0x0053C288, 0x00568638, 0x0056881C
TABLE, HEADINGS, LITERALS = 0x00518B98, 0x0052E290, 0x00541930

UPDATE_GROUPS, SHOW_GROUPS, DISPLAY_GROUPS = ("tpu", "tpv"), ("tps", "tpt"), ("tpd",)
SLAB = 0x100
COMMON = [(MONITOR, 0x114), (LINE, 4), (FILE, 0x40)]
UPDATE_GLOBALS = COMMON + [(MASKS, 0x10), (PLAYER, 4), (REPLAY_BYTE, 4), (MODE, 4), (STATE, 4), (HELD_A, 4), (HELD_B, 4)]
SHOW_GLOBALS = COMMON + [(MASKS, 0x120), (OPTION, 4), (LEVEL, 4), (LEVEL_MASKS, 0x700)]
LEVELS_IN_WINDOW = 4                                   # (0x6b words a level: the window holds levels 0..3 and the bank words a bad tip number reaches)

for _group in UPDATE_GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, SLAB)] + UPDATE_GLOBALS, gap=0)
    READ_ONLY[_group] = [(TABLE, 0x1B8)]
for _group in SHOW_GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, 0x10)] + SHOW_GLOBALS, gap=0)
    READ_ONLY[_group] = [(TABLE, 0x600), (HEADINGS, 8), (LITERALS, 0xA0)]


for _group in DISPLAY_GROUPS:
    SLAB_BYTES[_group] = 0x10
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, 0x10), (MONITOR, 0x114)], gap=0)
    READ_ONLY[_group] = []


def install_display_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_tpd_installed", False):
        return
    fc._tpd_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    ensure(fc, 0x004734B0, 1, "engine_004734b0")                                            # Mess_SetTextJustify
    ensure(fc, 0x00473580, 4, "engine_00473580")                                            # Mess_SetRGB
    ensure(fc, 0x004734F0, 1, "engine_004734f0")                                            # Mess_SetScale
    ensure(fc, 0x004735B0, 4, "engine_004735b0")                                            # Mess_DrawPanelText
    ensure(fc, 0x00473500, 2, "engine_00473500",                                            # Mess_GetScale: the drawn width of a line
           result=lambda a: rng.choice([0, 1, 10, 11, 219, 220, 221, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, 0x0CCCCCCC, 0x0CCCCCCD, rng.randrange(0, 0x1000), rng.getrandbits(32), rng.getrandbits(32)]))


def install_update_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_tpu_installed", False):
        return
    fc._tpu_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    answer = lambda a: rng.choice([0, 0, 0, 0, 0, 0, 0, 0x100, 0xFFFFFF00, 1, 0xFF])        # (a byte in AL: the upper bits are the callee's leftovers)
    ensure(fc, 0x00419420, 0, "engine_00419420", result=answer)
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=answer)                            # Video_Playing
    ensure(fc, 0x004C4950, 1, "engine_004c4950", convention="thiscall", this=True)         # TrickTips_Show


def install_show_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_tps_installed", False):
        return
    fc._tps_installed = True
    mark_under_test()
    stop_clock(fc)


GROUP_STUBS = {group: install_update_stubs for group in UPDATE_GROUPS}
GROUP_STUBS.update({group: install_show_stubs for group in SHOW_GROUPS})
GROUP_STUBS.update({group: install_display_stubs for group in DISPLAY_GROUPS})

EDGE = [0, 1, 2, 3, 4, 5, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE]


def edge(rng):
    return rng.choice(EDGE + [rng.randrange(0, 20), rng.getrandbits(32)])


def mask_word(rng):
    return rng.choice([0, 0xFFFFFFFF, 0x3FFFFF, 1, 1 << rng.randrange(32), rng.getrandbits(32), rng.getrandbits(32) & 0x3FFFFF])


COUNTERS = [0x302C, 0x3030, 0x3038, 0x303C, 0x3040, 0x3048, 0x3050, 0x3054, 0x3058]
HIGHEST = [0xA4, 0xA8, 0xB0, 0xB4, 0xB8, 0xC0, 0xC8, 0xCC, 0xD0]          # the monitor's highest value of each
RISES = [0xD8, 0xDC, 0xE4, 0xE8, 0xEC, None, 0xFC, 0x100, 0x104]


@spec(0x004C4620, "TrickTips_Update", "tpu")
def gen_update(env, n):
    return update_case(env, False)


@spec(0x004C4620, "TrickTips_Update", "tpv")
def gen_update_aimed(env, n):
    """The second fixture: every counter rises, the rise counts sit at their thresholds, the first frame and the held flags are common."""
    return update_case(env, True)


def update_case(env, aimed):
    img, rng = Image(), env.rng
    skater = env.slab - 0x302C
    img.put(MONITOR, noise(rng, 0x114))
    img.put(env.slab, noise(rng, 0x30))
    for index, counter in enumerate(COUNTERS):
        value = edge(rng)
        img.u32(skater + counter, value)
        around = [(value - 1) & 0xFFFFFFFF, value, (value + 1) & 0xFFFFFFFF, (value - 1) & 0xFFFFFFFF, edge(rng), rng.getrandbits(32)]
        img.u32(MONITOR + HIGHEST[index], rng.choice(around[:4] if aimed else around))
        if RISES[index] is not None:
            img.u32(MONITOR + RISES[index], rng.choice([0, 1, 2, 2, 3, 4, 4, 5, 6] if aimed else [0, 1, 2, 2, 2, 3, 4, 4, 4, 5, 6, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, rng.getrandbits(32)]))
    frames = rng.choice([0, 0, 0, 1, 2, 0x100, 0x257, 0x258, 0xFFFFFFFF, rng.getrandbits(32)] if not aimed else [0, 0, 0, 0, 1, 2, 7])
    img.u32(MONITOR + 0x58, frames)
    img.u32(MONITOR + 0x60, rng.choice([0, 1, 0x257, 0x258, 0x259, 0xFFFFFFFF, rng.getrandbits(32), rng.randrange(0, 0x300)]))
    img.u32(MONITOR + 0x10C, rng.choice([0, 1, 0x94, 0x95, 0x96, 0x97, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.u32(MONITOR + 0x64, rng.getrandbits(32))
    held = rng.choice([(0, 0), (1, 0), (0, 1), (1, 1), (1, 0), (0xFF, 0), (0, 0x80)] if aimed else [(0, 0), (0, 0), (0, 0), (1, 0), (0, 1), (1, 1), (0xFF, 0), (0, 0x80)])
    img.u32(HELD_A, held[0] | (rng.getrandbits(24) << 8 if aimed else 0))            # (the upper bytes belong to other variables: only the byte counts)
    img.u32(HELD_B, held[1] | (rng.getrandbits(24) << 8 if aimed else 0))
    for k in range(4):
        img.u32(MASKS + 4 * k, mask_word(rng))
    if aimed and rng.random() < 0.6:                    # (every tip with state 0 seen but one: the search runs to the end of the table or finds the last)
        img.u32(MASKS, (0x3FFFFF & ~(1 << rng.choice([0] + list(range(7, 22))))) | (rng.getrandbits(10) << 22))
    img.u32(PLAYER, skater)
    img.u32(REPLAY_BYTE, rng.choice([0, 0, 0, 0, 0, 0, 0, 1, 0xFF, 0x80]) | (rng.getrandbits(24) << 8 if aimed else 0))
    img.u32(MODE, rng.choice([1, 1, 1, 1, 1, 1, 1, 1, 0, 2, 0xFFFFFFFF, 0x100]))
    img.u32(STATE, rng.choice([0, 0, 1, 2, 3, 3, 4, 5, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFC]))
    return [], img.pokes()


@spec(0x004C4950, "TrickTips_Show", "tps", this=lambda env: MONITOR)
def gen_show(env, n):
    return show_case(env, False)


@spec(0x004C4950, "TrickTips_Show", "tpt", this=lambda env: MONITOR)
def gen_show_aimed(env, n):
    """The second fixture: the 600-frame window at its edges and both banks' masks set so that each test is the deciding one."""
    return show_case(env, True)


def show_case(env, aimed):
    img, rng = Image(), env.rng
    img.put(MONITOR, noise(rng, 0x114))
    tip = rng.choice([-1, 0xFFFFFFFF] + list(range(22)) * 3 + [0x16, 0x17, 0x1F, 0x20, 0x21, 0x3F, 0x40, 0x41, 0x43, 0x80000000, 0xFFFFFFFB, rng.getrandbits(32) | 0x80000000]) & 0xFFFFFFFF
    signed = tip - (1 << 32) if tip >= 1 << 31 else tip
    bank = 0 if signed < 0x20 else 1 if signed < 0x40 else tip
    frames = rng.choice([0, 0, 1, 2, 0x100, 0x258, 0x259, 0x4B0, 0xFFFFFFFF, rng.getrandbits(32), rng.randrange(0, 0x1000)])
    img.u32(MONITOR + 0x58, frames)
    img.u32(MONITOR + 0x5C, rng.choice([(frames - 0x258) & 0xFFFFFFFF, (frames - 0x257) & 0xFFFFFFFF, (frames - 0x259) & 0xFFFFFFFF, 0, frames, 0xFFFFFDA8, 0xFFFFFDA7, 0xFFFFFDA9,
                                        rng.getrandbits(32), rng.randrange(0, 0x1000)]))
    img.u32(OPTION, rng.choice([0, 1, 1, 1, 1, 2, 0x100, 0xFFFFFFFF]))
    level = rng.randrange(LEVELS_IN_WINDOW)
    img.u32(LEVEL, level)
    bit = 1 << ((tip - (bank << 5)) & 31)
    choose = lambda: rng.choice([0, bit, (~bit) & 0xFFFFFFFF, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32) | bit, rng.getrandbits(32) & ~bit & 0xFFFFFFFF])
    img.u32(LEVEL_MASKS + 4 * ((level * 0x6B + bank) & 0xFFFFFFFF), choose() if rng.random() < 0.6 else 0)
    img.u32(MASKS + 4 * bank if bank < 0x44 else MASKS, choose())
    img.u32(MASKS + 8 + 4 * bank if bank < 0x44 else MASKS + 8, choose())
    return [tip], img.pokes()


@spec(0x004C4420, "TrickTips_Display", "tpd")
def gen_display(env, n):
    img, rng = Image(), env.rng
    img.put(MONITOR, noise(rng, 0x114))
    timer = rng.choice([0, 1, 1, 2, 5, 9, 10, 11, 100, 189, 190, 191, 192, 199, 200, 201, 0xFFFFFFFF, 0x10009, 0x1000A, 0xFFFF00C8, rng.getrandbits(32), rng.randrange(0, 0x100), rng.randrange(0xC0, 0xD0)])
    img.u32(MONITOR + 0xA0, timer)
    pointer = lambda: rng.choice([0, 0, 0x533378, 0x53336C, 0x533360, rng.getrandbits(32) | 1])
    img.u32(MONITOR + 0x90, rng.choice([0, 0x52E290, 0x52E294, rng.getrandbits(32) | 1]) if rng.random() < 0.9 else 0)
    for k in range(3):
        img.u32(MONITOR + 0x94 + 4 * k, pointer())
    return [], img.pokes()


# ---- the two empty virtual methods the bit classes share (the Move slot of the bits that do not move: a lone `ret`) -----------------------------------------------------------------------------

EMPTY_GROUPS = ("bem",)
for _group in EMPTY_GROUPS:
    SLAB_BYTES[_group] = 0x10
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, 0x10)], gap=0)
    READ_ONLY[_group] = []


def install_empty_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_bem_installed", False):
        return
    fc._bem_installed = True
    mark_under_test()
    stop_clock(fc)


GROUP_STUBS.update({group: install_empty_stubs for group in EMPTY_GROUPS})
for _address, _name in ((0x004044D0, "CBit_EmptyHook"), (0x004044E0, "CBit_EmptyHook1"), (0x004057E0, "CBit_EmptyHook2")):
    spec(_address, _name, "bem")(lambda env, n: ([], Image().pokes()))
# 12 s10: the car class's slot 0 (`mov eax, 1; ret 4`), the raw image class's three empty methods (`ret 8`) and a window class's slot 0 (a lone `ret`)
for _address, _name, _arguments in ((0x004138D0, "Car_AlwaysTrue", 1), (0x004573F0, "Image_NothingTwo", 2), (0x00459BD0, "Window_Nothing", 0)):
    spec(_address, _name, "bem")(lambda env, n, k=_arguments: ([env.rng.getrandbits(32) for _ in range(k)], Image().pokes()))
