"""Phase 4.15e probe: the scene renderer's small functions (group rm3d: 0x00461a50..0x00464a80, 0x004657c0, 0x00466030..0x00466080) and its frame functions (group rscn: 0x0045f3f0, 0x00460a90, 0x004667e0,
0x00466a00).

Slab layout (offsets): fake vtable 0x0000, model 0x0100 (0x180), wibble list 0x0308 (its tag a dword before it) up to 0x0500, objects 0x0600 (0x4c each), model table 0x0740, model data 0x0780 (0x200), pulse
table 0x0a08 (tag and size before it), RGB table 0x0c00, zone record 0x0d00 (0x400), big model 0x1000 (0x2f00: the link matrix is at +0x2e58), scratch words 0x3f00, arena 0x4000 (0x2000).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from render_specs import BUFFER_A, BUFFER_B, DB_BYTES, DB_FIRST, SCRATCH, noise, word

GROUPS = ("rm3d", "rscn")

WIB = 0x0308
OBJECTS = 0x0600
MODEL_TABLE = 0x0740
MODEL_DATA = 0x0780
PULSE = 0x0A08
RGB = 0x0C00
ZONE = 0x0D00
BIG = 0x1000
ARENA = 0x4000
ARENA_BYTES = 0x2000
SLAB = ARENA + ARENA_BYTES + 0x100

REGION_RECORDS = 0x0056D428
REGION_STRIDE = 0x44
GTE_WINDOW = (0x006A3E00, 0x120)
GLOBALS = [GTE_WINDOW, (REGION_RECORDS, 22 * REGION_STRIDE), (0x00562090, 0x70), (0x005620E0, 0x24), (0x00563A00, 0x140), (0x00563B28, 0x820), (0x00564340, 0x48), (0x0056A898, 4), (0x0053DB80, 0x34 * 16),
           (0x00567CA4, 0xC), (0x005672C0, 0xCC0 + 0x660), (0x00537C80, 0x10), (0x00533F90, 8), (0x0056A960, 4), (0x00533908, 4), (0x0055F6BC, 4), (0x0056DB28, 4), (0x00562108, 4), (0x0056E320, 4), (0x0056DCA8, 0x10), (0x0055FA3C, 4), (0x029DA390, 0x14), (0x0056A880, 4),
           (0x00560FD0, 8), (0x00533F3C, 4), (0x00561C74, 4), (0x00561C90, 4), (0x00533F40, 0x20), (0x0053C1F8, 0x1AC * 3), (0x0053A3B8, 0x170 * 3 + 4),
           (DB_FIRST, DB_BYTES)]
STATIC = [(0x00527D50, 8), (0x00518D60, 8), (0x00518908, 0x10), (0x00534198, 0x20), (0x00519A74, 0xC), (0x00549D98, 4), (0x00518484, 4)]
FAKE_DELETE = 0x004AD950


def regions(slab):
    return merged([(slab, SLAB), (slab + 0xBEF8, 8), (slab + 0xCEF8, 8)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {env.slab: struct.pack("<4I", FAKE_DELETE, FAKE_DELETE, FAKE_DELETE, FAKE_DELETE)}


for _group in GROUPS:
    SLAB_BYTES[_group] = 0xD000
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render5_installed", False):
        return
    fc._render5_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    ensure(fc, 0x004D14D0, 2, "engine_004d14d0")
    ensure(fc, 0x004F6D00, 2, "engine_004f6d00", pokes=[(0, 4, lambda a: rng.choice([512, 640, 320, 800, rng.randrange(1, 1024)])), (1, 4, lambda a: rng.choice([240, 480, 400, 600, rng.randrange(1, 800)]))],
           stack_to={0: SCRATCH + 0x20, 1: SCRATCH + 0x24})
    ensure(fc, 0x004E87F0, 2, "engine_004e87f0", blob=(1, 8), stack_to=SCRATCH)
    ensure(fc, 0x004604F0, 1, "engine_004604f0")
    for address, count in ((0x004E8850, 1), (0x004E87C0, 1), (0x004E8890, 2), (0x004E8700, 1), (0x004E8860, 1)):
        ensure(fc, address, count, "engine_%08x" % address, stack_to=SCRATCH + 0x100)
    ensure(fc, 0x004E8880, 6, "engine_004e8880", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x004E8740, 1, "engine_004e8740", result=lambda a: rng.choice([0, 0, 1]))
    ensure(fc, 0x004E8720, 5, "engine_004e8720", stack_to=SCRATCH + 0x100)
    ensure(fc, 0x004B0C60, 1, "engine_004b0c60", convention="thiscall", this=True, result=lambda a: rng.randrange(0, 3))
    ensure(fc, 0x004E8870, 1, "engine_004e8870", text=True, stack_to=SCRATCH + 0x180)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def m3(address, name):
    return spec(address, name, "rm3d", this=None)


def sc(address, name):
    return spec(address, name, "rscn", this=None)


def region_record(region):
    return REGION_RECORDS + REGION_STRIDE * region


def render_state(img, env, rng):
    """The words of the frame the small submitters and the time keep."""
    img.put(0x005620A0, noise(rng, 0x60))
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(0x00564360, noise(rng, 0x28))
    img.u32(0x005620A4, rng.choice([0, 1, 0x40, 0x4000, word(rng)]))
    img.u32(0x0056437C, rng.choice([0, 0, 1]))
    img.u32(0x00564380, rng.choice([0, 1, 5]))
    img.u32(0x00564378, rng.choice([0, 1]))


# ---- the small submitters ----------------------------------------------------------------------------------------------------------------------------------------------------------------

for _address, _name in ((0x00461A50, "M3D_SubmitStateGeometry"), (0x00461A90, "M3D_SubmitStateGeometryVariant"), (0x00461AD0, "RenderBackgroundModel"), (0x00461B70, "RenderModelNonRotated"),
                        (0x00461BB0, "RenderBackgroundModelNonRotated")):
    def _gen(env, n):
        img, rng = Image(), env.rng
        render_state(img, env, rng)
        return [env.slab + 0x100], img.pokes()
    m3(_address, _name)(_gen)


@m3(0x00461B10, "M3D_SetGeometryState")
def gen_set_geometry_state(env, n):
    img, rng = Image(), env.rng
    render_state(img, env, rng)
    return [env.slab + 0x100, rng.choice([0, 2, 0x10, word(rng)])], img.pokes()


@m3(0x00461B50, "RenderModelInSuperFast")
def gen_super_fast(env, n):
    img, rng = Image(), env.rng
    render_state(img, env, rng)
    return [env.slab + 0x100, rng.choice([0, 2, 0x10, word(rng)])], img.pokes()


@m3(0x00461BF0, "M3d_BuildTransform")
def gen_build_transform(env, n):
    img, rng = Image(), env.rng
    at = env.slab + BIG
    img.put(at, noise(rng, 0x2F00))
    img.u8(at + 0xD8, rng.choice([0, 1, 2, 3]))
    img.u8(at + 5, rng.choice([0, 2, 0x80, 0x82]))
    img.put(at + 0x2E58, struct.pack("<9h", *[rng.choice([0x1000, -0x1000, 0, rng.randrange(-0x1000, 0x1000)]) for _ in range(9)]))
    return [at], img.pokes()


@m3(0x00461C90, "uWibble")
def gen_u_wibble(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x100, noise(rng, 8))
    img.u32(0x00563B30, word(rng))
    img.put(0x00563B3C, noise(rng, 0x800))
    return [env.slab + 0x100], img.pokes()


@m3(0x00461CC0, "vWibble")
def gen_v_wibble(env, n):
    return gen_u_wibble(env, n)


def wib_scene(img, env, rng, region, tiled=True):
    """A wibble table for `region`: one or two lists, each naming an object whose model has a few textured faces.

    The texture coordinates are wrapped back into the page by a loop that adds the face's wrap (0xf800 for a texture word ending in 0x021) until the four coordinates agree in bit 16; a coordinate
    set that never agrees would loop for ever, so the entries are either well inside the page (any wibble) or, when the list scrolls, within one wrap of the page's end with no wibble."""
    record = region_record(region)
    img.put(record, noise(rng, REGION_STRIDE))
    img.u32(record + 0x2C, env.slab + WIB)
    img.u32(record + 0x10, env.slab + OBJECTS)
    img.u32(record + 0x14, env.slab + MODEL_TABLE)
    img.u32(env.slab + WIB - 8, 6 if rng.random() < 0.97 else rng.choice([0, 7]))
    img.u32(env.slab + MODEL_TABLE, env.slab + MODEL_DATA)
    img.u32(0x00563AF0, rng.choice([0, 1, 0x10, 0x123, rng.getrandbits(16)]))
    model = env.slab + MODEL_DATA
    img.put(model, noise(rng, 0x200))
    img.u16(model + 2, 1)
    img.u16(model + 4, 1)
    face0 = model + 0x1C + 2 * 8
    for k in range(4):
        face = face0 + 0x30 * k
        header = (12 << 18) | 1 | 0x20
        img.u32(face, header)
        img.u32(face + 0x1C, (rng.getrandbits(22) << 10) | 0x021)
    lists = rng.choice([1, 2])
    at = env.slab + WIB
    for k in range(lists):
        obj_index = k
        header = obj_index * 0x24 + 0xC
        count = rng.choice([0, 1, 2, 3])
        scrolling = rng.random() < 0.4
        scroll_u = rng.choice([0x200, 0x400, 0x600, 0xFE00]) if scrolling else 0
        scroll_v = rng.choice([0x100, 0x300, 0x500]) if scrolling else 0
        img.u32(at, header)
        img.put(at + 4, noise(rng, 12))
        img.u16(at + 4, scroll_u)
        img.u16(at + 6, scroll_v)
        img.u16(at + 0xC, count)
        # (the scroll is multiplied by the clock and shifted down by four: the clock decides what it comes to; keep it small)
        img.u32(0x00563AF0, rng.choice([0, 1, 2, 3]) if scrolling else rng.choice([0, 1, 0x10, 0x123, rng.getrandbits(16)]))
        for e in range(count):
            entry = bytearray(noise(rng, 16))
            for byte_index in (0, 1, 4, 5, 8, 9, 12, 13):
                entry[byte_index] = rng.randrange(0xF8, 0x100) if scrolling else rng.randrange(0x20, 0xD0)
            if scrolling:
                for byte_index in (2, 3, 6, 7, 10, 11, 14, 15):
                    entry[byte_index] &= 0x0F
            img.put(at + 0x10 + 0x10 * e, bytes(entry))
        obj = env.slab + OBJECTS + 0x4C * obj_index
        img.put(obj, noise(rng, 0x4C))
        img.u8(obj + 5, rng.choice([0, 0, 0x80]))
        img.u8(obj + 0x1F, region)
        img.u16(obj + 0x1A, 0)
        at += 0x10 + 0x10 * count
    img.u32(at, 0)


