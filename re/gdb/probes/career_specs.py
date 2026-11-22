"""Case generators for the Phase 4.8 forced-call probe (career_calls.py).

Every spec names one retail function and produces (args, pokes) for each case: the arguments of
the call and the memory writes that put the game in a random legal state first.  The native replay
test (src/runtime/career_retail_calls_test.cpp) maps the same addresses to the ported functions.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Callable

import career_state as cs
from career_state import u8, u16, u32

PLAYER_SCORE = 0x16C
PLAYER_CHARACTER = 0x2CC0
PLAYER_TAPES = 0x31E4
PLAYER_MEDAL = 0x31E8


class Skip(Exception):
    """This case cannot be built from the current game state."""


@dataclass
class Spec:
    address: int
    name: str
    group: str
    gen: Callable
    this: int | None = None


SPECS: list[Spec] = []


def spec(address, name, group, this=None):
    def deco(fn):
        SPECS.append(Spec(address, name, group, fn, this))
        return fn
    return deco


def select(groups, only):
    out = [s for s in SPECS if s.group in groups]
    if only:
        out = [s for s in out if s.address in only]
    return out


class Env:
    """What the generators know about the running game (read once, from the base snapshot)."""

    def __init__(self, rng, base):
        self.rng = rng
        self.base = base
        self.contest = [self.u8(cs.LEVELS + i * cs.LEVEL_STRIDE + 0x48) != 0 for i in range(13)]
        self.gap_range = [(self.s32(cs.LEVELS + i * cs.LEVEL_STRIDE + 0xB0), self.s32(cs.LEVELS + i * cs.LEVEL_STRIDE + 0xB4))
                          for i in range(13)]
        self.players = [p for p in (self.u32(cs.PLAYERS), self.u32(cs.PLAYERS + 4)) if p]
        self.gaps = []
        table = self.u32(0x0056DD58)
        if table:
            at = table
            while self.s16(at + 4) != -1:
                self.gaps.append(at)
                at += 0x2C
        # Only the first NODE_SAMPLE trigger nodes are captured; the specs poke NumNodes down to that.
        self.node_count = min(self.s32(0x0056E214), cs.NODE_SAMPLE)
        self.node_array = self.u32(0x0056E210)
        self.nodes = []
        for i in range(self.node_count if self.node_array else 0):
            p = self.u32(self.node_array + i * 4)
            self.nodes.append((i, p, self.s16(p)))
        self.goal_types = [[self.s32(cs.LEVELS + l * cs.LEVEL_STRIDE + 0xB8 + g * 0x18) for g in range(10)] for l in range(13)]

    # ---- base memory --------------------------------------------------------------------
    def bytes(self, address, size):
        for base, data in self.base.items():
            if base <= address and address + size <= base + len(data):
                return data[address - base:address - base + size]
        raise KeyError(f"{address:#x}")

    def u8(self, a): return self.bytes(a, 1)[0]
    def u16(self, a): return struct.unpack("<H", self.bytes(a, 2))[0]
    def s16(self, a): return struct.unpack("<h", self.bytes(a, 2))[0]
    def u32(self, a): return struct.unpack("<I", self.bytes(a, 4))[0]
    def s32(self, a): return struct.unpack("<i", self.bytes(a, 4))[0]

    # ---- per case ------------------------------------------------------------------------
    def begin_case(self):
        pass

    def video(self, _args=None):
        return self.rng.choice([0, 0, 1])

    def pack(self, _args=None):
        return self.rng.choice([0, 0, 1])

    def rnd(self, args):
        limit = args[0] if args else 0
        limit = limit if limit < 0x7FFFFFFF else 0
        return self.rng.randrange(limit) if limit > 0 else 0


def install_stubs(fc, env):
    fc.stub(0x004011E0, 2, "assert", when=lambda a: (a[0] & 0xFF) == 0, keep=slice(1, 2))
    fc.stub(0x004AD620, 3, "sfx")
    fc.stub(0x004736F0, 5, "message", text=True)
    fc.stub(0x004736C0, 1, "message_prepare")
    fc.stub(0x0048FD60, 3, "cheat_effect", convention="stdcall")
    fc.stub(0x004BE350, 0, "disco_start")
    fc.stub(0x004CDE70, 0, "video_playing", result=env.video)
    fc.stub(0x004DE150, 0, "pack_new_free", result=env.pack)
    fc.stub(0x004C9340, 1, "rnd", result=env.rnd)
    fc.stub(0x0040B500, 1, "rand", result=env.rnd)
    fc.stub(0x004CF850, 1, "printf_log")
    fc.stub(0x004236D0, 0, "reload_custom_skaters")


# ---- shared state -----------------------------------------------------------------------------

def base_state(env, level=None, mode=None, all_characters=False, loaded=False):
    """A random career state: the active character's record (and the player's), the manager, the
    level and game mode, the player's score. `all_characters` randomizes every record."""
    rng = env.rng
    s = cs.Sampler(rng, env.contest)
    active = s.manager(loaded=loaded)
    lvl = s.level(level)
    s.game_mode(mode if mode is not None else (1 if rng.random() < 0.9 else rng.choice([0, 2, 3, 7, 9])))
    player_characters = []
    for player in env.players:
        s.put(player + PLAYER_SCORE, u32(rng.choice([0, rng.randrange(0, 12000), rng.randrange(0, 200000), 0xFFFFFF00])))
        pc = rng.choice([active, rng.randrange(0, 20)])
        player_characters.append(pc)
        s.put(player + PLAYER_CHARACTER, u32(pc))
        s.put(player + PLAYER_TAPES, u32(rng.randrange(0, 50)))
    if all_characters:
        s.all_records()
    else:
        for c in {active, *player_characters}:
            s.record(c)
    s.put(0x0055FD7C, u32(rng.getrandbits(10) & rng.getrandbits(10)))   # goals got this level
    s.put(0x0055FE08, u32(rng.choice([0, 1, 3, rng.randrange(0, 70)])))  # cash icons in the level
    return s, lvl


