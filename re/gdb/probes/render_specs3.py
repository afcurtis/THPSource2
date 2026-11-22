"""Phase 4.15e probe: the animation pose (group ranim: decomp.cpp 0x004305f0..0x0043125f and the pose side of m3d.cpp 0x00464b90..0x00465110).

A model object (0x180 bytes) names its region (a byte at +0x1f); the region record (0x44 bytes at 0x0056d428 + 0x44 * region) holds the pointers the pose code goes through: the model table (its count
a dword before it), the raw region (the bone count at +8), the animation table (its kind a dword before it: 0x2a uncompressed, 0x2c compressed), the hierarchy table (a parent index per bone: the root
is its own parent) and the hooks. The generators lay a complete legal scene out in the slab: a random tree, a stream of random channels the decoder can run through, the key frames of the blends.

Slab layout (offsets): fake vtable 0x0000, model 0x0100 (0x180), animation table 0x0308 (0x400; its kind at 0x0300), models 0x0800 (0x40), raw region 0x0840 (0x20), hierarchy 0x0880 (0x40), hooks 0x08c0
(0x80), packet 0x0a00 (0x200), hook record 0x0c00 (0x20), second model object 0x0c40 (0x180), stream / key frames 0x1000 (0x2000), output 0x3000 (0x1000), arena 0x4000 (0x4000).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from render_specs import SCRATCH, noise, word

GROUPS = ("ranim",)

VTABLE = 0x0000
MODEL = 0x0100
KIND = 0x0300
ANIMS = 0x0308
MODELS = 0x0800
RAW = 0x0840
HIERARCHY = 0x0880
HOOKS = 0x08C0
PACKET = 0x0A00
HOOK = 0x0C00
MODEL2 = 0x0C40
STREAM = 0x1000
STREAM_BYTES = 0x2000
OUT = 0x3000
ARENA = 0x4000
ARENA_BYTES = 0x4000
SLAB = ARENA + ARENA_BYTES + 0x100

REGION_RECORDS = 0x0056D428
REGION_STRIDE = 0x44
POSE_BUFFER = 0x00567CB0
GLOBALS = [(REGION_RECORDS, 21 * REGION_STRIDE), (POSE_BUFFER, 0x2D8), (0x006A3E00, 0x120)]
FAKE_DELETE = 0x004AD950


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {env.slab + VTABLE: struct.pack("<4I", FAKE_DELETE, FAKE_DELETE, FAKE_DELETE, FAKE_DELETE)}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_render3_installed", False):
        return
    fc._render3_installed = True
    mark_under_test()
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    ensure(fc, FAKE_DELETE, 1, "engine_004ad950", convention="thiscall", this=True)        # slot 0 of the fake children (the deleting destructor)
    ensure(fc, 0x00466BB0, 1, "engine_00466bb0")                                           # operator delete


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def an(address, name):
    return spec(address, name, "ranim", this=None)


def an_this(address, name, this):
    return spec(address, name, "ranim", this=(lambda env: env.slab + this))


def model_at(env, k=0):
    return env.slab + (MODEL, MODEL2)[k]


def random_tree(rng, count):
    """A legal hierarchy: one root (its own parent) and every other bone's parent a bone that is already placed."""
    order = list(range(count))
    rng.shuffle(order)
    parent = [0] * count
    parent[order[0]] = order[0]
    for k in range(1, count):
        parent[order[k]] = order[rng.randrange(0, k)]
    return parent


def put_region(img, env, rng, region, count, kind, with_hooks=True):
    """The region record of `region`: tables in the slab, `count` bones."""
    record = REGION_RECORDS + REGION_STRIDE * region
    img.u32(record + 0x14, env.slab + MODELS + 4)
    img.u32(record + 0x18, env.slab + RAW)
    img.u32(record + 0x1C, env.slab + ANIMS)
    img.u32(record + 0x20, env.slab + HIERARCHY)
    img.u32(record + 0x24, env.slab + HOOKS)
    img.u16(record + 0x38, count)
    img.u32(env.slab + MODELS, count)
    img.u32(env.slab + RAW + 8, count)
    img.u32(env.slab + KIND, kind)
    return record


def put_animations(img, env, rng, count_anims, frames, kind, segments=0):
    """The animation table: the count, then (offset, frame count) per animation; `segments` (the high half of the frame count) asks for key frames."""
    img.u32(env.slab + ANIMS, count_anims)
    for k in range(count_anims):
        img.u32(env.slab + ANIMS + 4 + 8 * k, (STREAM - ANIMS) + 0x900 * k)
        img.u32(env.slab + ANIMS + 8 + 8 * k, (frames[k] & 0xFFFF) | (segments << 16))


