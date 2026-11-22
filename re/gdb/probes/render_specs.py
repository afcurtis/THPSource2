"""Phase 4.15e probe: the display buffers of db.cpp (group rdb: 0x0042fc60..0x004305e0).

The two buffer records (0xb4 bytes each at 0x00560fe0 / 0x00561094, and the flags between them at 0x00561050..0x00561114) hold flags, ordering table pointers and the display / draw
environments; the generators fill them with random bytes and then make the flags and pointers legal: the ordering tables are two fake ones in the slab (a head word and the link word at +8), the
primitive area a third. Everything the PSX library, the GPU and the renderer do is a stub. The assertion routine only logs (its line and file globals are cut out of the captured memory).

Slab layout (offsets): ordering table 0 at 0x100 (0x100), table 1 at 0x200 (0x100), rectangles at 0x300 (0x20), arena 0x400 (0x2000 bytes).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("rdb",)

TABLE0 = 0x100
TABLE1 = 0x200
RECTS = 0x300
ARENA = 0x400
ARENA_BYTES = 0x2000
SLAB = ARENA + ARENA_BYTES + 0x100
SCRATCH = 0x7FF00800 + 0xE00     # (renderkit::kScratch: where the port builds the locals the retail code keeps on the stack)

BUFFER_A = 0x00560FE0
BUFFER_B = 0x00561094
DB_FIRST = 0x00560FC8
DB_BYTES = 0x1F8
GLOBALS = [(DB_FIRST, DB_BYTES), (0x00561140, 0x40), (0x00563A60, 4), (0x0056A898, 8), (0x00533F38, 8), (0x0056E31C, 4)]
STATIC = [(0x00527D50, 8), (0x0053C27C, 0x1000)]


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def r(address, name, group, this=None):
    resolve = this if callable(this) else ((lambda env: env.slab + this) if this is not None else None)
    return spec(address, name, group, this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render_installed", False):
        return
    fc._render_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)    # (the clock at 0x0056e31c must not tick while a case runs)
    # the ordering tables and the primitive area are megabytes (the pointers are all the display code keeps): hand out slab addresses without a size check
    grabbed = [0]

    def fake_alloc(args):
        grabbed[0] += 1
        return env.slab + ARENA + 0x20 * (grabbed[0] % 64)

    ensure(fc, 0x0046F490, 4, "alloc", result=fake_alloc)
    # the PSX library and the GPU: they only report their arguments
    ensure(fc, 0x004E8740, 1, "engine_004e8740", result=lambda a: rng.choice([0, 0, 1]))       # DrawSync (the answer decides whether the callback marks the buffers)
    for address, count in ((0x004E86F0, 1), (0x004E8700, 1), (0x004E8720, 5), (0x004E8710, 5), (0x004E8A00, 0), (0x004E8850, 1), (0x004E8790, 1), (0x004E87A0, 1), (0x004E87F0, 2),
                           (0x004E89F0, 0), (0x004D0BC0, 2), (0x004D0C30, 0), (0x004D3B00, 2), (0x004D3160, 1), (0x004F39E0, 2), (0x00466BE0, 0), (0x00464690, 2), (0x004C9300, 1),
                           (0x004DBEA0, 0)):
        ensure(fc, address, count, "engine_%08x" % address)
    ensure(fc, 0x004E8840, 4, "engine_004e8840", blob=(0, 8), stack_to=SCRATCH)                  # ClearImage(rect, ...): the rectangle is a local of the caller


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def rdb(address, name):
    return r(address, name, "rdb", None)


def buffers(img, env, rng, ready=None):
    """Both buffer records with random bytes, then the pointers and flags the functions go through made legal."""
    img.put(DB_FIRST, noise(rng, DB_BYTES))
    for k, base in enumerate((BUFFER_A, BUFFER_B)):
        img.u32(base + 0x84, env.slab + (TABLE0, TABLE1)[k])        # ordering table of the buffer
        img.u32(base + 0xB0, env.slab + ARENA + 0x80 * k)           # its primitive area
        img.u32(base + 0x74, rng.choice([0, 0, 1]))                  # the buffer is waiting to be drawn / displayed
        img.u32(base + 0x78, rng.choice([0, 0, 1]))
    img.u32(0x00560FD4, rng.choice([BUFFER_A, BUFFER_B]))
    img.u32(0x00560FD8, rng.choice([BUFFER_A, BUFFER_B]))
    for table in (TABLE0, TABLE1):
        img.put(env.slab + table, noise(rng, 0x20))
    img.u32(0x00561154, rng.choice([2, 0x1002, 0x2002, 0x10CA, word(rng) & 0xFFFF]))
    img.u32(0x0056114C, rng.choice([0xFFFFFFFF, 0, word(rng), rng.getrandbits(24)]))
    img.u32(0x00563A60, word(rng))
    img.u32(0x0056E31C, word(rng))
    img.u32(0x00561148, rng.choice([0, 1, 2]))
    img.u32(0x0056A89C, rng.choice([0, 0, 1]))
    return img


@rdb(0x0042FC60, "Db_OTSize")
def gen_ot_size(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00561154, word(rng))
    return [], img.pokes()


@rdb(0x0042FC70, "Db_UpdateSky")
def gen_update_sky(env, n):
    img, rng = Image(), env.rng
    img.put(DB_FIRST, noise(rng, DB_BYTES))
    img.u32(0x0056114C, rng.choice([0xFFFFFFFF, word(rng), rng.getrandbits(24)]))
    return [], img.pokes()


@rdb(0x0042FCD0, "Db_CallBack")
def gen_callback(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    for address in (0x00561058, 0x0056105C, 0x0056110C, 0x00561110):
        img.u32(address, rng.choice([0, 0, 1, 2]))
    return [], img.pokes()


@rdb(0x0042FD20, "Db_Vblank")
def gen_vblank(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    for address in (0x00561054, 0x0056105C, 0x00561058, 0x00561050, 0x00561108, 0x00561110, 0x0056110C, 0x00561104):
        img.u32(address, rng.choice([0, 0, 1, 1, 2]))
    return [], img.pokes()


@rdb(0x0042FED0, "Db_WaitRender")
def gen_wait_render(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    for address in (0x00561058, 0x0056110C):
        img.u32(address, rng.choice([0, 0, 1]))
    return [], img.pokes()


@rdb(0x0042FFC0, "Db_Render")
def gen_render(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    for address in (0x00561058, 0x0056110C, 0x0056105C, 0x00561110, 0x00561054, 0x00561108):
        img.u32(address, rng.choice([0, 0, 1]))
    return [], img.pokes()


@rdb(0x004301A0, "Db_InstallCallback")
def gen_install(env, n):
    return [], Image().pokes()


@rdb(0x004301B0, "Db_Reset")
def gen_reset(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    img.u32(0x00561090, rng.choice([0, env.slab + ARENA + 0x400, env.slab + ARENA + 0x500]))
    img.u32(0x00561064, env.slab + ARENA + 0x600)
    img.u32(0x00561118, env.slab + ARENA + 0x700)
    return [], img.pokes()


@rdb(0x004301F0, "Db_Init")
def gen_init(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    img.u32(0x0056A898, rng.randrange(0, 4))
    img.u32(0x00533F3C, rng.choice([1, 2]))
    img.u32(0x00561090, rng.choice([0, 0, env.slab + ARENA + 0x400]))
    return [word(rng), word(rng), rng.choice([0, 1, 0x100, word(rng)])], img.pokes()


@rdb(0x004302C0, "HiRes_ClearOTag")
def gen_hires(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    img.u32(0x0056A898, rng.randrange(0, 4))
    img.u32(0x00533F3C, rng.choice([1, 2]))
    img.u32(0x00561090, rng.choice([0, 0, env.slab + ARENA + 0x400]))
    return [word(rng), word(rng), rng.choice([0, 1, 0x100, word(rng)]), rng.choice([0, 1, 0x100, word(rng)]), rng.choice([0, 1, word(rng)])], img.pokes()


@rdb(0x004304B0, "Db_FlipClear")
def gen_flip_clear(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    return [], img.pokes()


@rdb(0x00430510, "Db_DisplayDraw")
def gen_display_draw(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    return [], img.pokes()


@rdb(0x004305B0, "Db_Flip")
def gen_flip(env, n):
    img, rng = Image(), env.rng
    buffers(img, env, rng)
    return [], img.pokes()
