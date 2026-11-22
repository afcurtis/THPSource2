"""Phase 4.11a case generators, part 2: the region table, the environment list and the spool queue (spool.cpp).

Groups `psxreg` (slot lookups, clearing, environments), `psxq` (the queue and the spooler), `psxbits` (the named-resource list).
The world is the one psx_specs.seed_world lays out; the generators here put legal slot / environment / queue states in it.
"""

from __future__ import annotations

from career_state import u8, u16, u32
from psx_specs import (BITS_LIST, CHAINS, ENV_ATTACHED, ENV_CURRENT, ENV_CURSOR, ENV_HEAD, ENV_SLOTS, ITER_BUCKET, ITER_NODE, LOADING, MAT_COUNT, MAT_STRIDE, MATS,
                       NAME_POOL, NAMES, OBJ_STRIDE, OBJS, Ov, Q_READ, Q_WRITE, QUEUE, SAVED, SPOOL_MODE, STRIDE, VT, material_states, pick, slot_addr, world)
from create_specs import Skip, spec

QUEUE_STRIDE = 0x11
FREE_OBJECTS = [12, 13, 14, 15]          # objects no chain uses (the placed objects of the model lookups)
SCRATCH_NAME = NAMES + 0x80               # a name poked per case


def chain_head(env, chain):
    return env.slab + OBJS + CHAINS[chain][0] * OBJ_STRIDE


def chain_tail(env, chain):
    return env.slab + OBJS + CHAINS[chain][-1] * OBJ_STRIDE


def fill_slot(env, ov, rng, i, name=None, image=None, state=None, keep=None, objects=None):
    """Make slot i an occupied slot with consistent pointers (the raw image, its checksum table and the model table of one image)."""
    base = slot_addr(i)
    ov.put(base, bytes(STRIDE))
    name = name or rng.choice(NAME_POOL)
    if rng.random() < 0.15:
        name = name.upper()
    image = rng.randrange(len(world.images)) if image is None else image
    info = world.images[image]
    state = rng.choice([1, 1, 1, 1, 2, 0]) if state is None else state
    ov.put(base, name.encode()[:9] + b"\0")
    ov.u8(base + 0x0A, state)
    ov.u8(base + 0x0B, rng.choice([0, 0, 1]) if keep is None else keep)
    ov.u32(base + 0x0C, info["address"] + info["tables"])
    ov.u32(base + 0x10, 0 if objects is None else objects)
    ov.u32(base + 0x14, info["table"] + 4)
    ov.u32(base + 0x18, info["address"])
    for off in (0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30):
        ov.u32(base + off, rng.choice([0, rng.getrandbits(32) | 1]))
    ov.u16(base + 0x38, rng.getrandbits(16))
    ov.u8(base + 0x3B, rng.choice([0, 1]))
    ov.u8(base + 0x3C, rng.choice([0, 1]))
    ov.u8(base + 0x3D, rng.choice([0, 1, 1]))
    ov.u8(base + 0x3E, rng.choice([0, 1]))
    ov.u8(base + 0x3F, rng.choice([0, 1]))
    ov.u32(base + 0x40, rng.getrandbits(20))
    return name, image


def clean_table(ov):
    """Empty slots (a case that fills some leaves the others blank)."""
    for i in range(20):
        ov.put(slot_addr(i), bytes(STRIDE))


def fill_table(env, ov, rng, occupied=0.6, objects_chains=(4, 5), skip=()):
    """Random slots; some of the occupied ones own an unattached object chain.  Returns the (name, image) of each slot or None."""
    placed = []
    for i in range(20):
        if i in skip:
            placed.append(None)
            continue
        if rng.random() > occupied:
            ov.put(slot_addr(i), bytes(STRIDE))
            placed.append(None)
            continue
        objects = 0
        if rng.random() < 0.2:
            objects = chain_head(env, rng.choice(objects_chains))
        placed.append(fill_slot(env, ov, rng, i, objects=objects))
    return placed


def usage_counts(env, ov, rng, zero_chance=0.1):
    for i in range(len(world.checksums)):
        a = env.slab + MATS + i * MAT_STRIDE
        ov.u16(a + 0x10, 0 if rng.random() < zero_chance else rng.choice([1, 2, 3, 4, 5, 9]))


