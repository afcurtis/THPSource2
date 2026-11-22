"""Phase 4.9 case generators: recolouring a part's palette (create.cpp).

The group shares the "textures" slab layout (the texture-set manager whose sets name the bitmaps) and
adds, in the same slab,
  0x8000  a fake create object (0x330 bytes): saved palette pointers +0x8c, engine palettes +0x140,
          colour shifts +0x164, bitmap names +0x288, the custom slot +0x284, colour presets +0x318
  0x8400  saved palette A and 0x9400 saved palette B (256 entries of 16 bytes: hue, saturation, value, alpha)
  0xa400  eight fake engine palettes (0x20 bytes, the flag byte at +3)
  0xa600  eight fake scene materials (0x20 bytes, the palette pointer at +0x1c)
The custom records, the texture-manager globals and the bitmap hue table are at their retail addresses.
"""

from __future__ import annotations

from career_state import u8, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec
from create_specs_color import saved_palette
from create_specs_textures import ENTRIES, SET_COUNT, counts, name_bytes, seed_textures, textures_regions

OBJ = 0x8000
OBJ_SIZE = 0x330
SAVED_A = 0x8400
SAVED_B = 0x9400
PALETTES = 0xA400
MATERIALS = 0xA600
SCRATCH = 0x7C00 + 0x80        # output words (inside the textures scratch window)

CUSTOM = 0x0056A488
TEXTURE_GLOBAL = 0x0056A87C
EDITOR_GLOBAL = 0x00561CB4
TABLE_NAMES = ["crpants0.bmp", "kneepad0.bmp", "kneepdb0.bmp", "campant0.bmp", "camshot0.bmp", "sshirta0.bmp", "ssleeve0.bmp",
               "slngslv0.bmp", "plbshrt0.bmp", "plsslve0.bmp"]


def palette_regions(slab):
    return [*textures_regions(slab), (slab + OBJ, OBJ_SIZE), (slab + SAVED_A, 0x2000), (slab + PALETTES, 0x400),
            (TEXTURE_GLOBAL, 4), (EDITOR_GLOBAL, 4), (CUSTOM, 0x200)]


def narrow_palette(rng, low, high):
    """A saved palette whose hues all lie between `low` and `high` degrees."""
    entries = b""
    for _ in range(256):
        hue = rng.randrange(low, high) << 20
        entries += (hue.to_bytes(4, "little", signed=True) + rng.randrange(0, 0x100000).to_bytes(4, "little")
                    + rng.randrange(0, 0x100000).to_bytes(4, "little") + u32(rng.choice([0, 0x8000])))
    return entries


def seed_palette(env, rng):
    slab = env.slab
    writes = dict(seed_textures(env, rng))
    # a few database entries carry bitmap names from the hue table (in mixed case)
    for i, name in enumerate(TABLE_NAMES[:6]):
        text = name.upper() if i % 2 else name
        writes[slab + ENTRIES + i * 0x20 + 8] = name_bytes(text)
    writes[slab + SAVED_A] = saved_palette(rng, 256)
    writes[slab + SAVED_B] = rng.choice([narrow_palette(rng, 100, 160), saved_palette(rng, 256), narrow_palette(rng, 300, 360)])
    # the palette objects and materials
    env.materials = {}
    for j in range(8):
        writes[slab + PALETTES + j * 0x20 + 3] = u8(rng.choice([0, 1, 1, 0x0C, 0x0D]))
        checksum = 0x1000 + j * 7
        material = slab + MATERIALS + j * 0x20
        env.materials[checksum] = material
        writes[material + 0x1C] = u32(slab + PALETTES + j * 0x20)
    # the create object: saved palette pointers for the nine kinds
    saved = b"".join(u32(slab + rng.choice([SAVED_A, SAVED_B])) for _ in range(9))
    writes[slab + OBJ + 0x8C] = saved
    return writes


SLAB_BYTES["palette"] = 0xA800
SEEDS["palette"] = seed_palette
EXTRA_REGIONS["palette"] = palette_regions
READ_ONLY["palette"] = [(0x00525C00, 0x1B0), (0x00525E10, 0x1F0), (0x0055FE10, 0x10)]   # colour presets, the bitmap hue table, its
#  names and the empty string that ends it


def obj(env):
    return env.slab + OBJ


def palette_ptr(env, rng):
    return rng.choice([0, env.slab + PALETTES + rng.randrange(0, 8) * 0x20])


