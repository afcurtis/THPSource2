"""Phase 4.10i probe, part 3: the case generators of Front_Display (fdisp) and the status changes and state helpers (fst); see frontend_specs_front.py for the groups, regions and stubs.

The menus are real CMenu objects in the layout Front_Init leaves (the names are the pointer cells' strings), the name entry and the paused bitmap are fake objects whose virtual
methods are reported services, and the career world is the running game's own (a level is loaded): the generators poke the status, the mode, the counters and the records the
screens read.
"""

from __future__ import annotations

import struct

from create_specs import spec
from frontend_specs_front import FAKE_PLAYER, SLAB, STRING_POOL, string_at, menu_at, TICK, ENTRY, SPRITE
from frontend_specs_front_cases import (ANIM_SCALE, CURRENT_LEVEL, GAME_MODE, GOT_NEW_STATUS, HORSE_LENGTH, HORSE_WORD, PLAYER_COUNT, PLAYERS, STATUS, VT_CMENU, horse_world,
                                        menu_image, new_image, player_pointer)

# the menus as Front_Init builds them: variable, the pointer cells of the names
YES, NO = 0x0052E410, 0x0052E414
CELLS = {
    "yesno": (0x00561C1C, [YES, NO]),
    "end": (0x00561C20, [0x0052E5E0, 0x0052E550, 0x0052E510, 0x0052E558, 0x0052E55C, 0x0052E560, 0x0052E564, 0x0052E54C, 0x0052E970, 0x0052E568]),
    "replay_full": (0x00561C24, [0x0052E534, 0x0052E538, 0x0052E53C, 0x0052E548]),
    "replay_short": (0x00561C28, [0x0052E534, 0x0052E538, 0x0052E548]),
    "replay_comp": (0x00561C2C, [0x0052E534, 0x0052E538, 0x0052E53C, 0x0052E5B0]),
    "heat": (0x00561C30, [0x0052E5A8, 0x0052E564, 0x0052E5AC, 0x0052E970]),
    "player": (0x00561C34, [0x0052E5F0, 0x0052E564, 0x0052E5AC, 0x0052E970]),
    "time": (0x00561C38, [0x0052E500, 0x0052E504, 0x0052E508, 0x0052E50C]),
    "sound": (0x00561C3C, [0x005616C0, 0x0056175C, 0x00561734, 0x0052E5D8]),
    "paused": (0x00561C40, [0x0052E968, 0x0052E550, 0x0052E98C, 0x0052E5C8, 0x0052E518]),
}
MENU_SLOT = {name: i for i, name in enumerate(CELLS)}


def fdisp(address, name):
    return spec(address, name, "fdisp")


def fst(address, name):
    return spec(address, name, "fst")


def standard_menu(env, img, rng, which, *, present=True, line=None, extra=()):
    """A menu of the front end in its init state (its names are the strings the cells point to), its variable set; `present` False clears the variable;
    `extra` entries (pointer cells) are inserted before the last entry."""
    variable, cells = CELLS[which]
    if extra:
        cells = cells[:-1] + list(extra) + cells[-1:]
    if not present:
        img.u32(variable, 0)
        return 0
    pointers = [env.u32(c) if c < 0x00540000 and c not in (0x005616C0, 0x0056175C, 0x00561734) else c for c in cells]
    count = len(pointers)
    base = menu_at(env, MENU_SLOT[which])
    hidden = {e for e in range(count) if rng.random() < 0.15}
    hidden.discard(rng.randrange(0, count))        # (retail's menu step never ends when no entry is shown)
    _, _ = menu_image(env, img, rng, MENU_SLOT[which], count=count, line=line if line is not None else rng.randrange(0, count), hidden=hidden)
    for e, p in enumerate(pointers):
        img.u32(base + 0x18 * (e + 1), p)
    img.u32(variable, base)
    return base


def paused_extras(rng):
    """The pause menu may also hold End Game (the horse game's) and Quit (never added by the update: the lookups of it are dead code a menu with the entry reaches)."""
    return tuple(cell for cell, chance in ((0x0052E514, 0.3), (0x0052E970, 0.3)) if rng.random() < chance)


def all_menus(env, img, rng, *, absent=()):
    for which in CELLS:
        extra = ()
        if which in ("replay_full", "replay_comp") and rng.random() < 0.3:
            extra = (0x0052E540,)             # "Save Video To Disk"
        elif which == "paused":
            extra = paused_extras(rng)
        standard_menu(env, img, rng, which, present=which not in absent, extra=extra)


