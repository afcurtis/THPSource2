"""Phase 4.11a case generators, part 3: the named-resource list (BITS), animation tables, model files, material selection, texture references.

Groups `psxbits`, `psxanim`, `psxtex` (spool.cpp 0x004b2b70, 0x004b2f70, 0x004b34f0.., 0x004b3e50.., 0x004b4ad0..).  The world is the one psx_specs.seed_world lays
out: six region images (+0x500 texture records, +0x600 models, +0x7a0 colour table, +0x5c parse block), two animation files (+0x300 animation table).
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import Skip, spec
from psx_specs import (ANIM_TABLE, ANIMS, BITS_LIST, ITER_BUCKET, MAT_COUNT, MAT_STRIDE, MATS, NAME_POOL, NAMES, NODES, OBJ_STRIDE, OBJS, Ov, PART_TABLES, REG_COUNT, REG_SIZE,
                       REGS, SCRATCH, SCRATCH_NAME, SKATER, SLOT_BASE, STRIDE, material_states, pick, slot_addr, world)
from psx_specs_regions import clean_table, fill_slot, fill_table, usage_counts

SELECTION_RECORDS = 0x0053A514
GAME_MODE = 0x00533F38
CUSTOM_INDEX = 0x0056A854
PLAYER = 0x0056A858
PLAYER_TWO = 0x0056A85C
PART_POINTERS = 0x00568648
REPLAY_SKATERS = 0x0056B8D4
PALETTE_BASE = 0x0056DB3C
PALETTE_FLAG = 0x0056DB40
LIST_AT = SCRATCH + 0x200       # the animation number lists
CHECKSUMS_AT = SCRATCH + 0x100  # a checksum list and its count (+0xf0)
COUNT_AT = SCRATCH + 0xF0


def put_name(env, ov, name, offset=0):
    ov.put(env.slab + SCRATCH_NAME + offset, name.encode() + b"\0")
    return env.slab + SCRATCH_NAME + offset


# ---- the named-resource list --------------------------------------------------------------------------------------------------------

@spec(0x004B34F0, "BITS_ParseNamedResourceTag", "psxbits")
def gen_parse_tag(env, n):
    ov = Ov()
    rng = env.rng
    image = world.images[5]       # (any other image makes the walk of the trailing tables read checksums as pointers: retail faults)
    if rng.random() < 0.5:
        ov.u32(BITS_LIST, env.slab + SCRATCH + 0x400)
    # the tag: one of the blobs, whose frames name material pointers by index
    tag = pick(rng, world.bits_tags)
    return [image["address"], tag], ov.pokes()


@spec(0x004B3680, "BITS_FindNamedResource", "psxbits")
def gen_find_named(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(BITS_LIST, rng.choice([env.slab + SCRATCH + 0x400, env.slab + SCRATCH + 0x408, 0]))
    roll = rng.random()
    if roll < 0.55:
        name = rng.choice(world.bits_names)
        if rng.random() < 0.4:
            name = name.upper()
        if rng.random() < 0.15:
            name = name + "ZZ"
        if rng.random() < 0.15:
            name = name[:-1] or name
    elif roll < 0.62:
        name = "punchittX"
    elif roll < 0.7:
        name = "punchitt" + chr(rng.choice([1, 2]))       # (the ninth character is compared with the entry's frame count byte: eight equal ones already match)
    else:
        name = rng.choice(["nothing", "", "wal", "grindgrindgrind", "KICK"])
    return [put_name(env, ov, name)], ov.pokes()


# ---- animation tables --------------------------------------------------------------------------------------------------------------

def anim_slot(env, ov, rng, index, anim, state=1, name=None, keep=0):
    """Slot `index` holds the animation file `anim` (its tables as the parser publishes them)."""
    base = slot_addr(index)
    info = world.anims[anim]
    ov.put(base, bytes(STRIDE))
    ov.put(base, (name or rng.choice(NAME_POOL)).encode()[:9] + b"\0")
    ov.u8(base + 0x0A, state)
    ov.u8(base + 0x0B, keep)
    ov.u32(base + 0x18, info["address"])
    ov.u32(base + 0x1C, info["address"] + ANIM_TABLE)
    ov.u32(base + 0x20, info["address"] + info["hierarchy"])
    ov.u8(base + 0x3D, 1)
    return info


def number_lists(env, ov, rng, animations, bad=False):
    """The default and extra animation number lists (int16, each ended by -1) at LIST_AT."""
    values = []
    first = sorted(rng.sample(range(animations + 2), rng.randrange(0, animations + 1)))
    second = sorted(rng.sample(range(animations + 2), rng.randrange(0, animations + 1)))
    blob = b"".join(struct.pack("<h", v) for v in first) + struct.pack("<h", -1) + b"".join(struct.pack("<h", v) for v in second) + struct.pack("<h", -1)
    if bad and first:
        blob = struct.pack("<h", 0x300) + blob[2:]
    ov.put(env.slab + LIST_AT, blob)
    return env.slab + LIST_AT


@spec(0x004B40A0, "PSX_BuildAnimationTables", "psxanim")
def gen_build_tables(env, n):
    ov = Ov()
    rng = env.rng
    material_states(env, ov, rng)
    usage_counts(env, ov, rng, zero_chance=0.05)
    clean_table(ov)
    region = rng.randrange(20)
    anim = rng.randrange(len(world.anims))
    info = anim_slot(env, ov, rng, region, anim, state=rng.choice([1, 1, 1, 2]))
    lists = number_lists(env, ov, rng, info["animations"], bad=rng.random() < 0.05)
    if rng.random() < 0.05:
        lists = 0
    return [region, lists], ov.pokes()


def file_image_case(env, ov, rng, image_address):
    env.file_image = image_address


@spec(0x004B46A0, "PSX_LoadAnimationResource", "psxanim")
def gen_load_animation(env, n):
    ov = Ov()
    rng = env.rng
    material_states(env, ov, rng)
    usage_counts(env, ov, rng, zero_chance=0.05)
    clean_table(ov)
    anim = rng.randrange(len(world.anims))
    env.file_image = world.anims[anim]["address"]
    existing = rng.random() < 0.25
    name = rng.choice(["skwalk", "skjump", "anim1", "tony", "x"])
    if existing:
        anim_slot(env, ov, rng, rng.randrange(20), rng.randrange(len(world.anims)), state=rng.choice([1, 2]), name=name)
    else:
        # some slots are in use: a free one is found among them
        for i in range(rng.randrange(0, 15)):
            fill_slot(env, ov, rng, i, name=rng.choice(NAME_POOL))
    ov.u16(REPLAY_SKATERS, rng.choice([0, 0, 1, 2, 0xFFFE]))
    return [put_name(env, ov, name), rng.choice([0, 1, 1, 2])], ov.pokes()


@spec(0x004B3E50, "Spool_StripAnim", "psxanim")
def gen_strip(env, n):
    ov = Ov()
    rng = env.rng
    clean_table(ov)
    region = rng.randrange(20)
    image = rng.randrange(5)
    info = world.images[image]
    base = info["address"]
    slot = slot_addr(region)
    ov.put(slot, rng.choice(NAME_POOL).encode() + b"\0")
    ov.u8(slot + 0x0A, 1)
    ov.u32(slot + 0x0C, base + info["tables"] if rng.random() > 0.08 else base + info["tables"] + 4)
    ov.u32(slot + 0x14, info["table"] + 4)
    ov.u32(slot + 0x18, base)
    ov.u32(slot + 0x20, base + 0x7E0 if rng.random() > 0.08 else base + 0x20)
    ov.u32(slot + 0x28, base + 0x7A8 if rng.random() > 0.2 else 0)
    ov.u16(slot + 0x38, info["models"] if rng.random() > 0.1 else rng.randrange(0, 4))
    ov.u8(slot + 0x3C, 0 if rng.random() > 0.1 else 1)
    if rng.random() < 0.08:
        ov.u32(base + 0x7A4, 0)
    return [region], ov.pokes()


@spec(0x004B43E0, "Skater_LoadModelFile", "psxanim")
def gen_load_model_file(env, n):
    ov = Ov()
    rng = env.rng
    material_states(env, ov, rng)
    usage_counts(env, ov, rng, zero_chance=0.05)
    clean_table(ov)
    image = rng.randrange(5)
    info = world.images[image]
    env.file_image = info["address"] if rng.random() > 0.05 else 0
    existing = rng.random() < 0.2
    name = rng.choice(["tony", "hawk", "burnq", "muska"])
    for i in range(rng.randrange(0, 12)):
        fill_slot(env, ov, rng, i, name=rng.choice(NAME_POOL))
    if existing:
        fill_slot(env, ov, rng, rng.randrange(20), name=name)
    ov.u8(SELECTION_RECORDS, rng.choice([0, 2]))
    ov.u32(info["address"] + 0x74, 0 if rng.random() > 0.1 else 5)        # hooks
    ov.u32(info["address"] + 0x78, 0 if rng.random() > 0.1 else 5)        # wib
    ov.u32(info["address"] + 0x7C, 0 if rng.random() > 0.1 else 5)        # pulse
    arg = put_name(env, ov, name) if rng.random() > 0.03 else 0
    return [arg, rng.randrange(0, 4)], ov.pokes()


@spec(0x004B47A0, "PlayerSpool_LoadPartManifest", "psxanim")
def gen_part_manifest(env, n):
    ov = Ov()
    rng = env.rng
    name = rng.choice(["tony", "hawk", "burnquist", "", "mmm"]) if rng.random() < 0.9 else "abcd"
    env.file_image = rng.choice([0, env.slab + REGS])
    base = env.slab + SCRATCH + 0x600
    a, b, c, d = rng.choice([base, 0]) if rng.random() < 0.1 else base, base + 0x40, rng.choice([base + 0x80, 0]) if rng.random() < 0.1 else base + 0x80, base + 0xC0
    return [put_name(env, ov, name), a, b, c, d], ov.pokes()


@spec(0x004B5010, "PSX_RelocateDefinitionPointers", "psxanim")
def gen_relocate(env, n):
    rng = env.rng
    return [world.images[rng.randrange(5)]["address"], rng.choice([0, 1, 5, 11, rng.getrandbits(8)])], {}


@spec(0x004B50A0, "PlayerSpool_ModelSlot", "psxanim")
def gen_model_slot(env, n):
    ov = Ov()
    rng = env.rng
    skater = rng.randrange(4)
    ov.u8(SELECTION_RECORDS + 0x170 * skater, rng.choice([0, 2, 3, 1]))
    return [rng.randrange(0, 20), skater], ov.pokes()


# ---- material selection -------------------------------------------------------------------------------------------------------------

def model_slot(env, ov, rng, region, image):
    info = world.images[image]
    base = slot_addr(region)
    ov.put(base, bytes(STRIDE))
    ov.put(base, rng.choice(NAME_POOL).encode() + b"\0")
    ov.u8(base + 0x0A, 1)
    ov.u32(base + 0x14, info["table"] + 4)
    ov.u32(base + 0x18, info["address"])
    return info


@spec(0x004B4AD0, "PSX_CollectModelMaterials", "psxanim")
def gen_collect(env, n):
    ov = Ov()
    rng = env.rng
    clean_table(ov)
    region = rng.randrange(20)
    image = rng.randrange(5)
    info = model_slot(env, ov, rng, region, image)
    model = info["model_pointers"][rng.randrange(3)]
    have = rng.choice([0, 0, 1, 2, 5])
    ov.u32(env.slab + COUNT_AT, have)
    for k in range(have):
        ov.u32(env.slab + CHECKSUMS_AT + 4 * k, rng.choice([world.checksums[r] for r in info["refs"]]) if rng.random() < 0.5 else rng.getrandbits(32))
    # most calls are made on an empty region slot's address, like the retail callers
    return [slot_addr(region), model if rng.random() > 0.03 else 0, env.slab + CHECKSUMS_AT if rng.random() > 0.03 else 0, env.slab + COUNT_AT if rng.random() > 0.03 else 0], ov.pokes()


@spec(0x004B4E00, "PSX_ScoreMaterialPaletteUsage", "psxanim")
def gen_score(env, n):
    rng = env.rng
    checksum = pick(rng, world.checksums) if rng.random() > 0.2 else rng.getrandbits(32)
    return [checksum], {}


@spec(0x004B4E70, "PSX_SelectMaterialForModel", "psxanim")
def gen_select(env, n):
    ov = Ov()
    rng = env.rng
    clean_table(ov)
    skater = env.slab + SKATER
    region = rng.randrange(20)
    image = rng.randrange(5)
    info = model_slot(env, ov, rng, region, image)
    index = rng.randrange(4)
    ov.u32(skater + 0x2CC0, index)
    ov.u8(skater + 0x1F, region)
    for k in range(4):
        ov.u8(SELECTION_RECORDS + 0x170 * k, rng.choice([0, 0, 2, 3]))
    custom = (rng.getrandbits(8) & 2) != 0
    ov.u8(SELECTION_RECORDS + 0x170 * index, 2 if custom else 0)
    ov.u32(GAME_MODE, rng.choice([7, 0, 3]))
    ov.u32(CUSTOM_INDEX, rng.randrange(2))
    for k in range(3):
        ov.u32(env.slab + PART_TABLES + 0x20 * k + 0x6C, rng.getrandbits(32))
        ov.u32(PART_POINTERS + 4 * k, env.slab + PART_TABLES + 0x20 * k)
    ov.u32(PLAYER, rng.choice([skater, 0, 1]))
    ov.u32(PLAYER_TWO, rng.choice([skater, 0, 2]))
    env.model_roll = rng.choice([rng.randrange(info["models"] or 1), rng.randrange(info["models"] or 1), info["models"]]) & 0xFFFFFFFF
    material_states(env, ov, rng)
    return [skater], ov.pokes()


# ---- texture references ---------------------------------------------------------------------------------------------------------------

def texture_slot(env, ov, rng, region, image, shrunk=1):
    info = world.images[image]
    base = slot_addr(region)
    ov.put(base, bytes(STRIDE))
    ov.put(base, rng.choice(NAME_POOL).encode() + b"\0")
    ov.u8(base + 0x0A, 1)
    ov.u32(base + 0x14, info["table"] + 4)
    ov.u32(base + 0x18, info["address"])
    ov.u8(base + 0x3B, 1 if rng.random() > 0.08 else 0)
    ov.u8(base + 0x3C, shrunk if rng.random() > 0.08 else 1 - shrunk)
    return info


def records_state(env, ov, rng, info):
    for k in range(info["records"]):
        rec = info["address"] + 0x500 + 0x20 * k
        ov.u32(rec, rng.getrandbits(8))
        ov.u32(rec + 4, rng.choice([0x10, 0x100, 0x100, 0x10, 0x20]))
        ov.u32(rec + 8, rng.getrandbits(16))
        ov.u32(rec + 0xC, rng.randrange(len(info["refs"])))
        ov.u16(rec + 0x10, rng.randrange(1, 300))
        ov.u16(rec + 0x12, rng.randrange(1, 300))


@spec(0x004B2B70, "PSX_LoadTextureReferences", "psxtex")
def gen_load_textures(env, n):
    ov = Ov()
    rng = env.rng
    clean_table(ov)
    region = rng.randrange(20)
    image = rng.randrange(5)
    info = texture_slot(env, ov, rng, region, image)
    records_state(env, ov, rng, info)
    for r in info["refs"]:
        a = env.slab + MATS + r * MAT_STRIDE
        ov.u8(a + 0x12, rng.choice([0, 0, 1]))
        ov.u8(a + 0x13, rng.choice([0, 1]))
        ov.u16(a + 0x10, rng.choice([0, 1, 2]))
    # the two materials that are always tiled
    for r in (22, 23):
        a = env.slab + MATS + r * MAT_STRIDE
        ov.u8(a + 0x12, rng.choice([0, 0, 1]))
    if rng.random() < 0.12:
        ov.u32(info["address"] + info["tables"] + 4 * info["models"] + 4 + 4 * rng.randrange(len(info["refs"])), 0)       # a texture whose material was never made
    ov.u32(PALETTE_BASE, rng.getrandbits(20))
    ov.u8(PALETTE_FLAG, rng.choice([0, 1]))
    for i in range(8):
        ov.u16(env.slab + NODES + 0x10 * i, rng.getrandbits(16))
        ov.u16(env.slab + NODES + 0x10 * i + 4, rng.choice([0, 1, 2, 5]))
    return [region, rng.getrandbits(32) if rng.random() < 0.7 else 0], ov.pokes()


@spec(0x004B2F70, "PSX_ClearTextureReferences", "psxtex")
def gen_clear_textures(env, n):
    ov = Ov()
    rng = env.rng
    clean_table(ov)
    region = rng.randrange(20)
    image = rng.randrange(5)
    info = texture_slot(env, ov, rng, region, image, shrunk=1)
    for r in info["refs"]:
        a = env.slab + MATS + r * MAT_STRIDE
        ov.u8(a + 0x12, rng.choice([0, 1, 1]))
        ov.u16(a + 0x20, rng.getrandbits(16))
        ov.u16(a + 0x22, rng.getrandbits(16))
    records_state(env, ov, rng, info)
    return [region], ov.pokes()