def env_arrangement(env, ov, rng, placed, k=None, taken=()):
    """A legal environment state: k attached environment regions (0, 1 or 2), their object chains linked as the attach code links them.

    Returns the list of (environment index, slot).  `placed` is updated for the slots it fills."""
    k = rng.choice([0, 1, 1, 2, 2]) if k is None else k
    ov.u32(ENV_SLOTS, 0xFFFFFFFF)
    ov.u32(ENV_SLOTS + 4, 0xFFFFFFFF)
    ov.u32(ENV_ATTACHED, 0)
    ov.u32(ENV_ATTACHED + 4, 0)
    ov.u32(ENV_HEAD, 0)
    ov.u32(ENV_CURRENT, rng.getrandbits(32) | 1)
    ov.u32(ENV_CURSOR, rng.choice([0, 1]))
    if k == 0:
        return []
    free = [i for i in range(20) if i not in taken]
    slots = rng.sample(free, k)
    chains = rng.sample([0, 1, 2, 3], k)
    first_index = rng.choice([0, 1])
    out = []
    for n in range(k):
        index = first_index if n == 0 else first_index ^ 1
        placed[slots[n]] = fill_slot(env, ov, rng, slots[n], state=1, objects=chain_head(env, chains[n]))
        ov.u32(ENV_SLOTS + 4 * index, slots[n])
        ov.u32(ENV_ATTACHED + 4 * index, rng.getrandbits(32) | 1)
        out.append((index, slots[n]))
    if k == 1:
        ov.u32(ENV_HEAD, chain_head(env, chains[0]))
    else:
        # the region attached last is the head of the list; its chain ends in the other one's head
        newest = rng.choice([0, 1])
        older = newest ^ 1
        ov.u32(chain_tail(env, chains[newest]) + 0x20, chain_head(env, chains[older]))
        ov.u32(ENV_HEAD, chain_head(env, chains[newest]))
    return out


def full_state(env, ov, rng, k=None):
    """Slots, material counts and an environment state, all legal."""
    material_states(env, ov, rng)
    usage_counts(env, ov, rng)
    placed = fill_table(env, ov, rng, occupied=rng.choice([0.3, 0.6, 0.9]))
    envs = env_arrangement(env, ov, rng, placed, k=k)
    return placed, envs


def occupied(placed):
    return [i for i, p in enumerate(placed) if p is not None]


def idle_queue(ov):
    for i in range(20):
        ov.u8(QUEUE + i * QUEUE_STRIDE + 0x0D, 0)
    ov.u32(Q_READ, 0)
    ov.u32(Q_WRITE, 0)
    ov.u32(LOADING, 0)
    ov.u32(SPOOL_MODE, 0)


# ---- the slot table -----------------------------------------------------------------------------------------------------------

@spec(0x004B1AC0, "Spool_Init", "psxreg")
def gen_init(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=rng.choice([0.2, 0.7, 1.0]), objects_chains=(0, 1, 2, 3, 4, 5))
    for i in range(20):
        ov.u8(QUEUE + i * QUEUE_STRIDE + 0x0D, rng.choice([0, 1]))
    ov.u32(Q_READ, rng.randrange(20))
    ov.u32(Q_WRITE, rng.randrange(20))
    ov.u32(BITS_LIST, rng.getrandbits(32))
    ov.u32(ENV_HEAD, rng.getrandbits(32))
    ov.u32(ENV_CURSOR, rng.getrandbits(1))
    ov.u32(ENV_SLOTS, rng.getrandbits(5))
    ov.u32(ENV_SLOTS + 4, rng.getrandbits(5))
    for b in rng.sample(range(0x200), 12):
        ov.u32(0x0056CC18 + 4 * b, rng.getrandbits(32) | 1)
    return [], ov.pokes()


@spec(0x004B1BB0, "PSX_FindFreeRegionSlot", "psxreg")
def gen_find_free(env, n):
    ov = Ov()
    rng = env.rng
    fill_table(env, ov, rng, occupied=rng.choice([0.0, 0.3, 0.8, 0.95, 1.0, 1.0]))
    return [], ov.pokes()


@spec(0x004B1D10, "PSX_GetModelPointer", "psxreg")
def gen_model_pointer(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=0.7)
    live = [i for i in occupied(placed)]
    if not live:
        raise Skip
    slot = rng.choice(live)
    models = world.images[placed[slot][1]]["models"]
    obj = env.slab + OBJS + rng.choice(FREE_OBJECTS) * OBJ_STRIDE
    ov.u8(obj + 0x1F, slot)
    ov.u16(obj + 0x1A, rng.randrange(max(models, 1)))
    if rng.random() < 0.15:
        ov.u32(slot_addr(slot) + 0x14, 0)
    return [obj], ov.pokes()


