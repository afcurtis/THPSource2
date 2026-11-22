"""Phase 4.10h probe, part 2: the case generators of the competition, the medals, the tapes and the tag game (see frontend_specs_goals.py for the groups, the regions and the stubs;
the window groups' generators are in frontend_specs_goals_win.py).

These groups start from the 4.8 probe's career sampler (career_specs.base_state, career_specs_competition.competition_state: the career records, the skater manager, the level, the
players' scores and the competition object in a legal state); the generators add what the displays read (the medal objects, the counters, the marquee text, the camera, the draw
list).  The medal objects are fake objects in the screens' slab whose virtual methods are reported services (a fake vtable with the delete and the update).
"""

from __future__ import annotations

import struct

import career_specs as cspecs
import career_specs_competition as ccomp
import career_state as cs
from create_specs import spec
from frontend_specs_goals import FAKE_PLAYER, VT_MEDAL
from frontend_specs_screens import OBJ, RENDERER, TEXTS, TEXT_POOL, TEXT_STRIDE
from frontend_specs_skwin import A, new_image

OBJECT = 0x0055FE28
CURRENT_LEVEL = 0x0056A898
PLAYERS = 0x0056A858
GAME_MODE = 0x00533F38
HUMAN_MODE = 0x00560080
MENU_CELL = 0x00561C1C            # the pause menu object the competition sets a line on


def gcmp(address, name, this=None):
    return spec(address, name, "gcmp", this=this)


def gdisp(address, name, this=OBJECT):
    return spec(address, name, "gcdisp", this=this)


def gmdl(address, name, this=None):
    return spec(address, name, "gmdl", this=this)


def gtag(address, name):
    return spec(address, name, "gtag")


# ---- the career world -------------------------------------------------------------------------------------------------------------------------------------

def cenv(env):
    """The 4.8 probe's view of the running game (the contest levels, the players), made once from the base snapshot."""
    cached = getattr(env, "_career_env", None)
    if cached is None or cached.base is not env.base:
        cached = cspecs.Env(env.rng, env.base)
        env._career_env = cached
    return cached


def peek32(env, img, address):
    """A word as the case will see it: what the case pokes, else the base."""
    if all(address + i in img.b for i in range(4)):
        return struct.unpack("<I", bytes(img.b[address + i] for i in range(4)))[0]
    return env.u32(address)


def comp_world(env, img, rng, *, level=None):
    """A competition in a legal state: the career, the manager, the level, the players' scores, the object's participants, judges, counters, ranking and flags."""
    sampler, lvl = ccomp.competition_state(cenv(env), level)
    for address, data in sampler.pokes.items():
        img.put(address, data)
    return lvl


def players(env):
    return cenv(env).players


def player_character(env, img, index=0):
    return peek32(env, img, players(env)[index] + 0x2CC0)


def medal_object(env, img, rng, base):
    img.garbage(base, 0x110, rng)
    img.u32(base, A(env, VT_MEDAL))
    img.u32(base + 0xFC, rng.choice([0, 3, 0x20, 0x27, 0x28, 0x29, 0x60]))
    img.u32(base + 0xE0, rng.randrange(0x5000, 0x30000))


def set_medals(env, img, rng, how):
    """The three medal objects: none, the first only, or all three (`how` is 0, 1 or 3)."""
    for k in range(3):
        if k < how:
            medal_object(env, img, rng, A(env, OBJ[k]))
            img.u32(OBJECT + 0x108 + 4 * k, A(env, OBJ[k]))
        else:
            img.u32(OBJECT + 0x108 + 4 * k, 0)


def camera(env, img, rng):
    """The camera record the medals read (a rotation of nine words and a position) and the pointer to it."""
    cam = A(env, OBJ[3])
    img.garbage(cam, 0x80, rng)
    img.u32(0x005620E0, cam)
    for k in range(9):
        img.u16(cam + 0x34 + 2 * k, rng.randrange(-0x1000, 0x1001))
    for k in range(3):
        img.u32(cam + 4 + 4 * k, rng.randrange(-0x200, 0x200))


