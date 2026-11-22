"""Phase 4.9 case generators: the colour math and palette functions of create_color.cpp.

Slab layout (offsets from the scratch slab):
  0x0000  sixteen scratch dwords: the output words the functions write through pointers
  0x0100  four fake 0x20-byte palette objects (only the flag byte at +3 is read; the engine's
          palette behind them is a stubbed service)
  0x1000  the saved HSV palette: 256 entries of 16 bytes (hue, saturation, value, alpha word)
"""

from __future__ import annotations

from career_state import u8, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, SPECS, Spec, Skip, s32, spec

OUT = 0x0000
PALETTE_OBJECTS = 0x0100
SAVED = 0x1000
HUE_FULL = 360 << 20


def word(rng):
    """A full 32-bit argument word: only the low byte of a colour channel is used."""
    return rng.getrandbits(32)


def channel_word(rng):
    return rng.choice([word(rng), rng.getrandbits(8), rng.getrandbits(8), 0, 0xFF, 0x80])


def scratch_words(env, p, count=6):
    for i in range(count):
        p.put(env.slab + OUT + 4 * i, u32(0xA5A5A500 + i))


@spec(0x00423C90, "Color_Min3", "color")
def gen_min3(env, n):
    r = env.rng
    return [channel_word(r), channel_word(r), channel_word(r)], {}


@spec(0x00423CB0, "Color_Max3", "color")
def gen_max3(env, n):
    r = env.rng
    return [channel_word(r), channel_word(r), channel_word(r)], {}


def rgb_args(rng):
    mode = rng.randrange(0, 6)
    if mode == 0:
        v = rng.getrandbits(8)
        return [v, v, v]
    if mode == 1:
        return [0, 0, 0]
    if mode == 2:
        a = rng.getrandbits(8)
        return [a, a, rng.getrandbits(8)]
    return [channel_word(rng), channel_word(rng), channel_word(rng)]


@spec(0x00423CD0, "Color_RgbToHsv", "color")
def gen_rgb_to_hsv(env, n):
    p = Pokes()
    scratch_words(env, p)
    a = env.slab + OUT
    return rgb_args(env.rng) + [a, a + 4, a + 8], p.pokes


def hue_value(rng):
    return rng.choice([
        rng.randrange(0, HUE_FULL), rng.randrange(0, HUE_FULL), rng.randrange(0, HUE_FULL),
        0, HUE_FULL - 1, HUE_FULL, HUE_FULL + rng.randrange(0, 1 << 24), 2 * HUE_FULL + 5,
        -rng.randrange(1, HUE_FULL), -HUE_FULL, -HUE_FULL - 1, -2 * HUE_FULL, rng.randrange(0, 6) << 20,
    ])


def unit_value(rng):
    return rng.choice([
        rng.randrange(0, 0x100000), rng.randrange(0, 0x100000), rng.randrange(0, 0x100000),
        0, 0xFFFFF, 0x100000, 0x100001, 0x7FFFFFFF, -1, -0x100000, rng.randrange(-5, 5),
    ])


@spec(0x00423EA0, "Color_HsvToRgb", "color")
def gen_hsv_to_rgb(env, n):
    p = Pokes()
    scratch_words(env, p)
    a = env.slab + OUT
    return [hue_value(env.rng), unit_value(env.rng), unit_value(env.rng), a, a + 1, a + 2], p.pokes


@spec(0x0041F0C0, "Color_PresetHsv", "color")
def gen_preset(env, n):
    p = Pokes()
    scratch_words(env, p)
    a = env.slab + OUT
    return [env.rng.choice([0, 1, 2, 3, env.rng.randrange(0, 36), env.rng.randrange(0, 36)]), a, a + 4, a + 8], p.pokes


def random_entry(rng):
    return (s32(rng.choice([rng.randrange(0, HUE_FULL), rng.randrange(0, HUE_FULL), 0, HUE_FULL - 1, rng.randrange(-1000, 1000)]))
            + s32(rng.choice([rng.randrange(0, 0x100000), rng.randrange(0, 0x100000), 0, 0xFFFFF, 0x100000, rng.randrange(-50, 50)]))
            + s32(rng.choice([rng.randrange(0, 0x100000), rng.randrange(0, 0x100000), 0, 0xFFFFF, 0x100000]))
            + u32(rng.choice([0, 0x8000, 0x8000, rng.getrandbits(16)])))


def saved_palette(rng, count=256):
    return b"".join(random_entry(rng) for _ in range(count))


def seed_slab(env, rng):
    """Pre-fill the slab (called once before the base snapshot): a random saved palette."""
    return {env.slab + SAVED: saved_palette(rng, 256)}


SLAB_BYTES["color"] = 0x2000
SEEDS["color"] = seed_slab
READ_ONLY["color"] = [(0x00525C00, 0x1B0)]   # the colour preset table


@spec(0x00424280, "Palette_GetHsv", "color")
def gen_palette_hsv(env, n):
    p = Pokes()
    scratch_words(env, p)
    a = env.slab + OUT
    index = env.rng.choice([0, 1, 2, -1, env.rng.randrange(0, 256), env.rng.randrange(0, 256), 255, 256])
    return [index, env.slab + SAVED, a, a + 4, a + 8], p.pokes


def palette_object(env, p, sixteen):
    obj = env.slab + PALETTE_OBJECTS
    flags = (1 if sixteen else 0) | env.rng.choice([0, 0, 0x0C, 0x02])
    p.put(obj, b"\0\0\0" + u8(flags))
    return obj


@spec(0x00424330, "Palette_ShiftHsv", "color")
def gen_shift(env, n):
    p = Pokes()
    rng = env.rng
    obj = palette_object(env, p, rng.random() < 0.4)
    limit = rng.choice([0, 1, 16, 32, 100, 255, 256, 300, -1, rng.randrange(0, 256)])
    # (a hue below -360 degrees is rejected by the converter, which then leaves the previous entry's
    # colour in a local that starts as stack garbage: keep the shift where every hue converts)
    dh = rng.choice([0, 1, -1, 30, -30, 90, 180, 359, -359, 719, rng.randrange(-359, 720), rng.randrange(-359, 720)])
    ds = rng.choice([0, 1, -1, 64, -64, 128, 255, 256, -256, rng.randrange(-300, 300)])
    dv = rng.choice([0, 1, -1, 64, -64, 128, 255, 256, -256, rng.randrange(-300, 300)])
    return [obj, env.slab + SAVED, dh, ds, dv, limit], p.pokes


@spec(0x004241A0, "Palette_Remember", "color")
def gen_remember(env, n):
    p = Pokes()
    rng = env.rng
    sixteen = rng.random() < 0.4
    obj = palette_object(env, p, sixteen)
    # random 15-bit colours for the engine's palette, including the alpha bit
    colours = b"".join(rng.choice([rng.getrandbits(16), rng.getrandbits(16), 0, 0x7FFF, 0xFFFF, 0x8000]).to_bytes(2, "little")
                       for _ in range(256))
    env.pal_bytes = colours
    return [obj, env.slab + SAVED], p.pokes
