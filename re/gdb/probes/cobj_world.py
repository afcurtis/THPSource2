"""Phase 4.15f probe: the scene the create-a-skater object groups share (cobj_specs3 and later): a part database, a source region with 19 models and their material tables, scene materials,
texture sets, fake skater objects and a build region. The scene is written once (seed) into the slab and into two free records of the region table; the cases vary small things.

Slab layout (offsets from the slab; the groups take the windows they touch):
  0x00100  the part database (0x2090 bytes)
  0x02200  skater objects (0x400 apart, three of them)
  0x02e00  the source model table (count word, then 19 pointers), 0x02f00 the build model table
  0x03000  the trailing tables the stub PSX_FindTrailingTables answers (count and pointers of the region's materials), 0x03100 the materials of the region (0x20 each)
  0x03200  the texture records the faces name (0x20 each), 0x03300 the scene materials PSX_FindSceneMaterial answers (0x20 each)
  0x03400  the part map (0x3c0 bytes), 0x03800 a stitch log (0x1e0), 0x03a00 a checksum list (0x80) and a count cell (0x3af0), 0x03b00 strings, 0x03d00 a pose (0x20 bones of 0x18 bytes)
  0x04100  texture sets (0x80 each), 0x04500 their texture entries (0x40 each), 0x04900 fake engine objects (a vtable, an animated object's storage), 0x04d00 a raw region
  0x05000  the source models (19, 0x300 bytes apart), 0x09000 the build models (19, 0x300 bytes apart)
  0x0d000  the manifest strings (the part names, the texture names)
  0x0e000  the allocator arena
"""

from __future__ import annotations

import struct

DB = 0x00100
DB_BYTES = 0x2090
SKATERS = 0x02200
SKATER_STRIDE = 0x400
SRC_TABLE = 0x02E00
BUILD_TABLE = 0x02F00
TABLES = 0x03000
MATERIALS = 0x03100
TEXTURES = 0x03200
SCENE = 0x03300
PART_MAP = 0x03400
LOG = 0x03800
LIST = 0x03A00
COUNT = 0x03AF0
STRINGS = 0x03B00
POSE = 0x03D00
SETS = 0x04100
SET_ENTRIES = 0x04500
FAKE = 0x04900
RAW = 0x04D00
SRC_MODELS = 0x05000
BUILD_MODELS = 0x09000
MODEL_STRIDE = 0x300
MANIFEST = 0x0D000
ARENA = 0x0E000
ARENA_BYTES = 0x10000
SLAB = ARENA + ARENA_BYTES + 0x100

FAKE_VTABLE = FAKE + 0x00           # ten slots of 4 bytes
ANIM_VTABLE = FAKE + 0x40           # the animated object's vtable: slot 0 is a function nothing else in these flows calls
ANIM_DELETE = 0x00463580
ANIMATED = FAKE + 0x80              # the storage TRG_AllocObjectStorage hands out (0x168 bytes)
MANIFEST_BLOCK = MANIFEST + 0xF00
NAME_STRIDE = 0x20
TEXTURE_NAMES = MANIFEST + 0x400    # the texture group names the manifest names (0x20 each)

# the fake vtable's slots: addresses of retail functions the harness stubs (none is called otherwise by the groups that use them)
FAKE_SLOTS = [0x00421040, 0x004203C0, 0x004214D0, 0x00420860, 0x0041FE50, 0x0041F3D0, 0x00425200, 0x00425270, 0x004252A0, 0x00424A90]

REGION_TABLE = 0x0056D428
REGION_STRIDE = 0x44
PARTS = 19
PART_NAMES = ["head", "stomach", "chest", "right_bicep", "right_forearm", "right_hand", "left_bicep", "left_forearm", "left_hand", "pelvis", "left_thigh", "left_shin", "left_shoe",
              "right_thigh", "right_shin", "right_shoe", "board", "front_wheel", "back_wheel"]