def text_buffers(env, img, rng):
    """The sound levels menu's lines (their text is the entries' names)."""
    for address, text in ((0x005616C0, b"SFX level: 5"), (0x0056175C, b"Music level: 5"), (0x00561734, b"Skip track: ---")):
        img.put(address, text + b"\0" + bytes(0x20 - len(text) - 1))


def display_world(env, img, rng, status, *, mode=None, players=None):
    mode = rng.choice([1, 1, 2, 3, 4, 7, 8, 9, 10]) if mode is None else mode
    players = rng.choice([1, 2, 2]) if players is None else players
    img.u32(STATUS, status)
    img.u32(GAME_MODE, mode)
    img.u32(PLAYER_COUNT, players)
    img.u32(0x0056A85C, env.slab + FAKE_PLAYER if players == 2 else 0)
    img.u32(CURRENT_LEVEL, rng.randrange(0, 14))
    img.u32(TICK, rng.randrange(0, 100000))
    img.u32(0x0056A8F0, rng.choice([0, 0, 1]))
    img.u32(0x0056A8D4, rng.randrange(0, 200))
    img.u32(ANIM_SCALE, rng.randrange(0, 60))
    img.u32(0x005616E0, rng.randrange(0, 40))
    img.u8(0x00561BF0, rng.choice([0, 0, 1]))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u8(0x00560080, rng.choice([0, 0, 1]))
    img.u32(0x00561C90, rng.randrange(0, 14))
    for k in range(5):
        img.u32(0x00561918 + 4 * k, rng.choice([rng.randrange(-0xE0, 0x120), 0x3C, 0x5A]))
        img.u32(0x00561710 + 4 * k, rng.randrange(0x180, 0x2E0))
    all_menus(env, img, rng)
    text_buffers(env, img, rng)
    img.u32(0x00561BF8, env.slab + ENTRY if status == 0x2B else rng.choice([env.slab + ENTRY, env.slab + ENTRY, 0]))
    img.u32(0x0056B70C, env.slab + SPRITE)
    img.u32(0x00560FD4, env.u32(0x00560FD4))
    for address in (0x0056A2EC, 0x0056A2E8):
        img.u32(address, rng.randrange(0, 12))
    img.u32(0x005618C4, rng.randrange(0, 11))
    img.u32(0x00561994, rng.randrange(0, 11))


def hud_stats(env, img, rng):
    """The HUD skater's statistics (the one-player table) and the two players' scores."""
    hud = env.u32(0x0056A960)
    img.u32(hud + 0x16C, rng.choice([0, rng.randrange(0, 1000000), 0xFFFFFFFF]))
    img.u16(hud + 0x172, rng.randrange(0, 5000))
    img.u16(hud + 0x176, rng.choice([0, 1, 2, rng.randrange(0, 40)]))
    img.u32(hud + 0x178, rng.choice([0, 1, rng.randrange(0, 90000)]))
    for k in range(3):
        text = rng.choice([b"Kickflip", b"Ollie to Grind", b"360 Flip", b"A very long trick name that goes on and on"])
        img.put(hud + 0x17C + 100 * k, text[:90] + b"\0")
    for player in (player_pointer(env, 0), env.slab + FAKE_PLAYER):
        if player:
            img.u32(player + 0x16C, rng.choice([0, rng.randrange(0, 5000), rng.randrange(0, 100000)]))
            img.u32(player + 0x2A8, rng.choice([0, rng.randrange(0, 5000)]))
            img.u32(player + 0x2CDC, rng.randrange(0, 10))
            img.u32(player + 0x2CE0, rng.randrange(0, 10))
            img.u32(player + 0x2CC4, rng.randrange(0, 4))
    for address in (0x0056B7DC, 0x0056B79C, 0x0056B7B0, 0x0056B7A4, 0x0056B7E0, 0x0056B798, 0x0056B7B4, 0x0056B7A0):
        img.u32(address, rng.randrange(0, 20))


