"""Phase 4.11a2 case generators, part 2: the skater view's spool state (player_spool.cpp, 0x004a62b0..0x004a6bf0), group `pskv`.

The state block (`this`) is made up in the scratch slab with the manager (the same one pspool_specs lays out), an owner object that holds the part-name lists, 20 model files, the part
strings and the skater table's flag words; the engine services (skater manager, file names, manifest parser, preview object, PSX region calls) are stubbed and reported.

Slab layout (offsets from the scratch slab):
  0x0100  the manager (pspool_specs)   0x1100  the state block (0x800 bytes)   0x1a00  the owner (0x1400 bytes: vtable, name lists)
  0x3000  fake vtables   0x3200  model stems (0x20 apart)   0x3400  part strings (0x10 apart)
  0x4000  the allocation arena   0x6000  20 model files of 0x100 bytes
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Skip, spec
from pspool_specs import ARENA, ARENA_SIZE, GLOBAL, LOADING, MGR, STEPPING, VT_MANAGER, Ov, ensure, manager_state, pick, seed_manager, under_test
import pspool_specs

SPOOL = 0x1100
SPOOL_BYTES = 0x800
OWNER = 0x1A00
OWNER_BYTES = 0x1400
VTABLES = 0x3000
STEMS = 0x3200
PARTSTR = 0x3400
FILES = 0x6000
FILE_BYTES = 0x100
SLAB = 0x8000

SLOT_TABLE = 0x0056D428
SLOT_STRIDE = 0x44
ROWS = 0x0053A3B8
ROW_STRIDE = 0x170
ROW_FACE, ROW_FLAGS = 0x150, 0x15C
PLAYER_COUNT = 0x0056A888
SKATER_MANAGER = 0x0056C580
OWNER_SLOT_4 = 0x004ADA70           # the owner's virtual method 4 (an engine entry point nothing here runs)
PART_WORDS = ["head", "torso", "legs", "arms", "hair", "hat", "board", "shoes", "gloves", "shirt", "pants", "face"]

S_SLOTS, S_PARTS, S_PRIMARY, S_MANIFEST, S_THIRD, S_CURRENT, S_LOADED, S_MADE, S_PREVIEWS, S_COSTUMES, S_OWNER = 0, 0x54, 0x644, 0x648, 0x64C, 0x650, 0x654, 0x6A4, 0x6F4, 0x744, 0x794
O_ANIM_NAMES, O_ANIM_COUNT, O_MODEL_NAMES, O_MODEL_COUNT, O_STRIDE = 0x8A0, 0x920, 0x924, 0x9A4, 0x84

STEM_COUNT = 16
SMALL_SIZES = [0x40, 0x80, 0xC0, 0x100, 0x20]
PART_STRINGS = 48


def regions(slab):
    return [(slab, 0x3A00), (slab + ARENA, 0x2000), (slab + FILES, 20 * FILE_BYTES), (SLOT_TABLE, 20 * SLOT_STRIDE), (ROWS, 20 * ROW_STRIDE), (GLOBAL, 4),
            (LOADING, 0x20), (PLAYER_COUNT, 4)]


def seed_world(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    slab = env.slab
    writes = {}
    for k in range(STEM_COUNT):
        writes[slab + STEMS + 0x20 * k] = (pspool_specs.NAME_POOL[k % len(pspool_specs.NAME_POOL)] + str(k)).encode() + b"\0"
    strings = []
    for k in range(PART_STRINGS):
        text = PART_WORDS[k % len(PART_WORDS)] + ("" if k < len(PART_WORDS) else str(k // len(PART_WORDS)))
        strings.append(text)
        writes[slab + PARTSTR + 0x10 * k] = text.encode() + b"\0"
    for k in range(20):
        writes[slab + FILES + FILE_BYTES * k] = rng.randbytes(FILE_BYTES)
    writes[slab + VTABLES + 0x40] = u32(OWNER_SLOT_4) * 6
    seed_manager(env, writes)
    # the owner: a vtable (method 4 is an engine entry point) and the part-name lists (a case changes the counts and the pointers it needs)
    owner = bytearray(OWNER_BYTES)
    struct.pack_into("<I", owner, 0, slab + VTABLES + 0x40)
    for k in range(32):
        struct.pack_into("<I", owner, O_ANIM_NAMES + 4 * k, part_ptr(env, rng.randrange(PART_STRINGS)))
    struct.pack_into("<I", owner, O_ANIM_COUNT, 12)
    for i in range(20):
        for k in range(32):
            struct.pack_into("<I", owner, O_MODEL_NAMES + O_STRIDE * i + 4 * k,
                             part_ptr(env, rng.randrange(PART_STRINGS) if rng.random() < 0.6 else rng.randrange(len(PART_WORDS))))
        struct.pack_into("<I", owner, O_MODEL_COUNT + O_STRIDE * i, 12)
    writes[slab + OWNER] = bytes(owner)
    return writes


for _g in ("pskv",):
    SLAB_BYTES[_g] = SLAB
    SEEDS[_g] = seed_world
    EXTRA_REGIONS[_g] = regions
    READ_ONLY[_g] = []


def part_ptr(env, k):
    return env.slab + PARTSTR + 0x10 * (k % PART_STRINGS)


def stem_ptr(env, k):
    return env.slab + STEMS + 0x20 * (k % STEM_COUNT)


def view_state(env, ov, rng, loaded=None, slots=None, current=None, customs=None, with_manager=True, kinds=(0, 0, 1), manager_count=None, manager_mode=None,
               lists=False):
    """A random state block, skater table flags and PSX slot raws (and, with `lists`, part indices and the owner's part-name counts); returns the manager's entries."""
    slab = env.slab
    env.block_sizes = SMALL_SIZES
    # which skaters have a model in memory, a preview object, a costume
    for i in range(20):
        has_file = rng.random() < 0.4 if slots is None else (i in slots)
        if has_file:
            ov.u32(slab + SPOOL + S_SLOTS + 4 * i, slab + FILES + FILE_BYTES * i)
        if (rng.choice([0, 0, 1]) if loaded is None else (1 if i in loaded else 0)):
            ov.u32(slab + SPOOL + S_LOADED + 4 * i, 1)
        if rng.random() < 0.4:
            ov.u32(slab + SPOOL + S_MADE + 4 * i, 1)
            ov.u32(slab + SPOOL + S_PREVIEWS + 4 * i, slab + ARENA + 0x1000 + 0x10 * i)
        ov.u32(slab + SPOOL + S_COSTUMES + 4 * i, rng.randrange(0, 5))
    ov.u32(slab + SPOOL + S_CURRENT, rng.randrange(20) if current is None else current)
    ov.u32(slab + SPOOL + S_OWNER, slab + OWNER)
    for off in (S_PRIMARY, S_MANIFEST, S_THIRD):
        ov.u32(slab + SPOOL + off, 0xFFFFFFFF)
    if lists:
        for i in range(20):
            for j in range(19):
                if rng.random() < 0.5:
                    ov.u32(slab + SPOOL + S_PARTS + 4 * i + 0x50 * j, rng.choice([0, rng.randrange(0, 12), rng.getrandbits(32)]))
        ov.u32(slab + OWNER + O_ANIM_COUNT, rng.choice([0, 1, 3, 6, 12, 20]))
        for i in range(20):
            ov.u32(slab + OWNER + O_MODEL_COUNT + O_STRIDE * i, rng.choice([0, 2, 5, 12, 19, 25]))
    # the skater table: flag dwords (bit 1: a custom skater) and face pointers
    for i in range(20):
        custom = (rng.random() < 0.15) if customs is None else (i in customs)
        ov.u32(ROWS + ROW_STRIDE * i + ROW_FLAGS, (rng.getrandbits(8) & ~2) | (2 if custom else 0))
        ov.u32(ROWS + ROW_STRIDE * i + ROW_FACE, rng.getrandbits(32) | 0x400000)
    # the PSX region slots' raw pointers
    for r in range(20):
        if rng.random() < 0.35:
            ov.u32(SLOT_TABLE + SLOT_STRIDE * r + 0x18, slab + ARENA + 0x1800 + 0x20 * r)
    ov.u32(PLAYER_COUNT, rng.choice([0, 1, 2, 3, 4]))
    entries = []
    if with_manager:
        entries = manager_state(env, ov, rng, count=manager_count, mode=manager_mode, kinds=kinds)
    return entries


@spec(0x004A62B0, "SkaterSpool_SetSlotBuffer", "pskv", this=lambda env: env.slab + SPOOL)
def gen_set_slot_buffer(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False)
    slot = rng.choice([0, 19, rng.randrange(20), rng.randrange(20), rng.randrange(20), 20, 0xFFFFFFFF])
    return [slot, rng.choice([0, env.slab + ARENA + 0x300, env.slab + ARENA + 0x700])], ov.pokes()


@spec(0x004A6340, "SkaterSpool_Preview", "pskv", this=lambda env: env.slab + SPOOL)
def gen_preview(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False)
    return [rng.choice([0, 19, rng.randrange(20), rng.randrange(20), 20, 0xFFFFFFFF])], ov.pokes()


@spec(0x004A63B0, "SkaterSpool_SlotUsable", "pskv", this=lambda env: env.slab + SPOOL)
def gen_slot_usable(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False)
    return [rng.choice([0, 19, rng.randrange(20), rng.randrange(20), rng.randrange(20)])], ov.pokes()


@spec(0x004A6440, "SkaterSpool_InstallModel", "pskv", this=lambda env: env.slab + SPOOL)
def gen_install_model(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False)
    slot = rng.randrange(20)
    roll = rng.random()
    if roll < 0.9:
        ov.u32(env.slab + SPOOL + S_SLOTS + 4 * slot, env.slab + FILES + FILE_BYTES * slot)       # (a null file would make the copy fault in retail too)
    else:
        ov.u32(env.slab + SPOOL + S_SLOTS + 4 * slot, 0)
        ov.u32(ROWS + ROW_STRIDE * slot + ROW_FLAGS, 0)       # not usable: nothing happens
    return [rng.randrange(20), slot], ov.pokes()


@spec(0x004A6560, "SkaterSpool_ResolvePartIndices", "pskv", this=lambda env: env.slab + SPOOL)
def gen_resolve(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False, lists=True)
    skater = rng.randrange(20)
    names = env.slab + OWNER + O_ANIM_NAMES
    parts = env.slab + OWNER + O_MODEL_NAMES + O_STRIDE * skater
    name_count = rng.choice([0, 1, 4, 8, 12, 20, 32])
    part_count = rng.choice([0, 1, 5, 12, 19, 25, 32])
    if rng.random() < 0.05:
        names, name_count = 0, 0
    # make most parts findable
    for k in range(part_count):
        if rng.random() < 0.7 and name_count:
            ov.u32(parts + 4 * k, rng.choice([part_ptr(env, rng.randrange(PART_STRINGS))]))
            ov.u32(names + 4 * rng.randrange(name_count), part_ptr(env, rng.randrange(PART_STRINGS)))
    return [skater, names, name_count, parts, part_count], ov.pokes()


@spec(0x004A6720, "SkaterSpool_ApplyPartIndices", "pskv", this=lambda env: env.slab + SPOOL)
def gen_apply(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, with_manager=False, lists=True)
    return [rng.choice([0, env.slab + OWNER, env.slab + OWNER + 0x40, env.slab + ARENA + 0x1000]), rng.randrange(20)], ov.pokes()


@spec(0x004A67A0, "SkaterSpool_LoadedCount", "pskv", this=lambda env: env.slab + SPOOL)
def gen_loaded_count(env, n):
    ov = Ov()
    view_state(env, ov, env.rng, with_manager=False)
    return [], ov.pokes()


@spec(0x004A67C0, "SkaterSpool_Begin", "pskv", this=lambda env: env.slab + SPOOL)
def gen_begin(env, n):
    ov = Ov()
    rng = env.rng
    view_state(env, ov, rng, manager_mode=rng.choice([0, 1, 2]))
    return [], ov.pokes()


@spec(0x004A6880, "SkaterSpool_Update", "pskv", this=lambda env: env.slab + SPOOL)
def gen_update(env, n):
    ov = Ov()
    rng = env.rng
    entries = view_state(env, ov, rng, manager_count=rng.choice([0, 1, 2, 3]), manager_mode=rng.choice([0, 1, 1, 2]))
    point_at_entries(env, ov, rng, entries)
    return [rng.randrange(20)], ov.pokes()


def point_at_entries(env, ov, rng, entries):
    """Make the state block's queue indices name entries of the manager (or nothing)."""
    base = env.slab + SPOOL
    ones = [e[0] for e in entries if e[2] == 0]
    for off in (S_PRIMARY, S_MANIFEST):
        roll = rng.random()
        if roll < 0.4 and ones:
            ov.u32(base + off, rng.choice(ones))
        elif roll < 0.5:
            ov.u32(base + off, rng.randrange(8))
        else:
            ov.u32(base + off, 0xFFFFFFFF)
    if rng.random() < 0.1:
        ov.u32(base + S_THIRD, rng.randrange(8))


@spec(0x004A68F0, "SkaterSpool_FinishLoads", "pskv", this=lambda env: env.slab + SPOOL)
def gen_finish_loads(env, n):
    ov = Ov()
    rng = env.rng
    entries = view_state(env, ov, rng, manager_count=rng.choice([2, 3, 5]), manager_mode=rng.choice([0, 1, 2]), kinds=(0, 0, 0, 1), lists=True)
    env.block_sizes = SMALL_SIZES + [0x10800, 0x10801, 0x20000]
    point_at_entries(env, ov, rng, entries)
    # entries the indices name are usually finished files with a block
    base = env.slab + SPOOL
    for off in (S_PRIMARY, S_MANIFEST):
        index = None
        # (read back what we poked: the last write wins)
        index = ov.bytes.get(base + off)
        if index is not None and index != 0xFF and rng.random() < 0.8:
            e = env.slab + MGR + 4 + index * 0x28
            ov.u8(e, 1)
            ov.u32(e + 0x14, 0)
            ov.u32(e + 0x1C, env.slab + ARENA + 0x2000 + 0x100 * index)
    return [], ov.pokes()


@spec(0x004A6BF0, "PlayerSpool_LoadResources", "pskv", this=lambda env: env.slab + SPOOL)
def gen_load_resources(env, n):
    ov = Ov()
    rng = env.rng
    roll = rng.random()
    view_state(env, ov, rng, manager_count=rng.choice([0, 0, 1, 2, 3, 8]), manager_mode=rng.choice([0, 1, 2]),
               loaded=set(rng.sample(range(20), rng.choice([0, 1, 3, 5, 8, 14, 20]))) if roll < 0.7 else None,
               slots=set(rng.sample(range(20), rng.choice([0, 2, 5, 7, 9, 14]))) if roll < 0.6 else None)
    return [rng.choice([0, 19, rng.randrange(20), rng.randrange(20), rng.randrange(20), rng.randrange(20)])], ov.pokes()


# ---- the services -----------------------------------------------------------------------------------------------------------------------

def install_stubs(fc, env):
    if getattr(fc, "pskv_stubs", False):
        return
    fc.pskv_stubs = True
    pspool_specs.install_stubs(fc, env)          # (the manager's services: files, PSX queue, allocator)
    under_test()
    rng = lambda: env.rng      # noqa: E731
    ensure(fc, 0x0046F510, 1, result=lambda a: rng().choice(getattr(env, "block_sizes", SMALL_SIZES)))
    ensure(fc, 0x004A5080, 1)
    ensure(fc, 0x004B2120, 1)
    ensure(fc, 0x004B2390, 0)
    ensure(fc, 0x0048A420, 0)
    ensure(fc, 0x00489490, 0)
    ensure(fc, 0x004B1700, 2, result=lambda a: stem_ptr(env, rng().randrange(STEM_COUNT)))
    ensure(fc, 0x004B4940, 6)
    ensure(fc, 0x00457420, 4, convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    ensure(fc, 0x00480CD0, 2, convention="thiscall", this=True)
    ensure(fc, 0x004B0C30, 0, convention="thiscall", this=True, result=lambda a: rng().randrange(20))
    ensure(fc, 0x004B0C40, 0, convention="thiscall", this=True, result=lambda a: rng().randrange(0, 5))
    ensure(fc, 0x004B0C60, 1, convention="thiscall", this=True, result=lambda a: rng().randrange(20))
    ensure(fc, 0x004B0CD0, 1, convention="thiscall", this=True, result=lambda a: rng().randrange(0, 5))
    ensure(fc, 0x004A6EA0, 0, convention="thiscall", this=True)
    ensure(fc, OWNER_SLOT_4, 0, convention="thiscall", this=True)


GROUP_STUBS = {"pskv": install_stubs}
