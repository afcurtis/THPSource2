"""Phase 4.15b probe: the replay recorder's session and frame functions (video.cpp; groups vrs, vrw, vra).

  vrs  the header, the stream buffers, starting a recording or a playback, the animation registration
  vrw  Replay_WriteFrame (and its checked wrapper, and the matrix-to-angles step)
  vra  Replay_ApplySkaterFrame, advancing frames and highlights, Video_Playback, the stepping buttons
  vrc  Camera_SelectNearestTriggerPoint 0x00411fc0 (12 s9): the replay camera trigger; its five callees (line of sight, node decode, the two distances, Rnd) are stubs

Two fake skaters (only the fields the code reads are captured) stand for Player[0] and Player[1]; the playback state blocks and the globals around them are captured whole; the streams live in the
slab. The outside services (the object lists, the collision queries, the sound, the board, the trigger system, the allocator) are stubs; the sound queue, the bit stream and the highlight code are
real retail code. Slab layout: see the constants.
"""

from __future__ import annotations

import os

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from video_specs import (CAR_ANGLES, CARS, END_FRAME, END_Y, MUTED, NUM_FRAMES, OPEN, PLAYER, QUEUE, QUEUE_READ, QUEUE_WRITE, RANGES, SPLAT_OTHER, SPLAT_PLAYER, START_FRAME, START_Y, STATE0,
                         STATE1, TRICK_OTHER, TRICK_PLAYER, WORLD, WORLD_BYTES, short, word)

GROUPS = ("vrs", "vrw", "vra", "vrc")

HEADER = 0x0000            # 0x400 bytes: where a header is written / read from
SKATER_A = 0x0400          # 0x3200 bytes each (only windows are captured)
SKATER_B = 0x3800
ITEMS = 0x6C00             # 0x30 bytes each, eight enviro items
NODES = 0x6D80             # eight node records of 0x10 bytes (OffsetList points at them)
OFFSETS = 0x6E00           # eight node pointers
LINKS = 0x6E40             # four link tables of 0x20 bytes
TRICKS = 0x6EC0            # eight trick-object records of 0x20 bytes, a ring through +0x10
FAKE = 0x6FC0              # a fake object (vtable at +0) the line-to-sphere query "hits"
FAKE_VTABLE = 0x6FE0       # its vtable (slot 2 is an unreached retail function)
ARENA = 0x7000             # (the allocator stub is replaced: it hands out the streams)
ARENA_SIZE = 0x400
BIG = 0x9000               # two 0x80000-byte stream buffers overlap here: a recording clears the whole buffer of its skater
STREAM_A = BIG
STREAM_B = BIG + 0x40000
SLAB = BIG + 0xC0000

SKATER_WINDOWS = [(0x08, 0x10), (0x4C, 0x0C), (0x90, 0x14), (0xBC, 0x0C), (0xD8, 4), (0xF4, 4), (0x2C88, 0x10), (0x2CC0, 8), (0x2E58, 0x20), (0x3080, 4), (0x30B8, 4)]

GLOBALS = [(0x00533F38, 8), (0x00533F3C, 4), (0x0053A514, 0x5C0), (0x00561990, 4), (0x00561BFC, 4), (0x00561C04, 4), (0x005244CC, 4), (0x0055F6BC, 4), (0x0055F72C, 4), (0x0055FB36, 8),
           (0x00568608, 8), (0x00568A24, 4), (0x0056A488, 0x200), (0x0056A838, 4), (0x0056A858, 8), (0x0056A898, 4), (0x0056A8D0, 4), (0x0056A8E0, 4), (0x0056A90C, 4), (0x0056A954, 4),
           (0x0056AF40, 4), (0x0056AFF0, 0x300), (0x0056B830, 4), (0x0056B8D4, 0x28), (0x0056DB78, 4), (0x0056DB88, 4), (0x0056DB90, 4), (0x0056E210, 4), (0x0056E320, 4), (WORLD, WORLD_BYTES),
           (0x006A3E00, 0x120)]
STATIC = [(0x005481AC, 0x8C)]

ENGINE = None


def vr_regions(slab):
    windows = [(slab + HEADER, 0x400), (slab + STREAM_A, 0x800), (slab + STREAM_B, 0x800), (slab + ITEMS, 0x180), (slab + NODES, 0x80), (slab + OFFSETS, 0x40), (slab + LINKS, 0x80),
               (slab + TRICKS, 0x100), (slab + FAKE, 0x40), (slab + ARENA, ARENA_SIZE)]
    for base in (SKATER_A, SKATER_B):
        windows += [(slab + base + off, size) for off, size in SKATER_WINDOWS]
    windows += GLOBALS
    return merged(windows, gap=0)


def vr_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = vr_regions
    READ_ONLY[_group] = STATIC
    SEEDS[_group] = vr_seed


def vrc(address, name):
    return spec(address, name, "vrc")


def vrs(address, name):
    return spec(address, name, "vrs")


def vrw(address, name):
    return spec(address, name, "vrw")


