"""More case generators for the Phase 4.8 probe: gaps, cash icons, stat points, levels, records."""

from __future__ import annotations

import career_state as cs
from career_specs import SPECS, Skip, Spec, base_state, spec
from career_state import u8, u16, u32


# ---- gaps -------------------------------------------------------------------------------------

def gap_state(env):
    """A state in which a random gap record is (usually) active: returns (sampler, level, gap)."""
    rng = env.rng
    if not env.gaps:
        raise Skip()
    gap = rng.choice(env.gaps)
    gid = env.s16(gap + 4) % 10000
    levels = [l for l in range(13) if env.gap_range[l][0] <= gid <= env.gap_range[l][1]]
    level = rng.choice(levels) if levels and rng.random() < 0.9 else rng.randrange(0, 13)
    s, lvl = base_state(env, level=level, mode=1)
    s.put(0x0055FD30, u32(rng.getrandbits(8) & rng.getrandbits(8)))    # goal gaps got
    s.put(0x0055FDE0, u32(rng.getrandbits(8) & rng.getrandbits(8)))    # trick gaps got
    s.put(0x0055FDD8, u32(rng.randrange(0, 12)))                       # goal gap count
    s.put(0x0055FE04, u32(rng.randrange(0, 12)))                       # trick gap count
    s.put(0x0055FDD4, u32(rng.randrange(0, 100)))                      # total gaps
    s.put(0x0056A23C, b"".join(u32(rng.choice([0, rng.getrandbits(32) & rng.getrandbits(32)])) for _ in range(24)))
    return s, lvl, gap


def gap_spec(address, name, kind=None):
    def gen(env, n):
        s, lvl, gap = gap_state(env)
        if kind is not None and env.rng.random() < 0.8:
            same = [g for g in env.gaps if env.s16(g + 2) == kind]
            if same:
                gap = env.rng.choice(same)
                gid = env.s16(gap + 4) % 10000
                levels = [l for l in range(13) if env.gap_range[l][0] <= gid <= env.gap_range[l][1]]
                if levels:
                    s.level(env.rng.choice(levels))
        return [gap], s.pokes
    SPECS.append(Spec(address, name, "gaps", gen))


for _addr, _name, _kind in [
    (0x00414770, "Career_GapActive", None),
    (0x004147B0, "Career_GapIsGoal", 1),
    (0x004147E0, "Career_GapIsTrick", 2),
    (0x00414810, "Career_GapGoalNumber", 1),
    (0x004148F0, "Career_GapTrickNumber", 2),
    (0x004149D0, "Career_GapNumber", None),
    (0x00414A50, "Career_GotGoalGap", 1),
    (0x00414A80, "Career_GotTrickGap", 2),
    (0x00414AB0, "Career_GiveGoalGap", 1),
    (0x00414AE0, "Career_GiveTrickGap", 2),
    (0x00414B10, "Career_AnyoneGotGap", None),
    (0x00414B50, "Career_GiveGap", None),
    (0x00414BD0, "Career_AwardGoalGap", 1),
    (0x00414D90, "Career_AwardTrickGap", 2),
]:
    gap_spec(_addr, _name, _kind)


@spec(0x00414B90, "Career_CountBits", "gaps")
def gen_count_bits(env, n):
    return [env.rng.choice([0, 1, 0xFFFFFFFF, env.rng.getrandbits(32), env.rng.getrandbits(8)])], {}


@spec(0x00414BB0, "Career_NumGoalGapsGot", "gaps")
def gen_num_goal_gaps(env, n):
    s, _, _ = gap_state(env)
    return [], s.pokes


@spec(0x00414BC0, "Career_NumTrickGapsGot", "gaps")
def gen_num_trick_gaps(env, n):
    s, _, _ = gap_state(env)
    return [], s.pokes


@spec(0x00415560, "Career_CountGaps", "gaps")
def gen_count_gaps(env, n):
    s, _, _ = gap_state(env)
    return [], s.pokes


@spec(0x00415590, "Career_GotAllGaps", "gaps")
def gen_got_all_gaps(env, n):
    s, _, _ = gap_state(env)
    return [], s.pokes


# ---- cash icons, pickups, level tally ---------------------------------------------------------

def node_state(env):
    rng = env.rng
    if not env.nodes:
        raise Skip()
    power = [nd for nd in env.nodes if nd[2] == 5]
    s, lvl = base_state(env, mode=1)
    s.put(0x0056E214, u32(env.node_count))   # only the captured nodes are live
    s.put(0x0055FE08, u32(rng.randrange(0, 70)))
    s.put(0x0055FD80, u32(rng.randrange(0, 12)))
    s.put(0x0055FAF0, u32(rng.randrange(0, 12)))
    return s, lvl, power


