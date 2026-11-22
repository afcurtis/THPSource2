"""Phase 4.11a case generators: the PSX region slots and the scene-material table of spool.cpp (0x004b1ac0..).

The probe (frontend_calls.py with the group names `psxmat` / `psxreg`) forces calls of the retail functions on the live
process, but the state they work on is made up: the seed zeroes the region table (0x0056d428, 20 slots of 0x44 bytes), the
material hash table (0x0056cc18) and the spool queue, and lays a small PSX world out in the scratch slab.  Each case pokes a
random legal state into the slots and calls one function; the native replay (psx_spool_retail_calls_test.cpp) starts from the
same bytes.

Slab layout (offsets from the scratch slab):
  0x0000  24 scene materials (0x30 apart, 0x2c bytes: usage u16 +0x10, loaded byte +0x12, texture +0x14, checksum +0x18,
          palette node +0x1c, rectangle +0x20/+0x22, hash links +0x24/+0x28)
  0x0600  8 palette lookup nodes (0x10 apart, the use count is the u16 at +4)
  0x0700  16 environment objects (0x50 apart: vtable +0, list link +0x20)
  0x0c00  the fake object vtable (slot 0 = the virtual delete)
  0x0c40  6 model tables (0x40 apart: count, then the model pointers)
  0x0d00  names (0x10 apart)
  0x1000  6 region images of 0x400 bytes (header, chunks, trailing tables)
  0x3000  the allocation arena (0x1000)
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, Skip, spec

VIRTUAL_DELETE = 0x004AD9B0

SLOT_BASE = 0x0056D428
STRIDE = 0x44
BUCKETS = 0x0056CC18
ITER_NODE = 0x0056D418
ITER_BUCKET = 0x0056D424
ENV_TABLES = 0x0056D41C
SAVED = 0x0056D978
QUEUE = 0x0056D9C8
ENV_CURRENT = 0x0056DB1C
ENV_ATTACHED = 0x0056DB20
ENV_HEAD = 0x0056DB28
ENV_CURSOR = 0x0056DB2C
Q_READ = 0x0056DB30
Q_WRITE = 0x0056DB34
BITS_LIST = 0x0056DB38
MAT_BYTES = 0x0056DB44
SPOOL_MODE = 0x0056DB48
ENV_SLOTS = 0x00537C80
LOADING = 0x005615D0
STATIC_WINDOW = (0x0056CC18, 0x0056DB50 - 0x0056CC18)

MATS = 0x0000
MAT_STRIDE = 0x30
MAT_COUNT = 24
NODES = 0x0600
NODE_COUNT = 8
OBJS = 0x0700
OBJ_STRIDE = 0x50
OBJ_COUNT = 16
VT = 0x0C00
MODEL_TABLES = 0x0C40
MODEL_TABLE_STRIDE = 0x40
NAMES = 0x0E00
REGS = 0x1000
REG_SIZE = 0x800
REG_COUNT = 6
ANIMS = 0x4000
ANIM_COUNT = 2
ARENA = 0x5000
ARENA_SIZE = 0x4000
SCRATCH = 0x9000
SCRATCH_SIZE = 0x1000
SKATER = 0xA000
SKATER_SIZE = 0x2D00
PART_TABLES = 0xCE00
SLAB = 0xD000
MAGIC = 0xB10C0001
SCRATCH_NAME = NAMES + 0x80

# the object chains the environment cases use: head object -> ... (indices into the object array)
CHAINS = [[0, 1, 2], [3, 4], [5], [6, 7], [8, 9, 10], [11]]
NAME_POOL = ["warehouse", "hangar", "school", "mall", "skhead", "tony", "bits", "items", "enviro", "sk2", "park", "ny"]


class Ov:
    """A byte overlay: the writes that put the game in a case's state (later writes win, overlapping pokes are merged)."""

    def __init__(self):
        self.bytes: dict[int, int] = {}

    def put(self, address: int, data: bytes) -> None:
        for i, b in enumerate(data):
            self.bytes[address + i] = b

    def u32(self, address: int, value: int) -> None:
        self.put(address, u32(value))

    def u16(self, address: int, value: int) -> None:
        self.put(address, u16(value))

    def u8(self, address: int, value: int) -> None:
        self.put(address, u8(value))

    def pokes(self) -> dict[int, bytes]:
        out: dict[int, bytes] = {}
        run_start = None
        run: list[int] = []
        for address in sorted(self.bytes):
            if run_start is not None and address == run_start + len(run):
                run.append(self.bytes[address])
            else:
                if run_start is not None:
                    out[run_start] = bytes(run)
                run_start, run = address, [self.bytes[address]]
        if run_start is not None:
            out[run_start] = bytes(run)
        return out