TEXTURE_VOCABULARY = [f"g{g}_{k}" for g in range(4) for k in range(3)] + [f"new_{k}" for k in range(12)]
MATERIAL_COUNT = 8
SET_COUNT = 3


def u8(v):
    return struct.pack("<B", v & 0xFF)


def u16(v):
    return struct.pack("<H", v & 0xFFFF)


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def s16(v):
    return struct.pack("<h", v)


class World:
    """Where the scene is, once seed() has chosen the region records."""

    source = None        # the region record index of the source region
    build = None         # the region record index of the build region
    other = None         # a third free record
    checksums = []       # the checksums of the materials (MATERIAL_COUNT of them)
    palettes = []        # their palette values
    sets = {}            # texture set id -> the address of its record (set by seed_scene)


def free_records(count):
    from opentony.memory import mem

    import gdb

    found = []
    for slot in range(19, 1, -1):
        base = REGION_TABLE + slot * REGION_STRIDE
        if mem.u32(base) == 0 and mem.u32(base + 0x14) == 0 and (mem.u32(base + 0x38) & 0xFFFF) == 0:
            found.append(slot)
            if len(found) == count:
                break
    gdb.write(f"cobj world: free region records {found}\n")
    if len(found) < 2:
        raise RuntimeError("not enough unused region records")
    return found


def region_windows():
    """The record windows the scene uses (the whole region table is small: 20 records)."""
    return [(REGION_TABLE, REGION_STRIDE * 20)]


POSITIONS = [(-200, -100, 50), (-120, 40, -80), (0, 0, 0), (100, 60, 120), (180, -90, -40), (60, -160, 90), (-60, 140, -120), (150, 150, 150), (-180, -170, 10), (30, 100, -200)]


def make_model(rng, base, texture_count, vertices=None, faces=None, flag=None, ghost_limit=1):
    """A PSX model: header (flags +0, vertex count +2, normal count +4, face count +6), vertices and normals (8 bytes each; a vertex flag 1 is a stitch vertex, 2 a ghost whose x names the
    stitch vertex it mirrors in eighths). The positions come from a small pool, so that two models have vertices that sit on the same spot. Returns (bytes, vertex count, face count)."""
    n = rng.randrange(3, 9) if vertices is None else vertices
    face_count = rng.randrange(1, 5) if faces is None else faces
    header = u8(rng.choice([0, 0, 0x01, 0x20]) if flag is None else flag) + u8(0) + u16(n) + u16(n) + u16(face_count) + bytes(0x1C - 8)
    verts = b""
    norms = b""
    for _ in range(n):
        x, y, z = rng.choice(POSITIONS)
        kind = rng.choice([0, 0, 0, 1, 1, 2])
        if kind == 2:
            x = 8 * rng.randrange(0, max(1, ghost_limit))
        verts += s16(x) + s16(y) + s16(z) + u16(kind)
        norms += s16(x) + s16(rng.randrange(-60, 60)) + s16(rng.randrange(-60, 60)) + u16(kind)
    return header + verts + norms, n, face_count


def build_faces(rng, base, offset, face_count, slab):
    """The faces of a model (8 dwords each); each uses a texture record and, when its 0x20 flag is set, a palette of the material of that texture."""
    out = b""
    used = []
    for _ in range(face_count):
        k = rng.randrange(0, MATERIAL_COUNT)
        flagged = rng.random() < 0.4
        palette = World.palettes[k] if rng.random() < 0.9 else rng.getrandbits(32)
        out += u32((8 << 18) | (0x20 if flagged else 0) | rng.randrange(0, 0x10)) + u32(rng.getrandbits(32)) + u32(rng.getrandbits(32)) + u32(rng.getrandbits(32))
        out += u32(slab + TEXTURES + 0x20 * k) + u32(rng.getrandbits(32)) + u32(rng.getrandbits(32)) + u32(palette if flagged else rng.getrandbits(32))
        used.append((k, flagged))
    return out, used