def vra(address, name):
    return spec(address, name, "vra")


# ---- the stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

STATE = {"slab": 0, "rng": None, "clock": 0, "queries": [], "ticks": 0}


def install_stubs(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_vr_installed", False):
        return
    fc._vr_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    STATE["slab"] = env.slab
    STATE["rng"] = rng
    slab = env.slab

    def costume(a):
        return rng.choice([0, 1, 2, 3, 7, rng.getrandbits(8)])

    def custom_slot(a):
        return rng.choice([0, 1, 2, 3])

    def angles(a):
        return b"".join((rng.choice([0, 0x7FF, 0x800, 0xFFF, rng.randrange(0, 0x1000), rng.randrange(0, 0x1000), rng.getrandbits(16)])).to_bytes(2, "little") for _ in range(3))

    def sleep_tick(a):
        STATE["ticks"] += 1
        from opentony.memory import mem

        return (mem.u32(0x0056E320) + rng.choice([1, 3, 7, 50])) & 0xFFFFFFFF

    handed = {"n": 0}

    def stream_buffer(a):
        handed["n"] += 1
        if rng.random() < 0.06:
            return 0
        return slab + (STREAM_A if handed["n"] % 2 else STREAM_B)

    ensure(fc, 0x0046F490, 4, "alloc", result=stream_buffer)
    ensure(fc, 0x004B0CD0, 1, "engine_004b0cd0", convention="thiscall", this=True, result=costume)
    ensure(fc, 0x004B1810, 1, "engine_004b1810", result=custom_slot)
    ensure(fc, 0x004657C0, 2, "engine_004657c0", pokes=[(0, None, angles)])
    ensure(fc, 0x0047B7D0, 1, "engine_0047b7d0", stack_to=pcplatform_scratch())
    ensure(fc, 0x0047B400, 3, "engine_0047b400", stack_to=pcplatform_scratch(), result=lambda a: rng.choice([1, 1, 1, 0]))
    ensure(fc, 0x00452DE0, 0, "engine_00452de0", result=lambda a: rng.choice([1, 1, 1, 0]))
    ensure(fc, 0x00492B60, 0, "engine_00492b60")
    ensure(fc, 0x00492C70, 1, "engine_00492c70", result=lambda a: rng.choice([0, 1, 1]))
    ensure(fc, 0x00492BA0, 1, "engine_00492ba0")
    ensure(fc, 0x0046C630, 0, "engine_0046c630", convention="thiscall", this=True)
    ensure(fc, 0x0046C5F0, 0, "engine_0046c5f0", convention="thiscall", this=True)
    ensure(fc, 0x0049C7D0, 0, "engine_0049c7d0", convention="thiscall", this=True)
    ensure(fc, 0x00461BF0, 1, "engine_00461bf0")
    ensure(fc, 0x004F6550, 1, "engine_004f6550", writes=[(0x0056E320, 4, sleep_tick)])
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")
    ensure(fc, 0x0046DD20, 2, "engine_0046dd20", convention="thiscall", this=True)
    ensure(fc, 0x004B1EF0, 1, "engine_004b1ef0", result=lambda a: slab + ITEMS + 0x30 * rng.randrange(0, 8))
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: slab + LINKS + 0x20 * rng.randrange(0, 4))
    ensure(fc, 0x004C58B0, 1, "engine_004c58b0")
    ensure(fc, 0x004BDC40, 1, "engine_004bdc40")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=pcplatform_scratch(0x100))
    ensure(fc, 0x004C4B80, 1, "engine_004c4b80")
    ensure(fc, 0x004660B0, 2, "engine_004660b0", stack_to=pcplatform_scratch(0x100))
    ensure(fc, 0x004C4BA0, 1, "engine_004c4ba0")
    ensure(fc, 0x00464200, 6, "engine_00464200", stack_to={0: pcplatform_scratch(0x200), 1: pcplatform_scratch(0x210), 2: pcplatform_scratch(0x220)},
           result=lambda a: (slab + FAKE) if rng.random() < 0.3 else 0)
    ensure(fc, 0x004AD9B0, 0, "engine_004ad9b0", convention="thiscall", this=True)
    ensure(fc, 0x00480FD0, 1, "engine_00480fd0")
    ensure(fc, 0x00481110, 1, "engine_00481110")
    ensure(fc, 0x00480FF0, 1, "engine_00480ff0")
    ensure(fc, 0x00406070, 1, "engine_00406070", stack_to=pcplatform_scratch(0x240))
    ensure(fc, 0x00406090, 1, "engine_00406090", stack_to=pcplatform_scratch(0x250))
    ensure(fc, 0x00403D30, 1, "engine_00403d30", result=lambda a: (slab + FAKE) if rng.random() < 0.6 else 0)
    ensure(fc, 0x0040B060, 5, "engine_0040b060", convention="thiscall", this=True, stack_to=pcplatform_scratch(0x260))
    ensure(fc, 0x0046D7A0, 0, "engine_0046d7a0", convention="thiscall", this=True)
    ensure(fc, 0x0046D040, 0, "engine_0046d040", convention="thiscall", this=True)
    ensure(fc, 0x004A12D0, 1, "engine_004a12d0")
    ensure(fc, 0x0046E080, 0, "engine_0046e080", convention="thiscall", this=True)
    ensure(fc, 0x00411FC0, 1, "engine_00411fc0")
    ensure(fc, 0x0044E9C0, 0, "engine_0044e9c0")


