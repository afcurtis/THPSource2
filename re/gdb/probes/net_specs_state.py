"""Phase 4.13b2 probe, part 2: the network state, its flags, queues and small queries (group nets). See net_specs.py for the worlds, the slab layout and the stubs."""

from __future__ import annotations

import struct

from frontend_specs_menu import Image
from net_specs import (BITS, CONFIG, DPREC, IDS, IMAGE, LOCAL, REMOTE, STRINGS, net_world, nets, put_string, reset_state, string_at, u)

RING = 0x006A1560              # the shared ring of 16 slots (head at +0, slots from +8)
POINTS = 0x006A16B0            # tables of sixteen {key, value} pairs (head +0, count +8, pairs from +0xc)
TAGS = 0x006A39B0


def world(env, flag=None, host=None, lost=None):
    img, rng = Image(), env.rng
    net_world(img, env, rng, flag=flag, host=host, lost=lost)
    return img, rng


def table_world(img, rng, table, *, full=False):
    """A table of pairs: some filled slots, a head and a count (the count is whatever the table says, it is not checked)."""
    filled = 16 if full else rng.choice([0, 1, 2, 5, 15, 16])
    # (a head outside 0..15 is masked like retail does; the odd ones below stay inside the table: -1 reads its second word, nothing reaches wild memory)
    img.u32(table, rng.choice([0, 1, 7, 15, 16, 17, 0x8000000F, 0xFFFFFFFF]) if rng.random() < 0.25 else rng.randrange(0, 16))
    img.u32(table + 4, rng.getrandbits(32))
    img.u32(table + 8, rng.choice([0, filled, rng.randrange(0, 40)]))
    slots = rng.sample(range(16), filled) if filled < 16 else list(range(16))
    for k in range(16):
        if k in slots:
            img.u32(table + 0xC + 8 * k, rng.choice([1, 2, 3, 0x1234, rng.getrandbits(32) | 1]))
            img.u32(table + 0x10 + 8 * k, rng.choice([0, 1, 2, 77, rng.getrandbits(32)]))
        else:
            img.u32(table + 0xC + 8 * k, 0)
            img.u32(table + 0x10 + 8 * k, rng.getrandbits(32))


def a_table(rng):
    return rng.choice([POINTS, TAGS])


@nets(0x004DD890, "NetGame_Reset")
def gen_reset(env, n):
    img, rng = world(env)
    return [rng.getrandbits(32)], img.pokes()


@nets(0x004DDB70, "NetGame_Close")
def gen_close(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DDBE0, "NetGame_Nothing")
def gen_nothing(env, n):
    return [], {}


@nets(0x004DDC00, "NetGame_LoadSettings")
def gen_load_settings(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DDCE0, "NetGame_SaveSettings")
def gen_save_settings(env, n):
    img, rng = world(env)
    return [], img.pokes()


def record_world(img, env, rng):
    slab = env.slab
    pass_text = rng.choice(["", "secret", "Secret", "192.168.0.1", "other"])
    img.u32(slab + DPREC + 0x38, put_string(img, env, 3, rng.choice(["Tony's game", "host", "x", ""])))
    img.u32(slab + DPREC + 0x3C, rng.choice([0, put_string(img, env, 4, pass_text), put_string(img, env, 4, pass_text)]))
    if rng.random() < 0.5:
        img.put(0x006A3BB1, pass_text.encode("latin-1") + b"\0")


@nets(0x004DDDB0, "NetGame_ResolveSessionName")
def gen_resolve(env, n):
    img, rng = world(env)
    record_world(img, env, rng)
    img.u8(0x006A3B26, rng.choice([0, 1, 1, 1]))
    return [rng.getrandbits(32)], img.pokes()


@nets(0x004DDE10, "NetGame_Connect")
def gen_connect(env, n):
    img, rng = world(env)
    record_world(img, env, rng)
    img.u8(0x006A3B26, rng.choice([0, 1, 1, 1]))
    return [rng.getrandbits(32), string_at(env, 20)], img.pokes()


def a_skater(env, rng):
    return env.slab + rng.choice([LOCAL, REMOTE])


@nets(0x004DDF30, "Skater_RegisterPlayer")
def gen_register(env, n):
    img, rng = world(env)
    skater = a_skater(env, rng)
    img.u32(skater + 0x2CC4, rng.choice([0, 1, 1, 0, 2]))
    return [skater], img.pokes()


