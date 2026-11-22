"""Phase 4.15c probe: the classes at the head of the executable (groups wbd and wasr): the script object, the baddy base class, the type-0xcb baddy, the movers' physics step and the assertion
routine (0x00401000..0x00403410).

The assertion routine has a group of its own (wasr): the other functions report their failed assertions to the routine as a stubbed service, so it cannot be under test in the same run.

Slab layout (offsets, shared with world_specs): object 0x000, sprite A 0x280, sprite B 0x300, record 0x380, strings 0x3a0, script objects 0x600 / 0x700 / 0x800 (0x100 each), the type-0xcb
object's table of children 0x900 (8 entries of 0x10 bytes).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import OBJ, RECORD, SPRITE_A, SPRITE_B, SPRITE_VTABLE, STRINGS, fake_sprite, fill_object, noise, sprite_pointer, word
from world_specs import install_stubs as install_base_stubs

GROUPS = ("wbd", "wasr")

SCRIPT = (0x600, 0x700, 0x800)
TABLE = 0x900
SLAB = 0xA00
SCRATCH = 0x7FF00800 + 0xC00

SCRIPT_LIST = 0x0055F6B0        # head, then the word count, the baddy count, the baddy list head
BADDY_COUNT = 0x0055F6B8
BADDY_LIST = 0x0055F6BC
OWNER = 0x0056A954
PLAYER_RECORD = 0x0056A960
STEP_WORDS = 0x0056AF38
GLOBALS = [(SCRIPT_LIST, 0x10), (OWNER, 0x10), (STEP_WORDS, 8)]

ASSERT_FORMATS = (0x00535AFC, 0x00535B50, 0x00535B68, 0x00535BC0, 0x00524160, 0x005241B0)


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(SPRITE_VTABLE, 8)]
READ_ONLY["wasr"] = [*READ_ONLY["wasr"], *[(_text, 0x40) for _text in ASSERT_FORMATS]]      # (the formats the routine's formatter is handed: the port reads them for the call's text)


def wbd(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wbd", this=resolve)


def wasr(address, name):
    return spec(address, name, "wasr")


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_wbd_installed", False):
        return
    fc._wbd_installed = True
    install_base_stubs(fc, env)         # (the object base class's services; also marks the functions under test)
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x004801F0, 1, "engine_004801f0", convention="thiscall", this=True)       # ObjectList_Remove
    ensure(fc, 0x004801D0, 1, "engine_004801d0", convention="thiscall", this=True)       # ObjectList_PushHead
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")                                          # SFX_StopVoice
    ensure(fc, 0x004C9FD0, 5, "engine_004c9fd0", result=lambda a: rng.getrandbits(32))   # CPlatform__AI
    ensure(fc, 0x004027F0, 1, "engine_004027f0", convention="thiscall", this=True, result=lambda a: rng.getrandbits(32))   # TRG_ApplyScriptFieldWord
    # the assertion routine's formatter: the buffer and the argument list are locals of its frame
    ensure(fc, 0x00500132, 4, "engine_00500132", stack_to={0: SCRATCH, 3: SCRATCH + 0x200}, text=2)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


# ---- the script object ----------------------------------------------------------------------------------------------------------------------------------------------------------------

def script_chain(img, env, rng, count):
    """Up to three script objects linked through +0x20, each with an id word at +0xc8."""
    count = min(count, 3)
    for k in range(3):
        base = env.slab + SCRIPT[k]
        img.put(base, noise(rng, 0xD0))
        img.u32(base + 0x20, env.slab + SCRIPT[k + 1] if k + 1 < count else 0)
    img.u32(SCRIPT_LIST, env.slab + SCRIPT[0] if count else 0)


def script_id_case(address, name):
    @wbd(address, name)
    def gen(env, n):
        img, rng = Image(), env.rng
        count = rng.choice([0, 1, 2, 3])
        script_chain(img, env, rng, count)
        ids = [rng.choice([0, 1, 2, 7, 0xFFFF, rng.getrandbits(16)]) for _ in range(3)]
        for k in range(3):
            img.u16(env.slab + SCRIPT[k] + 0xC8, ids[k])
        wanted = rng.choice([ids[0], ids[1], ids[2], rng.getrandbits(16), ids[0]])
        return [wanted | (rng.getrandbits(16) << 16 if rng.random() < 0.5 else 0)], img.pokes()
    return gen


script_id_case(0x00401000, "ScriptObject_ClearFlag")
script_id_case(0x00401030, "ScriptObject_SetFlag")


def script_state(img, env, rng):
    fill_object(img, env, rng, 0x100)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    img.u16(SCRIPT_LIST + 4, rng.choice([0, 1, 2, 3, 0xFFFF, rng.getrandbits(16)]))
    img.u32(SCRIPT_LIST, rng.choice([0, env.slab + SCRIPT[0]]))


@wbd(0x00401100, "ScriptObject_ScalarDelete", OBJ)
def gen_script_scalar_delete(env, n):
    img, rng = Image(), env.rng
    script_state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wbd(0x00401120, "ScriptObject_Destruct", OBJ)
def gen_script_destruct(env, n):
    img, rng = Image(), env.rng
    script_state(img, env, rng)
    return [], img.pokes()


@wbd(0x004011D0, "Object_AddSpin", OBJ)
def gen_add_spin(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x80)
    return [], img.pokes()


for _address in (0x00401240, 0x00401250, 0x00401260):
    wbd(_address, "Object_EmptyHook_%08X" % _address)(lambda env, n: ([], Image().pokes()))


# ---- the baddy ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def baddy_state(img, env, rng, high=0x1F0):
    fill_object(img, env, rng, high)
    fake_sprite(img, env, rng)
    img.u32(env.slab + OBJ + 0xA4, sprite_pointer(env, rng))
    img.u32(BADDY_COUNT, rng.choice([0, 1, 2, 5, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(env.slab + OBJ + 0x1D0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 3, rng.getrandbits(32)]))


@wbd(0x00401270, "Baddy_Construct", OBJ)
def gen_baddy_construct(env, n):
    img, rng = Image(), env.rng
    baddy_state(img, env, rng)
    return [], img.pokes()


@wbd(0x004012D0, "Baddy_ScalarDelete", OBJ)
def gen_baddy_scalar_delete(env, n):
    img, rng = Image(), env.rng
    baddy_state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wbd(0x004012F0, "Baddy_Destruct", OBJ)
def gen_baddy_destruct(env, n):
    img, rng = Image(), env.rng
    baddy_state(img, env, rng)
    for offset in (0x138, 0x13C, 0xEC, 0xE8):
        img.u32(env.slab + OBJ + offset, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))
    return [], img.pokes()


@wbd(0x00401410, "Baddy_StepPhysics", OBJ)
def gen_step_physics(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x100)
    img.put(STEP_WORDS, noise(rng, 8))
    if rng.random() < 0.5:        # small velocities, so the dead zone and the decay show
        for k in range(3):
            img.u32(env.slab + OBJ + 0x4C + 4 * k, rng.choice([0, 1, -1, 2, -2, 3, -3, 0x100, -0x100]) & 0xFFFFFFFF)
        for k in range(3):
            img.u16(env.slab + OBJ + 0x70 + 2 * k, rng.choice([0, 1, -1, 2, -2, 0x10, -0x10, 0x7FFF, 0x8000]) & 0xFFFF)
    for k in range(3):
        img.u8(env.slab + OBJ + 0x6C + k, rng.choice([0, 1, 2, 4, 8, 15, 16, 31, 32, 33, rng.getrandbits(8)]))
        img.u8(env.slab + OBJ + 0x7C + k, rng.choice([0, 1, 2, 4, 8, 15, 16, 31, 32, 33, rng.getrandbits(8)]))
    return [], img.pokes()


def ai_case(address, name):
    @wbd(address, name, OBJ)
    def gen(env, n):
        img, rng = Image(), env.rng
        fill_object(img, env, rng, 0x80)
        img.put(env.slab + SCRIPT[0], noise(rng, 0x80))
        img.u32(PLAYER_RECORD, env.slab + SCRIPT[0])          # (the player record is an object: its +0x6a flags say whether it is dying)
        return [word(rng), word(rng), word(rng)], img.pokes()
    return gen


ai_case(0x00402F80, "Object_AiViewOfPlayer")
ai_case(0x00402FC0, "Object_AiViewByPlayer")


# ---- the type-0xcb baddy ----------------------------------------------------------------------------------------------------------------------------------------------------------

@wbd(0x00403000, "TRG_ConstructSubtype0x00cb", OBJ)
def gen_construct_cb(env, n):
    img, rng = Image(), env.rng
    baddy_state(img, env, rng)
    img.put(env.slab + RECORD, noise(rng, 0x20))
    img.u32(OWNER, word(rng))
    return [env.slab + RECORD + rng.choice([0, 0, 0, 0, 4, 1, 2, 3]), rng.getrandbits(32)], img.pokes()


def cb_state(img, env, rng):
    baddy_state(img, env, rng)
    img.u32(env.slab + OBJ + 0x1E0, rng.choice([0, env.slab + TABLE, env.slab + TABLE]))
    img.u16(env.slab + OBJ + 0x1DC, rng.choice([0, 1, 2, 3, 8, 8]))
    img.u32(env.slab + OBJ + 0x1E4, sprite_pointer(env, rng))
    img.u32(env.slab + OBJ + 0x1E8, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 7, rng.getrandbits(32)]))
    for k in range(8):
        base = env.slab + TABLE + 0x10 * k
        img.put(base, noise(rng, 0x10))
        img.u32(base + 0xC, rng.choice([0, env.slab + SPRITE_A, env.slab + SPRITE_A]))
    for offset in (0x138, 0x13C, 0xEC, 0xE8):
        img.u32(env.slab + OBJ + offset, rng.choice([0, 0, env.slab + SPRITE_B, rng.getrandbits(32)]))


@wbd(0x004030B0, "Subtype0x00cb_ScalarDelete", OBJ)
def gen_cb_scalar_delete(env, n):
    img, rng = Image(), env.rng
    cb_state(img, env, rng)
    return [rng.choice([0, 1, 2, 3, word(rng)])], img.pokes()


@wbd(0x004030D0, "Subtype0x00cb_Destruct", OBJ)
def gen_cb_destruct(env, n):
    img, rng = Image(), env.rng
    cb_state(img, env, rng)
    return [], img.pokes()


@wbd(0x00403400, "TRG_ApplyScriptFieldWordThunk", OBJ)
def gen_apply_thunk(env, n):
    img, rng = Image(), env.rng
    fill_object(img, env, rng, 0x40)
    return [word(rng)], img.pokes()


# ---- the assertion routine ------------------------------------------------------------------------------------------------------------------------------------------------------

@wasr(0x004011E0, "M3dUtils_InBetween")
def gen_assertion(env, n):
    rng = env.rng
    return [rng.choice([0, 0, 0, 0x100, 0xFF00, 1, 0xFF, 0x12345601, rng.getrandbits(32)]), rng.choice(ASSERT_FORMATS), word(rng), word(rng)], Image().pokes()