@m3(0x00461CF0, "M3D_ApplyTextureWib")
def gen_apply_wib(env, n):
    img, rng = Image(), env.rng
    region = rng.randrange(0, 21)
    wib_scene(img, env, rng, region)
    # (a face whose wrap is zero never settles: the texture word's low five bits and bits 5..9 are never both zero when a wrap is needed)
    if rng.random() < 0.05:
        return [rng.choice([0xFFFFFFFF, 0xFF])], img.pokes()
    return [region], img.pokes()


@m3(0x00462170, "M3D_UpdateColourPulse")
def gen_colour_pulse(env, n):
    img, rng = Image(), env.rng
    region = rng.randrange(0, 21)
    record = region_record(region)
    img.put(record, noise(rng, REGION_STRIDE))
    img.u32(record + 0x30, env.slab + PULSE)
    img.u32(record + 0x28, env.slab + RGB)
    img.put(env.slab + RGB, noise(rng, 0x100))
    img.u32(env.slab + PULSE - 8, 7 if rng.random() < 0.97 else 0)
    at = env.slab + PULSE
    pulses = rng.choice([1, 2, 3])
    for p in range(pulses):
        count = rng.choice([1, 2, 3, 4])
        img.u8(at, rng.randrange(0, 16))
        img.u8(at + 1, count)
        img.u8(at + 2, rng.randrange(0, count))
        img.u8(at + 3, rng.randrange(0, 4))
        for e in range(count):
            img.put(at + 4 + 4 * e, noise(rng, 3) + bytes([rng.choice([1, 2, 5, 10, 30, rng.randrange(1, 255)])]))
        at += 4 + 4 * count
    img.u32(env.slab + PULSE - 4, at - (env.slab + PULSE))
    img.u32(0x00563AF0, rng.choice([5, 10, 100, 1000, rng.getrandbits(16)]))
    img.u32(0x00564374, rng.choice([0, 3, 9, 90, 999, rng.getrandbits(16)]))
    img.put(0x006A3F08, noise(rng, 8))
    return [region], img.pokes()


