"""Phase 4.14a probe: the math layer (groups gte, mathk, psxs).

The GTE emulation of the PC build, the rotation / matrix / vector helpers on top of it, the Q12 trigonometry and dot products and the PSX library stubs (0x004e2070..0x004e3d1f, 0x004e7c60..0x004e8d3f,
0x004f3980, 0x004f5f50..0x004f60d0). Every function works on the register file in 0x006a3e00..0x006a3f20 (poked per case with edge-biased random words), on matrices and vectors in a scratch slab, and on a
few globals (the memory card state, the frame clocks); the nested functions (the clamps, `gte_rtir`, the rotation builders, the CRT's `_ftol` and `pow`) are the real retail functions in every case. The only
stubs are the services of the three small utilities: the multimedia timer test, `clock()`, `fopen` and `fclose` (on the call sites, so Wine's own threads are not affected).

Slab layout (offsets): matrices at 0x000 / 0x040 / 0x080 (0x40 bytes each: a matrix and the 14 bytes after it that the multiply copies from stack), angles 0x100, vectors 0x120 / 0x140 / 0x160, a primitive 0x180, a
rectangle 0x1c0, the clut words 0x1e0, text 0x200 (paths 0x40 bytes each).
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import check_disjoint, ensure, merged
from frontend_specs_menu import Image
from platform_specs import mark_under_test, stop_clock

GROUPS = ("gte", "mathk", "mathr", "psxs")

SLAB = 0x1000
MAT_A = 0x000
MAT_B = 0x040
MAT_C = 0x080
ANGLES = 0x100
VEC_A = 0x120
VEC_B = 0x140
VEC_C = 0x160
PRIM = 0x180
RECT = 0x1C0
CLUT = 0x1E0
TEXT = 0x200

KSTACK = 0x7FF00700            # what an argument that points at a stack local is recorded as (pc_platform.hpp kStackLocal)

GTE = 0x006A3E00
GTE_BYTES = 0x120

WINDOWS = [(GTE, GTE_BYTES), (0x006A74BC, 8), (0x0056E31C, 8), (0x00561C04, 4), (0x0056A8E0, 4), (0x029D836C, 4)]
TABLES = [(0x0055BD08, 8), (0x00549D3C, 4)]


def math_regions(slab):
    result = merged([(slab, SLAB)] + WINDOWS, gap=0)
    check_disjoint(result, TABLES)
    return result


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = math_regions
    READ_ONLY[_group] = TABLES


def u(value):
    return value & 0xFFFFFFFF


def gte(address, name):
    return spec(address, name, "gte")


CW_PC53 = 0x027F      # the process default: round to 53 bits (what the physics frame runs under)
CW_PC24 = 0x007F      # the render window: 24 bits (the game's own `_controlfp(0x20000, 0x30000)`)
TRAMP = 0x1000        # the trampoline's data and code, past the captured window of the slab


def trampoline(env, target, control_word):
    """gdb cannot write `$fctrl` under winedbg, so the 24-bit cases call a stub in the slab instead: it keeps the return address, loads the control word, calls `target` with the forced call's
    own stack arguments (cdecl), loads the process default again and returns. Data at +0 (the return address) and +4 / +6 (the two control words), code at +0x10."""
    import struct as _struct

    from opentony.fcall import write_chunks

    base = env.slab + TRAMP
    code = bytes([0x59]) + bytes([0x89, 0x0D]) + _struct.pack("<I", base)                 # pop ecx ; mov [base], ecx
    code += bytes([0xD9, 0x2D]) + _struct.pack("<I", base + 4)                              # fldcw [base + 4]
    call_at = base + 0x10 + len(code)
    code += bytes([0xE8]) + _struct.pack("<i", target - (call_at + 5))                      # call target
    code += bytes([0xD9, 0x2D]) + _struct.pack("<I", base + 6)                              # fldcw [base + 6]
    code += bytes([0xFF, 0x35]) + _struct.pack("<I", base) + bytes([0xC3])                  # push dword [base] ; ret
    write_chunks(base, _struct.pack("<IHH", 0, control_word, CW_PC53) + bytes(8) + code)
    return base + 0x10


def mathk(address, name):
    """A function of the math helpers: captured twice, under the 53-bit default (group mathk) and under 24 bits (group mathr; fixtures math-calls-p24-*)."""

    def decorate(generate):
        spec(address, name, "mathk")(generate)
        spec(address, name, "mathr", via=lambda env: trampoline(env, address, CW_PC24))(generate)
        return generate

    return decorate


def psxs(address, name):
    return spec(address, name, "psxs")


# ---- random words -------------------------------------------------------------------------------------------------------------------------------------------------------------

def pick16(rng, mode="rand"):
    if mode == "small":
        return rng.choice([0, 0, 1, -1, 2, -2, 0x10, -0x10, rng.randrange(-64, 64), rng.randrange(-300, 300)])
    if mode == "q12":
        return rng.choice([0, 0x1000, -0x1000, 0x800, -0x800, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x1000, 0x1001), rng.randrange(-0x2000, 0x2000)])
    return rng.choice([0, 1, -1, 0x7FFF, -0x8000, 0x1000, -0x1000, 0x4000, -0x4000, rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def pick32(rng, mode="rand"):
    if mode == "small":
        return rng.choice([0, 1, -1, rng.randrange(-5000, 5000), rng.randrange(-100000, 100000)])
    return rng.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x8000, -0x8000, 0x7FFF, -0x8001, 0xFFFF, 0x10000, -0x10000, rng.randrange(-0x100000, 0x100000), rng.randrange(-0x7FFFFFFF, 0x7FFFFFFF),
                       rng.getrandbits(32)])


def pick_mode(rng):
    return rng.choice(["rand", "small", "q12", "q12", "small", "rand"])


def put16(img, address, value):
    img.u16(address, value)


def put_matrix(img, address, rng, mode):
    for k in range(9):
        put16(img, address + 2 * k, pick16(rng, mode))


def put_vector16(img, address, rng, mode):
    for k in range(3):
        put16(img, address + 2 * k, pick16(rng, mode))


def gte_world(img, rng, mode=None):
    """A random register file: matrices, vectors, translation and background, IR and MAC (edge-biased), the projection words, the screen and depth words."""
    mode = mode or pick_mode(rng)
    # the whole window gets random bytes first, so any register an op reads has an unusual value
    img.put(GTE, bytes(rng.getrandbits(8) for _ in range(GTE_BYTES)))
    img.u32(GTE + 0x00, rng.choice([0, 1, 0x100, 0x200, 0x400, 0x1000, rng.randrange(1, 0x2000), rng.getrandbits(32)]))          # H
    for base in (0x10, 0x80, 0xC8):                                                                                              # RT, LLM, LCM
        put_matrix(img, GTE + base, rng, mode)
    for base in (0x58, 0x60, 0x68):                                                                                              # V0, V1, V2
        put_vector16(img, GTE + base, rng, mode)
        put16(img, GTE + base + 6, rng.getrandbits(16))
    put_vector16(img, GTE + 0xB0, rng, rng.choice(["rand", "small", "rand"]))                                                    # IR
    put16(img, GTE + 0x34, pick16(rng, rng.choice(["q12", "rand", "small"])))                                                    # IR0
    for base in (0x48, 0xA0, 0xB8):                                                                                              # BK, TR, MAC
        for k in range(3):
            img.u32(GTE + base + 4 * k, pick32(rng, rng.choice(["rand", "small"])))
    img.u32(GTE + 0xC4, rng.getrandbits(32))
    for off in (0xE8, 0xEC):                                                                                                     # OFX, OFY
        img.u32(GTE + off, rng.choice([0, 160 << 16, 120 << 16, rng.randrange(-0x40000, 0x40000), pick32(rng)]))
    for k in range(4):                                                                                                           # SZ0..SZ3
        put16(img, GTE + 0xFA + 2 * k, rng.choice([0, 1, 0x100, 0xFFFF, rng.getrandbits(16)]))
    for k in range(6):                                                                                                           # SXY
        put16(img, GTE + 0x04 + 2 * k, pick16(rng, rng.choice(["small", "rand"])))


def projection_world(img, rng):
    """A register file whose RTPS / RTPT transform lands on the screen most of the time: RT near the identity, small vectors, a positive depth."""
    gte_world(img, rng, "q12")
    for k in range(9):
        value = 0x1000 if k % 4 == 0 else 0
        put16(img, GTE + 0x10 + 2 * k, value + rng.randrange(-0x300, 0x300))
    for base in (0x58, 0x60, 0x68):
        put_vector16(img, GTE + base, rng, rng.choice(["small", "rand"]))
    img.u32(GTE + 0xA0, rng.choice([0, rng.randrange(-5000, 5000)]))
    img.u32(GTE + 0xA4, rng.choice([0, rng.randrange(-5000, 5000)]))
    img.u32(GTE + 0xA8, rng.choice([0, 1, 0x100, rng.randrange(-100, 70000), rng.randrange(0, 0x3000), 0xFFFF, 0x10000]))
    img.u32(GTE + 0x00, rng.choice([0x100, 0x200, 0x155, 0x400, rng.randrange(1, 0x1000)]))
    img.u32(GTE + 0xE8, rng.choice([0, 160 << 16, rng.randrange(-0x40000, 0x40000)]))
    img.u32(GTE + 0xEC, rng.choice([0, 120 << 16, rng.randrange(-0x40000, 0x40000)]))


def clamp_world(img, rng):
    """MAC words around the clamp bounds (and the IR / RGB words the colour step reads)."""
    gte_world(img, rng)
    for k in range(3):
        img.u32(GTE + 0xB8 + 4 * k, rng.choice([0, 1, -1, 0x7FFF, 0x8000, -0x8000, -0x8001, 0x7FFFFFFF, -0x80000000, rng.randrange(-70000, 70000), rng.getrandbits(32)]))


# ---- the GTE ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def plain(address, name, world=None):
    """An op with no arguments: a random register file."""

    @gte(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        (world or gte_world)(img, rng)
        return [], img.pokes()

    return generate


for _address, _name in ((0x004E2070, "Math_CrossResultAngleClamp"), (0x004E2130, "mygte_lvtosvU"), (0x004E21E0, "mygte_svtorgb")):
    plain(_address, _name, clamp_world)

for _address, _name in ((0x004E22F0, "gte_avsz4"), (0x004E23A0, "gte_gpf12"), (0x004E23F0, "gte_gpl0"), (0x004E2450, "gte_gpl12"), (0x004E24B0, "gte_lcv0"), (0x004E2550, "gte_lcv0_lm"),
                        (0x004E25F0, "gte_lcv1"), (0x004E2690, "gte_lcv1_lm"), (0x004E2730, "gte_lcv2"), (0x004E27D0, "gte_lcir"), (0x004E2870, "gte_lcirtr"), (0x004E2930, "gte_llir_sf0"),
                        (0x004E29D0, "gte_llirbk"), (0x004E2A90, "gte_llv0"), (0x004E2B30, "gte_llv0bk"), (0x004E2BF0, "gte_llv0bk_lm"), (0x004E2CB0, "gte_llv1bk"), (0x004E2D70, "gte_llv1bk_lm"),
                        (0x004E2E30, "gte_llv2bk"), (0x004E2F20, "gte_nclip"), (0x004E2F80, "Fixed_CrossProductS16"), (0x004E2FF0, "Math_CrossFixed12"), (0x004E3070, "gte_rt"),
                        (0x004E3130, "gte_rtir"), (0x004E31D0, "gte_rtirtr"), (0x004E3290, "gte_rtir_sf0"), (0x004E39A0, "Fixed_MatrixMultiplyQ12"), (0x004E3A40, "gte_rtv0tr"),
                        (0x004E3B00, "gte_rtv0_sf0"), (0x004E3BA0, "gte_rtv1"), (0x004E3C40, "gte_rtv2"), (0x004E3CE0, "gte_sqr0")):
    plain(_address, _name)

for _address, _name in ((0x004E3330, "gte_rtps"), (0x004E34E0, "gte_rtpt")):
    plain(_address, _name, projection_world)


@gte(0x004E22B0, "gte_ldlzc")
def gen_ldlzc(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    value = 0
    while value == 0:       # (retail loops for ever on 0)
        bits = rng.randrange(1, 33)
        value = rng.getrandbits(bits)
        if rng.random() < 0.4:
            value = u(~value)
        if rng.random() < 0.2:
            value = u(value << rng.randrange(0, 32))
    return [value], img.pokes()


@gte(0x004E2EF0, "gte_mvmva")
def gen_mvmva(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    return [rng.choice([0, 0, 0, 1, 1, 2, 5]), rng.getrandbits(8), rng.choice([0, 0, 3, 3, 1, 2]), rng.getrandbits(8), rng.getrandbits(8)], img.pokes()


# ---- the math on top of it -----------------------------------------------------------------------------------------------------------------------------------------------

ANGLE_PICKS = [0, 1, 0x100, 0x400, 0x800, 0xC00, 0xFFF, 0x1000, 0x1001, 0x2000, -1, -0x400, -0x1000, 0x7FFFFFFF, -0x80000000, 0x12345, -0x54321]


def pick_angle(rng):
    return rng.choice(ANGLE_PICKS + [rng.randrange(0, 0x1000), rng.randrange(-0x10000, 0x10000), rng.randrange(-0x8000, 0x8000), rng.getrandbits(32) - 0x80000000])


def pick_angle16(rng):
    return rng.choice([0, 0x400, 0x800, -0x400, 0xFFF, 0x1000, 0x7FFF, -0x8000, rng.randrange(0, 0x1000), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def math_world(img, env, rng):
    gte_world(img, rng)
    slab = env.slab
    mode = rng.choice(["q12", "q12", "small", "rand"])
    for base in (MAT_A, MAT_B, MAT_C):
        put_matrix(img, slab + base, rng, mode)
        img.put(slab + base + 18, bytes(rng.getrandbits(8) for _ in range(46)))
    return slab


def rotate_spec(address, name):
    @mathk(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        slab = math_world(img, env, rng)
        return [u(pick_angle(rng)), slab + MAT_A], img.pokes()

    return generate


rotate_spec(0x004E7C60, "Math_RotateMatrixX")
rotate_spec(0x004E7DE0, "Math_RotateMatrixY")
rotate_spec(0x004E7F60, "Math_RotateMatrixZ")


def angles_spec(address, name):
    @mathk(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        slab = math_world(img, env, rng)
        for k in range(3):
            put16(img, slab + ANGLES + 2 * k, pick_angle16(rng))
        return [slab + ANGLES, slab + MAT_C], img.pokes()

    return generate


angles_spec(0x004E80E0, "Math_RotationMatrixFromAngles")
angles_spec(0x004E8140, "Math_RotationMatrixFromAnglesZYX")
angles_spec(0x004E81A0, "Math_RotationMatrixFromAnglesAgain")
angles_spec(0x004E8280, "Math_RotationMatrixChain")


@mathk(0x004E8200, "Math_MatrixMultiplyQ12")
def gen_matrix_multiply(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    out = rng.choice([MAT_C, MAT_C, MAT_A, MAT_B])          # (the output may be one of the inputs)
    return [slab + out, slab + MAT_A, slab + MAT_B], img.pokes()


@mathk(0x004E83E0, "Math_IntSqrt")
def gen_isqrt(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    root = rng.randrange(0, 46341)
    value = rng.choice([0, 1, 2, 3, 4, 0x7FFFFFFF, -1, -0x80000000, root * root, root * root - 1, root * root + 1, rng.randrange(1, 100000), rng.randrange(1, 0x7FFFFFFF), rng.randrange(-100000, 100000)])
    return [u(value)], img.pokes()


@mathk(0x004E8410, "Math_MatrixTranspose")
def gen_transpose(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    return [slab + MAT_A, slab + rng.choice([MAT_B, MAT_C])], img.pokes()


@mathk(0x004E8460, "Matrix_SetTranslation")
def gen_set_translation(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    for k in range(3):
        img.u32(slab + VEC_A + 4 * k, pick32(rng))
    return [slab + MAT_A, slab + VEC_A], img.pokes()


def pick_vector(rng):
    kind = rng.choice(["small", "q12", "big", "huge", "zero", "axis", "mixed"])
    if kind == "zero":
        return [0, 0, 0]
    if kind == "axis":
        v = [0, 0, 0]
        v[rng.randrange(3)] = rng.choice([1, -1, 0x1000, -0x1000, 123456, -123456])
        return v
    if kind == "small":
        return [rng.randrange(-50, 50) for _ in range(3)]
    if kind == "q12":
        return [rng.randrange(-0x3000, 0x3000) for _ in range(3)]
    if kind == "big":
        return [rng.randrange(-0x100000, 0x100000) for _ in range(3)]
    if kind == "huge":
        return [rng.randrange(-0x7FFFFFFF, 0x7FFFFFFF) for _ in range(3)]
    return [pick32(rng) for _ in range(3)]


@mathk(0x004E8480, "Vector_NormalizeQ12")
def gen_normalize(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    for k, value in enumerate(pick_vector(rng)):
        img.u32(slab + VEC_A + 4 * k, u(value))
    return [slab + VEC_A, slab + rng.choice([VEC_B, VEC_B, VEC_A])], img.pokes()


@mathk(0x004E8560, "orig_ratan2")
def gen_ratan2(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    return [u(pick32(rng, rng.choice(["rand", "small", "rand"]))), u(pick32(rng, rng.choice(["rand", "small", "rand"])))], img.pokes()


@mathk(0x004E8580, "Math_AtanSlopeQ12")
def gen_atan_slope(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    return [u(pick32(rng, rng.choice(["rand", "small", "rand"])))], img.pokes()


@mathk(0x004E85A0, "Fixed_MatrixConvertQ12")
def gen_convert(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    for k, value in enumerate(pick_vector(rng)):
        img.u32(slab + VEC_A + 4 * k, u(value))
    return [slab + MAT_A, slab + VEC_A, slab + VEC_B], img.pokes()


def trig_spec(address, name):
    @mathk(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        gte_world(img, rng)
        return [u(pick_angle(rng))], img.pokes()

    return generate


trig_spec(0x004F3980, "Math_CosQ12")
trig_spec(0x004F39B0, "Math_SinQ12")


@mathk(0x004F5F50, "Fixed_CrossSignQ12")
def gen_cross_sign(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    a, b = pick32(rng, rng.choice(["rand", "small"])), pick32(rng, rng.choice(["rand", "small"]))
    c, d = pick32(rng, rng.choice(["rand", "small"])), pick32(rng, rng.choice(["rand", "small"]))
    if rng.random() < 0.15:
        c, d = -a, b          # (the sum is zero)
    return [u(a), u(b), u(c), u(d)], img.pokes()


@mathk(0x004F5F90, "Fixed_DotProductQ12")
def gen_dot(env, n):
    img, rng = Image(), env.rng
    slab = math_world(img, env, rng)
    for k, value in enumerate(pick_vector(rng)):
        img.u32(slab + VEC_A + 4 * k, u(value))
    for k, value in enumerate(pick_vector(rng)):
        img.u32(slab + VEC_B + 4 * k, u(value))
    return [slab + VEC_A, slab + VEC_B], img.pokes()


@mathk(0x004F5FC0, "Fixed_MulQ12")
def gen_mul(env, n):
    img, rng = Image(), env.rng
    gte_world(img, rng)
    return [u(pick32(rng, rng.choice(["rand", "small", "rand"]))), u(pick32(rng, rng.choice(["rand", "small", "q12"])))], img.pokes()


# ---- the stubs and the small utilities -----------------------------------------------------------------------------------------------------------------------------------

# the PSX library stubs of 0x004e86f0..0x004e8d3f by shape: what the function does with its stack arguments (a bare `ret`, `xor eax,eax`, the first or second argument)
STUB_SHAPES = {
    "004e86f0": "arg0", "004e8700": "arg0", "004e8710": "arg0", "004e8720": "arg0",
    "004e8730": "zero", "004e8740": "zero", "004e8750": "zero", "004e8760": "zero",
    "004e8770": "zero", "004e8780": "arg0", "004e8790": "zero", "004e87a0": "zero",
    "004e87b0": "zero", "004e87c0": "zero", "004e87d0": "nothing", "004e87e0": "nothing",
    "004e8840": "zero", "004e8850": "nothing", "004e8860": "zero", "004e8870": "zero",
    "004e8880": "zero", "004e8890": "nothing", "004e8960": "zero", "004e8970": "zero",
    "004e8980": "zero", "004e8990": "zero", "004e89a0": "nothing", "004e89b0": "zero",
    "004e89c0": "zero", "004e89d0": "zero", "004e89e0": "zero", "004e89f0": "nothing",
    "004e8a00": "zero", "004e8a10": "nothing", "004e8a20": "zero", "004e8a30": "arg1",
    "004e8a40": "arg0", "004e8a50": "zero", "004e8a60": "zero", "004e8a70": "nothing",
    "004e8a80": "zero", "004e8a90": "zero", "004e8aa0": "zero", "004e8ab0": "nothing",
    "004e8ac0": "zero", "004e8ad0": "nothing", "004e8ae0": "nothing", "004e8af0": "zero",
    "004e8b00": "zero", "004e8b10": "nothing", "004e8b20": "zero", "004e8b30": "nothing",
    "004e8b40": "zero", "004e8b50": "zero", "004e8b60": "zero", "004e8b70": "nothing",
    "004e8b80": "nothing", "004e8b90": "zero", "004e8ba0": "zero", "004e8bb0": "nothing",
    "004e8bc0": "zero", "004e8bd0": "zero", "004e8be0": "zero", "004e8bf0": "nothing",
    "004e8c00": "nothing", "004e8c10": "zero", "004e8c20": "zero", "004e8c30": "zero",
    "004e8c40": "nothing", "004e8c50": "nothing", "004e8c60": "nothing", "004e8c70": "nothing",
    "004e8c80": "zero16", "004e8c90": "nothing", "004e8ca0": "nothing", "004e8cb0": "nothing",
    "004e8cc0": "nothing",
}

STUB_NAME = {"arg0": "ReturnArg", "arg1": "ReturnSecondArg", "zero": "ReturnZero", "nothing": "Nothing", "zero16": "ReturnZero16"}


def stub_spec(address, shape, name):
    @psxs(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        gte_world(img, rng)
        count = {"arg0": 1, "arg1": 2}.get(shape, rng.choice([0, 1, 2, 3]))
        return [u(pick32(rng)) for _ in range(count)], img.pokes()

    return generate


for _key, _shape in STUB_SHAPES.items():
    stub_spec(int(_key, 16), _shape, f"PsxStub_{STUB_NAME[_shape]}_{_key}")
for _address in (0x004F39E0, 0x004F5FE0, 0x004F6070):
    stub_spec(_address, "nothing", f"Utils_Nothing_{_address:08x}")


@psxs(0x004E87F0, "PsxLib_InitRectPrimitive")
def gen_rect_primitive(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.put(slab + PRIM, bytes(rng.getrandbits(8) for _ in range(0x20)))
    for k in range(4):
        img.u16(slab + RECT + 2 * k, rng.choice([0, 0, 1, 0x100, 0xFFFF, rng.getrandbits(16)]))
    return [slab + PRIM, slab + RECT], img.pokes()


def clut_spec(address, name):
    @psxs(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        img.put(env.slab + CLUT, bytes(rng.getrandbits(8) for _ in range(16)))
        return [env.slab + CLUT, u(pick32(rng, rng.choice(["rand", "small"]))), u(pick32(rng, rng.choice(["rand", "small"])))], img.pokes()

    return generate


clut_spec(0x004E88A0, "PsxLib_LoadClut256")
clut_spec(0x004E88F0, "PsxLib_LoadClut16")


@psxs(0x004E8940, "PsxLib_InitTerminator")
def gen_terminator(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + PRIM, bytes(rng.getrandbits(8) for _ in range(0x20)))
    return [env.slab + PRIM], img.pokes()


def card_world(img, rng):
    img.u8(0x006A74BC, rng.choice([0, 1, 0x80, rng.getrandbits(8)]))
    img.u32(0x006A74C0, rng.choice([0, 1, 2, 3, rng.getrandbits(32)]))


@psxs(0x004E8CD0, "PsxCard_SetBusy")
def gen_card_busy(env, n):
    img, rng = Image(), env.rng
    card_world(img, rng)
    return [], img.pokes()


@psxs(0x004E8CF0, "PsxCard_SetDone")
def gen_card_done(env, n):
    img, rng = Image(), env.rng
    card_world(img, rng)
    return [], img.pokes()


@psxs(0x004E8D10, "PsxCard_GetState")
def gen_card_state(env, n):
    img, rng = Image(), env.rng
    card_world(img, rng)
    img.put(env.slab + CLUT, bytes(rng.getrandbits(8) for _ in range(16)))
    return [rng.getrandbits(8), env.slab + CLUT, env.slab + CLUT + 4], img.pokes()


# per-case scripting of the services of the utilities
STATE = {"timer": 0, "now": 0, "file": 0}


@psxs(0x004F5FF0, "Timer_UpdateFrameClock")
def gen_frame_clock(env, n):
    img, rng = Image(), env.rng
    STATE["timer"] = rng.choice([0, 0, 0, 1, 0x100])
    last = rng.choice([0, 0, 5, 1000, rng.randrange(1, 100000), 0xFFFFFF00])
    now = rng.choice([last, last + 10, last + 16, last + 17, last + 100, last + 5000, u(last - 20), rng.randrange(0, 0x10000), rng.getrandbits(32), u(last + 0x7FFFFFFF // 60)])
    STATE["now"] = u(now)
    img.u32(0x029D836C, last)
    img.u32(0x0056E31C, rng.choice([0, 1000, 0xFFFFFFF0, rng.getrandbits(32)]))
    img.u32(0x0056E320, rng.choice([0, 1000, 0xFFFFFFF0, rng.getrandbits(32)]))
    img.u32(0x00561C04, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A8E0, rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


def put_text(img, env, offset, text):
    address = env.slab + TEXT + offset
    img.put(address, text.encode("latin-1") + b"\0")
    return address


def pick_path(rng):
    stem = "".join(rng.choice("ABCDEFGH_xyz09") for _ in range(rng.randrange(0, 12)))
    return rng.choice([stem, stem + ".PRK", stem + ".a.b", "." + stem, stem + ".", stem + ".park.PRK", "dir.x\\" + stem])


@psxs(0x004F6080, "Path_ChangeExtension")
def gen_change_extension(env, n):
    img, rng = Image(), env.rng
    path = put_text(img, env, 0, pick_path(rng))
    extension = put_text(img, env, 0x80, rng.choice([".PRK", ".txt", "", ".x.y", "abc"]))
    return [path, extension], img.pokes()


@psxs(0x004F60D0, "ParkFile_Exists")
def gen_park_exists(env, n):
    img, rng = Image(), env.rng
    name = put_text(img, env, 0, pick_path(rng))
    STATE["file"] = rng.choice([0, 0, 0x00DB1234, 0x00DB0010])
    return [name], img.pokes()


def install_math_stubs(fc, env):
    """The services of the three small utilities, on the call sites of the functions under test (nothing else in the game is stubbed)."""
    if getattr(fc, "_math_installed", False):
        return
    fc._math_installed = True
    mark_under_test()
    stop_clock(fc)
    fc.stub_site(0x004F5FF0, 5, 0, "engine_004dab30", result=lambda a: STATE["timer"])
    fc.stub_site(0x004F5FFA, 5, 0, "engine_00502ccb", result=lambda a: STATE["now"])
    fc.stub_site(0x004F6116, 5, 2, "crt_fopen", text=True, stack_to=KSTACK, result=lambda a: STATE["file"])
    fc.stub_site(0x004F6125, 5, 1, "crt_fclose")


GROUP_STUBS = {group: install_math_stubs for group in GROUPS}
