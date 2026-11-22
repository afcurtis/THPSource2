"""Phase 4.15a probe: rail.cpp, the node accessors, the lookup table and the runtime record list (group rln).

The functions work on the level's node table (`OffsetList` 0x0056e210 / `NumNodes` 0x0056e214: an array of pointers to node records, the first word of a record is its type), on the lookup
table (0x0056b858 / 0x0056b85c) and on the list of runtime records (0x0056b860). They reach three services, which are stubs with scripted answers: the node decoder `TRG_DecodeNodePosition`
(0x004c8650: writes the node's position to a stack local and returns the address after it; the trigger word of a rail node is the word there, or six bytes further in the levels 10 and up
except 13), `TRG_GetNodeLinks` (0x004c8550: the node's link table, a word count and the linked nodes) and the allocator (0x0046f490, the lookup table's buffer); the record destructors reach the
object operator delete (0x00466bb0). Everything else, the accessors calling each other and the record destructor, is the real retail code.

The slab holds 32 node slots whatever `NumNodes` says (a node number at or past `NumNodes` is an assertion failure that retail survives only when its table entry is readable). The node pointers
are fixed content (the seed); a case pokes only the nodes it uses, and only the bytes that differ from the base (a poke is the slow part of a case).
Slab layout (offsets): node pointers 0x000 (32 dwords), node records 0x100 + 0x10 * k, link tables 0x300 + 0x20 * k, trigger-word areas 0x700 + 0x10 * k, runtime records 0x900 + 0x40 * k
(k < 8), the lookup table 0xb00 (0x200 bytes).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("rln",)

SLAB = 0xD00
NODE_PTRS = 0x000
NODES = 0x100
LINKS = 0x300
TRIG = 0x700
RECORDS = 0x900
LOOKUP = 0xB00
SLOTS = 32

POSITION = 0x7FF00C00                 # what the position out-pointer is recorded as (pc_platform.hpp kScratchBase + 0x400)

CURRENT_LEVEL = 0x0056A898
PLAYERS = 0x00533F3C
OFFSET_LIST = 0x0056E210
NUM_NODES = 0x0056E214
LOOKUP_PTR = 0x0056B858
LOOKUP_COUNT = 0x0056B85C
LIST_HEAD = 0x0056B860

RAIL_VTABLE = 0x005196A4
BASE_VTABLE = 0x005184B4

WINDOWS = [(OFFSET_LIST, 8), (LOOKUP_PTR, 0x10), (CURRENT_LEVEL, 4), (PLAYERS, 4)]

STATE = {"pos": {}, "lookup": 0}


def rln_regions(slab):
    return merged([(slab, SLAB)] + WINDOWS, gap=0)


def rln_seed(env, rng):
    """The fixed content: the node pointers (slot k's record is at NODES + 0x10 * k)."""
    return {env.slab + NODE_PTRS: b"".join((env.slab + NODES + 0x10 * k).to_bytes(4, "little") for k in range(SLOTS))}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = rln_regions
    READ_ONLY[_group] = [(RAIL_VTABLE, 0x10)]
    SEEDS[_group] = rln_seed


def rln(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "rln", this=resolve)


# ---- the stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------

def install_rail_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_rln_installed", False):
        return
    fc._rln_installed = True
    mark_under_test()
    slab = env.slab
    # the decoder writes the 12-byte position to the caller's local (fill, replayed as three scripted dwords) and returns the address after it
    ensure(fc, 0x004C8650, 2, "engine_004c8650", stack_to=POSITION, fill=(0, lambda a: STATE["pos"].get(a[1], bytes(12))), result=lambda a: slab + TRIG + 0x10 * a[1])
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: slab + LINKS + 0x20 * a[0])
    ensure(fc, 0x0046F490, 4, "engine_0046f490", result=lambda a: STATE["lookup"])
    ensure(fc, 0x00466BB0, 1, "engine_00466bb0")


GROUP_STUBS = {group: install_rail_stubs for group in GROUPS}


# ---- the world --------------------------------------------------------------------------------------------------------------------------------------------------------------

LEVELS = [0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 13, 14, 15, 20, 31]


def base_byte(env, address):
    for start, data in env.base.items():
        if start <= address < start + len(data):
            return data[address - start]
    raise KeyError(f"{address:#x}")


def changed(env, img):
    """The pokes of an image, without the bytes the base already holds (a poke is the slow part of a case)."""
    out = Image()
    for address, value in img.b.items():
        if base_byte(env, address) != value:
            out.b[address] = value
    return out.pokes()


def pick_word(rng):
    if rng.random() < 0.03:
        return 0xFFFF
    word = rng.getrandbits(4)                                    # the kind of rail
    if rng.random() < 0.4:
        word |= rng.choice([1, 2, 3]) << 4                       # the player-count mask
    if rng.random() < 0.2:
        word |= 0x8000                                           # done
    if rng.random() < 0.25:
        word |= rng.getrandbits(15) & 0x7F00
    return word


