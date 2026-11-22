"""Phase 4.11a2 case generators, part 1: the player spool manager (spooler.cpp, 0x004b50e0..0x004b5b6d), group `pspl`.

The probe (frontend_calls.py with the group name) forces calls of the retail functions on the live process, but the state they work on is made up:
a 0xa10-byte manager object in the scratch slab (the global at 0x0056db4c points at it, or at nothing), 64 queue entries of random but legal content,
names to look up and queue, and an arena for the allocations.  Services (the allocator, the file layer, the PSX region spooler) are stubbed and reported.

Slab layout (offsets from the scratch slab):
  0x0100  the manager (0xa10 bytes; the 0x28 bytes before it are the "entry -1" a bad index reads)
  0x1100  a second, unconstructed manager-sized block (construct / create write there)
  0x2100  names (0x20 apart: lookups); 0x2800 the name a call is handed
  0x3000  fake vtables (0x20 apart); 0x3100 the ring-order output
  0x4000  the allocation arena (0x8000)
"""

from __future__ import annotations

import struct

from career_state import u8, u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec

GLOBAL = 0x0056DB4C
STEPPING = 0x005615DC
LOADING = 0x005615D0
VT_MANAGER = 0x00519738
VT_BASE = 0x005184B4

MGR = 0x0100
MGR_SIZE = 0xA10
OTHER = 0x1100
NAMES = 0x2100
GIVEN = 0x2800
VTABLES = 0x3000
OUT = 0x3100
ARENA = 0x4000
ARENA_SIZE = 0x40000      # (addresses only: the blocks are never touched, so just the first 0x2000 bytes are captured)
SLAB = 0xC000

COUNT, CURRENT, MODE = 0xA04, 0xA08, 0xA0C
E_BASE, E_STRIDE, E_COUNT = 4, 0x28, 0x40

NAME_POOL = ["tony", "hawk1", "spider", "mullen", "kareem", "rodney", "x", "skate01", "big_ped", "ab"]
EXTENSIONS = ["", ".psh", ".psx", ".PSX", ".bin", ".psxx", ".Psx"]
# the fake virtual methods an object under test reaches (retail entry points nothing in these cases runs)
FAKE_SLOTS = [0x004AD9B0, 0x004ADA10, 0x004AD9E0, 0x004ADA40]

SERVICE_NAMES = {}      # (kept for the replay's reading: address -> engine_xxxxxxxx)


class Ov:
    """A byte overlay: the writes that put the game in a case's state (later writes win, adjacent bytes merge into few pokes)."""

    def __init__(self):
        self.bytes: dict[int, int] = {}

    def put(self, address: int, data: bytes) -> None:
        for i, b in enumerate(data):
            self.bytes[address + i] = b

    def u32(self, address: int, value: int) -> None:
        self.put(address, u32(value))

    def u16(self, address: int, value: int) -> None:
        self.put(address, u16(value))

    def u8(self, address: int, value: int) -> None:
        self.put(address, u8(value))

    def pokes(self) -> dict[int, bytes]:
        out: dict[int, bytes] = {}
        run_start = None
        run: list[int] = []
        for address in sorted(self.bytes):
            if run_start is not None and address == run_start + len(run):
                run.append(self.bytes[address])
            else:
                if run_start is not None:
                    out[run_start] = bytes(run)
                run_start, run = address, [self.bytes[address]]
        if run_start is not None:
            out[run_start] = bytes(run)
        return out


def pick(rng, seq):
    return seq[rng.randrange(len(seq))]


def regions(slab):
    return [(slab, 0x1C00), (slab + NAMES, 0x900), (slab + VTABLES, 0x180), (slab + ARENA, 0x2000), (GLOBAL, 4), (LOADING, 0x20)]


def seed_manager(env, writes):
    """The manager block starts as retail's constructor leaves it: every entry's handle is -1 (the groups poke the rest)."""
    for i in range(E_COUNT):
        writes[env.slab + MGR + E_BASE + i * E_STRIDE + 0x18] = u32(0xFFFFFFFF)


