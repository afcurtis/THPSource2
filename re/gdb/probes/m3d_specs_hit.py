"""Phase 4.15a probe: the collision of a line with the linked objects of the world (m3dcolij.cpp and the 0x004f43e0 culling; group m3h): the culling of an object chain against a box (0x004f43e0), the test of a
line against the faces of a model (0x004f4c50) and against the faces of an object (0x00463e50).

Like m3d_specs.py the GTE register file (0x006a3e00, 0x120 bytes) is a window poked with random words, and the model tables of the regions (0x0056d43c, 0x44 bytes each) are redirected into the slab; the
globals the face test works with (the face masks, the trigger face, the scratch matrix and point, the best normal), the vertex buffer pointer and the environment lists are windows. The environment index
lookup (0x004b3990) and the trigger command point (0x004c4bd0) are stubs; the helpers (the matrix routines, the vertex transform, the corner and overlap tests) are the real code.

Slab layout (offsets): the model pointer table at 0x000 (8 dwords), four models of 0x100 bytes at 0x100 (the vertex count at +2, the normal count at +4, the face count at +6, the radius at +8, the packed box
at +0xc, the vertices from +0x1c: eight bytes each, then the normals, then the faces: sixteen bytes each), the vertex buffer at 0x500 (0x100 bytes), the line record at 0x600 (0x90 bytes), five objects of 0x40
bytes at 0x700, the culling box at 0x840 (six dwords), the environment command arrays at 0x880 (two of 0x20 bytes).
"""

from __future__ import annotations

import m3d_specs as ms
from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from rail_specs import changed

GROUPS = ("m3h",)

SLAB = 0xE00
TABLE = 0x000
MODELS = 0x100
MODEL_STRIDE = 0x100
MODEL_SLOTS = 4
VERTS = 0x500
INFO = 0x600
OBJECTS = 0x700
OBJECT_STRIDE = 0x40
OBJECT_SLOTS = 5
BOX = 0x840
COMMANDS = 0x880
SPHERES = 0x9C0
SPHERE_STRIDE = 0xB8
LINE = 0xCC0                  # the start, the end and the hit point (0x10 bytes each)

GTE = 0x006A3E00
GTE_BYTES = 0x120
MODEL_TABLES = 0x0056D43C
REGIONS = 3
VERTEX_BUFFER = 0x005620BC
FACE_MASK_A = 0x00567A60
HIT_OBJECT = 0x00567A6C
BEST_DISTANCE = 0x005643A8
TRIGGER_FACE = 0x00567A64
FACE_MASK_B = 0x00567A68
SCRATCH = 0x00567BB0
BEST_NORMAL = 0x0056438C
TRIGGER_FLAG = 0x00567C80
ENV_FLAG = 0x0056DB2C
ENV_LISTS = 0x0056D41C

VIEW = [(0x005620C0, 0x40), (0x00563A96, 0x10)]
VIEW_CONSTANTS = [(0x00519A74, 0x0C), (0x00518910, 4), (0x00518484, 4), (0x00549D98, 4)]
CAMERA = 0xB80                # the camera record of the slab (the position as three dwords from +4)

WINDOWS = VIEW + [(GTE, GTE_BYTES), (MODEL_TABLES, 0x44 * REGIONS + 4), (VERTEX_BUFFER, 4), (FACE_MASK_A, 0x10), (BEST_DISTANCE, 4), (SCRATCH, 0x20), (BEST_NORMAL, 0x10), (TRIGGER_FLAG, 4), (ENV_FLAG, 4), (ENV_LISTS, 8)]

STATE = {"env": 0}


