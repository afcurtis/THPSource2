"""Phase 4.9 case generators: the texture database and texture-set manager (cre_tex.cpp, cre_texset.cpp).

Slab layout for the "textures" group (offsets from the scratch slab; the slab is laid out like the
retail 0x6a10-byte manager object):
  0x0000  the set holder: vtable, then 40 seeded 0x44-byte set records from +4, the count at +0x6604
  0x6608  the texture database: vtable, 0x100 entry pointers from +4, the count at +0x404
  0x7000  40 seeded 0x20-byte database entries (sorted by name)
  0x7500  the allocator arena (FileIO_AllocLoadedBuffer hands these out in order)
  0x7c00  scratch: a 0x44-byte record for Commit / Allocate, a name buffer for Find

The "cretex" group runs TextureSetManager_LoadCretex on the real CRETEX.BIN (and on small synthetic
files) in a slab that holds the whole manager, a large arena and the file image.
"""

from __future__ import annotations

import os
import struct

from career_state import u8, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, Skip, s32, spec

HOLDER = 0
SET_COUNT = 0x6604
DATABASE = 0x6608
DB_COUNT = DATABASE + 0x404
ENTRIES = 0x7000
ARENA = 0x7500
ARENA_SIZE = 0x700
SCRATCH = 0x7C00
SETS_SEEDED = 40
ENTRIES_SEEDED = 40

# cretex group
CRETEX_SLAB = 0x10000
CRETEX_ARENA = 0x7000
CRETEX_ARENA_USABLE = 0x2800  # the allocator's share of the arena; the last 0x800 bytes hold pre-seeded entries
CRETEX_ARENA_SIZE = 0x3000
CRETEX_FILE = 0xA000          # the real CRETEX.BIN (20460 bytes)
CRETEX_SYNTH = 0xF000         # a small synthetic file poked per case
CRETEX_NAME = 0xFF00          # where the file name string lives in the slab


def lower_names(rng, count):
    """Sorted unique lower-case texture names (8.3 style, at most 15 characters)."""
    names = set()
    while len(names) < count:
        stem = "".join(rng.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randrange(3, 8)))
        names.add(f"{stem}{rng.randrange(0, 10)}.bmp")
    return sorted(names)


def name_bytes(text, size=16):
    raw = text.encode("latin-1")
    return raw + b"\0" * (size - len(raw))


def seed_textures(env, rng):
    """The fixed content of the slab for the "textures" group."""
    slab = env.slab
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    names = lower_names(rng, ENTRIES_SEEDED)
    env.seed_names = names
    writes = {}
    pointers = b""
    for i, name in enumerate(names):
        entry = slab + ENTRIES + i * 0x20
        pointers += u32(entry)
        writes[entry] = u32(i) + u32(0) + name_bytes(name) + u8(rng.choice([0, 1])) + b"\0\0\0"
    writes[slab + DATABASE + 4] = pointers
    # the sets
    sets = b""
    env.set_ids = []
    for i in range(SETS_SEEDED):
        ident = i if rng.random() < 0.9 else rng.randrange(0, SETS_SEEDED)
        env.set_ids.append(ident)
        root = -1 if (i == 0 or rng.random() < 0.4) else rng.randrange(0, i)
        textures = [slab + ENTRIES + rng.randrange(0, ENTRIES_SEEDED) * 0x20 for _ in range(4)]
        sets += (u32(ident) + s32(root) + name_bytes(f"set{i}") + name_bytes(f"Display {i}", 24)
                 + b"".join(u32(t) for t in textures) + u8(rng.randrange(1, 5)) + u8(rng.choice([0, 1]))
                 + u8(rng.choice([0, 0, 0, 1])) + u8(rng.choice([0, 0, 1, 1, 2, 4, 5, 6])))
    writes[slab + HOLDER + 4] = sets
    return writes