@spec(0x00414FA0, "Career_MoneyNumber", "money")
def gen_money_number(env, n):
    s, lvl, power = node_state(env)
    rng = env.rng
    i = rng.randrange(0, env.node_count) if (not power or rng.random() < 0.3) else rng.choice(power)[0]
    p = env.nodes[i][1]
    s.put(p, u16(rng.choice([5, 5, 5, 4, 7])))
    s.put(p + 2, u16(rng.getrandbits(16)))
    return [i], s.pokes


def money_gen(env, n, amount=False):
    s, lvl, power = node_state(env)
    rng = env.rng
    i = rng.choice(power)[0] if power and rng.random() < 0.85 else rng.randrange(0, env.node_count)
    p = env.nodes[i][1]
    s.put(p, u16(5))
    s.put(p + 2, u16(rng.getrandbits(16)))
    s.put(0x00568608, b"".join(u32(rng.choice([0, rng.getrandbits(32)])) for _ in range(8)))
    args = [i]
    if amount:
        args.append(rng.choice([0, 100, 500, 1000, 1500]))
    return args, s.pokes


@spec(0x00415060, "Career_GotMoney", "money")
def gen_got_money(env, n):
    return money_gen(env, n)


@spec(0x00415180, "Career_GiveMoney", "money")
def gen_give_money(env, n):
    return money_gen(env, n, amount=True)


@spec(0x004152F0, "Career_GetLevelPickup", "money")
def gen_get_level_pickup(env, n):
    s, lvl, _ = node_state(env)
    rng = env.rng
    s.put(0x0055FD80, u32(rng.choice([0, 1, 2, 4, 5, 11])))
    s.put(0x0055FAF0, u32(rng.choice([0, 1, 3, 5, 6, 12])))
    return [], s.pokes


@spec(0x00415380, "Career_CountThings", "money")
def gen_count_things(env, n):
    s, lvl, power = node_state(env)
    rng = env.rng
    for _ in range(rng.randrange(0, 12)):
        i = rng.randrange(0, env.node_count)
        p = env.nodes[i][1]
        s.put(p, u16(rng.choice([5, 5, 5, 4, 2])))
        s.put(p + 2, u16(rng.choice([0x18, 0x19, 0x1A, 0x21, 0x21, rng.getrandbits(16)])))
    return [], s.pokes


# ---- stat points ------------------------------------------------------------------------------

def custom_records(s, rng):
    for idx in range(4):
        base = cs.CUSTOM + idx * 0x80
        s.put(base + 0x21, u8(rng.choice([0, 1, 4, 5, 0xFF, 3])))
        s.put(base + 0x22, u8(rng.choice([0, 1])))
        s.put(base + 0x25, u8(rng.choice([0, 1])))
        s.put(base + 0x26, u8(rng.choice([0, 1, 0, 0, 5])))
        s.put(base + 0x2C, b"".join(u32(rng.randrange(0, 8)) for _ in range(10)))


@spec(0x004155A0, "Career_BuyPoint", "stats")
def gen_buy_point(env, n):
    s, _ = base_state(env)
    rng = env.rng
    custom_records(s, rng)
    return [rng.choice([rng.randrange(0, 10)] * 4 + [-1, 10]), rng.choice([0, 50, 100, 500, 100000])], s.pokes


@spec(0x004156F0, "Career_SellPoint", "stats")
def gen_sell_point(env, n):
    s, _ = base_state(env)
    rng = env.rng
    custom_records(s, rng)
    return [rng.choice([rng.randrange(0, 10)] * 4 + [-1, 10])], s.pokes


@spec(0x00415820, "Career_GetPointCost", "stats")
def gen_point_cost(env, n):
    s, _ = base_state(env)
    custom_records(s, env.rng)
    return [], s.pokes


# ---- levels -----------------------------------------------------------------------------------

def level_state(env):
    rng = env.rng
    s, lvl = base_state(env, all_characters=True)
    s.put(0x0056A8E4, u32(rng.choice([0, 0, 0, 1])))
    s.put(0x0056A68C, u8(rng.getrandbits(8)))
    return s, lvl


@spec(0x004158D0, "Career_NumLevelsWithMedals", "levels")
def gen_levels_with_medals(env, n):
    s, _ = level_state(env)
    return [pick_record(env, s)], s.pokes


@spec(0x00415910, "Career_LevelOpenCareerMode", "levels")
def gen_level_open_career(env, n):
    s, _ = level_state(env)
    rng = env.rng
    return [rng.choice([rng.randrange(0, 14)] * 6 + [-1]), pick_record(env, s)], s.pokes


@spec(0x004159D0, "Career_LevelOpen", "levels")
def gen_level_open(env, n):
    s, _ = level_state(env)
    rng = env.rng
    return [rng.randrange(0, 13), rng.choice([0, 1, 1, 1, 2, 3, 4, 5, 9])], s.pokes


@spec(0x00415A90, "Career_HighestOpenLevel", "levels")
def gen_highest_open(env, n):
    s, _ = level_state(env)
    rng = env.rng
    return [rng.choice([0, 1, 1, 2, 3, 5]), rng.choice([0, 1])], s.pokes