def seed_world(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    writes = {}
    # the names lookups use
    for k in range(16):
        name = NAME_POOL[k % len(NAME_POOL)] + (EXTENSIONS[k % 3 + (k // 10)] if k >= 10 else "")
        writes[env.slab + NAMES + 0x20 * k] = name.encode() + b"\0"
    for k, slot in enumerate(FAKE_SLOTS):
        writes[env.slab + VTABLES + 4 * k] = u32(slot)
    seed_manager(env, writes)
    # fake vtable 0x3020: the destructor slot is an unreached entry point (a manager-sized object of another class)
    return writes


SLAB_BYTES["pspl"] = SLAB
SEEDS["pspl"] = seed_world
EXTRA_REGIONS["pspl"] = regions
READ_ONLY["pspl"] = []


def entry_at(mgr, i):
    return mgr + E_BASE + i * E_STRIDE


def manager_state(env, ov, rng, mgr=None, count=None, current=None, mode=None, vtable=VT_MANAGER, kinds=(0, 0, 1), dense=False):
    """A random legal manager: `count` entries queued (the rest are free entries, some with leftovers), the current entry, the mode and the file layer's words."""
    mgr = env.slab + MGR if mgr is None else mgr
    # (the base state is a zeroed manager whose entries hold the free handle -1, and every case is restored to it: only what differs is poked)
    if count is None:
        count = rng.choice([0, 1, 2, 3, 5, 8, 12, 0x20, 0x3E, 0x3F])
    if current is None:
        current = rng.randrange(count + 1)
    if mode is None:
        mode = rng.choice([0, 1, 1, 2])
    ov.u32(mgr, vtable)
    ov.u32(mgr + COUNT, count)
    ov.u32(mgr + CURRENT, current)
    ov.u32(mgr + MODE, mode)
    entries = []
    for i in range(E_COUNT):
        busy = i < count or (dense and i < 0x30) or rng.random() < 0.08
        e = entry_at(mgr, i)
        if not busy:
            continue
        name = rng.choice(NAME_POOL)
        kind = pick(rng, kinds)
        if kind == 0 and rng.random() < 0.5:
            name += pick(rng, [".psh", ".bin", ".psx"])
        processed = rng.choice([0, 1, 1])
        ov.u8(e + 0, processed)
        ov.put(e + 1, name.encode()[:18] + b"\0")
        ov.u32(e + 0x14, kind)
        ov.u32(e + 0x18, rng.choice([0xFFFFFFFF, rng.randrange(20), rng.randrange(20)]))
        buffer = rng.choice([0, env.slab + ARENA + 0x100 * rng.randrange(1, 30), env.slab + ARENA + 0x100 * rng.randrange(1, 30)])
        ov.u32(e + 0x1C, buffer)
        ov.u32(e + 0x20, rng.choice([0, 1, 1]))
        ov.u32(e + 0x24, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0x100, 0x20000)]))
        entries.append((i, name, kind, processed))
    ov.u32(GLOBAL, mgr)
    ov.u8(STEPPING, rng.choice([0, 1]))
    ov.u32(LOADING, rng.choice([0, 0, 1]))
    return entries


def given_name(env, ov, rng, entries=None, extension=None):
    """Poke a name for a call (one of the queued names, a plain pool name or an unknown one) and return its address."""
    roll = rng.random()
    if entries and roll < 0.55:
        name = rng.choice(entries)[1]
    elif roll < 0.8:
        name = rng.choice(NAME_POOL)
    else:
        name = rng.choice(["nobody", "tony2", "TONY", ""])
    if extension is not None:
        name = name + extension
    ov.put(env.slab + GIVEN, name.encode() + b"\0")
    return env.slab + GIVEN


# ---- create and destroy ---------------------------------------------------------------------------------------------------------------

@spec(0x004B50E0, "PlayerSpool_Create", "pspl")
def gen_create(env, n):
    ov = Ov()
    rng = env.rng
    ov.u32(GLOBAL, rng.choice([0, 0, 0, env.slab + OTHER]))
    ov.u8(STEPPING, rng.choice([0, 1]))
    return [], ov.pokes()


@spec(0x004B5190, "PlayerSpool_Destroy", "pspl")
def gen_destroy(env, n):
    ov = Ov()
    rng = env.rng
    roll = rng.random()
    if roll < 0.15:
        ov.u32(GLOBAL, 0)
    elif roll < 0.35:
        # an object of another class: its vtable's first slot is an engine entry point
        ov.put(env.slab + OTHER, bytes(0x40))
        ov.u32(env.slab + OTHER, env.slab + VTABLES)
        ov.u32(GLOBAL, env.slab + OTHER)
    else:
        manager_state(env, ov, rng, mode=rng.choice([0, 0, 1, 2]))
    return [], ov.pokes()


@spec(0x004B51B0, "PlayerSpool_RingOrder", "pspl")
def gen_ring_order(env, n):
    ov = Ov()
    rng = env.rng
    count = rng.choice([1, 2, 3, 4, 5, 7, 20, 20, 20])
    start = rng.choice([0, count - 1, rng.randrange(count), rng.randrange(count), rng.randrange(count)])
    ov.put(env.slab + OUT, bytes(4 * 24))
    return [env.slab + OUT, count, start], ov.pokes()


@spec(0x004B5200, "PlayerSpool_Construct", "pspl", this=lambda env: env.slab + OTHER)
def gen_construct(env, n):
    ov = Ov()
    rng = env.rng
    ov.put(env.slab + OTHER, rng.randbytes(MGR_SIZE))
    ov.u8(STEPPING, rng.choice([0, 1]))
    return [], ov.pokes()


@spec(0x004B5240, "PlayerSpool_ScalarDelete", "pspl", this=lambda env: env.slab + MGR)
def gen_scalar_delete(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, mode=rng.choice([0, 1, 2]))
    return [rng.choice([0, 1, 1, 3])], ov.pokes()


@spec(0x004B5260, "PlayerSpool_Destruct", "pspl", this=lambda env: env.slab + MGR)
def gen_destruct(env, n):
    ov = Ov()
    manager_state(env, ov, env.rng, mode=env.rng.choice([0, 1, 2]))
    return [], ov.pokes()


@spec(0x004B52B0, "PlayerSpool_Reset", "pspl", this=lambda env: env.slab + MGR)
def gen_reset(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, mode=rng.choice([0, 1, 2]), dense=rng.random() < 0.3)
    return [], ov.pokes()


# ---- stepping the queue ----------------------------------------------------------------------------------------------------------------

@spec(0x004B5300, "PlayerSpool_ProcessPending", "pspl", this=lambda env: env.slab + MGR)
def gen_process_pending(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, count=rng.choice([1, 2, 3, 5]), mode=rng.choice([0, 1, 1, 2]))
    return [], ov.pokes()


@spec(0x004B5350, "PlayerSpool_Finalize", "pspl", this=lambda env: env.slab + MGR)
def gen_finalize(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, count=rng.choice([1, 2, 3, 5]), mode=rng.choice([0, 1, 2, 2]))
    return [], ov.pokes()


@spec(0x004B5580, "PlayerSpool_ForceFinalize", "pspl", this=lambda env: env.slab + MGR)
def gen_force_finalize(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, count=rng.choice([1, 2, 3, 5]), mode=rng.choice([0, 1, 2]))
    return [], ov.pokes()


@spec(0x004B55A0, "PlayerSpool_IsIdle", "pspl", this=lambda env: env.slab + MGR)
def gen_idle(env, n):
    ov = Ov()
    rng = env.rng
    count = rng.choice([0, 1, 3, 5])
    manager_state(env, ov, rng, count=count, current=rng.choice([count, count, rng.randrange(count + 1)]))
    return [], ov.pokes()


@spec(0x004B55C0, "PlayerSpool_Pump", "pspl", this=lambda env: env.slab + MGR)
def gen_pump(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, count=rng.choice([1, 2, 3, 5]), mode=rng.choice([0, 1, 1, 1, 2]))
    return [], ov.pokes()


@spec(0x004B5620, "PlayerSpool_LoadQueuedItem", "pspl", this=lambda env: env.slab + MGR)
def gen_load_queued_item(env, n):
    ov = Ov()
    rng = env.rng
    count = rng.choice([1, 2, 3, 5])
    manager_state(env, ov, rng, count=count, current=rng.randrange(count), mode=rng.choice([0, 1, 2]))
    return [], ov.pokes()


@spec(0x004B57D0, "PlayerSpool_AdvanceEntry", "pspl", this=lambda env: env.slab + MGR)
def gen_advance_entry(env, n):
    ov = Ov()
    rng = env.rng
    count = rng.choice([1, 2, 3, 5])
    manager_state(env, ov, rng, count=count, current=rng.randrange(count), mode=rng.choice([0, 1, 2]))
    return [], ov.pokes()


# ---- queueing and lookups --------------------------------------------------------------------------------------------------------------

@spec(0x004B5370, "PlayerSpool_QueueResource", "pspl", this=lambda env: env.slab + MGR)
def gen_queue(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, count=rng.choice([0, 0, 1, 2, 5, 0x20, 0x3E, 0x3F]), mode=rng.choice([0, 1, 2]))
    stem = rng.choice(NAME_POOL)
    roll = rng.random()
    text = stem + rng.choice([".psx", ".psh", ".PSX", ".bin", ".psx", ".psxx", ".Psx", ".a.psx", ""]) if roll < 0.95 else rng.choice([".psx", ".x"])
    if "." not in text:
        text += ".psh"
    ov.put(env.slab + GIVEN, text.encode() + b"\0")
    return [env.slab + GIVEN, rng.choice([0, 0, 1]), rng.choice([0, 1, 1, 0, 2]), rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0x10800, 0])], ov.pokes()


