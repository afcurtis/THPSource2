"""Phase 4.15b probe: the rest of the short-vector class of vector.cpp and the three small stream functions the first video groups missed (group vcs).

Vectors of three shorts live in the slab (an input, an other input, an output, and the scalar / divisor / count words they read); the GTE register window is captured for the length. The stream functions
work on the playback states and the Player array like the vbt group's.

Slab layout (offsets): vectors 0x00 / 0x10 / 0x20, a short scalar 0x30, a dword divisor 0x34, a count 0x38, the fake skaters 0x100 / 0x200.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image
from video_specs import MUTED, PLAYER, QUEUE_WRITE, STATE0, STATE1, WORLD, WORLD_BYTES, short, word

GROUPS = ("vcs",)

SLAB = 0x400
VEC_A = 0x00
VEC_B = 0x10
VEC_OUT = 0x20
SCALAR = 0x30
DIVISOR = 0x34
COUNT = 0x38
SKATER_A = 0x100
SKATER_B = 0x200
GTE = 0x006A3E00
GTE_BYTES = 0x120


def vcs_regions(slab):
    return merged([(slab, SLAB), (PLAYER, 8), (MUTED, 4), (WORLD, WORLD_BYTES), (GTE, GTE_BYTES)], gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = vcs_regions
    READ_ONLY[_group] = []


def vcs(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "vcs", this=resolve)


def install_stubs(fc, env):
    pass


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def vectors(img, env, rng, outs=True):
    for base in (VEC_A, VEC_B) + ((VEC_OUT,) if outs else ()):
        for k in range(3):
            img.u16(env.slab + base + 2 * k, short(rng) & 0xFFFF)


def angles(img, env, rng, base):
    for k in range(3):
        img.u16(env.slab + base + 2 * k, rng.choice([0, 0x7FF, 0x800, 0x801, 0xFFF, 0x1000, 0xF800, 0xF7FF, 0x7FFF, 0x8000, rng.randrange(0, 0x1000), rng.getrandbits(16)]))


@vcs(0x004CADC0, "Math_Vector3sLength", VEC_A)
def gen_length(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    return [], img.pokes()


@vcs(0x004CAE50, "Math_Vector3sMulScalar", VEC_A)
def gen_mul_assign(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.u16(env.slab + SCALAR, short(rng) & 0xFFFF)
    alias = rng.random() < 0.1
    return [env.slab + (VEC_A + 2 if alias else SCALAR)], img.pokes()


@vcs(0x004CAE80, "Math_Vector3sDivScalar", VEC_A)
def gen_div_assign(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.u32(env.slab + DIVISOR, rng.choice([1, -1, 2, 3, 7, 0x100, 0x1000, -0x1000, 0x7FFF, 0x12345, rng.randrange(1, 100000), -rng.randrange(1, 100000)]) & 0xFFFFFFFF)
    return [env.slab + DIVISOR], img.pokes()


for _address, _name in ((0x004CAEB0, "Math_Vector3sShiftRight"), (0x004CAED0, "Math_Vector3sShiftLeft")):
    def _gen(env, n):
        img, rng = Image(), env.rng
        vectors(img, env, rng)
        img.u16(env.slab + COUNT, rng.choice([0, 1, 2, 4, 8, 12, 15, 16, 17, 31, 32, 33, 0x100, 0x10F, rng.getrandbits(16)]))
        return [env.slab + COUNT], img.pokes()
    vcs(_address, _name, VEC_A)(_gen)


@vcs(0x004CAFA0, "Math_Vector3sAngleDifferenceOut", VEC_A)
def gen_angle_difference(env, n):
    img, rng = Image(), env.rng
    angles(img, env, rng, VEC_A)
    angles(img, env, rng, VEC_B)
    vectors(img, env, rng, outs=False)
    for k in range(3):
        img.u16(env.slab + VEC_OUT + 2 * k, rng.getrandbits(16))
    angles(img, env, rng, VEC_A)
    angles(img, env, rng, VEC_B)
    return [env.slab + (VEC_A if rng.random() < 0.1 else VEC_OUT), env.slab + VEC_B], img.pokes()


@vcs(0x004CB0E0, "Math_Vector3sMulScalarOut")
def gen_mul_out(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.u16(env.slab + SCALAR, short(rng) & 0xFFFF)
    return [env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT), env.slab + VEC_A, env.slab + SCALAR], img.pokes()


@vcs(0x004CB180, "Math_Vector3sDivScalarOut")
def gen_div_out(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.u32(env.slab + DIVISOR, rng.choice([1, -1, 2, 3, 7, 0x100, 0x1000, -0x1000, 0x7FFF, 0x12345, rng.randrange(1, 100000), -rng.randrange(1, 100000)]) & 0xFFFFFFFF)
    return [env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT), env.slab + VEC_A, env.slab + DIVISOR], img.pokes()


@vcs(0x004CB220, "Math_Vector3sShiftLeftOut")
def gen_shift_left_out(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    img.u32(env.slab + COUNT, rng.choice([0, 1, 2, 4, 8, 12, 15, 16, 17, 31, 32, 33, 0x100, 0x10F, rng.getrandbits(32)]))
    return [env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT), env.slab + VEC_A, env.slab + COUNT], img.pokes()


@vcs(0x004CB270, "Math_Vector3sNegateOut", VEC_A)
def gen_negate_out(env, n):
    img, rng = Image(), env.rng
    vectors(img, env, rng)
    return [env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT)], img.pokes()


for _address, _name in ((0x004CB2C0, "Replay_ClearBodyState"), (0x004CB2B0, "Replay_ClearBodyStateThunk")):
    def _gen_clear(env, n):
        img, rng = Image(), env.rng
        for address in (0x56E604, 0x56E608, 0x56E60C, 0x56E670, 0x56E674, 0x56E678, 0x56E67C, 0x56E680, 0x56E684, 0x56E47C, 0x56E480, 0x56E484, 0x56E4E8, 0x56E4EC, 0x56E4F0, 0x56E4F4, 0x56E4F8, 0x56E4FC):
            img.u32(address, rng.choice([0, 0, word(rng)]))
        return [], img.pokes()
    vcs(_address, _name)(_gen_clear)


def players(img, env, rng):
    img.u32(PLAYER, env.slab + SKATER_A)
    img.u32(PLAYER + 4, env.slab + SKATER_B)


@vcs(0x004CB3C0, "Replay_RecordSoundStop")
def gen_record_stop(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 1, 1, 2]))
    img.u32(MUTED, rng.choice([0, 0, 0, 1, 0x100]))
    img.u32(QUEUE_WRITE, rng.choice([0, 29, rng.randrange(0, 30)]))
    return [word(rng)], img.pokes()


@vcs(0x004CBFA0, "Replay_StopStream")
def gen_stop_stream(env, n):
    img, rng = Image(), env.rng
    players(img, env, rng)
    for block in (STATE0, STATE1):
        img.u32(block, rng.choice([0, 1, 2]))
        img.u32(block + 0x14, word(rng))
        img.u32(block + 0x1C, rng.choice([0, 1, 0x20, word(rng)]))
    return [rng.choice([env.slab + SKATER_A, env.slab + SKATER_B, env.slab + SKATER_B])], img.pokes()


@vcs(0x004CC020, "Replay_RewindStream")
def gen_rewind_stream(env, n):
    img, rng = Image(), env.rng
    players(img, env, rng)
    for block in (STATE0, STATE1):
        img.u32(block + 0x0C, word(rng))
        img.u32(block + 0x14, word(rng))
        img.u32(block + 0x1C, rng.choice([0, 1, 0x20, word(rng)]))
        img.u32(block + 0x20, word(rng))
        img.u32(block + 0x24, word(rng))
    return [rng.choice([env.slab + SKATER_A, env.slab + SKATER_B, env.slab + SKATER_B])], img.pokes()