def put_stream(img, env, rng, offset=0, size=0x1800):
    img.put(env.slab + STREAM + offset, noise(rng, size))


def put_model(img, env, rng, k, region, anim, frame, flags=0, loop=0):
    at = model_at(env, k)
    img.put(at, noise(rng, 0x180))
    img.u8(at + 0x1F, region)
    img.u8(at + 4, flags)
    img.u32(at + 0xE8, 0)
    img.u32(at + 0xEC, 0)
    img.u16(at + 0xF0, 0xFFFF)
    img.u8(at + 0xF2, rng.choice([anim, anim ^ 1, 0xFF]))
    img.u8(at + 0xF3, rng.randrange(0, 0x80))
    img.u16(at + 0xF4, frame)
    img.u16(at + 0xF6, anim)
    img.u8(at + 0xF8, loop)
    img.u32(at + 0x138, 0)
    img.u32(at + 0x13C, 0)
    img.u32(at + 0x140, 0)
    return at


def small_matrix(rng):
    return struct.pack("<9h", *[rng.choice([0x1000, -0x1000, 0, rng.randrange(-0x1000, 0x1000)]) for _ in range(9)])


# ---- the channel decoder -------------------------------------------------------------------------------------------------------------------------------------------------------------------

@an(0x004305F0, "Animation_DecompressStream")
def gen_decompress(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng, 0, 0x400)
    # the head byte is mostly a legal combination (any of the sixteen kinds, any division count), the rest noise
    img.u8(env.slab + STREAM, rng.randrange(0, 256))
    stride = rng.choice([1, 2, 3, 6, 12])
    count = rng.choice([1, 2, 3, 5, 8, 13, 20, rng.randrange(1, 24)])
    img.put(env.slab + OUT, noise(rng, 0x800))
    return [env.slab + STREAM, env.slab + OUT, stride, count], img.pokes()


# ---- the pose ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def pose_scene(img, env, rng, force=None):
    """A model with a compressed animation and everything the decompressor needs; returns the model."""
    count = rng.choice([1, 2, 3, 4, 6, 8])
    region = rng.randrange(0, 21)
    anim_count = rng.choice([1, 2])
    frames = [rng.choice([1, 2, 3, 6, 11, 20]) for _ in range(anim_count)]
    anim = rng.randrange(0, anim_count)
    frame = rng.randrange(0, frames[anim])
    put_region(img, env, rng, region, count, 0x2C)
    if rng.random() < 0.03:
        img.u32(env.slab + RAW + 8, count + 1)            # (the model table and the raw region disagree: an assertion)
    parent = random_tree(rng, count)
    for k, p in enumerate(parent):
        img.u16(env.slab + HIERARCHY + 2 * k, p)
    put_animations(img, env, rng, anim_count, frames, 0x2C)
    put_stream(img, env, rng, 0, 0x1900)
    at = put_model(img, env, rng, 0, region, anim, frame)
    if rng.random() < 0.5:
        # an already prepared model: the pose data and the order exist (a second call for the same animation, or another frame)
        order = []
        root = parent.index(next(k for k, p in enumerate(parent) if p == k))
        pending = [root]
        seen = {root}
        while pending:
            cur = pending.pop(0)
            order.append(cur)
            for k, p in enumerate(parent):
                if p == cur and k != root and k not in seen:
                    seen.add(k)
                    pending.append(k)
        img.u32(at + 0xEC, env.slab + OUT)
        img.u32(at + 0xE8, env.slab + OUT + 0xA00)
        img.put(env.slab + OUT, noise(rng, 0xA00))
        img.put(env.slab + OUT + 0xA00, b"".join(struct.pack("<H", b) for b in order))
        img.u16(at + 0xF0, root)
    return at, count


@an(0x00430920, "Animation_GetDecompressedPose")
def gen_pose(env, n):
    img, rng = Image(), env.rng
    mode = rng.random()
    if mode < 0.75:
        at, count = pose_scene(img, env, rng)
    elif mode < 0.88:
        # the uncompressed form: a pointer into the animation, or the blend of key frames
        count = rng.choice([1, 2, 4])
        region = rng.randrange(0, 21)
        anim_count = rng.choice([1, 2])
        frames = [rng.choice([3, 8, 12]) for _ in range(anim_count)]
        anim = rng.randrange(0, anim_count)
        put_region(img, env, rng, region, count, 0x2A)
        put_animations(img, env, rng, anim_count, frames, 0x2A, segments=rng.choice([0, 0, 0, 2, 3]))
        put_stream(img, env, rng, 0, 0x1900)
        img.put(env.slab + HIERARCHY, noise(rng, 0x40))
        at = put_model(img, env, rng, 0, region, anim, rng.randrange(0, frames[anim]), loop=rng.choice([0, 1]))
        img.u8(at + 4, rng.choice([0, 0, 4]))
        img.u32(at + 0x138, env.slab + OUT)
        img.put(env.slab + OUT, noise(rng, 0x400))
    else:
        # an animation of another kind (the assertion that nothing else is supported)
        at, count = pose_scene(img, env, rng)
        img.u32(env.slab + KIND, rng.choice([0, 0x2B, 0x2D, 0x2A]))
    return [at], img.pokes()


