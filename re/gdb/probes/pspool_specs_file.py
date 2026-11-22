"""Phase 4.11a2 case generators, part 3: the rest of pre.cpp (group `prem`) and the run of fileio.cpp's neighbours (groups `fiol`, `fade`, `fonto`).

The PRE manager is a 0x144-byte object in the scratch slab (the global at 0x0056b834 points at it).  The file layer's words, the fade's words and the font test screen's flags are real
game globals: a case pokes a legal state, calls the retail function, and the replay starts from the same bytes.  The engine (allocator, file backend, the screen) is stubbed.

Slab layout (offsets from the scratch slab):
  0x0100  the PRE manager (0x144 bytes)   0x0400  names (0x20 apart)   0x0800  a second block (the object construct writes)   0x0900  fake vtable
  0x1000  a fake font object (0x40 bytes) with its vtable at 0x1100   0x2000  the renderer (+0x84 -> the head table at 0x2100)   0x3000  the draw list arena (0x400 bytes)
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from pspool_specs import ARENA, ARENA_SIZE, FAKE_SLOTS, Ov, ensure, pick, under_test

PRE_GLOBAL = 0x0056B834
PRE_VT = 0x005196A0
SLAB = 0x4000
PRE = 0x0100
NAMES = 0x0400
BLOCK = 0x0800
VTABLE = 0x0900
FONT = 0x1000
FONT_VT = 0x1100
RENDERER = 0x2000
HEADS = 0x2100
LIST = 0x3000
LIST_BYTES = 0x400
ARENA2 = 0x3800          # (the allocation arena: addresses only)

PRE_NAMES = ["tony.pre", "level.pre", "a", "hawk", "x.y", "long_name_pre", "mall"]

# ---- the PRE manager ----------------------------------------------------------------------------------------------------------------------


def pre_regions(slab):
    return [(slab, 0x1000), (PRE_GLOBAL, 4)]


def seed_pre(env, rng):
    env.alloc_base = ARENA2
    env.alloc_size = ARENA_SIZE
    writes = {}
    for k, name in enumerate(PRE_NAMES + ["", "y"]):
        writes[env.slab + NAMES + 0x20 * k] = name.encode() + b"\0"
    writes[env.slab + VTABLE] = u32(FAKE_SLOTS[0])
    return writes


for _g in ("prem",):
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_pre
    EXTRA_REGIONS[_g] = pre_regions
    READ_ONLY[_g] = []


def pre_state(env, ov, rng, used=None, vtable=PRE_VT, holes=False):
    """A manager with `used` buffers registered under names (the first slots, or scattered with `holes`); returns the names it holds."""
    base = env.slab + PRE
    used = rng.choice([0, 1, 2, 3, 7, 12, 15, 16]) if used is None else used
    ov.u32(base, vtable)
    held = []
    slots = list(range(used)) if not holes else sorted(rng.sample(range(16), used))
    for i in slots:
        ov.u32(base + 4 + 4 * i, env.slab + ARENA2 + 0x40 * (i + 1))
        name = rng.choice(PRE_NAMES)
        ov.put(base + 0x44 + 16 * i, name.encode()[:15] + b"\0")
        held.append(name[:15])
    ov.u32(PRE_GLOBAL, base)
    return held


def name_arg(env, ov, rng, held, extra=None):
    roll = rng.random()
    if held and roll < 0.5:
        name = rng.choice(held)
    else:
        name = rng.choice(PRE_NAMES)
    ov.put(env.slab + NAMES + 0x300, name.encode() + b"\0")
    return env.slab + NAMES + 0x300


@spec(0x004A8F90, "PRE_DestroyGlobal", "prem")
def gen_pre_destroy(env, n):
    ov = Ov()
    rng = env.rng
    roll = rng.random()
    if roll < 0.15:
        ov.u32(PRE_GLOBAL, 0)
    elif roll < 0.3:
        ov.u32(env.slab + BLOCK, env.slab + VTABLE)
        ov.u32(PRE_GLOBAL, env.slab + BLOCK)
    else:
        pre_state(env, ov, rng, holes=rng.random() < 0.3)
    return [], ov.pokes()


@spec(0x004A9060, "PRE_ScalarDelete", "prem", this=lambda env: env.slab + PRE)
def gen_pre_scalar_delete(env, n):
    ov = Ov()
    rng = env.rng
    pre_state(env, ov, rng, holes=rng.random() < 0.3)
    return [rng.choice([0, 1, 1, 3])], ov.pokes()


@spec(0x004A9080, "PRE_Destruct", "prem", this=lambda env: env.slab + PRE)
def gen_pre_destruct(env, n):
    ov = Ov()
    rng = env.rng
    pre_state(env, ov, rng, holes=rng.random() < 0.3)
    return [], ov.pokes()


@spec(0x004A91D0, "PRE_RegisterBuffer", "prem", this=lambda env: env.slab + PRE)
def gen_pre_register(env, n):
    ov = Ov()
    rng = env.rng
    held = pre_state(env, ov, rng, used=rng.choice([0, 0, 1, 2, 3, 7, 12, 14, 15]), holes=rng.random() < 0.2)
    buffer = rng.choice([env.slab + ARENA2 + 0x1000, env.slab + ARENA2 + 0x1100, 0])
    return [buffer, name_arg(env, ov, rng, held)], ov.pokes()


@spec(0x004A9510, "PRE_Find", "prem", this=lambda env: env.slab + PRE)
def gen_pre_find(env, n):
    ov = Ov()
    rng = env.rng
    held = pre_state(env, ov, rng, holes=rng.random() < 0.4)
    return [name_arg(env, ov, rng, held)], ov.pokes()


@spec(0x004A9580, "PRE_Dump", "prem", this=lambda env: env.slab + PRE)
def gen_pre_dump(env, n):
    ov = Ov()
    rng = env.rng
    held = pre_state(env, ov, rng, holes=rng.random() < 0.4)
    return [name_arg(env, ov, rng, held)], ov.pokes()


@spec(0x004A95F0, "PRE_DumpGlobal", "prem")
def gen_pre_dump_global(env, n):
    ov = Ov()
    rng = env.rng
    held = pre_state(env, ov, rng)
    return [name_arg(env, ov, rng, held)], ov.pokes()


@spec(0x004A9610, "PRE_ReturnZero", "prem")
def gen_pre_zero(env, n):
    return [], {}


@spec(0x004A9620, "PRE_NothingA", "prem")
def gen_pre_nothing_a(env, n):
    return [], {}


@spec(0x004A9630, "PRE_NothingB", "prem")
def gen_pre_nothing_b(env, n):
    return [], {}


@spec(0x004A9640, "PRE_NothingC", "prem")
def gen_pre_nothing_c(env, n):
    return [], {}


def install_pre_stubs(fc, env):
    if getattr(fc, "pre_stubs", False):
        return
    fc.pre_stubs = True
    under_test()
    ensure(fc, 0x00466BB0, 1)
    ensure(fc, FAKE_SLOTS[0], 1, convention="thiscall", this=True)


# ---- the file layer ----------------------------------------------------------------------------------------------------------------------

LOADING = 0x005615D0
FILE_WINDOW = (0x00561540, 0xB0)       # the file layer's words (0x561544 .. 0x5615ec)
UNUSED_FLAG = 0x0052B9EC
SCRATCH_RECT = 0x005F0100


def file_regions(slab):
    return [(slab, 0x1000), FILE_WINDOW, (UNUSED_FLAG, 4)]


def seed_file(env, rng):
    env.alloc_base = ARENA2
    env.alloc_size = ARENA_SIZE
    writes = {env.slab + NAMES + 0x20 * k: name.encode() + b"\0" for k, name in enumerate(["tony.psx", "file.bin", "", "x"])}
    return writes


for _g in ("fiol",):
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_file
    EXTRA_REGIONS[_g] = file_regions
    READ_ONLY[_g] = []


@spec(0x00448DD0, "FileIO_OpenLoad", "fiol")
def gen_open_load(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(0x0056158C, rng.choice([0, 1, 5, 0x100, rng.getrandbits(20), rng.getrandbits(24)]))
    name = env.slab + NAMES + 0x20 * rng.randrange(4) if rng.random() < 0.9 else 0
    return [name, rng.choice([0, 1, 1, 0, 2, 7])], ov.pokes()


@spec(0x00448F40, "FileIO_SetUnusedFlag", "fiol")
def gen_set_flag(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(UNUSED_FLAG, rng.getrandbits(32))
    return [rng.choice([0, 1, rng.getrandbits(32)])], ov.pokes()


def file_state(ov, rng, loading=None, done_equal=None, busy=None, stepping=None):
    ov.u32(LOADING, rng.choice([0, 1, 1, 1]) if loading is None else loading)
    size = rng.choice([0x100, 0x2000, rng.getrandbits(20)])
    ov.u32(0x00561544, size)
    equal = (rng.random() < 0.4) if done_equal is None else done_equal
    ov.u32(0x005615CC, size if equal else rng.choice([0, 0, rng.getrandbits(20)]))
    ov.u32(0x005615C8, rng.getrandbits(32) & ~3)
    ov.u16(0x00561560, rng.choice([0, 3, 0x7FFF, 0x8001, 0xFFFF, rng.getrandbits(16)]))
    ov.u32(0x005615D4, rng.choice([0, 1]))
    ov.u32(0x005615EC, rng.choice([0, 1, 5, 99]))
    ov.u32(0x00561584, rng.getrandbits(32))
    ov.u32(0x0056155C, rng.choice([0, 1, 7]))
    ov.u32(0x005615E0, rng.choice([0, 0, 0, 1]) if busy is None else busy)
    ov.u8(0x005615DC, rng.choice([0, 0, 0, 1]) if stepping is None else stepping)


@spec(0x004493F0, "FileIO_FinishRead", "fiol")
def gen_finish_read(env, n):
    ov = Ov()
    file_state(ov, env.rng)
    return [], ov.pokes()


@spec(0x004494B0, "FileIO_Continue", "fiol")
def gen_continue(env, n):
    ov = Ov()
    file_state(ov, env.rng)
    return [], ov.pokes()


def install_file_stubs(fc, env):
    if getattr(fc, "file_stubs", False):
        return
    fc.file_stubs = True
    under_test()
    rng = lambda: env.rng      # noqa: E731
    ensure(fc, 0x0046F490, 4, "alloc", result=lambda a: 0 if a[0] > 0x100000 or rng().random() < 0.1 else env.alloc(a))
    ensure(fc, 0x00449030, 1, result=lambda a: rng().choice([rng().randrange(1, 0x10000), 1, 1999999, 2000000, 0, 0xFFFFFFFF, 0x7FFFFFFF]))
    ensure(fc, 0x00449230, 1)
    ensure(fc, 0x00449660, 0)
    ensure(fc, 0x0046F4E0, 2)
    ensure(fc, 0x004E7BC0, 1, result=lambda a: rng().choice([0, 0, 1, 0xFFFFFFFF]))
    ensure(fc, 0x004E7AD0, 3, result=lambda a: rng().choice([0, 4, 0xFFFFFFFF, 0xFFFFFFFF, 100]))
    ensure(fc, 0x00489A10, 0)
    ensure(fc, 0x0045D690, 0)
    ensure(fc, 0x004E8730, 4, stack_to=SCRATCH_RECT, blob=(0, 8))


# ---- the fade -----------------------------------------------------------------------------------------------------------------------------

FADE_WINDOW = (0x005615F0, 0x30)       # steps, colour channels, level, layer, frames, flash, flash frames
ARENA_CURSOR = 0x00560FD0
ARENA_END = 0x00568628
RENDERER_PTR = 0x00560FD4
SCREEN_W = 0x029DA394
SCREEN_H = 0x029DA398
FADE_SIZE = (0x00527D50, 8)


def fade_regions(slab):
    return [(slab + RENDERER, 0x200), (slab + LIST, LIST_BYTES), FADE_WINDOW, (ARENA_CURSOR, 8), (ARENA_END, 4), (SCREEN_W, 8)]


def seed_fade(env, rng):
    # the renderer: +0x84 points at a head table of 8-byte entries; one packet from an earlier layer is linked at layer 3
    writes = {}
    renderer = bytearray(0x200)
    struct.pack_into("<I", renderer, 0x84, env.slab + HEADS)
    writes[env.slab + RENDERER] = bytes(renderer)
    heads = bytearray(0x80)
    for layer in range(16):
        struct.pack_into("<I", heads, 8 * layer, env.slab + LIST + 0x300 if layer == 3 else 0)
    writes[env.slab + HEADS] = bytes(heads)
    return writes


def fade_fixed(slab):
    return [FADE_SIZE]


for _g in ("fade",):
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_fade
    EXTRA_REGIONS[_g] = lambda slab: [(slab + RENDERER, 0x200), (slab + HEADS, 0x80), (slab + LIST, LIST_BYTES), FADE_WINDOW, (ARENA_CURSOR, 8), (ARENA_END, 4), (SCREEN_W, 8)]
    READ_ONLY[_g] = [FADE_SIZE]


def fade_state(ov, rng, frames=None, flash=None, flash_frames=None):
    ov.u32(0x00561604, rng.choice([0, 0xFF0000, rng.getrandbits(24), rng.getrandbits(32)]))
    ov.u32(0x005615FC, rng.choice([0, 0xFF0000, rng.getrandbits(24), rng.getrandbits(32)]))
    ov.u32(0x00561600, rng.choice([0, 0xFF0000, rng.getrandbits(24), rng.getrandbits(32)]))
    for address in (0x005615F8, 0x005615F4, 0x005615F0):
        ov.u32(address, rng.choice([0, rng.getrandbits(16), rng.getrandbits(32)]))
    ov.u8(0x00561608, rng.choice([0, 0x10, 0x80, 0xFF]))
    ov.u32(0x0056160C, rng.choice([3, 5, 0, 15]))
    ov.u32(0x00561610, rng.choice([0, 1, 2, 30]) if frames is None else frames)
    ov.u32(0x00561614, rng.choice([0, 1]) if flash is None else flash)
    ov.u32(0x00561618, rng.choice([0, 1, 2, 30]) if flash_frames is None else flash_frames)


@spec(0x004497E0, "Fade_Reset", "fade")
def gen_fade_reset(env, n):
    ov = Ov()
    fade_state(ov, env.rng)
    return [], ov.pokes()


@spec(0x00449800, "Fade_IsIdle", "fade")
def gen_fade_idle(env, n):
    ov = Ov()
    fade_state(ov, env.rng)
    return [], ov.pokes()


@spec(0x00449810, "Fade_Start", "fade")
def gen_fade_start(env, n):
    ov = Ov()
    rng = env.rng
    fade_state(ov, rng)
    return [rng.choice([0, 0xFF, rng.getrandbits(8), rng.getrandbits(32)]), rng.choice([0, 0xFF, rng.getrandbits(8), rng.getrandbits(32)]),
            rng.choice([0, 0xFF, rng.getrandbits(8), rng.getrandbits(32)]), rng.choice([0, 1, 2, 3, 7, 30, 255, rng.getrandbits(16)]),
            rng.choice([0, 0x10, 0x80, 0xFF, rng.getrandbits(32)]), rng.choice([0, 3, 5, 9])], ov.pokes()


@spec(0x004498A0, "Fade_StartFlash", "fade")
def gen_fade_flash(env, n):
    ov = Ov()
    rng = env.rng
    fade_state(ov, rng)
    return [rng.choice([1, 2, 3, 7, 30, 255, -1, -5, 0x1000000]), rng.choice([0, 3, 5, 9])], ov.pokes()


@spec(0x004498F0, "Fade_Update", "fade")
def gen_fade_update(env, n):
    ov = Ov()
    fade_state(ov, env.rng)
    return [], ov.pokes()


@spec(0x004499B0, "Fade_Draw", "fade")
def gen_fade_draw(env, n):
    ov = Ov()
    rng = env.rng
    fade_state(ov, rng, frames=rng.choice([0, 1, 5]), flash=rng.choice([0, 1]))
    cursor = env.slab + LIST + rng.choice([0, 0x10, 0x40, 0x100, 0x1C0, 0x1D0, 0x3D0, 0x3E0])
    ov.u32(ARENA_CURSOR, cursor)
    ov.u32(ARENA_END, env.slab + LIST + LIST_BYTES - rng.choice([0, 0, 0x10, 0x20, 0x30]))
    ov.u32(RENDERER_PTR, env.slab + RENDERER)
    ov.u32(SCREEN_W, rng.choice([512, 640, 800, 1024, 320]))
    ov.u32(SCREEN_H, rng.choice([240, 480, 600, 768, 200]))
    ov.u32(env.slab + HEADS + 8 * 5, rng.choice([0, env.slab + LIST + 0x200]))
    return [], ov.pokes()


def install_fade_stubs(fc, env):
    return


# ---- the font object and the font test screen -------------------------------------------------------------------------------------------

TEST_FLAGS = (0x0056B038, 0x30)        # 0x56b038, 0x56b058 and 0x56b0d9 (the flags the test screen reads and sets)


def font_regions(slab):
    return [(slab + FONT, 0x140), (slab + BLOCK, 0x100), (0x0056B038, 0xA2)]


def seed_font(env, rng):
    env.alloc_base = ARENA2
    env.alloc_size = ARENA_SIZE
    env.ticks = 0
    env.frames_left = 1
    writes = {}
    writes[env.slab + FONT] = struct.pack("<I", env.slab + FONT_VT) + bytes(0x3C)
    writes[env.slab + FONT_VT] = u32(FAKE_SLOTS[3]) * 4       # (slot 1, the draw method, is the one the loop calls)
    return writes


for _g in ("fonto",):
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_font
    EXTRA_REGIONS[_g] = font_regions
    READ_ONLY[_g] = []


@spec(0x00449CA0, "Font_Construct", "fonto", this=lambda env: env.slab + BLOCK)
def gen_font_construct(env, n):
    ov = Ov()
    ov.put(env.slab + BLOCK, env.rng.randbytes(0x80))
    return [], ov.pokes()


@spec(0x00449CB0, "Font_ScalarDelete", "fonto", this=lambda env: env.slab + BLOCK)
def gen_font_scalar_delete(env, n):
    ov = Ov()
    rng = env.rng
    ov.put(env.slab + BLOCK, rng.randbytes(0x80))
    return [rng.choice([0, 1, 1, 3])], ov.pokes()


@spec(0x0044A060, "Font_BaseDestruct", "fonto", this=lambda env: env.slab + BLOCK)
def gen_font_base_destruct(env, n):
    ov = Ov()
    ov.put(env.slab + BLOCK, env.rng.randbytes(0x80))
    return [], ov.pokes()


@spec(0x00449B40, "FontTest_Loop", "fonto")
def gen_font_test(env, n):
    ov = Ov()
    rng = env.rng
    env.frames_left = rng.choice([1, 1, 2, 3, 4])
    env.step = 0
    ov.put(env.slab + FONT, struct.pack("<I", env.slab + FONT_VT) + rng.randbytes(0x3C))
    ov.u8(0x0056B0D9, rng.choice([0, 0, 1]))
    ov.u8(0x0056B038, rng.choice([0, 1]))
    ov.u8(0x0056B058, rng.choice([0, 1]))
    return [], ov.pokes()


def install_font_stubs(fc, env):
    if getattr(fc, "font_stubs", False):
        return
    fc.font_stubs = True
    under_test()
    rng = lambda: env.rng      # noqa: E731
    ensure(fc, 0x00466BB0, 1)
    ensure(fc, 0x004302C0, 4)
    ensure(fc, 0x004E8960, 1, result=lambda a: tick(env))
    ensure(fc, 0x0044ADA0, 1, result=lambda a: env.slab + FONT)
    ensure(fc, 0x004F7CE0, 0, writes=[(0x0056B038, 1, lambda a: rng().choice([0, 0, 1])), (0x0056B058, 1, lambda a: rng().choice([0, 0, 1]))])
    ensure(fc, 0x004F5FF0, 0)
    ensure(fc, 0x004DAE00, 0)
    ensure(fc, 0x004304B0, 0)
    ensure(fc, 0x004B0950, 0)
    ensure(fc, 0x004B0800, 4)
    ensure(fc, 0x004B0600, 7)
    ensure(fc, FAKE_SLOTS[3], 4, convention="thiscall", this=True)
    ensure(fc, 0x004E8740, 1)
    ensure(fc, 0x00430510, 0)
    ensure(fc, 0x00489A10, 0)
    ensure(fc, 0x00466C00, 0, writes=[(0x0056B0D9, 1, lambda a: leave(env))])
    ensure(fc, 0x0044B0A0, 1)


def tick(env):
    env.ticks += 1
    return env.ticks


def leave(env):
    """The frame counter of the case: the last frame sets the exit flag."""
    env.frames_left -= 1
    return 1 if env.frames_left <= 0 else 0


GROUP_STUBS = {"prem": install_pre_stubs, "fiol": install_file_stubs, "fade": install_fade_stubs, "fonto": install_font_stubs}