@m3(0x004645F0, "M3dInit_InitAtStart")
def gen_init_at_start(env, n):
    img, rng = Image(), env.rng
    img.put(0x00562090, noise(rng, 0x70))
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(0x00563B28, noise(rng, 0x820))
    img.u32(0x0056A880, rng.choice([0, 0, 1]))
    return [], img.pokes()


@m3(0x00464690, "M3D_WindowRatio")
def gen_window_ratio(env, n):
    img, rng = Image(), env.rng
    img.put(0x005620A0, noise(rng, 0x20))
    img.put(0x00563A60, noise(rng, 0x20))
    return [word(rng), word(rng)], img.pokes()


@m3(0x00464710, "M3D_SetFogRange")
def gen_fog_range(env, n):
    img, rng = Image(), env.rng
    img.put(0x00563A3C, noise(rng, 0x10))
    img.put(0x00567CA4, noise(rng, 0xC))
    img.u32(0x0056209C, word(rng))
    rangeval = rng.choice([0x800, 0x1000, 0x2000, 0x400, 1, 0x80000000, 0x100, 3, 0x1800, 0x10000])
    return [0, rng.choice([0x1770, 0, word(rng)]), rangeval], img.pokes()


@m3(0x00464A80, "M3D_PrepareTextureWib")
def gen_prepare_wib(env, n):
    img, rng = Image(), env.rng
    at = env.slab + WIB
    lists = rng.choice([0, 1, 2, 3])
    for k in range(lists):
        count = rng.choice([0, 1, 2, 4])
        img.u32(at, 0x40 + k)
        img.put(at + 4, noise(rng, 12))
        img.u16(at + 0xC, count)
        for e in range(count):
            entry = bytearray(noise(rng, 16))
            for byte_index in (2, 3, 6, 7, 10, 11, 14, 15):
                entry[byte_index] = rng.choice([0, 0x10, 0x20, 0xF0, rng.randrange(0, 256)])
            img.put(at + 0x10 + 0x10 * e, bytes(entry))
        at += 0x10 + 0x10 * count
    img.u32(at, 0)
    return [env.slab + WIB if rng.random() < 0.97 else 0], img.pokes()


