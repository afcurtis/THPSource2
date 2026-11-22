"""Phase 4.14c probe: the case generators of main.cpp (group mn); see core_specs_main.py for the layout, the stubs and the windows."""

from __future__ import annotations

import struct

from core_specs_main import (ARENA, CAM0, CAM1, CONFIG, ENTRY, FONT, LISTOBJ, MATBLOCK, MATERIAL, OT_A, OT_B, OWNER, P0, P1, POLY, PTROBJ, RENDER, REPLAY_A, REPLAY_B, STATE,
                             STREAM_START, TEXT, VTABLE, mn, u)
from frontend_specs_menu import Image

SLOT0 = 0x004E87B0


def r32(rng):
    return rng.choice([0, 1, 2, 3, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x1000), rng.randrange(0, 0x100000)])


def small(rng, limit=8):
    return rng.randrange(0, limit)


def put_words(img, address, values):
    for k, v in enumerate(values):
        img.u32(address + 4 * k, u(v))


def put_random(img, rng, address, size):
    img.put(address, bytes(rng.getrandbits(8) for _ in range(size)))


def begin(env, rng):
    """The scripted answers of the stubs, fresh for each case."""
    slab = env.slab
    STATE["slab"] = slab
    STATE["cheat"] = {number: rng.choice([0, 0, 1]) for number in (3, 4, 7, 9, 0xA, 0xB, 0xD, 0xF, 0x10)}
    STATE["cheat"][8] = rng.choice([0x64, 0x64, 0x64, 0, 1, 0x32, 0xFF])
    STATE["cam"] = rng.getrandbits(16)
    STATE["region"] = rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 7])
    STATE["playing"] = rng.choice([0, 0, 1])
    STATE["material"] = rng.choice([slab + MATERIAL, slab + MATERIAL, slab + MATERIAL, 0])
    STATE["bits"] = rng.choice([4, 8, 4, 8, 16, 0])
    STATE["size"] = rng.choice([0x20, 0x40, 0x80, 0x100])
    STATE["fmv"] = []
    STATE["logo"] = []
    STATE["frames"] = []
    STATE["vmode"] = rng.choice([0, 0, 0, 0, 1, 2, 3, 4, 5, 9])
    STATE["ai"] = rng.choice([0, 0, 1, 0x100])
    STATE["storage"] = []
    STATE["last"] = rng.getrandbits(32)
    STATE["slotres"] = rng.randrange(0, 4)
    STATE["costume"] = rng.getrandbits(32)
    STATE["modelname"] = slab + TEXT + 0x40
    STATE["sprintf"] = bytes(rng.choice(b"abcxyz_09") for _ in range(rng.randrange(3, 7))) + bytes(8 - 0)[:0]
    STATE["sprintf"] = (STATE["sprintf"] + b"\0" * 8)[:8]
    STATE["normal"] = b"".join(struct.pack("<i", rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1000)])) for _ in range(3))
    STATE["counts"] = [rng.randrange(0, 5), rng.randrange(0, 5)]
    STATE["unlock"] = rng.choice([0, 1])
    STATE["goals"] = {number: rng.choice([0, 1]) for number in (1, 3, 6)}
    STATE["used"] = rng.getrandbits(32)
    STATE["otsize"] = rng.choice([1, 2, 3, 4, 5, 7])
    STATE["model"] = rng.choice([slab + MATERIAL, 0x12345678, 0x12345678, 0])
    STATE["record"] = rng.choice([0, 0, 0, 0x1234, 1])
    STATE["selectable"] = rng.choice([0, 1])
    STATE["replay_flag"] = rng.choice([0, 1])
    STATE["replaying"] = rng.choice([0, 0, 0, 1])
    STATE["card_status"] = rng.choice([0, 0, 0, 1])
    STATE["alloc_null"] = False


def player_fields(img, rng, base, slab):
    """Random contents for the captured windows of a player object."""
    from core_specs_main import PLAYER_WINDOWS

    for offset, size in PLAYER_WINDOWS:
        put_random(img, rng, slab + base + offset, size)