@spec(0x004B1F40, "PSX_GetModelNameChecksum", "psxreg")
def gen_name_checksum(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=0.7)
    live = [i for i in occupied(placed) if world.images[placed[i][1]]["models"] > 0]
    if not live:
        raise Skip
    slot = rng.choice(live)
    models = world.images[placed[slot][1]]["models"]
    obj = env.slab + OBJS + rng.choice(FREE_OBJECTS) * OBJ_STRIDE
    ov.u8(obj + 0x1F, slot)
    ov.u16(obj + 0x1A, rng.randrange(models))
    return [obj], ov.pokes()


@spec(0x004B1DE0, "PSX_FindModelIndexByChecksum", "psxreg")
def gen_find_model_index(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=0.7)
    live = occupied(placed)
    if not live:
        raise Skip
    slot = rng.choice(live)
    info = world.images[placed[slot][1]]
    if rng.random() < 0.15:
        ov.u8(slot_addr(slot) + 0x0A, 0)
    checksum = rng.choice(info["checksums"]) if info["checksums"] and rng.random() < 0.7 else rng.getrandbits(32)
    return [checksum, slot], ov.pokes()


def name_case(env, ov, rng, placed):
    live = occupied(placed)
    if live and rng.random() < 0.7:
        name = placed[rng.choice(live)][0]
        if rng.random() < 0.3:
            name = name.swapcase()
        if rng.random() < 0.15:
            name = name[:-1] or name
    else:
        name = rng.choice(NAME_POOL + ["zzz"])
    ov.put(env.slab + SCRATCH_NAME, name.encode()[:9] + b"\0")
    return env.slab + SCRATCH_NAME


@spec(0x004B3230, "PSX_FindLoadedRegionByName", "psxreg")
def gen_find_by_name(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=rng.choice([0.3, 0.7, 1.0]))
    return [name_case(env, ov, rng, placed)], ov.pokes()


@spec(0x004B3270, "PSX_ClearNamedRegion", "psxreg")
def gen_clear_named(env, n):
    ov = Ov()
    rng = env.rng
    placed, envs = full_state(env, ov, rng)
    return [name_case(env, ov, rng, placed)], ov.pokes()


@spec(0x004B32F0, "PSX_ClearRegion", "psxreg")
def gen_clear_region(env, n):
    ov = Ov()
    rng = env.rng
    placed, envs = full_state(env, ov, rng)
    live = occupied(placed)
    choice = rng.random()
    if envs and choice < 0.4:
        region = rng.choice(envs)[1]
    elif live and choice < 0.9:
        region = rng.choice(live)
    else:
        region = rng.choice([-1, rng.randrange(20)])
    return [region, rng.choice([0, 1]), rng.choice([0, 0, 1])], ov.pokes()


@spec(0x004B3460, "Spool_ClearUnusedRegions", "psxreg")
def gen_clear_unused(env, n):
    ov = Ov()
    placed, envs = full_state(env, ov, env.rng)
    return [], ov.pokes()


@spec(0x004B34B0, "Spool_ClearEnvironmentRegions", "psxreg")
def gen_clear_env(env, n):
    ov = Ov()
    placed, envs = full_state(env, ov, env.rng)
    return [], ov.pokes()


@spec(0x004B32B0, "Spool_ClearAllPSXs", "psxreg")
def gen_clear_all(env, n):
    ov = Ov()
    rng = env.rng
    placed, envs = full_state(env, ov, rng)
    queue_state(env, ov, rng, rng.choice([0, 0, 1, 2]), envs=envs)
    return [], ov.pokes()


@spec(0x004B4640, "PSX_InitFreeSlot", "psxreg")
def gen_init_free(env, n):
    ov = Ov()
    rng = env.rng
    fill_table(env, ov, rng, occupied=rng.choice([0.0, 0.3, 0.8, 0.95]))
    return [], ov.pokes()


@spec(0x004B3990, "Spool_GetEnvIndex", "psxreg")
def gen_env_index(env, n):
    ov = Ov()
    rng = env.rng
    a, b = rng.choice([-1, rng.randrange(20)]), rng.choice([-1, rng.randrange(20)])
    ov.u32(ENV_SLOTS, a & 0xFFFFFFFF)
    ov.u32(ENV_SLOTS + 4, b & 0xFFFFFFFF)
    region = rng.choice([a, b, rng.randrange(20)]) & 0xFFFFFFFF
    return [region], ov.pokes()


