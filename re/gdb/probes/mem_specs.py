"""Phase 4.14b probe: the heap (group mem; mem.cpp, 0x0046e7f0..0x0046f52f).

The heap is the game's allocator, so the forced calls run it on a heap of their own: the zone table, the free-list heads and the use counters (0x0056a964..0x0056a99b) are poked per case to
describe two zones in the scratch slab, each cut into blocks (used blocks carry their zone in the tag nibble of the header, free blocks a random junk nibble and the link to the next free
block, which is how the real heap looks after its splits). Everything else is the real retail code: the assertion routine and `printf` are the only stubs (the shared ones), and the two
wrappers that other groups stub (`FileIO_AllocLoadedBuffer`, `Mem_Delete`) are functions under test here.

The out-of-memory stop `Bang` (0x0046ee20) spins for ever after a print, so a case never asks for a block that is not there (zone 1 with the flag set, or the top allocation).

Slab layout (offsets): zone 0 area 0x000..0x800, zone 1 area 0x800..0x1000, buffers 0x1000 / 0x1400 (0x400 each).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, Skip, spec
from frontend_specs_menu import Image
from platform_specs import mark_under_test

GROUPS = ("mem",)

SLAB = 0x1800
ZONE_AREA = (0x000, 0x800), (0x800, 0x800)
BUF_A = 0x1000
BUF_B = 0x1400

WINDOW = 0x0056A964
WINDOW_BYTES = 0x38
HEAD = 0x0056A964                   # dword per zone
USED = 0x0056A96C                   # dword per zone
SPARE = (0x0056A974, 0x0056A978, 0x0056A998)      # words nothing writes (Mem_GetFree reads them for a zone outside 0..1)
ZONES = 0x0056A97C                  # begin, end per zone
FLAG = 0x0056A98C
LIMIT = 0x0056A990
COUNTER = 0x0056A994

for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = lambda slab: [(slab, SLAB), (WINDOW, WINDOW_BYTES)]
    READ_ONLY[_group] = []


def u(value):
    return value & 0xFFFFFFFF


def rnd4(size):
    return u(size + 3) & 0xFFFFFFFC


class Block:
    def __init__(self, address, size, free, junk):
        self.address = address
        self.size = size
        self.free = free
        self.junk = junk

    @property
    def payload(self):
        return self.address + 8

    @property
    def end(self):
        return self.address + 8 + self.size


class Zone:
    def __init__(self, index, begin, end, blocks):
        self.index = index
        self.begin = begin
        self.end = end
        self.blocks = blocks

    def free(self):
        return [b for b in self.blocks if b.free]

    def used(self):
        return [b for b in self.blocks if not b.free]


def junk_nibble(rng):
    return rng.choice([3, 3, 3, 0, 1, 2, rng.randrange(16), rng.randrange(16)])


def build_zone(rng, index, begin, nbytes, shape):
    """The blocks of one zone: payload sizes in multiples of 4, no two free blocks side by side (the list merges them)."""
    end = begin + nbytes
    blocks = []
    pos = begin
    while pos < end:
        left = end - pos - 8
        if left <= 44 or shape == "one":
            size = left
        else:
            kind = rng.random()
            if kind < 0.6:
                size = 4 * rng.randrange(1, 20)
            elif kind < 0.9:
                size = 4 * rng.randrange(1, 80)
            else:
                size = 4 * rng.randrange(1, left // 4)
            if left - size < 12:
                size = left
        blocks.append([pos, size])
        pos += 8 + size
    out = []
    bias = {"busy": 0.2, "empty": 0.8, "mixed": 0.45, "one": 1.0, "full": 0.0}[shape]
    previous_free = False
    for address, size in blocks:
        free = rng.random() < bias and not previous_free
        if shape == "one":
            free = True
        out.append(Block(address, size, free, junk_nibble(rng)))
        previous_free = free
    return Zone(index, begin, end, out)


class World:
    def __init__(self, zones):
        self.zones = zones


def write_zone(img, zone, rng, junk_payload=False):
    free = zone.free()
    for k, block in enumerate(zone.blocks):
        nibble = block.junk if block.free else zone.index
        img.u32(block.address, (block.size << 4) | nibble)
        if block.free:
            following = [b for b in free if b.address > block.address]
            img.u32(block.address + 4, following[0].address if following else 0)
        if junk_payload and block.size >= 8 and rng.random() < 0.3:
            img.u32(block.address + 8, rng.choice([0x33333333, 0, rng.getrandbits(32)]))
    img.u32(HEAD + 4 * zone.index, free[0].address if free else 0)


def heap_world(img, env, rng, shapes=("mixed", "mixed", "busy", "empty", "one", "full"), wild_words=True):
    """Two zones in the slab and the globals that describe them."""
    slab = env.slab
    zones = []
    for index, (offset, nbytes) in enumerate(ZONE_AREA):
        size = rng.choice([nbytes, nbytes, nbytes, nbytes // 2, 0x100, 0x80, 4 * rng.randrange(0x10, nbytes // 4)])
        start = slab + offset + rng.choice([0, 0, 0, 4 * rng.randrange(0, 16)])
        size = min(size, slab + offset + nbytes - start)
        zone = build_zone(rng, index, start, size, rng.choice(shapes))
        zones.append(zone)
        write_zone(img, zone, rng)
        img.u32(ZONES + 8 * index, zone.begin)
        img.u32(ZONES + 8 * index + 4, zone.end)
        used = sum(8 + b.size for b in zone.used())
        img.u32(USED + 4 * index, used if rng.random() < 0.85 else rng.choice([0, used + 16, rng.getrandbits(32)]))
    z1 = zones[1]
    exact = u(98 * (z1.end - z1.begin)) // 100
    used1 = sum(8 + b.size for b in z1.used())
    limit = rng.choice([exact, exact, exact, used1, used1 + 1, max(used1 - 1, 0), rng.randrange(0, 0x1000), rng.getrandbits(32)])
    img.u32(LIMIT, u(limit))
    img.u32(FLAG, rng.choice([0, 0, 1, rng.getrandbits(8)]))
    img.u32(COUNTER, rng.choice([0, 1, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))
    for address in SPARE:
        img.u32(address, rng.getrandbits(32) if wild_words else 0)
    return World(zones)


def junk_word(rng):
    return rng.choice([0x33333333, 0, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32), 0x00000013])


def scatter(img, rng, address):
    """Junk at an address inside a free block's payload (the tag nibble of a block cut there comes from it)."""
    img.u32(address, junk_word(rng))