def players(img, env, rng, two=None):
    """The player pointers and (random) objects. `two`: force the second player in or out."""
    slab = env.slab
    second = rng.random() < 0.35 if two is None else two
    STATE["second"] = second
    player_fields(img, rng, P0, slab)
    img.u32(0x0056A858, slab + P0)
    if second:
        player_fields(img, rng, P1, slab)
        img.u32(0x0056A85C, slab + P1)
    else:
        img.u32(0x0056A85C, 0)
    img.u32(slab + P0 + 0x2CC0, rng.randrange(0, 4))
    img.u32(slab + P1 + 0x2CC0, rng.randrange(0, 4))
    return second


def screen(img, rng):
    img.u32(0x00527D50, rng.choice([320, 512, 640, rng.randrange(1, 0x800)]))
    img.u32(0x00527D54, rng.choice([240, 480, 640, rng.randrange(1, 0x800)]))
    img.u32(0x029DA394, rng.choice([320, 512, 640, rng.randrange(1, 0x800)]))
    img.u32(0x029DA398, rng.choice([240, 480, 640, rng.randrange(1, 0x800)]))
    img.u32(0x029DA3A0, rng.choice([0, 0, 0, 1, 2, 3]))


def render_world(img, env, rng, equal_tables=None):
    """The render state, its ordering tables and the polygon arena."""
    slab = env.slab
    put_random(img, rng, slab + RENDER, 0x100)
    img.u32(0x00560FD4, slab + RENDER)
    img.u32(slab + RENDER + 0x84, slab + OT_A)
    same = rng.random() < 0.5 if equal_tables is None else equal_tables
    img.u32(slab + RENDER + 0x88, slab + (OT_A if same else OT_B))
    img.u32(slab + RENDER + 0x8C, rng.getrandbits(32))
    img.u32(slab + RENDER + 0xB0, slab + POLY - 8 * rng.randrange(0, 4))
    cursor = slab + POLY + 4 * rng.randrange(0, 0x40)
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, cursor + rng.choice([0x10, 0x2B, 0x2C, 0x2D, 0x60, 0x100, 0x180]))
    for table in (OT_A, OT_B):
        put_words(img, slab + table, [rng.getrandbits(32), 0, rng.getrandbits(32)])
    img.u32(0x00563A38, slab + PTROBJ)
    img.u16(slab + PTROBJ + 8, rng.getrandbits(16))
    img.u32(0x00561150, rng.choice([0, 0x100, 0x400, rng.randrange(0, 0x400)]))
    screen(img, rng)


# ---- the small ones ------------------------------------------------------------------------------------------------------------------------------------------------------------

@mn(0x00466B60, "ResetMainDemoTimer")
def gen_demo_timer(env, n):
    begin(env, env.rng)
    return [], Image().pokes()


@mn(0x00466C00, "PollHost")
def gen_poll_host(env, n):
    begin(env, env.rng)
    return [], Image().pokes()


@mn(0x00466B70, "CClass_OpNew")
def gen_class_new(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x0056A920, r32(rng))
    return [rng.choice([1, 3, 4, 5, 0x20, 0x40, 0x7F, 0x100, rng.randrange(1, 0x200)])], img.pokes()


@mn(0x00466BB0, "CClass_OpDelete")
def gen_class_delete(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x0056A920, r32(rng))
    img.u32(0x0056A970, r32(rng))
    return [env.slab + ARENA + 0x100], img.pokes()


