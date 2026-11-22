"""Phase 4.15a probe: the rail events and the terrain effects of physics.cpp (group phe): the registration of a rail's trick object (0x00490ae0), the completion of a rail's gap (0x00490c70), the handplant
(0x00490e30), the glass of an acknowledged tag (0x00493f40) and the terrain dust and rumble (0x00495af0).

Like phys_specs.py: the live player-one skater (all of it captured) and a scratch slab; the level's node table is the one of rail_specs.py (the same slab layout, so the rail walk `Rail_PrevNodeQuick` and the
accessors are the real code); the trigger layer (node links 0x004c8550, commands 0x004c5b00, the gap routines 0x0048f690 / 0x00414c50), the environment item lookup (0x004b1ef0), the trick object registration
(0x0048f800 / 0x004bd980), the take-off (0x0048f5f0), the sound, the animation player, the angle converter and the effect layer (0x00406070..0x0040b060, 0x0046f2c0, 0x00403d30, 0x004de5c0) are stubs. The vector add of
the dust (0x004ca9f0), `Vibrate`, the state change and the animation requests are the real code.

Slab layout (offsets; the node table is rail_specs.py's: pointers 0x000, nodes 0x100, link tables 0x300, trigger words 0x700, lookup 0xb00): the gap definitions at 0xd00 (0x2c bytes each, five), the chain of
script objects at 0xe00 (0x20 each, four), the scripts at 0xf00 (0x40 each, four), the environment item at 0x1000 (0x40 bytes), the effect objects at 0x1100 (0x100 each, eight).
"""

from __future__ import annotations

import phys_specs as ps
import phys_specs_misc as pm
import phys_specs_stats as pk
import phys_specs_trick as pt
import rail_specs as rs
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("phe",)

SLAB = 0x1E00
BAIL_STATES = 0x1B00           # the state script offsets (16 shorts) and the grind script offsets (0xe0 shorts) the bail reads
BAIL_GRIND = 0x1B40
STATS_CONFIG = 0x1900          # the trick configuration record the stat machinery reads (0x60 bytes)
PAD = 0x1A00                   # the pad record of the rail physics
GAPS = 0xD00
GAP_STRIDE = 0x2C
CHAIN = 0xE00
CHAIN_STRIDE = 0x20
SCRIPTS = 0xF00
SCRIPT_STRIDE = 0x40
ITEM = 0x1000
OBJECTS = 0x1100
OBJECT_STRIDE = 0x100

SCRIPT_LIST = 0x0056E220
GAP_TABLE = 0x0056DD58
EFFECT_GATE = 0x005244CC
PARTICLES = 0x0055F72C

SCRATCH_SHORTS = 0x7FF00E40      # what the three shorts handed to the effect layer are recorded as (pc_platform.hpp kScratchBase + 0x640)
SCRATCH_VECTOR = 0x7FF00E60      # the glass direction (kScratchBase + 0x660)

WINDOWS = rs.WINDOWS + [(0x0056B860, 4)] + ps.WINDOWS + pt.WINDOWS + pk.WINDOWS + [(0x0056AF28, 0x0C), (0x00567C7C, 4), (0x00568804, 4), (0x0056B7EC, 4), (pm.GTE, pm.GTE_BYTES)] + [(SCRIPT_LIST, 4), (GAP_TABLE, 4), (EFFECT_GATE, 4), (PARTICLES, 4)]

STATE = {"item": 0, "takeoff": None, "allocs": [], "free": [], "hits": [], "rnd": [], "segments": []}