def make_world(img, env, rng, count, *, rails=0.6, links="free", back=None, words_ok=False):
    """The node table: `count` nodes in the table, the first count + 3 slots written (the others are zero: not rails, no links). `links`: 'free' (any slot), 'forward' (a link always leads to a
    higher node: a rail walk ends), 'chain' (each node leads to the next). `words_ok`: no trigger word is the "none" value (a caller that writes through the word's address)."""
    slab = env.slab
    STATE["pos"].clear()
    level = rng.choice(LEVELS)
    img.u32(CURRENT_LEVEL, level)
    img.u32(PLAYERS, rng.choice([1, 1, 2, 3, 4, 0]))
    img.u32(OFFSET_LIST, slab + NODE_PTRS)
    img.u32(NUM_NODES, count)
    shift = 6 if (level >= 10 and level != 13) else 0
    limit = min(SLOTS, count + 3)
    for k in range(limit):
        node = slab + NODES + 0x10 * k
        if rng.random() < rails:
            img.u16(node, rng.choice([0x0A, 0x0A, 0x0B]))
        else:
            img.u16(node, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 0x64, 0x1F4, 0x3E8, 0x3E9]))
        area = slab + TRIG + 0x10 * k
        word = pick_word(rng)
        while words_ok and word == 0xFFFF:
            word = pick_word(rng)
        img.u16(area + shift, word)
        if rng.random() < 0.3:
            img.u16(area + (0 if shift else 6), rng.getrandbits(16))          # (what the other candidate address holds: a different value, so the wrong offset shows)
        STATE["pos"][k] = bytes(rng.getrandbits(8) for _ in range(12))
        table = slab + LINKS + 0x20 * k
        if links == "chain":
            targets = [k + 1] if k + 1 < SLOTS else []
        else:
            targets = []
            for _ in range(rng.choice([0, 1, 1, 1, 2, 2, 3])):
                if links == "forward":
                    if k + 1 < SLOTS:
                        targets.append(rng.randrange(k + 1, min(SLOTS, k + 6)))
                else:
                    targets.append(rng.randrange(0, min(SLOTS, count + 3)))
        if back is not None and rng.random() < 0.25:
            targets.append(back)
        img.u16(table, len(targets))
        for i, target in enumerate(targets):
            img.u16(table + 2 + 2 * i, target)
        if rng.random() < 0.3:
            img.u16(table + 2 + 2 * len(targets), rng.randrange(0, SLOTS))      # (a word after the last link: not a link)
    return limit


def node_arg(rng, count, junk=True):
    """A node number: mostly a node of the table, sometimes one at or past `NumNodes` (still a written slot), the upper half sometimes dirty (the functions use the low word)."""
    node = rng.randrange(0, count) if rng.random() < 0.85 else rng.randrange(0, min(SLOTS, count + 3))
    if junk and rng.random() < 0.08:
        node |= rng.getrandbits(16) << 16
    return node


def count_of(rng):
    return rng.choice([1, 2, 3, 5, 8, 12, 16, 20, 26, 29, rng.randrange(1, 30)])