def seed_scene(env, rng, with_build=True):
    """Write the scene into the slab (returns the writes). The region records are chosen here."""
    slab = env.slab
    writes = {}
    found = free_records(3)
    World.source, World.build = found[0], found[1]
    World.other = found[2] if len(found) > 2 else found[1]
    World.checksums = [rng.getrandbits(32) | 1 for _ in range(MATERIAL_COUNT)]
    World.palettes = [rng.getrandbits(32) for _ in range(MATERIAL_COUNT)]
    env.materials = {}
    # the materials of the region (trailing table records) and the texture records the faces name
    for k in range(MATERIAL_COUNT):
        record = bytearray(0x20)
        record[0xC:0x10] = u32(World.palettes[k])
        record[0x18:0x1C] = u32(World.checksums[k])
        writes[slab + MATERIALS + 0x20 * k] = bytes(record)
        texture = bytearray(0x20)
        texture[0x18:0x1C] = u32(World.checksums[k])
        writes[slab + TEXTURES + 0x20 * k] = bytes(texture)
        # the scene material (a rectangle in the picture memory, x0 y0 .. x1 y1 as bytes, and a usage count)
        x0, y0 = rng.randrange(0, 100), rng.randrange(0, 100)
        scene = bytearray(0x20)
        scene[0], scene[1], scene[10], scene[11] = x0, y0, (x0 + rng.randrange(0, 150)) & 0xFF, (y0 + rng.randrange(0, 150)) & 0xFF
        scene[0x10:0x12] = u16(rng.choice([0, 1, 2, 5]))
        writes[slab + SCENE + 0x20 * k] = bytes(scene)
        env.materials[World.checksums[k]] = slab + SCENE + 0x20 * k
    # the trailing tables: tables[19] is the count of the materials, the pointers follow
    tables = bytearray(4 * (PARTS + 1 + MATERIAL_COUNT))
    tables[4 * PARTS:4 * PARTS + 4] = u32(MATERIAL_COUNT)
    for k in range(MATERIAL_COUNT):
        tables[4 * (PARTS + 1 + k):4 * (PARTS + 2 + k)] = u32(slab + MATERIALS + 0x20 * k)
    writes[slab + TABLES] = bytes(tables)
    # the source models, and the build region's models: the same bytes (what the stitcher's copy leaves there)
    table = u32(PARTS)
    build_table = u32(PARTS)
    for p in range(PARTS):
        at = slab + SRC_MODELS + MODEL_STRIDE * p
        model, n, face_count = make_model(rng, at, MATERIAL_COUNT)
        faces, _ = build_faces(rng, at, len(model), face_count, slab)
        writes[at] = model + faces
        writes[slab + BUILD_MODELS + MODEL_STRIDE * p] = model + faces
        table += u32(at)
        build_table += u32(slab + BUILD_MODELS + MODEL_STRIDE * p)
    writes[slab + SRC_TABLE] = table
    writes[slab + BUILD_TABLE] = build_table
    build = REGION_TABLE + World.build * REGION_STRIDE
    writes[build] = b"CUSTOM0\0"
    writes[build + 0x14] = u32(slab + BUILD_TABLE + 4)
    writes[build + 0x38] = u16(PARTS)
    # the part database
    db = bytearray(DB_BYTES)
    for i in range(PARTS):
        entry = 4 + 0x34 * i
        name = PART_NAMES[i].encode()
        db[entry:entry + len(name) + 1] = name + b"\0"
        db[entry + 0x10:entry + 0x14] = u32(rng.randrange(0, 4))
        db[entry + 0x14:entry + 0x18] = u32(i)
        db[entry + 0x18:entry + 0x1C] = u32(rng.randrange(0, 4))
        count = rng.randrange(1, 4)
        for k in range(count):
            db[entry + 0x1C + 4 * k:entry + 0x20 + 4 * k] = u32(World.checksums[rng.randrange(0, MATERIAL_COUNT)])
        db[entry + 0x30:entry + 0x34] = u32(count)
    db[0x1864:0x1868] = u32(PARTS)
    for g in range(4):
        count = rng.randrange(1, 5)
        db[0x1C68 + 4 * g:0x1C6C + 4 * g] = u32(count)
        for i in range(16):
            label = (f"g{g}_{i}" if i < count else "").encode()
            at = 0x1868 + i * 0x40 + g * 0x10
            db[at:at + len(label) + 1] = label + b"\0"
    db[0x1C78:0x1C7C] = u32(World.source)
    db[0:4] = u32(0x0051876C)
    writes[slab + DB] = bytes(db)
    # the source region record: its name, model table, raw region and part count
    source = REGION_TABLE + World.source * REGION_STRIDE
    writes[source] = b"sk2def0\0"
    writes[source + 0x14] = u32(slab + SRC_TABLE + 4)
    writes[source + 0x18] = u32(slab + RAW)
    writes[source + 0x38] = u16(PARTS)
    # the fake skaters
    for k in range(3):
        skater = bytearray(0x31C)
        skater[0x280:0x284] = u32(slab + DB)
        skater[0x84:0x88] = u32(World.build)
        for i in range(PARTS):
            skater[0x1C + 4 * i:0x20 + 4 * i] = u32(i)
        writes[slab + SKATERS + SKATER_STRIDE * k] = bytes(skater)
    # the texture sets and their texture entries (a set: +8 name, +0x30 four entry pointers, +0x41 has a second texture)
    World.sets = {}
    for k in range(SET_COUNT):
        record = bytearray(0x80)
        name = TEXTURE_VOCABULARY[0].encode()                  # (a set is named like the texture group the part database lists: FindBodyGroup finds it)
        record[8:8 + len(name) + 1] = name + b"\0"
        for j in range(4):
            entry = bytearray(0x40)
            texture = f"skin{k}{j}_{rng.randrange(0, 3)}.bmp".encode()
            entry[8:8 + len(texture) + 1] = texture + b"\0"
            entry[0x18] = rng.choice([0, 0, 1])
            writes[slab + SET_ENTRIES + 0x40 * (4 * k + j)] = bytes(entry)
            record[0x30 + 4 * j:0x34 + 4 * j] = u32(slab + SET_ENTRIES + 0x40 * (4 * k + j))
        record[0x40] = 4
        record[0x41] = rng.choice([0, 1, 1])
        writes[slab + SETS + 0x80 * k] = bytes(record)
        World.sets[k + 1] = slab + SETS + 0x80 * k
    # the fake engine objects: a vtable the harness stubs, the animated object's storage (its first word is a pointer to that vtable)
    writes[slab + FAKE_VTABLE] = b"".join(u32(a) for a in FAKE_SLOTS)
    writes[slab + ANIM_VTABLE] = u32(ANIM_DELETE)
    writes[slab + ANIMATED] = u32(slab + ANIM_VTABLE)
    # the pose (32 bones of a rotation matrix and a translation, small values)
    pose = b""
    for _ in range(0x20):
        matrix = [rng.choice([0, 0, 0, 8, -8]) for _ in range(9)]
        matrix[0] = matrix[4] = matrix[8] = 0x1000
        pose += b"".join(s16(v) for v in matrix) + b"".join(s16(rng.randrange(-4, 5)) for _ in range(3))
    writes[slab + POSE] = pose
    # the manifest strings: the part names, then the texture group names
    for i, name in enumerate(PART_NAMES):
        writes[slab + MANIFEST + NAME_STRIDE * i] = name.encode() + b"\0"
    for j, name in enumerate(TEXTURE_VOCABULARY):
        writes[slab + TEXTURE_NAMES + NAME_STRIDE * j] = name.encode() + b"\0"
    return writes