def horse_state(env, img, rng):
    horse_world(env, img, rng)
    img.u32(0x0056A854, rng.choice([0, 1]))
    img.u32(0x00568680, rng.choice([0, 1, 1]))
    img.u32(0x00568660, rng.choice([0, 5, 1000, 20000]))
    img.u32(0x00568A60, rng.randrange(0, 6))
    img.u32(0x00568A64, rng.randrange(0, 6))
    img.u32(0x0056174C, rng.randrange(0, 6))
    img.u32(0x00561750, rng.randrange(0, 6))
    img.u32(0x00561938, rng.randrange(0, 8))
    img.u32(0x00561998, rng.choice([0, 1, 2]))
    img.u32(0x00561774, rng.choice([0, 1]))
    img.u32(0x00561B2C, rng.choice([0, 0x100, 0x1E0, 0x1F0, 0x200, 0x210]))
    img.u32(0x00561BE8, rng.choice([0, 1]))
    img.u8(0x00561BE4, rng.choice([0, 0, 1]))
    img.u32(0x00561730, rng.choice([0, 1]))
    for p in (player_pointer(env, 0),):
        img.u32(p + 0x2A8, rng.choice([0, 5, 1000, 20000, 0x80000000]))


def score_table(env, img, rng):
    """The high-score table: eight places of three letters, a character and a score, for every level, and the row being edited."""
    level = 0x00569EBC
    for row in range(14 * 8):
        letters = bytes(rng.choice(LETTERS) for _ in range(3))
        img.put(level + 8 * row, letters + bytes([rng.randrange(0, 21)]) + struct.pack("<I", rng.randrange(0, 1000000)))
    img.u32(0x0052BF34, rng.choice([-1 & 0xFFFFFFFF] + list(range(8))))
    img.u32(0x00561BF4, rng.randrange(0, 3))
    img.u32(0x0056BB3C, rng.choice([0, 3, 16]))
    for k in range(3):
        img.u8(0x0056A408 + k, rng.choice(b"ABCXYZ"))


LETTERS = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ  " + bytes([0])
STATUSES = [1, 3, 4, 5, 6, 7, 0xC, 0x11, 0xD, 0xD, 0xD, 0xE, 0xF, 0x10, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1E, 0x1F, 0x20, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x2E, 0x2E, 0x2F, 0x33,
            0, 2, 8, 0x12, 0x13, 0x28, 0x30, 0x37, 0x38]


def tune_results(env, img, rng, mode, players):
    """The statistics screen's branches: a tie, a win, the horse game's match against the score set, the letters running out."""
    p1 = player_pointer(env)
    p2 = env.slab + FAKE_PLAYER
    if mode == 8:
        a = rng.randrange(0, 10)
        img.u32(p1 + 0x2CDC, a)
        img.u32(p2 + 0x2CDC, rng.choice([a, a, rng.randrange(0, 10)]))
    elif mode == 7:
        scored = rng.choice([0, 5, 1000, 20000])
        img.u32(p1 + 0x2A8, scored)
        img.u32(0x00568660, rng.choice([0, scored, scored + 7, max(scored - 3, 1), 4]))
        who = rng.choice([0, 1])
        img.u32(0x0056A854, who)
        length = len(horse_word_of(img))
        img.u32(0x0056174C + 4 * who, rng.choice([length - 1, length - 2, 0, 1]))
        img.u32(0x00568680, rng.choice([0, 1, 1]))
        img.u32(0x00561BE4, 0)
    elif players == 1:
        hud = env.u32(0x0056A960)
        img.u32(hud + 0x178, rng.choice([0, 0, 5000]))


def horse_word_of(img):
    """The horse word as the case pokes it (the bytes up to the terminator)."""
    out = []
    for at in range(0x0056A478, 0x0056A488):
        value = img.b.get(at, 0)
        if value == 0:
            break
        out.append(value)
    return bytes(out)


@fdisp(0x0044CA60, "Front_Display")
def gen_display(env, n):
    img, rng = new_image(env)
    status = STATUSES[n % len(STATUSES)] if n < 2 * len(STATUSES) else rng.choice(STATUSES)
    mode = rng.choice([1, 1, 2, 3, 4, 7, 8, 9, 10])
    players = rng.choice([1, 2, 2])
    if status == 0xD:
        players = rng.choice([1, 2, 2, 2])
        mode = rng.choice([7, 8, 9, 2, 3, 1]) if players == 2 else rng.choice([1, 2, 3])
    display_world(env, img, rng, status, mode=mode, players=players)
    hud_stats(env, img, rng)
    horse_state(env, img, rng)
    score_table(env, img, rng)
    img.u32(HORSE_LENGTH, len(horse_word_of(img)))
    if status in (0xE, 0xF, 0x10, 0x18, 0x19, 0x1A, 0x33):
        img.u32(0x005616E0, rng.choice([0, 1, 2, 5, 30, 40]))
        img.u32(ANIM_SCALE, rng.choice([0, 1, 10, 25]))
    if status == 0xD:
        tune_results(env, img, rng, mode, players)
    if status == 0xD and mode == 7:
        img.u32(0x0056E208, rng.choice([1, 3, 6]))
    return [], img.pokes()


