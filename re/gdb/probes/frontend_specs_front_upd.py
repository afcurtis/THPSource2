"""Phase 4.10i probe, part 4: the case generator of Front_Update (fupd).

One generator puts the front end in a random legal state for a status (the menus as real CMenu objects, the pads, the counters, the competition and career words the screens read)
and calls the update once. The services it reaches (the career and competition calls, the replay and save-card calls, the sound and text calls) are the stubs of
frontend_specs_front.py with scripted answers; the CMenu code, the status changes and the button polling run for real.
"""

from __future__ import annotations

import os

from create_specs import spec
from frontend_specs_front import ENTRY, string_at
from frontend_specs_front_cases import GAME_MODE, menu_image, new_image
from frontend_specs_front_cases2 import display_world, hud_stats, horse_state, paused_extras, score_table, standard_menu, text_buffers

SHORT_NAMES = [2, 5, 6, 20, 29, 30]          # strings short enough for the horse word buffer
DE_PREFIX = 35                                 # "de_": the cheat restarts the list leaves out

# the statuses the update handles (the unhandled ones fall out of the jump table)
STATUSES = [0, 0, 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 0xB, 0xB, 0xB, 0xC, 0xD, 0xD, 0xE, 0xE, 0xE, 0xF, 0xF, 0x10, 0x10, 0x11, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19,
            0x1A, 0x1A, 0x24, 0x2A, 0x2B, 0x2B, 0x2E, 0x2E, 0x2F, 0x2F, 0x31, 0x31, 0x32, 0x32, 0x33, 0x34, 0x34, 0x35, 0x36, 0x37, 0x15, 0x1B, 0x38]

if os.environ.get("P410_STATUSES"):           # a targeted capture: only these statuses (comma separated, hex)
    STATUSES = [int(token, 16) for token in os.environ["P410_STATUSES"].split(",")]

PAD_SELECT = (0x0056B0D9, 0x0056B245)
PAD_ACCEPT = (0x0056B029, 0x0056B195)
PAD_BACK = (0x0056AFF9, 0x0056B165)
PAD_OTHER = (0x0056B009, 0x0056B019, 0x0056B0E9, 0x0056B039, 0x0056B049, 0x0056B059, 0x0056B069, 0x0056B175, 0x0056B185, 0x0056B255, 0x0056B1A5, 0x0056B1B5, 0x0056B1C5, 0x0056B1D5)
PAD_MENU = (0x0056B098, 0x0056B0A8, 0x0056B204, 0x0056B214, 0x0056B079, 0x0056B1E5, 0x0056B089, 0x0056B1F5)
PAD_HELD = (0x0056B028, 0x0056B194, 0x0056B0D8, 0x0056B244, 0x0056B0E8, 0x0056B254)


def fupd(address, name):
    return spec(address, name, "fupd")


def pads(img, rng, accept=0.45, back=0.2):
    for address in PAD_SELECT + PAD_ACCEPT + PAD_BACK + PAD_OTHER + PAD_MENU + PAD_HELD:
        img.u8(address, 0)
    if rng.random() < accept:
        img.u8(rng.choice(PAD_SELECT + PAD_ACCEPT), 1)
    if rng.random() < back:
        img.u8(rng.choice(PAD_BACK), 1)
    if rng.random() < 0.15:
        img.u8(rng.choice(PAD_OTHER), 1)
    for address in PAD_MENU:
        if rng.random() < 0.12:
            img.u8(address, 1)
    for address in PAD_HELD:
        if rng.random() < 0.1:
            img.u8(address, 1)
    img.u32(0x0056A900, rng.choice([0, 0, 1]))
    img.u32(0x0056A8E0, rng.choice([0, 0, 1]))


def competition_world(img, rng):
    for address, values in ((0x0055FED0, [0, 1, 500, 1000]), (0x0055FED4, [0, 1, 5, 999, 1000]), (0x0055FED8, [0, 1])):
        img.u32(address, rng.choice(values))
    for address in (0x0055FEDC, 0x0055FEDD, 0x0055FEDE, 0x0055FEDF):
        img.u8(address, rng.choice([0, 1]))
    img.u8(0x0055FF1C, rng.choice([0, 1, 2, 3, 4]))


