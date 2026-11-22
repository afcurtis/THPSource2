"""Phase 4.15a probe: the rail set-up of rail.cpp (group rlc): Rail_CreateRails, the CRail constructor Rail_CreateRuntimeRecord and M3dColij_FlushCache.

The same level data as rail_specs.py (the node table, the node decoder and link stubs, the lookup table's buffer), plus the services the set-up reaches: the object operator new (0x00466b70, answered
with the next of 16 records in the slab), Mem_Delete (0x0046f4d0, the lookup table's buffer), and the collision queries (0x004624d0 / 0x00466090: the second fills the hit fields of the record the
caller keeps in a stack local, scripted per call). `Math_NormalizeFixed12`, the vector operators and the accessors are the real retail code.

A rail walk (the constructor follows a chain of nodes) ends only on a node without a link or on its own first node, so the graphs must keep it from running for ever: the rails are paths whose links
lead to higher nodes (a branch may skip nodes, a path may end at the head of a later path) and isolated rings (every node one link into the ring, none from outside).
Slab layout as in rail_specs.py, with the runtime records at 0xd00 + 0x40 * k (k < 16) and the slab 0x1100 bytes.
"""

from __future__ import annotations

import rail_specs as rs
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("rlc",)

SLAB = 0x1100
RECORDS2 = 0xD00
OBJECTS = 16
CHECK_FLAG = 0x005614C4
QUERY_FLAGS = 0x00567C78                # 0x00567c78 and 0x00567c7c
CACHE = [(0x00533AB8, 8), (0x00567C88, 8), (0x00567A70, 0x140)]
INFO = 0x7FF00D00                       # what the query record's address is recorded as (pc_platform.hpp kScratchBase + 0x500)

GENERATION = 0x00533AB4
LISTS = [(0x0056DB28, 4), (0x0056AF40, 4)]
WINDOWS = rs.WINDOWS + [(CHECK_FLAG, 4), (QUERY_FLAGS, 8), (GENERATION, 4)] + LISTS + CACHE

STATE = {"hits": [], "slope": 0, "hity": 0, "new": []}


def rlc_regions(slab):
    return merged([(slab, SLAB)] + WINDOWS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = rlc_regions
    READ_ONLY[_group] = [(rs.RAIL_VTABLE, 0x10)]
    SEEDS[_group] = rs.rln_seed


def rlc(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "rlc", this=resolve)


# ---- the stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------

def next_hit(_a):
    return STATE["hits"].pop(0) if STATE["hits"] else 0


def install_build_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_rlc_installed", False):
        return
    fc._rlc_installed = True
    mark_under_test()
    rs.install_rail_stubs(fc, env)
    ensure(fc, 0x00466B70, 1, "engine_00466b70", result=lambda a: STATE["new"].pop(0) if STATE["new"] else 0)
    ensure(fc, 0x0046F4D0, 1, "engine_0046f4d0")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=INFO)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=INFO,
           pokes=[(0, 4, next_hit, 0x68), (0, 2, lambda a: STATE["slope"], 0x7A), (0, 4, lambda a: STATE["hity"], 0x70)])


GROUP_STUBS = {group: install_build_stubs for group in GROUPS}


# ---- the world --------------------------------------------------------------------------------------------------------------------------------------------------------------

def pick_position(rng):
    """A node position in Q12 (the decoder writes the node's whole units shifted left by 12)."""
    kind = rng.choice(["near", "near", "near", "far", "flat", "huge"])
    if kind == "huge":
        return [rng.randrange(-0x40000000, 0x40000000) for _ in range(3)]
    reach = 0x20000 if kind == "far" else 0x2000
    values = [(rng.randrange(-reach, reach) << 12) & 0xFFFFFFFF for _ in range(3)]
    if kind == "flat":
        values[0] = values[2] = 0
    return [v - (1 << 32) if v & 0x80000000 else v for v in values]


def pack(values):
    return b"".join((v & 0xFFFFFFFF).to_bytes(4, "little") for v in values)


def scripted_hits(rng, calls=200):
    """What the world answers to the collision queries: nothing, everything or a mix."""
    chance = rng.choice([0.0, 0.1, 0.3, 0.5, 0.8, 1.0])
    return [rng.choice([0x1234, 0x7FF0, 0xDEAD0, 1]) if rng.random() < chance else 0 for _ in range(calls)]