@fdisp(0x0044DD80, "Front_DrawPausedBitmap")
def gen_paused(env, n):
    img, rng = new_image(env)
    img.u32(0x0056B70C, env.slab + SPRITE)
    img.u32(0x00537B3C, rng.choice([0, 1]))
    return [], img.pokes()


@fdisp(0x0044E630, "Front_HorseMessage")
def gen_horse_message(env, n):
    img, rng = new_image(env)
    counter = menu_at(env, 9) + 0x180
    img.u32(counter, rng.choice([0, 0x20, 0x100, 0x17F, 0x180, 0x1E0, 0x1E0, 0x1FF, 0x200, 0x200, 0x240]))
    img.u8(0x00561BE4, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561BE8, rng.choice([0, 1, 1]))
    img.u32(0x00561730, rng.choice([0, 1]))
    img.u32(0x00568650, rng.choice([0, 0x2D, 7]))
    return [string_at(env, rng.randrange(0, 20)), string_at(env, rng.randrange(0, 20)), counter, rng.choice([0x182, 0xBD, 0x34])], img.pokes()


# ---- the status changes and state helpers -------------------------------------------------------------------------------------------------------------------------

def state_world(env, img, rng, *, status=None, mode=None):
    display_world(env, img, rng, rng.choice([0, 1, 3, 0xD, 0xE, 0x13, 0x2A, 0x2B, 0x33, 0xF, 0x10, 0x1A]) if status is None else status, mode=mode)
    hud_stats(env, img, rng)
    score_table(env, img, rng)
    img.u8(0x006A3B21, rng.choice([0, 0, 0, 1]))


@fst(0x0044DDA0, "Front_PollButtons")
def gen_poll(env, n):
    img, rng = new_image(env)
    for address in (0x0056B0D9, 0x0056B245, 0x0056B029, 0x0056B195, 0x0056AFF9, 0x0056B165, 0x0056B009, 0x0056B019, 0x0056B0E9, 0x0056B039, 0x0056B049, 0x0056B059, 0x0056B069, 0x0056B175,
                    0x0056B185, 0x0056B255, 0x0056B1A5, 0x0056B1B5, 0x0056B1C5, 0x0056B1D5):
        img.u8(address, rng.choice([0, 0, 0, 0, 1]))
    img.u32(0x0056A900, rng.choice([0, 1]))
    img.u32(0x0056A8E0, rng.choice([0, 1]))
    out = menu_at(env, 9) + 0x180
    img.put(out, bytes(16))
    return [out, out + 4, out + 8, out + 12], img.pokes()


@fst(0x0044DFA0, "CommonSwitchOnMenuCode")
def gen_common(env, n):
    img, rng = new_image(env)
    state_world(env, img, rng, status=rng.choice([0, 1, 0xD, 0x13, 0x13]))
    standard_menu(env, img, rng, "yesno")
    return [], img.pokes()