@spec(0x004B2AC0, "PSX_AttachEnvironment", "psxreg")
def gen_attach(env, n):
    ov = Ov()
    rng = env.rng
    placed = [None] * 20
    clean_table(ov)
    # the region is already in the environment slots (the spooler stores it before it attaches); an older one may be attached
    k = rng.choice([1, 2, 2])
    envs = env_arrangement(env, ov, rng, placed, k=k)
    if not envs:
        raise Skip
    region = envs[-1][1] if k == 2 else envs[0][1]
    # the region being attached is not yet linked into the list: its chain ends in 0 and the head is the older environment (or nothing)
    index = [i for i, s in envs if s == region][0]
    chain = None
    for c in range(4):
        if chain_head(env, c) == env_objects(ov, region):
            chain = c
    if chain is None:
        raise Skip
    ov.u32(chain_tail(env, chain) + 0x20, 0)
    others = [s for i, s in envs if s != region]
    ov.u32(ENV_HEAD, 0 if not others else env_objects(ov, others[0]))
    if rng.random() < 0.1:
        ov.u32(ENV_SLOTS + 4 * index, 0xFFFFFFFF)      # not in the environment slots: the assertion fires
    return [region], ov.pokes()


def env_objects(ov, slot):
    base = slot_addr(slot) + 0x10
    return sum(ov.bytes.get(base + k, 0) << (8 * k) for k in range(4))


@spec(0x004B3090, "PSX_DetachEnvironment", "psxreg")
def gen_detach(env, n):
    ov = Ov()
    rng = env.rng
    placed = [None] * 20
    clean_table(ov)
    envs = env_arrangement(env, ov, rng, placed, k=rng.choice([1, 2, 2]))
    if not envs:
        raise Skip
    index = rng.choice([i for i, s in envs])
    if rng.random() < 0.08:
        index = 1 - envs[0][0] if len(envs) == 1 else index        # an empty environment slot: the assertion fires
    if len(envs) == 2 and rng.random() < 0.15:
        # the list is not in the order the code expects: the head is the chain of the other environment, not linked to ours
        pass
    return [index], ov.pokes()


# ---- the spool queue ------------------------------------------------------------------------------------------------------------

def queue_name(rng, kind):
    pool = [p for p in NAME_POOL if len(p) <= 8]
    return rng.choice(pool)


def queue_state(env, ov, rng, active, envs=(), kinds=(0, 0, 1, 2), start=None):
    """The queue with `active` entries pending from the read index; the write index follows them.  Returns the entries."""
    idle_queue(ov)
    read = rng.randrange(20) if start is None else start
    ov.u32(Q_READ, read)
    entries = []
    for k in range(active):
        i = (read + k) % 20
        kind = rng.choice(kinds)
        name = queue_name(rng, kind)
        base = QUEUE + i * QUEUE_STRIDE
        ov.put(base, (name + ".psx").encode() + b"\0")
        ov.u8(base + 0x0D, 1)
        ov.u8(base + 0x0E, kind)
        ov.u8(base + 0x0F, rng.randrange(20))
        ov.u8(base + 0x10, rng.choice([0, 1]))
        entries.append((i, kind, name))
    ov.u32(Q_WRITE, (read + active) % 20)
    return entries


