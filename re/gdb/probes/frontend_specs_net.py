"""Phase 4.10i2 probe, part 3: the network lobby (netmenu.cpp).

Groups: net1 (the lobby window's small methods and the summary's helpers), net2 (the lobby: the state machine and its pages) and net3 (the constructors and destructors, the
screen loop, the summary window).

The lobby is a window of menu lists and items; the probe builds the window in the scratch slab with fake lists and items (the menu probe's fake vtables with the slots the lobby
calls added: list slot 1 and 3, item slot 7 and 8), the network layer's answers poked into its statics and scripted through its stubs, and the pads' edge bytes poked per case.
The text the lobby formats (sprintf is the CRT's, run for real) lands in the slab and the statics.

Slab layout: the menu probe's (vtables at 0, glyphs at 0x200, fonts for Font_Find at 0x3d00, ...), plus
  0x9000 the lobby window (0xa00)   0x9a00 the summary window (0x360)   0x9e00 five fake lists of 0x1c0 bytes   0xa700 three styles of 0x180   0xb000 text buffers
The allocator's arena is 0x4800..0x9000.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import LazyRanges, SCRATCH_LINE, check_disjoint, engine, ensure, merged, without
from frontend_specs_shell import DRIVE, over, tick
from frontend_specs_menu import (ARENA, CHILD_STRIDE, CHILDREN, FIND_FONTS, FONT_DRAW, FONT_SIZE, GLYPHS, ITEM_DRAW, ITEM_HIGHLIGHT, ITEM_NAVIGATE, ITEM_SET_POSITION, ITEM_SNAP, LIST_V2, LIST_V4,
                                 LIST_V5, LIST_V6, LIST_V7, PROPS, SPRITE_DRAW, STRING_POOL, VIRTUAL_DELETE, VT_FONT, VT_ITEM, VT_LIST, VT_SPRITE, Image, child_at, item_fields, seed_menu,
                                 sprite_at, string_at)

GROUPS = ("net1", "net2", "net3", "net4")

SLAB = 0xC000
WINDOW = 0x9000
SUMMARY = 0x9A00
LISTS = 0x9E00
LIST_STRIDE = 0x1C0
STYLES = 0xA700
BUFFERS = 0xB000
VT_WINDOW = 0x0140
VT_PROMPT = 0x0180

# retail entry points the fake vtables point at (nothing in a lobby call reaches them)
LIST_V1 = 0x004ADA30
LIST_V3 = 0x004ADA40
ITEM_V7 = 0x004ADA50
ITEM_V8 = 0x004ADA60
WINDOW_UPDATE = 0x004ADBA0

# the lobby's mutable statics
NET_WORLD = [
    (0x006A1410, 0x10), (0x006A13F8, 8), (0x006A18B0, 4), (0x006A18D8, 0x50), (0x006A3A88, 0x10), (0x006A3AB0, 0x350),
    (0x0056AD10, 0x20), (0x005358F8, 8), (0x00561C74, 4), (0x00561C90, 4), (0x00533F38, 4), (0x0056A888, 4), (0x0056AF20, 4), (0x0056E31C, 8), (0x00568624, 4), (0x0056114C, 4),
    (0x0056AFF8, 0x3C0), (0x005618C0, 0x10), (0x0056A990, 0x10), (0x00560FD4, 4), (0x0056A488, 0xA80),
]
NET_WORLD += [(0x0053A514 + character * 0x170, 4) for character in range(20)]
NET_STATIC = [
    (0x0056AD30, 0x1F0), (0x00535900, 0x300), (0x0052EAC0, 0x50), (0x00525400, 0x80), (0x005252F0, 0x10), (0x00525454, 0x20), (0x00525854, 0x10), (0x005256DC, 8), (0x00525898, 8),
    (0x005359C0, 0x20), (0x0053C1F8, 14 * 0x1AC), (0x0053A3D8, 20 * 0x170 + 0x20), (0x00527D54, 4), (0x0052C8C0, 8), (0x00535AEC, 0x10), (0x0055FE10, 0x10),
    (0x00535A00, 0x120), (0x0052D860, 0x30), (0x005316B0, 0x20), (0x005318E0, 0x10), (0x0052DF4C, 0x10), (0x005258F0, 0x30), (0x0052EAF0, 0x10),
]


from frontend_specs_menu import READ_ONLY as _MENU_RO  # noqa: E402

NET_STATIC += list(_MENU_RO["menu"])


def net_regions(slab):
    out = [(slab, SLAB)] + NET_WORLD
    result = without(merged(out, gap=0x10), [])
    check_disjoint(result, _RO)
    return result


def net_read_only():
    def build():
        from opentony.memory import mem

        mutable = NET_WORLD + [(0x0053A514 + c * 0x170, 4) for c in range(20)]
        strings = []
        # the strings the pointer cells and the item tables point to
        for start, stride, count in ((0x0052EAC4, 4, 14), (0x005359C0, 4, 8)):
            for k in range(count):
                value = mem.u32(start + stride * k)
                if 0x00520000 <= value < 0x00540000:
                    strings.append((value, 0x60))
        for start, count in ((0x0056AD38, 11), (0x0056AE40, 9), (0x00535910, 3), (0x00535970, 3)):
            for k in range(count):
                value = mem.u32(start + 24 * k)
                if 0x00520000 <= value < 0x00540000:
                    strings.append((value, 0x60))
        for k in range(14):
            value = mem.u32(0x0053C238 + 0x1AC * k)
            if 0x00520000 <= value < 0x00540000:
                strings.append((value, 0x60))
        return merged(without(NET_STATIC + strings, mutable), gap=0)

    return LazyRanges(build)


_RO = net_read_only()


def seed_net(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_menu(env, rng)
    env.alloc_base = ARENA
    env.alloc_size = WINDOW - ARENA
    # the fake vtables get the slots the lobby uses
    item = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ITEM_SET_POSITION), (2, ITEM_DRAW), (3, ITEM_HIGHLIGHT), (5, ITEM_NAVIGATE), (6, ITEM_SNAP), (7, ITEM_V7), (8, ITEM_V8)):
        struct.pack_into("<I", item, 4 * slot, entry)
    write_chunks(env.slab + VT_ITEM, bytes(item))
    lists = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, LIST_V1), (2, LIST_V2), (3, LIST_V3), (4, LIST_V4), (5, LIST_V5), (6, LIST_V6), (7, LIST_V7)):
        struct.pack_into("<I", lists, 4 * slot, entry)
    write_chunks(env.slab + VT_LIST, bytes(lists))
    write_chunks(env.slab + VT_WINDOW, struct.pack("<II", VIRTUAL_DELETE, WINDOW_UPDATE))
    write_chunks(env.slab + VT_PROMPT, struct.pack("<IIIIIIII", VIRTUAL_DELETE, 0, 0, 0, 0, 0, 0, ITEM_V7))
    return writes


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = net_regions
    READ_ONLY[_group] = _RO
    SEEDS[_group] = seed_net


def net1(address, name, this=None):
    return spec(address, name, "net1", this=(lambda env: env.slab + this) if this is not None else None)


def net2(address, name, this=None):
    return spec(address, name, "net2", this=(lambda env: env.slab + this) if this is not None else None)


def net3(address, name, this=None):
    return spec(address, name, "net3", this=(lambda env: env.slab + this) if this is not None else None)


def net4(address, name, this=None):
    return spec(address, name, "net4", this=(lambda env: env.slab + this) if this is not None else None)


# ---- the world ----------------------------------------------------------------------------------------------------------------------------------------------------

def new_image(env):
    img = Image()
    clock = env.rng.randrange(0, 100000)
    img.u32(0x0056E31C, clock)
    return img, env.rng


def net_world(env, img, rng, *, state=None, tick=None):
    """The network layer's statics and the pads."""
    DRIVE["edit"] = 0
    for k in range(16):
        fake_item(env, img, rng, 40 + k, k)
    clock = rng.randrange(1000, 100000)
    img.u32(0x0056E31C, clock)
    img.u32(0x00568624, clock - rng.choice([0, 1, 2, 3, 50]))
    for address in (0x006A3B20, 0x006A3B21, 0x006A3B22, 0x006A3B23, 0x006A3B24, 0x006A3B30, 0x006A3C11, 0x006A3C13, 0x006A13FC, 0x006A1417, 0x006A3DE1, 0x0056AD18, 0x0056AD19, 0x0056AD20):
        img.u8(address, rng.choice([0, 0, 1]))
    img.u8(0x006A3B21, rng.choice([0, 1, 1]))
    img.u8(0x006A3B22, rng.choice([0, 1]))
    img.u32(0x006A18B0, rng.choice([0, 1, 2, 3, 5, 6, 7, 12, 13, 0xFFFFFFFF]))
    img.u32(0x006A3A90, rng.choice([0, 1, 2, 3, 5, 6, 7]))
    img.u32(0x0056AD10, rng.choice([0, 1]))
    img.u32(0x0056AD14, rng.choice([0, 1, 5, 6]))
    img.u32(0x0056AD1C, rng.choice([0, 1, 2]))
    img.u32(0x005358F8, rng.choice([0, 1, 2, 5, 6, 9, 10, 4]))
    img.u32(0x005358FC, rng.choice([0, 1, 2, 3, 6, 7, 8, 9]))
    img.u32(0x00561C74, rng.randrange(0, 10))
    img.u32(0x00561C90, rng.choice([0xFFFFFFFF, 0, 3, 5, 12, 13]))
    img.u32(0x00533F38, rng.randrange(0, 12))
    img.u32(0x0056A888, rng.choice([0, 1, 2]))
    img.u32(0x0056114C, rng.getrandbits(32))
    img.u32(0x00560FD4, env.slab + BUFFERS + 0x400)
    img.u32(env.slab + BUFFERS + 0x400 + 0x88, env.slab + BUFFERS + 0x440)
    for address, size in ((0x006A3B71, 0x40), (0x006A3BB1, 0x40), (0x006A3B31, 0x40), (0x006A3ABF, 0x20), (0x006A18DF, 0x20), (0x006A3DCD, 0x20)):
        text = rng.choice([b"", b" ", b"  \t ", b"Host", b"Tony", b"192.168.0.1", b"a b", bytes(rng.randrange(33, 127) for _ in range(rng.randrange(1, 14)))])
        img.put(address, text + b"\0" + bytes(size - len(text) - 1) if size > len(text) else text[:size - 1] + b"\0")
    img.put(0x006A3C13, bytes([rng.choice([0, 0, 1])]) + b"chat message\0")
    # the pads: the edge bytes (back on either pad, confirm on any) and the poll bytes
    for address in (0x0056AFF9, 0x0056B165, 0x0056B029, 0x0056B195, 0x0056B0D9):
        img.u8(address, rng.choice([0, 0, 0, 1]))
    for pad in range(2):
        base = 0x0056B089 + pad * 0x16C
        for offset in (-0x11, -0x10, -1, 0, 0xF, 0x10, 0x1F, 0x20):
            img.u8(base + offset, rng.choice([0, 0, 0, 1]))
        img.u32(0x0056A998 + pad * 4, rng.choice([0, 0, 1, 5]))
    for character in range(20):
        img.u32(0x0053A514 + character * 0x170, rng.choice([0, 0, 2, 0x82, 1]))
    for slot in range(21):
        img.put(0x0056A488 + slot * 0x80, bytes(rng.randrange(33, 127) for _ in range(rng.randrange(1, 20))) + bytes(1))
    return clock