def textures_regions(slab):
    return [(slab + HOLDER + 4, (SETS_SEEDED + 1) * 0x44), (slab + SET_COUNT, 4), (slab + DATABASE, 0x408),
            (slab + ENTRIES, ARENA + ARENA_SIZE - ENTRIES), (slab + SCRATCH, 0x100)]


SLAB_BYTES["textures"] = 0x8000
SEEDS["textures"] = seed_textures
EXTRA_REGIONS["textures"] = textures_regions
READ_ONLY["textures"] = []


def holder(env):
    return env.slab + HOLDER


def database(env):
    return env.slab + DATABASE


def counts(env, p, rng):
    """Random prefix of the seeded tables: the set count and the entry count."""
    sets = rng.choice([0, 1, rng.randrange(0, SETS_SEEDED + 1), SETS_SEEDED, SETS_SEEDED])
    entries = rng.choice([0, 1, rng.randrange(0, ENTRIES_SEEDED + 1), ENTRIES_SEEDED])
    p.put(env.slab + SET_COUNT, u32(sets))
    p.put(env.slab + DB_COUNT, u32(entries))
    return sets, entries


@spec(0x0041D860, "TextureDatabase_Find", "textures", this=database)
def gen_find(env, n):
    p = Pokes()
    rng = env.rng
    sets, entries = counts(env, p, rng)
    pick = rng.randrange(0, 5)
    if pick == 0 and entries:
        name = rng.choice(env.seed_names[:entries])                       # a hit
    elif pick == 1 and entries:
        name = rng.choice(env.seed_names[:entries]).upper()               # a hit in another case
    elif pick == 2:
        name = "".join(rng.choice("abcxyz") for _ in range(rng.randrange(1, 9))) + ".bmp"   # a miss
    elif pick == 3:
        name = "Zz" + str(rng.randrange(100)) + ".BMP"                    # sorts after everything
    else:
        name = "0" + str(rng.randrange(100)) + ".bmp"                     # sorts before everything
    at = env.slab + SCRATCH + 0x80
    p.put(at, name.encode("latin-1") + b"\0")
    return [at], p.pokes


@spec(0x0041D9C0, "TextureDatabase_Allocate", "textures", this=database)
def gen_allocate(env, n):
    p = Pokes()
    rng = env.rng
    sets, entries = counts(env, p, rng)
    name = rng.choice([rng.choice(env.seed_names), rng.choice(env.seed_names).upper(),
                       "".join(rng.choice("abcxyzABC") for _ in range(rng.randrange(1, 9))) + ".bmp"])
    record = u32(rng.randrange(0, 300)) + u32(rng.getrandbits(32)) + name_bytes(name) + u8(rng.choice([0, 1])) + b"\xaa\xbb\xcc"
    at = env.slab + SCRATCH
    p.put(at, record)
    return [at], p.pokes


@spec(0x0041DC30, "TextureSet_FindById", "textures", this=holder)
def gen_find_by_id(env, n):
    p = Pokes()
    rng = env.rng
    counts(env, p, rng)
    return [rng.choice([rng.randrange(0, SETS_SEEDED), rng.randrange(0, SETS_SEEDED), -1, 0, 39, 40, 1000])], p.pokes


@spec(0x0041DC60, "TextureSet_FindByGroup", "textures", this=holder)
def gen_find_by_group(env, n):
    p = Pokes()
    rng = env.rng
    counts(env, p, rng)
    return [rng.choice([0, 1, 2, 4, 5, 6, 7, 3, -1])], p.pokes


@spec(0x0041DCB0, "TextureSet_FindRoot", "textures", this=holder)
def gen_find_root(env, n):
    p = Pokes()
    rng = env.rng
    sets, _ = counts(env, p, rng)
    # most calls name a real set; some name none (the assertions)
    ident = rng.choice([rng.randrange(0, SETS_SEEDED)] * 6 + [-1, 999])
    return [rng.choice([-1, -1, rng.randrange(0, SETS_SEEDED), rng.randrange(0, 10)]), ident], p.pokes


