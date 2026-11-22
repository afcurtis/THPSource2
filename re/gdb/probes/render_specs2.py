"""Phase 4.15e probe: the palette slots (group rpal: pal.cpp 0x0048a0b0..0x0048ab05) and the palette cache of the D3D texture layer (group rpc: 0x004da460..0x004da840).

A palette slot is a flag byte (1: free) in two 256-byte tables (16-colour and 256-colour palettes); the source-lookup nodes are a doubly linked list of 0x18-byte records the generators lay out in
the slab; the cache records (also 0x18 bytes: tag, count, converted colours, source colours, links) are a second list. The VRAM allocator, the image stubs and the CRT allocator are stubs.

Slab layout (offsets): lookup nodes 0x100 (16 of 0x20), cache records 0x300 (16 of 0x20), textures 0x500 (6 of 0x40), colours 0x700 (3 of 0x400), arena 0x1400 (0x2000 bytes).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from render_specs import SCRATCH, noise, word

GROUPS = ("rpal", "rpc")

NODES = 0x100
RECORDS = 0x300
TEXTURES = 0x500
COLOURS = 0x700
COLOUR_BYTES = 0x400
ARENA = 0x1400
ARENA_BYTES = 0x2000
SLAB = ARENA + ARENA_BYTES + 0x100

PAL_WINDOW = (0x0056B2EC, 0x41C)
CACHE_WINDOW = (0x006A04F8, 0x38)
GLOBALS = [(0x00535FAC, 8), PAL_WINDOW, CACHE_WINDOW, (0x029DA3A0, 4)]

LOOKUP_HEAD = 0x0056B2F0
LOOKUP_BYTES = 0x0056B704
CACHE_HEAD = 0x006A0524
CACHE_TAIL = 0x006A0528
CACHE_COUNT = 0x006A0518
TEXTURE_LIST = 0x006A051C


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render2_installed", False):
        return
    fc._render2_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    ensure(fc, 0x005002FE, 1, "engine_005002fe", result=env.alloc)             # crt_new (operator new of the CRT)
    ensure(fc, 0x005002F3, 1, "engine_005002f3")                               # crt_delete
    # the VRAM allocator answers with the strip's position through its two pointer arguments
    ensure(fc, 0x00489440, 8, "engine_00489440", pokes=[(3, 4, lambda a: rng.choice([0, 0x300, 0x340, 0x380, rng.randrange(0, 0x400)])),
                                                        (4, 4, lambda a: rng.choice([0, 0, 0x1E0, rng.randrange(0, 0x200)]))])
    ensure(fc, 0x00489450, 2, "engine_00489450", masks={0: 0xFFFF, 1: 0xFFFF})
    ensure(fc, 0x00489490, 0, "engine_00489490")
    ensure(fc, 0x004E8730, 4, "engine_004e8730", blob=(0, 8), stack_to=SCRATCH)
    ensure(fc, 0x004E8740, 1, "engine_004e8740")
    ensure(fc, 0x004E8770, 2, "engine_004e8770", blob=(0, 8), stack_to=SCRATCH)
    ensure(fc, 0x004D8F10, 4, "engine_004d8f10")                                # the texture re-upload


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def pal(address, name):
    return spec(address, name, "rpal", this=None)


def rpc(address, name):
    return spec(address, name, "rpc", this=None)


def node_at(env, k):
    return env.slab + NODES + 0x20 * k


def record_at(env, k):
    return env.slab + RECORDS + 0x20 * k


def colours_at(env, k):
    return env.slab + COLOURS + COLOUR_BYTES * k


def slot_tables(img, rng):
    """The palette tables: free flags, the counts and the strip positions."""
    img.put(PAL_WINDOW[0], noise(rng, PAL_WINDOW[1]))
    for base in (0x0056B2F4, 0x0056B3F4):
        img.put(base, bytes(rng.choice([0, 1, 1]) for _ in range(256)))
    img.u32(0x00535FAC, rng.choice([0x10, 0x20, 0x100, rng.randrange(0, 0x100), 0]))
    img.u32(0x00535FB0, rng.choice([0x10, 0x20, 0x100, rng.randrange(0, 0x100), 0]))
    img.u32(0x0056B6F8, rng.choice([0, 0x40, 0x380, rng.randrange(0, 0x400)]))
    img.u32(0x0056B6FC, rng.choice([0, 0x40, 0x380, rng.randrange(0, 0x400)]))
    img.u32(0x0056B2EC, rng.choice([0, 0, 0x1E0, rng.randrange(0, 0x200)]))
    img.u32(0x0056B4F4, rng.choice([0, 0, 0x1E0, rng.randrange(0, 0x200)]))


def cache_params(img, rng):
    """The device's pixel format: bits and shifts of each component (a 555 or 565 or 4444 or 8888 surface)."""
    fmt = rng.choice([(5, 5, 5, 1, 10, 5, 0, 15), (5, 6, 5, 0, 11, 5, 0, 0), (4, 4, 4, 4, 8, 4, 0, 12), (8, 8, 8, 8, 16, 8, 0, 24), (5, 5, 5, 1, 10, 5, 0, 15),
                      (rng.randrange(1, 9), rng.randrange(1, 9), rng.randrange(1, 9), rng.randrange(0, 9), rng.randrange(0, 24), rng.randrange(0, 24), rng.randrange(0, 24),
                       rng.randrange(0, 31))])
    rb, gb, bb, ab, rs, gs, bs, as_ = fmt
    img.u32(0x006A0500, rb)
    img.u32(0x006A04FC, gb)
    img.u32(0x006A04F8, bb)
    img.u32(0x006A0504, ab)
    img.u32(0x006A0510, rs)
    img.u32(0x006A050C, gs)
    img.u32(0x006A0508, bs)
    img.u32(0x006A0514, as_)