def build(img, env, rng, count, *, rails=0.7, ring=0.3, cap=15):
    """The node table with rails: paths with forward links and isolated rings. Returns (rail nodes, the head of every path or ring)."""
    slab = env.slab
    rs.STATE["pos"].clear()
    level = rng.choice(rs.LEVELS)
    img.u32(rs.CURRENT_LEVEL, level)
    img.u32(rs.PLAYERS, rng.choice([1, 1, 2, 3, 4]))
    img.u32(rs.OFFSET_LIST, slab + rs.NODE_PTRS)
    img.u32(rs.NUM_NODES, count)
    shift = 6 if (level >= 10 and level != 13) else 0
    limit = min(rs.SLOTS, count + 3)
    rail_nodes = [k for k in range(1, count) if rng.random() < rails][:cap]
    if rng.random() < 0.05 and count > 0:
        rail_nodes = [0] + rail_nodes[:cap - 1]                  # (the first node a rail: an assertion failure retail goes on from)
    links = {k: [] for k in range(limit)}
    heads = []
    components = []
    group = []
    for k in rail_nodes:
        if group and rng.random() < 0.3:
            components.append(group)
            group = []
        group.append(k)
    if group:
        components.append(group)
    is_ring = [rng.random() < ring for _ in components]
    for comp, closed in zip(components, is_ring):
        heads.append(comp[0])
        if closed:
            for a, b in zip(comp, comp[1:] + comp[:1]):
                links[a].append(b)                               # a ring: isolated, one link per node
        else:
            for a, b in zip(comp, comp[1:]):
                links[a].append(b)
            for a in comp[:-1]:
                if rng.random() < 0.2:
                    later = [x for x in comp if x > a]
                    if later:
                        links[a].append(rng.choice(later))        # a branch that skips ahead
    for ci, comp in enumerate(components):
        # a path may end at the head of a later path (never at a ring: a walk that enters a ring from outside would not end)
        if not is_ring[ci] and rng.random() < 0.25:
            later = [c[0] for cj, c in enumerate(components) if cj > ci and not is_ring[cj]]
            if later:
                links[comp[-1]].append(rng.choice(later))
    non_rail = [k for k in range(limit) if k not in rail_nodes]
    for k in rail_nodes:
        if non_rail and rng.random() < 0.2:
            links[k].append(rng.choice(non_rail))                # a link to a node that is no rail
        rng.shuffle(links[k])
    for k in range(limit):
        node = slab + rs.NODES + 0x10 * k
        if k in rail_nodes:
            img.u16(node, rng.choice([0x0A, 0x0A, 0x0B]))
        else:
            img.u16(node, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 0x64, 0x1F4, 0x3E8, 0x3E9]))
        area = slab + rs.TRIG + 0x10 * k
        word = rs.pick_word(rng)
        while word == 0xFFFF:
            word = rs.pick_word(rng)
        if k in rail_nodes and rng.random() < 0.7:
            word &= 0x7FFF                                       # (most rail nodes are not marked done)
        img.u16(area + shift, word)
        if rng.random() < 0.3:
            img.u16(area + (0 if shift else 6), rng.getrandbits(16))
        rs.STATE["pos"][k] = pack(pick_position(rng))
        table = slab + rs.LINKS + 0x20 * k
        img.u16(table, len(links[k]))
        for i, target in enumerate(links[k]):
            img.u16(table + 2 + 2 * i, target)
    return rail_nodes, heads


def world_flags(img, rng):
    img.u8(CHECK_FLAG, rng.choice([0, 1, 1]))
    img.u32(QUERY_FLAGS, rng.choice([0, 1]))
    img.u32(QUERY_FLAGS + 4, rng.choice([0, 1]))
    STATE["hits"] = scripted_hits(rng)
    STATE["slope"] = rng.randrange(-0x1000, 0x1000)
    STATE["hity"] = rng.randrange(-0x100000, 0x100000)