@spec(0x0041DDB0, "TextureSet_Step", "textures", this=holder)
def gen_step(env, n):
    p = Pokes()
    rng = env.rng
    sets, _ = counts(env, p, rng)
    if sets == 0:
        raise Skip()    # retail then reads through the id argument as a pointer
    ident = rng.choice([rng.randrange(0, SETS_SEEDED)] * 7 + [-1, 999])
    return [rng.choice([-1, rng.randrange(0, SETS_SEEDED), rng.randrange(0, 10)]), ident,
            rng.choice([1, -1, 1, -1, 2, -2])], p.pokes


@spec(0x0041EF50, "TextureSet_Commit", "textures", this=holder)
def gen_commit(env, n):
    p = Pokes()
    rng = env.rng
    sets, _ = counts(env, p, rng)
    ident = rng.choice([sets, sets, sets, rng.randrange(0, SETS_SEEDED)])   # an existing id trips the assertion
    record = (u32(ident) + s32(rng.choice([-1, rng.randrange(0, 20)])) + name_bytes(f"new{n}") + name_bytes(f"New {n}", 24)
              + b"".join(u32(env.slab + ENTRIES + rng.randrange(0, ENTRIES_SEEDED) * 0x20) for _ in range(4))
              + u8(rng.choice([1, 2, 3, 4, 4, 5, 6])) + u8(rng.choice([0, 1])) + u8(rng.choice([0, 1])) + u8(rng.choice([0, 1, 4, 5, 6])))
    at = env.slab + SCRATCH
    p.put(at, record)
    return [at], p.pokes


@spec(0x0041F060, "TextureSet_Dump", "textures", this=holder)
def gen_dump(env, n):
    p = Pokes()
    counts(env, p, env.rng)
    return [], p.pokes


# ---- LoadCretex ----------------------------------------------------------------------------------

def cretex_seed(env, rng):
    """The real CRETEX.BIN, the file name string and a database with a few entries already in it."""
    env.alloc_base = CRETEX_ARENA
    env.alloc_size = CRETEX_ARENA_USABLE
    path = os.environ.get("P49_CRETEX", (os.path.expanduser("~/THPSource2") + "/build/assets/all-pkr/files/data/CRETEX.BIN"))
    with open(path, "rb") as fh:
        data = fh.read()
    assert len(data) <= CRETEX_SYNTH - CRETEX_FILE, len(data)
    return {env.slab + CRETEX_FILE: data, env.slab + CRETEX_NAME: b"cretex.bin\0"}


def cretex_regions(slab):
    return [(slab, 0x6A10), (slab + CRETEX_ARENA, CRETEX_ARENA_SIZE), (slab + CRETEX_FILE, CRETEX_SYNTH - CRETEX_FILE),
            (slab + CRETEX_SYNTH, 0x600)]


SLAB_BYTES["cretex"] = CRETEX_SLAB
SEEDS["cretex"] = cretex_seed
EXTRA_REGIONS["cretex"] = cretex_regions
READ_ONLY["cretex"] = []