def pcplatform_scratch(offset=0):
    return 0x7FF00800 + 0xC00 + offset


CAM_A = 0x7400             # the camera objects of the two skaters (0x600 bytes each: the fields at +0x8, +0x3a4..+0x3e4, +0x504, +0x5b4)
CAM_B = 0x7A00
RECORDS = 0x8000           # the words after a camera point's position (TRG_DecodeNodePosition answers a pointer into them): 0x46 records of 4 bytes
VIEW = 0x8200              # what 0x00563a38 points at (a word at +0xc)
POINT_LIST = 0x0055FA58
POINT_COUNT = 0x0055FAE4
REGISTRY = 0x00524CB8


def vrc_regions(slab):
    windows = []
    for base, cam in ((SKATER_A, CAM_A), (SKATER_B, CAM_B)):
        windows += [(slab + base + 0x08, 0x0C), (slab + base + 0x310C, 0x0C), (slab + base + 0x29B0, 4)]
        windows += [(slab + cam + 0x08, 0x0C), (slab + cam + 0x3A4, 0x40), (slab + cam + 0x504, 4), (slab + cam + 0x5B4, 4)]
    windows += [(slab + RECORDS, 0x120), (slab + VIEW, 0x20)]
    windows += [(STATE0 + 0x28, 0x28), (STATE1 + 0x28, 0x28), (PLAYER, 4), (POINT_LIST, 0x90), (REGISTRY - 4, 0x120), (0x0056E320, 4), (0x00563A38, 8), (0x00563A40, 8),
                (0x00524A40, 0x0C), (0x00567CA4, 8)]
    return merged(windows, gap=0)


EXTRA_REGIONS["vrc"] = vrc_regions


def install_cam_stubs(fc, env):
    """The five services of the camera trigger. The positions it hands the line-of-sight and distance services are locals: the probe records the bytes they hold, not their addresses."""
    install_stubs(fc, env)
    if getattr(fc, "_vrc_installed", False):
        return
    fc._vrc_installed = True
    rng = env.rng
    slab = env.slab

    def node(a):
        return b"".join(rng.choice([0, 1, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.randrange(0, 0x400000)]).to_bytes(4, "little") for _ in range(3))

    def crap_distance(a):
        return rng.choice([0, 1, 0x7FF, 0xFA0, 0xFA1, 0x1000, 0x2327, 0x2328, 0x2329, 0x3000, rng.randrange(0, 0x3000), rng.randrange(0, 0x2400), rng.randrange(0, 0xFA0), 0xFFFFFFFF, 0x7FFFFFFF])

    def distance(a):
        return rng.choice([0, 0x1C1, 0x1C2, 0x1C3, 0x95F, 0x960, 0x961, rng.randrange(0, 0x1000), 0x7FFFFFFF, 0xFFFFFFFF, rng.randrange(-100, 0x2000) & 0xFFFFFFFF])

    ensure(fc, 0x004CA0C0, 2, "engine_004ca0c0", keep=[], blob=[(0, 12), (1, 12)], result=lambda a: rng.choice([0, 1, 1, 0, 5]))
    ensure(fc, 0x004C8650, 2, "engine_004c8650", keep=[1], fill=(0, node), result=lambda a: slab + RECORDS + 4 * rng.randrange(0, 0x46))
    ensure(fc, 0x004C9620, 2, "engine_004c9620", keep=[], blob=[(0, 12), (1, 12)], result=crap_distance)
    ensure(fc, 0x004C9590, 2, "engine_004c9590", result=distance)
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.randrange(0, 5))


GROUP_STUBS = {group: install_stubs for group in GROUPS}
GROUP_STUBS["vrc"] = install_cam_stubs


# ---- the world --------------------------------------------------------------------------------------------------------------------------------------------------------------

def skater_address(env, base):
    return env.slab + base


def skaters(img, env, rng, second=0.7):
    img.u32(PLAYER, env.slab + SKATER_A)
    img.u32(PLAYER + 4, env.slab + SKATER_B if rng.random() < second else 0)