@m3(0x004657C0, "M3D_MatrixAngles")
def gen_matrix_angles(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + 0x100, struct.pack("<9h", *[rng.choice([0x1000, -0x1000, 0, 0x800, rng.randrange(-0x1000, 0x1000)]) for _ in range(9)]))
    img.put(env.slab + 0x200, noise(rng, 8))
    return [env.slab + 0x200, env.slab + 0x100], img.pokes()


@m3(0x00466030, "Blockmap_ClearRuntimeGrid")
def gen_clear_grid(env, n):
    img, rng = Image(), env.rng
    img.put(0x005672C0, noise(rng, 0xCC0 + 0x660))
    return [rng.choice([0, 0, -1 & 0xFFFFFFFF, 1, 2, 0x7FFFFFFF, -2 & 0xFFFFFFFF, -1 & 0xFFFFFFFF])], img.pokes()


@m3(0x00466080, "Blockmap_ClearGrid0")
def gen_clear_grid0(env, n):
    img, rng = Image(), env.rng
    img.put(0x005672C0, noise(rng, 0xCC0 + 0x660))
    return [], img.pokes()


# ---- the frame functions -------------------------------------------------------------------------------------------------------------------------------------------------------------------

@sc(0x0045F3F0, "M3d_RenderCleanup")
def gen_cleanup(env, n):
    img, rng = Image(), env.rng
    img.put(DB_FIRST, noise(rng, DB_BYTES))
    for base in (BUFFER_A, BUFFER_B):
        img.u32(base + 0x84, env.slab + 0x3F00)
    img.u32(0x00560FD4, rng.choice([BUFFER_A, BUFFER_B]))
    img.u32(0x00560FD0, env.slab + 0x3000 + 4 * rng.randrange(0, 8))
    img.u32(0x0056433C, rng.choice([env.slab + 0x3F00, env.slab + 0x3F00, env.slab + 0x3F00 + 0x1000]))
    img.put(env.slab + 0x3F00, noise(rng, 0x20))
    img.put(env.slab + 0xBEF8, noise(rng, 8))
    img.put(env.slab + 0xCEF8, noise(rng, 8))
    edges = env.slab + 0x100
    img.put(edges, noise(rng, 0x10))
    img.u32(0x00563A38, edges)
    img.u32(0x029DA3A0, rng.choice([0, 0, 1]))
    img.u32(0x029DA394, rng.choice([0x200, 0x2AA, 0x400, rng.randrange(0x100, 0x500)]))
    img.u32(0x029DA398, rng.choice([0xF0, 0x1E0, 0x168, rng.randrange(0x80, 0x400)]))
    img.put(0x005620A0, noise(rng, 0x20))
    return [], img.pokes()