def goal_arg(env, rng):
    return rng.choice([rng.randrange(0, 10)] * 6 + [-1, 10, 11, 31])


# ---- goals ------------------------------------------------------------------------------------

@spec(0x00414030, "Career_Got", "goals")
def gen_got(env, n):
    s, _ = base_state(env)
    return [goal_arg(env, env.rng)], s.pokes


@spec(0x004140C0, "Career_JustGot", "goals")
def gen_just_got(env, n):
    s, _ = base_state(env)
    return [goal_arg(env, env.rng)], s.pokes


@spec(0x00414130, "Career_LevelTapes", "goals")
def gen_level_tapes(env, n):
    s, _ = base_state(env)
    rng = env.rng
    c = rng.randrange(0, 21)
    s.record(c)
    rec = cs.RECORDS + c * cs.RECORD_STRIDE   # (a null record faults in retail, see record_or_null)
    return [rec, rng.randrange(0, 13)], s.pokes


@spec(0x004141A0, "Career_NumLevelGoals", "goals")
def gen_num_level_goals(env, n):
    s, _ = base_state(env)
    return [], s.pokes


@spec(0x004141D0, "Career_CountMoney", "goals")
def gen_count_money(env, n):
    s, _ = base_state(env)
    return [], s.pokes


@spec(0x00414280, "Career_CheckScore", "goals")
def gen_check_score(env, n):
    s, lvl = base_state(env, mode=1)
    rng = env.rng
    if not env.players:
        raise Skip()
    # put the score near a goal threshold half of the time
    row = cs.LEVELS + lvl * cs.LEVEL_STRIDE + 0xB8
    thresholds = [env.s32(row + g * 0x18 + 4) for g in range(10) if env.s32(row + g * 0x18) == 0]
    if thresholds and rng.random() < 0.6:
        s.put(env.players[0] + PLAYER_SCORE, u32(rng.choice(thresholds) + rng.choice([-1, 0, 1, 500])))
    return [], s.pokes


@spec(0x004142E0, "Career_GiveGoal", "goals")
def gen_give_goal(env, n):
    s, _ = base_state(env)
    return [goal_arg(env, env.rng)], s.pokes


@spec(0x00414540, "Career_GoalIndex", "goals")
def gen_goal_index(env, n):
    s, _ = base_state(env)
    return [env.rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1])], s.pokes


@spec(0x00414570, "Career_GiveGoalType", "goals")
def gen_give_goal_type(env, n):
    s, lvl = base_state(env, mode=1)
    types = [t for t in env.goal_types[lvl] if 0 <= t <= 7]
    if not types:
        raise Skip()
    return [env.rng.choice(types)], s.pokes


@spec(0x004145E0, "Career_CheckClear", "goals")
def gen_check_clear(env, n):
    s, lvl = base_state(env, mode=1)
    rng = env.rng
    # make "all cash icons collected" likely: a full mask for the character's level
    s.put(0x0055FE08, u32(rng.choice([0, 1, 2, 5, 64])))
    return [], s.pokes


@spec(0x00414650, "Career_GotGoalType", "goals")
def gen_got_goal_type(env, n):
    s, _ = base_state(env)
    return [env.rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8])], s.pokes


@spec(0x00414670, "Career_GiveMedal", "goals")
def gen_give_medal(env, n):
    s, _ = base_state(env)
    rng = env.rng
    return [rng.choice([rng.randrange(0, 13)] * 5 + [-1, 13]), rng.choice([10, 11, 12, 10, 11, 12, 0, 3, 16])], s.pokes