def fake_skater(img, env, rng, base, frame_state=True):
    sk = env.slab + base
    for k in range(3):
        img.u32(sk + 0x08 + 4 * k, rng.choice([0, 0x1000, -0x1000 & 0xFFFFFFFF, rng.randrange(-0x7FFFFFF, 0x7FFFFFF) & 0xFFFFFFFF, rng.randrange(-0x40000, 0x40000) * 0x1000 & 0xFFFFFFFF]))
        img.u32(sk + 0x4C + 4 * k, word(rng))
        img.u32(sk + 0xBC + 4 * k, word(rng))
    img.u32(sk + 0x90, word(rng))
    img.u32(sk + 0x94, word(rng))
    img.u32(sk + 0x98, word(rng))
    img.u32(sk + 0xD8, word(rng))
    img.u16(sk + 0xF4, rng.choice([0, 1, 5, 0x15, 0x16, 0x19, 0x1A, 0x23, 0x24, 0x25, 0x26, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(0, 0x100)]))
    img.u16(sk + 0xF6, rng.choice(ANIMATIONS))
    img.u32(sk + 0x2C88, rng.choice([0, 0, 1, 1000, word(rng)]))
    img.u32(sk + 0x2C8C, rng.choice([0, 0x100, 0x1000, 0x7FFF, rng.randrange(-0x20000, 0x20000)]) & 0xFFFFFFFF)       # (bits_needed never ends for a delta of 0x40000000 or more)
    img.u32(sk + 0x2CC0, rng.choice([0, 1, 2, 3]))
    img.u32(sk + 0x2CC4, 0 if base == SKATER_A else 1)
    for k in range(16):
        img.u16(sk + 0x2E58 + 2 * k, short(rng) & 0xFFFF)
    img.u32(sk + 0x3080, rng.choice([0, 0x1000, word(rng)]))
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 3, 4, 5, 8, 15, 16, 17, word(rng)]))


ANIMATIONS = [0, 1, 0x28, 0x29, 0x2A, 0x2C, 0x2E, 0x36, 0x37, 0x39, 0x3A, 0x41, 0x42, 0x44, 0x47, 0x48, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0xB1, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xFF, 0x1FF]


def state_block(img, env, rng, block, stream, mode):
    """One playback state: a stream cursor inside the slab, a half used word, counters and the previous-frame values."""
    img.u32(block, mode)
    img.u32(block + 0x04, env.slab + stream)
    img.u32(block + 0x08, env.slab + stream + 0x200)
    img.u32(block + 0x0C, env.slab + stream + 0x400)
    img.u32(block + 0x14, env.slab + stream + 4 * rng.randrange(0, 0x60))
    img.u32(block + 0x18, rng.getrandbits(32))
    img.u32(block + 0x1C, rng.choice([0x20, 0x20, 1, 2, 5, 8, 16, 31, rng.randrange(1, 33)]))
    img.u32(block + 0x20, rng.choice([0, 1, 100, 1000, rng.randrange(0, 5000)]))
    img.u32(block + 0x24, rng.choice([0, 1, 2, 10, 100, rng.randrange(0, 1000)]))
    img.u32(block + 0x50, rng.choice([0, 1, 2, 3, 4, 5, 6, 0xFFFFFFFF]))
    for slot in range(5):
        record = block + 0x54 + 8 * slot
        img.u16(record, rng.randrange(0, 7))
        img.u16(record + 2, rng.choice([rng.randrange(0, 7), 0x7FFF, 0xFFFF]))
        img.u32(record + 4, rng.choice([0, 0, 5, 100, rng.randrange(0, 2000)]))
    # the previous-frame values
    positions = rng.choice([[0, 0, 0], [rng.randrange(-0x80000, 0x80000) for _ in range(3)], [rng.randrange(-0x400, 0x400) for _ in range(3)]])
    for k in range(3):
        img.u32(block + 0x98 + 4 * k, positions[k] & 0xFFFFFFFF)
        img.u32(block + 0xA4 + 4 * k, rng.choice([0, positions[k], rng.randrange(-0x80000, 0x80000)]) & 0xFFFFFFFF)
    for k in range(4):
        img.u16(block + 0x88 + 2 * k, rng.choice([0, 0x7FF, 0x800, 0xFFF, rng.randrange(0, 0x1000), rng.randrange(0, 0x1000), rng.getrandbits(16)]))
        img.u16(block + 0x90 + 2 * k, rng.choice([0, 0x7FF, 0x800, 0xFFF, rng.randrange(0, 0x1000), rng.randrange(0, 0x1000), rng.getrandbits(16)]))
    img.u32(block + 0x7C, rng.choice([0, 1, 2, 4, 15, 16, word(rng)]))
    img.u32(block + 0x80, rng.choice([0, 1, 0x28, 0xFF, word(rng), rng.randrange(0, 0x100)]))
    img.u32(block + 0x84, rng.choice([0, 1, 5, 0x20, 0xFF, 0xFFFFFFFF, rng.randrange(0, 0x100), word(rng)]))
    for off in (0xB0, 0xB4):
        img.u32(block + off, rng.choice([0, 1, 0x40, 0xFFF, 0x1000, rng.randrange(0, 0x2000), rng.randrange(-0x20000, 0x20000) & 0xFFFFFFFF]))
    img.u32(block + 0xB8, rng.choice([0, 10, 1000, rng.randrange(0, 5000)]))
    img.u32(block + 0xBC, rng.choice([0, 10, 1000, rng.randrange(0, 5000)]))
    img.u32(block + 0xC0, rng.choice([0, 1, 0, 0]))
    for k in range(8):
        for j in range(3):
            img.u32(block + 0xC4 + 0x10 * k + 4 * j, word(rng))
            img.u16(block + 0x144 + 8 * k + 2 * j, short(rng) & 0xFFFF)