def m3h_regions(slab):
    return merged([(slab, SLAB)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = m3h_regions
    READ_ONLY[_group] = VIEW_CONSTANTS


def m3h(address, name):
    return spec(address, name, "m3h")


def install_hit_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_m3h_installed", False):
        return
    fc._m3h_installed = True
    mark_under_test()
    ensure(fc, 0x004B3990, 1, "engine_004b3990", result=lambda a: STATE["env"])
    ensure(fc, 0x004C4BD0, 2, "engine_004c4bd0")


GROUP_STUBS = {group: install_hit_stubs for group in GROUPS}


# ---- the worlds -------------------------------------------------------------------------------------------------------------------------------------------------------------

def coordinate(rng, scale=0x200):
    return rng.randrange(-scale, scale + 1) if rng.random() < 0.9 else rng.randrange(-0x8000, 0x8000)


def put_models(img, env, rng, region, *, faces=True, trigger=False):
    """The model table of the region and four models with vertices, normals and faces; returns the vertex counts."""
    slab = env.slab
    img.u32(MODEL_TABLES + 0x44 * region, slab + TABLE)
    counts = []
    for k in range(MODEL_SLOTS):
        img.u32(slab + TABLE + 4 * k, slab + MODELS + MODEL_STRIDE * k)
        model = slab + MODELS + MODEL_STRIDE * k
        vertices = rng.choice([1, 2, 3, 4, 5, 6, 6])
        normals = rng.choice([1, 2, 3, 4])
        count = rng.choice([0, 1, 2, 3, 4, 5, 6]) if faces else 0
        img.u16(model + 2, vertices)
        img.u16(model + 4, normals)
        img.u16(model + 6, count)
        img.u32(model + 8, rng.choice([0x1000, 0x40000, 0x100000, 0x7FFFFFFF, 0, rng.randrange(0, 0x400000)]))
        for j in range(3):
            img.u32(model + 0xC + 4 * j, ((rng.randrange(-0x40, 0x40) & 0xFFFF) << 16) | (rng.randrange(-0x40, 0x40) & 0xFFFF))
        for v in range(vertices):
            for j in range(3):
                img.u16(model + 0x1C + 8 * v + 2 * j, coordinate(rng) & 0xFFFF)
            img.u16(model + 0x1C + 8 * v + 6, rng.getrandbits(16))
        base = model + 0x1C + 8 * vertices
        for nrm in range(normals):
            for j in range(3):
                img.u16(base + 8 * nrm + 2 * j, coordinate(rng, 0x1000) & 0xFFFF)
            img.u16(base + 8 * nrm + 6, rng.getrandbits(16))
        face = base + 8 * normals
        for f in range(count):
            at = face + 0x10 * f
            img.u32(at, (0x10 << 16) | rng.choice([0, 0x10, 0x10, 0x11, rng.getrandbits(16) & 0xFFFF]))
            indices = [rng.randrange(0, max(vertices, 1)) for _ in range(4)]
            img.u32(at + 4, indices[0] | (indices[1] << 8) | (indices[2] << 16) | (indices[3] << 24))
            img.u32(at + 8, rng.getrandbits(32))
            flags = rng.choice([0, 0x10000, 0x20000, 0x30000, 0x40000, 0x20000, rng.getrandbits(32)]) & ~0xFFF8 | (rng.randrange(0, normals) << 3)
            if trigger:
                flags = 0x20000 | (rng.randrange(0, normals) << 3)         # (zone faces: the line marks the zone it passes through)
            img.u32(at + 0xC, flags & 0xFFFFFFFF)
        counts.append(vertices)
    return counts


def put_globals(img, rng):
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    img.u32(FACE_MASK_A, rng.choice([0, 0, 0, 0x40000, 0x20, rng.getrandbits(32) & rng.getrandbits(32)]))
    img.u32(FACE_MASK_B, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0x0, rng.getrandbits(32)]))
    img.u32(TRIGGER_FACE, rng.choice([0xFFFFFFFF, 0, 3, rng.getrandbits(32)]))
    img.u32(ENV_FLAG, rng.getrandbits(32))
    img.u32(TRIGGER_FLAG, rng.getrandbits(32))
    img.put(BEST_NORMAL, bytes(rng.getrandbits(8) for _ in range(0x10)))