def medal_masks(env, img, rng, character, how):
    """The character's record: three contest levels medaled ('gold' or 'mixed'; 'some': any number up to three), the others carry none (the medal screen collects at most three,
    the level table flags more than three levels as contests)."""
    contest = [level for level in range(13) if cenv(env).contest[level]]
    count = min(3, len(contest)) if how in ("gold", "mixed") else rng.randrange(0, min(3, len(contest)) + 1)
    chosen = rng.sample(contest, count)
    masks = []
    for level in range(13):
        mask = rng.getrandbits(10)
        if level in chosen:
            if how == "gold":
                mask |= 0x400
            else:
                mask |= rng.choice([0x400, 0x800, 0x1000])
        masks.append(mask)
    img.put(cs.RECORDS + character * cs.RECORD_STRIDE + 0x0C, b"".join(struct.pack("<H", m) for m in masks))


def pause_menu(env, img, rng):
    img.u32(MENU_CELL, A(env, OBJ[3]) + 0x600)


# ---- the competition object ---------------------------------------------------------------------------------------------------------------------------------

@gcmp(0x004195B0, "Competition_ConstructGlobal")
def gen_construct_global(env, n):
    img, rng = new_image(env)
    img.garbage(OBJECT, 0x174, rng)
    return [], img.pokes()


@gcmp(0x0041AD40, "Competition_Constructor", this=OBJECT)
def gen_constructor(env, n):
    img, rng = new_image(env)
    img.garbage(OBJECT, 0x174, rng)
    return [], img.pokes()


@gcmp(0x0041AE30, "Competition_Destructor", this=OBJECT)
def gen_destructor(env, n):
    img, rng = new_image(env)
    img.garbage(OBJECT, 0x174, rng)
    return [], img.pokes()


@gcmp(0x0041ADD0, "Competition_DeletingDestructor", this=OBJECT)
def gen_deleting_destructor(env, n):
    img, rng = new_image(env)
    img.garbage(OBJECT, 0x174, rng)
    return [rng.choice([0, 1, 1, 2, 3])], img.pokes()


def element_spec(address, name, offset, with_flag):
    def gen(env, n):
        img, rng = new_image(env)
        img.garbage(OBJECT, 0x174, rng)
        return ([rng.choice([0, 1, 1, 2, 3])] if with_flag else []), img.pokes()
    spec(address, name, "gcmp", this=OBJECT + offset)(gen)


element_spec(0x0041AE00, "Competition_ParticipantConstructor", 4 + 0x14 * 3, False)
element_spec(0x0041ADF0, "Competition_ParticipantDestructor", 4 + 0x14 * 5, False)
element_spec(0x0041AE20, "Competition_JudgeConstructor", 0xB8 + 8 * 2, False)
element_spec(0x0041AE10, "Competition_JudgeDestructor", 0xB8 + 8 * 4, False)
element_spec(0x0041AEA0, "Competition_ParticipantDeletingDestructor", 4 + 0x14 * 6, True)
element_spec(0x0041AEC0, "Competition_JudgeDeletingDestructor", 0xB8 + 8 * 1, True)