def phe_regions(slab):
    return merged([(slab, SLAB), (ps.skater(), ps.SKATER_BYTES)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = phe_regions
    READ_ONLY[_group] = [(rs.RAIL_VTABLE, 0x10)]
    SEEDS[_group] = rs.rln_seed


def phe(address, name):
    return spec(address, name, "phe", this=lambda env: ps.skater())


def install_event_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test

    if getattr(fc, "_phe_installed", False):
        return
    fc._phe_installed = True
    mark_under_test()
    slab = env.slab
    pk.install_stat_stubs(fc, env)
    INFO = 0x7FF00D00
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO, pokes=[(0, None, lambda a: STATE["hits"].pop(0) if STATE["hits"] else bytes(0x50), 0x40)])
    ensure(fc, 0x0049D080, 0, "engine_0049d080", convention="thiscall", this=True)
    SA = 0x7FF00800 + 0xC00
    ensure(fc, 0x00465830, 8, "engine_00465830", stack_to={2: SA, 3: SA + 0x10, 4: SA + 0x20, 5: SA + 0x30, 6: SA + 0x40, 7: SA + 0x50},
           pokes=[(6, None, lambda a: (STATE["segments"] or [(bytes(12), 0)])[0][0])], result=lambda a: STATE["segments"].pop(0)[1] if STATE["segments"] else 0)
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: STATE["rnd"].pop(0) if STATE["rnd"] else 0)
    # the node table (rail_specs.py)
    ensure(fc, 0x004C8650, 2, "engine_004c8650", stack_to=rs.POSITION, fill=(0, lambda a: rs.STATE["pos"].get(a[1], bytes(12))), result=lambda a: slab + rs.TRIG + 0x10 * a[1])
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: slab + rs.LINKS + 0x20 * a[0])
    # the skater's own services (phys_specs*.py)
    ensure(fc, 0x0048D660, 2, "engine_0048d660")
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004DECE0, 0, "engine_004dece0", result=lambda a: ps.STATE["agree"])
    ensure(fc, 0x00466C10, 0, "engine_00466c10")
    ensure(fc, 0x0048F720, 1, "engine_0048f720", convention="thiscall", this=True)
    ensure(fc, 0x0048E8A0, 0, "engine_0048e8a0", convention="thiscall", this=True)
    ensure(fc, 0x004657C0, 2, "engine_004657c0", stack_to=pm.SCRATCH_ANGLES, fill=(0, lambda a: pm.STATE["angles"]))
    ensure(fc, 0x0042F700, 1, "engine_0042f700")
    ensure(fc, 0x0042F680, 1, "engine_0042f680")
    ensure(fc, 0x0048CD40, 0, "engine_0048cd40", convention="thiscall", this=True, result=lambda a: pt.STATE["landed"])
    ensure(fc, 0x0048D7B0, 1, "engine_0048d7b0")
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: pt.STATE["movie"])
    ensure(fc, 0x004896A0, 4, "engine_004896a0")
    ensure(fc, 0x004DE010, 1, "engine_004de010", result=lambda a: pt.STATE["remote"])
    ensure(fc, 0x004DE4D0, 2, "engine_004de4d0")
    # the rail events
    ensure(fc, 0x004B1EF0, 1, "engine_004b1ef0", result=lambda a: STATE["item"])
    ensure(fc, 0x0048F800, 1, "engine_0048f800", convention="thiscall", this=True)
    ensure(fc, 0x004BD980, 3, "engine_004bd980")
    ensure(fc, 0x0048F690, 4, "engine_0048f690", convention="thiscall", this=True)
    ensure(fc, 0x00414C50, 1, "engine_00414c50")
    ensure(fc, 0x004C5B00, 1, "engine_004c5b00")
    manual = ps.skater() + 0x3064

    def take_off_effect(a):
        # the take-off may put the skater into the air (+0x3064): a case says whether, else the field stays as it was
        value, STATE["takeoff"] = STATE["takeoff"], None
        return mem.u32(manual) if value is None else value

    ensure(fc, 0x0048F5F0, 0, "engine_0048f5f0", convention="thiscall", this=True, writes=[(manual, 4, take_off_effect)])
    # the effect layer
    ensure(fc, 0x00406070, 1, "engine_00406070")
    ensure(fc, 0x00406090, 1, "engine_00406090", stack_to=SCRATCH_SHORTS, blob=(0, 6))
    ensure(fc, 0x00406160, 1, "engine_00406160")
    ensure(fc, 0x004061C0, 3, "engine_004061c0")
    ensure(fc, 0x004061E0, 3, "engine_004061e0")
    ensure(fc, 0x0046F2C0, 1, "engine_0046f2c0", result=lambda a: STATE["free"].pop(0) if STATE["free"] else 0)
    ensure(fc, 0x00403D30, 1, "engine_00403d30", result=lambda a: STATE["allocs"].pop(0) if STATE["allocs"] else 0)
    ensure(fc, 0x00406200, 4, "engine_00406200", convention="thiscall", this=True, result=lambda a: a[0])
    ensure(fc, 0x0040B060, 5, "engine_0040b060", convention="thiscall", this=True, stack_to={1: SCRATCH_VECTOR}, blob=(1, 12))
    ensure(fc, 0x004DE5C0, 1, "engine_004de5c0")
    ensure(fc, 0x00461BF0, 1, "engine_00461bf0")


