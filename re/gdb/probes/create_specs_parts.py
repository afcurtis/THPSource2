"""Phase 4.9 case generators: the custom skater's part database (create.cpp).

The group takes an unused record of the game's region table (0x0056d428 + slot * 0x44) for its
fake region, and lays these out in the scratch slab (offsets from the slab):
  0x0000  the part database (0x1c80 bytes): 40 seeded parts, group names and counts, the region index
  0x2000  the region's model table: 0x7a pointers to fake models
  0x2200  fake models (0x40 bytes each; only the first byte, whose 0x20 bit marks a hidden part, is read)
  0x3000  a fake editor object (0x290 bytes: slot animations +0x1c, region index +0x84, palette buffer +0x88,
          palette handles +0x8c, the part database pointer +0x280)
  0x3400  the part map the builder fills (0x78 * 8 bytes)
  0x3800  the slot counter cell, 0x3900 scratch names
"""

from __future__ import annotations

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, Skip, s32, spec

PARTDB = 0x0000
PARTDB_SIZE = 0x1C80
MODELS = 0x2000
FAKE_MODELS = 0x2200
EDITOR = 0x3000
EDITOR_SIZE = 0x290
MAP = 0x3400
COUNTER = 0x3800
NAMES = 0x3900
REGION_TABLE = 0x0056D428
REGION_STRIDE = 0x44
PART_NAMES = ["head", "stomach", "chest", "right_bicep", "right_forearm", "right_hand", "left_bicep", "left_forearm",
              "left_hand", "pelvis", "left_thigh", "left_shin", "left_shoe", "right_thigh", "right_shin", "right_shoe",
              "board", "front_wheel", "back_wheel"]
SEEDED_PARTS = 40

state = {"region": None}


def free_region():
    from opentony.memory import mem
    for slot in range(15, 1, -1):
        base = REGION_TABLE + slot * REGION_STRIDE
        if mem.u32(base) == 0 and mem.u32(base + 0x14) == 0 and (mem.u32(base + 0x38) & 0xFFFF) == 0:
            return slot
    raise RuntimeError("no unused region record")


def parts_regions(slab):
    if state["region"] is None:
        state["region"] = free_region()
    record = REGION_TABLE + state["region"] * REGION_STRIDE
    return [(slab + PARTDB, PARTDB_SIZE), (slab + EDITOR, EDITOR_SIZE), (slab + MAP, 0x3C0), (slab + COUNTER, 4),
            (slab + NAMES, 0x100), (record, REGION_STRIDE)]


def seed_parts(env, rng):
    slab = env.slab
    region = state["region"]
    writes = {}
    db = slab + PARTDB
    for i in range(SEEDED_PARTS):
        name = PART_NAMES[i] if i < len(PART_NAMES) else rng.choice(PART_NAMES + [f"extra{i}"])
        if i >= len(PART_NAMES) and rng.random() < 0.5:
            name = f"extra{i}"
        entry = db + 4 + i * 0x34
        writes[entry] = name.encode() + b"\0" + b"\0" * (0x10 - len(name) - 1)
        writes[entry + 0x10] = u32(rng.randrange(0, 16)) + u32(rng.randrange(-1, 20) & 0xFFFFFFFF) + u32(rng.randrange(0, 4))
        writes[entry + 0x1C] = b"".join(u32(rng.getrandbits(32)) for _ in range(5)) + u32(rng.randrange(0, 6))
    writes[db + 0x1864] = u32(19)
    for g in range(4):
        count = rng.randrange(1, 17)
        writes[db + 0x1C68 + 4 * g] = u32(count)
        for i in range(16):
            label = f"g{g}_{rng.randrange(6)}" if i < count else ""
            writes[db + 0x1868 + i * 0x40 + g * 0x10] = label.encode() + b"\0" * (0x10 - len(label))
    writes[db + 0x1C78] = u32(region)
    # the model table and the fake models
    models = b""
    for i in range(0x7A):
        models += u32(slab + FAKE_MODELS + i * 0x40)
        writes[slab + FAKE_MODELS + i * 0x40] = u8(rng.choice([0, 0x20, 0x01, 0x21, 0x20]))
    writes[slab + MODELS] = models
    # the region record: model table pointer and a part count
    writes[REGION_TABLE + region * REGION_STRIDE + 0x14] = u32(slab + MODELS)
    writes[REGION_TABLE + region * REGION_STRIDE + 0x38] = u16(19)
    writes[slab + EDITOR + 0x84] = u32(region)
    writes[slab + EDITOR + 0x280] = u32(slab + PARTDB)
    return writes