@spec(0x004B3750, "PSX_SpoolQueueCopyName", "psxq")
def gen_copy_name(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(Q_WRITE, rng.randrange(20))
    first = rng.choice(NAME_POOL)[:8]
    ov.put(env.slab + SCRATCH_NAME, first.encode() + b"\0")
    ov.put(env.slab + SCRATCH_NAME + 0x20, rng.choice([".psx", ".bmp", "", ".x"]).encode() + b"\0")
    return [env.slab + SCRATCH_NAME, env.slab + SCRATCH_NAME + 0x20], ov.pokes()


@spec(0x004B37A0, "PSX_QueueResource", "psxq")
def gen_queue_resource(env, n):
    ov = Ov()
    rng = env.rng
    placed = fill_table(env, ov, rng, occupied=rng.choice([0.0, 0.3, 0.7, 0.95]))
    entries = queue_state(env, ov, rng, rng.choice([0, 0, 1, 3, 19, 20]), kinds=(0, 1, 2))
    live = occupied(placed)
    roll = rng.random()
    if live and roll < 0.25:
        name = placed[rng.choice(live)][0]
    elif entries and roll < 0.5:
        name = rng.choice(entries)[2]
    else:
        name = rng.choice(NAME_POOL + ["longname9x"])
    ov.put(env.slab + SCRATCH_NAME, name.encode()[:10] + b"\0")
    # the write index points at a free entry unless the queue is full
    return [env.slab + SCRATCH_NAME, rng.choice([0, 1, 2]), rng.randrange(256), rng.choice([0, 1, 1, 2])], ov.pokes()


def spooler_state(env, ov, rng, mode=None, kinds=(0, 0, 1, 2)):
    placed, envs = full_state(env, ov, rng)
    entries = queue_state(env, ov, rng, rng.choice([1, 1, 2]), kinds=kinds)
    ov.u32(SPOOL_MODE, rng.choice([0, 0, 1, 2, 2, 3]) if mode is None else mode)
    ov.u32(LOADING, rng.choice([0, 0, 0, 1, 5]))
    return placed, envs, entries


@spec(0x004B39B0, "PSX_Spooler", "psxq")
def gen_spooler(env, n):
    ov = Ov()
    rng = env.rng
    placed, envs, entries = spooler_state(env, ov, rng, kinds=(0, 1, 2, 2, 3))
    if rng.random() < 0.1 and entries:
        ov.u8(QUEUE + entries[0][0] * QUEUE_STRIDE + 0x0D, 0)         # the current entry is idle
    # a kind 1 entry wants its slot to be a legal environment slot
    for i, kind, name in entries:
        if kind == 1:
            slot = rng.randrange(20)
            ov.u8(QUEUE + i * QUEUE_STRIDE + 0x0F, slot)
            if rng.random() < 0.6:
                fill_slot(env, ov, rng, slot, state=1, objects=chain_head(env, rng.choice([4, 5])))
    return [], ov.pokes()


@spec(0x004B3DF0, "PSX_DrainSpooler", "psxq")
def gen_drain(env, n):
    ov = Ov()
    rng = env.rng
    placed, envs, entries = spooler_state(env, ov, rng, mode=rng.choice([0, 0, 1, 2]), kinds=(0, 2, 1))
    ov.u32(LOADING, 0)
    return [], ov.pokes()


@spec(0x004B3E20, "PSX_SpoolBusy", "psxq")
def gen_busy(env, n):
    ov = Ov()
    rng = env.rng
    queue_state(env, ov, rng, rng.choice([0, 1, 3]))
    ov.u32(LOADING, rng.choice([0, 0, 1]))
    return [], ov.pokes()


# ---- saving and restoring a region image ------------------------------------------------------------------------------------------------

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES  # noqa: E402
from psx_specs import STATIC_WINDOW  # noqa: E402

SAVE_RAW = 0              # a raw region image (0x7530 bytes)
SAVE_ARENA = 0x8000       # where the allocator hands the copy
SAVE_SAVED = 0x10000      # a copy made earlier
SAVE_BYTES = 0x7530


def seed_save(env, rng):
    env.alloc_base = SAVE_ARENA
    env.alloc_size = 0x8000
    env.psx_seeded = True
    return {STATIC_WINDOW[0]: bytes(STATIC_WINDOW[1]), env.slab + SAVE_RAW: rng.randbytes(SAVE_BYTES), env.slab + SAVE_SAVED: rng.randbytes(SAVE_BYTES)}


SLAB_BYTES["psxsave"] = 0x18000
SEEDS["psxsave"] = seed_save
READ_ONLY["psxsave"] = []
EXTRA_REGIONS["psxsave"] = lambda slab: [(slab + SAVE_RAW, SAVE_BYTES), (slab + SAVE_ARENA, SAVE_BYTES), (slab + SAVE_SAVED, SAVE_BYTES), STATIC_WINDOW]


@spec(0x004B1C20, "PSX_SaveRegionImage", "psxsave")
def gen_save_image(env, n):
    ov = Ov()
    slot = env.rng.randrange(20)
    ov.u32(slot_addr(slot) + 0x18, env.slab + SAVE_RAW)
    return [slot], ov.pokes()


@spec(0x004B1C70, "PSX_RestoreRegionImage", "psxsave")
def gen_restore_image(env, n):
    ov = Ov()
    rng = env.rng
    slot = rng.randrange(20)
    ov.u32(slot_addr(slot) + 0x18, env.slab + SAVE_RAW)
    ov.u32(0x0056D978 + 4 * slot, env.slab + SAVE_SAVED)
    return [slot], ov.pokes()