@spec(0x004B5810, "PlayerSpool_HasResource", "pspl", this=lambda env: env.slab + MGR)
def gen_has_resource(env, n):
    ov = Ov()
    rng = env.rng
    entries = manager_state(env, ov, rng)
    return [given_name(env, ov, rng, entries)], ov.pokes()


@spec(0x004B5830, "PlayerSpool_Acquire", "pspl", this=lambda env: env.slab + MGR)
def gen_acquire(env, n):
    ov = Ov()
    rng = env.rng
    entries = manager_state(env, ov, rng)
    return [given_name(env, ov, rng, entries)], ov.pokes()


@spec(0x004B58A0, "PlayerSpool_EntryReady", "pspl", this=lambda env: env.slab + MGR)
def gen_entry_ready(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng)
    return [rng.choice([0, 1, 2, 3, rng.randrange(0x40), rng.randrange(0x40), 0xFFFFFFFF])], ov.pokes()


@spec(0x004B5920, "PlayerSpool_TakeBufferNamed", "pspl", this=lambda env: env.slab + MGR)
def gen_take_buffer_named(env, n):
    ov = Ov()
    rng = env.rng
    entries = manager_state(env, ov, rng, kinds=(0, 0, 0, 1))
    return [given_name(env, ov, rng, entries)], ov.pokes()