def put_stream(img, env, rng, stream, words=0x200):
    for k in range(words):
        img.u32(env.slab + stream + 4 * k, rng.getrandbits(32))


def world(img, env, rng):
    """The globals the frame code reads: the clock, the lists, the node tables, the trick-object ring."""
    img.u32(0x0056E320, rng.choice([0, 1000, 5000, rng.randrange(0, 100000), 0xFFFFFFF0]))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x00561BFC, rng.choice([0x14, 0x15, 0x16, 0, 1, 2, 5, 0x20]))
    img.u32(0x00561990, rng.choice([0, 0, 1]))
    img.u32(0x0056E770, rng.choice([0, 0, 1]))
    img.u32(0x0055F72C, rng.choice([0, 100, 0xF9, 0xFA, 0xFB, 500, 0xFFFFFFFF]))
    img.u32(0x0056B830, rng.choice([0, 0x1234, env.slab + FAKE]))
    img.u32(0x0055F6BC, env.slab + FAKE)
    img.u32(0x0056AF40, env.slab + FAKE + 8)
    img.u32(0x0056DB90, env.slab + TRICKS)
    img.u32(0x0056E210, env.slab + OFFSETS)
    for k in range(8):
        img.u32(env.slab + OFFSETS + 4 * k, env.slab + NODES + 0x10 * k)
        img.u16(env.slab + NODES + 0x10 * k, rng.choice([0xC, 0xE, 0xC, 1, 2, 5, 0x10, 0x20]))
    for t in range(4):
        base = env.slab + LINKS + 0x20 * t
        n = rng.choice([0, 1, 2, 3, 5, 7])
        img.u16(base, n)
        for j in range(7):
            img.u16(base + 2 + 2 * j, rng.randrange(0, 8))
    for k in range(8):
        base = env.slab + TRICKS + 0x20 * k
        img.u32(base + 4, word(rng))
        img.u32(base + 0x10, env.slab + TRICKS + 0x20 * ((k + 1) % 8))
        img.u16(base + 8, rng.randrange(0, 4))
        img.u8(base + 0xA, rng.choice([0, 1]))
        img.u8(base + 0xB, rng.choice([0, 1, 2]))
    for k in range(8):
        base = env.slab + ITEMS + 0x30 * k
        img.u8(base + 5, rng.getrandbits(8))
        img.u32(base + 0x24, word(rng))
    img.u32(env.slab + FAKE, env.slab + FAKE_VTABLE)
    for slot in range(3):
        img.u32(env.slab + FAKE_VTABLE + 4 * slot, 0x4AD9B0)
    img.u32(0x0056A954, rng.choice([0, 0x1234]))
    img.u32(0x0056DB78, word(rng))
    img.u32(0x0056DB88, rng.choice([0, 1]))
    img.u32(0x005244CC, rng.choice([0, 1]))
    img.u32(0x0056A8E0, word(rng))
    img.u32(0x0056A90C, word(rng))


# ---- vrs: the session ----------------------------------------------------------------------------------------------------------------------------------------------------

@vrs(0x004CB810, "Replay_WriteHeader")
def gen_write_header(env, n):
    img, rng = Image(), env.rng
    skaters(img, env, rng, second=0.8)
    for base in (SKATER_A, SKATER_B):
        fake_skater(img, env, rng, base)
    for block in (STATE0, STATE1):
        img.u32(block + 0x20, rng.choice([0, 1, 500, rng.randrange(0, 9000)]))
        for k in range(10):
            img.u32(block + 0x54 + 4 * k, word(rng))
    img.u32(0x00533F3C, rng.choice([1, 2]))
    img.u32(0x0056A898, rng.choice([0, 1, 12, 13, rng.randrange(0, 20)]))
    img.u32(0x00533F38, rng.choice([0, 1, 2, 7, rng.randrange(0, 12)]))
    img.u32(0x00568A24, word(rng))
    img.u32(0x0056A838, word(rng))
    img.u32(0x00568608, word(rng))
    img.u32(0x0056860C, word(rng))
    for k in range(8):
        img.u8(0x0055FB36 + k, rng.getrandbits(8))
    for entry in range(4):
        img.u8(0x0053A514 + 0x170 * entry, rng.choice([0, 2, 3, 1, 2, 0x82]))
    for k in range(0x80):
        img.u32(0x0056A488 + 4 * k, rng.getrandbits(32))
    return [env.slab + HEADER, rng.choice([0, 1, 0, 1, 2, 0xFFFFFFFF])], img.pokes()


@vrs(0x004CBA30, "Replay_AllocateStreamBuffers")
def gen_allocate(env, n):
    img, rng = Image(), env.rng
    img.u32(0x00533F3C, rng.choice([1, 2, 2, 3, 0]))
    for block in (STATE0, STATE1):
        for off in (4, 8, 0xC):
            img.u32(block + off, word(rng))
    return [], img.pokes()