@fst(0x0044E0F0, "Front_SwitchOnEndLevel")
def gen_end_level(env, n):
    img, rng = new_image(env)
    mode = rng.choice([1, 1, 1, 2, 3, 4, 7, 9, 10])
    state_world(env, img, rng, status=rng.choice([0, 1, 0xB, 0xD, 0xE, 0x13, 0x32, 0x34, 0x35, 0x36, 0x37, 0x2A]), mode=mode)
    player = player_pointer(env)
    img.u32(PLAYER_COUNT, rng.choice([1, 2]))
    img.u32(player + 0x31E4, rng.choice([0, 0, 3]))
    img.u32(player + 0x2CC0, rng.randrange(0, 21))
    record = env.u32(player + 0x29B0)
    img.u32(record + 0x504, rng.choice([0x17, 1, 5]))
    char = rng.randrange(0, 21)
    img.u32(player + 0x2CC0, char)
    config = 0x00568A6C + char * 0x104
    total = rng.choice([100, 499, 500, 1500, 2000, 2500])
    img.u32(config + 4, total)
    img.u32(config + 8, rng.choice([total, total, rng.randrange(0, 3000)]))
    img.u32(0x0056A68C, rng.choice([0, 1, 2, 3, 4, 0x1F]))
    img.u32(0x0056DB64, rng.choice([0, 1, 2]))
    img.u32(0x00561C90, rng.randrange(0, 14))
    img.u32(player + 0x16C, rng.choice([0, rng.randrange(0, 1000000), 0xFFFFFFF0]))
    img.u32(0x00568658, rng.choice([0, 1]))
    if rng.random() < 0.4:
        # the career milestone: 500 or 2000 earned, all of the character's money still in hand, one player, a career level
        img.u32(GAME_MODE, 1)
        img.u32(PLAYER_COUNT, 1)
        img.u32(STATUS, rng.choice([0, 1, 0xE, 0x2A, 0x33]))
        img.u8(0x00560080, 0)
        img.u32(player + 0x31E4, 0)
        img.u32(0x0056A68C, rng.choice([0, 1, 2, 4]))
        total = rng.choice([500, 600, 2000, 2500])
        img.u32(config + 4, total)
        img.u32(config + 8, total)
        img.u32(0x00561C90, rng.randrange(0, 14))
    return [], img.pokes()


@fst(0x0044E3F0, "Front_ResetScoreSlide")
def gen_reset_slide(env, n):
    img, rng = new_image(env)
    for k in range(10):
        img.u32(0x00561918 + 4 * k, rng.randrange(0, 0x1000))
        img.u32(0x00561710 + 4 * k, rng.randrange(0, 0x1000))
    return [], img.pokes()


def end_heat_world(env, img, rng):
    state_world(env, img, rng)
    standard_menu(env, img, rng, "yesno")


@fst(0x0044E420, "Front_SwitchOnEndHeat")
def gen_end_heat(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    return [], img.pokes()


@fst(0x0044E4E0, "Front_SwitchOnEndingScreen")
def gen_ending(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    img.u32(STATUS, rng.choice([0xE, 0xE, 0, 1, 0xD]))
    return [], img.pokes()


@fst(0x0044E540, "Front_SwitchOnEndLevelMenu")
def gen_level_menu(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    img.u32(STATUS, rng.choice([0x2A, 0x2A, 0, 1, 0xD]))
    img.u32(0x00561BF8, rng.choice([0, env.slab + ENTRY]))
    return [], img.pokes()


@fst(0x0044E5C0, "Front_SwitchOnTimeSelect")
def gen_time_select(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    img.u32(STATUS, rng.choice([0x33, 0x33, 0, 1, 0xD]))
    return [], img.pokes()


@fst(0x0044E7E0, "Front_PrintHorseLetters")
def gen_horse_letters(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    horse_world(env, img, rng)
    img.u32(STATUS, rng.choice([0x2B, 0x2B, 0, 1, 0xD]))
    img.u32(0x00561BF8, rng.choice([0, 0, 0, env.slab + ENTRY]))
    return [], img.pokes()


@fst(0x0044E950, "Front_SelectPlayReplay")
def gen_select_replay(env, n):
    img, rng = new_image(env)
    state_world(env, img, rng)
    return [], img.pokes()


@fst(0x0044E9C0, "Front_SwitchOnVideoScreen")
def gen_video_screen(env, n):
    img, rng = new_image(env)
    end_heat_world(env, img, rng)
    img.u32(STATUS, rng.choice([0xF, 0x10, 0x1A, 0, 1, 0xD]))
    img.put(0x0056B8D4, struct.pack("<H", rng.choice([0, 0, 1, 2])))
    img.u8(0x0055FF1C, rng.choice([0, 1, 2, 3, 5]))
    img.u8(0x00560080, rng.choice([0, 1]))
    img.u32(GAME_MODE, rng.choice([1, 2, 3]))
    return [], img.pokes()


@fst(0x0044EA70, "Front_ReplayRestart")
def gen_replay_restart(env, n):
    img, rng = new_image(env)
    img.u32(CURRENT_LEVEL, rng.choice([5, 5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12]))
    img.u32(0x005477EC, rng.getrandbits(32))
    return [], img.pokes()