SLAB_BYTES["parts"] = 0x4000
SEEDS["parts"] = seed_parts
EXTRA_REGIONS["parts"] = parts_regions
READ_ONLY["parts"] = [(0x00525DB0, 0x60), (0x00525F08, 0xD8)]    # the name pointer table and the name literals


def editor(env):
    return env.slab + EDITOR


def region_record(env):
    return REGION_TABLE + state["region"] * REGION_STRIDE


@spec(0x0041F3D0, "Part_GroupIndex", "parts")
def gen_group_index(env, n):
    p = Pokes()
    rng = env.rng
    name = rng.choice(PART_NAMES + PART_NAMES + ["unknown_part", "Head", "pelv"])
    p.put(env.slab + NAMES, name.encode() + b"\0")
    return [env.slab + NAMES], p.pokes


@spec(0x0041FE50, "PartDb_FindBodyGroup", "parts", this=lambda env: env.slab + PARTDB)
def gen_find_body_group(env, n):
    p = Pokes()
    rng = env.rng
    group = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, -1, 4])
    name = rng.choice([f"g{rng.randrange(4)}_{rng.randrange(6)}"] * 3 + ["nothing", ""])
    p.put(env.slab + NAMES, name.encode() + b"\0")
    return [env.slab + NAMES, group], p.pokes


def editor_state(env, p, rng, region_ok=True):
    p.put(editor(env) + 0x84, u32(state["region"] if region_ok else 0xFFFFFFFF))
    p.put(editor(env) + 0x1C, bytes(rng.randrange(0, 256) for _ in range(0x4C)))
    p.put(editor(env) + 0x88, u32(rng.choice([0, env.slab + NAMES + 0x80])))
    p.put(editor(env) + 0x8C, bytes(rng.randrange(0, 256) for _ in range(36)))


@spec(0x00421040, "Part_Classify", "parts", this=editor)
def gen_classify(env, n):
    p = Pokes()
    rng = env.rng
    part = rng.choice([0, 1, 2, 3, 4, 9, 10, 15, rng.randrange(0, SEEDED_PARTS)])
    p.put(env.slab + COUNTER, u32(rng.choice([4, 4, 5, 10, 0x12, 0x13, 0x14])))
    return [part, env.slab + COUNTER], p.pokes


@spec(0x00421250, "Part_BuildMap", "parts", this=editor)
def gen_build_map(env, n):
    p = Pokes()
    rng = env.rng
    editor_state(env, p, rng)
    # how many parts the region has, and which of the models are hidden
    parts = rng.choice([0, 1, 5, 19, 19, 19, 30, 40])
    p.put(region_record(env) + 0x38, u16(parts))
    p.put(env.slab + MAP, bytes(rng.randrange(0, 256) for _ in range(0x40)))
    return [env.slab + MAP], p.pokes


@spec(0x004203C0, "Part_CheckedRegion", "parts", this=editor)
def gen_checked_region(env, n):
    p = Pokes()
    rng = env.rng
    editor_state(env, p, rng, region_ok=rng.random() < 0.7)
    return [], p.pokes


@spec(0x004214D0, "Part_RegionRecord", "parts", this=editor)
def gen_region_record(env, n):
    p = Pokes()
    p.put(editor(env) + 0x84, u32(env.rng.randrange(0, 40)))
    return [], p.pokes


@spec(0x00420860, "Part_FreePalettes", "parts", this=editor)
def gen_free_palettes(env, n):
    p = Pokes()
    editor_state(env, p, env.rng)
    return [], p.pokes