def update_world(env, img, rng, status):
    players = rng.choice([1, 1, 2])
    mode = rng.choice([1, 1, 1, 2, 3, 4, 7, 8, 9, 10])
    if status == 0x33:
        players = 2
    display_world(env, img, rng, status, mode=mode, players=players)
    hud_stats(env, img, rng)
    horse_state(env, img, rng)
    score_table(env, img, rng)
    pads(img, rng)
    competition_world(img, rng)
    text_buffers(env, img, rng)
    img.u8(0x006A3B21, rng.choice([0, 0, 0, 1]))
    img.u32(0x006A18BC, rng.choice([0, 0x2000, 0x4000]))
    img.u32(0x0056B144, rng.choice([1, 1, 1, 0]))
    img.u32(0x0056B2B0, rng.choice([0, 1]))
    img.u32(0x0056E5D8, rng.choice([0, 2, 2, 1]))
    img.u8(0x00561990, rng.choice([0, 0, 1]))
    img.u32(0x00568658, rng.choice([0, 1]))
    img.u32(0x0056A8EC, rng.choice([0, 1, 1]))
    img.u8(0x0056B038, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A8D0, rng.randrange(0, 12))
    img.u32(0x00561C04, rng.choice([0, 1, 1]))
    img.u32(0x00561C08, rng.choice([0, 0, 0x20, 8, 3, 1]))
    img.u32(0x005616D8, rng.choice([0, 0, 1]))
    img.u32(0x00561C0C, rng.randrange(0, 0x4000))
    img.u32(0x00561C48, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561C4C, rng.randrange(0, 1000))
    img.u32(0x0052BF38, rng.choice([0, 1, 1]))
    img.u32(0x00561B6C, rng.choice([0, 0, 10, 11]))
    img.u32(0x005618D4, rng.choice([0, 1]))
    img.u32(0x005618D0, rng.choice([0, 1]))
    img.u32(0x00568A58, rng.randrange(0, 3))
    img.u32(0x00568A5C, rng.randrange(0, 3))
    img.u32(GAME_MODE, mode)
    img.u32(0x0056A68C, rng.randrange(0, 0x40))
    img.u32(0x0055FB3C, rng.randrange(0, 13))
    img.u32(0x0055FB40, rng.randrange(0, 13))
    img.u32(0x005685FC, rng.randrange(0, 0x9000))
    img.u32(0x00561B9C, rng.randrange(0, 7))
    img.u32(0x0056E774, rng.randrange(-1, 2) & 0xFFFFFFFF)
    img.u32(0x00561BEC, rng.choice([0, 0x1234, 0xAAAA]))
    img.u32(0x00561754, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 7]))
    img.put(0x00561B68, bytes(2))
    img.u32(0x005616E8, rng.randrange(0, 100000))
    img.u32(0x005616DC, rng.randrange(0, 100000))
    img.u32(0x00561B88, rng.randrange(0, 100000))
    img.u32(0x0056198C, rng.randrange(0, 100000))
    img.u32(0x00561B80, rng.choice([0, 1]))
    img.u32(0x00561B84, rng.randrange(0, 5))
    img.u32(0x00561B74, rng.randrange(0, 3))


def timing(env, img, rng, status):
    """The unlock and goal screens count ticks from the status change: put the clock around the thresholds they test."""
    clock = rng.randrange(0, 100000)
    img.u32(0x0056E31C, clock)
    img.u32(0x0056E320, clock)
    for address in (0x005616DC, 0x005616E8, 0x00561B88, 0x0056198C):
        img.u32(address, (clock - rng.choice([0, 1, 5, 0x14, 0x15, 0x28, 0x3C, 0x50, 0x64, 0xA0, 0x140, 0x142, 0x300, rng.randrange(0, 0x200)])) & 0xFFFFFFFF)
    img.u32(0x005616E0, rng.choice([0, 1, 0x1E, 0x1F, 0x20, 40]))


@fupd(0x0044EAC0, "Front_Update")
def gen_update(env, n):
    img, rng = new_image(env)
    status = STATUSES[n % len(STATUSES)] if n < 3 * len(STATUSES) else rng.choice(STATUSES)
    update_world(env, img, rng, status)
    timing(env, img, rng, status)
    pad_boost = status in (0xB, 0xC, 0xE, 0xF, 0x10, 0x11, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x24, 0x2A, 0x2B, 0x2E, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37)
    if pad_boost:
        pads(img, rng, accept=0.6, back=0.25)
    if status == 0:
        img.u32(0x00561C40, 0)                                       # no pause menu yet
        img.u32(0x0056B038, 0)
    elif status in (1,):
        standard_menu(env, img, rng, "paused", line=rng.randrange(0, 5), extra=paused_extras(rng))
        img.u8(0x0056B038, rng.choice([0, 0, 0, 1]))
    elif status == 2:
        if rng.random() < 0.5:
            img.u32(0x00561C44, 0)
        else:
            base, _ = menu_image(env, img, rng, 11, names=[rng.randrange(0, 20) for _ in range(rng.randrange(1, 8))])
            img.u32(0x00561C44, base)
        count = rng.choice([1, 1, 3, 6, 12])
        img.u32(0x0056E1B4, count)
        for k in range(20):
            img.u32(0x0056E164 + 4 * k, string_at(env, rng.choice([DE_PREFIX, 32, 33, 34, rng.randrange(0, 20)])))
        # (retail's menu step never ends when every restart is a "de_" one and none is added: keep one that is added)
        img.u32(0x0056E164 + 4 * rng.randrange(0, count), string_at(env, rng.choice([33, 34, rng.randrange(0, 20)])))
    elif status == 0xD:
        img.u32(GAME_MODE, rng.choice([1, 7, 7, 2]))
        img.u32(0x00568650, rng.choice([-3 & 0xFFFFFFFF, 0, 1, 2, 40]))
        img.u32(0x00561998, rng.choice([0, 1, 2]))
        img.u32(0x0052BF34, rng.choice([-1 & 0xFFFFFFFF, 3]))
    elif status == 0x2B:
        img.u32(0x00561BF8, env.slab + ENTRY)
    elif status == 0x2E:
        img.u32(0x0052BF34, rng.choice([-1 & 0xFFFFFFFF, 0, 2, 5]))
    elif status == 0x11:
        pass
    return [], img.pokes()
