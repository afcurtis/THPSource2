"""Phase 4.10a case generators: the text effects (texteffects.cpp, 0x004b94a0..0x004ba780).

The effects are objects the frontend allocates; the probe builds them in the scratch slab.  The globals they read
(screen height, the pause flags, the level kind and game mode, the blood sprite pointer, a clip flag) are the game's
own: every case pokes them (the game restores them after each case).  The blood sprite is a fake object in the slab
whose vtable slots hold entry points of retail functions nothing here reaches, stubbed; the clip rectangle call and
the sprite constructors are stubbed services.

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000  the blood sprite's vtable (+0 delete, +0xc draw)      0x0040  the blood sprite object (0x20 bytes)
  0x0100  a falling effect (0x140 bytes)   0x0240  a zoom effect (0x40)   0x0300  a base effect (0x40)
  0x0400  a font object (0x14c bytes)      0x0500  pointer targets for the callbacks (x, y, r, g, b)
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec

SPRITE_VT = 0x0000
SPRITE = 0x0040
FALLING = 0x0100
ZOOM = 0x0240
BASE = 0x0300
FONT = 0x0400
TARGETS = 0x0500
SLAB = 0x0800

SCREEN_HEIGHT = 0x00527D54
PAUSED_A = 0x0056A8E0
PAUSED_B = 0x00561C04
BLOOD_SPRITE = 0x0056DB6C
CLIP_FLAG = 0x029DA3A0
LEVEL_KIND = 0x00533F3C
GAME_MODE = 0x00533F38
CLIP_HEIGHT_A = 0x00533FF2
CLIP_HEIGHT_B = 0x00534022
BLOOD_NAME = 0x00540BBC

VIRTUAL_DELETE = 0x004AD9B0
BLOOD_DRAW = 0x004AD9D0


def seed_effects(env, rng):
    from opentony.fcall import write_chunks

    vt = bytearray(0x10)
    struct.pack_into("<I", vt, 0x00, VIRTUAL_DELETE)
    struct.pack_into("<I", vt, 0x0C, BLOOD_DRAW)
    write_chunks(env.slab + SPRITE_VT, bytes(vt))
    write_chunks(env.slab + SPRITE, struct.pack("<I", env.slab + SPRITE_VT) + bytes(0x1C))
    return {}


SLAB_BYTES["effects"] = SLAB
SEEDS["effects"] = seed_effects
READ_ONLY["effects"] = [(CLIP_HEIGHT_A, 2), (CLIP_HEIGHT_B, 2), (BLOOD_NAME, 0x20)]
EXTRA_REGIONS["effects"] = lambda slab: [(slab, SLAB), (SCREEN_HEIGHT, 4), (PAUSED_A, 4), (PAUSED_B, 4), (BLOOD_SPRITE, 4),
                                         (CLIP_FLAG, 4), (GAME_MODE, 8)]


def install_effect_stubs(fc, env):
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(BLOOD_DRAW, 3, "blood_draw", convention="thiscall", this=True)
    fc.stub(0x004568D0, 5, "clip_rect")
    fc.stub(0x00466B70, 1, "operator_new", result=lambda a: env.slab + SPRITE)
    fc.stub(0x00466BB0, 1, "operator_delete")
    fc.stub(0x00457870, 4, "sprite_construct_named", convention="thiscall", this=True, text=True,
            result=lambda a: env.slab + SPRITE)
    fc.stub(0x00457120, 0, "sprite_init", convention="thiscall", this=True)


GROUP_STUBS = {"effects": install_effect_stubs}


# ---- helpers ------------------------------------------------------------------------------------------


def espec(address, name, this=None):
    return spec(address, name, "effects", this=(lambda env: env.slab + this) if this is not None else None)


def garbage(rng, size):
    return bytes(rng.randrange(256) for _ in range(size))


def globals_state(env, rng, p, overrides=None):
    """Poke the game globals the effects read."""
    values = {
        SCREEN_HEIGHT: rng.choice([240, 480, 300]),
        PAUSED_A: rng.choice([0, 0, 1]),
        PAUSED_B: rng.choice([0, 0, 1]),
        CLIP_FLAG: rng.choice([0, 1]),
        BLOOD_SPRITE: env.slab + SPRITE,
        LEVEL_KIND: rng.choice([0, 1, 1, 2, 3]),
        GAME_MODE: rng.choice([0, 1, 7, 7, 3]),
    }
    values.update(overrides or {})
    p.put(PAUSED_A, struct.pack("<I", values[PAUSED_A]))
    p.put(PAUSED_B, struct.pack("<I", values[PAUSED_B]))
    p.put(SCREEN_HEIGHT, struct.pack("<I", values[SCREEN_HEIGHT]))
    p.put(BLOOD_SPRITE, struct.pack("<I", values[BLOOD_SPRITE]))
    p.put(CLIP_FLAG, struct.pack("<I", values[CLIP_FLAG]))
    p.put(GAME_MODE, struct.pack("<II", values[GAME_MODE], values[LEVEL_KIND]))


def effect_object(env, rng, p, base, size, vtable, patch=None, **fields):
    """Random garbage, then the named dwords: enabled (+8), runs_paused (+4 byte) and any offset given as 0x...."""
    data = bytearray(garbage(rng, size))
    struct.pack_into("<I", data, 0, vtable)
    for key, value in fields.items():
        if key == "runs":
            data[4] = value
        elif key == "enabled":
            struct.pack_into("<I", data, 8, value)
        else:
            struct.pack_into("<I", data, int(key[1:], 16), value & 0xFFFFFFFF)
    if patch is not None:
        patch(data)
    p.put(env.slab + base, bytes(data))


def font_object(env, rng, p, effect=0, scale=0x1000):
    data = bytearray(garbage(rng, 0x50))
    struct.pack_into("<I", data, 0x30, scale)
    struct.pack_into("<I", data, 0x40, effect)
    p.put(env.slab + FONT, bytes(data))


def targets(env, rng, p, y=None):
    """x and y (dwords) and r, g, b (bytes) targets for the callbacks."""
    data = bytearray(garbage(rng, 0x20))
    struct.pack_into("<i", data, 0x04, y if y is not None else rng.randrange(-0x40000, 0x40000))
    p.put(env.slab + TARGETS, bytes(data))
    return env.slab + TARGETS, env.slab + TARGETS + 4, env.slab + TARGETS + 8, env.slab + TARGETS + 9, env.slab + TARGETS + 10


def small(rng):
    return rng.choice([0, 1, 2, 5, 9, 10, 11, 29, 30, 31, 0x32, 0x33, 0x40, -1, rng.randrange(-5, 40)])


# ---- base class ---------------------------------------------------------------------------------------


@espec(0x004B94A0, "TextEffect_Construct", this=BASE)
def gen_effect_construct(env, n):
    p = Pokes()
    effect_object(env, env.rng, p, BASE, 0x40, env.rng.getrandbits(32))
    return [], p.pokes


@espec(0x004B94C0, "TextEffect_Delete", this=BASE)
def gen_effect_delete(env, n):
    p = Pokes()
    effect_object(env, env.rng, p, BASE, 0x40, env.slab)
    return [env.rng.choice([0, 1, 2, 3])], p.pokes


@espec(0x004B94E0, "TextEffect_Destroy", this=BASE)
def gen_effect_destroy(env, n):
    p = Pokes()
    effect_object(env, env.rng, p, BASE, 0x40, env.slab)
    return [], p.pokes


@espec(0x004B94F0, "TextEffect_Bind", this=BASE)
def gen_effect_bind(env, n):
    rng = env.rng
    p = Pokes()
    effect_object(env, rng, p, BASE, 0x40, env.slab, enabled=rng.choice([0, 1, 1]))
    font_object(env, rng, p, effect=rng.choice([0, 0, 0x1234]))
    return [env.slab + FONT], p.pokes


@espec(0x004B95A0, "TextEffect_Unbind", this=BASE)
def gen_effect_unbind(env, n):
    rng = env.rng
    p = Pokes()
    effect_object(env, rng, p, BASE, 0x40, env.slab, enabled=rng.choice([0, 1, 1]))
    font_object(env, rng, p, effect=rng.choice([env.slab + BASE, env.slab + BASE, 0, 0x1234]))
    return [env.slab + FONT], p.pokes


@espec(0x004B9610, "TextEffect_PositionDefault", this=BASE)
def gen_position_default(env, n):
    rng = env.rng
    p = Pokes()
    effect_object(env, rng, p, BASE, 0x40, env.slab, enabled=rng.choice([0, 1, 1]))
    x, y, *_ = targets(env, rng, p)
    return [rng.choice([x, 0]), rng.choice([y, 0])], p.pokes


@espec(0x004B9690, "TextEffect_ColourDefault", this=BASE)
def gen_colour_default(env, n):
    rng = env.rng
    p = Pokes()
    effect_object(env, rng, p, BASE, 0x40, env.slab, enabled=rng.choice([0, 1, 1]))
    _, _, r, g, b = targets(env, rng, p)
    return [rng.choice([r, 0]), rng.choice([g, 0]), rng.choice([b, 0])], p.pokes


@espec(0x004B9710, "TextEffect_GlyphDefault", this=BASE)
def gen_glyph_default(env, n):
    rng = env.rng
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], {}


@espec(0x004B9720, "TextEffect_Disable", this=BASE)
def gen_disable(env, n):
    p = Pokes()
    effect_object(env, env.rng, p, BASE, 0x40, env.slab, enabled=env.rng.getrandbits(32))
    return [], p.pokes


# ---- FallingTextEffect --------------------------------------------------------------------------------


def falling_state(env, rng, p, patch=None, **fields):
    effect_object(env, rng, p, FALLING, 0x140, env.slab, patch=patch, **fields)


@espec(0x004B9730, "FallingTextEffect_Construct", this=FALLING)
def gen_falling_construct(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004B9750, "FallingTextEffect_Delete", this=FALLING)
def gen_falling_delete(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    return [env.rng.choice([0, 1, 2, 3])], p.pokes


@espec(0x004B9770, "FallingTextEffect_Destroy", this=FALLING)
def gen_falling_destroy(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004B9780, "FallingTextEffect_SetBlood", this=FALLING)
def gen_falling_set_blood(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    return [env.rng.choice([0, 1, 2, env.rng.getrandbits(32)])], p.pokes


@espec(0x004B9790, "FallingTextEffect_Init", this=FALLING)
def gen_falling_init(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    globals_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004B9820, "FallingTextEffect_Update", this=FALLING)
def gen_falling_update(env, n):
    rng = env.rng
    p = Pokes()
    falling_state(env, rng, p, enabled=rng.choice([0, 1, 1, 1]), runs=rng.choice([0, 1, 1]), **{"x120": rng.randrange(0, 200)})
    globals_state(env, rng, p)
    return [], p.pokes


@espec(0x004B9860, "FallingTextEffect_GlyphDone", this=FALLING)
def gen_falling_glyph_done(env, n):
    rng = env.rng
    p = Pokes()
    enabled = rng.choice([0, 1, 1, 1, 1])
    blood = rng.choice([1, 1, 1, 0, 2])
    falling_state(env, rng, p, enabled=enabled, **{"x11c": blood})
    sprite = env.slab + SPRITE if (enabled and blood == 1) or rng.random() < 0.8 else 0
    globals_state(env, rng, p, overrides={BLOOD_SPRITE: sprite})
    x = rng.choice([rng.randrange(-30, 540), rng.randrange(-20, 520), 0x200 - 8, 0x1F0, -16, -17, 0x201])
    y = rng.randrange(-20, 500)
    height = rng.randrange(-5, 60)
    return [(x << 12) + rng.randrange(0, 4096), (y << 12) + rng.randrange(0, 4096), ((y + height) << 12) + rng.randrange(0, 4096),
            rng.choice([rng.randrange(0, 32), rng.randrange(0, 100)])], p.pokes


@espec(0x004B9AC0, "FallingTextEffect_ClearClip")
def gen_clear_clip(env, n):
    p = Pokes()
    globals_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004B9AE0, "FallingTextEffect_ClearClipFor")
def gen_clear_clip_for(env, n):
    return [env.rng.choice([5, 7, 0, 3, 6])], {}


@espec(0x004B9B30, "FallingTextEffect_Position", this=FALLING)
def gen_falling_position(env, n):
    rng = env.rng
    p = Pokes()
    cursor = rng.randrange(0, 80)
    enabled = rng.choice([0, 1, 1, 1])
    slot = cursor & 0x1F
    countdown = rng.choice([rng.randrange(-60, 5), -1, 0, 3])
    # a countdown that has run out makes a glyph fall: most slots are negative
    falling_state(env, rng, p, patch=lambda data: struct.pack_into("<h", data, 0x0C + slot * 8, countdown), enabled=enabled,
                  **{"x110": cursor, "x118": rng.randrange(0, 50), "x11c": rng.choice([0, 1, 1]), "x10c": rng.choice([240, 270, 510])})
    x, y, *_ = targets(env, rng, p, y=rng.choice([rng.randrange(0, 0x1E0000), rng.randrange(-0x40000, 0x300000)]))
    return [rng.choice([x, x, x, 0]) if not enabled else x, rng.choice([y, y, y, 0]) if not enabled else y], p.pokes


@espec(0x004B9C80, "FallingTextEffect_Colour", this=FALLING)
def gen_falling_colour(env, n):
    rng = env.rng
    p = Pokes()
    enabled = rng.choice([0, 1, 1, 1])
    falling_state(env, rng, p, enabled=enabled)
    _, _, r, g, b = targets(env, rng, p)
    if enabled:
        return [r, g, b], p.pokes
    return [rng.choice([r, 0]), rng.choice([g, 0]), rng.choice([b, 0])], p.pokes


@espec(0x004B9D50, "FallingTextEffect_SetScale", this=FALLING)
def gen_falling_set_scale(env, n):
    p = Pokes()
    falling_state(env, env.rng, p)
    return [env.rng.getrandbits(32)], p.pokes


@espec(0x004B9D60, "FallingTextEffect_Bind", this=FALLING)
def gen_falling_bind(env, n):
    rng = env.rng
    p = Pokes()
    falling_state(env, rng, p, enabled=rng.choice([0, 1, 1]), **{"x114": rng.choice([0x1000, 0x800, rng.getrandbits(32)])})
    font_object(env, rng, p, effect=rng.choice([0, 0, 0x1234]))
    return [env.slab + FONT], p.pokes


@espec(0x004B9DE0, "FallingTextEffect_Finished", this=FALLING)
def gen_falling_finished(env, n):
    rng = env.rng
    p = Pokes()
    falling_state(env, rng, p, enabled=rng.getrandbits(32), **{"x118": rng.choice([0, 0, 1, 5, 99])})
    return [], p.pokes


@espec(0x004B9E00, "FallingTextEffect_BloodUpdate", this=FALLING)
def gen_blood_update(env, n):
    rng = env.rng
    p = Pokes()
    enabled = rng.choice([0, 1, 1, 1])
    blood = rng.choice([1, 1, 1, 0])
    falling_state(env, rng, p, enabled=enabled, **{"x11c": blood, "x120": rng.choice([0, 20, 0x32, 0x33, 0x40, 0x50, 0x51, 0x52, 0x60, 200, 3000])})
    globals_state(env, rng, p, overrides={BLOOD_SPRITE: env.slab + SPRITE})
    p.put(env.slab + SPRITE, struct.pack("<I", env.slab + SPRITE_VT) + garbage(rng, 0x1C))
    return [], p.pokes


@espec(0x004B9EF0, "FallingTextEffect_CreateSprite")
def gen_create_sprite(env, n):
    rng = env.rng
    p = Pokes()
    globals_state(env, rng, p, overrides={BLOOD_SPRITE: rng.choice([0, 0, 0, env.slab + SPRITE])})
    p.put(env.slab + SPRITE, struct.pack("<I", env.slab + SPRITE_VT) + garbage(rng, 0x1C))
    return [], p.pokes


@espec(0x004B9FB0, "FallingTextEffect_DestroySprite")
def gen_destroy_sprite(env, n):
    rng = env.rng
    p = Pokes()
    globals_state(env, rng, p, overrides={BLOOD_SPRITE: rng.choice([env.slab + SPRITE, env.slab + SPRITE, 0])})
    return [], p.pokes


# ---- ZoomTextEffect -----------------------------------------------------------------------------------


def zoom_state(env, rng, p, **fields):
    effect_object(env, rng, p, ZOOM, 0x40, env.slab, **fields)


@espec(0x004BA020, "ZoomTextEffect_Construct", this=ZOOM)
def gen_zoom_construct(env, n):
    p = Pokes()
    zoom_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004BA040, "ZoomTextEffect_Delete", this=ZOOM)
def gen_zoom_delete(env, n):
    p = Pokes()
    zoom_state(env, env.rng, p)
    return [env.rng.choice([0, 1, 2, 3])], p.pokes


@espec(0x004BA060, "ZoomTextEffect_Destroy", this=ZOOM)
def gen_zoom_destroy(env, n):
    p = Pokes()
    zoom_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004BA070, "ZoomTextEffect_Bind", this=ZOOM)
def gen_zoom_bind(env, n):
    rng = env.rng
    p = Pokes()
    state = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5])
    limit = {1: 5, 2: 5, 3: 30, 4: 10, 5: 10}.get(state, 10)
    counter = rng.choice([0, 1, limit - 1, limit, limit, rng.randrange(0, limit + 1), rng.randrange(-3, limit + 4)])
    zoom_state(env, rng, p, enabled=rng.choice([1, 1, 1, 0]), **{"x0c": state, "x10": counter, "x18": rng.randrange(0, 0x2000)})
    globals_state(env, rng, p)
    font_object(env, rng, p, effect=rng.choice([0, 0]), scale=rng.randrange(0, 0x2000))
    return [env.slab + FONT], p.pokes


@espec(0x004BA430, "ZoomTextEffect_Init", this=ZOOM)
def gen_zoom_init(env, n):
    p = Pokes()
    zoom_state(env, env.rng, p)
    return [], p.pokes


@espec(0x004BA450, "ZoomTextEffect_Update", this=ZOOM)
def gen_zoom_update(env, n):
    rng = env.rng
    p = Pokes()
    zoom_state(env, rng, p, enabled=rng.choice([0, 1, 1, 1]), runs=rng.choice([0, 1, 1]), **{"x10": rng.randrange(0, 40)})
    globals_state(env, rng, p)
    return [], p.pokes


@espec(0x004BA480, "ZoomTextEffect_Colour", this=ZOOM)
def gen_zoom_colour(env, n):
    rng = env.rng
    p = Pokes()
    state = rng.choice([0, 1, 5, 5, 6, 6, 7])
    enabled = rng.choice([0, 1, 1, 1])
    zoom_state(env, rng, p, enabled=enabled, **{"x0c": state, "x10": rng.choice([rng.randrange(0, 11), rng.randrange(-3, 14)])})
    _, _, r, g, b = targets(env, rng, p)
    if enabled:
        return [r, g, b], p.pokes
    return [rng.choice([r, 0]), rng.choice([g, 0]), rng.choice([b, 0])], p.pokes


@espec(0x004BA760, "ZoomTextEffect_Finished", this=ZOOM)
def gen_zoom_finished(env, n):
    rng = env.rng
    p = Pokes()
    zoom_state(env, rng, p, enabled=rng.getrandbits(32) if rng.random() < 0.5 else rng.choice([0, 1]), **{"x0c": rng.choice([5, 6, 6, 7])})
    return [], p.pokes


@espec(0x004BA780, "ZoomTextEffect_Start", this=ZOOM)
def gen_zoom_start(env, n):
    p = Pokes()
    zoom_state(env, env.rng, p)
    return [], p.pokes