def synthetic_file(rng):
    """A small, valid cretex file with the real file's layout: six sections, each ended by an empty
    string; textures are named freely (the database creates them on a miss)."""
    def tex(prefix, digit=None):
        stem = "".join(rng.choice("abcdefghij") for _ in range(rng.randrange(3, 5)))
        tail = "".join(rng.choice("0123456789") for _ in range(2))
        name = f"{prefix}{stem}{tail}"
        if digit is not None:
            name = (name + "abcdefgh")[:7] + str(digit)
        return (name + ".bmp").encode()

    out = b""
    for _ in range(rng.randrange(2, 6)):                      # section 1: name, texture, [second texture], display, flag
        digit = rng.choice([1, 2, 3])
        second = b"" if rng.random() < 0.6 else (tex("cap_")[:7] + b"0.bmp")
        out += (b"hair" + bytes([97 + rng.randrange(0, 6)]) + b"\0" + tex("f", digit) + b"\0" + second + b"\0"
                + b"Hair\0" + bytes([rng.choice([0, 1])]))
    out += b"\0"
    for _ in range(rng.randrange(2, 6)):                      # section 2: name, flag, three textures, one skipped, display
        out += (b"shirt\0" + bytes([rng.choice([0, 1, 1, 100])]) + tex("t") + b"\0" + tex("s") + b"\0"
                + (b"" if rng.random() < 0.5 else tex("a")) + b"\0" + b"skip\0" + b"Shirt\0")
    out += b"\0"
    names = [b"pants", b"kneepad", b"jeans"]                  # section 3: name, value byte
    for name in names:
        out += name.upper() + b"\0" + bytes([rng.choice([0, 1])])
    out += b"\0"
    for name in names[:2]:                                    # section 4: name, texture, display
        out += name + b"\0" + tex("p") + b"\0" + b"Pants\0"
    out += b"\0"
    for _ in range(rng.randrange(1, 3)):                      # section 5: texture, display
        out += tex("sock") + b"\0" + b"Socks\0"
    out += b"\0"
    for _ in range(rng.randrange(1, 3)):                      # section 6: texture, display
        out += tex("shoe") + b"\0" + b"Shoes\0"
    out += b"\0"
    return out


@spec(0x0041DEA0, "TextureSetManager_LoadCretex", "cretex", this=lambda env: env.slab)
def gen_load_cretex(env, n):
    p = Pokes()
    rng = env.rng
    if n == 0:
        file_at = env.slab + CRETEX_FILE
    else:
        file_at = env.slab + CRETEX_SYNTH
        data = synthetic_file(rng)
        assert len(data) < 0x600, len(data)
        p.put(file_at, data)
    env.file_ptr = file_at
    # a database that already has some entries: sorted lower-case names in the arena's first blocks
    existing = rng.choice([0, 0, 5, 20])
    if existing:
        names = lower_names(rng, existing)
        # (kept tiny: each entry is 0x1c bytes in the slab's last arena page)
        base = env.slab + CRETEX_ARENA + CRETEX_ARENA_USABLE
        pointers = b""
        for i, name in enumerate(names):
            entry = base + i * 0x20
            pointers += u32(entry)
            p.put(entry, u32(i) + u32(0) + name_bytes(name) + b"\0\0\0\0\0\0\0\0")
        p.put(env.slab + DATABASE + 4, pointers)
        p.put(env.slab + DB_COUNT, u32(existing))
    else:
        p.put(env.slab + DB_COUNT, u32(0))
    return [env.slab + CRETEX_NAME], p.pokes


@spec(0x0041EF50, "TextureSet_Commit", "cretex", this=lambda env: env.slab)
def gen_commit_full(env, n):
    """A set table at or near its 0x180 limit (the whole manager is captured in this group): the
    scan for a duplicate id walks every slot, and a record written at 0x180 lands on the count."""
    p = Pokes()
    rng = env.rng
    sets = [0x180, 0x17F, 0x180, 0x100, 0x17F][n % 5]
    p.put(env.slab + SET_COUNT, u32(sets))
    # the table is otherwise empty (zero ids): give a few slots ids so the duplicate scan can hit
    for slot in rng.sample(range(0, min(sets, 0x100)), 3):
        p.put(env.slab + 4 + slot * 0x44, u32(rng.randrange(1, 0x200)))
    ident = rng.choice([rng.randrange(1, 0x200), sets, 0])
    record = (u32(ident) + s32(-1) + name_bytes(f"full{n}") + name_bytes(f"Full {n}", 24)
              + b"".join(u32(0) for _ in range(4)) + u8(rng.choice([1, 2, 5])) + u8(0) + u8(0) + u8(rng.choice([0, 4])))
    at = env.slab + CRETEX_SYNTH
    p.put(at, record)
    return [at], p.pokes