def fake_item(env, img, rng, index, ident, *, selectable=None, highlighted=0):
    base = child_at(env, index)
    item_fields(env, img, base, rng, id=ident, selectable=rng.choice([0, 1, 1]) if selectable is None else selectable, highlighted=highlighted, props=0)
    return base


def fake_list(env, img, rng, list_index, first_item, count, *, highlighted=None):
    """A list of `count` items (ids 0..count-1, one highlighted) at the list slot, the way a real MenuList is laid out; returns (list, [items])."""
    base = env.slab + LISTS + LIST_STRIDE * list_index
    img.garbage(base, LIST_STRIDE, rng)
    img.u32(base, env.slab + VT_LIST)
    highlighted = rng.randrange(count) if highlighted is None else highlighted
    items = []
    for k in range(count):
        item = fake_item(env, img, rng, first_item + k, k, highlighted=1 if k == highlighted else 0)
        items.append(item)
        img.u32(base + 0x14 + 4 * k, item)
    for k in range(count, 0x60):
        img.u32(base + 0x14 + 4 * k, 0)
    img.u32(base + 0x194, count)
    img.u32(base + 0x198, highlighted)
    return base, items


def window_world(env, img, rng, *, state=None):
    """The lobby window with its five lists, the fonts, the styles, the prompt and the side panels."""
    base = env.slab + WINDOW
    img.garbage(base, 0xA00, rng)
    img.u32(base, env.slab + VT_WINDOW)
    img.u32(base + 0xA8, 0)
    img.put(base + 0x28, bytes(0x80))                      # (the window base's child slots)
    clock = net_world(env, img, rng)
    img.u32(base + 0xAC, clock + rng.choice([-5, -1, 0, 1, 2, 10]) & 0xFFFFFFFF)
    state = rng.randrange(0, 15) if state is None else state
    img.u32(base + 0xF0, state)
    img.u32(base + 0xF4, rng.choice([state, state, rng.randrange(0, 15)]))
    img.u32(base + 0xF8, clock + rng.choice([-30, -1, 0, 1, 30, 100]) & 0xFFFFFFFF)
    img.u32(base + 0xFC, rng.choice([0, 0, 1, 2, 4]))
    img.u32(base + 0x100, rng.choice([0xFFFFFFFF, 0, 1, 2, 3]))
    img.put(base + 0xB0, bytes(rng.choice([0, 1, 2]) and rng.randrange(32, 127) for _ in range(rng.choice([0, 1, 8]))) + b"\0")
    img.u8(base + 0x138, rng.choice([0, 1]))
    # the lists: main (11), found (4), form (9), settings (3), game (3); the items are the window's fields
    item_cursor = 0
    for index, (list_slot, items_slot, count) in enumerate(((0x144, 0x148, 11), (0x180, 0x184, 4), (0x194, 0x198, 9), (0x1BC, 0x1C0, 3), (0x1CC, 0x1D0, 3))):
        lst, items = fake_list(env, img, rng, index, item_cursor, count)
        item_cursor += count
        img.u32(base + list_slot, lst)
        for k, item in enumerate(items):
            img.u32(base + items_slot + 4 * k, item)
    for offset in (0x1DC, 0x328, 0x474, 0x5C0):
        img.u32(base + offset, env.slab + VT_FONT)
    for k, offset in enumerate((0x70C, 0x710, 0x714)):
        img.u32(base + offset, env.slab + STYLES + 0x180 * k)
        img.u32(env.slab + STYLES + 0x180 * k, env.slab + VT_SPRITE)
    img.u32(base + 0x9E4, env.slab + 0x1200)               # the main menu prompt (a fake object with the prompt vtable)
    img.u32(env.slab + 0x1200, env.slab + VT_PROMPT)
    img.u32(base + 0x9E8, sprite_at(env, 0))
    img.u32(base + 0x9EC, sprite_at(env, 1))
    img.u32(base + 0x9F0, rng.choice([0, sprite_at(env, 2)]))
    img.u32(base + 0x9F4, sprite_at(env, 3))
    img.u32(base + 0x140, sprite_at(env, 4))
    for k in range(7):
        text = bytes(rng.randrange(33, 127) for _ in range(rng.randrange(0, 20))) + b"\0"
        img.put(base + 0x734 + 0x38 * k, text)
        img.u32(base + 0x718 + 4 * k, rng.choice([0, 1, 2]))
    img.u32(base + 0x104, 0)
    # the chat message (+0x884) and the typed text (+0x8bc) are short strings in retail (the keyboard handler caps them); the garbage fill would leave them unterminated
    for offset in (0x884, 0x8BC):
        img.put(base + offset, bytes(rng.randrange(33, 127) for _ in range(rng.randrange(0, 20))) + b"\0")
    return base