def slot_addr(i: int) -> int:
    return SLOT_BASE + i * STRIDE


def regions(slab):
    return [(slab + MATS, MAT_COUNT * MAT_STRIDE), (slab + NODES, NODE_COUNT * 0x10), (slab + OBJS, OBJ_COUNT * OBJ_STRIDE),
            (slab + MODEL_TABLES, REG_COUNT * MODEL_TABLE_STRIDE), (slab + NAMES, 0x100), (slab + REGS, REG_COUNT * REG_SIZE),
            (slab + ANIMS, ANIM_COUNT * REG_SIZE), (slab + ARENA, ARENA_SIZE), (slab + SCRATCH, SCRATCH_SIZE), STATIC_WINDOW, (ENV_SLOTS, 8), (LOADING, 4),
            (slab + SKATER, 0x40), (slab + SKATER + 0x2CC0, 8), (slab + PART_TABLES, 0x100),
            (0x0053A514, 0x170 * 4), (0x00533F38, 4), (0x0056A854, 12), (0x00568648, 12), (0x0056B8D4, 4)]


class World:
    """What the seed laid out (the generators need the addresses and the random choices)."""

    def __init__(self):
        self.checksums: list[int] = []
        self.images: list[dict] = []
        self.anims: list[dict] = []
        self.mat_c: list[int] = []
        self.bits_names: list[str] = []
        self.bits_tags: list[int] = []


world = World()


def parse_block(data, animation_tables=0, animation_hierarchy=0, models=0, model_count=0, rgb=0, hooks=0, wib=0, pulse=0, checksums=0):
    """The words the (stubbed) region parser publishes into the slot, kept in the image at +0x5c; see parse_effect."""
    struct.pack_into("<10I", data, 0x5C, MAGIC, animation_tables, animation_hierarchy, models, model_count, rgb, hooks, wib, pulse, checksums)