@nets(0x004DE000, "NetGame_ClearPlayers")
def gen_clear_players(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE010, "NetGame_IsRemotePlayer")
def gen_is_remote(env, n):
    img, rng = world(env)
    skater = a_skater(env, rng)
    if rng.random() < 0.6:
        img.u32(skater + 0x2CC4, rng.choice([0, 1]))
        img.u32(0x006A3D2C, rng.choice([0, 1]))
    return [skater], img.pokes()


@nets(0x004DE040, "NetGame_IsHost")
def gen_is_host(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE060, "NetGame_IsGuest")
def gen_is_guest(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE0A0, "NetGame_PauseState")
def gen_pause_state(env, n):
    img, rng = world(env)
    return [rng.choice([0, 1, 0xFF, 0x100, 0x0F, rng.getrandbits(32)]), rng.choice([0, 1, 5, 0x100])], img.pokes()


@nets(0x004DE150, "NetGame_ModeUsesNet")
def gen_mode_uses_net(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE170, "NetGame_AbortRequested")
def gen_abort(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE180, "NetGame_SetCustomLevel")
def gen_set_custom_level(env, n):
    img, rng = world(env)
    name = put_string(img, env, 5, rng.choice(["Park One", "x", "A" * 30, "My Park"]))
    return [rng.choice([0x64, 0x64, 0, 1, 3, 7, u(-1)]), rng.choice([name, name, 0])], img.pokes()


@nets(0x004DE270, "NetGame_RestoreLevel")
def gen_restore_level(env, n):
    img, rng = world(env, host=rng_host(env))
    img.u8(0x006A3ABC, env.rng.choice([0, 1, 1]))
    return [], img.pokes()


def rng_host(env):
    return env.rng.choice([0, 1, 1])


@nets(0x004DE2B0, "NetGame_LoadCustomLevel")
def gen_load_custom_level(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE300, "NetGame_GuestValue")
def gen_guest_value(env, n):
    img, rng = world(env)
    return [rng.getrandbits(32)], img.pokes()


@nets(0x004DE320, "NetGame_StashCareerState")
def gen_stash(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DE420, "NetGame_RestoreCareerState")
def gen_restore_career(env, n):
    img, rng = world(env)
    return [], img.pokes()


def mask_value(rng):
    return rng.choice(BITS + [0x1000, 0x20000, rng.getrandbits(32), 0])


def on_value(rng):
    return rng.choice([0, 1, 1, 0xFF, 0x100, 0x101, rng.getrandbits(32)])


@nets(0x004DE4D0, "NetGame_SetLocalFlag")
def gen_set_local_flag(env, n):
    img, rng = world(env)
    return [mask_value(rng), on_value(rng)], img.pokes()


@nets(0x004DE500, "NetGame_SetRemoteFlag")
def gen_set_remote_flag(env, n):
    img, rng = world(env)
    return [mask_value(rng), on_value(rng)], img.pokes()


@nets(0x004DE570, "NetGame_SetLocalState")
def gen_set_local_state(env, n):
    img, rng = world(env)
    return [rng.choice([0, 5, 7, 0x16, rng.getrandbits(32)])], img.pokes()


@nets(0x004DE5C0, "NetGame_TagAcknowledged")
def gen_tag_ack(env, n):
    img, rng = world(env)
    return [rng.choice([env.slab + LOCAL, env.slab + LOCAL, env.slab + REMOTE, 0x1234, 0])], img.pokes()


@nets(0x004DE5F0, "NetGame_SpawnHitSparks")
def gen_sparks(env, n):
    img, rng = world(env)
    img.u32(0x0055F72C, rng.choice([0, 100, 0xF9, 0xFA, 0xFB, 0x200, u(-5), 0x7FFFFFFF, 0x80000000]))
    return [], img.pokes()


@nets(0x004DE7C0, "NetGame_PlayerTagged")
def gen_player_tagged(env, n):
    img, rng = world(env)
    first = rng.choice([env.slab + LOCAL, env.slab + REMOTE])
    return [first, env.slab + (REMOTE if first == env.slab + LOCAL else LOCAL), rng.choice([0, 1, 0x100, rng.getrandbits(32)])], img.pokes()


@nets(0x004DE9C0, "NetGame_InputReset")
def gen_input_reset(env, n):
    img, rng = world(env)
    return [], img.pokes()


