"""Phase 4.9 case generators: the bitmap spool queue (create_spool.cpp) and the help-text layout
(create_ui.cpp).

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000  the spool queue: 0x40 entries of 0x2c bytes from +8 (keys, slots and names are seeded once,
          the free flags are poked per case)
  0x1000  the text box object: font pointer +0x10, eight 0x80-byte lines from +0x14, line count +0x414
  0x1500  the message text (poked per case)
  0x1800  the bitmap name the spool add call hands over
  0x1a00  the word buffer of the tokenizer
"""

from __future__ import annotations

from career_state import u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec

QUEUE = 0
BOX = 0x1000
MESSAGE = 0x1500
NAME = 0x1800
WORD = 0x1A00
ENTRIES = 0x40
STRIDE = 0x2C


def entry(i):
    return QUEUE + 8 + i * STRIDE


def seed_spool(env, rng):
    """Random keys, slots and bitmap names for the 64 queue entries."""
    writes = {}
    env.spool_keys = []
    for i in range(ENTRIES):
        key = rng.choice([0x1000 + rng.randrange(0, 12), rng.getrandbits(32)])
        slot = rng.randrange(0, 9)
        name = f"bmp{rng.randrange(100)}_{i}.bmp".encode()[:0x13] + b"\0"
        env.spool_keys.append((key, slot))
        writes[env.slab + entry(i)] = u32(key)
        writes[env.slab + entry(i) + 4] = name + b"\0" * (0x14 - len(name))
        writes[env.slab + entry(i) + 0x18] = u32(slot)
        writes[env.slab + entry(i) + 0x20] = u32(rng.getrandbits(32)) + u32(rng.getrandbits(32)) + u32(rng.getrandbits(32))
    return writes


SLAB_BYTES["spool"] = 0x2200
SEEDS["spool"] = seed_spool
READ_ONLY["spool"] = []


def flags_state(env, p, rng):
    """Random free flags: a run of used entries, a run of free ones, or a scatter."""
    pattern = rng.randrange(0, 4)
    used = rng.randrange(0, ENTRIES + 1)
    for i in range(ENTRIES):
        if pattern == 0:
            free = i >= used
        elif pattern == 1:
            free = rng.random() < 0.5
        elif pattern == 2:
            free = i < used
        else:
            free = rng.random() < 0.9
        p.put(env.slab + entry(i) + 0x1C, u32(1 if free else 0))


@spec(0x00424870, "Spool_Compact", "spool", this=lambda env: env.slab + QUEUE)
def gen_compact(env, n):
    p = Pokes()
    flags_state(env, p, env.rng)
    return [], p.pokes


@spec(0x004248E0, "Spool_FindFree", "spool", this=lambda env: env.slab + QUEUE)
def gen_find_free(env, n):
    p = Pokes()
    flags_state(env, p, env.rng)
    return [], p.pokes


@spec(0x00424780, "Spool_Add", "spool", this=lambda env: env.slab + QUEUE)
def gen_add(env, n):
    p = Pokes()
    rng = env.rng
    flags_state(env, p, rng)
    if rng.random() < 0.5:
        key, slot = rng.choice(env.spool_keys)        # probably an entry that is in use: the update path
    else:
        key, slot = rng.getrandbits(32), rng.randrange(0, 9)
    name = f"new{rng.randrange(1000)}.bmp".encode() + b"\0"
    p.put(env.slab + NAME, name)
    return [key, env.slab + NAME, slot, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], p.pokes


def sentence(rng, words):
    out = []
    for i in range(words):
        out.append("".join(rng.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randrange(1, 12))))
        if i < words - 1:
            out.append(rng.choice([" "] * 12 + ["&"]))
    text = "".join(out)
    if rng.random() < 0.2:
        text = text.replace(" ", "  ", 1)     # a double space: an empty word
    return text


@spec(0x00425F20, "Text_NextWord", "spool")
def gen_next_word(env, n):
    p = Pokes()
    rng = env.rng
    text = sentence(rng, rng.randrange(1, 8))
    p.put(env.slab + MESSAGE, text.encode() + b"\0")
    offset = rng.choice([0, 0, rng.randrange(0, len(text) + 1)])
    return [env.slab + MESSAGE + offset, env.slab + WORD], p.pokes


@spec(0x00425CA0, "Text_Layout", "spool", this=lambda env: env.slab + BOX)
def gen_layout(env, n):
    p = Pokes()
    rng = env.rng
    text = sentence(rng, rng.randrange(1, 40))
    text = text[:280]                       # seven lines at most: a longer message runs past the line table
    p.put(env.slab + MESSAGE, text.encode() + b"\0")
    p.put(env.slab + BOX + 0x10, u32(0x1234))          # the font (only passed to the width service)
    return [env.slab + MESSAGE], p.pokes