def simple(address, name, *, junk=True):
    @rln(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        count = count_of(rng)
        make_world(img, env, rng, count, rails=rng.choice([0.2, 0.6, 0.9]))
        return [node_arg(rng, count, junk)], changed(env, img)

    return generate


simple(0x004A9F70, "TRG_ReadRailTriggerWord")
simple(0x004A9FD0, "Rail_Type")
simple(0x004A9FF0, "Rail_FlagsPointer")
simple(0x004AA040, "Rail_IsRailNode")
simple(0x004AA1C0, "Rail_NumLinks")
simple(0x004AA220, "Rail_NextNode")
simple(0x004AA290, "Rail_PrevNode")


@rln(0x004AA090, "Rail_GetLink")
def gen_get_link(env, n):
    img, rng = Image(), env.rng
    count = count_of(rng)
    make_world(img, env, rng, count, rails=rng.choice([0.4, 0.9]))
    index = rng.choice([0, 0, 1, 1, 2, 3, rng.randrange(0, 12), rng.randrange(0, 12), -1, -2, 20])
    return [node_arg(rng, count), index & 0xFFFFFFFF if rng.random() < 0.9 else (index & 0xFFFF) | (rng.getrandbits(16) << 16)], changed(env, img)


@rln(0x004AA330, "Rail_PrevNodeQuick")
def gen_prev_quick(env, n):
    img, rng = Image(), env.rng
    count = count_of(rng)
    start = rng.randrange(0, count)
    mode = rng.choice(["forward", "forward", "chain"])
    make_world(img, env, rng, count, rails=rng.choice([0.6, 0.9, 1.0]), links=mode, back=start if mode == "forward" else None)
    node = rng.randrange(0, min(SLOTS, count + 2))
    return [node, start | ((rng.getrandbits(16) << 16) if rng.random() < 0.08 else 0)], changed(env, img)


# ---- the runtime records ------------------------------------------------------------------------------------------------------------------------------------------------

def put_records(img, env, rng, total, *, firsts=None, vtable=RAIL_VTABLE):
    """`total` records linked into the list in order (0: an empty list); returns their addresses."""
    slab = env.slab
    addresses = [slab + RECORDS + 0x40 * k for k in range(total)]
    img.u32(LIST_HEAD, addresses[0] if addresses else 0)
    for k, record in enumerate(addresses):
        img.u32(record, vtable)
        img.u8(record + 4, rng.choice([0, 1, 1]))
        img.u8(record + 5, rng.getrandbits(4))
        img.u16(record + 6, (firsts[k] if firsts else rng.randrange(0, SLOTS)))
        img.put(record + 8, bytes(rng.getrandbits(8) for _ in range(24)))
        img.u32(record + 0x20, addresses[k - 1] if k > 0 else 0)
        img.u32(record + 0x24, addresses[k + 1] if k + 1 < total else 0)
    return addresses


def chains(rng, count):
    """Record start nodes, and the back edge for a single record (a ring)."""
    total = rng.choice([0, 1, 1, 2, 3, 5, 8])
    firsts = [rng.randrange(0, count) for _ in range(total)]
    return total, firsts, (firsts[0] if total == 1 else None)


@rln(0x004AA3C0, "Rail_SetState")
def gen_set_state(env, n):
    img, rng = Image(), env.rng
    count = count_of(rng)
    total, firsts, back = chains(rng, count)
    make_world(img, env, rng, count, rails=rng.choice([0.7, 1.0]), links=rng.choice(["forward", "forward", "chain"]), back=back)
    put_records(img, env, rng, total, firsts=firsts)
    node = rng.choice(firsts) if firsts and rng.random() < 0.5 else rng.randrange(0, min(SLOTS, count + 2))
    return [node, rng.choice([0, 1, 1, 2, 0xFF, rng.getrandbits(32)])], changed(env, img)


def state_setter(address, name):
    @rln(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        count = count_of(rng)
        total, firsts, back = chains(rng, count)
        make_world(img, env, rng, count, rails=rng.choice([0.7, 1.0]), links=rng.choice(["forward", "forward", "chain"]), back=back)
        put_records(img, env, rng, total, firsts=firsts)
        node = rng.choice(firsts) if firsts and rng.random() < 0.5 else rng.randrange(0, min(SLOTS, count + 2))
        return [node], changed(env, img)

    return generate


state_setter(0x004AA410, "Rail_ClearState")
state_setter(0x004AA420, "Rail_ActivateNode")


@rln(0x004AA430, "GetIndexOfNode")
def gen_index_of(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 10, 30, 60])
    img.u32(LOOKUP_PTR, env.slab + LOOKUP)
    img.u32(LOOKUP_COUNT, count)
    nodes = []
    for k in range(max(count, 1)):
        node = rng.randrange(0, 0x10000) if rng.random() < 0.3 else rng.randrange(0, 64)
        nodes.append(node)
        img.u16(env.slab + LOOKUP + 4 * k, node)
        img.u16(env.slab + LOOKUP + 4 * k + 2, rng.randrange(0, 64))
    pick = rng.choice(nodes) if rng.random() < 0.7 else rng.randrange(0, 70)
    if rng.random() < 0.1:
        pick = 0
    return [pick | ((rng.getrandbits(16) << 16) if rng.random() < 0.1 else 0)], changed(env, img)


@rln(0x004AA4B0, "SetupLookup")
def gen_setup_lookup(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 5, 10, 20, 29, rng.randrange(1, 30)])
    make_world(img, env, rng, count, rails=rng.choice([0.3, 0.6, 1.0]), links=rng.choice(["free", "forward", "chain"]))
    STATE["lookup"] = env.slab + LOOKUP
    img.u32(LOOKUP_PTR, rng.getrandbits(32))
    img.u32(LOOKUP_COUNT, rng.getrandbits(32))
    return [], changed(env, img)


@rln(0x004AA870, "Rail_Init")
def gen_rail_init(env, n):
    img, rng = Image(), env.rng
    put_records(img, env, rng, rng.choice([0, 1, 2, 3, 5, 8]))
    return [], changed(env, img)


@rln(0x004AA8A0, "Rail_DeleteAll")
def gen_delete_all(env, n):
    img, rng = Image(), env.rng
    put_records(img, env, rng, rng.choice([0, 1, 2, 3, 5, 8]))
    return [], changed(env, img)


THIS = {"object": 0}


def record_this(address, name, with_flag):
    @spec(address, name, "rln", this=lambda env: THIS["object"])      # (the generator picks the object; the runner asks for `this` after generating the case)
    def generate(env, n):
        img, rng = Image(), env.rng
        addresses = put_records(img, env, rng, rng.choice([1, 2, 3, 5, 8]))
        THIS["object"] = rng.choice(addresses)
        return ([rng.choice([0, 1, 1, 2, 3, rng.getrandbits(32)])] if with_flag else []), changed(env, img)

    return generate


record_this(0x004AAF10, "CRail_destruct", True)
record_this(0x004AAF30, "CRail_destroy", False)