@nets(0x004DF030, "NetGame_DrawStatusText")
def gen_draw_status(env, n):
    img, rng = world(env)
    if rng.random() < 0.6:
        img.u32(0x00561C74, rng.choice([5, 8, 9]))
    return [], img.pokes()


@nets(0x004DF160, "NetGame_NothingLog")
def gen_nothing_log(env, n):
    return [], {}


def ring_world(img, rng, ring=RING):
    img.u32(ring, rng.choice([0, 3, 15, 16, 17, 0xFFFFFFFF]) if rng.random() < 0.15 else rng.randrange(0, 16))
    img.u32(ring + 4, rng.getrandbits(32))
    chance = rng.choice([0.0, 0.1, 0.5, 0.9, 1.0])
    for k in range(16):
        img.u32(ring + 8 + 4 * k, rng.choice([1, 2, 0x1234, rng.getrandbits(32) | 1]) if rng.random() < chance else 0)


@nets(0x004DF1D0, "NetRing_Pop")
def gen_ring_pop(env, n):
    img, rng = world(env)
    ring_world(img, rng)
    return [RING, rng.choice([0, 1, 0xFF, 0x100, rng.getrandbits(32)])], img.pokes()


@nets(0x004DF220, "NetRing_Remove")
def gen_ring_remove(env, n):
    img, rng = world(env)
    ring_world(img, rng)
    value = rng.choice([0, 1, 2, 0x1234, rng.getrandbits(32)])
    if rng.random() < 0.6:
        img.u32(RING + 8 + 4 * rng.randrange(0, 16), value)
    return [RING, value], img.pokes()


@nets(0x004DF250, "NetRing_Push")
def gen_ring_push(env, n):
    img, rng = world(env)
    return [rng.getrandbits(32)], img.pokes()


@nets(0x004DF280, "NetRing_Clear")
def gen_ring_clear(env, n):
    img, rng = world(env)
    ring_world(img, rng)
    return [RING], img.pokes()


@nets(0x004DF2C0, "NetGame_OnPoint")
def gen_on_point(env, n):
    img, rng = world(env)
    table_world(img, rng, POINTS)
    packet = string_at(env, 6)
    img.u32(packet + 4, rng.choice([1, 2, 3, 0x1234, rng.getrandbits(32)]))
    player = rng.choice([env.slab + LOCAL, env.slab + LOCAL, env.slab + REMOTE])
    return [rng.getrandbits(32), player, rng.choice([0, 1, 5, 77, rng.getrandbits(32)]), packet], img.pokes()


@nets(0x004DF300, "NetTable_Add")
def gen_table_add(env, n):
    img, rng = world(env)
    table = a_table(rng)
    table_world(img, rng, table, full=rng.random() < 0.2)
    key, value = rng.choice([1, 2, 3, 0x1234, rng.getrandbits(32)]), rng.choice([0, 1, 2, 77, rng.getrandbits(32)])
    if rng.random() < 0.3:
        slot = rng.randrange(0, 16)
        img.u32(table + 0xC + 8 * slot, key)
        img.u32(table + 0x10 + 8 * slot, value)
    return [table, key, value], img.pokes()


@nets(0x004DFA00, "NetTable_Take")
def gen_table_take(env, n):
    img, rng = world(env)
    table = a_table(rng)
    table_world(img, rng, table)
    return [table, rng.choice([0x006A1874, 0x006A187C]), rng.choice([0, 1, 0xFF, 0x100])], img.pokes()


@nets(0x004DFDA0, "NetTable_Remove")
def gen_table_remove(env, n):
    img, rng = world(env)
    table = a_table(rng)
    table_world(img, rng, table)
    key, value = rng.choice([1, 2, 3, 0x1234, rng.getrandbits(32)]), rng.choice([0, 1, 2, 77, rng.getrandbits(32)])
    if rng.random() < 0.6:
        slot = rng.randrange(0, 16)
        img.u32(table + 0xC + 8 * slot, key)
        img.u32(table + 0x10 + 8 * slot, value)
    return [table, key, value], img.pokes()


@nets(0x004E00B0, "NetGame_NothingB")
def gen_nothing_b(env, n):
    return [], {}


@nets(0x004E0170, "NetGame_SendStateIfFive")
def gen_send_if_five(env, n):
    img, rng = world(env)
    return [rng.choice([5, 5, 4, 0, 6, rng.getrandbits(32)])], img.pokes()
