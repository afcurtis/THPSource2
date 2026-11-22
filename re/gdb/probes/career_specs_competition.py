"""Case generators for the Phase 4.8 probe: the competition judging and ranking rules."""

from __future__ import annotations

import career_state as cs
from career_specs import SPECS, PLAYER_SCORE, Skip, Spec, base_state
from career_state import u8, u16, u32

OBJECT = 0x0055FE28
PLAYER_DIFFICULTY = 0x174
LADDER_INIT = [0x3CF, 0x398, 0x366, 0x334, 0x208]
CONTEST_LEVELS = [2, 5, 7]


def competition_state(env, level=None):
    rng = env.rng
    contest = [l for l in range(13) if env.contest[l]] or CONTEST_LEVELS
    lvl = rng.choice(contest) if level is None else level
    s, _ = base_state(env, level=lvl, mode=1)
    humans = rng.randrange(1, 5)
    s.put(OBJECT + 0xA4, u8(humans))
    # participants: the first `humans` are people, the rest computer riders with distinct brackets
    for e in range(8):
        base = OBJECT + 4 + e * 0x14
        human = 1 if e < humans else 0
        s.put(base + 4, u8(human))
        s.put(base + 5, u8(e if human else 0xFF))
        s.put(base + 6, u8(rng.randrange(0, 20)))
        s.put(base + 7, u8(rng.randrange(0, 4)))
        s.put(base + 8, u8(rng.choice([0xFF, 0, 1, 2, 3, 4, 5, 6, 7])))
        s.put(base + 9, u8(e if human else 0xFF))
        s.put(base + 10, b"".join(u16(rng.choice([0, rng.randrange(0, 1000)])) for _ in range(4)))
    order = list(range(8))
    rng.shuffle(order)
    s.put(OBJECT + 0xEC, bytes(order))
    s.put(OBJECT + 0xF4, u8(rng.randrange(0, 3)))
    s.put(OBJECT + 0xF5, u8(rng.randrange(0, 8)))
    s.put(OBJECT + 0xF6, u8(rng.randrange(0, 8)))
    for j in range(5):
        jb = OBJECT + 0xB8 + j * 8
        s.put(jb + 4, u8(rng.choice([rng.randrange(0, 8), 0xFF])))
        s.put(jb + 5, u8(rng.randrange(0, 60)))
        s.put(jb + 6, u8(rng.choice([rng.randrange(0, 8), 0xFF])))
        s.put(jb + 7, u8(rng.randrange(0, 60)))
    s.put(OBJECT + 0xA8, u32(rng.randrange(0, 100)))
    s.put(OBJECT + 0xAC, u32(rng.randrange(0, 100)))
    s.put(OBJECT + 0xB0, u32(rng.randrange(0, 100)))
    s.put(OBJECT + 0x108, u32(0))
    s.put(0x00560080, u8(rng.choice([0, 0, 0, 1])))
    s.put(0x00560081, u8(rng.choice([0, 1])))
    s.put(0x0056007C, u32(rng.randrange(0, 8)))
    ladder = LADDER_INIT if rng.random() < 0.8 else [rng.randrange(0, 1000) for _ in range(5)]
    s.put(0x00525774, b"".join(u16(v) for v in ladder))
    for player in env.players:
        s.put(player + PLAYER_SCORE, u32(rng.choice([0, rng.randrange(0, 30000), rng.randrange(0, 90000), 60000, 35000])))
        s.put(player + PLAYER_DIFFICULTY, u16(rng.randrange(0, 12)))
    return s, lvl


def comp_spec(address, name, gen_args, this=True):
    def gen(env, n):
        s, lvl = competition_state(env)
        return gen_args(env), s.pokes
    SPECS.append(Spec(address, name, "competition", gen, this=OBJECT if this else None))


def tag_gen(env, n):
    rng = env.rng
    s, _ = base_state(env, mode=rng.choice([9, 9, 9, 1]))
    if not env.players:
        raise Skip()
    it = rng.choice(env.players)
    s.put(0x0056DB5C, u32(it))
    s.put(it + PLAYER_SCORE, u32(rng.choice([0, rng.randrange(0, 12000), 12000, 15000, 0xFFFFFF00])))
    skater = rng.choice([it, rng.choice(env.players), rng.randrange(0, 1 << 24)])
    return [skater, rng.choice([0, 1, 2, 3, 3, 4, 9]), rng.choice([0, 5, 50, 100, 255, -20])], s.pokes


SPECS.append(Spec(0x004B8410, "Tag_StatModifier", "competition", tag_gen))

comp_spec(0x00419460, "Competition_AllMedalsWon", lambda e: [], this=False)
comp_spec(0x00419500, "Competition_AllGoldsWon", lambda e: [], this=False)
comp_spec(0x00419880, "Competition_AssignHeatOrder", lambda e: [])
comp_spec(0x00419910, "Competition_JudgeScore", lambda e: [e.rng.randrange(0, 8)])
comp_spec(0x00419CC0, "Competition_AiScore", lambda e: [e.rng.randrange(0, 8)])
comp_spec(0x00419F90, "Competition_MedalDisplayDone", lambda e: [])
comp_spec(0x00419FB0, "Competition_NextHuman", lambda e: [])
comp_spec(0x0041AEE0, "Competition_SortRanking", lambda e: [])