@vrs(0x004CBCE0, "Replay_ConfigureStreamState")
def gen_configure(env, n):
    img, rng = Image(), env.rng
    skaters(img, env, rng, second=0.8)
    for base in (SKATER_A, SKATER_B):
        fake_skater(img, env, rng, base)
    world(img, env, rng)
    for block, stream in ((STATE0, STREAM_A), (STATE1, STREAM_B)):
        state_block(img, env, rng, block, stream, rng.choice([0, 1, 2]))
        img.u32(block + 0x08, env.slab + stream + 0x200)
    # a recording clears the whole 0x80000-byte buffer: the slab holds only the start of it, which is what the replay clears too
    put_stream(img, env, rng, STREAM_A, 0x80)
    put_stream(img, env, rng, STREAM_B, 0x80)
    for k in range(5):
        img.u32(0x0056E75C + 4 * k, word(rng))
    skater = rng.choice([env.slab + SKATER_A, env.slab + SKATER_A, env.slab + SKATER_B])      # (retail dereferences a null skater)
    mode = rng.choice([1, 1, 2, 2, 0, 3])
    first = rng.choice([0, 5, 10, 100])
    last = rng.choice([0x7FFFFFFF, 0x7FFFFFFF, first + rng.randrange(0, 200), first - 3])
    return [skater, mode, first & 0xFFFFFFFF, last & 0xFFFFFFFF], img.pokes()


@vrs(0x004CDC20, "Replay_StartPlayback")
def gen_start_playback(env, n):
    img, rng = Image(), env.rng
    world(img, env, rng)
    count = rng.choice([1, 1, 2, 2, 2, 3, 0, 0xFFFF])
    img.u32(0x0056B8D4, count | (rng.getrandbits(16) << 16))
    img.u32(0x00561990, rng.choice([0, 1]))
    img.u32(STATE0 + 4, env.slab + STREAM_A)
    img.u32(STATE1 + 4, env.slab + STREAM_B if count == 2 else rng.choice([env.slab + STREAM_B, 0]))
    put_stream(img, env, rng, STREAM_A, 0x60)
    put_stream(img, env, rng, STREAM_B, 0x60)
    return [], img.pokes()


@vrs(0x004CDDC0, "Replay_RegisterAnimations")
def gen_register(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0056B8D4, rng.choice([0xFFFF, 0xFFFE, 0xFFFD, 0, 1, 2, 5]) | (rng.getrandbits(16) << 16))
    return [], img.pokes()


# ---- vrw: writing a frame ------------------------------------------------------------------------------------------------------------------------------------------------

def writing_world(img, env, rng):
    skaters(img, env, rng, second=0.7)
    for base in (SKATER_A, SKATER_B):
        fake_skater(img, env, rng, base)
    for block, stream in ((STATE0, STREAM_A), (STATE1, STREAM_B)):
        state_block(img, env, rng, block, stream, 1)
        img.u32(block + 0x1C, rng.choice([0x20, 0x20, 1, 2, 5, 8, 16, 31, rng.randrange(1, 33)]))
        cursor = env.slab + stream + 4 * rng.randrange(0, 0x40)
        img.u32(block + 0x14, cursor)
        # the buffer-full test: the boundary is far below the cursor (full) or anywhere (not full)
        img.u32(block + 0x0C, (cursor + 0x218 - 0x7B800 - rng.choice([0, 1, 4, 0x100])) & 0xFFFFFFFF if rng.random() < 0.08 else env.slab + stream)
    img.u32(0x0056E320, rng.choice([0, 100, 5000, rng.randrange(0, 100000)]))
    img.u32(0x0056E338, rng.randrange(0, 30))
    img.u32(0x0056E438, rng.randrange(0, 30))
    for k in range(30):
        img.u32(0x0056E340 + 8 * k, rng.choice([1, 2, 3, 0, 1, 2]))
        img.u32(0x0056E340 + 8 * k + 4, rng.choice([0, 5, 0x1FF, 0x200, 0x7FFFFFFF, rng.randrange(0, 0x220)]))
    # the four event slots: -1 mostly
    for slot in (SPLAT_OTHER, SPLAT_PLAYER, TRICK_PLAYER, TRICK_OTHER):
        img.u32(slot, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 2, 3, 4, 5, 0x1FF, 0x200, 0x1234]))


@vrw(0x004CC0A0, "Replay_MatrixToAngles")
def gen_matrix_to_angles(env, n):
    img, rng = Image(), env.rng
    sk = env.slab + SKATER_A
    for k in range(16):
        img.u16(sk + 0x2E58 + 2 * k, short(rng) & 0xFFFF)
    for k in range(6):
        img.u16(STATE0 + 0x88 + 2 * k, rng.getrandbits(16))
    return [sk + 0x2E58, STATE0 + 0x88], img.pokes()