@spec(0x004B5990, "PlayerSpool_TakeBuffer", "pspl", this=lambda env: env.slab + MGR)
def gen_take_buffer(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng, kinds=(0, 0, 0, 1))
    return [rng.choice([0, 1, 2, 3, rng.randrange(0x40), rng.randrange(0x40)])], ov.pokes()


@spec(0x004B5A00, "PlayerSpool_ReleaseEntry", "pspl", this=lambda env: env.slab + MGR)
def gen_release_entry(env, n):
    ov = Ov()
    rng = env.rng
    manager_state(env, ov, rng)
    return [rng.choice([0, 1, 2, 3, rng.randrange(0x40), rng.randrange(0x40)])], ov.pokes()


@spec(0x004B5B00, "PlayerSpool_FindEntry", "pspl", this=lambda env: env.slab + MGR)
def gen_find_entry(env, n):
    ov = Ov()
    rng = env.rng
    entries = manager_state(env, ov, rng)
    return [given_name(env, ov, rng, entries), rng.choice([0, 1])], ov.pokes()


# ---- the services -----------------------------------------------------------------------------------------------------------------------

UNDER_TEST: set = set()      # the functions the selected groups call: a stub on one would intercept the forced call itself


def ensure(fc, address, args, name=None, **kw):
    """Stub a service unless a selected group tests it; a stub an earlier module installed at the address is switched off (two breakpoints at one address corrupt the stack)."""
    if address in UNDER_TEST:
        return
    name = name or "engine_%08x" % address
    for probe in [p for p in fc._probes if getattr(p, "address", None) == address and p.enabled]:
        probe.enabled = False
    fc.stub(address, args, name, **kw)


def under_test():
    import os
    from create_specs import SPECS
    selected = [g for g in os.environ.get("P410_GROUPS", "").split(",") if g]
    UNDER_TEST.clear()
    UNDER_TEST.update(s.address for s in SPECS if s.group in selected)


def install_stubs(fc, env):
    if getattr(fc, "pspool_stubs", False):
        return      # (the groups share one set: two breakpoints at an address corrupt the stack)
    fc.pspool_stubs = True
    under_test()
    rng = lambda: env.rng      # noqa: E731
    # (a file of an absurd size, as a missing file reports, gets no block: the arena would run out)
    ensure(fc, 0x0046F490, 4, "alloc", result=lambda a: 0 if a[0] > 0x100000 else env.alloc(a))
    ensure(fc, 0x00466B70, 1, result=env.alloc)
    ensure(fc, 0x00466BB0, 1)
    ensure(fc, 0x00449030, 1,
           result=lambda a: rng().choice([rng().randrange(1, 0x10000), rng().randrange(1, 0x400), 0, 0xFFFFFFFF, rng().randrange(0x10000, 0x20000)]))
    ensure(fc, 0x00449230, 1)
    ensure(fc, 0x00449660, 0)
    ensure(fc, 0x004494B0, 0, writes=[(LOADING, 4, lambda a: rng().choice([0, 0, 1]))])
    ensure(fc, 0x004B37A0, 4, result=lambda a: rng().randrange(20))
    ensure(fc, 0x004B39B0, 0)
    ensure(fc, 0x004B3DF0, 0)
    ensure(fc, 0x004B2450, 1)
    ensure(fc, 0x004B3270, 1)
    for slot in FAKE_SLOTS:
        ensure(fc, slot, 1, convention="thiscall", this=True)


GROUP_STUBS = {"pspl": install_stubs}