@mn(0x00466BE0, "CalcPolyBufferEnd")
def gen_poly_end(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    render_world(img, env, rng)
    return [], img.pokes()


@mn(0x00466DE0, "Display_DrawDimOverlay")
def gen_dim_overlay(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    render_world(img, env, rng)
    img.u32(0x0056A924, rng.choice([0, 4, 0x20, 0x24, 0x28, 0x2C, 0x100, 0xFFFFFFFF]))
    img.u32(env.slab + OT_A + 8, rng.getrandbits(32))
    return [], img.pokes()


@mn(0x00468350, "Display_AddQuad")
def gen_add_quad(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    render_world(img, env, rng)
    values = [u(rng.choice([0, 1, -1, 0x7FFF, -0x8000, rng.randrange(-0x400, 0x400), rng.randrange(-0x8000, 0x8000)])) for _ in range(8)]
    return values + [rng.getrandbits(32)], img.pokes()


@mn(0x004684F0, "Display_AddRect")
def gen_add_rect(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    render_world(img, env, rng)
    return [u(rng.randrange(-100, 700)) for _ in range(4)], img.pokes()


PRIMITIVE_CODES = [0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x50, 0x68, 0xB0]


@mn(0x00467610, "FlipPrimitives")
def gen_flip_primitives(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    render_world(img, env, rng, equal_tables=False)
    count = STATE["otsize"]
    start = slab + OT_A + 8 * count - 8
    nodes = [start] + [slab + OT_A + 0x80 + 0x80 * k for k in range(rng.choice([0, 1, 2, 3]))]
    for k, node in enumerate(nodes):
        size = 0x80 if k else 0x48
        put_random(img, rng, node, size)
        img.u32(node, nodes[k + 1] if k + 1 < len(nodes) else 0)
        img.u32(node + 4, rng.choice([0, 1, 0x1234, rng.getrandbits(32)]))
        code = rng.choice(PRIMITIVE_CODES * 3 + [0x20 + 4 * k for k in range(37)] + [rng.getrandbits(8)])
        img.u8(node + 7, code)
        if code == 0xB0:
            img.u32(node + 0x10, rng.choice([0, 1, 5]))
            img.u32(node + 0x14, rng.choice([0, 1, 2]))
    img.u32(0x029DA394, rng.choice([320, 640, rng.randrange(1, 0x10000)]))
    return [], img.pokes()


@mn(0x00468B00, "FPS_Init")
def gen_fps_init(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x0056E320, r32(rng))
    return [], img.pokes()


def clock_world(img, env, rng):
    slab = env.slab
    now = rng.choice([0, 100, 5000, rng.getrandbits(32), rng.randrange(0, 100000)])
    img.u32(0x0056E320, now)
    img.u32(0x00568604, u(now - rng.choice([0, 1, 2, 3, 5, 10, 100, 20000, now + 1])))
    img.u32(0x00561C04, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A934, rng.randrange(0, 3))
    put_words(img, 0x0056868C, [rng.choice([0, 1, 2, 3, 5, 20, 100]) for _ in range(3)])
    img.u32(0x0056A944, r32(rng))
    img.u32(0x0056A948, rng.choice([0, 0, 1]))
    img.u8(0x006A3D26, rng.choice([0, 0, 1]))
    img.u32(0x00568810, r32(rng))
    img.u32(0x005685F4, r32(rng))
    img.u32(0x0056A868, rng.choice([0, 0, 1]))
    img.u32(0x0056A93C, r32(rng))
    img.u32(0x0056865C, r32(rng))
    img.u32(0x00568808, r32(rng))
    img.u32(0x00568804, r32(rng))
    img.u32(0x0056A940, r32(rng))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 9, 9, 10]))
    put_random(img, rng, slab + OWNER, 0x40)
    players(img, env, rng)
    img.u32(0x0056DB60, slab + P1)          # (the versus record: its +0x2cc8 is in the player window)
    img.u32(slab + P0 + 0x30B8, rng.choice([0, 1, 5]))
    img.u32(slab + P0 + 0x2DD4, rng.choice([0, 0, 1]))


@mn(0x00468B30, "AnimationClock_Update")
def gen_clock_update(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    clock_world(img, env, rng)
    return [], img.pokes()


# ---- the input and the frame ends ----------------------------------------------------------------------------------------------------------------------------------------------

@mn(0x004699F0, "Game_InputUpdate")
def gen_input_update(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x00568A58, rng.choice([0, 0, 1]))
    img.u32(0x00568A5C, rng.choice([0, 0, 1]))
    return [], img.pokes()


@mn(0x00469D30, "CheckSkatersStop")
def gen_check_stop(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    players(img, env, rng)
    img.u32(0x00568658, rng.choice([0, 1, 1, 1, 2]))
    img.u32(0x00561C04, rng.choice([0, 0, 0, 1]))
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    for base in (P0, P1):
        img.u32(slab + base + 0x30B8, rng.choice([0, 7, 1, 3]))
        img.u32(slab + base + 0x2DD4, rng.choice([0, 0, 1]))
    for address in (0x0056B018, 0x0056B0A8, 0x0056B184, 0x0056B214):
        img.u32(address, rng.getrandbits(32))
    return [], img.pokes()


def message_world(env, rng, img):
    img.u32(0x0056A868, rng.choice([0, 0, 1]))
    img.u32(0x0056A938, r32(rng))
    img.u8(0x00561990, rng.choice([0, 1]))
    img.u32(0x0056A898, rng.randrange(0, 4))
    img.put(0x0053C290 + 0x1AC * 0, b"")
    for level in range(4):
        img.u32(0x0053C290 + 0x1AC * level, rng.choice([0, 0, 1, 5]))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x00533F38, rng.choice([0, 1, 4, 5, 7, 9, 10]))
    img.u32(0x00563A4C, r32(rng))


@mn(0x0046A1A0, "MainLoop_End")
def gen_loop_end(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    render_world(img, env, rng)
    tick = rng.choice([0, 5, 0xFFFFFFFF, rng.getrandbits(32)])
    img.u32(0x0056E31C, tick)
    img.u32(0x00568624, rng.choice([tick, u(tick - 1), u(tick + 1), rng.getrandbits(32)]))
    return [], img.pokes()


@mn(0x0046A250, "Level_Teardown")
def gen_teardown(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x0056A94C, rng.choice([0, 0, 1, 2, 5]))
    img.u32(0x00533F38, rng.choice([0, 1, 1, 1, 7]))
    img.u32(0x0056A898, rng.choice([0, 2, 5, 7, 7, 3]))
    img.u32(0x00537C80, r32(rng))
    img.u32(0x00534110, rng.choice([0x30, 0x30, 7, 7, 5, 0x16, rng.getrandbits(8)]))
    img.u32(0x00561BFC, r32(rng))
    img.u32(0x005615D8, rng.choice([0, 0, env.slab + ARENA + 0x800]))
    img.u32(0x0056152C, rng.randrange(0, 0x1000))
    return [], img.pokes()


@mn(0x0046A500, "IntroMovies")
def gen_intro(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    STATE["fmv"] = [(rng.choice([0, 0, 1]), rng.choice([0, 1]), rng.choice([0, 1])) for _ in range(3)]
    for address in (0x0056B0D8, 0x0056B038, 0x0056B048):
        img.u8(address, rng.getrandbits(8))
    return [], img.pokes()


@mn(0x0046A580, "LogoScreen")
def gen_logo(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    STATE["logo"] = [tuple(rng.choice([0, 0, 0, 1]) for _ in range(4)) for _ in range(3)]
    for address in (0x0056B0D9, 0x0056B029, 0x0056B245, 0x0056B195):
        img.u8(address, rng.getrandbits(8))
    return [], img.pokes()


@mn(0x0046A600, "Game_MessageScreen")
def gen_message_screen(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    img.u32(0x0056114C, r32(rng))
    put_random(img, rng, 0x005619E0, 0x14C)
    return [0x005619E0, env.slab + TEXT], img.pokes()


@mn(0x00469910, "Game_Init")
def gen_game_init(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    level_world(img, env, rng)
    return [], img.pokes()


def level_world(img, env, rng):
    """What PlayAway_Init, the replay set-up and Game_Init share."""
    slab = env.slab
    players(img, env, rng)
    img.u32(0x00533F3C, rng.choice([1, 1, 2, 2, 0, 3]))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 9]))
    for address in (0x0056E774, 0x0056E5D8, 0x00568658, 0x0056A8D0, 0x0056DB8C, 0x0056E310, 0x00568614):
        img.u32(address, rng.getrandbits(32))
    img.u32(0x0056E774, rng.choice([0, 0, 0, 1, 2, -1, 3]) & 0xFFFFFFFF)
    img.u32(0x0056E5DC, rng.choice([0, slab + REPLAY_A]))
    img.u32(0x0056E5E0, rng.choice([0, slab + STREAM_START]))
    img.u32(0x0056E320, r32(rng))
    img.u32(0x0056DB8C, rng.choice([0, 1]))
    put_random(img, rng, slab + STREAM_START, 0x50)
    put_random(img, rng, 0x0056E624, 0x34)
    for address in (0x00533FB4, 0x00533FCC):
        img.u16(address, rng.getrandbits(16))


@mn(0x00468CF0, "PlayAway_Init")
def gen_play_away(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    level_world(img, env, rng)
    STATE["alloc_null"] = rng.random() < 0.15
    return [], img.pokes()


# ---- the bitmap and the appearance ------------------------------------------------------------------------------------------------------------------------------------------

def material_world(img, env, rng):
    slab = env.slab
    put_random(img, rng, slab + MATERIAL, 0x20)
    img.u32(slab + MATERIAL + 0x1C, slab + MATBLOCK)
    img.put(slab + MATBLOCK, bytes(rng.getrandbits(8) for _ in range(0x10)))
    img.u8(slab + MATBLOCK + 3, rng.choice([0, 2, 0xFF, 1, rng.getrandbits(8)]))
    img.u8(0x0056DB40, rng.choice([0, 1]))


@mn(0x004674D0, "Skater_LoadBitmap")
def gen_load_bitmap(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    material_world(img, env, rng)
    img.put(env.slab + TEXT, b"".join([bytes(rng.choice(b"abc_01g") for _ in range(rng.randrange(2, 8))), b"\0"]))
    return [rng.getrandbits(32), env.slab + TEXT], img.pokes()


@mn(0x00468E90, "Skater_SetupAppearance")
def gen_setup_appearance(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    players(img, env, rng)
    material_world(img, env, rng)
    skater = rng.choice([P0, P1])
    img.u32(slab + skater + 0x2CC0, rng.randrange(0, 4))
    slot = rng.randrange(0, 2)
    name = bytes(rng.choice(b"abcdXYZ_09") for _ in range(rng.randrange(3, 10)))
    img.put(slab + TEXT + 0x40, name + b"\0")
    img.put(slab + TEXT, name + b"\0")
    for character in range(4):
        img.u32(0x0053A514 + 0x170 * character, rng.choice([0, 2, 3, 0x100, rng.getrandbits(32)]))
    img.u32(0x00568648 + 4 * slot, rng.choice([0, 0x00DB0010]))
    img.u8(0x0056A688 + slot, rng.getrandbits(8))
    img.u8(0x0056A2DC + slot, rng.getrandbits(8))
    img.u16(0x0056B8D4, rng.choice([0, 0, 1, 2]))
    img.u32(0x0056B8F4 + 4 * slot, r32(rng))
    img.u32(0x0056B8EC + 4 * slot, r32(rng))
    img.u32(slab + ENTRY, r32(rng))
    return [slot, slab + skater, slab + TEXT + 0x40], img.pokes()


@mn(0x004691E0, "SkaterObjects_Create")
def gen_create_objects(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    players(img, env, rng)
    material_world(img, env, rng)
    img.u32(0x00533F3C, rng.choice([1, 1, 2, 2]))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 10, 7, 9]))
    img.u32(0x0056A854, rng.choice([0, 1]))
    img.put(slab + TEXT + 0x40, b"abcd\0")
    for character in range(4):
        img.u32(0x0053A514 + 0x170 * character, rng.choice([0, 2, 3, 0x100, rng.getrandbits(32)]))
    for slot in range(2):
        img.u32(0x00568648 + 4 * slot, rng.choice([0, 0x00DB0010]))
        img.u8(0x0056A688 + slot, rng.getrandbits(8))
        img.u8(0x0056A2DC + slot, rng.getrandbits(8))
    img.u16(0x0056B8D4, rng.choice([0, 0, 1]))
    img.u32(0x0056A898, rng.choice([8, 3, 8]))
    img.u32(0x0056E774, rng.choice([0, 0, 1]))
    img.u32(slab + ENTRY, r32(rng))
    return [], img.pokes()


# ---- the replay ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@mn(0x00469400, "Replay_ApplyLoadedHeaderAndRestart")
def gen_apply_header(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    img.u8(0x00561990, rng.choice([0, 1, 1, 1]))
    img.u32(0x0056E5DC, slab + REPLAY_A)
    img.u32(0x0056E454, slab + REPLAY_B)
    put_random(img, rng, slab + REPLAY_A, 0x280)
    img.u16(slab + REPLAY_A + 0x2C, rng.choice([1, 2, 2, 1, 0, 3, 0xFFFF]))
    put_random(img, rng, slab + REPLAY_B, 0x30)
    img.u8(0x0052BE24, rng.choice([0x41, 0x49, 0x4A, 0x40, 0x7F, 0x80, rng.getrandbits(8)]))
    img.u32(0x0056E774, rng.getrandbits(32))
    img.u16(0x0056B8D4, rng.getrandbits(16))
    img.u32(0x0056A8D0, rng.getrandbits(32))
    put_random(img, rng, 0x0056E62C, 0x28)
    put_random(img, rng, 0x0056E4A4, 0x28)
    return [], img.pokes()


def masks_world(img, env, rng):
    slab = env.slab
    img.u32(0x0056E774, rng.choice([0, 0, 1, -1 & 0xFFFFFFFF]))
    img.u32(0x0056E5E0, rng.choice([0, slab + STREAM_START]))
    put_random(img, rng, slab + STREAM_START, 0x50)
    img.u32(0x00533F38, rng.choice([1, 1, 0, 7]))
    img.u32(0x0056A898, rng.randrange(0, 15))
    put_random(img, rng, slab + CONFIG, 0x100)
    for address in (0x00568608, 0x0056860C, 0x0056E5D8):
        img.u32(address, rng.getrandbits(32))


@mn(0x004695A0, "Game_PrepareVideoMasks")
def gen_masks(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    masks_world(img, env, rng)
    return [], img.pokes()


@mn(0x00469900, "Game_PrepareVideoMasksWrapper")
def gen_masks_wrapper(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    masks_world(img, env, rng)
    return [], img.pokes()


@mn(0x00469700, "Game_ConfigureReplayStreams")
def gen_configure_streams(env, n):
    img, rng = Image(), env.rng
    begin(env, rng)
    slab = env.slab
    level_world(img, env, rng)
    img.u32(0x0056E774, rng.choice([0, 1, 2, 3, 5, -1 & 0xFFFFFFFF, -3 & 0xFFFFFFFF]))
    put_random(img, rng, 0x0056E624, 0x34)
    if rng.random() < 0.1:
        img.u32(0x0056E774, 0)
        img.u32(0x0056A858, 0)          # (only the recording branch tolerates a missing player: it asserts, the others read through it)
    return [], img.pokes()


# ---- the display ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def display_world(img, env, rng):
    slab = env.slab
    begin(env, rng)
    render_world(img, env, rng)
    players(img, env, rng)
    message_world(env, rng, img)
    clock_world(img, env, rng)
    img.u32(0x0056A8CC, rng.choice([0, 0, env.slab + TEXT]))
    img.u32(0x0056A86C, rng.choice([0, 0, 1]))
    img.u32(0x0056A8E0, rng.choice([0, 0, 1]))
    img.u32(0x0056A900, rng.choice([0, 0, 1]))
    img.u32(0x0056A860, rng.choice([0, 1]))
    img.u8(0x0056B0E8, rng.choice([0, 1]))
    img.u32(0x00534084, rng.choice([0, 1]))
    img.u8(0x0056B255, rng.choice([0, 1]))
    img.u32(0x00533F3C, rng.choice([1, 1, 2]))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 9, 9, 10]))
    img.u32(0x0056A868, rng.choice([0, 0, 1]))
    img.u32(0x00561C04, rng.choice([0, 0, 1]))
    img.u32(0x00561BFC, rng.choice([0, 0x2A, 5]))
    img.u32(0x0055FA38, rng.getrandbits(32))
    for address in (0x0056DB28, 0x0056AF40, 0x0055F6BC, 0x0056B830, 0x0055F6B0, 0x0056A8C4):
        img.u32(address, rng.getrandbits(32))
    img.u32(0x006A3D14, 1 if STATE["second"] and rng.random() < 0.3 else 0)          # (the viewport index of the single layout)
    for base in (P0, P1):
        for off in (0x2DD4, 0x3210, 0x2F14, 0x31F0, 0x2F64):
            img.u32(slab + base + off, rng.choice([0, 1, rng.getrandbits(32)]))
    # the stage
    img.u32(0x0052870C, rng.choice([0, rng.getrandbits(32), 0x7FFFFFFF, 0x80000000]))
    img.u32(0x005613BC, rng.getrandbits(32))
    for base in (P0, P1):
        img.u32(slab + base + 0xC, rng.choice([0, rng.getrandbits(32), 0x7FFFFFFF, 0x80000000]))
    for character in range(4):
        img.u32(0x0053A514 + 0x170 * character, rng.choice([0, 0x100, 0x1FF, rng.getrandbits(32)]))
    put_random(img, rng, slab + OWNER, 0x40)
    img.u32(0x0056A960, slab + OWNER)
    for base in (P0, P1):
        img.u8(slab + base + 0xDC, rng.choice([0, 0, 1, 7]))
        img.u32(slab + base + 0x30B8, rng.choice([0, 1, 7]))
        img.u16(slab + base + 0x9C, rng.getrandbits(16))
        img.u16(slab + base + 0x9E, rng.choice([0, 1, 0x100, rng.getrandbits(16)]))
        img.u16(slab + base + 0xA0, rng.getrandbits(16))
        img.u32(slab + base + 0x317C, rng.choice([0, 255, 128, rng.getrandbits(32), 0x7FFFFFFF, 0x80000000, 300]))
        for off in range(0x2E58, 0x2E6A, 2):
            img.u16(slab + base + off, rng.choice([0, 0x1000, rng.getrandbits(16)]))
        img.u32(slab + base + 0x29B0, slab + (CAM0 if base == P0 else CAM1))
    for cam in (CAM0, CAM1):
        put_random(img, rng, slab + cam + 8, 0x10)
    img.u32(0x0056A88C, rng.choice([0, 0, 1]))
    img.u32(0x0056A8C4, rng.getrandbits(32))
    img.u8(0x0056437C, rng.getrandbits(8))
    img.u32(0x00564378, rng.getrandbits(32))
    img.u32(0x0056DB5C, rng.choice([slab + P0, slab + P1, 0]))
    img.u32(0x0056DB60, slab + P1)
    img.u32(0x0056DB64, rng.choice([2, 2, 0, 1]))
    img.u32(0x00568614, slab + PTROBJ)
    put_random(img, rng, slab + PTROBJ, 0x20)
    put_random(img, rng, 0x006A3E00, 0x120)
    STATE["playing"] = rng.choice([0, 0, 1])
    if not STATE["second"] and STATE["vmode"] in (1, 2, 3, 4):
        STATE["vmode"] = 0          # (the split-screen layouts draw the second player; decided last, after every call that rolls the players)
    # the ordering tables hold primitives for the flip cheat
    count = rng.choice([1, 2, 3, 4])
    STATE["otsize"] = count
    node = slab + OT_A + 8 * count - 8
    put_random(img, rng, node, 0x40)
    img.u32(node, 0)
    img.u8(node + 7, rng.choice([0x20, 0x68, 0x28, 0x50, 0x01]))
    img.u32(node + 4, 1)


@mn(0x00468520, "Display")
def gen_display(env, n):
    img, rng = Image(), env.rng
    display_world(img, env, rng)
    return [], img.pokes()


@mn(0x00467C90, "M3D_RenderStage")
def gen_render_stage(env, n):
    img, rng = Image(), env.rng
    display_world(img, env, rng)
    return [rng.getrandbits(32), 1 if STATE["second"] and rng.random() < 0.4 else 0], img.pokes()


@mn(0x0046A0F0, "Game_Display")
def gen_game_display(env, n):
    img, rng = Image(), env.rng
    display_world(img, env, rng)
    return [], img.pokes()


@mn(0x0046A3A0, "Game_LevelLoop")
def gen_level_loop(env, n):
    img, rng = Image(), env.rng
    level_world(img, env, rng)
    display_world(img, env, rng)          # (after the level world: it decides whether a second player exists, which the layout index depends on)
    STATE["frames"] = [rng.choice([0, 0, 0]) for _ in range(rng.choice([0, 1, 2]))] + [rng.choice([7, 8, 9])]
    img.u32(0x0056A8D0, 0)
    img.u32(0x0056A94C, rng.choice([0, 0, 2]))
    img.u32(0x005615D8, rng.choice([0, 0, env.slab + ARENA + 0x800]))
    img.u32(0x0056152C, rng.randrange(0, 0x1000))
    img.u32(0x00534110, rng.choice([0x30, 0x30, 7, 5]))
    img.u32(0x00568614, rng.choice([0, env.slab + PTROBJ]))
    img.put(env.slab + PTROBJ, struct.pack("<I", env.slab + VTABLE))
    img.put(env.slab + LISTOBJ, struct.pack("<I", env.slab + VTABLE))          # (the pointer object PlayAway_Init makes is the storage it asks for)
    img.put(env.slab + VTABLE, struct.pack("<II", SLOT0, 0x004E87D0))
    img.u32(0x00561BFC, rng.choice([0x16, 5, 0]))
    return [], img.pokes()