def summary_world(env, img, rng):
    base = env.slab + SUMMARY
    img.garbage(base, 0x360, rng)
    img.u32(base, env.slab + VT_WINDOW)
    img.u32(base + 0xA8, 0)
    img.put(base + 0x28, bytes(0x80))
    for offset in (0xB0, 0x1FC):
        img.u32(base + offset, env.slab + VT_FONT)
    img.u32(base + 0x348, env.slab + STYLES)
    img.u32(env.slab + STYLES, env.slab + VT_SPRITE)
    return base


# ---- net1: the small methods ----------------------------------------------------------------------------------------------------------------------------------------

@net1(0x0047CDA0, "NetMenu_Return0")
def gen_ret0(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32)], img.pokes()


@net1(0x0047CDB0, "NetMenu_IsReady", this=WINDOW)
def gen_is_ready(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@net1(0x0047E210, "NetMenu_SetItemTextures")
def gen_set_textures(env, n):
    img, rng = new_image(env)
    base = window_world(env, img, rng)
    table = env.slab + BUFFERS
    count = rng.choice([0, 1, 3, 9])
    for k in range(count):
        img.put(table + 24 * k, struct.pack("<I", rng.choice([0, rng.getrandbits(32)])) + bytes(20))
    return [base + 0x198, table, count], img.pokes()


@net1(0x0047E3C0, "NetMenu_EnableArrows", this=WINDOW)
def gen_enable_arrows(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@net1(0x0047E3E0, "NetMenu_AddChat", this=WINDOW)
def gen_add_chat(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    text = env.slab + BUFFERS + 0x100
    img.put(text, bytes(rng.randrange(33, 127) for _ in range(rng.randrange(0, 20))) + b"\0")
    return [rng.choice([0, 1, 2, 3]), text], img.pokes()


@net1(0x0047F1E0, "NetMenu_ConfirmPressed", this=WINDOW)
def gen_confirm(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@net1(0x0047F240, "NetMenu_ReturnFalse")
def gen_return_false(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32)], img.pokes()


@net1(0x0047F820, "NetSummary_Return0")
def gen_summary_ret0(env, n):
    return gen_ret0(env, n)


@net1(0x0047F830, "NetSummary_IsReady", this=WINDOW)
def gen_summary_ready(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    img.u8(env.slab + WINDOW + 0xAC, rng.choice([0, 1, 5]))
    return [], img.pokes()


@net1(0x0047FAC0, "NetMenu_ReturnTrue")
def gen_return_true(env, n):
    return gen_ret0(env, n)


@net1(0x0047FAD0, "NetMenu_BackPressed")
def gen_back_pressed(env, n):
    img, rng = new_image(env)
    net_world(env, img, rng)
    return [], img.pokes()


@net1(0x0047FC70, "NetMenu_DefaultHostName")
def gen_default_host(env, n):
    img, rng = new_image(env)
    net_world(env, img, rng)
    return [], img.pokes()


@net3(0x0047FB20, "NETMENU_InitSummary")
def gen_init_summary(env, n):
    img, rng = new_image(env)
    net_world(env, img, rng)
    img.u32(0x0056AF20, rng.choice([0, 0]))
    return [], img.pokes()


@net3(0x0047FC00, "NetMenu_SummaryUpdate")
def gen_summary_update(env, n):
    img, rng = new_image(env)
    net_world(env, img, rng)
    summary = summary_world(env, img, rng)
    img.u32(0x0056AF20, rng.choice([0, summary]))
    return [], img.pokes()


@net3(0x0047FC20, "NetMenu_CloseSummary")
def gen_close_summary(env, n):
    img, rng = new_image(env)
    net_world(env, img, rng)
    summary = env.slab + SUMMARY
    img.u32(summary, env.slab + VT_SPRITE)            # (the fake sprite vtable's first slot is the virtual delete)
    img.u32(0x0056AF20, rng.choice([0, summary]))
    return [rng.choice([0, 1, 0x100])], img.pokes()


# ---- net2: the lobby ----------------------------------------------------------------------------------------------------------------------------------------------

@net2(0x0047CDC0, "NetMenu_RefreshGames", this=WINDOW)
def gen_refresh_games(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@net2(0x0047E4E0, "NetMenu_UpdateLobby", this=WINDOW)
def gen_update_lobby(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng, state=rng.choice([7, 0xC, 0xD, 2, 0xB, rng.randrange(0, 15)]))
    return [], img.pokes()


@net2(0x0047E770, "NetMenu_Update", this=WINDOW)
def gen_update(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


# ---- net3: construction, destruction, the screen loop and the summary window --------------------------------------------------------------------------------------------

@net3(0x0047CE60, "NetMenu_Construct", this=WINDOW)
def gen_construct(env, n):
    img, rng = new_image(env)
    base = env.slab + WINDOW
    img.put(base, bytes(0xA00))
    net_world(env, img, rng)
    img.u8(0x006A3B22, rng.choice([0, 1]))
    img.u32(0x0056AD10, rng.choice([0, 1]))
    return [], img.pokes()


@net3(0x0047E260, "NetMenu_Destruct", this=WINDOW)
def gen_destruct(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [], img.pokes()


@net3(0x0047E240, "NetMenu_Delete", this=WINDOW)
def gen_delete(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@net3(0x0047F250, "NetSummary_Construct", this=SUMMARY)
def gen_summary_construct(env, n):
    img, rng = new_image(env)
    img.put(env.slab + SUMMARY, bytes(0x360))
    net_world(env, img, rng)
    return [], img.pokes()


@net3(0x0047F790, "NetSummary_Destruct", this=SUMMARY)
def gen_summary_destruct(env, n):
    img, rng = new_image(env)
    summary_world(env, img, rng)
    net_world(env, img, rng)
    return [], img.pokes()


@net3(0x0047F770, "NetSummary_Delete", this=SUMMARY)
def gen_summary_delete(env, n):
    img, rng = new_image(env)
    summary_world(env, img, rng)
    net_world(env, img, rng)
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@net3(0x0047F840, "NetSummary_DrawPlayer", this=SUMMARY)
def gen_summary_draw_player(env, n):
    img, rng = new_image(env)
    summary_world(env, img, rng)
    net_world(env, img, rng)
    img.u32(0x0056E31C, rng.choice([0, 15, 30, 31, 45, 59, 60, 61, 90, rng.randrange(0, 100000)]))
    return [rng.choice([0, 1])], img.pokes()


@net3(0x0047F930, "NetSummary_Draw", this=SUMMARY)
def gen_summary_draw(env, n):
    img, rng = new_image(env)
    summary_world(env, img, rng)
    net_world(env, img, rng)
    img.u32(0x00561C90, rng.randrange(0, 14))
    return [], img.pokes()


@net4(0x0047CBB0, "NetMenu_Main")
def gen_main(env, n):
    img, rng = new_image(env)
    window_world(env, img, rng)
    img.u8(0x0056AD20, 0)
    img.u8(0x0056AD19, 0)
    img.u32(0x0056AD1C, rng.choice([0, 1, 2]))
    DRIVE["iter"] = 0
    DRIVE["limit"] = rng.randrange(0, 5)
    return [rng.choice([0, 1])], img.pokes()


# ---- stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def install_net_stubs(fc, env):
    import os

    from create_specs import SPECS
    from frontend_specs_front import UNDER_TEST
    from frontend_specs_menu import install_menu_stubs
    from opentony.memory import mem

    rng = env.rng
    this_stub = dict(thiscall=True)
    selected = [g for g in os.environ.get("P410_GROUPS", "").split(",") if g]
    UNDER_TEST.clear()
    UNDER_TEST.update(spec_.address for spec_ in SPECS if spec_.group in selected)
    install_menu_stubs(fc, env)
    # the menu probe's stubs that do not fit
    ensure(fc, 0x0046F550, 3, "engine_0046f550", masks={0: 0xFF, 1: 0xFF}, result=lambda a: rng.choice([-1, 0, 1, 2, 3]))
    ensure(fc, FONT_DRAW, 4, "font_draw", convention="thiscall", this=True, text=2, stack_to=SCRATCH_LINE)
    ensure(fc, 0x004AD620, 3, "sfx")
    ensure(fc, WINDOW_UPDATE, 0, "element_update", convention="thiscall", this=True)
    ensure(fc, LIST_V1, 3, "list_v1", convention="thiscall", this=True)
    ensure(fc, LIST_V3, 0, "list_v3", convention="thiscall", this=True)
    ensure(fc, ITEM_V7, 0, "item_v7", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    ensure(fc, ITEM_V8, 1, "item_v8", convention="thiscall", this=True)
    # the network layer
    for address in (0x004E02D0, 0x004DDB70, 0x004DDC00, 0x004DD980, 0x004DDD60, 0x004DDCE0, 0x004DE9C0, 0x004DE2B0, 0x004DE320, 0x004DE420, 0x004DE270, 0x004DFE50, 0x004DFFB0):
        engine(fc, address, 0)
    engine(fc, 0x004DE040, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004DE060, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004DE150, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004DE180, 2)
    engine(fc, 0x004DE570, 1)
    engine(fc, 0x004DDDB0, 1, result=lambda a: rng.choice([0, 0, env.slab + BUFFERS + 0x200 + 0x20 * a[0]]))
    engine(fc, 0x004DDE10, 2, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004DF370, 1, fill=(0, lambda a: b"typed" + bytes(3)))
    engine(fc, 0x004E41B0, 1, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x00432980, 2)
    engine(fc, 0x005009D1, 1, result=lambda a: 1 if (a[0] & 0xFF) in (0x20, 0x09, 0x0A, 0x0B, 0x0C, 0x0D) else 0)
    def edit_box(a):
        k = DRIVE.get("edit", 0)
        DRIVE["edit"] = k + 1
        return child_at(env, 40 + k % 16)

    engine(fc, 0x004DC100, 4, result=edit_box, **this_stub)
    engine(fc, 0x004DC370, 3, **this_stub)
    engine(fc, 0x00454930, 0, **this_stub)
    engine(fc, 0x00454970, 0, **this_stub)
    engine(fc, 0x004E8740, 1)
    engine(fc, 0x004C9300, 1)
    engine(fc, 0x004B2390, 0)
    engine(fc, 0x0048A420, 0)
    engine(fc, 0x00489490, 0)
    engine(fc, 0x0042FC70, 0)
    engine(fc, 0x00418150, 0)
    engine(fc, 0x004F7CE0, 0, writes=[(0x0056E31C, 4, lambda a: (mem.u32(0x0056E31C) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    engine(fc, 0x004B1360, 1, result=lambda a: string_at(env, rng.randrange(0, 8)), **this_stub)
    engine(fc, 0x004B18A0, 1, result=lambda a: rng.randrange(0, 20))
    engine(fc, 0x004B0BC0, 1, **this_stub)
    engine(fc, 0x004B0C50, 0, result=lambda a: rng.randrange(0, 20), **this_stub)
    # the window services (sprites), fonts and files
    ensure(fc, 0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, result=lambda a: env.alloc_log[-1][0])
    ensure(fc, 0x00457120, 0, "sprite_init", convention="thiscall", this=True)
    ensure(fc, 0x00457820, 0, "sprite_width", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    ensure(fc, 0x00457830, 0, "sprite_height", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 300))
    ensure(fc, 0x0044ADA0, 1, "font_load", text=True, result=lambda a: env.slab + FIND_FONTS + FONT_SIZE * rng.randrange(4))
    ensure(fc, 0x0044B0A0, 1, "font_unload", text=True)
    ensure(fc, 0x00448DD0, 2, "file_open", text=True, result=lambda a: 1 + rng.randrange(0x1000))
    ensure(fc, 0x005002FE, 1, "crt_new", result=lambda a: env.alloc(a))
    engine(fc, 0x004D74A0, 3, result=lambda a: env.alloc_log[-1][0], **this_stub)
    engine(fc, 0x004D78C0, 0, **this_stub)
    ensure(fc, 0x005002F3, 1, "crt_delete")
    engine(fc, 0x004D7990, 3, **this_stub)
    engine(fc, 0x004E8730, 4)
    engine(fc, 0x00466C00, 0)
    engine(fc, 0x004F5FF0, 0)
    engine(fc, 0x004DAE00, 0)
    engine(fc, 0x004304B0, 0)
    engine(fc, 0x00466BE0, 0)
    engine(fc, 0x00489A10, 0)
    engine(fc, 0x00430510, 0)
    engine(fc, 0x00489670, 1)
    if "net4" in selected:
        # the screen loop is tested with the window's own methods replaced by seams: the window is a prebuilt fake and its update leaves the done flag
        ensure(fc, 0x00466B70, 1, "operator_new", result=lambda a: env.slab + WINDOW if a[0] == 0x9F8 else env.alloc(a))
        engine(fc, 0x0047CE60, 0, result=lambda a: env.slab + WINDOW, **this_stub)
        engine(fc, 0x0047E770, 0, writes=[(0x0056AD20, 1, lambda a: (tick(), 1 if over() else 0)[1]), (0x0056AD19, 1, lambda a: 0), (0x0056AD1C, 4, lambda a: rng.choice([0, 1, 2]))],
               **this_stub)
        engine(fc, 0x0047E240, 1, **this_stub)
    if "net3" in selected or "net1" in selected:
        pass


GROUP_STUBS = {group: install_net_stubs for group in GROUPS}