# ---- the model list --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@an_this(0x00431190, "ModelList_Construct", MODEL)
def gen_list_construct(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + MODEL, noise(rng, 0x40))
    return [], img.pokes()


def list_scene(img, env, rng):
    at = env.slab + MODEL
    img.put(at, noise(rng, 0x40))
    count = rng.choice([0, 1, 2, 5, 12])
    for k in range(12):
        child = env.slab + MODEL2 + 0x20 * k if rng.random() < 0.7 else 0
        img.u32(at + 4 + 4 * k, child)
        if child:
            img.put(child, noise(rng, 0x20))
            img.u32(child, env.slab + VTABLE)
    img.u32(at + 0x34, count if rng.random() < 0.95 else rng.choice([-1 & 0xFFFFFFFF, 13]))
    return at


@an_this(0x004311B0, "ModelList_ScalarDelete", MODEL)
def gen_list_scalar_delete(env, n):
    img, rng = Image(), env.rng
    list_scene(img, env, rng)
    return [rng.choice([0, 1, 1, 3])], img.pokes()


@an_this(0x004311D0, "ModelList_Destruct", MODEL)
def gen_list_destruct(env, n):
    img, rng = Image(), env.rng
    list_scene(img, env, rng)
    return [], img.pokes()


# ---- the pose side of the M3D renderer -------------------------------------------------------------------------------------------------------------------------------------------------------

def hooks_scene(img, env, rng, count, with_pose=True):
    """The hook table: entries of eight bytes (a vector of three shorts, the bone at +6)."""
    for k in range(8):
        img.put(env.slab + HOOKS + 8 * k, noise(rng, 6) + struct.pack("<H", rng.randrange(0, count)))
    img.put(env.slab + HOOK, noise(rng, 6) + struct.pack("<H", rng.randrange(0, count)))