def object_state(env, p, rng, pointers=True, flags=True):
    """Random engine palette pointers, shifts and names for the nine kinds."""
    if pointers:
        for kind in range(9):
            p.put(obj(env) + 0x140 + 4 * kind, u32(palette_ptr(env, rng) if rng.random() < 0.85 else 0))
    for kind in range(9):
        shift = (rng.choice([0, 1, -1, 359, -359, 400, 720, rng.randrange(-2000, 2000)]),
                 rng.choice([0x80, 0, 0x100, rng.randrange(0, 0x200)]),
                 rng.choice([0x80, 0, 0x100, rng.randrange(0, 0x200)]))
        p.put(obj(env) + 0x164 + 0x10 * kind, b"".join((x & 0xFFFFFFFF).to_bytes(4, "little") for x in shift))
        if flags:
            p.put(obj(env) + 0x28F + 0x10 * kind, u8(rng.choice([0x30, 0x30, 0x30, 0x31, 0x41])))
    p.put(obj(env) + 0x1F4, u32(env.slab + PALETTES + rng.randrange(0, 8) * 0x20))   # (never null: retail then shifts through it)
    p.put(obj(env) + 0x284, u32(rng.randrange(0, 4)))
    p.put(obj(env) + 0x318, bytes(rng.choice([0, 1, rng.randrange(0, 36)]) for _ in range(4)))


def custom_state(env, p, rng):
    """The custom records name set ids; make sure the sets exist and a few point at table bitmaps."""
    sets = rng.randrange(8, 41)
    p.put(env.slab + SET_COUNT, u32(sets))
    p.put(env.slab + 0x6608 + 0x404, u32(rng.randrange(6, 41)))
    for slot in range(4):
        p.put(CUSTOM + slot * 0x80 + 0x68, u32(rng.choice(env.set_ids[:sets])))     # (a set that exists: retail reads
        p.put(CUSTOM + slot * 0x80 + 0x74, u32(rng.choice(env.set_ids[:sets])))     #  through the pointer it finds)
    p.put(TEXTURE_GLOBAL, u32(rng.choice([env.slab, env.slab, 0])))
    p.put(EDITOR_GLOBAL, u32(rng.choice([env.slab, 0, 0])))


@spec(0x004226E0, "Bitmap_HueLookup", "palette")
def gen_bitmap_hue(env, n):
    p = Pokes()
    rng = env.rng
    name = rng.choice(TABLE_NAMES + [t.upper() for t in TABLE_NAMES] + ["nothing.bmp", "crpants1.bmp", "crpants"])
    at = env.slab + SCRATCH + 0x40
    p.put(at, name.encode() + b"\0")
    out = env.slab + SCRATCH
    p.put(out, u32(0xA5A5A5A5) + u32(0xA6A6A6A6) + u32(0xA7A7A7A7))
    return [at, out, out + 4, out + 8], p.pokes


@spec(0x00422790, "Palette_AverageHsv", "palette", this=obj)
def gen_average(env, n):
    p = Pokes()
    rng = env.rng
    object_state(env, p, rng)
    custom_state(env, p, rng)
    out = env.slab + SCRATCH
    p.put(out, u32(0xA5A5A5A5) + u32(0xA6A6A6A6) + u32(0xA7A7A7A7))
    return [rng.choice([0, 1, 2, 2, 3, 3, 4, 5, 6, 6, 7, 8]), out, out + 4, out + 8, rng.choice([0, 1])], p.pokes


@spec(0x00422A80, "Palette_SetShift", "palette", this=obj)
def gen_set_shift(env, n):
    p = Pokes()
    rng = env.rng
    object_state(env, p, rng)
    custom_state(env, p, rng)
    return [rng.choice([0, 1, 2, 3, 6, 4]), rng.choice([-1, -1, rng.randrange(0, 360), rng.randrange(0, 360)]),
            rng.randrange(0, 256), rng.randrange(0, 256), rng.choice([0, 1])], p.pokes


@spec(0x00422C10, "Palette_ApplyPreset", "palette", this=obj)
def gen_apply_preset(env, n):
    p = Pokes()
    rng = env.rng
    object_state(env, p, rng)
    custom_state(env, p, rng)
    return [rng.choice([1, 2, 3, 6, 4, 5, 0, 7]), rng.choice([0, 1])], p.pokes


@spec(0x004214F0, "Palette_UpdateAnimation", "palette", this=obj)
def gen_update_animation(env, n):
    p = Pokes()
    rng = env.rng
    object_state(env, p, rng)
    return [rng.choice([0, 1, 2, 2, 3, 4, 5, 6, 7, 8]), rng.choice([0, 1])], p.pokes


@spec(0x00422460, "Palette_SetHandle", "palette", this=obj)
def gen_set_handle(env, n):
    p = Pokes()
    rng = env.rng
    p.put(obj(env) + 0x1F8, u32(rng.choice([0] + list(env.materials))))
    p.put(obj(env) + 0x1F4, u32(palette_ptr(env, rng)))
    # the engine palette the read service hands back
    env.pal_bytes = b"".join(rng.choice([rng.getrandbits(16), 0x7FFF, 0x8000]).to_bytes(2, "little") for _ in range(256))
    return [rng.choice([0, 1, 2, 2, 3, 6, 8]), rng.choice([0] + list(env.materials))], p.pokes