@vrw(0x004CC190, "Replay_WriteFrame")
def gen_write_frame(env, n):
    img, rng = Image(), env.rng
    writing_world(img, env, rng)
    put_stream(img, env, rng, STREAM_A)
    put_stream(img, env, rng, STREAM_B)
    return [env.slab + rng.choice([SKATER_A, SKATER_A, SKATER_B])], img.pokes()


@vrw(0x004CC0E0, "Replay_WriteFrameChecked")
def gen_write_frame_checked(env, n):
    img, rng = Image(), env.rng
    writing_world(img, env, rng)
    put_stream(img, env, rng, STREAM_A)
    put_stream(img, env, rng, STREAM_B)
    if rng.random() < 0.1:
        for block in (STATE0, STATE1):
            img.u32(block + 0x0C, 0)
    skater = rng.choice([env.slab + SKATER_A, env.slab + SKATER_A, env.slab + SKATER_B])
    return [skater], img.pokes()


# ---- vra: playing a frame ------------------------------------------------------------------------------------------------------------------------------------------------

def playing_world(img, env, rng, mode=2):
    skaters(img, env, rng, second=0.7)
    for base in (SKATER_A, SKATER_B):
        fake_skater(img, env, rng, base)
    world(img, env, rng)
    for block, stream in ((STATE0, STREAM_A), (STATE1, STREAM_B)):
        state_block(img, env, rng, block, stream, mode)
        img.u32(block + 0x1C, rng.choice([0, 1, 2, 5, 8, 16, 31, 32, rng.randrange(0, 33)]))
        img.u32(block + 0x14, env.slab + stream + 4 * rng.randrange(0, 0x60))
    put_stream(img, env, rng, STREAM_A)
    put_stream(img, env, rng, STREAM_B)
    # the highlight ranges (the loaded replay's): five {start, end, score} records inside state 0
    for slot in range(5):
        record = RANGES + 8 * slot
        img.u16(record, rng.randrange(0, 7))             # (a skip of n frames runs the game n times: keep the ranges short)
        img.u16(record + 2, rng.randrange(0, 7))
        img.u32(record + 4, rng.choice([0, 5, 10]))
    img.u32(0x0056E774, rng.choice([0, 1, 2, 3, 4, 0xFFFFFFFF]))
    img.u32(0x0056E698, rng.choice([0, 1]))


@vra(0x004CCD70, "Replay_ApplySkaterFrame")
def gen_apply(env, n):
    img, rng = Image(), env.rng
    playing_world(img, env, rng)
    skater = env.slab + rng.choice([SKATER_A, SKATER_A, SKATER_B])
    mode = rng.choice([1, 1, -1 & 0xFFFFFFFF, -1 & 0xFFFFFFFF, 0])
    return [skater, mode], img.pokes()


@vra(0x004CBBB0, "Replay_AdvanceFrames")
def gen_advance(env, n):
    img, rng = Image(), env.rng
    playing_world(img, env, rng)
    return [rng.choice([0, 1, 2, 3, 0xFFFFFFFF, 5])], img.pokes()


@vra(0x004CBC40, "Replay_AdvanceHighlight")
def gen_advance_highlight(env, n):
    img, rng = Image(), env.rng
    playing_world(img, env, rng)
    return [], img.pokes()


@vra(0x004CD750, "Video_Playback")
def gen_playback(env, n):
    img, rng = Image(), env.rng
    playing_world(img, env, rng)
    for block in (STATE0, STATE1):
        img.u32(block + 0x24, rng.choice([0, 0, 1, 2, 5]))
    which = rng.choice([SKATER_A, SKATER_A, SKATER_B])
    skater = env.slab + which
    if rng.random() < 0.3:
        # the glass of a window breaks when the frame leaves the skater in state 4 with a board trick animation and the glass count is under 250: an all-zero stream keeps the skater's own
        # state and animation (every "changed" flag reads 0)
        block = STATE0 if which == SKATER_A else STATE1
        img.u32(skater + 0x30B8, 4)
        img.u16(skater + 0xF6, rng.choice([0x28, 0x29, 0x2A, 0x39, 0x3A, 0x41, 0x42, 0x44, 0x47, 0x48, 0x8B, 0x8C]))
        img.u32(0x0055F72C, rng.choice([0, 100, 0xF9, 0xFFFFFFFF]))
        img.u32(block + 0x24, rng.choice([1, 2, 5]))
        img.u32(block + 0x18, 0)
        for k in range(0x200):
            img.u32(env.slab + (STREAM_A if which == SKATER_A else STREAM_B) + 4 * k, 0)
    return [skater], img.pokes()


