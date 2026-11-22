"""Phase 4.15c probe: the object base class of the world objects (group wob): the constructors and destructors, the flash, the sprite object, the trigger-record readers, the model-part
order of the skater and the animation pose update (0x0047fe30..0x00481150).

A fake object (0x260 bytes, all of it captured) stands for any world object; two fake sprite objects (vtable 0x005183fc, whose slot 0 is stubbed), a record of fixed-point words, a pool of
strings and two arrays of string pointers sit in the slab. The record helper, the allocators and the sprite object's methods are services.

Slab layout (offsets): object 0x000, sprite A 0x280, sprite B 0x300, record 0x380, strings 0x3a0 (8 of 0x20), array A 0x4a0, array B 0x540.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("wob",)

OBJ = 0x000
OBJ_BYTES = 0x260
SPRITE_A = 0x280
SPRITE_B = 0x300
RECORD = 0x380
STRINGS = 0x3A0
ARRAY_A = 0x4A0
ARRAY_B = 0x540
SLAB = 0x600

SPRITE_FLAG = 0x005244CC
SPRITE_VTABLE = 0x005183FC
SPRITE_DELETE = 0x004045E0
SCRATCH = 0x7FF00800 + 0xC00     # (worldkit::kScratch: where the port builds the records the retail code keeps in locals)
GLOBALS = [(SPRITE_FLAG, 4)]


def wob_regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = wob_regions
    READ_ONLY[_group] = [(SPRITE_VTABLE, 8)]


def wob(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wob", this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_wob_installed", False):
        return
    fc._wob_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    ensure(fc, 0x0047FDB0, 0, "engine_0047fdb0", convention="thiscall", this=True)       # PSX_InitObjectRecord
    ensure(fc, 0x0047FE20, 0, "engine_0047fe20", convention="thiscall", this=True)       # PSX_DestroyObjectRecord
    ensure(fc, 0x0047FD60, 1, "engine_0047fd60")                                          # Object_Free
    ensure(fc, 0x004B3230, 1, "engine_004b3230", result=lambda a: rng.getrandbits(8))     # PSX_FindLoadedRegionByName
    ensure(fc, 0x00464C00, 2, "engine_00464c00")                                          # M3D_ReadLinksPacket
    ensure(fc, 0x00465060, 1, "engine_00465060")                                          # M3D_InterpolateAnimationPose
    ensure(fc, 0x00461BF0, 1, "engine_00461bf0")                                          # M3d_BuildTransform
    ensure(fc, 0x00465300, 1, "engine_00465300")                                          # M3D_BuildPose
    ensure(fc, 0x00403D30, 1, "engine_00403d30", result=lambda a: env.slab + SPRITE_B)   # the display's allocator
    ensure(fc, 0x00404590, 0, "engine_00404590", convention="thiscall", this=True, result=lambda a: env.slab + SPRITE_B)   # the sprite's constructor answers the block
    ensure(fc, 0x004046F0, 2, "engine_004046f0", convention="thiscall", this=True)
    ensure(fc, 0x00404810, 1, "engine_00404810", convention="thiscall", this=True)
    ensure(fc, 0x00404840, 0, "engine_00404840", convention="thiscall", this=True)
    ensure(fc, 0x004048C0, 5, "engine_004048c0", convention="thiscall", this=True, stack_to={1: SCRATCH}, blob=(1, 6))
    ensure(fc, 0x00404860, 1, "engine_00404860", convention="thiscall", this=True)
    ensure(fc, SPRITE_DELETE, 1, "engine_004045e0", convention="thiscall", this=True)    # the sprite's deleting destructor (slot 0 of its vtable)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def fill_object(img, env, rng, high=0x180):
    """Random bytes over the fields of the object a function reads or writes (a state of random words everywhere would only make a poke slower)."""
    img.put(env.slab + OBJ, noise(rng, high))


def sprite_pointer(env, rng):
    """The object's sprite: none, or the fake sprite A (whose vtable is the retail sprite class's)."""
    return rng.choice([0, 0, env.slab + SPRITE_A])


def fake_sprite(img, env, rng):
    img.put(env.slab + SPRITE_A, noise(rng, 0x70))
    img.u32(env.slab + SPRITE_A, SPRITE_VTABLE)
    img.put(env.slab + SPRITE_B, noise(rng, 0x70))


# ---- the object base class --------------------------------------------------------------------------------------------------------------------------------------------------------

@wob(0x0047FE30, "Object_SelectNamedPsxRegion", OBJ)
def gen_select_region(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x40)
    return [rng.choice([0, env.slab + STRINGS, env.slab + STRINGS + 0x20, rng.getrandbits(32) | 1])], img.pokes()


@wob(0x0047FE90, "CItem__Flash", OBJ)
def gen_flash(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    if rng.random() < 0.5:
        img.u32(env.slab + OBJ + 0x38, rng.choice([0, 0x01000000, 0x02000000, 0x03000000, rng.getrandbits(32)]))
    frames = rng.choice([0, 0, 1, 2, 3, 5, 10, 30, -1, -2, -5, -10, -30, -0x7FFF, 0x7FFF, rng.randrange(1, 200), -rng.randrange(1, 200), -0x20001])
    channels = [rng.choice([0, 1, 0x40, 0x7F, 0x80, 0xFF, rng.getrandbits(8), rng.getrandbits(32)]) for _ in range(3)]
    if frames < 0 and (-frames) & 0xFFFF == 0:
        frames = -1
    return [frames & 0xFFFFFFFF, *channels], img.pokes()


@wob(0x0047FFC0, "CItem__FlashUpdate", OBJ)
def gen_flash_update(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    img.u16(env.slab + OBJ + 0x44, rng.choice([0, 1, 1, 2, 3, 10, 0xFFFF, rng.getrandbits(16)]))
    img.u32(env.slab + OBJ + 0x38, rng.choice([0, 0x01000000, 0x02000000, 0x03000000, rng.getrandbits(32)]))
    return [], img.pokes()


@wob(0x00480060, "Object_Construct", OBJ)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x100)
    return [], img.pokes()


@wob(0x00480140, "Object_ScalarDelete", OBJ)
def gen_scalar_delete(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x100)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    return [rng.choice([0, 1, 2, 3, 0x100, word(rng)])], img.pokes()


@wob(0x00480160, "Object_Destruct", OBJ)
def gen_destruct(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x100)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    return [], img.pokes()


@wob(0x00480720, "Object_DestructThunk", OBJ)
def gen_destruct_thunk(env, n):
    return gen_destruct(env, n)


@wob(0x004801C0, "Object_RecordDestructThunk", OBJ)
def gen_record_thunk(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x40)
    return [], img.pokes()


@wob(0x00480240, "TRG_ReadFixedPosition", OBJ)
def gen_read_fixed(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x40)
    img.put(env.slab + RECORD, noise(rng, 0x10))
    return [env.slab + RECORD + rng.choice([0, 0, 0, 0, 4, 1, 2, 3])], img.pokes()


@wob(0x004802C0, "TRG_ReadObjectParameters", OBJ)
def gen_read_parameters(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x40)
    img.put(env.slab + RECORD, noise(rng, 0x10))
    return [env.slab + RECORD + rng.choice([0, 0, 2, 4])], img.pokes()


@wob(0x004803E0, "Object_SetStateBit", OBJ)
def gen_set_state_bit(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x90)
    img.u8(env.slab + OBJ + 0x86, rng.choice([0, 0, 1, 2, 0x80, 0xFF, rng.getrandbits(8)]))
    return [rng.choice([1, 1, 0, 2, 0xFFFFFFFF, rng.getrandbits(32)]), rng.choice([0, 1, 2, 4, 0x80, 0xFF, rng.getrandbits(8), rng.getrandbits(32)])], img.pokes()


@wob(0x00480420, "Object_WantSprite", OBJ)
def gen_want_sprite(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    return [], img.pokes()


@wob(0x00480430, "Object_ReleaseSprite", OBJ)
def gen_release_sprite(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0xB0)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    return [], img.pokes()


@wob(0x00480460, "Object_SpriteUpdate", OBJ)
def gen_sprite_update(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0xB8)
    fake_sprite(img, env, rng)
    img.u32(SPRITE_FLAG, word(rng))
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    state = img.b.get(env.slab + OBJ + 0x6A, 0)
    img.u8(env.slab + OBJ + 0x6A, (state & 0xF7) | (8 if rng.random() < 0.8 else 0))
    # the scale is (whole - part) * 128 / whole / 6: whole must not be zero
    img.u16(env.slab + OBJ + 0xAC, rng.choice([1, 0x12C, 0x7FFF, 0x100, rng.randrange(1, 0x8000), rng.randrange(0x8000, 0x10000), 5, 6, 60]))
    img.u16(env.slab + OBJ + 0xAA, rng.choice([0, 1, 0x12C, 5, 0x8000, rng.getrandbits(16)]))
    img.u16(env.slab + OBJ + 0x9E, rng.choice([0xFF80, 0xFF7F, 0xFF00, 0x8000, 0, 0x7FFF, rng.getrandbits(16)]))
    return [], img.pokes()


@wob(0x004805D0, "Object_MarkDyingIfIdle", OBJ)
def gen_mark_dying(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    return [], img.pokes()


@wob(0x004805F0, "Object_IsDying", OBJ)
def gen_is_dying(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    return [], img.pokes()


@wob(0x00480600, "Object_AlwaysTrue")
def gen_always_true(env, n):
    return [word(env.rng), word(env.rng), word(env.rng)], Image().pokes()


@wob(0x00480610, "Animated_Construct", OBJ)
def gen_animated_construct(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x180)
    return [], img.pokes()


def animated_state(img, env, rng):
    fill_object(img, env, rng, 0x180)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    for offset in (0x138, 0x13C, 0xEC, 0xE8):
        img.u32(env.slab + OBJ + offset, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))


@wob(0x00480660, "Animated_ScalarDelete", OBJ)
def gen_animated_scalar_delete(env, n):
    img, rng = Image(), env.rng
    animated_state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, 0x100, word(rng)])], img.pokes()


@wob(0x00480680, "Animated_Destruct", OBJ)
def gen_animated_destruct(env, n):
    img, rng = Image(), env.rng
    animated_state(img, env, rng)
    return [], img.pokes()


@wob(0x00480C30, "CSuper_FrameReached", OBJ)
def gen_frame_reached(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x120)
    track = rng.randrange(0, 0x10000)
    first = rng.choice([0, 1, 5, 10, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)])
    last = rng.choice([0, 1, 5, 10, 20, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)])
    img.u16(env.slab + OBJ + 0x110, track)
    img.u16(env.slab + OBJ + 0x10C, first)
    img.u16(env.slab + OBJ + 0x10E, last)
    img.u8(env.slab + OBJ + 0x112, rng.choice([0, 1, 0xFF, 0x80, 0x7F, rng.getrandbits(8)]))
    frame = rng.choice([first, last, (first + last) // 2, first + 1, last + 1, first - 1, last - 1, 0, -1, 0x8000, rng.randrange(-40, 40)]) & 0xFFFFFFFF
    if rng.random() < 0.7:
        frame = (struct.unpack("<h", struct.pack("<H", rng.choice([first, last])))[0] + rng.choice([-1, 0, 0, 1])) & 0xFFFFFFFF
    return [track if rng.random() < 0.8 else rng.getrandbits(32), frame], img.pokes()


@wob(0x00480D90, "Skater_MergeModelParts", OBJ)
def gen_merge_parts(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x160)
    names = ["head", "body", "arm", "leg", "board", "Head", "hand", "foot"]
    for k, text in enumerate(names):
        img.put(env.slab + STRINGS + 0x20 * k, text.encode() + b"\0")
    order_count = rng.choice([0, 1, 2, 3, 5, 8, 0x13, 8, 8])
    part_count = rng.choice([0, 1, 2, 3, 5, 8, 0x13, 8, 8, 20])
    for k in range(order_count):
        img.u32(env.slab + ARRAY_A + 4 * k, env.slab + STRINGS + 0x20 * rng.randrange(0, 8))
    for k in range(part_count):
        img.u32(env.slab + ARRAY_B + 4 * k, env.slab + STRINGS + 0x20 * rng.randrange(0, 8))
    # (a null array is only walked when its count is zero: retail would read address 0)
    order_names = 0 if order_count == 0 and rng.random() < 0.5 else env.slab + ARRAY_A
    part_names = 0 if part_count == 0 and rng.random() < 0.5 else env.slab + ARRAY_B
    return [order_names, order_count, part_names, part_count], img.pokes()


@wob(0x00480F50, "Animation_UpdatePoseFrame", OBJ)
def gen_update_pose(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x150)
    img.u32(env.slab + OBJ + 0x13C, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))
    return [word(rng)], img.pokes()