@gcmp(0x004195E0, "Competition_InitIfActive")
def gen_init_if_active(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    img.u32(GAME_MODE, rng.choice([1, 1, 2, 9]))
    img.u8(HUMAN_MODE, rng.choice([0, 0, 1]))
    img.u32(0x00561C90, rng.randrange(0, 13))
    img.garbage(OBJECT, 0x174, rng)
    seats(img, rng)
    img.u32(0x0056E31C, rng.randrange(1, 100000))
    return [], img.pokes()


def seats(img, rng):
    """Which human sits where: the first seat is a human; sometimes the second is too (the table the multi-player mode edits)."""
    table = [0xFFFFFFFF] * 8
    table[0] = 0
    if rng.random() < 0.25:
        table[1] = 1
    img.put(0x00518638, struct.pack("<8I", *table))


@gcmp(0x00419600, "Competition_Init", this=OBJECT)
def gen_init(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    img.garbage(OBJECT, 0x174, rng)
    seats(img, rng)
    img.u32(0x0056E31C, rng.randrange(1, 100000))
    img.put(0x00568A2C, bytes(rng.randrange(256) for _ in range(0x20)))
    return [], img.pokes()


@gcmp(0x00419F10, "Competition_DestroyMedals", this=OBJECT)
def gen_destroy_medals(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    set_medals(env, img, rng, rng.choice([0, 1, 3, 3]))
    img.u32(0x0056A41C + 4 * rng.randrange(0, 20), rng.getrandbits(32))
    return [], img.pokes()


def logic_cells(env, img, rng):
    """The two words the logic functions are handed pointers to (the answer and the button flag)."""
    result, pressed = A(env, OBJ[3]) + 0x700, A(env, OBJ[3]) + 0x710
    img.u32(result, rng.getrandbits(32))
    img.u32(pressed, rng.choice([0, 1, 1]))
    return result, pressed


@gcmp(0x0041A040, "Competition_WinLoseLogic", this=OBJECT)
def gen_win_lose_logic(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    result, pressed = logic_cells(env, img, rng)
    img.u8(OBJECT + 0x169, rng.choice([0, 1, 1]))
    img.u32(0x00525780, rng.choice([1, 2, 1, 2, 3]))
    set_medals(env, img, rng, rng.choice([0, 3]))
    medal_masks(env, img, rng, player_character(env, img), rng.choice(["gold", "mixed", "some"]))
    pause_menu(env, img, rng)
    return [result, pressed], img.pokes()


@gcmp(0x0041A610, "Competition_EndGameLogic", this=OBJECT)
def gen_end_game_logic(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    result, pressed = logic_cells(env, img, rng)
    img.u8(OBJECT + 0x169, rng.choice([0, 0, 1]))
    set_medals(env, img, rng, rng.choice([0, 1, 3, 3]))
    medal_masks(env, img, rng, player_character(env, img), rng.choice(["gold", "mixed", "some"]))
    pause_menu(env, img, rng)
    return [result, pressed], img.pokes()


@gcmp(0x0041B540, "Competition_ResetMarquee", this=OBJECT)
def gen_reset_marquee(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    img.u32(OBJECT + 0xA8, rng.choice([0, 0, 1, 5, 40]))
    img.u32(players(env)[0] + 8, rng.getrandbits(32))
    img.u32(0x0056E31C, rng.randrange(1, 100000))
    return [], img.pokes()


# ---- the displays --------------------------------------------------------------------------------------------------------------------------------------------

def display_world(env, img, rng, *, medals=None, level=None):
    env.created = 0
    lvl = comp_world(env, img, rng, level=level)
    camera(env, img, rng)
    set_medals(env, img, rng, rng.choice([0, 1, 3]) if medals is None else medals)
    pause_menu(env, img, rng)
    img.u8(0x0056DCAD, rng.choice([0, 1, 2, 0xFF]))
    img.u8(0x0056DCAC, rng.randrange(0, 4))
    return lvl


@gdisp(0x00419DA0, "Competition_WinLoseDisplay")
def gen_win_lose_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=rng.choice([0, 0, 0, 1]))
    img.u32(GAME_MODE, rng.choice([1, 1, 1, 2]))
    medal_masks(env, img, rng, player_character(env, img), rng.choice(["gold", "mixed", "some"]))
    return [rng.choice([10, 11, 12, 10, 11, 12])], img.pokes()


@gdisp(0x0041A270, "Competition_MedalsDisplay")
def gen_medals_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=rng.choice([0, 0, 1, 3]))
    img.u8(OBJECT + 0x169, rng.choice([0, 0, 1]))
    medal_masks(env, img, rng, player_character(env, img), rng.choice(["gold", "gold", "mixed"]))
    return [], img.pokes()


def results_world(env, img, rng):
    lvl = display_world(env, img, rng)
    img.u8(OBJECT + 0xF4, rng.choice([0, 1, 2, 3]))
    img.u8(OBJECT + 0xB4, rng.choice([0, 0, 1]))
    img.u8(OBJECT + 0x169, rng.choice([0, 0, 0, 1]))
    img.u32(OBJECT + 0xB0, rng.choice([0, 20, 100, 180, 181, 200, 400]))
    img.u32(OBJECT + 0x104, rng.choice([0, 0, 1500, 25000, 100000]))
    img.u32(0x00561B74, rng.choice([0, 0, 1]))
    img.u32(GAME_MODE, rng.choice([1, 1, 1, 2]))
    medal_masks(env, img, rng, player_character(env, img), rng.choice(["gold", "mixed", "some"]))
    return lvl


@gdisp(0x0041A780, "Competition_ResultsDisplay")
def gen_results_display(env, n):
    img, rng = new_image(env)
    results_world(env, img, rng)
    # (the level 7 branch: the last contest level counts the settle flag)
    if rng.random() < 0.4:
        img.u32(CURRENT_LEVEL, 7)
    return [], img.pokes()


@gdisp(0x0041AF40, "Competition_MarqueeDisplay")
def gen_marquee_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=0)
    img.u32(OBJECT + 0xAC, rng.choice([0, 1, 5, 21, 40, 60, 100, 150, 161, 200, 400]))
    img.u8(OBJECT + 0xF4, rng.choice([0, 1, 2]))
    img.u8(OBJECT + 0xA4, rng.randrange(1, 5))
    return [], img.pokes()


@gdisp(0x0041B590, "Competition_EndGameDisplay")
def gen_end_game_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=0)
    img.u32(OBJECT + 0xA8, rng.choice([0, 1, 16, 31, 46, 61, 76, 77, 100, 120, 140, 300]))
    img.put(OBJECT + 0xE0, b"".join(struct.pack("<h", rng.randrange(0, 1000)) for _ in range(5)))
    img.u8(OBJECT + 0xEA, rng.randrange(0, 5))
    img.u8(OBJECT + 0xEB, rng.randrange(0, 5))
    img.u8(OBJECT + 0xB6, rng.choice([0, 1]))
    img.u8(OBJECT + 0xF4, rng.choice([0, 1, 2]))
    return [], img.pokes()


@gdisp(0x0041B8B0, "Competition_StartDisplay")
def gen_start_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=0)
    img.u32(OBJECT + 0x164, rng.choice([0, 0, 3, 100, 300, 600, 601, 700, 769, 800]))
    img.u8(OBJECT + 0xF4, rng.choice([0, 0, 1, 2]))
    img.u8(OBJECT + 0x168, rng.choice([0, 0, 1]))
    img.u8(OBJECT + 0xA4, rng.randrange(1, 5))
    # ("Player %d get ready!" is formatted into a 20-byte local: a three-digit number runs over the return address, so the seat numbers stay legal)
    img.u8(OBJECT + 0xF5, rng.randrange(0, 8))
    for k in range(8):
        img.u8(OBJECT + 0x0D + 0x14 * k, rng.randrange(0, 8))
    img.u32(OBJECT + 0x114, rng.choice([0, A(env, TEXTS + TEXT_STRIDE * rng.randrange(len(TEXT_POOL)))]))
    return [], img.pokes()


@gdisp(0x0041BA50, "Competition_PrizeMoneyDisplay")
def gen_prize_money_display(env, n):
    img, rng = new_image(env)
    display_world(env, img, rng, medals=0)
    img.u32(OBJECT + 0xB0, rng.choice([0, 1, 20, 21, 40, 60, 80, 100, 101, 125, 130, 140, 200, 1000]))
    img.u8(OBJECT + 0x16A, rng.choice([0, 0, 1]))
    cash = rng.choice([1, 9, 10, 99, 100, 999, 1000, 2500, 9999, 10000, 25000, 99999, 100000, 250000, rng.randrange(1, 999999), 0])
    return [cash], img.pokes()


# ---- the medal spawner and the tape constructor -----------------------------------------------------------------------------------------------------------------

@gmdl(0x0046E550, "Medal_Create")
def gen_medal_create(env, n):
    img, rng = new_image(env)
    env.created = 0
    comp_world(env, img, rng)
    camera(env, img, rng)
    return [rng.choice([10, 11, 12]), rng.randrange(-60, 60) & 0xFFFFFFFF, rng.randrange(-40, 40) & 0xFFFFFFFF, rng.randrange(0, 200), rng.randrange(0x400, 0x900),
            rng.randrange(1, 12)], img.pokes()


@gmdl(0x004B8FC0, "Tape_Construct", this=lambda env: A(env, OBJ[0]))
def gen_tape_construct(env, n):
    img, rng = new_image(env)
    comp_world(env, img, rng)
    base = A(env, OBJ[1])
    img.garbage(base, 0x200, rng)
    img.garbage(A(env, OBJ[0]), 0x130, rng)
    img.u32(GAME_MODE, rng.choice([1, 1, 2, 3]))
    img.u8(0x0056DCAD, rng.randrange(0, 4))
    img.u8(0x0056DCAC, rng.randrange(0, 4))
    kind = rng.choice([0x664, 0x665, 0x666, 0x10, 0x12, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0x20])
    return [kind, base + 0x100, base + 0x110, rng.randrange(0x400, 0x1400), rng.getrandbits(32), base + 0x120, rng.getrandbits(32), rng.choice([0, 1, 2])], img.pokes()


# ---- the tag game --------------------------------------------------------------------------------------------------------------------------------------------------

TAG_IT = 0x0056DB5C
TAG_RUNNER = 0x0056DB60


def tag_world(env, img, rng, *, scores=True):
    """Both players in a tag game: who is it, the positions, the scores, the counts. The running level has one player: the second is a fake object past the slab."""
    comp_world(env, img, rng)
    first, second = players(env)[0], A(env, FAKE_PLAYER)
    img.u32(PLAYERS, first)
    img.u32(PLAYERS + 4, second)
    img.u32(GAME_MODE, rng.choice([9, 9, 9, 1]))
    pair = [first, second] if rng.random() < 0.5 else [second, first]
    img.u32(TAG_IT, pair[0])
    img.u32(TAG_RUNNER, pair[1])
    img.u32(first + 0x29B0, second)
    img.u32(second + 0x29B0, first)
    for player in (first, second):
        img.garbage(player + 8, 0x10, rng)
        img.u32(player + 0x2F64, rng.choice([0, 0, 0, 1]))
        img.u32(player + 0x2CC4, rng.randrange(0, 4))
        img.u32(player + 0x2CC8, rng.getrandbits(32))
        img.u32(player + 0x2A8, rng.randrange(-100, 5000) & 0xFFFFFFFF)
        img.u32(player + 0x174, rng.randrange(0, 12))
        if scores:
            img.u32(player + 0x16C, rng.choice([0, 100, 2000, 2001, 5000, rng.randrange(0, 20000)]))
    img.u32(0x0056E31C, rng.randrange(1, 100000))
    img.u32(0x0056A928, rng.choice([1, 2, 3, 4]))
    img.u32(0x0056DB64, rng.choice([0, 1, 1, 2]))
    img.u32(0x006A3A90, rng.randrange(0, 40))
    img.u32(0x006A18B0, rng.randrange(0, 40))
    img.u32(0x005685F4, rng.choice([0, 100, 600, 601, 1000]))
    img.u32(0x00561BFC, rng.choice([0x33, 0, 5]))
    img.u8(0x006A3D48, rng.choice([0, 1]))
    img.u8(0x0056DB68, rng.choice([0, 1]))
    img.u8(0x0056DB69, rng.choice([0, 1]))
    img.put(0x0056B790, bytes(rng.randrange(0, 12) if i % 4 == 0 else 0 for i in range(0x60)))
    return first, second


def draw_list(env, img, rng, *, room=True):
    """The draw list's cursor, its end and the renderer's list head."""
    cursor = A(env, OBJ[3]) + 0x200
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, cursor + (0x18 * rng.randrange(1, 4) if room else rng.choice([0, 8, 0x10])))
    img.u32(0x00560FD4, A(env, RENDERER))
    cell = A(env, OBJ[3]) + 0x7F0
    img.u32(A(env, RENDERER) + 0x84, cell)
    img.u32(cell, rng.getrandbits(32))
    img.garbage(cursor, 0x80, rng)
    img.u32(0x029DA394, rng.choice([320, 512, 640, 800]))
    img.u32(0x029DA398, rng.choice([240, 448, 480, 600]))