def cache_list(img, env, rng, count, first=0):
    """`count` cache records linked head to tail (+0x10 next, +0x14 previous); their colour copies and sources are arena-like slab blocks; returns the addresses."""
    addresses = [record_at(env, first + k) for k in range(count)]
    for k, at in enumerate(addresses):
        img.put(at, noise(rng, 0x18))
        img.u32(at + 0x10, addresses[k + 1] if k + 1 < count else 0)
        img.u32(at + 0x14, addresses[k - 1] if k > 0 else 0)
        img.u32(at + 4, rng.choice([0x10, 0x100, rng.randrange(0, 0x40)]))
    img.u32(CACHE_HEAD, addresses[0] if addresses else 0)
    img.u32(CACHE_TAIL, addresses[-1] if addresses else 0)
    img.u32(CACHE_COUNT, count)
    return addresses


def palette_colours(img, rng, at, count):
    """`count` 15-bit colours with magenta (the transparent colour), black and the opaque bit scattered among them."""
    palette = bytearray()
    magenta = rng.random() < 0.4
    for k in range(count):
        choice = rng.random()
        if magenta and choice < 0.1:
            value = 0x7C1F | rng.choice([0, 0x8000])
        elif choice < 0.2:
            value = 0
        else:
            value = rng.getrandbits(16)
        palette += value.to_bytes(2, "little")
    img.put(at, bytes(palette))