GROUP_STUBS = {group: install_event_stubs for group in GROUPS}


# ---- the worlds -------------------------------------------------------------------------------------------------------------------------------------------------------------

def node_type(img, env, k):
    address = env.slab + rs.NODES + 0x10 * k
    low = img.b.get(address, None)
    high = img.b.get(address + 1, None)
    if low is None:
        low = rs.base_byte(env, address)
    if high is None:
        high = rs.base_byte(env, address + 1)
    return low | (high << 8)


def object_world(img, env, rng, count, *, objects=True):
    """The node table (forward links only: the walks end) with a few trick object nodes (type 0xe or 0xc: a count of shorts at +2, then, on a 4-byte boundary, the identifier) and some rail nodes linked to them.
    Returns the limit of the written slots and the nodes that are objects."""
    slab = env.slab
    limit = rs.make_world(img, env, rng, count, rails=rng.choice([0.6, 0.9, 1.0]), links="forward")
    found = []
    if objects:
        for k in rng.sample(range(limit), min(limit, rng.choice([0, 1, 1, 2, 2, 3]))):
            node = slab + rs.NODES + 0x10 * k
            img.u16(node, rng.choice([0xE, 0xC, 0xE, 0xC, 0x64]))        # (never a rail node: a link back to one would make a rail walk run for ever)
            shorts = rng.choice([0, 1, 2, 3, 4])
            img.u16(node + 2, shorts)
            at = node + 4 + 2 * shorts
            if at & 3:
                at += 2
            img.u32(at, rng.choice([1, 2, 3, 0x1234, 0, rng.getrandbits(32)]))
            found.append(k)
        for k in range(limit):
            if node_type(img, env, k) == 0xB and found and rng.random() < 0.55:
                table = slab + rs.LINKS + 0x20 * k
                ahead = list(range(k + 1, limit))
                targets = [rng.choice(found), rng.choice(ahead) if ahead else rng.choice(found), rng.choice(found)][: rng.choice([1, 2, 2, 3])]       # (a link back would make a rail walk run for ever)
                if rng.random() < 0.5:
                    targets.reverse()
                img.u16(table, len(targets))
                for i, target in enumerate(targets):
                    img.u16(table + 2 + 2 * i, target)
    return limit, found


def rail_node(img, env, rng, limit):
    """A node to start from: mostly a rail start node (type 0xb)."""
    nodes = [k for k in range(limit) if node_type(img, env, k) == 0xB]
    return rng.choice(nodes) if nodes and rng.random() < 0.85 else rng.randrange(0, limit)