def view_scene(img, env, rng, kind=None):
    """A model with an uncompressed pose or key frames or links, for the hook / part positions and the blends."""
    count = rng.choice([1, 2, 3, 5])
    region = rng.randrange(0, 21)
    anim_count = rng.choice([1, 2])
    frames = [rng.choice([6, 8, 12]) for _ in range(anim_count)]
    anim = rng.randrange(0, anim_count)
    kind = kind or rng.choice([0x2A, 0x2A, 0x2C])
    put_region(img, env, rng, region, count, kind)
    put_animations(img, env, rng, anim_count, frames, kind, segments=rng.choice([0, 0, 2, 3, 4]))
    put_stream(img, env, rng, 0, 0x1900)
    if kind == 0x2C:
        for k, p in enumerate(random_tree(rng, count)):
            img.u16(env.slab + HIERARCHY + 2 * k, p)
    at = put_model(img, env, rng, 0, region, anim, rng.randrange(0, frames[anim]), loop=rng.choice([0, 1]))
    if rng.random() < 0.3:
        img.u8(at + 4, 4)
        img.u32(at + 0x138, env.slab + OUT)
        img.put(env.slab + OUT, noise(rng, 0x400))
    img.u8(at + 0xD8, rng.choice([0, 0, 2, 3]))
    img.put(at + 0x118, small_matrix(rng) + noise(rng, 0x20))
    img.u32(at + 8, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    img.u32(at + 0xC, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    img.u32(at + 0x10, rng.randrange(-0x40000, 0x40000) & 0xFFFFFFFF)
    hooks_scene(img, env, rng, count)
    return at, count


@an(0x00464B90, "M3D_ReadHooksPacket")
def gen_read_hooks(env, n):
    img, rng = Image(), env.rng
    at = put_model(img, env, rng, 0, rng.randrange(0, 21), 0, 0)
    return [at, env.slab + PACKET], img.pokes()


@an(0x00464BB0, "M3D_ClearPoseStuff")
def gen_clear_pose(env, n):
    img, rng = Image(), env.rng
    at = put_model(img, env, rng, 0, 0, 0, 0)
    img.u32(at + 0x138, rng.choice([0, env.slab + ARENA + 0x100]))
    img.u32(at + 0x13C, rng.choice([0, env.slab + ARENA + 0x200]))
    img.u32(at + 0x140, word(rng))
    return [at], img.pokes()


@an(0x00464C00, "M3D_ReadLinksPacket")
def gen_read_links(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 4, 7, 12])
    region = rng.randrange(0, 21)
    img.u16(REGION_RECORDS + REGION_STRIDE * region + 0x38, rng.choice([1, 3, 8, 12]))
    img.u16(env.slab + PACKET + 2, count)
    names = [rng.randrange(0, 6) for _ in range(count)]
    for k in range(count):
        record = env.slab + PACKET + 4 + 12 * k
        img.put(record, noise(rng, 12))
        img.u16(record, rng.randrange(0, 6))
        img.u16(record + 2, rng.choice(names + [rng.randrange(0, 6)]))
    at = put_model(img, env, rng, 0, region, 0, 0)
    return [at, env.slab + PACKET], img.pokes()


@an(0x00464D10, "M3D_GetPartPosition")
def gen_part_position(env, n):
    img, rng = Image(), env.rng
    at, count = view_scene(img, env, rng)
    img.put(env.slab + OUT + 0xE00, noise(rng, 0x20))
    return [env.slab + OUT + 0xE00, at, rng.randrange(0, count)], img.pokes()


@an(0x00464E90, "M3D_GetHookPosition")
def gen_hook_position(env, n):
    img, rng = Image(), env.rng
    at, count = view_scene(img, env, rng)
    img.put(env.slab + OUT + 0xE00, noise(rng, 0x20))
    return [env.slab + OUT + 0xE00, at, env.slab + HOOK], img.pokes()


@an(0x00465060, "M3D_InterpolateAnimationPose")
def gen_interpolate(env, n):
    img, rng = Image(), env.rng
    at, count = view_scene(img, env, rng, 0x2A)
    return [at], img.pokes()


@an(0x00465110, "M3D_TweenAnimationBuffer")
def gen_tween(env, n):
    img, rng = Image(), env.rng
    at, count = view_scene(img, env, rng, 0x2A)
    first = rng.randrange(0, count)
    segments = rng.choice([2, 3, 4, 5])
    img.put(POSE_BUFFER, noise(rng, 0x2D8))
    img.put(env.slab + STREAM, noise(rng, 0x1900))
    return [count * 4, segments, env.slab + ANIMS, at, first, count], img.pokes()


@an(0x00465300, "M3D_BuildPose")
def gen_build_pose(env, n):
    img, rng = Image(), env.rng
    at, count = pose_scene(img, env, rng)
    img.u8(at + 4, 4 if rng.random() < 0.95 else rng.choice([0, 1]))
    pose = env.slab + OUT + 0x1000 - 0x200
    local = env.slab + PACKET + 0x100
    links = env.slab + PACKET + 4
    link_count = rng.choice([0, 1, 2, 3, 5, 6])
    img.put(pose, noise(rng, count * 24))
    img.u32(at + 0x138, pose if rng.random() < 0.97 else 0)
    img.u32(at + 0x13C, local if rng.random() < 0.97 else 0)
    img.u32(at + 0x140, links)
    img.u16(links - 2, link_count)
    depth = []
    for k in range(link_count):
        # a parent among the earlier links whose own chain is short (the matrix stack holds five entries)
        parent = rng.choice([p for p in range(k) if depth[p] < 3] + [0xFFFF]) if k else 0xFFFF
        depth.append(0 if parent == 0xFFFF else depth[parent] + 1)
        record = links + 12 * k
        img.put(record, noise(rng, 12))
        img.u16(record, rng.randrange(0, count))
        img.u16(record + 0xA, parent)
        angles = noise(rng, 12) if rng.random() < 0.7 else bytes(12)
        img.put(local + 12 * k, angles)
    return [at], img.pokes()


@an(0x00465830, "M3D_SegmentDistance")
def gen_segment_distance(env, n):
    img, rng = Image(), env.rng
    base = env.slab + OUT
    span = rng.choice([0x2000, 0x10000, 0x40000, 0x100000])
    points = []
    for k in range(4):
        point = [rng.randrange(-span, span) for _ in range(3)]
        points.append(point)
    if rng.random() < 0.15:
        points[1] = list(points[0])                       # (a segment of no length)
    if rng.random() < 0.1:
        points[3] = [points[2][0] + rng.randrange(-8, 8), points[2][1], points[2][2]]
    for k, point in enumerate(points):
        img.put(base + 0x10 * k, struct.pack("<3i", *point))
    img.put(base + 0x40, noise(rng, 0x30))
    return [base, base + 0x10, base + 0x20, base + 0x30, base + 0x40, base + 0x60, base + 0x50, base + 0x64], img.pokes()
