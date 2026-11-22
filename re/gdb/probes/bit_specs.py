"""Phase 4.15d probe: the bit lists and the base classes of bit.cpp (groups bit1: lists, base, textured quad; bit2: quad, polygon, chunk; bit3: glow, flat, linked, pixel).

A bit is a record of 0x60..0xb0 bytes with a vtable word first; the probe lays fake bits out in the scratch slab, each with a random body, the vtable of a fake class whose slot 0 (the deleting
destructor) is a stubbed engine function, and links them into the list the case calls for. Allocations come from the slab's arena; the animation resources are fake arrays in the slab (a byte or
dword count before the first entry). Services: the allocator (arena), Mem_GetBlockSize / Mem_Delete, Rnd, the resource and material lookups, the assertion routine.

Slab layout (offsets): fake vtable 0x0000, strings 0x0040 (4 of 0x20), animations 0x0100 (4 of 0x80), spare 0x0300, bits 0x0400 (24 of 0x100), arena 0x1c00 (0x2000 bytes).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("bit1", "bit2", "bit3")

FAKE_VTABLE = 0x0000
STRINGS = 0x0040
ANIMS = 0x0100
ANIM_STRIDE = 0x80
BITS = 0x0400
BIT_STRIDE = 0x100
BIT_COUNT = 24
ARENA = 0x1C00
ARENA_BYTES = 0x2000
SLAB = ARENA + ARENA_BYTES + 0x100
SCRATCH = 0x7FF00800 + 0xC00     # (bitkit::kScratch: where the port builds the locals the retail code keeps on the stack)

LISTS = {
    "nonrendered": 0x0055F704, "flat": 0x0055F6F0, "linked": 0x0055F720, "pixel": 0x0055F6E4, "polyline": 0x0055F70C, "gpolyline": 0x0055F6E0, "quad": 0x0055F708,
    "genpoly": 0x0055F724, "chunk": 0x0055F6C0, "glow": 0x0055F6E8, "glass": 0x0055F6EC, "gline": 0x0055F6CC, "special": 0x0055F710,
}
BIT_COUNTER = 0x0055F72C
BIT_BYTES = 0x0055F728
QUICK_ANIMS = 0x0055F6F4
FAKE_DELETE = 0x004AD950      # an engine function nothing here reaches: slot 0 of the fake vtable (the deleting destructor)
FAKE_MOVE = 0x004AD9A0        # a bare `ret` nothing here reaches: the other slots (Move, Display)
TIME_SCALE = 0x0056865C
GLOBALS = [(0x0055F6C0, 0x74), (0x00561C18, 4), (TIME_SCALE, 4), (0x005244E0, 0x10), (0x006A3E00, 0x120)]
STATIC = [(0x005244D0, 0x10)]       # the table of the four animation names

# the vtables of the classes
VT = {
    "bit": 0x005183D8, "special": 0x005183E0, "nonrendered": 0x005183EC, "ft4": 0x005183F4, "quad": 0x005183FC, "genpoly": 0x00518404, "chunk": 0x0051840C, "glow": 0x00518414,
    "wobbly": 0x0051841C, "flat": 0x00518424, "linked": 0x0051842C, "pixel": 0x00518434,
}


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    return {env.slab + FAKE_VTABLE: struct.pack("<4I", FAKE_DELETE, FAKE_MOVE, FAKE_MOVE, FAKE_MOVE)}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def bit(address, name, group, this=None):
    resolve = this if callable(this) else ((lambda env: env.slab + this) if this is not None else None)
    return spec(address, name, group, this=resolve)


def b1(address, name, this=BITS):
    return bit(address, name, "bit1", this)


def b2(address, name, this=BITS):
    return bit(address, name, "bit2", this)


def b3(address, name, this=BITS):
    return bit(address, name, "bit3", this)


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_bit_installed", False):
        return
    fc._bit_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, FAKE_DELETE, 1, "engine_004ad950", convention="thiscall", this=True)                 # the fake class's deleting destructor
    ensure(fc, FAKE_MOVE, 0, "engine_004ad9a0", convention="thiscall", this=True)                   # its other methods
    ensure(fc, 0x0046F510, 1, "engine_0046f510", result=lambda a: rng.choice([0x10, 0x20, 0x40, 0x68, 0x6C, 0x100, rng.randrange(8, 0x400)]))        # Mem_GetBlockSize
    ensure(fc, 0x004B3680, 1, "engine_004b3680", result=lambda a: env.slab + ANIMS + ANIM_STRIDE * rng.randrange(0, 4) + 8)   # BITS_FindNamedResource
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: rng.getrandbits(32))                # PSX_FindSceneMaterial


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def bit_at(env, index):
    return env.slab + BITS + BIT_STRIDE * index


def put_bit(img, env, rng, index, vtable=None, size=0x70, protected=None, dead=None):
    """A fake bit with a random body (the first dwords are the vtable, the list links and the age; the caller links it)."""
    at = bit_at(env, index)
    img.put(at, noise(rng, size))
    img.u32(at, env.slab + FAKE_VTABLE if vtable is None else vtable)
    img.u8(at + 0x3A, rng.choice([0, 0, 0, 1, 2]) if protected is None else protected)
    img.u8(at + 0x37, rng.choice([0, 0, 1, 2]) if dead is None else dead)
    return at


def link_chain(img, env, rng, first, count, vtable=None, size=0x70, protected=None, dead=None):
    """`count` bits linked as a doubly linked list starting at slot `first`; returns the address of the head (0 when there are none)."""
    addresses = [put_bit(img, env, rng, first + k, vtable, size, protected, dead) for k in range(count)]
    for k, at in enumerate(addresses):
        img.u32(at + 4, addresses[k - 1] if k > 0 else 0)
        img.u32(at + 8, addresses[k + 1] if k + 1 < count else 0)
    return addresses[0] if addresses else 0


def put_anim(img, env, rng, index, frames=None):
    """A fake animation: `frames` entries of 8 bytes at ANIMS + 0x80 * index + 8, the frame count in the byte (and dword) before."""
    at = env.slab + ANIMS + ANIM_STRIDE * index
    frames = rng.choice([1, 2, 3, 4, 5, 8, 12]) if frames is None else frames
    img.put(at, noise(rng, ANIM_STRIDE))
    img.u32(at + 4, frames)
    return at + 8


def quick_anims(img, env, rng):
    for k in range(4):
        img.u32(QUICK_ANIMS + 4 * k, put_anim(img, env, rng, k))


def time_scale(img, rng):
    img.u32(TIME_SCALE, rng.choice([0x100, 0x100, 0x100, 0x80, 0x200, 0x300, 0, 0xFF, rng.randrange(0, 0x400), 0xFFFFFF00]))


def counters(img, rng):
    img.u32(BIT_COUNTER, rng.choice([0, 1, 2, 5, 100, word(rng)]))
    img.u32(BIT_BYTES, rng.choice([0, 0x100, 0x1000, word(rng)]))


def list_heads(img, env, rng, heads=None):
    """Every list head: empty, or a chain (bits are shared by index so each head owns its own)."""
    slot = 0
    for name, address in LISTS.items():
        count = rng.choice([0, 0, 0, 1, 2]) if heads is None else heads.get(name, 0)
        head = link_chain(img, env, rng, slot, count) if count else 0
        slot += count
        img.u32(address, head)


# ---- the lists ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@b1(0x00403680, "Bit_Init", None)
def gen_bit_init(env, n):
    img, rng = Image(), env.rng
    img.put(0x0055F6C0, noise(rng, 0x74))
    img.u32(0x00561C18, word(rng))
    return [], img.pokes()


@b1(0x00403730, "DeleteBitList", None)
def gen_delete_list(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 5, 8])
    head = link_chain(img, env, rng, 0, count) if count else 0
    return [head], img.pokes()


@b1(0x00403760, "Bit_DeleteAll", None)
def gen_delete_all(env, n):
    img, rng = Image(), env.rng
    list_heads(img, env, rng)
    counters(img, rng)
    return [], img.pokes()


@b1(0x00403BD0, "Bit_RemoveDeadBits", None)
def gen_remove_dead(env, n):
    img, rng = Image(), env.rng
    list_heads(img, env, rng)
    return [], img.pokes()


@b1(0x00403C70, "RemoveDeadBits", None)
def gen_remove_dead_list(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 5, 8])
    head = link_chain(img, env, rng, 0, count) if count else 0
    return [head], img.pokes()


@b1(0x00403CA0, "Bit_ReduceRGB", None)
def gen_reduce_rgb(env, n):
    img, rng = Image(), env.rng
    img.u32(env.slab + BITS, rng.choice([word(rng), rng.getrandbits(32), 0x00FFFFFF, 0xFF808080, 0x01020304]))
    return [env.slab + BITS, rng.choice([0, 1, 2, 0x10, 0x80, 0xFF, 0x100, 300, -1, -5, rng.randrange(0, 0x120)]) & 0xFFFFFFFF], img.pokes()


@b1(0x00403D30, "CBit_New", None)
def gen_bit_new(env, n):
    img, rng = Image(), env.rng
    counters(img, rng)
    img.put(env.slab + ARENA, noise(rng, 0x200))
    return [rng.choice([0, 4, 0x3C, 0x54, 0x6C, 0x70, 0xB0, 0x1FF, 0x200, 0x300, rng.randrange(1, 0x240)])], img.pokes()


@b1(0x00403DC0, "CBit_Delete", None)
def gen_bit_delete(env, n):
    img, rng = Image(), env.rng
    counters(img, rng)
    return [rng.choice([env.slab + BITS, env.slab + ARENA, rng.getrandbits(32)])], img.pokes()


@b1(0x00403DF0, "CBit_AttachTo")
def gen_attach(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    head = link_chain(img, env, rng, 1, rng.choice([0, 1, 3])) if rng.random() < 0.7 else 0
    img.u32(LISTS["quad"], head)
    return [LISTS["quad"]], img.pokes()


@b1(0x00403E10, "CBit_DeleteFrom", lambda env: bit_at(env, getattr(env, "victim", 0)))
def gen_delete_from(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([1, 2, 3, 5])
    head = link_chain(img, env, rng, 0, count)
    # the bit under test is one of the chain; the head is the chain's, or another bit's
    env.victim = rng.randrange(0, count)
    img.u32(LISTS["quad"], head if rng.random() < 0.8 else bit_at(env, 7))
    return [LISTS["quad"]], img.pokes()


@b1(0x00403E40, "CBit_Die")
def gen_die(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    return [], img.pokes()


@b1(0x00403EA0, "CBit_SetPos")
def gen_set_pos(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    img.put(env.slab + BITS + 0xA0, noise(rng, 12))
    return [env.slab + BITS + 0xA0], img.pokes()


@b1(0x00403EC0, "CBit_SetRandomVelocity")
def gen_random_velocity(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    return [rng.choice([0, 1, 2, 5, 10, 100, rng.randrange(0, 50)])], img.pokes()


@b1(0x00403F00, "CBit_Construct")
def gen_bit_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x40)
    counters(img, rng)
    return [], img.pokes()


@b1(0x00403F50, "CBit_ScalarDelete")
def gen_bit_sdelete(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b1(0x00403F70, "CBit_Destruct")
def gen_bit_destruct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0)
    counters(img, rng)
    return [], img.pokes()


@b1(0x00403F90, "CBit_Move")
def gen_bit_move(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0x40)
    img.put(env.slab + BITS + 0x34, bytes(rng.choice([0, 1, 2, 3, 7, 8, 31, 32, 33, 0xFF]) for _ in range(3)))
    return [], img.pokes()




# ---- the classes that link themselves into a list -----------------------------------------------------------------------------------------------------------------------------------------

def chain_with(img, env, rng, list_name, size=0x90):
    """Bit 0 (the object under test) inside a chain of the list `list_name` (at a random place); the head is written."""
    others = rng.choice([0, 0, 1, 2, 3])
    place = rng.randrange(0, others + 1)
    slots = list(range(1, others + 1))
    order = slots[:place] + [0] + slots[place:]
    addresses = {k: put_bit(img, env, rng, k, size=size) for k in order}
    for i, k in enumerate(order):
        img.u32(addresses[k] + 4, addresses[order[i - 1]] if i > 0 else 0)
        img.u32(addresses[k] + 8, addresses[order[i + 1]] if i + 1 < len(order) else 0)
    img.u32(LISTS[list_name], addresses[order[0]])


def class_group(group, ctor, sdel, dtor, name, list_name, size=0x90, ctor_args=None, base_size=None):
    """Registers the three generators of a class: its constructor, deleting destructor and destructor."""
    deco = {"bit1": b1, "bit2": b2, "bit3": b3}[group]

    @deco(ctor, f"{name}_Construct")
    def gen_construct(env, n):
        img, rng = Image(), env.rng
        put_bit(img, env, rng, 0, size=base_size or size)
        counters(img, rng)
        if list_name:
            head = link_chain(img, env, rng, 1, rng.choice([0, 0, 1, 2])) if rng.random() < 0.5 else 0
            img.u32(LISTS[list_name], head)
        args = ctor_args(env, img, rng) if ctor_args else []
        return args, img.pokes()

    if sdel:
        @deco(sdel, f"{name}_ScalarDelete")
        def gen_sdelete(env, n):
            img, rng = Image(), env.rng
            chain_with(img, env, rng, list_name, size) if list_name else put_bit(img, env, rng, 0, size=size)
            counters(img, rng)
            return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()

    @deco(dtor, f"{name}_Destruct")
    def gen_destruct(env, n):
        img, rng = Image(), env.rng
        chain_with(img, env, rng, list_name, size) if list_name else put_bit(img, env, rng, 0, size=size)
        counters(img, rng)
        return [], img.pokes()


class_group("bit1", 0x00403FC0, 0x00403FE0, 0x00404000, "CSpecialDisplay", "special")
class_group("bit1", 0x00404020, 0x00404040, 0x00404060, "CNonRenderedBit", "nonrendered")
class_group("bit1", 0x00404080, 0x004040B0, 0x004040D0, "CFT4Bit", None, size=0x60)
class_group("bit1", 0x00404590, 0x004045E0, 0x00404600, "CQuadBit", "quad", size=0xA0)


# ---- the textured-quad bit -----------------------------------------------------------------------------------------------------------------------------------------------------------------

def ft4_body(img, env, rng, anim=True):
    put_bit(img, env, rng, 0, size=0x60)
    if anim:
        count = rng.choice([1, 2, 3, 5, 8, 12])
        base = put_anim(img, env, rng, 0, count)
        img.u32(bit_at(env, 0) + 0x44, base)
        img.u8(bit_at(env, 0) + 0x4D, count)
        img.u8(bit_at(env, 0) + 0x4E, rng.choice([0, 0, 1, count - 1, count, 0x7F, 0x80, 0xFF, rng.getrandbits(8)]))
        img.u8(bit_at(env, 0) + 0x4F, rng.getrandbits(8))
        img.u32(bit_at(env, 0) + 0x48, rng.choice([base, rng.getrandbits(32)]))
    return img


@b1(0x00404150, "CFT4Bit_SetScale")
def gen_set_scale(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    return [rng.choice([0, 1, 400, 0x7FFF, 0x8000, 0xFFFF, word(rng)])], img.pokes()


@b1(0x00404170, "CFT4Bit_SetSemiTransparent")
def gen_semi(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    return [], img.pokes()


@b1(0x004041C0, "CFT4Bit_SetTint")
def gen_set_tint(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    return [word(rng), word(rng), word(rng)], img.pokes()


@b1(0x00404210, "CFT4Bit_SetTransDecay")
def gen_set_trans_decay(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    return [rng.choice([0, 1, 5, 0x7FFF, 0x8000, 0xFFFF, word(rng)])], img.pokes()


@b1(0x00404220, "CFT4Bit_SetAnim")
def gen_set_anim_name(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    for k in range(4):
        put_anim(img, env, rng, k)
    return [rng.choice([env.slab + STRINGS, env.slab + STRINGS + 0x20, word(rng)])], img.pokes()


@b1(0x00404250, "CFT4Bit_SetAnimIndex")
def gen_set_anim_index(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    quick_anims(img, env, rng)
    index = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, -1, 4])
    if index in (-1, 4):
        # (the table is read past its ends: the neighbours are list heads, which point into the slab)
        img.u32(LISTS["flat"], bit_at(env, 3))
        img.u32(LISTS["nonrendered"], bit_at(env, 4))
    return [index & 0xFFFFFFFF], img.pokes()


@b1(0x004042D0, "CFT4Bit_SetFrame")
def gen_set_frame(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, True)
    count = img.b[bit_at(env, 0) + 0x4D]
    if rng.random() < 0.1:
        img.u32(bit_at(env, 0) + 0x44, 0)
    return [rng.choice([0, 1, count - 1, count, count + 1, -1, -2, 127, 128, 200, rng.randrange(-3, count + 3)]) & 0xFFFFFFFF], img.pokes()


@b1(0x00404390, "CFT4Bit_IncFrame")
def gen_inc_frame(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, True)
    img.u16(bit_at(env, 0) + 0x50, rng.choice([0, 0x80, 0x100, 0x200, 0x7FFF, 0x8000, 0xFFFF, 0xFF80, rng.getrandbits(16)]))
    time_scale(img, rng)
    return [], img.pokes()


@b1(0x004043D0, "CFT4Bit_IncFrameWithWrap")
def gen_inc_frame_wrap(env, n):
    return gen_inc_frame(env, n)


@b1(0x00406990, "CRibbonBit_Move")
def gen_ribbon_move(env, n):
    """12 s10: the move slot of a ribbon bit is a jump to CFT4Bit_IncFrameWithWrap."""
    return gen_inc_frame(env, n)


@b1(0x004068A0, "CFrag_Move")
def gen_frag_move(env, n):
    """12 s10: a fragment: the position and the three velocities and their drags, the frame, the age against its limit (the end of its life is CBit_Die, retail code in the capture)."""
    img, rng = Image(), env.rng
    at = put_bit(img, env, rng, 0)
    img.u16(at + 0xC, rng.choice([0, 1, 5, 0x7FFE, 0x7FFF, 0xFFFE, 0xFFFF, 0x8000, rng.getrandbits(16)]))
    img.u16(at + 0xE, rng.choice([0, 1, 2, 6, 30, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    count = rng.choice([0, 1, 3, 5, 12, 0x7F, 0x80, 0xFF])
    img.u8(at + 0x4D, count)
    img.u8(at + 0x4E, rng.choice([0, 1, max(count - 1, 0), count, 0x7F, 0x80, 0xFF, rng.getrandbits(8)]))
    for offset in (0x34, 0x35, 0x36):
        img.u8(at + offset, rng.choice([0, 1, 2, 3, 4, 31, 32, 33, 0xFF, rng.getrandbits(8)]))
    for offset in range(0x10, 0x34, 4):
        img.u32(at + offset, rng.choice([0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.randrange(0, 0x10000)]))
    return [], img.pokes()


@b1(0x00404460, "CFT4Bit_ChooseRandomFrame")
def gen_choose_frame(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, True)
    return [], img.pokes()


@b1(0x00404490, "CFT4Bit_Shrink")
def gen_shrink(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    img.u16(bit_at(env, 0) + 0x52, rng.choice([0, 0, 1, 5, 400, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u16(bit_at(env, 0) + 0x3E, rng.choice([0, 1, 5, 100, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u8(bit_at(env, 0) + 0x3A, rng.choice([0, 0, 0, 1]))
    return [rng.choice([0, 0, 1, 2, word(rng)])], img.pokes()


@b1(0x004044F0, "CFT4Bit_Fade")
def gen_fade(env, n):
    img, rng = Image(), env.rng
    ft4_body(img, env, rng, False)
    colour = rng.choice([0, 0x01000000, 0x2C808080, 0x00FFFFFF, 0x00000010, rng.getrandbits(32), rng.getrandbits(32), 0x2C0A0B0C])
    img.u32(bit_at(env, 0) + 0x40, colour)
    img.u16(bit_at(env, 0) + 0x3C, rng.choice([0, 1, 2, 5, 8, 0x10, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u8(bit_at(env, 0) + 0x3A, rng.choice([0, 0, 0, 1]))
    return [rng.choice([0, 0, 1, 2, word(rng)])], img.pokes()


# ---- the quad, the polygons and the chunk (group bit2) ---------------------------------------------------------------------------------------------------------------------------------------

SHORTS = 0x0300          # a few short vectors for the constructors: 8 of 8 bytes


def put_shorts(img, env, rng, index, low=-0x800, high=0x800):
    at = env.slab + SHORTS + 8 * index
    for k in range(3):
        img.u16(at + 2 * k, rng.choice([rng.randrange(low, high), 0, rng.randrange(-4, 5), rng.randrange(-0x7FFF, 0x7FFF)]) & 0xFFFF)
    return at


@b2(0x00404620, "CQuadBit_SetTextureIndex")
def gen_quad_texture_index(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    quick_anims(img, env, rng)
    index = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, -1, 4]) & 0xFFFFFFFF
    if index in (0xFFFFFFFF, 4):
        img.u32(LISTS["flat"], bit_at(env, 3))
        img.u32(LISTS["nonrendered"], bit_at(env, 4))
    return [index, rng.choice([0, 1, 2, 3, 4, -1, 12, rng.randrange(-2, 14)]) & 0xFFFFFFFF], img.pokes()


@b2(0x004046F0, "CQuadBit_SetTexture")
def gen_quad_texture(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    for k in range(4):
        put_anim(img, env, rng, k)
    return [env.slab + STRINGS, rng.choice([0, 1, 2, 3, 4, -1, 11, rng.randrange(-2, 12)]) & 0xFFFFFFFF], img.pokes()


@b2(0x00404810, "CQuadBit_FlagOn")
def gen_quad_flag(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    return [rng.choice([1, 2, 4, 0x80, 0xFF, 0x100, word(rng)])], img.pokes()


@b2(0x00404840, "CQuadBit_SetSemiTransparent")
def gen_quad_semi(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    return [], img.pokes()


@b2(0x00404860, "CQuadBit_SetTransparency")
def gen_quad_transparency(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    return [rng.choice([0, 1, 0x80, 0xFF, 0x100, 0x1FF, word(rng)])], img.pokes()


@b2(0x004048C0, "CQuadBit_OrientUsing")
def gen_quad_orient(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    img.put(env.slab + SPARE + 0x20, noise(rng, 12))
    at = env.slab + SPARE
    comp = lambda: rng.choice([-1, 1]) * rng.randrange(0x1000, 0x7000)
    values = [rng.choice([comp(), rng.randrange(-0x1000, 0x1000)]), comp(), comp()]
    for k, v in enumerate(values):
        img.u16(at + 2 * k, v & 0xFFFF)
    return [env.slab + SPARE + 0x20, at, rng.choice([0x1000, 0x2000, 0x800, 0x10000, rng.randrange(1, 0x8000), -0x1000 & 0xFFFFFFFF]),
            rng.choice([0x1000, 0x2000, 0x800, 0x10000, rng.randrange(1, 0x8000)]), word(rng)], img.pokes()


SPARE = 0x0300


@b2(0x00404AF0, "CGenPoly_Construct4")
def gen_genpoly4(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    counters(img, rng)
    img.u32(LISTS["genpoly"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return [put_shorts(img, env, rng, k) for k in range(4)], img.pokes()


@b2(0x00404BF0, "CGenPoly_Construct3")
def gen_genpoly3(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA0)
    counters(img, rng)
    img.u32(LISTS["genpoly"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return [put_shorts(img, env, rng, k) for k in range(3)], img.pokes()


@b2(0x00404BD0, "CGenPoly_ScalarDelete")
def gen_genpoly_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "genpoly", 0xA0)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b2(0x00404CB0, "CGenPoly_Destruct")
def gen_genpoly_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "genpoly", 0xA0)
    counters(img, rng)
    return [], img.pokes()


@b2(0x004051C0, "CChunkBit_Construct")
def gen_chunk_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xC0)
    counters(img, rng)
    img.u32(LISTS["chunk"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return [put_shorts(img, env, rng, k, -0x400, 0x400) for k in range(3)], img.pokes()


@b2(0x00405390, "CChunkBit_ScalarDelete")
def gen_chunk_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "chunk", 0xC0)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b2(0x004053B0, "CChunkBit_Destruct")
def gen_chunk_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "chunk", 0xC0)
    counters(img, rng)
    return [], img.pokes()


@b2(0x004053D0, "CChunkBit_SetRGB")
def gen_chunk_rgb(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xC0)
    return [word(rng) if rng.random() < 0.4 else rng.randrange(0, 256) for _ in range(3)], img.pokes()


@b2(0x004054B0, "CChunkBit_SetTexture")
def gen_chunk_texture(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xC0)
    return [word(rng) if rng.random() < 0.5 else rng.randrange(0, 0x10000) for _ in range(8)], img.pokes()


@b2(0x00405520, "CChunkBit_CalculateWorldCoords")
def gen_chunk_world(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xC0)
    at = bit_at(env, 0)
    for offset in (0x3C, 0x44, 0x4C, 0x54):
        for k in range(3):
            img.u16(at + offset + 2 * k, rng.choice([rng.randrange(-0x400, 0x400), rng.getrandbits(16), 0]))
    for k in range(3):
        img.u16(at + 0x8C + 2 * k, rng.choice([0, rng.randrange(0, 0x1000), rng.getrandbits(16)]))
        img.u32(at + 0x10 + 4 * k, rng.choice([0, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF, word(rng)]))
    img.put(0x006A3E00, noise(rng, 0x120))
    return [], img.pokes()


# ---- the glow, the flat, linked and pixel bits (group bit3) -------------------------------------------------------------------------------------------------------------------------------

GLOW_BLOCK = ARENA + 0x1000       # the sections and fringes of the fake glows


def fake_glow(img, env, rng, sections=None, fringes=None):
    """A glow in bit slot 0: `sections` bands per fringe, `fringes` fringes; the block holds the sections then the fringes (8 bytes each)."""
    sections = rng.choice([2, 4, 6, 8, 3]) if sections is None else sections
    fringes = rng.choice([0, 1, 2, 3]) if fringes is None else fringes
    at = put_bit(img, env, rng, 0, size=0xA8)
    block = env.slab + GLOW_BLOCK
    img.put(block, noise(rng, (fringes + 1) * sections * 8))
    img.u32(at + 0x3C, block)
    img.u32(at + 0x40, block + sections * 8)
    img.u32(at + 0x44, sections)
    img.u32(at + 0x48, fringes)
    return at, sections, fringes


@b3(0x004056C0, "CGlow_Construct")
def gen_glow_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA8)
    counters(img, rng)
    img.u32(LISTS["glow"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    return [rng.choice([1, 2, 3, 4, 8, 5]), rng.choice([0, 1, 2, 3])], img.pokes()


@b3(0x00405810, "CGlow_ConstructSimple")
def gen_glow_simple(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xA8)
    counters(img, rng)
    img.u32(LISTS["glow"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    img.put(env.slab + SPARE, noise(rng, 12))
    return [env.slab + SPARE] + [word(rng) for _ in range(8)], img.pokes()


@b3(0x004057F0, "CGlow_ScalarDelete")
def gen_glow_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "glow", 0xA8)
    img.u32(bit_at(env, 0) + 0x3C, env.slab + GLOW_BLOCK)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b3(0x00405990, "CGlow_Destruct")
def gen_glow_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "glow", 0xA8)
    img.u32(bit_at(env, 0) + 0x3C, env.slab + GLOW_BLOCK)
    counters(img, rng)
    return [], img.pokes()


@b3(0x00405A70, "CGlow_SetRadius")
def gen_glow_radius(env, n):
    img, rng = Image(), env.rng
    fake_glow(img, env, rng)
    return [word(rng)], img.pokes()


@b3(0x00405AA0, "CGlow_SetFringeWidth")
def gen_glow_fringe_width(env, n):
    img, rng = Image(), env.rng
    _, _, fringes = fake_glow(img, env, rng, fringes=rng.choice([1, 2, 3]))
    return [rng.choice([0, 1, fringes - 1, fringes, fringes + 1, rng.randrange(0, fringes)]), word(rng)], img.pokes()


@b3(0x00405B20, "CGlow_SetCentreRGB")
def gen_glow_centre(env, n):
    img, rng = Image(), env.rng
    fake_glow(img, env, rng)
    return [word(rng), word(rng), word(rng)], img.pokes()


@b3(0x00405B50, "CGlow_SetRGB")
def gen_glow_rgb(env, n):
    img, rng = Image(), env.rng
    fake_glow(img, env, rng)
    return [word(rng), word(rng), word(rng)], img.pokes()


@b3(0x00405B90, "CGlow_SetFringeRGB")
def gen_glow_fringe_rgb(env, n):
    img, rng = Image(), env.rng
    _, _, fringes = fake_glow(img, env, rng, fringes=rng.choice([1, 2, 3]))
    return [rng.randrange(0, fringes + 1), word(rng), word(rng), word(rng)], img.pokes()


@b3(0x00405C40, "CGlow_SetRadialRGB")
def gen_glow_radial_rgb(env, n):
    img, rng = Image(), env.rng
    _, sections, fringes = fake_glow(img, env, rng)
    return [rng.choice([0, sections - 1, sections, -1, rng.randrange(0, sections)]) & 0xFFFFFFFF, word(rng), word(rng), word(rng)], img.pokes()


@b3(0x00405D20, "CGlow_Reduce")
def gen_glow_reduce(env, n):
    img, rng = Image(), env.rng
    fake_glow(img, env, rng)
    return [rng.choice([0, 1, 5, 0x10, 0x80, 0xFF, 0x100, -1, rng.randrange(0, 0x100)]) & 0xFFFFFFFF], img.pokes()


@b3(0x00405D60, "CWobblyGlow_Construct")
def gen_wobbly_construct(env, n):
    img, rng = Image(), env.rng
    put_bit(img, env, rng, 0, size=0xB0)
    counters(img, rng)
    img.u32(LISTS["glow"], link_chain(img, env, rng, 1, rng.choice([0, 1, 2])) if rng.random() < 0.5 else 0)
    img.put(env.slab + SPARE, noise(rng, 12))
    return [env.slab + SPARE, rng.choice([0x100, 0x1000, 0x2000, 0x7FFFFFFF, 0x80000000, word(rng)]), word(rng), rng.choice([0x100, 0x200, 0x7F, 0xFF, 0x80000000, word(rng)]),
            word(rng), word(rng), word(rng), word(rng), word(rng), word(rng)], img.pokes()


@b3(0x00405E30, "CWobblyGlow_ScalarDelete")
def gen_wobbly_sdelete(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "glow", 0xB0)
    img.u32(bit_at(env, 0) + 0x3C, env.slab + GLOW_BLOCK)
    counters(img, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@b3(0x00405E50, "CWobblyGlow_Destruct")
def gen_wobbly_destruct(env, n):
    img, rng = Image(), env.rng
    chain_with(img, env, rng, "glow", 0xB0)
    img.u32(bit_at(env, 0) + 0x3C, env.slab + GLOW_BLOCK)
    counters(img, rng)
    return [], img.pokes()


@b3(0x00405E60, "CWobblyGlow_Move")
def gen_wobbly_move(env, n):
    """12 s5: a wobbly glow of up to eight sections (the phases and speeds are eight words each at +0x7c and +0x5c); the sine is retail code in the capture."""
    img, rng = Image(), env.rng
    sections = rng.choice([0, 1, 2, 3, 4, 6, 8, 8])
    at = put_bit(img, env, rng, 0, size=0xB0)
    block = env.slab + GLOW_BLOCK
    img.put(block, noise(rng, max(sections, 1) * 8))
    img.u32(at + 0x3C, block)
    img.u32(at + 0x44, sections)
    for k in range(8):
        img.u32(at + 0x5C + 4 * k, rng.choice([0x32, 0x63, 0, 0x1000, 0xFFFFFFFF, rng.randrange(0x32, 0x64), word(rng)]))
        img.u32(at + 0x7C + 4 * k, rng.choice([0, 0xFFF, 0x1000, 0x7FFFFFFF, 0x80000000, rng.randrange(0, 0x1000), word(rng)]))
    img.u32(at + 0x9C, rng.choice([0, 1, 0x100, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.randrange(0, 0x4000), word(rng)]))
    img.u32(at + 0xA0, rng.choice([0, 0x100, 0x1000, 0x7FFFFFFF, word(rng)]))
    return [], img.pokes()


def class_group3(ctor, sdel, dtor, name, list_name, size):
    class_group("bit3", ctor, sdel, dtor, name, list_name, size=size)


class_group3(0x00405EC0, 0x00405EF0, 0x00405F10, "CFlatBit", "flat", 0x70)
class_group3(0x00405F30, 0x00405F70, 0x00405F90, "CLinked2EndedBit", "linked", 0x70)
class_group3(0x00405FB0, 0x00405FD0, 0x00405FF0, "CPixel", "pixel", 0x50)