def state_noise(img, rng, sk):
    """The fields the state change and the animation requests read."""
    img.u32(sk + 0x30B8, rng.choice(ps.STATES))
    img.u32(sk + 0x3044, rng.choice([0, 1, 5, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(sk + 0xD8, rng.getrandbits(32))
    img.u16(sk + 0xF6, rng.choice([0x23, 0x23, 0, 1, 0x82, rng.getrandbits(16)]))
    img.u16(sk + 0xF4, rng.choice([0, 0xF, 0x10, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u32(sk + 0x2DD0, ps.word(rng))
    img.u32(sk + 0x2C78, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(sk + 0x2C7C, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(sk + 0x2C08, ps.word(rng))
    for k in range(9):
        img.u32(sk + 0x30F4 + 4 * k, ps.basis_word(rng))
    for k in range(3):
        img.u32(sk + 0x3118 + 4 * k, ps.word(rng))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))


# ---- the rail events --------------------------------------------------------------------------------------------------------------------------------------------------------

@phe(0x00490AE0, "Skater_RegisterRailTrickObject")
def gen_register(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    count = rs.count_of(rng)
    limit, _ = object_world(img, env, rng, count)
    node = rail_node(img, env, rng, limit)
    start = rng.randrange(0, count)
    img.u32(ps.GAME_MODE, rng.choice([8, 8, 8, 1, 0, 2]))
    img.u32(sk + 0x2E9C, start | ((rng.getrandbits(16) << 16) if rng.random() < 0.1 else 0))
    STATE["item"] = env.slab + ITEM if rng.random() < 0.85 else 0
    return [node | ((rng.getrandbits(16) << 16) if rng.random() < 0.1 else 0)], rs.changed(env, img)


def gap_world(img, env, rng, found):
    """The gap definitions (five entries, the last one the end), the chain of script objects and the scripts; the identifiers are the ones the object nodes carry."""
    slab = env.slab
    entries = rng.choice([0, 1, 2, 3, 4])
    numbers = rng.sample(range(0, 8), entries)
    for k in range(5):
        entry = slab + GAPS + GAP_STRIDE * k
        img.u16(entry + 4, numbers[k] if k < entries else 0xFFFF)
        img.u16(entry + 6, ps.short(rng) & 0xFFFF)
        img.put(entry + 8, bytes(rng.getrandbits(8) for _ in range(8)))
    img.u32(GAP_TABLE, slab + GAPS)
    scripts = rng.choice([0, 1, 2, 3, 4])
    img.u32(SCRIPT_LIST, slab + CHAIN if scripts else 0)
    for k in range(scripts):
        chain = slab + CHAIN + CHAIN_STRIDE * k
        script = slab + SCRIPTS + SCRIPT_STRIDE * k
        img.u32(chain, script)
        img.u32(chain + 0x14, slab + CHAIN + CHAIN_STRIDE * (k + 1) if k + 1 < scripts else 0)
        img.u16(script, rng.choice([0xC9, 0xC9, 0xC9, 0xC8, rng.getrandbits(16)]))
        at = (script + 5) & ~3
        img.u32(at, rng.choice([1, 2, 3, 0x1234, rng.getrandbits(32)]))
        img.u16(at + 4, rng.choice(numbers + [0xFFFF, 9]) if numbers else 9)


@phe(0x00490C70, "Skater_CompleteRailGap")
def gen_complete_gap(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    count = rs.count_of(rng)
    limit, found = object_world(img, env, rng, count)
    gap_world(img, env, rng, found)
    node = rail_node(img, env, rng, limit)
    img.u32(sk + 0x2EA0, node)
    img.u32(sk + 0x2E9C, rng.randrange(0, count) | ((rng.getrandbits(16) << 16) if rng.random() < 0.1 else 0))
    img.u32(sk + 0x2C70, ps.word(rng))
    return [], rs.changed(env, img)


@phe(0x00490E30, "Skater_EnterHandplant")
def gen_handplant(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    count = rs.count_of(rng)
    limit, _ = object_world(img, env, rng, count)
    state_noise(img, rng, sk)
    img.u32(sk + 0x3064, rng.choice([0, 0, 1, 5]))
    STATE["takeoff"] = rng.choice([None, 0, 1, 1, 7])
    img.u32(sk + 0x2EA0, rail_node(img, env, rng, limit))
    img.u32(sk + 0x2E9C, rng.randrange(0, count))
    for field in (0x2F54, 0x2E94, 0x2EC8, 0x4C, 0x50, 0x54):
        img.u32(sk + field, ps.word(rng))
    for k in range(3):
        img.u32(sk + 0x2EA8 + 4 * k, ps.word(rng))
    img.u32(ps.GAME_MODE, rng.choice([8, 8, 1, 0, 2]))
    STATE["item"] = env.slab + ITEM if rng.random() < 0.9 else 0
    return [], rs.changed(env, img)


# ---- the effects ------------------------------------------------------------------------------------------------------------------------------------------------------------

def objects_for_alloc(env, count):
    return [env.slab + OBJECTS + OBJECT_STRIDE * k for k in range(count)]


@phe(0x00493F40, "Skater_SpawnTagGlass")
def gen_glass(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.u32(PARTICLES, rng.choice([0, 100, 248, 249, 250, 251, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.u32(EFFECT_GATE, rng.choice([0, 1, rng.getrandbits(32)]))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    img.put(sk + 0x14, bytes(rng.getrandbits(8) for _ in range(6)))
    pm.STATE["angles"] = bytes(rng.getrandbits(8) for _ in range(6))
    objects = objects_for_alloc(env, 8)
    STATE["allocs"] = [rng.choice([objects[k], objects[k], objects[k], 0]) for k in range(9) if k < 8] + [0]
    return [], rs.changed(env, img)


@phe(0x00495AF0, "CBruce__DoTerrainEffects")
def gen_terrain(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    scale = rng.choice([0x1000, 0x8000, 0xC000, 0x10000, 0x14000, 0x20000, 0x40000, 0x100000, 0x7FFFFFFF])
    for k in range(3):
        img.u32(sk + 0x4C + 4 * k, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF if rng.random() < 0.9 else rng.choice([0, 0x7FFFFFFF, 0x80000000]))
    img.u32(sk + 0x30B0, rng.choice([0, 1, 2, 3, 3, 4, 5, 5, 6, 6, 7, 0xFFFFFFFF]))
    img.u32(PARTICLES, rng.choice([0, 100, 249, 250, 251, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.u32(EFFECT_GATE, rng.choice([0, 1, rng.getrandbits(32)]))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    img.put(sk + 0x14, bytes(rng.getrandbits(8) for _ in range(6)))
    for k in range(3):
        img.u32(sk + 8 + 4 * k, ps.word(rng))
    pm.STATE["angles"] = bytes(rng.getrandbits(8) for _ in range(6))
    img.u32(ps.GAME_MODE, rng.choice([0, 1, 7, 7, 9]))
    img.u32(sk + 0x2CC4, rng.choice([0, 1]))
    img.u32(pt.SECOND_PLAYER_MASK, rng.choice([0, 1]))
    img.u32(pt.VIBRATION, rng.choice([0, 1, 2]))
    img.u32(pt.VIBRATION + 4, rng.choice([0, 1, 2]))
    pt.STATE["movie"] = rng.choice([0, 0, 1, 0x100])
    objects = objects_for_alloc(env, 6)
    STATE["allocs"] = [objects[k] for k in range(6)]
    STATE["free"] = [rng.choice([20000, 10001, 10000, 5000, 0, 0x7FFFFFFF, 0xFFFFFFFF, rng.getrandbits(32)]) for _ in range(6)]
    for obj in objects:
        for k in range(3):
            img.u32(obj + 0x1C + 4 * k, ps.word(rng))
    return [], rs.changed(env, img)


@phe(0x0048EC00, "CBruce__TempTransform")
def gen_temp_transform(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    img.put(pm.GTE, bytes(rng.getrandbits(8) for _ in range(pm.GTE_BYTES)))
    img.u32(sk + 0x2C88, rng.choice([0, 0, 1, 5, ps.word(rng)]))
    img.u32(sk + 0x2CAC, rng.choice([0, 0, 0x100, 0x7FF, 0xFFF, rng.getrandbits(32)]))
    img.u32(sk + 0x2CB4, rng.choice([0, 0, 0x100, 0x800, 0xFFF, rng.getrandbits(32)]))
    img.u32(sk + 0x2C8C, rng.choice([0, 0x400, 0xC00, 0x1234, rng.getrandbits(32)]))
    img.u32(sk + 0x2F14, rng.choice([0, 0, 1, 9, 10, 100, -100 & 0xFFFFFFFF, -7 & 0xFFFFFFFF, 1234, rng.randrange(-5000, 5000) & 0xFFFFFFFF, rng.getrandbits(32)]))
    img.put(sk + 0x2E58, b"".join((ps.short(rng) & 0xFFFF).to_bytes(2, "little") for _ in range(9)))
    img.put(sk + 0x2E6A, bytes(rng.getrandbits(8) for _ in range(0x16)))
    for k in range(3):
        img.u32(sk + 8 + 4 * k, rng.getrandbits(32) if rng.random() < 0.3 else (rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF))
    for k in range(11):
        img.u32(sk + 0x30C8 + 4 * k, rng.getrandbits(32))
    return [], rs.changed(env, img)


def rail_path(img, env, rng, count):
    """A rail of `count` nodes in a chain (each leads to the next), every node a rail node of one kind, and the nodes at points of a path."""
    slab = env.slab
    rs.STATE["pos"].clear()
    level = rng.choice(rs.LEVELS)
    img.u32(rs.CURRENT_LEVEL, level)
    img.u32(rs.PLAYERS, 1)
    img.u32(rs.OFFSET_LIST, slab + rs.NODE_PTRS)
    img.u32(rs.NUM_NODES, count)
    shift = 6 if (level >= 10 and level != 13) else 0
    base = [rng.randrange(-0x400000, 0x400000) for _ in range(3)]
    step = [rng.choice([0x20000, 0x40000, 0x80000, -0x40000, 0, 0x10000]) for _ in range(3)]
    if step == [0, 0, 0]:
        step = [0x40000, 0, 0]
    points = []
    for k in range(min(rs.SLOTS, count + 3)):
        node = slab + rs.NODES + 0x10 * k
        img.u16(node, rng.choice([0x0A, 0x0A, 0x0B]))
        img.u16(slab + rs.TRIG + 0x10 * k + shift, rng.getrandbits(4))
        point = [base[j] + k * step[j] + rng.randrange(-0x8000, 0x8000) for j in range(3)]
        points.append(point)
        rs.STATE["pos"][k] = b"".join((v & 0xFFFFFFFF).to_bytes(4, "little") for v in point)
        table = slab + rs.LINKS + 0x20 * k
        img.u16(table, 1 if k + 1 < min(rs.SLOTS, count + 3) else 0)
        if k + 1 < rs.SLOTS:
            img.u16(table + 2, k + 1)
    return points


@phe(0x00494210, "Skater_UpdateRailPhysics")
def gen_rail_physics(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    pk.CONFIG = STATS_CONFIG
    pk.stats_world(img, env, rng, sk)
    count = rng.choice([3, 4, 5, 6, 8, 12])
    points = rail_path(img, env, rng, count)
    node = rng.randrange(0, count - 1)
    img.u32(sk + 0x2EA0, node)
    img.u32(sk + 0x2E9C, rng.randrange(0, node + 1))
    t = rng.choice([-0.4, -0.05, 0.0, 0.2, 0.5, 0.8, 0.95, 1.0, 1.1, 1.6, 2.5])
    here = [int(points[node][j] + (points[node + 1][j] - points[node][j]) * t) + rng.randrange(-0x400, 0x400) for j in range(3)]
    for j in range(3):
        img.u32(sk + 8 + 4 * j, here[j] & 0xFFFFFFFF)
        img.u32(sk + 0xBC + 4 * j, (here[j] + rng.choice([0, 0, rng.randrange(-0x4000, 0x4000)])) & 0xFFFFFFFF)
    scale = rng.choice([0x2000, 0x10000, 0x40000, 0x100000])
    for j in range(3):
        img.u32(sk + 0x4C + 4 * j, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
        img.u32(sk + 0x58 + 4 * j, rng.randrange(-scale, scale + 1) & 0xFFFFFFFF)
    img.u32(0x00568804, rng.choice([0x100, 0x1000, 0x80, rng.randrange(0, 0x2000)]))
    img.u32(ps.ANIMATION_SCALE, rng.choice([0x100, 0x80, 0x200, 0x1000, rng.randrange(0, 0x2000)]))
    img.u32(0x0056B7EC, rng.choice([0, 0, 1]))
    pad = slab + PAD
    img.u32(sk + 0x2CCC, pad)
    img.u8(pad + 0x80, rng.choice([0, 0, 1]))
    img.u8(pad + 0x90, rng.choice([0, 0, 1]))
    img.u8(sk + 0x31A1, rng.choice([0, 0, 0x10, 0xF0, 0x28, 0x29, 0xD8, 0xD7, rng.getrandbits(8)]))
    img.u32(sk + 0x2C0C, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2ED8, rng.choice([0x400, 0x1000, 0xFFFFF000, 0, ps.word(rng)]))
    img.u32(sk + 0x2EF0, rng.choice([0, 100, -100 & 0xFFFFFFFF, 3000, -3900 & 0xFFFFFFFF, 3999, 4001, -4001 & 0xFFFFFFFF, 6000, -9000 & 0xFFFFFFFF, rng.randrange(-6000, 6000) & 0xFFFFFFFF]))
    img.u32(sk + 0x2EF4, rng.choice([0, 50, -50 & 0xFFFFFFFF, 1000, rng.randrange(-3000, 3000) & 0xFFFFFFFF]))
    img.u32(sk + 0x2EFC, rng.choice([0, 0x1000, 0x20000, 0x100000, rng.getrandbits(32)]))
    img.u32(sk + 0x2EEC, rng.choice([0, 1]))
    img.u32(sk + 0x2E90, rng.choice([0, 1, 1]))
    img.u32(sk + 0x2BFC, rng.choice([0, 1, 2, 3]))
    img.u32(sk + 0x2C78, rng.choice([0, 1]))
    img.u32(sk + 0x2C7C, rng.choice([0, 1]))
    img.u32(sk + 0x3038, ps.word(rng))
    img.u32(sk + 0x2F50, ps.word(rng))
    img.u32(0x00567C7C, rng.choice([0, 1]))
    for j in range(3):
        img.u32(0x0056AF28 + 4 * j, rng.choice([0, 0, 0, 1]))
        img.u32(sk + 0x30F4 + 4 * j, ps.basis_word(rng))
        img.u32(sk + 0x3100 + 4 * j, ps.basis_word(rng))
        img.u32(sk + 0x310C + 4 * j, ps.basis_word(rng))
        img.u32(sk + 0x2ECC + 4 * j, rng.choice([0, ps.word(rng)]))
    img.put(sk + 0x2E58, bytes(rng.getrandbits(8) for _ in range(18)))
    img.u16(sk + 0x29F2, rng.choice([0, 1, 5, 15, rng.randrange(0, 16)]))
    for k in range(16):
        img.u16(slab + BAIL_STATES + 2 * k, ps.short(rng) & 0xFFFF)
    for k in range(0xE0):
        img.u16(slab + BAIL_GRIND + 2 * k, ps.short(rng) & 0xFFFF)
    img.u32(pt.STATE_OFFSETS, slab + BAIL_STATES)
    img.u32(pt.GRIND_OFFSETS, slab + BAIL_GRIND)
    img.u32(pt.TRICKS_BINARY, rng.choice([0x01000000, 0x12345678, 0xFFFFF000, rng.getrandbits(32)]))
    img.u32(sk + 0x2F64, 0)
    pk.gte_noise(img, rng)
    img.put(slab + 0x1100, bytes(rng.getrandbits(8) for _ in range(0x20)))
    STATE["hits"] = [rail_hit(env, rng), rail_hit(env, rng), rail_hit(env, rng)]
    STATE["rnd"] = [rng.randrange(0, 4000) for _ in range(4)]
    STATE["item"] = env.slab + ITEM
    return [], rs.changed(env, img)


def rail_hit(env, rng):
    """A zone query's answer: 0x50 bytes from +0x40 of the record."""
    data = bytearray(rng.getrandbits(8) for _ in range(0x50))
    body = (env.slab + ITEM) if rng.random() < 0.4 else 0
    data[0x28:0x2C] = body.to_bytes(4, "little")
    return bytes(data)


def plain(address, name):
    return spec(address, name, "phe")


@plain(0x004AAF70, "Rail_Update")
def gen_rail_update(env, n):
    img, rng = Image(), env.rng
    sk = ps.skater()
    slab = env.slab
    count = rng.choice([3, 4, 5, 6, 8])
    points = rail_path(img, env, rng, count)
    limit = min(rs.SLOTS, count + 3)
    # one or two records over the chain
    firsts = [0] if rng.random() < 0.6 else [0, rng.randrange(0, count)]
    addresses = [slab + rs.RECORDS + 0x40 * k for k in range(len(firsts))]
    img.u32(0x0056B860, addresses[0])
    box_low = [min(p[j] for p in points) - rng.choice([0x1000, 0x40000, 0x200000]) for j in range(3)]
    box_high = [max(p[j] for p in points) + rng.choice([0x1000, 0x40000, 0x200000]) for j in range(3)]
    for k, record in enumerate(addresses):
        img.u32(record, rs.RAIL_VTABLE)
        img.u8(record + 4, rng.choice([1, 1, 1, 0]))
        img.u8(record + 5, rng.getrandbits(4))
        img.u16(record + 6, firsts[k] if rng.random() < 0.9 else 0xFFFF)
        for j in range(3):
            img.u32(record + 8 + 4 * j, box_low[j] & 0xFFFFFFFF)
            img.u32(record + 0x14 + 4 * j, box_high[j] & 0xFFFFFFFF)
        img.u32(record + 0x20, addresses[k - 1] if k > 0 else 0)
        img.u32(record + 0x24, addresses[k + 1] if k + 1 < len(addresses) else 0)
    node = rng.randrange(0, count - 1)
    t = rng.choice([0.0, 0.3, 0.5, 0.9, 1.2])
    here = [int(points[node][j] + (points[node + 1][j] - points[node][j]) * t) + rng.randrange(-0x20000, 0x20000) for j in range(3)]
    for j in range(3):
        img.u32(sk + 8 + 4 * j, here[j] & 0xFFFFFFFF)
        img.u32(sk + 0xBC + 4 * j, (here[j] + rng.randrange(-0x20000, 0x20000)) & 0xFFFFFFFF)
    pad = slab + PAD
    img.u32(sk + 0x2CCC, pad)
    img.u8(pad, rng.choice([0, 1, 1, 1]))
    img.u32(sk + 0x2E90, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2E34, rng.choice([0, 0, 0, 1]))
    img.u32(sk + 0x2EC4, rng.choice([0, 1, 5, -1 & 0xFFFFFFFF]))
    img.u32(sk + 0x2EC8, rng.choice([0, 1, 2, rng.randrange(0, count)]))
    img.u32(sk + 0x2EC0, rng.choice([0, 1, 1]))
    img.u32(sk + 0x30B8, rng.choice([0, 1, 2, 3, 5]))
    for j in range(3):
        img.u32(sk + 0x4C + 4 * j, rng.randrange(-0x80000, 0x80000) & 0xFFFFFFFF)
        img.u32(sk + 0x30F4 + 4 * j, ps.basis_word(rng))
    img.u32(sk + 0x2EA4, rng.getrandbits(32))
    img.u32(0x00567C7C, rng.choice([0, 1]))
    for field in (0x2E94, 0x2E9C, 0x2EA0, 0x2EA8, 0x2EAC, 0x2EB0, 0x2EB4, 0x2EB8, 0x2EBC, 0x2E8C, 0x2BFC, 0x2C00):
        img.u32(sk + field, ps.word(rng))
    pk.gte_noise(img, rng)
    segments = []
    for _ in range(12):
        j = rng.randrange(0, count)
        base = points[j]
        blend = rng.choice([0.0, 0.0, 0.5, 1.0, rng.random()])
        nxt = points[min(j + 1, count - 1)]
        point = [int(base[i] + (nxt[i] - base[i]) * blend) for i in range(3)]
        if rng.random() < 0.3:
            point = list(points[rng.randrange(0, count)])
        segments.append((b"".join((v & 0xFFFFFFFF).to_bytes(4, "little") for v in point), rng.choice([0, 1, 100, 0x1000, 0x7FFF, rng.randrange(0, 0x3000)])))
    STATE["segments"] = segments
    STATE["hits"] = [rail_hit(env, rng) for _ in range(8)]
    return [sk], rs.changed(env, img)