def put_info(img, env, rng):
    info = env.slab + INFO
    start = [rng.randrange(-0x40, 0x40) << 12 for _ in range(3)]
    for k in range(3):
        img.u32(info + 4 * k, start[k] & 0xFFFFFFFF)
        img.u32(info + 0xC + 4 * k, (start[k] + rng.randrange(-0x40, 0x40) * 0x1000 + rng.randrange(-0x800, 0x800)) & 0xFFFFFFFF)
    img.u32(info + 0x40, rng.choice([0x7FFFFFFF, 0x100000, 0x1000, 0, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(info + 0x44, rng.choice([1, 0x1000, 0x40000, 0x7FFFF, 0x100, rng.randrange(1, 0x200000)]))
    for k in range(9):
        img.u16(info + 0x48 + 2 * k, ms.short(rng))
    img.u32(info + 0x68, rng.getrandbits(32))
    for k in range(3):
        img.u32(info + 0x6C + 4 * k, rng.getrandbits(32))
    img.u32(info + 0x80, rng.getrandbits(32))
    img.u32(info + 0x84, rng.getrandbits(32))
    img.u8(info + 0x88, rng.choice([0, 1, 1, 0xFF]))
    img.u8(info + 0x89, rng.choice([0, 1]))
    return info


def put_object(img, env, rng, k, region, *, next_object=0, rotate=0.5):
    obj = env.slab + OBJECTS + OBJECT_STRIDE * k
    img.u16(obj + 4, rng.choice([0, 0x20, 0x41, 0x61, 0x01, 0x40, 0x100, rng.getrandbits(16)]))
    img.u8(obj + 5, rng.choice([0, 0, 2, 3, rng.getrandbits(8)]))
    img.u16(obj + 6, rng.choice([0, 1, 5, 0x7FFF, 0xFFFF, rng.getrandbits(16)]))
    for j in range(3):
        img.u32(obj + 8 + 4 * j, (rng.randrange(-0x80, 0x80) << 12) & 0xFFFFFFFF if rng.random() < 0.9 else rng.getrandbits(32))
    for j in range(3):
        img.u16(obj + 0x14 + 2 * j, rng.choice([0, 0x400, 0x800, 0xC00, rng.randrange(0, 0x1000), rng.getrandbits(16)]) if rng.random() < rotate else 0)
    img.u16(obj + 0x1A, rng.randrange(0, MODEL_SLOTS))
    img.u8(obj + 0x1F, region)
    img.u32(obj + 0x20, next_object)
    for j in range(3):
        img.u16(obj + 0x28 + 2 * j, rng.choice([0, 0x1000, 0x800, 0x2000, ms.short(rng)]))
    return obj


# ---- the functions ----------------------------------------------------------------------------------------------------------------------------------------------------------

@m3h(0x004F4C50, "Collision_TestLinkedModelFaces")
def gen_model_faces(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    region = rng.randrange(0, REGIONS)
    trigger = rng.random() < 0.4
    counts = put_models(img, env, rng, region, trigger=trigger)
    put_globals(img, rng)
    info = put_info(img, env, rng)
    if trigger:
        img.u8(info + 0x88, 1)
        img.u32(FACE_MASK_A, 0)
        img.u32(FACE_MASK_B, 0xFFFFFFFF)
    for v in range(32):
        for j in range(3):
            img.u16(slab + VERTS + 8 * v + 2 * j, coordinate(rng) & 0xFFFF)
        img.u16(slab + VERTS + 8 * v + 6, rng.choice([0, 0, 0, 0x60F, 1, 4, 0x200, rng.getrandbits(16)]))
    obj = put_object(img, env, rng, 0, region)
    model = slab + MODELS + MODEL_STRIDE * rng.randrange(0, MODEL_SLOTS)
    return [model, slab + VERTS, info, obj], changed(env, img)


@m3h(0x00463E50, "Collision_TestLinkedObjectFaces")
def gen_object_faces(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    region = rng.randrange(0, REGIONS)
    trigger = rng.random() < 0.4
    put_models(img, env, rng, region, trigger=trigger)
    put_globals(img, rng)
    info = put_info(img, env, rng)
    if trigger:
        img.u8(info + 0x88, 1)
        img.u32(FACE_MASK_A, 0)
        img.u32(FACE_MASK_B, 0xFFFFFFFF)
    img.u32(VERTEX_BUFFER, slab + VERTS)
    img.put(slab + VERTS, bytes(rng.getrandbits(8) for _ in range(0x100)))
    obj = put_object(img, env, rng, 0, region, rotate=rng.choice([0.0, 0.5, 1.0]))
    for k in range(2):
        img.u32(ENV_LISTS + 4 * k, slab + COMMANDS + 0x20 * k)
        for j in range(8):
            img.u32(slab + COMMANDS + 0x20 * k + 4 * j, rng.getrandbits(32))
    STATE["env"] = rng.choice([0, 1])
    return [obj, info], changed(env, img)


@m3h(0x004F43E0, "Collision_CullLinkedObjects")
def gen_cull_linked(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    region = rng.randrange(0, REGIONS)
    put_models(img, env, rng, region, faces=False)
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    count = rng.choice([0, 1, 2, 3, 5])
    head = 0
    for k in reversed(range(count)):
        head = put_object(img, env, rng, k, region, next_object=head if k + 1 < count else 0, rotate=rng.choice([0.0, 0.5, 1.0]))
    base = [rng.randrange(-0x40, 0x40) << 12 for _ in range(3)]
    for k in range(3):
        img.u32(slab + BOX + 4 * k, base[k] & 0xFFFFFFFF)
        img.u32(slab + BOX + 12 + 4 * k, (base[k] + rng.randrange(-0x40, 0x80) * 0x1000) & 0xFFFFFFFF)
    return [head, rng.getrandbits(32), slab + BOX, rng.choice([0, 1, 2, 5, 0x7FFF, 0xFFFF, rng.getrandbits(32)])], changed(env, img)


@m3h(0x00464200, "M3dColij_LineToSphere")
def gen_line_to_sphere(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    img.u32(HIT_OBJECT, rng.choice([0, 0x1234, rng.getrandbits(32)]))
    img.u32(BEST_DISTANCE, rng.getrandbits(32))
    start = [rng.randrange(-0x20, 0x20) << 12 for _ in range(3)]
    delta = rng.choice([[0, 0, 0], [rng.randrange(-0x20, 0x20) << 12 for _ in range(3)], [rng.randrange(-0x3000, 0x3000) for _ in range(3)], [rng.randrange(-0x40, 0x40) << 12, 0, 0]])
    end = [start[k] + delta[k] for k in range(3)]
    line = slab + LINE
    for k in range(3):
        img.u32(line + 4 * k, start[k] & 0xFFFFFFFF)
        img.u32(line + 0x10 + 4 * k, end[k] & 0xFFFFFFFF)
        img.u32(line + 0x20 + 4 * k, rng.getrandbits(32))
    scale = rng.choice([0x1000, 0x1000, 0x800, 0x100, 1, 0, 0x7FFF])
    count = rng.choice([0, 1, 2, 3, 4])
    objects = [slab + SPHERES + SPHERE_STRIDE * k for k in range(4)]
    for k in range(4):
        obj = objects[k]
        radius = rng.choice([1, 2, 4, 8, 0x10, 0x40, 0x100, rng.randrange(0, 0x400)])
        img.u16(obj + 0xAE, radius)
        img.u8(obj + 0x6A, rng.choice([0, 0, 0, 0x40, 0xFF, rng.getrandbits(8)]))
        t = rng.choice([-0.3, 0.0, 0.2, 0.5, 0.8, 1.0, 1.3, rng.random()])
        spread = max(radius * scale, 0x1000) * rng.choice([0, 0.3, 1.0, 2.0])
        for j in range(3):
            centre = int(start[j] + delta[j] * t + rng.uniform(-spread, spread))
            img.u32(obj + 8 + 4 * j, centre & 0xFFFFFFFF)
        img.u32(obj + 0x20, objects[k + 1] if k + 1 < count else 0)
    head = objects[0] if count else 0
    skip = rng.choice([0, 0, objects[rng.randrange(0, 4)], rng.getrandbits(32)])
    return [line, line + 0x10, line + 0x20, head, skip, scale], changed(env, img)


@m3h(0x004F39F0, "Render_FlagObjectsOutsideView")
def gen_view(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    off_centre = rng.random() < 0.6
    region = rng.randrange(0, REGIONS)
    put_models(img, env, rng, region, faces=False)
    camera = slab + CAMERA
    img.u32(0x005620E0, camera)
    for k in range(3):
        img.u32(camera + 4 + 4 * k, rng.randrange(-0x400 if not off_centre else -0x10, 0x400 if not off_centre else 0x10) & 0xFFFFFFFF)
    for plane in range(6):
        base = (0x005620E8 + 6 * plane) if plane < 3 else (0x005620C0 + 6 * (plane - 3))
        for k in range(3):
            img.u16(base + 2 * k, rng.choice([0x1000, -0x1000, 0, rng.randrange(-0x1000, 0x1001), ms.short(rng)]) & 0xFFFF)
    img.u16(0x00563A96, rng.choice([0, 100, -100, rng.randrange(-4000, 4000)]) & 0xFFFF)
    img.u16(0x00563A9E, rng.choice([0, 100, -100, rng.randrange(-4000, 4000)]) & 0xFFFF)
    for k in range(MODEL_SLOTS):
        model = slab + MODELS + MODEL_STRIDE * k
        img.u32(model + 8, rng.choice([0x1000, 0x40000, 0x100000, 0, rng.randrange(0, 0x400000)] if not off_centre else [0, 0x1000, 0x2000]))
        img.u16(model + 6, rng.choice([0, 5, 10, 11, 20]) if not off_centre else rng.choice([11, 20]))
        if off_centre:
            # a box that does not hold the object position: all its corners on one side
            for j in range(3):
                low = rng.randrange(-0x70, 0x70)
                high = low + rng.randrange(0, 0x10)
                img.u32(model + 0xC + 4 * j, ((high & 0xFFFF) << 16) | (low & 0xFFFF))
    count = rng.choice([0, 1, 2, 3, 5])
    head = 0
    for k in reversed(range(count)):
        head = put_object(img, env, rng, k, region, next_object=head if k + 1 < count else 0, rotate=rng.choice([0.0, 0.0, 0.5]))
        obj = slab + OBJECTS + OBJECT_STRIDE * k
        img.u16(obj + 4, rng.choice([0, 0, 0, 0x8000, 0x2, 0x200, 0x202, 0x2000, 0x1000, rng.getrandbits(16)]))
        for j in range(3):
            img.u32(obj + 8 + 4 * j, (rng.randrange(-0x300 if not off_centre else -0x20, 0x300 if not off_centre else 0x20) << 12 | rng.randrange(0, 0x1000)) & 0xFFFFFFFF)
    return [head, rng.choice([0, 0, 1, 0x40, 0x8000, 0x2000, rng.getrandbits(16)])], changed(env, img)