# ---- the slots ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@pal(0x0048A0B0, "Pal_Init")
def gen_pal_init(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    img.u32(0x0056B6FC, rng.choice([0, 0, 0, 5]))
    first = rng.choice([0x10, 0x40, 0x100, rng.randrange(0, 0x101)]) if rng.random() < 0.95 else rng.choice([-1, 0x101, 0x7FFFFFFF]) & 0xFFFFFFFF
    second = rng.choice([0x10, 0x40, 0x100, rng.randrange(0, 0x101)]) if rng.random() < 0.95 else rng.choice([-1, 0x101, 0x7FFFFFFF]) & 0xFFFFFFFF
    return [first, second], img.pokes()


@pal(0x0048A320, "Pal_Cleanup")
def gen_pal_cleanup(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    return [], img.pokes()


def lookup_list(img, env, rng, count):
    addresses = [node_at(env, k) for k in range(count)]
    for k, at in enumerate(addresses):
        img.put(at, noise(rng, 0x18))
        img.u32(at + 0x10, addresses[k + 1] if k + 1 < count else 0)
        img.u32(at + 0x14, addresses[k - 1] if k > 0 else 0)
        img.u32(at + 8, rng.choice([0x1000 + k, rng.getrandbits(32)]))
    img.u32(LOOKUP_HEAD, addresses[0] if addresses else 0)
    img.u32(LOOKUP_BYTES, 0x1C * count + rng.choice([0, 0x1C]))
    return addresses


@pal(0x0048A360, "Palette_AllocSourceLookupNode")
def gen_lookup_alloc(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    lookup_list(img, env, rng, rng.choice([0, 0, 1, 3]))
    return [word(rng)], img.pokes()


@pal(0x0048A3B0, "RemovePaletteEntry")
def gen_lookup_remove(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    count = rng.choice([1, 2, 3, 5])
    addresses = lookup_list(img, env, rng, count)
    return [rng.choice(addresses)], img.pokes()


@pal(0x0048A400, "Palette_FindSourceLookupNode")
def gen_lookup_find(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 3, 6])
    addresses = lookup_list(img, env, rng, count)
    wanted = rng.choice([img.b.get(addresses[0] + 8, 0) for _ in range(1)] + [rng.getrandbits(32)]) if addresses else rng.getrandbits(32)
    if addresses and rng.random() < 0.7:
        at = rng.choice(addresses)
        wanted = sum(img.b[at + 8 + i] << (8 * i) for i in range(4))
    return [wanted], img.pokes()


@pal(0x0048A420, "Palette_ClearSourceLookup")
def gen_lookup_clear(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    count = rng.choice([0, 1, 2, 4, 6])
    nodes = lookup_list(img, env, rng, count)
    records = cache_list(img, env, rng, min(16, count + rng.choice([0, 1, 2])))
    for at in nodes:
        img.u16(at + 4, rng.choice([0, 0, 1, 2]))
        img.u8(at + 2, rng.randrange(0, 256))
        img.u8(at + 3, rng.choice([0, 1, 2, 3]))
        img.u32(at + 0xC, rng.choice(records) if records else 0)
    return [], img.pokes()


@pal(0x0048A490, "GetFree16Slot")
def gen_get_free16(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    if rng.random() < 0.2:
        img.put(0x0056B3F4, bytes(256))
    return [], img.pokes()


@pal(0x0048A500, "Free16Slot")
def gen_free16(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    row = rng.randrange(0, 0x20)
    img.u32(0x0056B6F8, rng.choice([0, 0x40, 0x380, rng.randrange(0, 0x400)]))
    img.u32(0x0056B4F4, row)
    x = int.from_bytes(bytes(img.b[0x0056B6F8 + i] for i in range(4)), "little")
    slot = rng.randrange(0, 0x20)
    palette = ((row + slot) << 6) | ((x >> 4) & 0x3F)
    return [palette if rng.random() < 0.9 else rng.getrandbits(16)], img.pokes()


@pal(0x0048A590, "GetFree256Slot")
def gen_get_free256(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    if rng.random() < 0.2:
        img.put(0x0056B2F4, bytes(256))
    return [], img.pokes()


@pal(0x0048A6F0, "Get256SlotNumber")
def gen_slot_number256(env, n):
    return [word(env.rng)], Image().pokes()


@pal(0x0048A700, "Get16SlotNumber")
def gen_slot_number16(env, n):
    return [word(env.rng)], Image().pokes()


@pal(0x0048A7E0, "Palette_CreateFromPsxColors")
def gen_palette_create(env, n):
    img, rng = Image(), env.rng
    slot_tables(img, rng)
    lookup_list(img, env, rng, rng.choice([0, 1, 2]))
    flags = rng.choice([1, 1, 2, 2, 0, 3, 5, 6])
    count = 0x10 if flags & 1 else 0x100
    colours = colours_at(env, 0)
    palette_colours(img, rng, colours, count)
    img.u8(0x0056B700, rng.choice([0, 0, 1]))
    if rng.random() < 0.9:
        img.u32(0x0056B4F4, 0)
        img.u32(0x0056B2EC, 0)
    return [word(rng), colours, flags], img.pokes()


# ---- the cache ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@rpc(0x004DA460, "Palette_AllocCacheRecord")
def gen_cache_alloc(env, n):
    img, rng = Image(), env.rng
    cache_list(img, env, rng, rng.choice([0, 1, 3]))
    if rng.random() < 0.3:
        img.u32(CACHE_TAIL, 0)
    return [], img.pokes()


@rpc(0x004DA4B0, "D3DTEX_FreePaletteEntry")
def gen_free_entry(env, n):
    img, rng = Image(), env.rng
    records = cache_list(img, env, rng, rng.choice([1, 2, 4]))
    return [rng.choice(records), rng.choice([0, 1])], img.pokes()


@rpc(0x004DA4D0, "Palette_FreeCacheRecord")
def gen_cache_free(env, n):
    img, rng = Image(), env.rng
    records = cache_list(img, env, rng, rng.choice([1, 2, 4]))
    return [rng.choice(records), rng.choice([0, 1])], img.pokes()


@rpc(0x004DA550, "Palette_FreeAllCacheRecords")
def gen_cache_free_all(env, n):
    img, rng = Image(), env.rng
    cache_list(img, env, rng, rng.choice([0, 1, 3, 5]))
    return [], img.pokes()


@rpc(0x004DA580, "Palette_CacheSource")
def gen_cache_source(env, n):
    img, rng = Image(), env.rng
    records = cache_list(img, env, rng, 2)
    return [rng.choice(records)], img.pokes()


def texture_at(env, k):
    return env.slab + TEXTURES + 0x40 * k


@rpc(0x004DA590, "Palette_ReloadTexture")
def gen_texture_reload(env, n):
    img, rng = Image(), env.rng
    records = cache_list(img, env, rng, 2)
    texture = texture_at(env, 0)
    img.put(texture, noise(rng, 0x40))
    img.u32(texture + 0x10, rng.getrandbits(32) & ~0x0 if rng.random() < 0.5 else rng.choice([0, 1, 4, 5, 0x201, 0x205]))
    img.u32(texture + 0x20, texture_at(env, 1))
    img.u32(texture_at(env, 1), rng.getrandbits(32))
    img.u32(0x029DA3A0, rng.choice([0, 0, 1]))
    return [texture, rng.choice(records), colours_at(env, 0)], img.pokes()


@rpc(0x004DA620, "Palette_SetCacheColours")
def gen_set_colours(env, n):
    img, rng = Image(), env.rng
    cache_params(img, rng)
    records = cache_list(img, env, rng, 2)
    record = rng.choice(records)
    count = rng.choice([0x10, 0x100, rng.randrange(0, 0x100)])
    img.u32(record + 4, count)
    img.u32(record + 8, colours_at(env, 2))
    img.put(colours_at(env, 2), noise(rng, count * 2))
    palette_colours(img, rng, colours_at(env, 0), count)
    chain = [texture_at(env, k) for k in range(rng.choice([0, 1, 3]))]
    for k, at in enumerate(chain):
        img.put(at, noise(rng, 0x40))
        img.u32(at + 0x10, rng.choice([0, 1, 4, 5]))
        img.u32(at + 0x20, rng.choice(records + [0]) if rng.random() < 0.6 else record)
        img.u32(at + 0x24, chain[k + 1] if k + 1 < len(chain) else 0)
    img.u32(TEXTURE_LIST, chain[0] if chain else 0)
    return [record, colours_at(env, 0)], img.pokes()


@rpc(0x004DA680, "Palette_ConvertColors")
def gen_palette_convert(env, n):
    img, rng = Image(), env.rng
    cache_params(img, rng)
    cache_list(img, env, rng, rng.choice([0, 1, 2]))
    count = rng.choice([0x10, 0x100, rng.randrange(0, 0x100), 1])
    palette_colours(img, rng, colours_at(env, 0), count)
    return [word(rng), colours_at(env, 0), count], img.pokes()


@rpc(0x004DA780, "Palette_ConvertInPlace")
def gen_convert_in_place(env, n):
    img, rng = Image(), env.rng
    cache_params(img, rng)
    count = rng.choice([0x10, 0x100, rng.randrange(0, 0x100), 1])
    palette_colours(img, rng, colours_at(env, 0), count)
    return [colours_at(env, 0), count], img.pokes()


PAL_OBJECT = 0x0600
COPY_TARGET = 0x1300


@rpc(0x0048AAD0, "Pal_CopyToMemory")
def gen_pal_copy(env, n):
    img, rng = Image(), env.rng
    records = cache_list(img, env, rng, 2)
    record = rng.choice(records)
    img.u32(record + 0xC, colours_at(env, 0))
    img.put(colours_at(env, 0), noise(rng, 0x200))
    pal_object = env.slab + PAL_OBJECT
    img.put(pal_object, noise(rng, 0x10))
    img.u32(pal_object + 0xC, record)
    img.put(env.slab + COPY_TARGET, noise(rng, 0x200))
    return [pal_object, env.slab + COPY_TARGET], img.pokes()


@rpc(0x0048AB00, "Pal_StoreFromMemory")
def gen_pal_store(env, n):
    img, rng = Image(), env.rng
    cache_params(img, rng)
    records = cache_list(img, env, rng, 2)
    record = rng.choice(records)
    count = rng.choice([0x10, 0x100, rng.randrange(0, 0x100)])
    img.u32(record + 4, count)
    img.u32(record + 8, colours_at(env, 2))
    img.put(colours_at(env, 2), noise(rng, count * 2))
    palette_colours(img, rng, colours_at(env, 0), count)
    chain = [texture_at(env, k) for k in range(rng.choice([0, 1, 3]))]
    for k, at in enumerate(chain):
        img.put(at, noise(rng, 0x40))
        img.u32(at + 0x10, rng.choice([0, 1, 4, 5]))
        img.u32(at + 0x20, rng.choice(records + [0]) if rng.random() < 0.6 else record)
        img.u32(at + 0x24, chain[k + 1] if k + 1 < len(chain) else 0)
    img.u32(TEXTURE_LIST, chain[0] if chain else 0)
    pal_object = env.slab + PAL_OBJECT
    img.put(pal_object, noise(rng, 0x10))
    img.u32(pal_object + 0xC, record)
    return [pal_object, colours_at(env, 0)], img.pokes()


@rpc(0x004305E0, "Animation_EmptyHook")
def gen_empty_hook(env, n):
    return [], []