@rlc(0x004AA5D0, "Rail_CreateRails")
def gen_create_rails(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([1, 2, 4, 6, 9, 12, 16, 20, rng.randrange(1, 21)])
    build(img, env, rng, count, rails=rng.choice([0.4, 0.7, 1.0]), ring=rng.choice([0.0, 0.3, 0.6]))
    world_flags(img, rng)
    STATE["new"] = [env.slab + RECORDS2 + 0x40 * k for k in range(OBJECTS)]
    rs.STATE["lookup"] = env.slab + rs.LOOKUP
    img.u32(rs.LOOKUP_PTR, rng.getrandbits(32))
    img.u32(rs.LOOKUP_COUNT, rng.getrandbits(32))
    img.u32(rs.LIST_HEAD, 0)
    return [], rs.changed(env, img)


THIS = {"object": 0}


@spec(0x004AA8C0, "Rail_CreateRuntimeRecord", "rlc", this=lambda env: THIS["object"])
def gen_create_record(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([2, 3, 4, 6, 9, 12, 16, 20, rng.randrange(2, 21)])
    rail_nodes, heads = build(img, env, rng, count, rails=rng.choice([0.5, 0.8, 1.0]), ring=rng.choice([0.0, 0.3, 0.6]))
    world_flags(img, rng)
    if not rail_nodes:
        raise_skip()
    node = rng.choice(heads) if rng.random() < 0.8 else rng.choice(rail_nodes)
    THIS["object"] = env.slab + RECORDS2
    other = env.slab + RECORDS2 + 0x40
    img.u32(rs.LIST_HEAD, rng.choice([0, other]))
    img.put(other, bytes(rng.getrandbits(8) for _ in range(0x28)))
    img.put(THIS["object"], bytes(rng.getrandbits(8) for _ in range(0x28)))
    return [node], rs.changed(env, img)


def raise_skip():
    from create_specs import Skip

    raise Skip()


@rlc(0x004629F0, "M3dColij_FlushCache")
def gen_flush(env, n):
    img, rng = Image(), env.rng
    for start, size in CACHE:
        img.put(start, bytes(rng.getrandbits(8) for _ in range(size)))
    return [], rs.changed(env, img)


@rlc(0x00462490, "M3D_AdvanceCollisionQueryGeneration")
def gen_generation(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.u16(GENERATION, rng.choice([0, 1, 5, 0xFFFE, 0xFFFF, 0xFFFF, rng.getrandbits(16)]))
    # two lists of up to five objects of the slab (the stamp is the word at +6, the next object at +0x20)
    objects = [slab + RECORDS2 + 0x40 * k for k in range(10)]
    for list_head, group in ((0x0056DB28, objects[:5]), (0x0056AF40, objects[5:])):
        count = rng.choice([0, 1, 3, 5])
        img.u32(list_head, group[0] if count else 0)
        for k, obj in enumerate(group):
            img.u16(obj + 6, rng.getrandbits(16))
            img.u32(obj + 0x20, group[k + 1] if k + 1 < count else 0)
    return [], rs.changed(env, img)


def cache_state(img, rng, empty=True):
    head = rng.choice([-1, -1, 0, 1, 5, 18, 19, rng.randrange(0, 20)]) if empty else rng.choice([0, 1, 5, 18, 19, rng.randrange(0, 20)])      # (removing from an empty cache would write below the entries)
    tail = rng.choice([0, 0, 1, 5, 19, 20, 21, rng.randrange(0, 20)])
    base = rng.choice([-1, 0, 10, 100, 499, 500, rng.randrange(0, 500)])
    write = rng.choice([0, 1, 100, 480, 499, 500, rng.randrange(0, 500)])
    for k in range(20):
        img.u32(0x00567A70 + 0x10 * k, rng.choice([0, 0x1234]))
        img.u32(0x00567A7C + 0x10 * k, rng.choice([base, base, rng.randrange(0, 500)]) & 0xFFFFFFFF)
    img.u32(0x00533AB8, head & 0xFFFFFFFF)
    img.u32(0x00533ABC, base & 0xFFFFFFFF)
    img.u32(0x00567C88, tail)
    img.u32(0x00567C8C, write)


@rlc(0x00463580, "M3dColij_CacheCanTake")
def gen_cache_can_take(env, n):
    img, rng = Image(), env.rng
    cache_state(img, rng)
    return [rng.choice([0, 1, 10, 100, 300, 499, 500, 501, 600, 0xFFFFFFFF])], rs.changed(env, img)


@rlc(0x00463690, "M3dColij_CacheRemoveOldest")
def gen_cache_remove(env, n):
    img, rng = Image(), env.rng
    cache_state(img, rng, empty=False)
    return [], rs.changed(env, img)
