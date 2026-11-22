"""Phase 4.15b probe: the small functions of camera.cpp (group cms): the quaternion class, the camera rigs' constructors and getters, the camera's setters, the override save and restore, the shake, the
blend target, the camera points, the empty hooks and the tuning initialisers.

A fake camera (0x700 bytes, all of it captured) stands for the CCamera; two free-standing rigs and two quaternion records, a record of shorts, a chain of four objects (for the shake flags) and a
destination buffer sit in the slab. The tuning globals (0x0055f750..0x0055fa20) are poked with random words so every initialiser's three stores show. The object list functions (remove, base destructor,
Object_Free) are stubs; CClass_OpDelete runs as retail code with its Mem_Delete call site stubbed.

Slab layout (offsets): camera 0x000, rigs 0x700 / 0x7a0, quaternions 0x840 / 0x860, the record of shorts 0x880, the object chain 0x8a0 (four objects of 0x80 bytes), the destination 0xaa0.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("cms",)

CAM = 0x000
CAM_BYTES = 0x700
RIG_A = 0x700
RIG_B = 0x7A0
QUAT_A = 0x840
QUAT_B = 0x860
RECORD = 0x880
OBJECTS = 0x8A0
DEST = 0xAA0          # (after the four objects of 0x80 bytes)
SLAB = 0xB00

TUNING = 0x0055F750
TUNING_BYTES = 0x2D0
CAMERA_COUNT = 0x0055FA34
POINTS = 0x0055FAE4
GLOBALS = [(TUNING, TUNING_BYTES), (0x0055FA30, 0x20), (POINTS, 4), (0x00524A40, 0x20), (0x0055F6BC, 4), (0x0056AF40, 4), (0x0056A920, 12), (0x0056A970, 4)]


def cms_regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = cms_regions
    READ_ONLY[_group] = []


def cms(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "cms", this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_cms_installed", False):
        return
    fc._cms_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    ensure(fc, 0x004801F0, 1, "engine_004801f0", convention="thiscall", this=True)
    ensure(fc, 0x00480160, 0, "engine_00480160", convention="thiscall", this=True)
    ensure(fc, 0x0047FD60, 1, "engine_0047fd60")
    fc.stub_site(0x00466BBC, 5, 1, "mem_delete", writes=[(0x0056A970, 4, lambda a: rng.getrandbits(32))])


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def short(rng):
    return rng.choice([0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def fill_camera(img, env, rng, low=0x4D0, high=0x520):
    """Random bytes in the window of the camera a function reads (a pre-state of random words everywhere would only make a poke slower)."""
    img.put(env.slab + CAM + low, bytes(rng.getrandbits(8) for _ in range(high - low)))


def tuning_noise(img, rng):
    img.put(TUNING, bytes(rng.getrandbits(8) for _ in range(TUNING_BYTES)))


# ---- the quaternion class -------------------------------------------------------------------------------------------------------------------------------------------------------

@cms(0x0040E000, "Camera_QuaternionConstruct", QUAT_A)
def gen_quat_construct(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + QUAT_A, bytes(rng.getrandbits(8) for _ in range(0x14)))
    return [word(rng), word(rng), word(rng), word(rng)], img.pokes()


@cms(0x0040E030, "Camera_QuaternionScalarDelete", QUAT_A)
def gen_quat_scalar_delete(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + QUAT_A, bytes(rng.getrandbits(8) for _ in range(0x14)))
    img.u32(0x0056A920, word(rng))
    img.u32(0x0056A970, word(rng))
    return [rng.choice([0, 1, 2, 3, rng.getrandbits(32)])], img.pokes()


@cms(0x0040E050, "Camera_QuaternionDestruct", QUAT_A)
def gen_quat_destruct(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + QUAT_A, bytes(rng.getrandbits(8) for _ in range(0x14)))
    return [], img.pokes()


@cms(0x0040E060, "Camera_QuaternionCopyConstruct", QUAT_A)
def gen_quat_copy(env, n):
    img, rng = Image(), env.rng
    img.put(env.slab + QUAT_A, bytes(rng.getrandbits(8) for _ in range(0x14)))
    img.put(env.slab + QUAT_B, bytes(rng.getrandbits(8) for _ in range(0x14)))
    return [env.slab + QUAT_B], img.pokes()


# ---- the rigs and camera points ---------------------------------------------------------------------------------------------------------------------------------------------------

def fill_rig(img, env, rng, base):
    img.put(env.slab + base, bytes(rng.getrandbits(8) for _ in range(0xA0)))


@cms(0x004117F0, "CameraRig_Construct", RIG_A)
def gen_rig_construct(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    return [], img.pokes()


@cms(0x004119E0, "CameraRig_Destruct", RIG_A)
def gen_rig_destruct(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    return [], img.pokes()


@cms(0x00411880, "CameraRig_ScalarDelete", RIG_A)
def gen_rig_scalar_delete(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    img.u32(0x0056A920, word(rng))
    img.u32(0x0056A970, word(rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@cms(0x004118A0, "CameraRig_ConstructFromRecord", RIG_A)
def gen_rig_from_record(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    for k in range(8):
        img.u16(env.slab + RECORD + 2 * k, short(rng) & 0xFFFF)
    if rng.random() < 0.1:
        for k in range(4):
            img.u16(env.slab + RECORD + 2 * k, 0)       # the zero quaternion: the length counts as 1
    return [env.slab + RECORD], img.pokes()


@cms(0x00411C50, "CameraRig_CopyMatrix", RIG_A)
def gen_rig_copy_matrix(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    img.put(env.slab + DEST, bytes(rng.getrandbits(8) for _ in range(0x40)))
    return [env.slab + DEST], img.pokes()


@cms(0x00411C70, "CameraRig_GetPosition", RIG_A)
def gen_rig_get_position(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    img.put(env.slab + DEST, bytes(rng.getrandbits(8) for _ in range(0x40)))
    return [env.slab + DEST], img.pokes()


@cms(0x00411CA0, "CameraRig_GetTarget", RIG_A)
def gen_rig_get_target(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    img.put(env.slab + DEST, bytes(rng.getrandbits(8) for _ in range(0x40)))
    return [env.slab + DEST], img.pokes()


@cms(0x00411EC0, "CameraPoint_Destruct", RIG_A)
def gen_point_destruct(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    return [], img.pokes()


@cms(0x00411ED0, "CameraPoint_ScalarDelete", RIG_A)
def gen_point_scalar_delete(env, n):
    img, rng = Image(), env.rng
    fill_rig(img, env, rng, RIG_A)
    img.u32(0x0056A920, word(rng))
    img.u32(0x0056A970, word(rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@cms(0x00411EF0, "CameraPoints_Clear")
def gen_points_clear(env, n):
    img, rng = Image(), env.rng
    img.u32(POINTS, word(rng))
    return [], img.pokes()


@cms(0x00411F00, "CameraPoints_ClearAndResetTuning")
def gen_points_reset(env, n):
    img, rng = Image(), env.rng
    img.u32(POINTS, word(rng))
    for k in range(3):
        img.u32(0x00524A40 + 4 * k, word(rng))
    return [], img.pokes()


# ---- the camera ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@cms(0x0040BC00, "Camera_Destruct", CAM)
def gen_destruct(env, n):
    img, rng = Image(), env.rng
    img.u32(CAMERA_COUNT, word(rng))
    return [], img.pokes()


@cms(0x0040BBE0, "Camera_ScalarDelete", CAM)
def gen_scalar_delete(env, n):
    img, rng = Image(), env.rng
    img.u32(CAMERA_COUNT, word(rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


def setter(address, name, shorts=False):
    @cms(address, name, CAM)
    def gen(env, n):
        img, rng = Image(), env.rng
        fill_camera(img, env, rng)
        return [word(rng)], img.pokes()
    return gen


for _address, _name in ((0x0040BCE0, "Camera_SetOverrides"), (0x0040BD00, "Camera_SetOverrideA"), (0x0040BD10, "Camera_SetOverrideB"), (0x0040BD20, "Camera_SetWindowWidth"), (0x0040BD30, "Camera_SetWindowHeight")):
    setter(_address, _name)


def plain(address, name, args=0):
    @cms(address, name, CAM)
    def gen(env, n):
        img, rng = Image(), env.rng
        fill_camera(img, env, rng)
        return [word(rng) for _ in range(args)], img.pokes()
    return gen


for _address, _name in ((0x0040C070, "Camera_PushOverrides"), (0x0040C0E0, "Camera_PopOverrides"), (0x0040C130, "Camera_PushOverrideB"), (0x0040C170, "Camera_PopOverrideB"), (0x0040F130, "Camera_ClearFlag419"),
                        (0x0040F1F0, "Camera_GetBlendValue")):
    plain(_address, _name)

for _address, _name, _args in ((0x0040BE60, "Camera_ShakeHook", 1), (0x0040F0D0, "Camera_Hook_F0D0", 2), (0x0040F0E0, "Camera_Hook_F0E0", 0), (0x0040F0F0, "Camera_Hook_F0F0", 2), (0x0040F100, "Camera_Hook_F100", 0),
                               (0x0040F110, "Camera_Hook_F110", 0), (0x0040F120, "Camera_Hook_F120", 2), (0x0040F140, "Camera_Hook_F140", 2), (0x0040F150, "Camera_Hook_F150", 2), (0x0040F160, "Camera_Hook_F160", 2),
                               (0x0040F170, "Camera_Hook_F170", 2), (0x0040F180, "Camera_Hook_F180", 2), (0x0040F190, "Camera_Hook_F190", 2), (0x004117B0, "CameraRig_Hook_7B0", 1), (0x004117A0, "CameraRig_Hook_7A0", 0),
                               (0x004117C0, "CameraRig_Hook_7C0", 0), (0x004117D0, "CameraRig_Hook_7D0", 0), (0x004117E0, "CameraRig_Hook_7E0", 0), (0x00411CC0, "CameraRig_Hook_CC0", 5), (0x00411CD0, "CameraRig_Hook_CD0", 0)):
    plain(_address, _name, _args)


@cms(0x004113E0, "CCamera__InCompetitionIntro", CAM)
def gen_in_intro(env, n):
    img, rng = Image(), env.rng
    fill_camera(img, env, rng)
    img.u32(env.slab + CAM + 0x504, rng.choice([0x17, 0x17, 0x16, 0x18, 0, word(rng)]))
    return [], img.pokes()


@cms(0x0040F1A0, "Camera_SetBlendTarget", CAM)
def gen_set_blend(env, n):
    img, rng = Image(), env.rng
    fill_camera(img, env, rng)
    return [rng.choice([0, 1, 0x100, 0x1000, 0xFFFF, word(rng)]), rng.choice([0, 0, 1, 2, 5, 30, 60, 0xFFFF, rng.getrandbits(32)])], img.pokes()


@cms(0x0040BE40, "Camera_FlagObjectsForShake")
def gen_flag_objects(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 4])
    for k in range(4):
        base = env.slab + OBJECTS + 0x80 * k
        img.put(base, bytes(rng.getrandbits(8) for _ in range(0x80)))
        img.u32(base + 0x20, env.slab + OBJECTS + 0x80 * (k + 1) if k + 1 < count else 0)
    return [word(rng), env.slab + OBJECTS if count else 0, word(rng)], img.pokes()


@cms(0x0040BD40, "Camera_ApplyShake", CAM)
def gen_apply_shake(env, n):
    img, rng = Image(), env.rng
    fill_camera(img, env, rng)
    tuning_noise(img, rng)
    count_a = rng.choice([0, 1, 2, 3, 4])
    count_b = rng.choice([0, 1, 2, 3, 4])
    for k in range(4):
        base = env.slab + OBJECTS + 0x80 * k
        img.put(base, bytes(rng.getrandbits(8) for _ in range(0x80)))
    # two chains in one block of four objects: A is objects 0..1 (up to count_a of them), B is objects 2..3
    for k in range(2):
        img.u32(env.slab + OBJECTS + 0x80 * k + 0x20, env.slab + OBJECTS + 0x80 * (k + 1) if k + 1 < min(count_a, 2) else 0)
        img.u32(env.slab + OBJECTS + 0x80 * (k + 2) + 0x20, env.slab + OBJECTS + 0x80 * (k + 3) if k + 1 < min(count_b, 2) else 0)
    img.u32(0x0055F6BC, env.slab + OBJECTS if count_a else 0)
    img.u32(0x0056AF40, env.slab + OBJECTS + 0x100 if count_b else 0)
    return [word(rng), rng.choice([0, 1, 2, 0, 1, 2, 3, 0xFFFFFFFF])], img.pokes()


# ---- the tuning initialisers ----------------------------------------------------------------------------------------------------------------------------------------------------

def tuning(address, name):
    @cms(address, name)
    def gen(env, n):
        img, rng = Image(), env.rng
        tuning_noise(img, rng)
        return [], img.pokes()
    return gen


for _thunk, _body in ((0x0040ED70, 0x0040ED80), (0x0040EDA0, 0x0040EDB0), (0x0040EDD0, 0x0040EDE0), (0x0040EE00, 0x0040EE10), (0x0040EE30, 0x0040EE40), (0x0040EE60, 0x0040EE70), (0x0040EE90, 0x0040EEA0),
                      (0x0040EEC0, 0x0040EED0), (0x0040EEF0, 0x0040EF00), (0x0040EF20, 0x0040EF30), (0x0040EF50, 0x0040EF60), (0x0040EF80, 0x0040EF90), (0x0040EFB0, 0x0040EFC0), (0x0040EFE0, 0x0040EFF0),
                      (0x0040F010, 0x0040F020), (0x0040F040, 0x0040F050), (0x0040F070, 0x0040F080), (0x0040F0A0, 0x0040F0B0)):
    tuning(_thunk, "Camera_TuningThunk_%08X" % _body)
    tuning(_body, "Camera_Tuning_%08X" % _body)