@spec(0x00415AE0, "Career_LevelNeeds", "levels")
def gen_level_needs(env, n):
    s, _ = level_state(env)
    return [env.rng.randrange(0, 13)], s.pokes


# ---- records ----------------------------------------------------------------------------------

def pick_record(env, s):
    """A random character's record address, with random state poked into it. (A null record makes
    retail fault right after the assert, so the assert path of these functions is never reached
    with one.)"""
    c = env.rng.randrange(0, 21)
    s.record(c)
    return cs.RECORDS + c * cs.RECORD_STRIDE


@spec(0x00416070, "Career_IsTrickPurchased", "records")
def gen_is_trick_purchased(env, n):
    s, _ = base_state(env, all_characters=True)
    rng = env.rng
    return [pick_record(env, s), rng.choice([rng.randrange(0, 128)] * 5 + [-1, 128, 200])], s.pokes


@spec(0x004164B0, "Career_CountGoldMedals", "records")
def gen_count_gold(env, n):
    s, _ = base_state(env, all_characters=True)
    return [pick_record(env, s)], s.pokes


@spec(0x00416530, "Career_CountMedals", "records")
def gen_count_medals(env, n):
    s, _ = base_state(env, all_characters=True)
    return [pick_record(env, s)], s.pokes


@spec(0x00416740, "Career_GameCleared", "records")
def gen_game_cleared(env, n):
    s, _ = base_state(env, all_characters=True)
    rng = env.rng
    c = rng.randrange(0, 20)
    if rng.random() < 0.6:   # make the record plausibly "cleared"
        rec = cs.RECORDS + c * cs.RECORD_STRIDE
        s.put(rec, u8(0x32))
        s.put(rec + 3, u8(3))
        for off in (0x10, 0x16, 0x1A):
            s.put(rec + off, u16(0x83FF | rng.choice([0, 0x8000, 0x8000])))
    return [c], s.pokes


@spec(0x00416790, "Career_ClearGame", "records")
def gen_clear_game(env, n):
    s, _ = level_state(env)
    return [env.rng.randrange(0, 20)], s.pokes


@spec(0x00416890, "Career_ClearGameWithEveryone", "records")
def gen_clear_everyone(env, n):
    s, _ = level_state(env)
    return [], s.pokes


# ---- Gap_CompleteDefinition (12 s4) ------------------------------------------------------------

EARLY_GAP_TABLE = 0x00541990   # levels 0-9 (and 13) use this table of 415 records, whose definitions have the goal (+2 == 1) and trick (+2 == 2) kinds; the Hangar the probe
                               # runs in has the late table loaded, which has none, so the spec points the table pointer (0x0056dd58) at this one


def early_gaps(env):
    if not hasattr(env, "early_gap_list"):
        env.early_gap_list = []
        at = EARLY_GAP_TABLE
        while env.s16(at + 4) != -1:
            env.early_gap_list.append(at)
            at += 0x2C
    return env.early_gap_list


@spec(0x00414C50, "Gap_CompleteDefinition", "gapc")
def gen_complete_gap(env, n):
    """A completed gap definition in a random level and game mode, with a gap definition already parked on the player half the time."""
    rng = env.rng
    s, lvl, gap = gap_state(env)
    table = early_gaps(env) if rng.random() < 0.9 else env.gaps
    if table is not env.gaps:
        s.put(0x0056DD58, u32(EARLY_GAP_TABLE))
    roll = rng.random()
    kind = 1 if roll < 0.45 else 2 if roll < 0.8 else None
    same = [g for g in table if kind is None or env.s16(g + 2) == kind]
    gap = rng.choice(same) if same else gap
    gid = env.s16(gap + 4) % 10000
    levels = [l for l in range(13) if env.gap_range[l][0] <= gid <= env.gap_range[l][1]]
    level = rng.choice(levels) if levels and rng.random() < 0.85 else rng.choice([0, 3, 9, 10, 11, 12, 13])
    s.put(cs.CURRENT_LEVEL, u32(level))
    s.game_mode(rng.choice([1] * 8 + [0, 2, 3, 7]))
    s.put(0x005614C4, u32(rng.choice([0] * 9 + [1])))                  # the custom park flag
    if rng.random() < 0.8:                                             # the goals already got in the level decide the branches
        s.put(0x0055FD7C, u32(rng.getrandbits(10) & rng.getrandbits(10)))
    goal_gaps = [g for g in table if env.s16(g + 2) == 1]
    trick_gaps = [g for g in table if env.s16(g + 2) == 2]
    for player in env.players[:1]:
        s.put(player + 0x3028, u32(rng.choice([0, rng.choice(goal_gaps)] if goal_gaps else [0])))
        s.put(player + 0x3024, u32(rng.choice([0, rng.choice(trick_gaps)] if trick_gaps else [0])))
    return [gap], s.pokes