@vra(0x004CDAB0, "Video_HandleStepping")
def gen_stepping(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 2, 2, 2]))
    img.u32(0x00561990, rng.choice([0, 0, 1]))
    img.u32(0x00561BFC, rng.choice([0x14, 0x15, 0x16, 0, 1, 2, 5]))
    img.u32(0x00533F3C, rng.choice([1, 2]))
    img.u32(0x0056E698, rng.choice([0, 1]))
    img.u32(0x0056E77C, rng.choice([0, 1, 2, 6]))
    img.u32(0x0056A8E0, word(rng))
    img.u32(0x0056A90C, word(rng))
    for k in range(0x2E8):
        if rng.random() < 0.05:
            img.u8(0x0056AFF0 + k, rng.choice([0, 1, 2]))
    for addr in (0x0056B009, 0x0056B175, 0x0056B018, 0x0056B184, 0x0056AFF8, 0x0056B164):
        img.u8(addr, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


@vra(0x004CDA90, "Video_Paused")
def gen_paused(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 2, 2]))
    img.u32(0x0056E698, rng.choice([0, 1, word(rng)]))
    return [], img.pokes()


# ---- vrc: the camera trigger ---------------------------------------------------------------------------------------------------------------------------------------------

@vrc(0x00411FC0, "Camera_SelectNearestTriggerPoint")
def gen_camera_trigger(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    count = rng.choice([0, 1, 1, 2, 2, 3, 3, 4, 5, 8, 0x46, 0xFFFFFFFE])
    entries = count if 0 < count <= 0x46 else 0
    img.u32(PLAYER, slab + SKATER_A)
    img.u32(POINT_COUNT, count)
    for k in range(0x46):
        img.u16(POINT_LIST + 2 * k, rng.choice([rng.randrange(0, 0x46), 0, 1, 0xFFFF, rng.getrandbits(16)]))
    flag_bits = [0, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800, 0x900, 0xA00, 0xC00, 0xF00, 0x1000, 0x10000]
    for k in range(-1, 0x46):
        img.u32(REGISTRY + 4 * k, (rng.choice(flag_bits) | rng.choice([0, 0xFF, rng.getrandbits(8), rng.getrandbits(8)]) | (rng.getrandbits(32) & 0xFFFF0000 if rng.random() < 0.1 else 0)) & 0xFFFFFFFF)
    for k in range(0x46):
        img.u16(slab + RECORDS + 4 * k, rng.choice([1, 1, 0, 2, 0xFFFF, 0x101, rng.getrandbits(16)]))
        img.u16(slab + RECORDS + 4 * k + 2, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 0xFFFF, 0x8000, 0x7FFF, rng.randrange(0, 12)]))
    clock = rng.choice([0, 100, 5000, rng.randrange(0, 100000), 0xFFFFFFF0])
    img.u32(0x0056E320, clock)
    img.u32(0x00563A38, slab + VIEW)
    img.u32(slab + VIEW + 0x0C, rng.getrandbits(32))
    for k in range(3):
        img.u32(0x00524A40 + 4 * k, rng.getrandbits(32))
    img.u32(0x00567CA4, word(rng))
    img.u32(0x00567CA8, word(rng))
    img.u32(0x00563A3C, word(rng))
    img.u32(0x00563A44, word(rng))
    for base, cam in ((SKATER_A, CAM_A), (SKATER_B, CAM_B)):
        img.u32(slab + base + 0x29B0, slab + cam)
        for k in range(3):
            img.u32(slab + base + 0x08 + 4 * k, rng.choice([0, 1, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.randrange(0, 0x400000)]))
            img.u32(slab + base + 0x310C + 4 * k, rng.choice([0, 1, 0xFFFFFFFF, 0x20, rng.randrange(-0x400, 0x400) & 0xFFFFFFFF, rng.getrandbits(32)]))
            img.u32(slab + cam + 0x08 + 4 * k, rng.getrandbits(32))
        for off in range(0x3A4, 0x3E4, 4):
            img.u32(slab + cam + off, rng.getrandbits(32))
        img.u32(slab + cam + 0x504, rng.getrandbits(32))
        img.u32(slab + cam + 0x5B4, rng.getrandbits(32))
    for block in (STATE0, STATE1):
        def index():
            return rng.choice([0xFFFFFFFF, rng.randrange(0, max(entries, 1)), rng.randrange(0, max(entries, 1)), rng.randrange(0, 0x46), rng.getrandbits(32)])

        img.u32(block + 0x28, rng.choice([0xFFFFFFFF, rng.randrange(0, 0x46), rng.randrange(0, max(entries, 1)), rng.randrange(0, max(entries, 1))]))
        for k in range(3):
            img.u32(block + 0x2C + 4 * k, rng.getrandbits(32))
        img.u32(block + 0x3C, (clock - rng.choice([0, 1, 0x3B, 0x3C, 0x3D, 0x100, 5000, -5000])) & 0xFFFFFFFF)
        img.u32(block + 0x40, index())
        img.u32(block + 0x44, rng.choice([0, 1, 5, 9, 10, 11, 12, 100, 0xFFFFFFFF, rng.randrange(0, 15)]))
        img.u32(block + 0x48, index())
        img.u32(block + 0x4C, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF, rng.randrange(0, 10), rng.getrandbits(32)]))
    skater = slab + rng.choice([SKATER_A, SKATER_A, SKATER_B])
    return [skater], img.pokes()