def mem(address, name):
    return spec(address, name, "mem")


def retry(generate):
    """A case needs a state the random world sometimes lacks: try again with a new world."""

    def wrapped(env, n):
        for _ in range(200):
            try:
                return generate(env, n)
            except Skip:
                continue
        raise Skip()

    wrapped.__name__ = generate.__name__
    return wrapped


# ---- the free list ----------------------------------------------------------------------------------------------------------------------------------------------------------------

@mem(0x0046E7F0, "AddToFreeList")
@retry
def gen_add_to_free_list(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone = rng.choice([0, 1])
    used = world.zones[zone].used()
    if not used:
        raise Skip()
    block = rng.choice(used)
    return [block.address, zone], img.pokes()


@mem(0x0046E8C0, "Mem_Init")
@retry
def gen_init(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    heap_world(img, env, rng)
    # the table: two spans in the slab (sometimes too small: the assertion fires), the first words of each carry a random tag nibble
    spans = []
    for index, (offset, nbytes) in enumerate(ZONE_AREA):
        begin = slab + offset + 4 * rng.randrange(0, 32)
        kind = rng.random()
        if kind < 0.8:
            end = slab + offset + nbytes - 4 * rng.randrange(0, 16)
        elif kind < 0.9:
            end = begin + rng.choice([8, 12, 16, 4])
        else:
            end = begin + 4 * rng.randrange(0, 3)
        spans.append((begin, end))
        img.u32(ZONES + 8 * index, begin)
        img.u32(ZONES + 8 * index + 4, end)
        img.u32(begin, rng.choice([0, 3, rng.getrandbits(32)]))
    return [], img.pokes()


# ---- allocation -------------------------------------------------------------------------------------------------------------------------------------------------------------------

def pick_size(rng, zone, kind):
    """A request for the given kind of fit: the sizes of the free blocks (exact, a little less, a little more), small, or too big."""
    free = zone.free()
    if kind == "small":
        return rng.choice([1, 2, 3, 4, 5, 8, 12, 16, 20, 33, 64, 100, 128])
    if kind == "zero":
        return 0
    if kind == "huge":
        return rng.choice([0x10000, 0x7FFFFF, 0x0FFFFFFF, 0x10000000, 0xFFFFFFFF, 0x900])
    block = rng.choice(free)
    if kind == "exact":
        return block.size - rng.choice([0, 0, 1, 2, 3, 4, 5, 8])
    if kind == "near":
        return max(block.size - rng.choice([8, 9, 11, 12, 13, 16, 20]), 1)
    return rng.randrange(1, block.size + 1)


def fits(zone, size):
    return any(b.size >= rnd4(size) for b in zone.free())


def junk_all_tails(img, rng, zone, size):
    """Junk where a cut could land: in every free block, the word after `size` bytes of its payload (front cut) and the word `size` bytes from its end (top cut)."""
    rounded = rnd4(size)
    for b in zone.free():
        if b.size >= rounded + 12:
            scatter(img, rng, b.address + rounded + 8)
        if b.size >= rounded + 12:
            scatter(img, rng, b.address + b.size - rounded)


def alloc_args(env, rng, img, world, allow_top=True, allow_placement=True):
    """(size, zone, flag, placement) of an allocation that retail answers (never the out-of-memory stop)."""
    z1 = world.zones[1]
    free = z1.free()
    if not free:
        raise Skip()
    mode = rng.choice(["small", "small", "fit", "fit", "exact", "near", "huge", "zero", "place", "place", "place_edge", "top", "top"])
    if mode in ("top",):
        if not allow_top:
            raise Skip()
        size = pick_size(rng, z1, rng.choice(["small", "fit", "exact", "near"]))
        if not fits(z1, size):
            raise Skip()
        junk_all_tails(img, rng, z1, size)
        return size, 0, rng.choice([0, 1]), 0
    if mode.startswith("place"):
        if not allow_placement:
            raise Skip()
        block = rng.choice(free)
        rounded = rnd4(rng.choice([4, 8, 12, 16, 24, 40, 64, rng.randrange(1, max(block.size // 2, 2))]))
        if block.size < rounded + 32:
            raise Skip()
        if mode == "place":
            lo = block.address + 16
            hi = block.address + 8 + block.size - rounded - 16
            if hi <= lo:
                raise Skip()
            placement = lo + 4 * rng.randrange(0, (hi - lo) // 4 + 1)
        else:
            # near the end of the block (the cut has no room for a tail) or past the last free block (retail has no case for it either)
            placement = rng.choice([block.address + block.size - rounded + 4 * rng.randrange(0, 3), block.address + block.size + 8 - rounded - 4])
            placement = max(placement, block.address + 16)
        scatter(img, rng, placement - 8 + rounded + 8)
        return rounded - rng.choice([0, 0, 1, 2, 3]) if rounded > 3 else rounded, 1, rng.choice([0, 1]), placement
    size = pick_size(rng, z1, mode)
    flag = rng.choice([0, 1])
    if not fits(z1, size):
        flag = 0
    if size == 0 or size > 0x0FFFFFFE:
        flag = 0
    junk_all_tails(img, rng, z1, size)
    return size, 1, flag, 0


@mem(0x0046E9C0, "Mem_NewX")
@retry
def gen_new_x(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    size, zone, flag, placement = alloc_args(env, rng, img, world)
    return [size, zone, flag, placement], img.pokes()


@mem(0x0046EE40, "Mem_NewTop")
@retry
def gen_new_top(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    z1 = world.zones[1]
    if not z1.free():
        raise Skip()
    size = pick_size(rng, z1, rng.choice(["small", "fit", "exact", "near", "zero", "small"]))
    if not fits(z1, size):
        raise Skip()
    junk_all_tails(img, rng, z1, size)
    return [size], img.pokes()


@mem(0x0046F490, "FileIO_AllocLoadedBuffer")
@retry
def gen_alloc_loaded(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    z1 = world.zones[1]
    size, zone, flag, placement = alloc_args(env, rng, img, world)
    if size > 0x0FFFFFF0:
        raise Skip()
    # the wrapper asks NewX for 4 bytes less than it is given (at least 4) and adds 0x10 to a placement
    wrapper_size = size + 4
    effective = (wrapper_size if wrapper_size > 4 else 8) - 4
    if placement:
        placement -= 0x10
        if placement < z1.begin:
            raise Skip()
    elif zone == 0 and not fits(z1, effective):
        raise Skip()
    elif zone == 1 and not fits(z1, effective):
        flag = 0
    return [wrapper_size, zone, flag, placement], img.pokes()


# ---- release ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def pick_used(world, rng):
    zone = rng.choice(world.zones)
    used = zone.used()
    if not used:
        raise Skip()
    return zone, rng.choice(used)


def break_tag(img, rng, block, probability=0.08):
    """A header with an unknown zone in its tag (the assertion fires)."""
    if rng.random() < probability:
        img.u32(block.address, (block.size << 4) | rng.randrange(2, 16))


@mem(0x0046F000, "Mem_DeleteX")
@retry
def gen_delete_x(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    break_tag(img, rng, block)
    return [block.payload], img.pokes()


@mem(0x0046F4D0, "Mem_Delete")
@retry
def gen_delete(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    break_tag(img, rng, block)
    return [block.payload - 4], img.pokes()


def shrink_size(rng, block):
    kind = rng.random()
    if kind < 0.6:
        size = 4 * rng.randrange(0, block.size // 4 + 1)
    elif kind < 0.75:
        size = max(block.size - rng.choice([0, 4, 8, 12, 16]), 0) & ~3
    elif kind < 0.85:
        size = rng.randrange(0, block.size + 1)         # (not a multiple of 4: the alignment assertion fires)
    else:
        size = block.size + 4 * rng.randrange(1, 5)     # (bigger than the block: the size assertion fires and nothing shrinks)
    return size


@mem(0x0046F0E0, "Mem_ShrinkX")
@retry
def gen_shrink_x(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    size = shrink_size(rng, block)
    if size + 4 <= block.size:
        scatter(img, rng, block.payload + size)
    if size >= u(block.size - 8):
        break_tag(img, rng, block, 0.3)         # (an unknown zone is only safe where nothing shrinks)
    return [block.payload, size], img.pokes()


@mem(0x0046F4E0, "Mem_Shrink")
@retry
def gen_shrink(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    wrapped = rng.choice([4 * rng.randrange(1, block.size // 4 + 2), rng.randrange(0, 12), block.size, block.size + 4])
    size = (wrapped if wrapped > 4 else 8) - 4
    if size + 4 <= block.size:
        scatter(img, rng, block.payload + size)
    if size >= u(block.size - 8):
        break_tag(img, rng, block, 0.3)
    return [block.payload - 4, wrapped], img.pokes()


@mem(0x0046F250, "Mem_GetBlockSizeX")
@retry
def gen_block_size_x(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    break_tag(img, rng, block, 0.2)
    return [block.payload], img.pokes()


@mem(0x0046F510, "Mem_GetBlockSize")
@retry
def gen_block_size(env, n):
    img, rng = Image(), env.rng
    world = heap_world(img, env, rng)
    zone, block = pick_used(world, rng)
    break_tag(img, rng, block, 0.2)
    return [block.payload - 4], img.pokes()


@mem(0x0046F2C0, "Mem_GetFree")
def gen_get_free(env, n):
    img, rng = Image(), env.rng
    heap_world(img, env, rng)
    return [u(rng.choice([0, 1, 0, 1, -1, 2, 3, -2]) & 0xFF) | (rng.getrandbits(24) << 8)], img.pokes()


# ---- copies -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def buffers(env, rng, img):
    heap_world(img, env, rng)
    img.put(env.slab + BUF_A, bytes(rng.getrandbits(8) for _ in range(0x200)))
    img.put(env.slab + BUF_B + 0x200, bytes(rng.getrandbits(8) for _ in range(0x200)))
    img.put(env.slab + BUF_B, bytes(rng.getrandbits(8) for _ in range(0x100)))


def copy_args(env, rng, img, aligned_bias=0.8):
    buffers(env, rng, img)
    dst = env.slab + rng.choice([BUF_A, BUF_B]) + rng.randrange(0, 0x40)
    src = env.slab + rng.choice([BUF_A, BUF_B]) + rng.randrange(0, 0x40)
    if rng.random() < aligned_bias:
        dst &= ~3
        src &= ~3
    return dst, src


@mem(0x0046F2E0, "Mem_Copy")
def gen_copy(env, n):
    img, rng = Image(), env.rng
    dst, src = copy_args(env, rng, img)
    count = rng.choice([0, 1, 2, 3, 4, 5, 7, 8, 16, 33, rng.randrange(0, 0x100), -1, -2, -3, -4, -5, -7])
    return [dst, src, u(count)], img.pokes()


@mem(0x0046F3B0, "Mem_CopyBytes")
def gen_copy_bytes(env, n):
    img, rng = Image(), env.rng
    dst, src = copy_args(env, rng, img, 0.3)
    count = rng.choice([0, 1, 2, 3, 4, 5, 7, 8, 16, 33, rng.randrange(0, 0x100), -1, -8])
    return [dst, src, u(count)], img.pokes()


@mem(0x0046F3E0, "Mem_Swap")
def gen_swap(env, n):
    img, rng = Image(), env.rng
    dst, src = copy_args(env, rng, img, 0.3)
    count = rng.choice([0, 1, 2, 3, 4, 5, 7, 8, 16, 33, rng.randrange(0, 0x100), 0x1F0])
    if rng.random() < 0.2:
        src = dst                                            # (the same bytes: nothing changes)
    return [dst, src, count], img.pokes()


for _address, _name in ((0x0046F460, "Mem_SuperDebugUsage"), (0x0046F470, "Mem_NoCheck"), (0x0046F480, "SuperMem_CheckForLeaks")):
    def _gen(env, n):
        img, rng = Image(), env.rng
        heap_world(img, env, rng)
        return [u(rng.getrandbits(32)) for _ in range(rng.choice([0, 1, 2]))], img.pokes()

    mem(_address, _name)(_gen)


def install_mem_stubs(fc, env):
    """Nothing beyond the shared assertion and log stubs (the heap's own wrappers are under test): the timer thread must not run while a case does."""
    if getattr(fc, "_mem_installed", False):
        return
    fc._mem_installed = True
    mark_under_test()
    from platform_specs import stop_clock

    stop_clock(fc)


GROUP_STUBS = {group: install_mem_stubs for group in GROUPS}