def build_image(slab, rng, index, mats):
    """One region image: header, two chunks, the -1 tag, then the trailing tables (checksums, material pointers, the 9- and 0x81-word
    tables, the texture records' offsets), texture records at +0x500, models at +0x600.  Returns the bytes and a description."""
    headers = (0, 1, 2, 1, 2, 0)[index]
    models = (1, 2, 0, 3, 5, 0)[index]
    base = slab + REGS + index * REG_SIZE
    data = bytearray(REG_SIZE)
    struct.pack_into("<I", data, 0, 0x50534D31 + index)
    struct.pack_into("<I", data, 4, 0x90)
    struct.pack_into("<I", data, 8, headers)
    for k in range(headers * 9):
        struct.pack_into("<I", data, 0xC + 4 * k, rng.getrandbits(32))
    struct.pack_into("<I", data, 0xC + headers * 0x24, models)
    at = 0x90
    for tag, size in ((0x1234 + index, 0x10), (0x5678, 8)):
        struct.pack_into("<II", data, at, tag, size)
        for k in range(size // 4):
            struct.pack_into("<I", data, at + 8 + 4 * k, rng.getrandbits(32))
        at += 8 + size
    struct.pack_into("<I", data, at, 0xFFFFFFFF)
    tables = at + 4
    p = tables
    checksums = [rng.getrandbits(32) for _ in range(models)]
    for k, c in enumerate(checksums):
        struct.pack_into("<I", data, p + 4 * k, c)
    p += 4 * models
    refs = rng.sample(range(MAT_COUNT), rng.randrange(2, 9))
    struct.pack_into("<I", data, p, len(refs))
    for k, r in enumerate(refs):
        struct.pack_into("<I", data, p + 4 + 4 * k, slab + MATS + r * MAT_STRIDE)
    p += 4 + 4 * len(refs)
    a_count = (0, 1, 2, 1, 0, 1)[index]
    struct.pack_into("<I", data, p, a_count)
    for k in range(a_count * 9):
        struct.pack_into("<I", data, p + 4 + 4 * k, rng.getrandbits(24))
    p += 4 + 36 * a_count
    b_count = (0, 1, 0, 1, 0, 0)[index]
    struct.pack_into("<I", data, p, b_count)
    for k in range(b_count * 0x81):
        struct.pack_into("<I", data, p + 4 + 4 * k, rng.getrandbits(24))
    p += 4 + 0x204 * b_count
    c_count = (2, 3, 1, 2, 3, 0)[index]
    struct.pack_into("<I", data, p, c_count)
    for k in range(c_count):
        struct.pack_into("<I", data, p + 4 + 4 * k, 0x500 + 0x20 * k)
    p += 4 + 4 * c_count
    assert p <= 0x500, hex(p)
    for k in range(c_count):
        rec = 0x500 + 0x20 * k
        struct.pack_into("<IIII", data, rec, rng.getrandbits(8), rng.choice([0x10, 0x100]), rng.getrandbits(16), rng.randrange(len(refs)))
        struct.pack_into("<HH", data, rec + 0x10, rng.randrange(1, 256), rng.randrange(1, 256))
    model_pointers = []
    for k in range(3):
        m = 0x600 + 0xA0 * k
        faces = rng.randrange(1, 4)
        struct.pack_into("<HHH", data, m + 2, 1, 1, faces)
        for f in range(faces):
            face = m + 0x2C + 0x20 * f
            ref = rng.choice(refs)
            struct.pack_into("<I", data, face, rng.choice([0, 0x20]) | (8 << 18))
            struct.pack_into("<I", data, face + 0x10, slab + MATS + (ref if rng.random() < 0.85 else rng.randrange(MAT_COUNT)) * MAT_STRIDE)
            struct.pack_into("<I", data, face + 0x1C, world.mat_c[ref] if rng.random() < 0.8 else rng.getrandbits(32))
        model_pointers.append(base + m)
    # the Rgb colour table the model-file case compacts, and the parse block
    colours = rng.randrange(1, 5)
    struct.pack_into("<II", data, 0x7A0, rng.getrandbits(32), 4 * colours)
    for c in range(colours):
        struct.pack_into("<I", data, 0x7A8 + 4 * c, rng.getrandbits(32))
    table = slab + MODEL_TABLES + index * MODEL_TABLE_STRIDE
    parse_block(data, animation_tables=0, animation_hierarchy=base + 0x7E0, models=table + 4, model_count=models, rgb=base + 0x7A8,
                hooks=0, wib=0, pulse=0, checksums=base + tables)
    return bytes(data), {"models": models, "headers": headers, "tables": tables, "checksums": checksums, "refs": refs, "model_pointers": model_pointers,
                         "records": c_count, "a": a_count, "b": b_count, "rgb": colours}


ANIM_TABLE = 0x300


def build_anim_image(slab, rng, k):
    """A loaded animation file: header, a chunk, trailing tables with a few material pointers, and the animation table at +0x300."""
    base = slab + ANIMS + k * REG_SIZE
    data = bytearray(REG_SIZE)
    struct.pack_into("<I", data, 0, 0x414E4D31 + k)
    struct.pack_into("<I", data, 4, 0x90)
    struct.pack_into("<II", data, 8, 0, 0)
    at = 0x90
    struct.pack_into("<II", data, at, 0x1234, 8)
    for q in range(2):
        struct.pack_into("<I", data, at + 8 + 4 * q, rng.getrandbits(32))
    at += 16
    struct.pack_into("<I", data, at, 0xFFFFFFFF)
    tables = at + 4
    refs = rng.sample(range(MAT_COUNT), rng.randrange(2, 5))
    struct.pack_into("<I", data, tables, len(refs))
    for q, r in enumerate(refs):
        struct.pack_into("<I", data, tables + 4 + 4 * q, slab + MATS + r * MAT_STRIDE)
    n = rng.randrange(2, 7)
    sizes = [rng.randrange(4, 14) for _ in range(n)]
    hierarchy_bytes = 4 * rng.randrange(0, 9)
    s = ANIM_TABLE
    struct.pack_into("<II", data, s - 8, rng.getrandbits(32), rng.getrandbits(32))
    struct.pack_into("<I", data, s, n)
    cumulative = 0
    for q in range(n):
        struct.pack_into("<II", data, s + 4 + 8 * q, 8 * n + cumulative + 4, rng.getrandbits(16))
        cumulative += sizes[q]
    data_at = s + 4 + 8 * n
    for q in range(cumulative):
        data[data_at + q] = rng.getrandbits(8)
    struct.pack_into("<I", data, data_at + cumulative - 4, hierarchy_bytes)
    hierarchy = data_at + cumulative
    for q in range(hierarchy_bytes // 4):
        struct.pack_into("<I", data, hierarchy + 4 * q, rng.getrandbits(32))
    parse_block(data, animation_tables=base + s, animation_hierarchy=base + hierarchy, checksums=base + tables)
    return bytes(data), {"address": base, "animations": n, "tables": tables, "refs": refs, "hierarchy": hierarchy}


def seed_world(env, rng):
    if getattr(env, "psx_seeded", False):
        return {}      # (the groups share one world)
    env.psx_seeded = True
    env.file_image = 0
    env.model_roll = 0
    slab = env.slab
    writes = {}
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    world.__init__()
    # the static window starts clean: no region, no material, an idle queue
    writes[STATIC_WINDOW[0]] = bytes(STATIC_WINDOW[1])
    writes[ENV_SLOTS] = u32(0xFFFFFFFF) + u32(0xFFFFFFFF)
    writes[LOADING] = u32(0)
    # materials: 16 with random checksums, 8 that share a bucket with one of them
    sums = [rng.getrandbits(32) for _ in range(16)]
    for k in range(6):
        sums.append((rng.getrandbits(32) & ~0x1FF) | (sums[k] & 0x1FF))
    sums += [0xA61814C4, 0x68B7B136]          # (the two board textures the texture loader always tiles)
    world.mat_c = [rng.getrandbits(32) for _ in sums]
    world.checksums = sums
    chains: dict[int, list[int]] = {}
    for i, c in enumerate(sums):
        chains.setdefault(c & 0x1FF, []).insert(0, i)       # (the newest material is the head, as the allocator links them)
    mats = [bytearray(MAT_STRIDE) for _ in range(MAT_COUNT)]
    for i, c in enumerate(sums):
        m = mats[i]
        struct.pack_into("<H", m, 0x10, rng.choice([0, 0, 1, 2, 3]))
        m[0x12] = rng.choice([0, 1, 1])
        struct.pack_into("<I", m, 0x14, rng.getrandbits(32))
        struct.pack_into("<I", m, 0x18, c)
        struct.pack_into("<I", m, 0x1C, slab + NODES + 0x10 * rng.randrange(NODE_COUNT))
        struct.pack_into("<HH", m, 0x20, rng.getrandbits(16), rng.getrandbits(16))
        struct.pack_into("<I", m, 0x0C, world.mat_c[i])
    for bucket, chain in chains.items():
        writes[BUCKETS + 4 * bucket] = u32(slab + MATS + chain[0] * MAT_STRIDE)
        for k, i in enumerate(chain):
            nxt = slab + MATS + chain[k + 1] * MAT_STRIDE if k + 1 < len(chain) else 0
            prv = slab + MATS + chain[k - 1] * MAT_STRIDE if k > 0 else 0
            struct.pack_into("<II", mats[i], 0x24, nxt, prv)
    for i in range(MAT_COUNT):
        writes[slab + MATS + i * MAT_STRIDE] = bytes(mats[i])
    writes[MAT_BYTES] = u32(0x30 * len(sums))
    for i in range(NODE_COUNT):
        node = bytearray(0x10)
        struct.pack_into("<HH", node, 0, rng.getrandbits(16), rng.getrandbits(16))
        struct.pack_into("<H", node, 4, rng.choice([0, 1, 2, 3, 5]))
        writes[slab + NODES + i * 0x10] = bytes(node)
    # environment objects and their chains
    for chain in CHAINS:
        for k, o in enumerate(chain):
            obj = bytearray(OBJ_STRIDE)
            struct.pack_into("<I", obj, 0, slab + VT)
            struct.pack_into("<I", obj, 0x20, slab + OBJS + chain[k + 1] * OBJ_STRIDE if k + 1 < len(chain) else 0)
            struct.pack_into("<I", obj, 4, rng.getrandbits(32))
            writes[slab + OBJS + o * OBJ_STRIDE] = bytes(obj)
    writes[slab + VT] = u32(VIRTUAL_DELETE)
    # model tables and region images
    for i in range(REG_COUNT):
        image, info = build_image(slab, rng, i, mats)
        info["address"] = slab + REGS + i * REG_SIZE
        info["table"] = slab + MODEL_TABLES + i * MODEL_TABLE_STRIDE
        world.images.append(info)
        writes[info["address"]] = image
        table = bytearray(MODEL_TABLE_STRIDE)
        struct.pack_into("<I", table, 0, info["models"])
        for k in range(min(info["models"] + 1, 14)):          # (one past the last model is a real model too: the index range assertion does not stop retail)
            struct.pack_into("<I", table, 4 + 4 * k, info["model_pointers"][k % 3])
        writes[info["table"]] = bytes(table)
    for k, name in enumerate(NAME_POOL):
        writes[slab + NAMES + 0x10 * k] = name.encode() + b"\0"
    for k in range(ANIM_COUNT):
        image, info = build_anim_image(slab, rng, k)
        world.anims.append(info)
        writes[info["address"]] = image
    # the named-resource list: three nodes, each pointing at a tag blob (a count, then per animation: an eight byte name, the frame count, the frames)
    world.bits_names = ["walk", "jump", "grind", "slide", "punchitt", "kick", "manual", "Walk_2"]
    tag_at = SCRATCH + 0x500
    names = list(world.bits_names)
    for node in range(3):
        blob = bytearray()
        count = rng.randrange(1, 3)
        blob += u32(count)
        for _ in range(count):
            name = names.pop(0) if names else "more"
            frames = rng.randrange(1, 3)
            blob += name.encode()[:8].ljust(8, b"\0") + u32(frames)
            for _f in range(frames):
                blob += u32(rng.getrandbits(32)) + u32(rng.randrange(2))
        writes[slab + tag_at] = bytes(blob)
        writes[slab + SCRATCH + 0x400 + 8 * node] = u32(slab + tag_at) + u32(slab + SCRATCH + 0x400 + 8 * (node + 1) if node < 2 else 0)
        world.bits_tags.append(slab + tag_at)
        tag_at += 0x40
    return writes


GROUP_NAMES = ("psxmat", "psxreg", "psxq", "psxbits", "psxanim", "psxtex", "psxsave")
for _g in GROUP_NAMES[:-1]:
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_world
    READ_ONLY[_g] = [(0x0053802C, 8), (0x00533F2C, 0x0C), (0x005365AC, 0x200)]       # ".psx", the two default animation lists
    EXTRA_REGIONS[_g] = regions


def random_slots(env, ov, rng, occupied=0.6, only=None):
    """Random contents for the 20 region slots.  Returns the list of (name, image index) of the occupied ones."""
    slab = env.slab
    placed = []
    for i in range(20):
        base = slot_addr(i)
        if only is not None and i not in only:
            continue
        ov.put(base, bytes(STRIDE))
        placed.append(None)
        if rng.random() > occupied:
            continue
        name = rng.choice(NAME_POOL)
        if rng.random() < 0.15:
            name = name.upper()
        image = rng.randrange(REG_COUNT)
        info = world.images[image]
        state = rng.choice([1, 1, 1, 2, 0])
        ov.put(base, name.encode()[:9] + b"\0")
        ov.u8(base + 0x0A, state)
        ov.u8(base + 0x0B, rng.choice([0, 0, 1]))
        ov.u32(base + 0x0C, info["address"] + info["tables"])
        ov.u32(base + 0x10, 0)
        ov.u32(base + 0x14, info["table"] + 4)
        ov.u32(base + 0x18, info["address"])
        ov.u32(base + 0x1C, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u32(base + 0x20, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u32(base + 0x24, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u32(base + 0x28, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u32(base + 0x2C, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u32(base + 0x30, rng.choice([0, rng.getrandbits(32) | 1]))
        ov.u16(base + 0x38, rng.getrandbits(16))
        ov.u8(base + 0x3B, rng.choice([0, 1]))
        ov.u8(base + 0x3C, rng.choice([0, 1]))
        ov.u8(base + 0x3D, rng.choice([0, 1, 1]))
        ov.u32(base + 0x40, rng.getrandbits(20))
        placed[-1] = (name, image)
    return placed


def pick(rng, seq):
    return seq[rng.randrange(len(seq))]


# ---- scene materials ---------------------------------------------------------------------------------------------------------

def material_states(env, ov, rng):
    """Random usage counts, loaded flags and nodes for the 24 materials (the hash chains stay as seeded)."""
    for i in range(len(world.checksums)):
        a = env.slab + MATS + i * MAT_STRIDE
        ov.u16(a + 0x10, rng.choice([0, 0, 0, 1, 2, 3]))
        ov.u8(a + 0x12, rng.choice([0, 1, 1]))
        ov.u32(a + 0x1C, env.slab + NODES + 0x10 * rng.randrange(NODE_COUNT))
        ov.u16(a + 0x20, rng.getrandbits(16))
        ov.u16(a + 0x22, rng.getrandbits(16))
    for i in range(NODE_COUNT):
        ov.u16(env.slab + NODES + 0x10 * i + 4, rng.choice([0, 1, 1, 2, 3, 5]))
    ov.u32(MAT_BYTES, rng.getrandbits(16) * 0x30)


@spec(0x004B1F70, "PSX_AllocSceneMaterial", "psxmat")
def gen_alloc_material(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(MAT_BYTES, rng.getrandbits(16) * 0x30)
    checksum = rng.choice([rng.getrandbits(32), pick(rng, world.checksums), (pick(rng, world.checksums) & ~0x1FF) | rng.randrange(0x200)])
    return [checksum], ov.pokes()


@spec(0x004B1FD0, "PSX_UnlinkMaterial", "psxmat")
def gen_unlink_material(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(MAT_BYTES, 0x30 + rng.getrandbits(16) * 0x30)
    return [env.slab + MATS + rng.randrange(len(world.checksums)) * MAT_STRIDE], ov.pokes()


@spec(0x004B2030, "PSX_FindSceneMaterial", "psxmat")
def gen_find_material(env, n):
    rng = env.rng
    checksum = rng.choice([rng.getrandbits(32), pick(rng, world.checksums), (pick(rng, world.checksums) & ~0x1FF) | rng.randrange(0x200)])
    return [checksum], {}


@spec(0x004B2060, "PSX_MaterialIteratorBegin", "psxmat")
def gen_iter_begin(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(ITER_NODE, rng.getrandbits(32))
    ov.u32(ITER_BUCKET, rng.getrandbits(10))
    return [], ov.pokes()


@spec(0x004B2080, "PSX_MaterialIteratorNext", "psxmat")
def gen_iter_next(env, n):
    ov = Ov()
    rng = env.rng
    bucket = rng.choice([rng.randrange(0x200), rng.randrange(0x200), 0, 0x1FF, 0x1FE, 0x200, 0x201, rng.randrange(0x200, 0x300)])
    node = rng.choice([0, 0, env.slab + MATS + rng.randrange(len(world.checksums)) * MAT_STRIDE])
    ov.u32(ITER_BUCKET, bucket)
    ov.u32(ITER_NODE, node)
    return [], ov.pokes()


@spec(0x004B20F0, "PSX_FindTrailingTables", "psxmat")
def gen_trailing(env, n):
    info = world.images[env.rng.randrange(REG_COUNT)]
    return [info["address"]], {}


@spec(0x004B2120, "PSX_DecrementMaterialUsage", "psxmat")
def gen_decrement(env, n):
    ov = Ov()
    rng = env.rng
    material_states(env, ov, rng)
    # the images' materials get legal counts most of the time (a zero or ten makes the assertion fire)
    for i in range(len(world.checksums)):
        ov.u16(env.slab + MATS + i * MAT_STRIDE + 0x10, rng.choice([1, 2, 3, 5, 9, 1, 2, 3, 0, 10, 11]))
    region = rng.choice([rng.randrange(20), rng.randrange(20), rng.randrange(20), 20, 0])
    from psx_specs_regions import fill_slot
    random_slots(env, ov, rng, occupied=0.5)
    if region < 20 and rng.random() < 0.9:
        fill_slot(env, ov, rng, region, state=rng.choice([1, 1, 1, 2]))      # (a free slot would make retail read the null raw image)
    elif region >= 20:
        region = 20
    return [region], ov.pokes()


@spec(0x004B2390, "PSX_FreeUnusedMaterials", "psxmat")
def gen_free_unused(env, n):
    ov = Ov()
    rng = env.rng
    material_states(env, ov, rng)
    if rng.random() < 0.3:
        for i in range(len(world.checksums)):
            ov.u16(env.slab + MATS + i * MAT_STRIDE + 0x10, 0)
    ov.u32(ITER_BUCKET, rng.getrandbits(8))
    ov.u32(ITER_NODE, rng.getrandbits(32))
    return [], ov.pokes()


@spec(0x004B2440, "PSX_NothingA", "psxmat")
def gen_nothing(env, n):
    return [], {}


def parse_effect(slot):
    """What the stubbed region parser publishes: if the slot's raw image carries a parse block (+0x5c), its words go into the slot."""
    from opentony.fcall import write_chunks
    from opentony.memory import mem
    base = SLOT_BASE + (slot & 0xFFFFFFFF) * STRIDE
    raw = mem.u32(base + 0x18)
    if raw == 0 or mem.u32(raw + 0x5C) != MAGIC:
        return 0
    words = [mem.u32(raw + 0x60 + 4 * k) for k in range(9)]
    for offset, value in ((0x1C, words[0]), (0x20, words[1]), (0x14, words[2]), (0x28, words[4]), (0x24, words[5]), (0x2C, words[6]), (0x30, words[7]), (0x0C, words[8])):
        write_chunks(base + offset, value.to_bytes(4, "little"))
    write_chunks(base + 0x38, (words[3] & 0xFFFF).to_bytes(2, "little"))
    return 0


def install_stubs(fc, env):
    if getattr(fc, "psx_stubs", False):
        return      # (the groups share one set: two breakpoints at an address corrupt the stack)
    fc.psx_stubs = True
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(0x004DA400, 1, "texture_release")
    fc.stub(0x0048A420, 0, "palette_clear")
    fc.stub(0x00466030, 1, "blockmap_clear")
    fc.stub(0x00449030, 1, "file_open_resource", result=lambda a: env.rng.randrange(0x40, 0x200))
    fc.stub(0x00449230, 1, "file_load_resource")
    fc.stub(0x004494B0, 0, "file_continue")
    fc.stub(0x004B2450, 1, "parse_environment", result=lambda a: parse_effect(a[0]))
    fc.stub(0x00448DD0, 2, "file_open_load", keep=slice(1, 2), text=True, result=lambda a: env.file_image)
    fc.stub(0x004B4940, 6, "parts_manifest")
    fc.stub(0x00492DE0, 0, "trickscript_init")
    fc.stub(0x004B1810, 1, "custom_slot_index", result=lambda a: env.rng.randrange(0, 4))
    fc.stub(0x0048A400, 1, "palette_find_node", result=lambda a: env.slab + NODES + 0x10 * env.rng.randrange(NODE_COUNT))
    fc.stub(0x00416980, 1, "career_get_cheat", result=lambda a: env.rng.choice([0, 1]))
    fc.stub(0x004D8CD0, 4, "texture_prepare")
    fc.stub(0x004DA260, 1, "palette_entries", result=lambda a: env.rng.randrange(0, 24))
    fc.stub(0x0048FBE0, 1, "model_roll", convention="stdcall", result=lambda a: env.model_roll)


GROUP_STUBS = {g: install_stubs for g in GROUP_NAMES}

import psx_specs_regions  # noqa: E402,F401  (registers the region and queue specs)
import psx_specs_anim  # noqa: E402,F401  (registers the named-resource, animation, model and texture specs)