@gtag(0x004B84C0, "Tag_ShowBanners")
def gen_tag_banners(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8500, "Tag_SwapRoles")
def gen_tag_swap(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8560, "Tag_CheckCaught")
def gen_tag_caught(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8610, "Tag_PushTriangle")
def gen_tag_push_triangle(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    draw_list(env, img, rng, room=rng.random() < 0.85)
    return [rng.randrange(-100, 600) & 0xFFFFFFFF for _ in range(6)] + [rng.getrandbits(32)], img.pokes()


@gtag(0x004B8720, "Tag_RotatedLine")
def gen_tag_rotated_line(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [rng.randrange(0, 512), rng.randrange(0, 240), rng.randrange(-30, 30) & 0xFFFFFFFF, rng.randrange(-30, 30) & 0xFFFFFFFF, rng.randrange(-30, 30) & 0xFFFFFFFF,
            rng.randrange(-30, 30) & 0xFFFFFFFF, rng.randrange(0, 0x1000), rng.choice([0, 0x208020, rng.getrandbits(32)])], img.pokes()


@gtag(0x004B8800, "Tag_RotatedTriangle")
def gen_tag_rotated_triangle(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    draw_list(env, img, rng, room=rng.random() < 0.85)
    return [rng.randrange(0, 512), rng.randrange(0, 240)] + [rng.randrange(-30, 30) & 0xFFFFFFFF for _ in range(6)] + [rng.randrange(0, 0x1000),
                                                                                                                         rng.choice([0, 0x208020])], img.pokes()


@gtag(0x004B8930, "Tag_DrawArrow")
def gen_tag_arrow(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    draw_list(env, img, rng)
    return [rng.randrange(0, 512), rng.randrange(0, 240), rng.randrange(4, 30), rng.randrange(4, 30), rng.randrange(1, 6), rng.randrange(0, 0x1000),
            rng.choice([0, 0x208020])], img.pokes()


@gtag(0x004B8990, "Tag_PointerToSkater")
def gen_tag_pointer(env, n):
    img, rng = new_image(env)
    first, second = tag_world(env, img, rng)
    draw_list(env, img, rng)
    pair = [first, second] if rng.random() < 0.5 else [second, first]
    return [pair[0], pair[1], rng.randrange(0, 512), rng.randrange(0, 240)], img.pokes()


@gtag(0x004B8A90, "Tag_DrawPointers")
def gen_tag_pointers(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    draw_list(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8B40, "Tag_VersusUpdate")
def gen_tag_versus(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8CA0, "Tag_PlayerBox")
def gen_tag_player_box(env, n):
    img, rng = new_image(env)
    first, second = tag_world(env, img, rng)
    return [rng.choice([first, second]), rng.randrange(0, 400), rng.randrange(0, 200), rng.choice([0x80, 0x24, 0]), rng.choice([0x80, 0x84, 0]), rng.choice([0x80, 0xB6, 0]),
            rng.randrange(0, 50), rng.randrange(0, 50), rng.randrange(0, 20), rng.getrandbits(8), rng.choice([0, 1, 1, 0, 2])], img.pokes()


@gtag(0x004B8EF0, "Tag_DrawScoreBoxes")
def gen_tag_boxes(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8FA0, "Tag_PlayerOneFlag")
def gen_tag_flag_one(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()


@gtag(0x004B8FB0, "Tag_PlayerTwoFlag")
def gen_tag_flag_two(env, n):
    img, rng = new_image(env)
    tag_world(env, img, rng)
    return [], img.pokes()
