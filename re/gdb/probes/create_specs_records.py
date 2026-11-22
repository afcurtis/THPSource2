"""Phase 4.9 case generators: the custom skaters' records (create.cpp, creedit.cpp, cresave.cpp).

The group shares the "textures" slab layout (the texture-set manager the random skater draws from)
and adds, in the same slab,
  0x8000  a working save image (0x1dc4 bytes, the menu's copy of CareerSaveImage)
  0x9e00  a fake editor / menu object (0x800 bytes: the hue dword at +0xc, four colour bytes at
          +0x318, the image pointer at +0x7cc)
The custom records (0x56a488), the staged appearance table, the live character arrays, the save
image, the texture-manager global and the career records live at their retail addresses.
"""

from __future__ import annotations

from career_state import u8, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, Skip, spec
from create_specs_textures import SCRATCH, SET_COUNT, counts, seed_textures, textures_regions

IMAGE = 0x8000
IMAGE_SIZE = 0x1DC4
EDITOR = 0x9E00
EDITOR_SIZE = 0x800
TRICK_MANAGER = 0x7E00          # a non-null pointer to hand the stubbed trick database service

CUSTOM = 0x0056A488
STAGED = 0x0056A6CC
TEXTURE_GLOBAL = 0x0056A87C
LIVE = 0x0055FAF8
SAVE_IMAGE = 0x00568A68
CAREER_RECORDS = 0x00568A6C
ROWS_FROM = 0x0053A3A4
ROWS_TO = 0x0053C1F8


def records_regions(slab):
    return [*textures_regions(slab), (slab + IMAGE, IMAGE_SIZE), (slab + EDITOR, EDITOR_SIZE),
            (CUSTOM, 0x200), (STAGED, 0x160), (TEXTURE_GLOBAL, 4), (LIVE, 0x50), (SAVE_IMAGE, IMAGE_SIZE)]


SLAB_BYTES["records"] = 0xA800
SEEDS["records"] = seed_textures
EXTRA_REGIONS["records"] = records_regions
READ_ONLY["records"] = [(ROWS_FROM, ROWS_TO - ROWS_FROM), (0x00539DF8, 0x28), (0x0056DB50, 0x10)]


def random_bytes(rng, n):
    return bytes(rng.randrange(0, 256) for _ in range(n))


def stats_state(env, p, rng, skip=()):
    """Random stat bytes (0..10, some at the ceiling) in every career record (except `skip`)."""
    for character in range(21):
        if character in skip:
            continue
        record = CAREER_RECORDS + character * 0x104
        p.put(record + 0x38, bytes(rng.choice([0, 1, 5, 8, 9, 10, 10, rng.randrange(0, 11)]) for _ in range(10)))


def live_state(env, p, rng):
    p.put(LIVE, bytes([rng.randrange(0, 20), rng.randrange(0, 20)]))
    p.put(LIVE + 2, bytes(rng.randrange(0, 4) for _ in range(20)))        # costume per character
    p.put(LIVE + 0x16, bytes(rng.randrange(0, 20) for _ in range(20)))    # mapped character
    p.put(LIVE + 0x2A, bytes(rng.randrange(0, 2) for _ in range(20)))     # enabled
    p.put(LIVE + 0x3E, bytes(rng.randrange(0, 4) for _ in range(6)))


def sets_state(env, p, rng):
    """A texture-set manager with at least a few sets (Step reads through the set it is given)."""
    sets, entries = counts(env, p, rng)
    if sets < 3:
        sets = rng.randrange(3, 41)
        p.put(env.slab + SET_COUNT, u32(sets))
    return sets


def slot_arg(rng):
    return rng.choice([0, 1, 2, 3])


def creation_state(env, rng):
    p = Pokes()
    sets_state(env, p, rng)
    stats_state(env, p, rng)
    live_state(env, p, rng)
    p.put(CUSTOM, random_bytes(rng, 0x200))
    p.put(TEXTURE_GLOBAL, u32(rng.choice([0, 0, 0, 0, 0, 0, 0xDEAD0000])))
    return p


@spec(0x00422F20, "CustomSkater_Init", "records")
def gen_init(env, n):
    rng = env.rng
    p = creation_state(env, rng)
    return [slot_arg(rng), env.slab, env.slab + TRICK_MANAGER], p.pokes


@spec(0x00423370, "CustomSkater_Randomize", "records")
def gen_randomize(env, n):
    rng = env.rng
    p = creation_state(env, rng)
    return [slot_arg(rng), env.slab, env.slab + TRICK_MANAGER], p.pokes


def editor_state(env, p, rng):
    obj = env.slab + EDITOR
    p.put(obj + 0x0C, u32(rng.getrandbits(32)))
    p.put(obj + 0x318, random_bytes(rng, 4))
    p.put(obj + 0x7CC, u32(env.slab + IMAGE))
    return obj


@spec(0x00422640, "CustomSkater_SaveColours", "records", this=lambda env: env.slab + EDITOR)
def gen_save_colours(env, n):
    rng = env.rng
    p = Pokes()
    editor_state(env, p, rng)
    p.put(CUSTOM, random_bytes(rng, 0x200))
    return [slot_arg(rng)], p.pokes


@spec(0x00422690, "CustomSkater_LoadColours", "records", this=lambda env: env.slab + EDITOR)
def gen_load_colours(env, n):
    rng = env.rng
    p = Pokes()
    editor_state(env, p, rng)
    p.put(CUSTOM, random_bytes(rng, 0x200))
    return [slot_arg(rng)], p.pokes


def copy_state(env, rng):
    p = Pokes()
    editor_state(env, p, rng)
    # a career record each slot maps to (whole records), the others get just their stat bytes
    whole = rng.sample(range(21), 6)
    stats_state(env, p, rng, skip=whole)
    live_state(env, p, rng)
    p.put(CUSTOM, random_bytes(rng, 0x200))
    p.put(STAGED, random_bytes(rng, 0x160))
    for character in whole:
        p.put(CAREER_RECORDS + character * 0x104, random_bytes(rng, 0x104))
    p.put(env.slab + IMAGE, random_bytes(rng, 0x80))
    return p


@spec(0x0042EF50, "CustomSave_CopySlot", "records", this=lambda env: env.slab + EDITOR)
def gen_copy_slot(env, n):
    rng = env.rng
    p = copy_state(env, rng)
    return [rng.choice([0, 1, 2, 3, 3]), rng.choice([0, 1, 2, 3, 0])], p.pokes


@spec(0x0042EF30, "CustomSave_CommitImage", "records", this=lambda env: env.slab + EDITOR)
def gen_commit_image(env, n):
    rng = env.rng
    p = Pokes()
    editor_state(env, p, rng)
    for offset in range(0, IMAGE_SIZE, 0x400):
        p.put(env.slab + IMAGE + offset, random_bytes(rng, min(0x40, IMAGE_SIZE - offset)))
    return [], p.pokes