@sc(0x00460A90, "Render_SubmitObjectList")
def gen_submit_list(env, n):
    img, rng = Image(), env.rng
    img.put(0x005620A0, noise(rng, 0x60))
    img.put(0x005620E0, noise(rng, 0x24))
    img.u32(0x005620E0, env.slab + 0x100)
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(env.slab + 0x100, noise(rng, 0x40))
    if rng.random() < 0.7:
        img.put(0x005620C0, bytes(0x12))
        img.put(0x005620E8, bytes(0x12))
    img.u32(0x0055FA3C, rng.choice([0, 0, 1]))
    img.u32(0x00533F90, word(rng))
    # (the cull sweeps the list: three objects linked by +0x20, each 0x80 bytes with the visibility word at +4)
    objects = [env.slab + 0x200 + 0x80 * k for k in range(rng.choice([1, 2, 3]))]
    # (the culling reads the object's model: a table in each region record the objects name, its first entry a small model whose box the planes are tested against)
    table, model = env.slab + 0x800, env.slab + 0x840
    img.u32(table, model)
    img.put(model, noise(rng, 0x20))
    img.u16(model + 6, rng.choice([0, 5, 11, 40]))
    img.u32(model + 8, rng.randrange(0, 0x40) << 12)
    for k in range(3):
        img.u32(REGION_RECORDS + REGION_STRIDE * k + 0x14, table)
    camera = env.slab + 0x100
    for k, at in enumerate(objects):
        img.put(at, noise(rng, 0x80))
        img.u32(at + 0x20, objects[k + 1] if k + 1 < len(objects) else 0)
        img.u8(at + 0x1F, rng.randrange(0, 3))
        img.u16(at + 0x1A, 0)
        if rng.random() < 0.7:
            for axis in range(3):
                img.u32(at + 8 + 4 * axis, ((rng.randrange(-300, 300)) << 12) & 0xFFFFFFFF)
            img.u16(at + 4, rng.choice([0, 0, 2, 0x200, 0x2000, 0x202, rng.getrandbits(16)]))
        img.u32(camera + 4, 0)
        img.u32(camera + 8, 0)
        img.u32(camera + 12, 0)
    img.put(0x006A3E00, noise(rng, 0x120))
    return [objects[0] if rng.random() < 0.97 else 0], img.pokes()


@sc(0x004667E0, "M3D_LoadZoneBlockmap")
def gen_zone_blockmap(env, n):
    img, rng = Image(), env.rng
    img.put(0x005672C0, noise(rng, 0xCC0 + 0x660))
    record = env.slab + ZONE
    width = rng.choice([1, 2, 3, 4])
    height = rng.choice([1, 2, 3])
    low = rng.randrange(-0x10000, 0)
    img.u32(record, low & 0xFFFFFFFF)
    img.u32(record + 4, word(rng))
    img.u32(record + 8, (low + rng.choice([0x1000, 0x2000, 0x8000]) * width) & 0xFFFFFFFF)
    img.u32(record + 12, word(rng))
    img.u32(record + 16, width | (height << 16))
    at = record + 20
    for row in range(height):
        for column in range(width):
            items = rng.choice([0, 1, 2, 3])
            img.u32(at, word(rng))
            img.u32(at + 4, word(rng))
            img.u32(at + 8, items)
            at += 12
            for k in range(items):
                img.u32(at, rng.randrange(0, 20))
                at += 4
            img.u32(at, word(rng))
            at += 4
    img.u32(0x00537C80, rng.randrange(0, 21))
    img.put(REGION_RECORDS, noise(rng, 22 * REGION_STRIDE))
    return [0, record], img.pokes()


@sc(0x00466A00, "ReplayLoadingBanner")
def gen_loading_banner(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x00561C74, rng.randrange(0, 4))
    img.u32(0x00561C90, rng.randrange(0, 3))
    for k in range(4):
        img.u32(0x00533F40 + 4 * k, env.slab + 0x100 + 0x20 * k)
        img.put(env.slab + 0x100 + 0x20 * k, b"State%d" % k + b"\0")
    for k in range(3):
        img.put(0x0053C1F8 + 0x1AC * k, b"Level%d" % k + b"\0")
        img.u32(0x0053A3B8 + 0x170 * k, env.slab + 0x200 + 0x20 * k)
        img.put(env.slab + 0x200 + 0x20 * k, b"Skater%d" % k + b"\0")
    return [], img.pokes()
