"""Phase 4.10i2 probe, part 2: the shell's small classes and helpers (shell_helpers.cpp).

Groups: pie (the pre-load progress screen's object), shl (the menu pad poll, the old menu base class, the panel's line meters, the pad repeat, the one-liners) and twin (the text
window). They run in the front end's world (frontend_specs_front / frontend_specs_shell) with a draw list in the slab: a cursor, an end, a renderer object whose +0x84 and +0x88
point at the heads of the main list and the viewport list.

Slab layout (offsets from the scratch slab; the arena starts at 0x4000, allocations come from the bump allocator at its start and the small blocks of the 0x6a10-byte databases
from 0x9000):
  0xC000 the draw packets (0x800)   0xCA00 the renderer   0xCA80 the main list's head cell   0xCB00 the viewport   0xCB40 the viewport's list head cell   0xCB80 the depth source
  0xD000 the objects under test (a pie object, three text windows of 0x60 bytes)   0xD400 a key table (400 bytes)   0xD800 strings
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import (ARENA, FRONT_WORLD, SCRATCH_LINE, SLAB, check_disjoint, engine, ensure, front_regions, merged, without, LazyRanges, front_read_only)
from frontend_specs_front_cases import new_image
from frontend_specs_shell import DRIVE, SHELL_TEXT, SHELL_WORLD, install_shell_stubs, seed_shell, shell_world

GROUPS = ("pie", "shl", "twin")

PACKETS = 0xC000
RENDERER = 0xCA00
HEADS = 0xCA80
VIEWPORT = 0xCB00
VIEWPORT_HEAD = 0xCB40
DEPTH = 0xCB80
OBJECTS = 0xD000
KEYS = 0xD400
STRINGS = 0xD800

HELPER_WORLD = [
    (0x0056DB4C, 4), (0x0056DB70, 4),          # the spooler, the head of the text windows
    (0x0056A990, 0x10),                        # the menu pad's repeat counters
    (0x00563A38, 4),                           # the pointer whose +8 is the depth source
    (0x00561D50, 0x60),                        # the slide table
    (0x005615D0, 0x10),                        # the spooler-in-use byte
    (0x005244CC, 4),                           # the effects-busy flag
]
HELPER_TEXT = [(0x0052CF30, 0x30), (0x005189FC, 4), (0x0052E910, 0x20)]


def helper_regions(slab):
    out = front_regions(slab)
    result = without(merged(list(out) + SHELL_WORLD + HELPER_WORLD, gap=0x40), [(0x00568638, 4), (0x0056881C, 0x20)])
    check_disjoint(result, _RO)
    return result


def helper_read_only():
    def build():
        base = list(front_read_only())
        mutable = FRONT_WORLD + SHELL_WORLD + HELPER_WORLD
        return merged(without(base + SHELL_TEXT + HELPER_TEXT, mutable), gap=0)

    return LazyRanges(build)


_RO = helper_read_only()

for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = helper_regions
    READ_ONLY[_group] = _RO
    SEEDS[_group] = seed_shell


def pie(address, name, this=None):
    return spec(address, name, "pie", this=(lambda env: env.slab + this) if this is not None else None)


def shl(address, name, this=None):
    return spec(address, name, "shl", this=(lambda env: env.slab + this) if this is not None else None)


def twin(address, name, this=None):
    return spec(address, name, "twin", this=(lambda env: env.slab + this) if this is not None else None)


# ---- worlds -------------------------------------------------------------------------------------------------------------------------------------------------------

def draw_world(env, img, rng, *, room=None):
    """The draw list: the cursor (with room for `room` bytes, or a random amount including none), the renderer and the list heads, the screen scale."""
    DRIVE["small"] = 0
    slab = env.slab
    cursor = slab + PACKETS + 4 * rng.randrange(0, 8)
    room = rng.choice([0, 0x10, 0x14, 0x20, 0x80, 0x200]) if room is None else room
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, cursor + room)
    img.u32(0x00560FD4, slab + RENDERER)
    img.u32(slab + RENDERER + 0x84, slab + HEADS)
    img.u32(slab + RENDERER + 0x88, rng.choice([slab + VIEWPORT, slab + HEADS]) if rng.random() < 0.3 else slab + VIEWPORT)
    img.u32(slab + HEADS, rng.choice([0, slab + PACKETS + 0x700, rng.getrandbits(32)]))
    img.u32(slab + VIEWPORT + 8, slab + VIEWPORT_HEAD)
    img.u32(slab + VIEWPORT_HEAD, rng.choice([0, slab + PACKETS + 0x740]))
    img.u32(0x029DA394, rng.choice([640, 512, 800, 1024, 320, 0x7FFFFF + 5]))
    img.u32(0x029DA398, rng.choice([480, 240, 600, 768, 100]))
    img.u32(slab + RENDERER + 0x84 + 0, slab + HEADS)
    return cursor


# ---- the pre-load progress screen ------------------------------------------------------------------------------------------------------------------------------------

def pie_object(env, img, rng, *, loading=None):
    base = env.slab + OBJECTS
    img.put(base, bytes(0x40))
    img.u32(base + 0x24, rng.randrange(1, 0x100))
    img.u32(base + 0x28, rng.choice([0, 1, 2, 3, 3, 5, 0xFFFFFFFF]))
    img.u32(base + 0x2C, rng.choice([0, 0, 8, 100, 240, 247, 248, 255, 300, 0x80000000]))
    img.u32(base + 0x30, rng.choice([0, 1]) if loading is None else loading)
    img.u32(base + 0x20, rng.getrandbits(32))
    return base


@pie(0x00454930, "Pie_Construct", this=OBJECTS)
def gen_pie_construct(env, n):
    img, rng = new_image(env)
    pie_object(env, img, rng)
    return [], img.pokes()


@pie(0x00454950, "Pie_Delete", this=OBJECTS)
def gen_pie_delete(env, n):
    img, rng = new_image(env)
    pie_object(env, img, rng)
    return [rng.choice([0, 1, 1, 3, 0x100])], img.pokes()


@pie(0x00454970, "Pie_Destruct", this=OBJECTS)
def gen_pie_destruct(env, n):
    img, rng = new_image(env)
    pie_object(env, img, rng)
    return [], img.pokes()


@pie(0x00454980, "Pie_Load", this=OBJECTS)
def gen_pie_load(env, n):
    img, rng = new_image(env)
    pie_object(env, img, rng)
    img.u32(0x0056DB4C, rng.randrange(0x600000, 0x680000) & ~3)
    img.u8(0x005615DC, rng.choice([0, 0, 0, 1]))
    return [1], img.pokes()          # (retail walks off the table for any other id: its second entry has no name pointer)


@pie(0x00454A60, "Pie_Update", this=OBJECTS)
def gen_pie_update(env, n):
    img, rng = new_image(env)
    pie_object(env, img, rng)
    draw_world(env, img, rng, room=0x700)
    img.u32(0x0056DB4C, rng.randrange(0x600000, 0x680000) & ~3)
    img.u32(0x0056B834, rng.randrange(0x600000, 0x680000) & ~3)
    return [], img.pokes()


@pie(0x00454C80, "SlideTable_Clear")
def gen_slide_clear(env, n):
    img, rng = new_image(env)
    for k in range(0x60):
        img.u8(0x00561D50 + k, rng.choice([0, 1, rng.randrange(256)]))
    return [], img.pokes()


@pie(0x00454C90, "SlideTable_ClearBody")
def gen_slide_clear_body(env, n):
    return gen_slide_clear(env, n)


FOR_EACH_FUNCTION = 0x004ACFE0       # a function that only returns: its stub records the element each call was made on


@pie(0x00454CB0, "Vector_ForEach")
def gen_for_each(env, n):
    img, rng = new_image(env)
    base = env.slab + OBJECTS
    return [base, rng.choice([4, 0x10, 0x34, 0x60]), rng.choice([0, 1, 2, 3, 5]), FOR_EACH_FUNCTION], img.pokes()


@pie(0x00454CE0, "Front_Stub_00454CE0")
def gen_ret0(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@pie(0x00454CF0, "Front_Stub_00454CF0")
def gen_ret1(env, n):
    return gen_ret0(env, n)


@pie(0x00454D00, "Front_Stub_00454D00")
def gen_ret2(env, n):
    return gen_ret0(env, n)


@pie(0x00454D10, "GameFmv_Play")
def gen_game_fmv(env, n):
    img, rng = new_image(env)
    return [rng.choice([0, 1, 27, 0x40, rng.getrandbits(32)])], img.pokes()


# ---- the menu pad poll, the old menu base, the panel meters, the one-liners -----------------------------------------------------------------------------------------------

def pad_world(img, rng, *, edges=0.3):
    for pad in range(2):
        base = 0x0056B089 + pad * 0x16C
        for offset in (-0x11, -0x10, -1, 0, 0xF, 0x10, 0x1F, 0x20):
            img.u8(base + offset, rng.choice([0, 0, 1]) if rng.random() < edges else 0)
        img.u32(0x0056A998 + pad * 4, rng.choice([0, 0, 1, 2, 5, 10]))


@shl(0x0046F550, "Menu_PadPoll")
def gen_pad_poll(env, n):
    img, rng = new_image(env)
    pad_world(img, rng, edges=rng.choice([0.1, 0.3, 0.6]))
    return [rng.choice([0xFFFFFFFF, 0, 1, 0xFFFFFFFF]), rng.choice([0, 0, 1, 0x100]), rng.choice([0, 3, 6, 10])], img.pokes()


@shl(0x0046F650, "Menu_PadPollAny")
def gen_pad_poll_any(env, n):
    img, rng = new_image(env)
    pad_world(img, rng, edges=rng.choice([0.1, 0.3, 0.6]))
    return [], img.pokes()


@shl(0x00473C00, "MenuF80_Construct", this=OBJECTS)
def gen_f80_construct(env, n):
    img, rng = new_image(env)
    img.put(env.slab + OBJECTS, bytes(rng.randrange(256) for _ in range(0x40)))
    return [], img.pokes()


@shl(0x00473C20, "MenuBase_Delete", this=OBJECTS)
def gen_base_delete(env, n):
    img, rng = new_image(env)
    img.put(env.slab + OBJECTS, bytes(rng.randrange(256) for _ in range(0x40)))
    return [rng.choice([0, 1, 1, 2, 3])], img.pokes()


@shl(0x00473C40, "MenuBase_Destruct", this=OBJECTS)
def gen_base_destruct(env, n):
    img, rng = new_image(env)
    img.put(env.slab + OBJECTS, bytes(rng.randrange(256) for _ in range(0x40)))
    return [], img.pokes()


def coordinate(rng):
    return rng.choice([rng.randrange(0, 512), rng.randrange(0, 240), rng.randrange(-40, 600), 0, 511, 0xFFFF8000 | rng.randrange(0x8000), rng.randrange(0x10000, 0x12000)]) & 0xFFFFFFFF


@shl(0x0048AB20, "Panel_Line")
def gen_panel_line(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng)
    return [coordinate(rng) for _ in range(4)] + [rng.getrandbits(32) & 0xFFFFFF], img.pokes()


@shl(0x004ACF20, "Screen_LineUnguarded")
def gen_line_unguarded(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng, room=0x400)
    return [coordinate(rng) for _ in range(4)] + [rng.getrandbits(32) & 0xFFFFFF], img.pokes()


@shl(0x0048ABF0, "Panel_BalanceLR")
def gen_balance_lr(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng, room=rng.choice([0x20, 0x50, 0x200]))
    maximum = rng.choice([1, 3, 10, 100, 255])
    return [rng.randrange(-maximum, maximum + 1), maximum, rng.randrange(1, 100), rng.randrange(40, 470), rng.randrange(10, 230), rng.getrandbits(24), rng.getrandbits(24)], img.pokes()


@shl(0x0048AC60, "Panel_BalanceUD")
def gen_balance_ud(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng, room=rng.choice([0x20, 0x80, 0x400]))
    maximum = rng.choice([1, 3, 10, 100, 255])
    return [rng.randrange(-maximum, maximum + 1), maximum, rng.randrange(1, 100), rng.randrange(10, 480), rng.randrange(40, 200), rng.getrandbits(32) & 0xFFFFFFFF, rng.getrandbits(24)], img.pokes()


@shl(0x004AC370, "Pad_RepeatDirection")
def gen_repeat_direction(env, n):
    img, rng = new_image(env)
    for address in (0x0056B098, 0x0056B0A8, 0x0056B204, 0x0056B214):
        img.u8(address, rng.choice([0, 0, 1, 1]))
    img.u32(0x00561C14, rng.choice([0, 1, 5, 15, 16, 19, 20, 24, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.randrange(0, 200)]))
    return [], img.pokes()


@shl(0x004AC420, "Front_Stub_004AC420")
def gen_ret3(env, n):
    return gen_ret0(env, n)


@shl(0x004ACFE0, "Front_Stub_004ACFE0")
def gen_ret4(env, n):
    return gen_ret0(env, n)


@shl(0x004266A0, "Credits_AllocZeroed")
def gen_credits_alloc(env, n):
    img, rng = new_image(env)
    return [rng.choice([0, 1, 3, 4, 5, 0x20, 0x41, 0x100])], img.pokes()


@shl(0x004266D0, "Credits_Free")
def gen_credits_free(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32)], img.pokes()


@shl(0x0044AA70, "Font_StringWidth", this=OBJECTS)
def gen_string_width(env, n):
    img, rng = new_image(env)
    text = bytes(rng.randrange(1, 256) for _ in range(rng.choice([0, 1, 5, 20]))) + b"\0"
    img.put(env.slab + STRINGS, text)
    return [env.slab + STRINGS], img.pokes()


@shl(0x00452E00, "Front_SetHandicapNamesJump")
def gen_handicap_jump(env, n):
    img, rng = new_image(env)
    for address in (0x00561CD0, 0x00561CD4, 0x00561CD8):
        img.u32(address, rng.getrandbits(32))
    return [], img.pokes()


@shl(0x00452E10, "Front_SetHandicapNames")
def gen_handicap(env, n):
    return gen_handicap_jump(env, n)


@shl(0x004B0B20, "ShellUtils_Tail")
def gen_shell_tail(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@shl(0x004BAE70, "Data_XorDecrypt")
def gen_xor(env, n):
    img, rng = new_image(env)
    size = rng.choice([0, 1, 7, 399, 400, 401, 900])
    img.put(env.slab + STRINGS, bytes(rng.randrange(256) for _ in range(size)))
    img.put(env.slab + KEYS, bytes(rng.randrange(256) for _ in range(400)))
    return [env.slab + STRINGS, size], img.pokes()


# ---- the effect object, the object record, the allocators -------------------------------------------------------------------------------------------------------------

FAKE_VTABLE = OBJECTS + 0x500       # a fake vtable: slot 0 the virtual delete, slot 1 a function that only returns (its stub reports it)
SLOT1_TARGET = 0x004AC1B0
EFFECT = OBJECTS + 0x600            # the effect object (0x130 bytes)


def effect_image(env, img, rng, *, child=None):
    base = env.slab + EFFECT
    img.put(base, bytes(rng.randrange(256) for _ in range(0x130)))
    img.u32(base, 0x0051979C)
    img.u32(base + 0xC8, rng.choice([0, env.slab + OBJECTS + 0x480]) if child is None else child)
    img.u32(env.slab + OBJECTS + 0x480, env.slab + 0x0000)       # a child object whose vtable is the fake delete vtable (the slab's start)
    img.u32(env.slab + FAKE_VTABLE, 0x0000)
    return base


def fake_vtables(env, img):
    from frontend_specs_front import VIRTUAL_DELETE
    img.put(env.slab + FAKE_VTABLE, struct.pack("<II", VIRTUAL_DELETE, SLOT1_TARGET))


@shl(0x004B9320, "Effect_Delete", this=EFFECT)
def gen_effect_delete(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [rng.choice([0, 1, 1, 2, 3])], img.pokes()


@shl(0x004B9340, "Effect_Destruct", this=EFFECT)
def gen_effect_destruct(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [], img.pokes()


@shl(0x004B93A0, "Effect_Kill", this=EFFECT)
def gen_effect_kill(env, n):
    img, rng = new_image(env)
    base = effect_image(env, img, rng)
    fake_vtables(env, img)
    img.u32(base, env.slab + FAKE_VTABLE)
    return [], img.pokes()


@shl(0x004B93B0, "Effect_SetExtra", this=EFFECT)
def gen_effect_extra(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [rng.getrandbits(32), rng.choice([0, 1, 0x1FF, rng.randrange(256)])], img.pokes()


@shl(0x004B93D0, "Effects_SetBusy")
def gen_effects_busy(env, n):
    img, rng = new_image(env)
    img.u32(0x005244CC, rng.choice([0, 1, 5]))
    return [], img.pokes()


@shl(0x004B93E0, "Effect_FreeChild", this=EFFECT)
def gen_effect_free_child(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [], img.pokes()


@shl(0x004B9400, "Effect_SetFrames", this=EFFECT)
def gen_effect_frames(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [rng.choice([0, 1, 100, 0xFFFF, 0x12345])], img.pokes()


@shl(0x004B9420, "Effect_Move", this=EFFECT)
def gen_effect_move(env, n):
    img, rng = new_image(env)
    effect_image(env, img, rng)
    return [], img.pokes()


@shl(0x0047FD30, "TRG_AllocObjectStorage")
def gen_object_alloc(env, n):
    return gen_credits_alloc(env, n)


@shl(0x0047FD60, "Object_Free")
def gen_object_free(env, n):
    return gen_credits_free(env, n)


@shl(0x0047FD70, "TRG_AllocObjectStorage2")
def gen_object_alloc2(env, n):
    return gen_credits_alloc(env, n)


@shl(0x0047FDA0, "PSX_FreeObjectRecordArray")
def gen_object_free2(env, n):
    return gen_credits_free(env, n)


@shl(0x0047FDB0, "PSX_InitObjectRecord", this=EFFECT)
def gen_object_record_init(env, n):
    img, rng = new_image(env)
    img.put(env.slab + EFFECT, bytes(rng.randrange(256) for _ in range(0x60)))
    return [], img.pokes()


@shl(0x0047FE00, "PSX_ObjectRecord_Delete", this=EFFECT)
def gen_object_record_delete(env, n):
    img, rng = new_image(env)
    img.put(env.slab + EFFECT, bytes(rng.randrange(256) for _ in range(0x60)))
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@shl(0x0047FE20, "PSX_DestroyObjectRecord", this=EFFECT)
def gen_object_record_destruct(env, n):
    img, rng = new_image(env)
    img.put(env.slab + EFFECT, bytes(rng.randrange(256) for _ in range(0x60)))
    return [], img.pokes()


# ---- the text window ----------------------------------------------------------------------------------------------------------------------------------------------

WINDOW = OBJECTS + 0x100          # three windows of 0x60 bytes
LINES = OBJECTS + 0x300           # line arrays: pointers (0x80) and flags (0x40)


def window_image(env, img, rng, index, *, lines=None, linked=True):
    base = env.slab + WINDOW + 0x60 * index
    lines = rng.choice([1, 2, 3, 5, 8]) if lines is None else lines
    img.put(base, bytes(0x60))
    img.u32(base, 0x00519824)
    img.u32(base + 0x50, lines)
    img.u32(base + 0x30, env.slab + LINES + 0x80 * index)
    img.u32(base + 0x34, env.slab + LINES + 0x80 * index + 0x60)
    for k in range(lines):
        img.u32(env.slab + LINES + 0x80 * index + 4 * k, rng.choice([0, env.slab + STRINGS + 0x20 * rng.randrange(0, 8)]))
        img.u8(env.slab + LINES + 0x80 * index + 0x60 + k, rng.randrange(256))
    img.u32(base + 0x44, rng.getrandbits(32))
    return base


def window_world(env, img, rng):
    """A list of one to three windows (each links to the next through +0x48 and back through +0x4c) under the head cell."""
    count = rng.choice([0, 1, 2, 3])
    bases = [window_image(env, img, rng, k) for k in range(3)]
    for k in range(3):
        img.u32(bases[k] + 0x48, bases[k + 1] if k + 1 < count else 0)
        img.u32(bases[k] + 0x4C, bases[k - 1] if k > 0 else 0)
    img.u32(0x0056DB70, bases[0] if count else 0)
    return bases


@twin(0x004BA7A0, "TextWin_Construct", this=OBJECTS + 0x100)
def gen_tw_construct(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng)
    bases = window_world(env, img, rng)
    img.put(bases[0], bytes(rng.randrange(256) for _ in range(0x60)))
    return [rng.getrandbits(32), rng.randrange(0, 400), rng.randrange(0, 200), rng.randrange(8, 400), rng.choice([8, 9, 16, 17, 40, 63, 64, 100, 120])], img.pokes()


@twin(0x004BA8A0, "TextWin_Delete", this=OBJECTS + 0x100)
def gen_tw_delete(env, n):
    img, rng = new_image(env)
    bases = window_world(env, img, rng)
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@twin(0x004BA8C0, "TextWin_Destruct", this=OBJECTS + 0x100)
def gen_tw_destruct(env, n):
    img, rng = new_image(env)
    bases = window_world(env, img, rng)
    return [], img.pokes()


@twin(0x004BA940, "TextWin_SetRect", this=OBJECTS + 0x100)
def gen_tw_rect(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng)
    window_world(env, img, rng)
    return [coordinate(rng), coordinate(rng), coordinate(rng), coordinate(rng)], img.pokes()


@twin(0x004BAA50, "TextWin_SetColors", this=OBJECTS + 0x100)
def gen_tw_colors(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [rng.choice([rng.randrange(256), rng.getrandbits(32)]) for _ in range(15)], img.pokes()


@twin(0x004BAAC0, "TextWin_SetOwner", this=OBJECTS + 0x100)
def gen_tw_owner(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [rng.getrandbits(32)], img.pokes()


@twin(0x004BAAD0, "TextWin_Clear", this=OBJECTS + 0x100)
def gen_tw_clear(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


def line_args(env, rng):
    return [rng.choice([env.slab + STRINGS + 0x20 * rng.randrange(0, 8), 0, rng.getrandbits(32)])]


@twin(0x004BAB00, "TextWin_SetLine", this=OBJECTS + 0x100)
def gen_tw_set_line(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [rng.choice([0, 1, 2, 4, 0xFFFFFFFF, 7, 100]), *line_args(env, rng)], img.pokes()


@twin(0x004BAB70, "TextWin_PushLine", this=OBJECTS + 0x100)
def gen_tw_push(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return line_args(env, rng), img.pokes()


@twin(0x004BABC0, "TextWin_InsertLine", this=OBJECTS + 0x100)
def gen_tw_insert(env, n):
    return gen_tw_push(env, n)


@twin(0x004BAC00, "TextWin_SetLineMarked", this=OBJECTS + 0x100)
def gen_tw_set_marked(env, n):
    return gen_tw_set_line(env, n)


@twin(0x004BAC70, "TextWin_PushLineMarked", this=OBJECTS + 0x100)
def gen_tw_push_marked(env, n):
    return gen_tw_push(env, n)


@twin(0x004BACC0, "TextWin_InsertLineMarked", this=OBJECTS + 0x100)
def gen_tw_insert_marked(env, n):
    return gen_tw_push(env, n)


@twin(0x004BADD0, "TextWin_Last")
def gen_tw_last(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@twin(0x004BADF0, "TextWin_DrawAll")
def gen_tw_draw_all(env, n):
    img, rng = new_image(env)
    draw_world(env, img, rng)
    window_world(env, img, rng)
    img.u32(0x00563A38, env.slab + DEPTH)
    img.put(env.slab + DEPTH, bytes(rng.randrange(256) for _ in range(16)))
    return [], img.pokes()


# ---- stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------

def real_sprintf(fc):
    """The CRT's sprintf runs for real in these groups (the native code formats the same text with retail_format): the shell's stub of it is switched off."""
    for probe in fc._probes:
        if getattr(probe, "address", None) == 0x005002A1:
            probe.enabled = False


def install_helper_stubs(fc, env):
    install_shell_stubs(fc, env)
    rng = env.rng
    this_stub = dict(thiscall=True)
    # the spooler (the pre-load screen)
    engine(fc, 0x004B52B0, 0, **this_stub)
    engine(fc, 0x004B5370, 4, result=lambda a: rng.randrange(1, 0x100), **this_stub)
    engine(fc, 0x004B5580, 0, **this_stub)
    engine(fc, 0x004B55C0, 0, **this_stub)
    engine(fc, 0x004B58A0, 1, result=lambda a: rng.choice([0, 1, 1]), **this_stub)
    engine(fc, 0x004B5990, 1, result=lambda a: rng.choice([0, 0x600000 + rng.randrange(0, 0x1000)]), **this_stub)
    engine(fc, 0x004A91D0, 2, **this_stub)
    engine(fc, 0x004E8940, 5)
    real_sprintf(fc)
    # the game movie, the vector element function, the memory services
    engine(fc, 0x00454D30, 1)
    engine(fc, 0x004E7090, 1)
    engine(fc, FOR_EACH_FUNCTION, 0, **this_stub)
    engine(fc, 0x004B0B50, 0)
    engine(fc, 0x0046F490, 4, result=env.alloc)
    engine(fc, 0x0046F4D0, 1)
    engine(fc, 0x0044AAA0, 1, masks={0: 0xFF}, result=lambda a: rng.randrange(0, 30), **this_stub)
    engine(fc, 0x00418140, 0, result=lambda a: env.slab + KEYS)
    engine(fc, 0x004801F0, 1, **this_stub)                                     # CBody::RemoveFrom
    engine(fc, 0x00480160, 0, **this_stub)                                     # the base class destructor
    engine(fc, 0x004CAE10, 1, **this_stub)                                     # Math_Vector3sAdd
    engine(fc, 0x004CAD60, 0, **this_stub)                                     # Math_Vector3sMask12
    engine(fc, 0x004F39B0, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))      # the sine table
    ensure(fc, SLOT1_TARGET, 0, "virtual_slot1", convention="thiscall", this=True)


GROUP_STUBS = {group: install_helper_stubs for group in GROUPS}
