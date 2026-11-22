"""Phase 4.9 case generators: stitching a custom skater's body models (create_stitch.cpp).

Everything lives in the scratch slab (the whole slab is captured):
  0x0000  the source region record (model table pointer +0x14, part count +0x38)
  0x0100  the build region record
  0x0200  the source model table (19 pointers), 0x0300 the build model table
  0x0400  the part map (0x78 pairs of dwords), 0x0800 a source stitch log (0x78 dwords)
  0x0a00  an output log, 0x0c00 the stitcher object (only its address is used)
  0x1000  the nineteen source models (0x200 bytes apart), 0x3600 the nineteen build models
  0x6000  the allocator arena for the copies
The models are generated once (seed) with random vertices, stitch and ghost flags and variable-length
faces; the cases vary the part counts, the hidden flags, the part map and which vertices are stitched.
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec

SRC_REGION = 0x0000
BUILD_REGION = 0x0100
SRC_TABLE = 0x0200
BUILD_TABLE = 0x0300
MAP = 0x0400
SRC_LOG = 0x0800
OUT_LOG = 0x0A00
STITCHER = 0x0C00
SRC_MODELS = 0x1000
BUILD_MODELS = 0x3600
MODEL_STRIDE = 0x200
ARENA = 0x6000
ARENA_SIZE = 0x3000
PARTS = 19


def s16(v):
    return struct.pack("<h", v)


def make_model(rng, ghost_limit):
    """A random model: (bytes, number of stitch vertices, vertex count)."""
    n = rng.randrange(3, 13)
    vertices = b""
    normals = b""
    stitches = 0
    for _ in range(n):
        flag = rng.choice([0, 0, 1, 1, 2, 2, 3])
        if flag & 1:
            stitches += 1
        x = rng.randrange(-200, 200)
        if flag & 2 and not flag & 1:
            x = 8 * rng.randrange(0, max(1, ghost_limit))        # a ghost names the stitch vertex it mirrors
        vertices += s16(x) + s16(rng.randrange(-200, 200)) + s16(rng.randrange(-200, 200)) + u16(flag)
        normals += s16(x) + s16(rng.randrange(-50, 50)) + s16(rng.randrange(-50, 50)) + u16(flag)
    faces = b""
    face_count = rng.randrange(0, 5)
    for _ in range(face_count):
        length = rng.randrange(1, 6)
        faces += u32((length << 18) | rng.getrandbits(18)) + b"".join(u32(rng.getrandbits(32)) for _ in range(length - 1))
    header = u8(rng.choice([0, 0, 0x01, 0x20])) + u8(0) + u16(n) + u16(n) + u16(face_count) + bytes(0x1C - 8)
    return header + vertices + normals + faces, stitches, n


def seed_stitch(env, rng):
    slab = env.slab
    writes = {}
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    src_totals = []
    src_stitches = 0
    for p in range(PARTS):
        data, stitches, _ = make_model(rng, 1)
        src_totals.append(stitches)
        writes[slab + SRC_MODELS + p * MODEL_STRIDE] = data
    total = sum(src_totals)
    env.src_stitches = src_totals
    env.build_vertices = []
    for p in range(PARTS):
        data, stitches, n = make_model(rng, total)
        writes[slab + BUILD_MODELS + p * MODEL_STRIDE] = data
        env.build_vertices.append(n)
    writes[slab + SRC_TABLE] = b"".join(u32(slab + SRC_MODELS + p * MODEL_STRIDE) for p in range(PARTS))
    writes[slab + BUILD_TABLE] = b"".join(u32(slab + BUILD_MODELS + p * MODEL_STRIDE) for p in range(PARTS))
    writes[slab + SRC_REGION + 0x14] = u32(slab + SRC_TABLE)
    writes[slab + SRC_REGION + 0x38] = u16(PARTS)
    writes[slab + BUILD_REGION + 0x14] = u32(slab + BUILD_TABLE)
    writes[slab + BUILD_REGION + 0x38] = u16(PARTS)
    return writes


SLAB_BYTES["stitch"] = 0x9000
SEEDS["stitch"] = seed_stitch
READ_ONLY["stitch"] = []


def map_bytes(rng, count=PARTS, complete=True):
    """A part map: a random permutation of the nineteen parts (every source part is mapped), the rest unused."""
    order = list(range(PARTS))
    rng.shuffle(order)
    entries = b"".join(u32(i) + u32(order[i]) for i in range(count))
    return entries + b"".join(u32(0xFFFFFFFF) + u32(0xFFFFFFFF) for _ in range(0x78 - count))


def model_at(env, base, p):
    return env.slab + base + p * MODEL_STRIDE


def stitcher(env):
    return env.slab + STITCHER


@spec(0x00425110, "Model_CountStitchVertices", "stitch")
def gen_count(env, n):
    rng = env.rng
    return [model_at(env, rng.choice([SRC_MODELS, BUILD_MODELS]), rng.randrange(PARTS))], {}


@spec(0x00425180, "Model_ByteSize", "stitch")
def gen_size(env, n):
    rng = env.rng
    return [model_at(env, rng.choice([SRC_MODELS, BUILD_MODELS]), rng.randrange(PARTS))], {}


@spec(0x00425200, "PartMap_Lookup", "stitch")
def gen_lookup(env, n):
    p = Pokes()
    rng = env.rng
    p.put(env.slab + MAP, map_bytes(rng, count=rng.choice([PARTS, PARTS, 5, 0])))
    return [rng.choice([rng.randrange(PARTS)] * 4 + [-1, 30]), env.slab + MAP], p.pokes


@spec(0x00425270, "PartMap_ReverseLookup", "stitch")
def gen_reverse(env, n):
    p = Pokes()
    rng = env.rng
    p.put(env.slab + MAP, map_bytes(rng, count=rng.choice([PARTS, PARTS, 5, 0])))
    return [rng.choice([rng.randrange(PARTS)] * 4 + [-1, 30]), env.slab + MAP], p.pokes


@spec(0x004252A0, "PartMap_Has", "stitch")
def gen_has(env, n):
    p = Pokes()
    rng = env.rng
    p.put(env.slab + MAP, map_bytes(rng, count=rng.choice([PARTS, PARTS, 5, 0])))
    return [rng.choice([rng.randrange(PARTS)] * 4 + [-1, 30]), env.slab + MAP], p.pokes


@spec(0x00424A90, "PartStitcher_LogStitches", "stitch", this=stitcher)
def gen_log(env, n):
    p = Pokes()
    rng = env.rng
    region = rng.choice([SRC_REGION, BUILD_REGION])
    p.put(env.slab + region + 0x38, u16(rng.choice([PARTS, PARTS, 1, 7, 0])))
    p.put(env.slab + OUT_LOG, bytes(0xA5 for _ in range(PARTS * 4)))
    return [env.slab + region, env.slab + OUT_LOG], p.pokes


@spec(0x00424BB0, "PartStitcher_CopyModels", "stitch", this=stitcher)
def gen_copy(env, n):
    p = Pokes()
    rng = env.rng
    p.put(env.slab + MAP, map_bytes(rng))
    for part in range(PARTS):
        if rng.random() < 0.3:
            p.put(model_at(env, SRC_MODELS, part), u8(rng.choice([0x20, 0x21, 0x00])))   # hidden / visible
    # a build region that already holds some models (they are deleted first)
    holders = [rng.choice([0, env.slab + BUILD_MODELS + q * MODEL_STRIDE]) for q in range(PARTS)]
    p.put(env.slab + BUILD_TABLE, b"".join(u32(h) for h in holders))
    p.put(env.slab + SRC_REGION + 0x38, u16(rng.choice([PARTS, PARTS, PARTS, 12])))
    return [env.slab + SRC_REGION, env.slab + BUILD_REGION, env.slab + MAP], p.pokes


@spec(0x00424E40, "PartStitcher_ReorderStitches", "stitch", this=stitcher)
def gen_reorder(env, n):
    p = Pokes()
    rng = env.rng
    p.put(env.slab + MAP, map_bytes(rng))
    log, total = b"", 0
    for count in env.src_stitches:
        log += u32(total)
        total += count
    p.put(env.slab + SRC_LOG, log)
    # (the build region always has all 19 parts: a log entry past its part count is uninitialised stack in retail)
    p.put(env.slab + SRC_REGION + 0x38, u16(rng.choice([PARTS, PARTS, 14])))
    return [env.slab + SRC_REGION, env.slab + BUILD_REGION, env.slab + SRC_LOG, env.slab + MAP], p.pokes


@spec(0x004257D0, "PartStitcher_AddStitch", "stitch")
def gen_add(env, n):
    p = Pokes()
    rng = env.rng

    def pick():
        part = rng.randrange(PARTS)
        vertex = rng.randrange(env.build_vertices[part])
        model = model_at(env, BUILD_MODELS, part)
        return model + 0x1C + vertex * 8, model + 0x1C + env.build_vertices[part] * 8 + vertex * 8
    stitch_vertex, stitch_normal = pick()
    ghost_vertex, ghost_normal = pick()
    while ghost_vertex == stitch_vertex:
        ghost_vertex, ghost_normal = pick()
    p.put(env.slab + BUILD_REGION + 0x38, u16(rng.choice([PARTS, PARTS, 9])))
    return [env.slab + BUILD_REGION, stitch_vertex, ghost_vertex, stitch_normal, ghost_normal], p.pokes
