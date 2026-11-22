"""Phase 5 (10g) probe: Game_GameplayUpdate 0x00469de0 (group gpu), the level loop's per-frame update.

The function runs the level-event, crowd and skater-animation updates, the object lists (or, while the world is frozen, the suspended objects' virtual update), the level-start countdown that moves
each camera's word at +0x5b4, the speech and trick-tip updates, the competition modes' one-shot latches and the dying-record test. Everything outside main.cpp is a stub that reports its call
(`engine_<address>`, a method records `this` first); CalcPolyBufferEnd, Utils_ClearExplosionStack and CheckSkatersStop run as retail code. The camera list's call site (0x00469e8e) scripts the freeze
flag the camera update can set.

Slab layout (offsets): player 0 at 0x0000 and player 1 at 0x3300 (windows only), cameras 0x6600 / 0x6c00 (the word at +0x5b4), the render state 0x7200, four list objects 0x7300 (0x100 each), a vtable 0x7700.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("gpu", "gpa", "gpo", "gsu", "gpf", "gas", "guh", "gua") + ('tri', 'tmh', 'tcr', 'tqs', 'tsp', 'tss', 'tup', 'trs', 'tdp') + ("gub", "gdp") + ("ggp", "gbr") + ("gof", "gic", "gzq", "gcs", "gcc", "gcp", "gcf") + ("gab",)      # (gab: the air collision recovery 0x00497aa0 and the air basis 0x00497df0, 12 s3; gpa: Skater_UpdateAnimationObjects and gpo: the three object-list loops, callees of the first; groups of their own so that the stubs of them stay on while Game_GameplayUpdate is captured; gsu: the skater's frame)

P0 = 0x0000
P1 = 0x3300
CAM0 = 0x6600
CAM1 = 0x6C00
RENDER = 0x7200
OBJ = 0x7300
OBJ_BYTES = 0x100
VTABLE = 0x7700
SLAB = 0x7800

PLAYER_WINDOWS = [(0x29B0, 4), (0x2DD4, 4), (0x30B8, 4)]
SLOT = 0x004E87B0            # the function the fake vtable's slot 3 holds (stubbed)

GLOBALS = [(0x00533F38, 8), (0x00568814, 4), (0x0056A900, 4), (0x0056A8D4, 4), (0x00561C04, 4), (0x0056A8E0, 4), (0x0055F6BC, 4), (0x0056AF40, 4), (0x0056A960, 4), (0x0056B830, 4),
           (0x00568658, 4), (0x00568818, 4), (0x00568610, 4), (0x0056A858, 8), (0x0056A854, 4), (0x0056AFF8, 0x2D8), (0x00568698, 0x16C), (0x00568630, 4), (0x0056A8C0, 4), (0x00568618, 4),
           (0x00561BFC, 4), (0x0056A8D0, 4), (0x0056E30C, 4), (0x00560FD4, 4), (0x00561150, 4), (0x00568628, 4)]

STATE = {"playing": 0, "dying": 0, "freeze": 0}


def gpu_regions(slab):
    windows = [(slab + RENDER, 0xC0), (slab + VTABLE, 0x20)]
    for base in (P0, P1):
        windows += [(slab + base + off, size) for off, size in PLAYER_WINDOWS]
    for base in (CAM0, CAM1):
        windows.append((slab + base + 0x5B4, 4))
    for k in range(4):
        windows.append((slab + OBJ + OBJ_BYTES * k, OBJ_BYTES))
    windows += list(GLOBALS)
    return merged(windows, gap=0)


GPA_PLAYER_WINDOWS = [(0xF6, 2), (0x107, 1), (0x16C, 4), (0x2A8, 4), (0x2DD4, 4), (0x308C, 0x0C), (0x30B8, 4)]
GPA_GLOBALS = [(0x00533F38, 4), (0x00568658, 4), (0x00568818, 4), (0x00568610, 4), (0x0056A858, 8), (0x0056AFF8, 0x2D8)]


def gpa_regions(slab):
    windows = []
    for base in (P0, P1):
        windows += [(slab + base + off, size) for off, size in GPA_PLAYER_WINDOWS]
    windows += list(GPA_GLOBALS)
    return merged(windows, gap=0)


# gpo: Ob_ClearInputFlags 0x00480fd0, Ob_QuickAI 0x00481110, Ob_MaybeUnSuspendOrCull 0x00481150. Slab: eight 0x80-byte objects at 0x000 (six list members and two player records), the list head
# cell at 0x400, a vtable at 0x420 (slot 0 the deleting destructor, slot 3 the update, both stubbed)
GPO_OBJECTS = 8
GPO_STRIDE = 0x80
GPO_HEAD = 0x400
GPO_VTABLE = 0x420
GPO_DELETE = 0x004045E0      # (stubs standing for the vtable's slots; retail functions nothing else reaches while the case runs)
GPO_UPDATE = 0x004E87B0
GPO_GLOBALS = [(0x0056A960, 4), (0x0056AF44, 4), (0x00533F3C, 4), (0x00535AF8, 4)]


def gpo_regions(slab):
    windows = [(slab, GPO_OBJECTS * GPO_STRIDE), (slab + GPO_HEAD, 4), (slab + GPO_VTABLE, 0x10)]
    windows += list(GPO_GLOBALS)
    return merged(windows, gap=0)


# gsu: Skater_Update 0x0046dd60 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes, all captured), two objects a sphere test can answer at 0x3700 / 0x3800 (their +0xf0 word).
GSU_SKATER_BYTES = 0x3600
GSU_HITS = (0x3700, 0x3800)
GSU_SPHERE = 0x7FF00800 + 0xC00 + 0xE0      # (worldkit::kScratch + 0xe0: the local vector the two sphere tests share)
GSU_GLOBALS = [(0x00561C04, 4), (0x006A3D18, 4), (0x0056D444, 0x44 * 3), (0x0055FA38, 4), (0x005685F4, 4), (0x0056A958, 4), (0x0056A858, 8), (0x0056E5D8, 8), (0x0056E450, 8),
               (0x00568638, 4), (0x0056881C, 0x40), (0x0055F6BC, 4), (0x0056B830, 4)]


def gsu_regions(slab):
    windows = [(slab, GSU_SKATER_BYTES)] + [(slab + hit, 0x100) for hit in GSU_HITS] + list(GSU_GLOBALS)
    return merged(windows, gap=0)


# gpf: Skater_PhysicsFrame 0x0049e680 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes, all captured), its pad profile at 0x3700. The zone query record is a stack local, recorded at
# the scratch address the port uses (skaterkit, kScratchBase + 0xf50). The vector and GTE helpers run as retail code (their writes to the GTE words are compared).
GPF_PROFILE = 0x3700
GPF_QUERY = 0x7FF00800 + 0xF50
GPF_GLOBALS = [(0x0056A898, 4), (0x0056B7C0, 12), (0x0056B7EC, 4), (0x0056A93C, 4), (0x005685F4, 4), (0x00567C78, 8), (0x00568638, 4), (0x0056881C, 0x40), (0x00533F38, 4), (0x0056DB64, 4),
               (0x006A3B20, 4), (0x0056A86C, 4), (0x0056E320, 4), (0x0056865C, 4), (0x006A3E00, 0x120)]
GPF_STATE = {"profile": [], "body": 0, "w78": 0, "w7c": 0, "distance": 0}


def gpf_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100)] + list(GPF_GLOBALS), gap=0)


# gas: Skater_ActionPhysicsStep 0x00493370 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes), its pad profile at 0x3700, the camera at 0x3800 (only its position at +8 is read). The matrix, GTE
# and dot-product helpers run as retail code (the GTE words are compared); the turn phase, the trick text and the heading update are stubs.
GAS_CAMERA = 0x3800
GAS_GLOBALS = [(0x0056865C, 4), (0x0056A88C, 4), (0x0056B7F0, 4), (0x0056B038, 0x24), (0x005685F4, 4), (0x006A3E00, 0x120)]


def gas_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100), (slab + GAS_CAMERA, 0x20)] + list(GAS_GLOBALS), gap=0)


# guh: Skater_UpdateHeading 0x00492f20 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and its pad profile at 0x3700. The frame stepper 0x00492ed0 runs as retail code; the animation requests and
# the completion check are stubs.
def guh_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100)], gap=0)


# the trick recognition groups (10g): the skater at 0x0000 (0x3600 bytes), the pad profile at 0x3700, the player's generated trick table at 0x3A00 (and an empty one at 0x3E00), and the globals the code reads.
# tri RefreshHistory (the record and pattern helpers run as retail code), tmh MatchHistory, tcr ConsumeRecord, tqs QueueStart, tsp StartPending, tss SelectSequence (the helpers run as retail code), tup Update (all stubbed),
# trs RecordState, tdp DerivePattern.
TRICK_GROUPS = ('tri', 'tmh', 'tcr', 'tqs', 'tsp', 'tss', 'tup', 'trs', 'tdp')
TRK_TABLE = 0x3A00
TRK_EMPTY = 0x3E00
TRK_GLOBALS = [(0x005685F4, 4), (0x0056A858, 4), (0x00568A4C, 8), (0x0056A894, 4), (0x00568658, 4), (0x00568638, 4), (0x0056881C, 0x40)]


def trk_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100), (slab + 0x3800, 0x800)] + list(TRK_GLOBALS), gap=0)


# gua: Skater_UpdateAcceleration 0x0049b010 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and its pad profile at 0x3700. The dot product, the root and the normal removal (0x00490610)
# run as retail code (the GTE words are compared); the animation requests, the stat and the sound are stubs.
GUA_GLOBALS = [(0x00533F38, 4), (0x0056A854, 4), (0x0056A3D8, 0x20), (0x006A3E00, 0x120)]


def gua_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100)] + list(GUA_GLOBALS), gap=0)


# gab (12 s3): Skater_AirCollisionRecovery 0x00497aa0 (thiscall on the skater; the stack argument is the query record, here at slab + 0x3a00: its normal at +0x78 / +0x7c) and Skater_UpdateAirBasis
# 0x00497df0 (thiscall, no arguments). Slab: the skater at 0x0000 (0x3600 bytes), its pad profile at 0x3700, the record at 0x3a00. The dot product, the root, the Q12 product, the normalisation, the
# cross product (the GTE words) and the basis publication run as retail code; the state transition, the animation request, the take-off and the empty hook are stubs.
GAB_RECORD = 0x3A00
GAB_GLOBALS = [(0x005685F4, 4), (0x0056B7AC, 4), (0x006A3E00, 0x120)]


def gab_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100), (slab + GAB_RECORD, 0xA0)] + list(GAB_GLOBALS), gap=0)


# gub: Skater_UpdateBrakeMode 0x0049df00 and gdp: Skater_PhysicsDispatcher 0x0049db80 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and its pad profile at 0x3700; for gdp the flag table at
# 0x3A00, the camera at 0x4000 (its word at +0x504) and a gap record at 0x4800. Everything the two call is a stub, except the dot product and the root in gub.
GDP_TABLE = 0x3A00
GDP_CAMERA = 0x4000
GDP_GAP = 0x4800
GUB_GLOBALS = [(0x0056865C, 4), (0x00568638, 4), (0x0056881C, 0x40)]
GDP_GLOBALS = [(0x0056DD58, 4), (0x0056A93C, 4), (0x005685F4, 4), (0x0056865C, 4)]


def gub_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100)] + list(GUB_GLOBALS), gap=0)


def gdp_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GDP_TABLE, 0x400), (slab + GDP_CAMERA + 0x500, 0x10), (slab + GDP_GAP, 0x20)] + list(GDP_GLOBALS), gap=0)


# ggp: Skater_GroundPreparation 0x0049dad0 and gbr: Skater_RunGroundedBounceRecovery 0x0049d9c0 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and its pad profile at 0x3700. Everything they call
# is a stub.
GBR_GLOBALS = [(0x005614C4, 4)]


def ggp_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GPF_PROFILE, 0x100)], gap=0)


def gbr_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES)] + list(GBR_GLOBALS), gap=0)


# gof: Skater_OuterFloorRecoveryCheck 0x00490730 (cdecl: the skater, x, y, z, a message). Slab: the skater at 0x0000 (0x3600 bytes) and a surface record at 0x3A00 (its flag word at +0xc). The zone queries are stubs
# that leave their results in the record (the one at skaterkit's kScratchBase + 0xf50, as for the frame's wall test); the debug flag is held at 0.
GOF_SURFACE = 0x3A00
GOF_GLOBALS = [(0x005614C4, 4)]


def gof_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GOF_SURFACE, 0x20)] + list(GOF_GLOBALS), gap=0)


# gic: M3D_InitCollisionQuery 0x004624d0 (cdecl: the record). Slab: the record at 0x0000 (0xa4 bytes) and up to three objects of each list the generation's wrap un-stamps at 0x0200 (0x40 apart). The
# GTE helpers, the root and the generation counter 0x00462490 run as retail code.
GIC_OBJECTS = 0x0200
GIC_GLOBALS = [(0x00567BB0, 0x12), (0x00533AB4, 2), (0x0056DB28, 4), (0x0056AF40, 4), (0x006A3E00, 0x120)]


def gic_regions(slab):
    return merged([(slab, 0xA4), (slab + GIC_OBJECTS, 0x40 * 6)] + list(GIC_GLOBALS), gap=0)


# gzq: M3D_QueryZoneCollision 0x00466090 and M3D_ZoneCollisionBroadphase 0x004660b0 (cdecl: the record, whether the game objects are tested). Slab: the record at 0x0000. The blockmap at
# 0x00567f80 is generated (a grid of up to 20 x 20 cells whose words are only handed on); the cell test, the object list and the commit are stubs.
GZQ_GLOBALS = [(0x00567F80, 0x660), (0x00567A60, 12), (0x00567C74, 0x14), (0x00568638, 4), (0x0056881C, 0x40), (0x0056AF40, 4)]


def gzq_regions(slab):
    return merged([(slab, 0xA4)] + list(GZQ_GLOBALS), gap=0)


# gcs: M3D_CommitCollisionSurface 0x00463d50 and M3D_ReadSurfaceFlags 0x0048ea80 (cdecl: the record). Slab: the record at 0x0000, the object hit at 0x0100 (its angles at +0x14), the surface at
# 0x0200. The matrix of three angles and RT x V0 run as retail code.
GCS_BODY = 0x0100
GCS_SURFACE = 0x0200
GCS_GLOBALS = [(0x00567BB0, 0x12), (0x00564390, 12), (0x006A3E00, 0x120), (0x0056B760, 0x90)]


def gcs_regions(slab):
    return merged([(slab, 0xA4), (slab + GCS_BODY, 0x40), (slab + GCS_SURFACE, 0x10)] + list(GCS_GLOBALS), gap=0)


# gcc: M3D_CollisionCell 0x004638d0 (cdecl: the cell, the record). Slab: the record at 0x0000, the cell's list at 0x0100, the items at 0x0200 (0x40 apart), the region's model table at 0x0600, the
# models at 0x0800 (0x200 apart). The face cache is a legal ring (entries 0x00567a70, oldest 0x00533ab8 / its first face 0x00533abc, next 0x00567c88 / write position 0x00567c8c) whose write position stays
# near either end of the 500 face boxes at 0x005643b0 (0x1c bytes each); its two helpers run as retail code, the object cull 0x004f4940 and the face test 0x00462a20 are stubs.
GCC_CELL, GCC_ITEMS, GCC_TABLE, GCC_MODELS = 0x0100, 0x0200, 0x0600, 0x0800
GCC_GLOBALS = [(0x005643B0, 0x1C * 64), (0x005643B0 + 0x1C * 460, 0x1C * 40), (0x00567A70, 0x140), (0x00533AB8, 8), (0x00567C88, 8), (0x0056D43C, 0x44 * 4), (0x00568638, 4), (0x0056881C, 0x40),
               (0x006A3E10, 0x12)]


def gcc_regions(slab):
    return merged([(slab, 0xA4), (slab + GCC_CELL, 0x40), (slab + GCC_ITEMS, 0x40 * 8), (slab + GCC_TABLE, 0x40), (slab + GCC_MODELS, 0x200 * 4)] + list(GCC_GLOBALS), gap=0)


# gcp: M3D_PrepareCollisionObjectList 0x004628f0 (cdecl: the first object of a list linked at +0x20, the record). Slab: the record at 0x0000, up to six objects at 0x0200 (0x40 apart); the linked
# objects' cull 0x004f43e0 and face test 0x00463e50 are stubs.
GCP_OBJECTS = 0x0200


def gcp_regions(slab):
    return merged([(slab, 0xA4), (slab + GCP_OBJECTS, 0x40 * 6), (0x006A3E10, 0x12)], gap=0)


# gcf: M3D_CollisionFaceTest 0x00462a20 (cdecl: the cell's item, the record, the item's face-cache entry). Slab: the record at 0x0000, the entry at 0x00c0, the item at 0x0100, the region's model table at
# 0x0180, the model at 0x0200, the two environments' trigger command arrays at 0x0700 / 0x0740. The face boxes are the first 56 of the ring at 0x005643b0. The x87 mul-div and Fixed_MulQ12 run as retail
# code; Spool_GetEnvIndex 0x004b3990 (scripted 0 or 1) and Trig_TriggerCommandPoint 0x004c4bd0 are stubs.
GCF_ENTRY, GCF_ITEM, GCF_TABLE, GCF_MODEL, GCF_LISTS = 0x00C0, 0x0100, 0x0180, 0x0200, 0x0700
GCF_GLOBALS = [(0x005643B0, 0x1C * 56), (0x0056438C, 0x10), (0x00567A60, 12), (0x00567C80, 4), (0x0056DB2C, 4), (0x0056D41C, 8), (0x0056D43C, 0x44 * 4), (0x00568638, 4), (0x0056881C, 0x40)]


def gcf_regions(slab):
    return merged([(slab, 0xA4), (slab + GCF_ENTRY, 0x10), (slab + GCF_ITEM, 0x40), (slab + GCF_TABLE, 0x10), (slab + GCF_MODEL, 0x500), (slab + GCF_LISTS, 0x80)] + list(GCF_GLOBALS), gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = trk_regions if _group in TRICK_GROUPS else {"gpa": gpa_regions, "gpo": gpo_regions, "gsu": gsu_regions, "gpf": gpf_regions, "gas": gas_regions, "guh": guh_regions,
                                                                      "gab": gab_regions, "gua": gua_regions, "gub": gub_regions, "gdp": gdp_regions, "ggp": ggp_regions, "gbr": gbr_regions, "gof": gof_regions, "gic": gic_regions, "gzq": gzq_regions, "gcs": gcs_regions, "gcc": gcc_regions, "gcp": gcp_regions, "gcf": gcf_regions}.get(_group, gpu_regions)
    READ_ONLY[_group] = [(0x005347B0, 0x10)] if _group == "gpf" else ([(0x005369C8, 0x100)] if _group in TRICK_GROUPS else [])


def gpu(address, name):
    return spec(address, name, "gpu")


def gpo(address, name):
    return spec(address, name, "gpo")


def gpa(address, name):
    return spec(address, name, "gpa")


def install_stubs(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpu_installed", False):
        return
    fc._gpu_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    for address, args in ((0x00469A30, 0), (0x0042F430, 0), (0x004CDAB0, 0), (0x004895C0, 1), (0x004498F0, 0), (0x00480FD0, 1), (0x00480FF0, 1), (0x00407270, 0), (0x004BDD00, 0), (0x00481110, 1),
                          (0x00481150, 0), (0x00403BD0, 0), (0x004AF630, 0), (0x0048B2B0, 0), (0x004C4620, 0), (0x00473760, 0), (0x004B39B0, 0), (0x004494B0, 0), (0x0044E7E0, 0),
                          (0x0044E540, 0), (0x0044E5C0, 0)):
        ensure(fc, address, args, "engine_%08x" % address)
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: STATE["playing"])        # Video_Playing
    ensure(fc, 0x004805F0, 0, "engine_004805f0", convention="thiscall", this=True, result=lambda a: STATE["dying"])        # Object_IsDying
    ensure(fc, SLOT, 0, "engine_%08x" % SLOT, convention="thiscall", this=True)             # the suspended objects' virtual update (slot 3)
    fc.stub_site(0x00469E8E, 5, 1, "engine_00480ff0", writes=[(0x00561C04, 4, lambda a: STATE["freeze"])])


def install_stubs_gpa(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpa_installed", False):
        return
    fc._gpa_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004CF850, 1, "printf_log")
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)       # Animation_Start
    ensure(fc, 0x004CBF30, 1, "engine_004cbf30")                                         # the replay reset of a skater's slot
    ensure(fc, 0x004AD9F0, 1, "engine_004ad9f0")                                         # SFX_StopVoice
    ensure(fc, 0x0044E0F0, 0, "engine_0044e0f0")                                         # the level-event completion


def install_stubs_gpo(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpo_installed", False):
        return
    fc._gpo_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, GPO_DELETE, 1, "engine_%08x" % GPO_DELETE, convention="thiscall", this=True)       # slot 0 of the fake vtable (the deleting destructor)
    ensure(fc, GPO_UPDATE, 0, "engine_%08x" % GPO_UPDATE, convention="thiscall", this=True)       # slot 3 (the update)
    ensure(fc, 0x004803B0, 0, "engine_004803b0", convention="thiscall", this=True)                # CBody__UnSuspend


def install_stubs_gsu(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gsu_installed", False):
        return
    fc._gsu_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    slab = env.slab
    ensure(fc, 0x004DE010, 1, "engine_004de010", result=lambda a: rng.choice([0, 0, 0, 0, 1, 0x100, 0xFFFFFF01]))                 # NetGame_IsRemotePlayer (a bool in AL)
    ensure(fc, 0x004113E0, 0, "engine_004113e0", convention="thiscall", this=True, result=lambda a: rng.choice([0] * 8 + [1, 0x100]))   # CCamera__InCompetitionIntro
    for address in (0x0048EC00, 0x0048F590, 0x0046D6E0, 0x0049E680, 0x0046E080, 0x0046D7A0):
        ensure(fc, address, 0, "engine_%08x" % address, convention="thiscall", this=True)
    for address in (0x004CD750, 0x004CC0E0, 0x004DE790, 0x0049A1B0, 0x0049A1E0):
        ensure(fc, address, 1, "engine_%08x" % address)
    ensure(fc, 0x004DE6D0, 0, "engine_004de6d0")
    ensure(fc, 0x0046D790, 1, "engine_0046d790", convention="thiscall", this=True)
    ensure(fc, 0x004A86F0, 1, "engine_004a86f0", convention="thiscall", this=True)
    ensure(fc, 0x00464200, 6, "engine_00464200", stack_to={2: GSU_SPHERE}, blob=(2, 12),                                            # M3dColij_LineToSphere: the object met, or 0
           result=lambda a: rng.choice([0, 0, 0, slab + GSU_HITS[0], slab + GSU_HITS[1]]))


def install_stubs_gpf(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpf_installed", False):
        return
    fc._gpf_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x00431320, 1, "engine_00431320", convention="thiscall", this=True)                 # Dust_Update (on the dust at +0x2c14)
    ensure(fc, 0x00431260, 2, "engine_00431260", convention="thiscall", this=True)                 # Dust_Reset
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,                 # Skater_ProfileValue: a stat
           result=lambda a: rng.choice([0, 1, 3, 5, 5, 7, 10, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11)]))
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0] * 6 + [1, 0x100]))   # Career_GetCheat (a bool in AL)
    for address in (0x0049E480, 0x0049E430, 0x0049C7D0, 0x00493370, 0x0049B010, 0x0049A280, 0x00492EA0, 0x0049DF00, 0x0049C970, 0x0046C630, 0x0049DB80, 0x0049D8A0,
                    0x0049D2F0, 0x0049C850, 0x004914D0, 0x0049D740, 0x0049D480, 0x0049E570):
        ensure(fc, address, 0, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x0048F5D0, 1, "engine_0048f5d0", convention="thiscall", this=True)
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)
    ensure(fc, 0x00490480, 5, "engine_00490480", convention="thiscall", this=True)
    ensure(fc, 0x0046DD20, 2, "engine_0046dd20", convention="thiscall", this=True)
    ensure(fc, 0x0049BAD0, 3, "engine_0049bad0", convention="thiscall", this=True)
    ensure(fc, 0x00496060, 3, "engine_00496060", convention="thiscall", this=True)
    ensure(fc, 0x00490730, 5, "engine_00490730")
    ensure(fc, 0x004B8560, 1, "engine_004b8560")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GPF_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, lambda a: GPF_STATE["body"], 0x68), (0, 4, lambda a: GPF_STATE["w78"], 0x78), (0, 4, lambda a: GPF_STATE["w7c"], 0x7C)])
    ensure(fc, 0x004660B0, 2, "engine_004660b0", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, lambda a: GPF_STATE["body"], 0x68), (0, 4, lambda a: GPF_STATE["distance"], 0x40), (0, 4, lambda a: GPF_STATE["w78"], 0x78),
                  (0, 4, lambda a: GPF_STATE["w7c"], 0x7C)])


def install_stubs_gas(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gas_installed", False):
        return
    fc._gas_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x0049B500, 3, "engine_0049b500", convention="thiscall", this=True)                 # Skater_ApplyTurnVelocityPhase(turn, 0, 0)
    ensure(fc, 0x0048DDC0, 0, "engine_0048ddc0", convention="thiscall", this=True)                 # Panel_RefreshTrickText (on the panel at +0x168)
    ensure(fc, 0x00492F20, 0, "engine_00492f20", convention="thiscall", this=True)                 # Skater_UpdateHeading


def install_stubs_guh(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_guh_installed", False):
        return
    fc._guh_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)
    ensure(fc, 0x00490450, 4, "engine_00490450", convention="thiscall", this=True)
    ensure(fc, 0x00490480, 5, "engine_00490480", convention="thiscall", this=True)
    ensure(fc, 0x00496280, 0, "engine_00496280", convention="thiscall", this=True)


def install_stubs_gua(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gua_installed", False):
        return
    fc._gua_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)
    ensure(fc, 0x00490420, 3, "engine_00490420", convention="thiscall", this=True)
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,                 # Skater_ProfileValue: a stat
           result=lambda a: rng.choice([0, 1, 3, 5, 5, 7, 10, 10, 0x1F4, 0xFFFFFFFF, 0x7FFFFFFF, rng.randrange(0, 11)]))
    ensure(fc, 0x004AD620, 3, "engine_004ad620")


def install_stubs_gab(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gab_installed", False):
        return
    fc._gab_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004900B0, 2, "engine_004900b0", convention="thiscall", this=True)    # CBruce__StateTransition(state, source line)
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)    # Animation_RequestStart(animation, source line)
    ensure(fc, 0x0048F5F0, 0, "engine_0048f5f0", convention="thiscall", this=True)    # Trick_TakeOff
    ensure(fc, 0x004962F0, 0, "engine_004962f0", convention="thiscall", this=True)    # Skater_Nothing


def install_stubs_gub(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gub_installed", False):
        return
    fc._gub_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x004900B0, 2, "engine_004900b0", convention="thiscall", this=True)
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)
    ensure(fc, 0x00490450, 4, "engine_00490450", convention="thiscall", this=True)
    ensure(fc, 0x00490480, 5, "engine_00490480", convention="thiscall", this=True)
    ensure(fc, 0x00431260, 2, "engine_00431260", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.choice([0, 0x3F, 0x40, 0x41, 0x7F, rng.randrange(0, 0x80)]))


def install_stubs_gdp(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gdp_installed", False):
        return
    fc._gdp_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    for address, args in ((0x0048F5F0, 0), (0x0048F690, 4), (0x004900B0, 2), (0x004904D0, 2), (0x0049DAD0, 0), (0x00496550, 0), (0x00495CC0, 0), (0x0049D9C0, 0), (0x00497F40, 0), (0x00494210, 0),
                          (0x00499710, 0), (0x004993F0, 0), (0x004995D0, 0)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x00414C50, 1, "engine_00414c50")
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: rng.choice([0, 1, 0x1234, rng.getrandbits(32)]))
    ensure(fc, 0x004C5B00, 1, "engine_004c5b00")
    ensure(fc, 0x004AD620, 3, "engine_004ad620")


def install_stubs_ggp(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_ggp_installed", False):
        return
    fc._ggp_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.choice([0, 0x3F, 0x40, 0x41, 0x7F, rng.randrange(0, 0x80)]))


def install_stubs_gbr(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gbr_installed", False):
        return
    fc._gbr_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x004957C0, 3, "engine_004957c0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 1, 0x100, 0xFFFFFFFF]))
    ensure(fc, 0x0049B500, 3, "engine_0049b500", convention="thiscall", this=True)
    ensure(fc, 0x004CF850, 2, "engine_004cf850")


GOF_STATE = {"hit": 0}


def install_stubs_gof(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gof_installed", False):
        return
    fc._gof_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    slab = env.slab
    ensure(fc, 0x0049B500, 3, "engine_0049b500", convention="thiscall", this=True)
    ensure(fc, 0x0046D2E0, 4, "engine_0046d2e0", convention="thiscall", this=True)
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GPF_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, lambda a: rng.choice([0, 0, 1, 0x1234]), 0x68), (0, 4, lambda a: rng.getrandbits(32), 0x6C), (0, 4, lambda a: rng.choice([0, 0x1000, 0xFFFFF000, 0x1769, 0x176A, rng.getrandbits(32)]), 0x70),
                  (0, 4, lambda a: rng.getrandbits(32), 0x74), (0, 4, lambda a: slab + GOF_SURFACE, 0x80)])


def install_stubs_gic(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gic_installed", False):      # (also the gcs group's)
        return
    fc._gic_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()


def install_stubs_gzq(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gzq_installed", False):
        return
    fc._gzq_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004628F0, 2, "engine_004628f0")
    ensure(fc, 0x004638D0, 2, "engine_004638d0")
    ensure(fc, 0x00463D50, 1, "engine_00463d50")


def install_stubs_gcc(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gcc_installed", False):
        return
    fc._gcc_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004F4940, 4, "engine_004f4940")
    ensure(fc, 0x00462A20, 3, "engine_00462a20")


def install_stubs_gcp(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gcp_installed", False):
        return
    fc._gcp_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004F43E0, 4, "engine_004f43e0")
    ensure(fc, 0x00463E50, 2, "engine_00463e50")


def install_stubs_gcf(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gcf_installed", False):
        return
    fc._gcf_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004B3990, 1, "engine_004b3990", result=lambda a: env.rng.choice([0, 1]))
    ensure(fc, 0x004C4BD0, 2, "engine_004c4bd0")


GROUP_STUBS = {"gcf": install_stubs_gcf, "gcp": install_stubs_gcp,"gcc": install_stubs_gcc, "gcs": install_stubs_gic, "gzq": install_stubs_gzq, "gic": install_stubs_gic, "gof": install_stubs_gof,"ggp": install_stubs_ggp, "gbr": install_stubs_gbr, "gub": install_stubs_gub, "gdp": install_stubs_gdp, "gpu": install_stubs, "gpa": install_stubs_gpa, "gpo": install_stubs_gpo, "gsu": install_stubs_gsu, "gpf": install_stubs_gpf, "gas": install_stubs_gas, "guh": install_stubs_guh, "gua": install_stubs_gua, "gab": install_stubs_gab}


def word(rng):
    return rng.choice([0, 1, 2, 0x40, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x1000)])


def gpo_world(img, env, rng):
    """A chain of up to six list members (some objects of the slab stay out of it), the two player records and the globals the loops read; returns the first member's address (0 for an empty list)."""
    slab = env.slab
    count = rng.randrange(0, 7)
    order = list(range(6))
    rng.shuffle(order)
    members = order[:count]
    for k in range(GPO_OBJECTS):
        at = slab + k * GPO_STRIDE
        img.u32(at, slab + GPO_VTABLE)
        for axis in range(3):
            img.u32(at + 8 + 4 * axis, rng.choice([0, 0x100, 0xFFFFFF00, rng.randrange(0, 0x4000), rng.getrandbits(16), rng.getrandbits(32)]))
        img.u16(at + 0x68, rng.choice([0, 1, 2, 0xFFFF, rng.getrandbits(16)]))
        img.u16(at + 0x6A, rng.choice([0, 2, 0x40, 0xC0, 0x42, 0x80, 0x0A, 0x08, rng.getrandbits(16)]))
        img.u32(at + 0x20, 0)
    for index, member in enumerate(members):
        img.u32(slab + member * GPO_STRIDE + 0x20, slab + members[index + 1] * GPO_STRIDE if index + 1 < len(members) else 0)
    # the player records: the first and, for two players, its successor
    img.u32(0x0056A960, slab + 6 * GPO_STRIDE)
    img.u32(slab + 6 * GPO_STRIDE + 0x20, slab + 7 * GPO_STRIDE if rng.random() < 0.7 else 0)
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    img.u32(0x00535AF8, rng.choice([0, 0x400, 0x1000, 0x10000, 0x100000, word(rng)]))
    for k in range(4):
        img.u32(slab + GPO_VTABLE + 4 * k, GPO_DELETE if k == 0 else (GPO_UPDATE if k == 3 else rng.getrandbits(32)))
    first = slab + members[0] * GPO_STRIDE if members else 0
    img.u32(slab + GPO_HEAD, first)
    return first


@gpo(0x00480FD0, "Ob_ClearInputFlags")
def gen_clear_input_flags(env, n):
    img, rng = Image(), env.rng
    first = gpo_world(img, env, rng)
    img.u32(0x0056AF44, 0)
    return [first], img.pokes()


@gpo(0x00481110, "Ob_QuickAI")
def gen_quick_ai(env, n):
    img, rng = Image(), env.rng
    gpo_world(img, env, rng)
    img.u32(0x0056AF44, 0)
    return [env.slab + GPO_HEAD], img.pokes()


@gpo(0x00481150, "Ob_MaybeUnSuspendOrCull")
def gen_maybe_unsuspend(env, n):
    img, rng = Image(), env.rng
    first = gpo_world(img, env, rng)
    img.u32(0x0056AF44, first)
    return [], img.pokes()


def q12_vector(rng, img, at, unit=False):
    for k in range(3):
        if unit:
            value = rng.choice([0, 0, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1001)])
        else:
            value = rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x40000, 0x40000), rng.randrange(-0x400, 0x400), rng.getrandbits(32)])
        img.u32(at + 4 * k, value & 0xFFFFFFFF)


@spec(0x0049E680, "Skater_PhysicsFrame", "gpf", this=lambda env: env.slab)
def gen_physics_frame(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    noise = lambda count: bytes(rng.getrandbits(8) for _ in range(count))      # noqa: E731
    small = lambda: rng.choice([0, 0, 1, 2, 5, 0xFFFFFFFF, word(rng)])             # noqa: E731
    img.u32(0x0056A898, rng.choice([0, 1, 2, 3, 9, 9, 10, 11, 12, 13, 14]))
    img.u16(slab + 0x29F4, rng.choice([0x64, 0x64, 0, 1, 0x7FFF, 0x8000, rng.getrandbits(16)]))
    img.u32(slab + 0x30B8, rng.choice([0, 0, 0, 1, 1, 2, 2, 3, 5, 7, 7, 8]))
    img.u32(0x0056B7EC, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2DA4, rng.choice([0, 0, 1, 5, 6, 100, 0xFFFFFFFF]))
    for field in (0x2F6C, 0x2F70):
        img.u32(slab + field, rng.choice([0, 0x800, 0x1000, 0x1001, 0x1FFF, 0x2000, 0x10000, 0x80000000, rng.getrandbits(32)]))
    img.u32(0x0056A93C, rng.choice([0, 1, 0x100, 0x400, 0x800, rng.getrandbits(16)]))
    # the pad profile and the stick
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    for off in (0x80, 0x90, 0xA0):
        img.u8(slab + GPF_PROFILE + off, rng.choice([0, 0, 0, 1]))
    img.u8(slab + 0x31A2, rng.choice([0, 0xD8, 0xD7, 0x80, 0x7F, rng.getrandbits(8)]))
    img.u8(slab + 0x31A1, rng.choice([0, 0x31, 0x32, 0xCE, 0xCF, rng.getrandbits(8)]))
    img.u32(slab + 0x2D8C, rng.choice([0, 0, word(rng)]))
    img.u32(0x005685F4, rng.choice([0, 0xB4, 0xB5, rng.getrandbits(32)]))
    q12_vector(rng, img, slab + 0x4C)
    img.put(slab + 0x58, noise(12))
    img.put(slab + 0x29B8, noise(4))
    img.put(slab + 0x3204, noise(4))
    img.u16(slab + 0xF6, rng.choice([0x23, 0x24, 0x0B, 0x0C, 0x2E, 6, 7, 8, 9, 10, 0, 0x5D, rng.getrandbits(16)]))
    img.u16(slab + 0xF4, rng.choice([0, 1, 17, 19, 0x40, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    img.u8(slab + 0x107, rng.choice([0, 1]))
    img.u32(slab + 0x2DD8, rng.choice([0, 0, 1]))
    img.u32(slab + 0x2DC8, rng.choice([0, 0x10000, 0x40000, 0x7FFFFFFF, rng.getrandbits(32)]))
    img.u32(slab + 0x2DCC, rng.choice([0, 0, 1, 3]))
    img.put(slab + 0x2E58, noise(0x20))
    q12_vector(rng, img, slab + 0x08)
    q12_vector(rng, img, slab + 0xBC)
    img.u32(slab + 0x2F64, rng.choice([0, 1, 1]))
    img.u32(slab + 0x2CC0, rng.randrange(0, 8))
    img.u32(slab + 0x30B0, word(rng))
    img.u16(slab + 0x2C64, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 1]))
    img.put(slab + 0x2F14, noise(0x14))
    img.u32(0x00533F38, rng.choice([0, 1, 9, 9, 7]))
    img.u32(0x0056DB64, rng.choice([0, 2, 2]))
    img.u8(0x006A3B21, rng.choice([0, 1]))
    img.u32(0x0056A86C, rng.choice([0] * 8 + [1]))
    img.u32(0x00567C7C, word(rng))
    img.u32(0x00567C78, word(rng))
    # the wall vector, the basis rows and the lean
    if rng.random() < 0.5:
        q12_vector(rng, img, slab + 0x310C, unit=True)
    else:
        for k in range(3):
            img.u32(slab + 0x310C + 4 * k, 0)
    q12_vector(rng, img, slab + 0x30F4, unit=True)
    q12_vector(rng, img, slab + 0x3100, unit=True)
    img.u32(slab + 0x3130, word(rng))
    img.u32(slab + 0x3148, rng.choice([0, 0, 1, 0xFFFFFFFF, word(rng)]))
    img.u32(slab + 0x2E90, rng.choice([0, 0, 1]))
    img.u32(slab + 0x30C4, rng.choice([0, 0, 1]))
    for field in (0x2F38, 0x2F40, 0x2DE4, 0x2CAC, 0x2CB0, 0x2CB4, 0x2F44, 0x2F48, 0x2F4C, 0x3200, 0x30C0, 0x2DA8, 0x2DAC, 0x2DB0, 0x2F3C):
        img.u32(slab + field, small())
    img.u32(0x0056E320, word(rng))
    img.u32(0x0056865C, rng.choice([0x100, 0x1000, 1, rng.randrange(0, 0x2000)]))
    img.put(0x0056B7C0, noise(12))
    GPF_STATE["body"] = rng.choice([0, 0, 1, 0x1234])
    GPF_STATE["w78"] = rng.getrandbits(32)
    GPF_STATE["w7c"] = rng.getrandbits(32)
    GPF_STATE["distance"] = rng.choice([0, 0x3B, 0x3C, 0x3D, rng.randrange(-0x100, 0x100) & 0xFFFFFFFF, rng.getrandbits(16)])
    # the narrow paths, built on purpose now and then: the lean's distance query (a standing skater in the animations 6, 7, 9, 10 with a lean and a hit), the fall stamp (the wall vector's y at
    # the limit -0xed8) and the blood frame of animation 0x2e (the frame the character's entry of the table at 0x5347b0 names)
    path = rng.random()
    if path < 0.3:
        img.u32(slab + 0x30B8, rng.choice([0, 7]))
        img.u32(0x0056A86C, 0)
        img.u32(slab + 0x3148, rng.choice([1, 0xFFFFFFFF, word(rng) | 1]))
        img.u16(slab + 0xF6, rng.choice([6, 7, 9, 10]))
        GPF_STATE["body"] = rng.choice([1, 0x1234])
        GPF_STATE["distance"] = rng.choice([0x3B, 0x3C, 0x3C, 0x3D, 0, rng.randrange(0, 0x80)])
    elif path < 0.45:
        img.u32(slab + 0x310C + 4, rng.choice([0xFFFFF128, 0xFFFFF127, 0xFFFFF129]))
    elif path < 0.6:
        character = rng.randrange(0, 3)
        img.u32(slab + 0x2F64, 1)
        img.u16(slab + 0xF6, 0x2E)
        img.u32(slab + 0x2CC0, character)
        img.u16(slab + 0xF4, rng.choice([[19, 0xFFFF, 17][character], 0xFF00, 0x1300, 17]))
    return [], img.pokes()


@spec(0x0046DD60, "Skater_Update", "gsu", this=lambda env: env.slab)
def gen_skater_update(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    noise = lambda count: bytes(rng.getrandbits(8) for _ in range(count))      # noqa: E731
    img.u32(0x00561C04, rng.choice([0] * 9 + [1]))
    region = rng.randrange(3)
    img.u8(slab + 0x1F, region)
    for k in range(3):
        img.u8(0x0056D444 + 0x44 * k + 0x23, rng.choice([0, 1, 1, 2]))
    img.put(slab + 0x24, noise(0x0C))
    img.put(slab + 0x08, noise(0x0C))
    img.put(slab + 0xBC, noise(0x0C))
    img.put(slab + 0x90, noise(0x0C))
    clock = rng.choice([0, 5, 100, 0xFFFFFFF0, rng.getrandbits(32)])
    img.u32(0x005685F4, clock)
    for stamp in (0x2E30, 0x2E34):
        img.u32(slab + stamp, rng.choice([0, 0, (clock - rng.randrange(0, 20)) & 0xFFFFFFFF, (clock - 10) & 0xFFFFFFFF, (clock - 11) & 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(slab + 0x2CC8, word(rng))
    img.u32(0x0055FA38, word(rng))
    img.u32(0x0056A958, word(rng))
    img.u32(0x0056A858, slab if rng.random() < 0.5 else rng.choice([0, slab + 0x100, word(rng)]))
    img.u32(0x0056A85C, word(rng))
    for record in (0x0056E5D8, 0x0056E450):
        img.u32(record, rng.choice([0, 1, 1, 1, 2, 3, 0xFFFFFFFF]))
        img.u32(record + 4, rng.choice([0, 1, word(rng), word(rng)]))
    img.u8(slab + 0xB2, rng.getrandbits(8))
    img.u16(slab + 0x31EC, rng.choice([0, 1, 0xFFFF, rng.getrandbits(16)]))
    img.u8(slab + 0x305C, rng.choice([0, 0, 1, 2, 0xFF]))
    img.u32(slab + 0x30B8, rng.choice([0, 0, 0, 1, 2, 5]))
    height = rng.getrandbits(32)
    img.u32(slab + 0x0C, height)
    img.u32(slab + 0x3080, (height - rng.choice([0, 0x40, -0x40, 0x41, -0x41, 1, rng.randrange(-0x100, 0x100), 0x80000000, rng.getrandbits(32)])) & 0xFFFFFFFF)
    img.u16(slab + 0xB4, rng.choice([0, 1, 0xFFFF, 0x8000, 0x7FFF, rng.getrandbits(16)]))
    img.u16(slab + 0xF6, rng.choice([0x5D, 0x5D, 0x5E, 0x5E, 0, 0x5F]))
    img.u16(slab + 0xF4, rng.choice([0, 0xF, 0x10, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)]))
    for flag in (0x320C, 0x3208):
        img.u32(slab + flag, rng.choice([0, 0, 1, word(rng)]))
    img.u32(slab + 0x30B0, word(rng))
    img.u32(0x0055F6BC, word(rng))
    img.u32(0x0056B830, word(rng))
    for hit in GSU_HITS:
        img.u16(slab + hit + 0xF0, rng.choice([0xFFFF, 0xFFFF, 0, 0xFFFE]))
    return [], img.pokes()


@gpa(0x00469A30, "Skater_UpdateAnimationObjects")
def gen_update_animation_objects(env, n):
    img, rng = Image(), env.rng
    slab = env.slab

    def waiting():
        return rng.choice([(0, 0), (7, 0), (7, 1), (7, 0)])

    second = rng.random() < 0.6
    states = [waiting(), waiting()]
    if rng.random() < 0.2:
        broken = rng.randrange(2) if second else 0
        states[broken] = rng.choice([(0, 1), (1, 0), (3, 1), (5, 0), (6, 0), (0xFFFFFFFF, 0)])
    img.u32(0x00533F38, rng.choice([0, 1, 2, 5, 7, 7, 8, 9, 10]))
    img.u32(0x00568658, rng.choice([1, 1, 1, 1, 0, 2]))
    img.u32(0x00568818, rng.choice([1, 1, 1, 2, 0, 0xFFFFFFFF, 0x3B, 0x3C, 0x3D, 0x80000000, rng.randrange(0, 0x80), word(rng)]))
    img.u32(0x00568610, word(rng))
    img.u32(0x0056A858, slab + P0)
    img.u32(0x0056A85C, slab + P1 if second else 0)
    for k, base in enumerate((P0, P1)):
        state, flag = states[k]
        img.u32(slab + base + 0x30B8, state)
        img.u32(slab + base + 0x2DD4, flag)
        img.u16(slab + base + 0xF6, rng.choice([0, 0, 0x5D, 0x5F, 0x5D, 0x5E, 1, rng.getrandbits(16)]))
        img.u8(slab + base + 0x107, rng.choice([0, 1, 1, 0x80]))
        img.u32(slab + base + 0x16C, word(rng))
        img.u32(slab + base + 0x2A8, rng.choice([0, 0, 5, 0xFFFFFFFF, word(rng)]))
        img.u32(slab + base + 0x308C, word(rng))
        img.u32(slab + base + 0x3090, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 5, word(rng)]))
        img.u32(slab + base + 0x3094, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 7, word(rng)]))
    # the pad bytes the countdown looks at (the three pad-history buffers): mostly clear, now and then set
    img.put(0x0056AFF8, bytes(rng.choice([0, 0, 0, 0, 1, 0xFF, rng.getrandbits(8)]) for _ in range(0x2D8)))
    return [], img.pokes()


@gpu(0x00469DE0, "Game_GameplayUpdate")
def gen_gameplay_update(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    STATE["playing"] = rng.choice([0, 0, 1])
    STATE["dying"] = rng.choice([0, 0, 0, 1, 0x100, 0xFFFFFFFF])
    # the level-start countdown is a narrow path: a share of the cases is built to reach it, one gate of it broken now and then
    countdown = rng.random() < 0.4
    mode = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 8, 9, 9, 10])
    latch = rng.choice([0, 1, 1, 1, 2])
    frozen = rng.choice([0, 0, 0, 0, 1])
    debug = rng.choice([0] * 6 + [1])
    step_limit = rng.choice([0, 1, 0x27, 0x28, 0x29, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, rng.randrange(0, 0x80)])
    second = rng.random() < 0.65
    states = [(rng.choice([0, 7, 7, 0, 1, 3, 5]), rng.choice([0, 0, 0, 1])) for _ in range(2)]
    STATE["freeze"] = rng.choice([0, 0, 0, 0, 0, 1])
    if countdown:
        mode = rng.choice([0, 1, 2, 5, 8, 9, 10])
        latch, frozen, debug, STATE["freeze"] = 1, 0, 0, 0
        step_limit = rng.choice([0, 1, 0x27, 0x10, rng.randrange(0, 0x28)])
        states = [(rng.choice([0, 7]), 0) for _ in range(2)]
        if rng.random() < 0.35:
            broken = rng.choice(["mode", "latch", "limit", "frozen", "first", "second", "freeze"])
            if broken == "mode":
                mode = 7
            elif broken == "latch":
                latch = rng.choice([0, 2])
            elif broken == "limit":
                step_limit = rng.choice([0x28, 0x29, 0x7FFFFFFF])
            elif broken == "frozen":
                frozen = 1
            elif broken == "first":
                states[0] = (rng.choice([1, 3, 0]), 1 if rng.random() < 0.5 else 0)
                if states[0][0] != 0:
                    states[0] = (states[0][0], rng.choice([0, 1]))
                else:
                    states[0] = (0, 1)
            elif broken == "second":
                states[1] = (rng.choice([1, 5]), 0)
                second = True
            else:
                STATE["freeze"] = 1
    img.u32(0x00533F38, mode)
    img.u32(0x00533F3C, rng.choice([1, 2, 2]))
    img.u32(0x00568814, rng.choice([0, 1, 1]))
    img.u32(0x0056A900, debug)
    img.u32(0x0056A8D4, rng.choice([0, 1, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(0x00561C04, frozen)
    img.u32(0x0056A8E0, rng.choice([0] * 6 + [1]) if not countdown else 0)
    img.u32(0x00568658, latch)
    img.u32(0x00568818, step_limit)
    img.u32(0x00568610, word(rng))
    # the players and their cameras (two skaters may share one camera, the word then takes both additions)
    img.u32(0x0056A858, slab + P0)
    img.u32(0x0056A85C, slab + P1 if second else 0)
    shared = rng.random() < 0.25
    for k, base in enumerate((P0, P1)):
        state, flag = states[k]
        img.u32(slab + base + 0x30B8, state)
        img.u32(slab + base + 0x2DD4, flag)
        img.u32(slab + base + 0x29B0, slab + (CAM0 if (k == 0 or shared) else CAM1))
    for base in (CAM0, CAM1):
        img.u32(slab + base + 0x5B4, word(rng))
    # the suspended objects (the frozen world's virtual update): a chain of up to four, some with an animation selected
    count = rng.randrange(0, 5)
    img.u32(0x0056B830, slab + OBJ if count else 0)
    for k in range(count):
        at = slab + OBJ + OBJ_BYTES * k
        img.u32(at, slab + VTABLE)
        img.u32(at + 0x20, at + OBJ_BYTES if k + 1 < count else 0)
        img.u16(at + 0xF6, rng.choice([0, 0, 1, 0xFFFF, 0x100, rng.getrandbits(16)]))
    for slot in range(8):
        img.u32(slab + VTABLE + 4 * slot, SLOT if slot == 3 else rng.getrandbits(32))
    img.u32(0x0055F6BC, word(rng))
    img.u32(0x0056AF40, word(rng))
    img.u32(0x0056A960, rng.choice([slab + VTABLE, 0x12345678, word(rng)]))
    # the three pad-history buffers, the latches of the competition modes, the render state CalcPolyBufferEnd reads
    img.put(0x0056AFF8, bytes(rng.getrandbits(8) for _ in range(0x2D8)))
    img.put(0x00568698, bytes(rng.getrandbits(8) for _ in range(0x16C)))
    img.u32(0x0056A854, rng.choice([0, 1, 1, 2]))
    img.u32(0x00568630, rng.choice([0, 0, 1]))
    img.u32(0x0056A8C0, rng.choice([0, 0, 1]))
    img.u32(0x00568618, rng.choice([0, 0, 1]))
    img.u32(0x00561BFC, rng.choice([0, 0, 1]))
    img.u32(0x0056A8D0, word(rng))
    img.u32(0x0056E30C, word(rng))
    img.put(slab + RENDER, bytes(rng.getrandbits(8) for _ in range(0xC0)))
    img.u32(0x00560FD4, slab + RENDER)
    img.u32(0x00561150, word(rng))
    return [], img.pokes()


@spec(0x00493370, "Skater_ActionPhysicsStep", "gas", this=lambda env: env.slab)
def gen_action_step(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    noise = lambda count: bytes(rng.getrandbits(8) for _ in range(count))      # noqa: E731
    edge = lambda: rng.choice([0, 0, 1, 0xFFFFFFFF, 0x7FF, 0x800, 0x801, 0xFFFFF800, 0xFFFFF7FF, 0x2D000, 0x5A000, 0xA0000, 0xFFFD3000, 0xFFF60000, rng.randrange(0, 0x3000),   # noqa: E731
                                 rng.randrange(0, 0x100000), (-rng.randrange(0, 0x100000)) & 0xFFFFFFFF, rng.getrandbits(32)])
    img.u32(0x0056865C, rng.choice([0x100, 0x100, 0x1000, 1, 0x80, rng.randrange(0, 0x2000), rng.getrandbits(32)]))
    img.u32(0x005685F4, rng.choice([0, 8, 9, 100, 0xB4, rng.getrandbits(8), rng.getrandbits(32)]))
    img.u32(0x0056A88C, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056B7F0, rng.choice([0, 0, 0, 1, 1]))
    img.put(0x0056B038, noise(0x24) if rng.random() < 0.1 else bytes(0x24))
    img.u32(slab + 0x30B8, rng.choice([0, 0, 0, 1, 1, 1, 2, 2, 3, 5, 7, 7, 8]))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    byte = lambda: rng.choice([0, 0, 0, 1, 1, 2, 0xFF, rng.getrandbits(8)])      # noqa: E731
    for off in (0x10, 0x20, 0x40, 0x51, 0x60, 0x71, 0x80, 0x90, 0xA0, 0xB0):
        img.u8(slab + GPF_PROFILE + off, byte() if rng.random() < 0.5 else 0)
    img.u8(slab + 0x29B7, rng.choice([0, 1, 2, 3, 0xFF]))
    stick = [0, 0x13, 0x14, 0x19, 0x1A, 0x1B, 0x7F, 0x80, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xFF, 0x31, 0xCE]
    img.u8(slab + 0x31A1, rng.choice(stick + [rng.getrandbits(8)]))
    img.u8(slab + 0x31A2, rng.choice([0, 0x1E, 0x1F, 0x1D, 0x28, 0x29, 0x27, 0xD8, 0xD7, 0xD9, 0x7F, 0x80, rng.getrandbits(8)]))
    img.u32(slab + 0x2C80, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2C84, rng.choice([0, 0, 1]))
    img.u32(slab + 0x2E80, rng.choice([0, 0, 0, 1, 0xFFFFFFFF, 2, 0xFFFFFFFE, rng.getrandbits(32)]))
    img.u32(slab + 0x3068, rng.choice([0, 0x700, 0x7FF, 0x800, 0xFFFFF801, 0xFFFFF800, 0xFFFFF7FF, 0x100, 0xFFFFFF00, rng.getrandbits(32)]))
    img.u32(slab + 0x306C, rng.getrandbits(4))
    img.u32(slab + 0x3074, rng.choice([0, 1]))
    img.u32(slab + 0x3060, rng.choice([0, 0, 1, 90, 99, 92, rng.getrandbits(8), rng.getrandbits(32)]))
    img.u32(slab + 0x30A4, rng.choice([0, 5, rng.getrandbits(32)]))
    for field in (0x3144, 0x3148):
        img.u32(slab + field, edge())
    img.u32(slab + 0x2E78, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(slab + 0x2E7C, rng.getrandbits(32))
    img.u32(slab + 0x2E84, rng.getrandbits(32))
    # the queued turn: three amounts left and the rates that spend them
    for k in range(3):
        img.u32(slab + 0x2CA0 + 4 * k, rng.choice([0, 0, 0, 1, 0x100, 0x1000, 0xFFFFFFFF, edge()]))
        img.u32(slab + 0x2C94 + 4 * k, rng.choice([0, 1, 0xFFFFFFFF, 0x80, 0xFFFFFF80, 0x100, 0x400, 0xFFFFFC00, edge()]))
        img.u32(slab + 0x2CAC + 4 * k, rng.getrandbits(32))
    if rng.random() < 0.15:
        for k in range(3):
            img.u32(slab + 0x2CA0 + 4 * k, 0)
    img.put(slab + 0x2E58, noise(0x20))
    q12_vector(rng, img, slab + 0x08)
    q12_vector(rng, img, slab + 0x4C)
    img.put(slab + 0x58, noise(12))
    q12_vector(rng, img, slab + 0x30F4, unit=True)
    # the camera
    if rng.random() < 0.5:
        img.u32(slab + 0x29B0, slab + GAS_CAMERA)
        q12_vector(rng, img, slab + GAS_CAMERA + 8)
    else:
        img.u32(slab + 0x29B0, 0)
    # the narrow paths: the analog stick alone, the quiet stick with no input, the wide limit
    path = rng.random()
    if path < 0.3:
        for off in (0x40, 0x60, 0x80, 0x90):
            img.u8(slab + GPF_PROFILE + off, 0)
        img.u8(slab + 0x31A1, rng.choice([0x1A, 0x40, 0x7F, 0x80, 0xBF, 0xE6, 0xFF, 0x31, 0xCE, rng.getrandbits(8)]))
    elif path < 0.45:
        for off in (0x40, 0x60, 0x80, 0x90):
            img.u8(slab + GPF_PROFILE + off, 0)
        img.u8(slab + 0x31A1, rng.choice([0, 1, 0x10, 0xF0, 0x19, 0xE7]))
    elif path < 0.6:
        # the lean word's edges: state 1, no buttons, the stick's second byte at the limits
        img.u32(slab + 0x30B8, 1)
        img.u32(0x0056B7F0, 0)
        img.u32(slab + 0x2E80, 0)
        for off in (0x10, 0x20, 0xA0, 0xB0):
            img.u8(slab + GPF_PROFILE + off, 0)
        img.u8(slab + 0x31A2, rng.choice([0x28, 0x29, 0x27, 0xD8, 0xD7, 0xD9]))
    elif path < 0.75:
        # the spin angle at the turn limit (one frame of 150 at the normal frame scale)
        img.u32(0x0056865C, 0x100)
        img.u32(slab + 0x30B8, rng.choice([1, 2]))
        img.u32(slab + 0x2C80, 1)
        count = rng.choice([1, 0xFFFFFFFF])
        img.u32(slab + 0x2E80, count)
        img.u32(slab + 0x3068, rng.choice([0x76A, 0x769, 0x76B, 0xFFFFF896, 0xFFFFF895, 0xFFFFF897]) if count == 1 else rng.choice([0xFFFFF896, 0xFFFFF895, 0xFFFFF897, 0x76A]))
        img.u32(slab + 0x2C84, rng.choice([0, 1]))
    elif path < 0.9:
        # the dead zone: no input, the accumulator one decay from the edge of the zone
        img.u32(slab + 0x30B8, 3)
        img.u32(0x0056B7F0, 0)
        img.u32(slab + 0x2C80, 0)
        for off in (0x40, 0x60, 0x80, 0x90):
            img.u8(slab + GPF_PROFILE + off, 0)
        img.u8(slab + 0x31A1, rng.choice([0x15, 0x16, 0x18, 0xEB, 0xEA]))
        goal = rng.choice([-0x800, -0x801, 0x7FF, 0x800, -0x7FF, 0x801])
        candidates = [x for x in range(-0x2000, 0x2001) if x - (x >> 2) == goal]
        if candidates:
            img.u32(slab + 0x3144, rng.choice(candidates) & 0xFFFFFFFF)
    return [], img.pokes()


@spec(0x00492F20, "Skater_UpdateHeading", "guh", this=lambda env: env.slab)
def gen_update_heading(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    wide_byte = rng.choice([0, 0, 1, 0xFF])
    speed = rng.choice([0, 0x1E, 0x1F, 0x1D, 0x7F, 0x80, rng.getrandbits(8)])
    img.u8(slab + GPF_PROFILE + 0xB0, wide_byte)
    img.u8(slab + 0x31A2, speed)
    img.u8(slab + 0xD8, rng.choice([0, 2, 2, 3, 1, rng.getrandbits(8)]))
    img.u32(slab + 0x2DD8, rng.choice([0, 0, 1]))
    img.u16(slab + 0xF6, rng.choice([0, 6, 6, 7, 7, 8, 9, 9, 10, 10, 5, 11, 0x10007, rng.getrandbits(16)]) & 0xFFFF)
    turn = rng.choice([0, 0, 1, 0xFFFFFFFF, 0x1000, 0xFFFFF000, 0x2D000, 0xFFFD3000, 0x5A000, 0xFFFA6000, 0x2C000, 0xFFFD4000, 0x10000, 0xFFFF0000,
                       rng.randrange(0, 0x60000), (-rng.randrange(0, 0x60000)) & 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)])
    img.u32(slab + 0x3148, turn)
    # the frame near the target, so that the stepper's three step sizes and the frame limits (0xc, 0xf) are met
    signed = turn - (1 << 32) if turn >= (1 << 31) else turn
    limit = 0x5A000 if (wide_byte != 0 or (speed - 256 if speed >= 128 else speed) > 0x1E) else 0x2D000
    base = rng.choice([22, 22, 15, 12])
    target = min(abs(base * signed) // limit, {22: 0x16, 15: 0xF, 12: 0xC}[base]) if abs(signed) < (1 << 24) else 0
    frame = target + rng.choice([-14, -13, -12, -5, -4, -3, -2, -1, 0, 0, 1, 2, 3, 4, 12, 13, 14]) if rng.random() < 0.75 else rng.getrandbits(16)
    img.u16(slab + 0xF4, frame & 0xFFFF)
    return [], img.pokes()


@spec(0x0049B010, "Skater_UpdateAcceleration", "gua", this=lambda env: env.slab)
def gen_update_acceleration(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    noise = lambda count: bytes(rng.getrandbits(8) for _ in range(count))      # noqa: E731
    img.u32(0x00533F38, rng.choice([0, 7, 7, 9]))
    img.u32(0x0056A854, rng.choice([0, 1, 2, 3]))
    for k in range(8):
        img.u32(0x0056A3D8 + 4 * k, rng.choice([0, 0, 0, 1, 5, 0xFFFFFFFF]))
    img.u32(slab + 0x2CC4, rng.randrange(0, 8))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 0, 0, 1]))
    img.u32(slab + 0x2DF8, rng.choice([0, 0, 0, 0, 1]))
    img.u8(slab + 0x107, rng.choice([0, 0, 1]))
    img.u16(slab + 0xF6, rng.choice([0, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 0x5E, 0x5E, 0x5F, rng.getrandbits(16)]))
    img.u16(slab + 0xF4, rng.choice([0, 0xA, 0xB, 0xF, 0x10, 0x13, 0x14, 0x15, 5, 0xFFFF, rng.getrandbits(16)]))
    img.u32(slab + 0x108, rng.getrandbits(32))
    img.u32(slab + 0x30B8, rng.choice([0] * 8 + [1, 2]))
    img.u32(slab + 0x2F2C, rng.choice([0, 0, 0, 1, 2, 5, 0xFFFFFFFF]))
    img.u32(slab + 0x2E78, rng.choice([0, 0, 1]))
    img.u32(slab + 0x2E7C, rng.choice([0, 0, 1]))
    img.u32(slab + 0x2DD8, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x30A8, rng.choice([0, 1, 1]))
    img.u32(slab + 0x30AC, rng.getrandbits(32))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    img.u8(slab + GPF_PROFILE + 0x10, rng.choice([0, 1, 1]))
    q12_vector(rng, img, slab + 0x30F4, unit=True)
    img.u32(slab + 0x30F8, rng.choice([0x1F3, 0x1F4, 0x1F5, 0, 0xFFFFFFFF, 0x1000, rng.getrandbits(32)]))
    img.put(slab + 0x58, noise(12))
    # the velocity: along one axis in whole steps of 64 (the speed is then the value), or anything
    if rng.random() < 0.6:
        value = rng.choice([0, 64, 1216, 1280, 19968, 20032, 20000, rng.randrange(0, 0x20000)]) * rng.choice([1, 1, -1])
        axis = rng.randrange(0, 3)
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, (value if k == axis else 0) & 0xFFFFFFFF)
        speed = (int((abs(value) * abs(value) // 4096) ** 0.5)) * 64
        img.u32(slab + 0x2DC8, rng.choice([speed - 1, speed, speed + 1, 2 * speed, 2 * speed + 1, 2 * speed - 1, 2 * speed + 2, 0x4E20, rng.getrandbits(17)]) & 0xFFFFFFFF)
        # a normal along the vertical axis, and a slope vector that makes the slope sit at the edge of the steep limit
        img.u32(slab + 0x3128, 0x10000000)
        img.u32(slab + 0x312C, 0)
        edge = rng.choice([0x1000, -0x1000, 0, 0x800])
        for k in range(3):
            img.u32(slab + 0x3118 + 4 * k, (edge if k == axis else 0) & 0xFFFFFFFF)
        if rng.random() < 0.3 and value:
            sign = 1 if value > 0 else -1
            img.u32(slab + 0x3118 + 4 * axis, (-0x1000 * sign) & 0xFFFFFFFF)
            img.u32(slab + 0x4C + 4 * axis, (sign * rng.choice([1227, 1228, 1229, 1216, 1280])) & 0xFFFFFFFF)
    else:
        q12_vector(rng, img, slab + 0x4C)
        q12_vector(rng, img, slab + 0x3118, unit=True)
        img.u32(slab + 0x3128, rng.getrandbits(32))
        img.u32(slab + 0x312C, rng.choice([0, 0x1000, 0xFFFFF000, rng.getrandbits(16), rng.getrandbits(32)]))
        img.u32(slab + 0x2DC8, rng.choice([0, 1, 0x1000, 0x20000, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(18)]))
    path = rng.random()
    if path < 0.5:
        # the jump button down, nothing in the slot table, the skater on the ground and moving along an axis (the speed is exact)
        img.u32(0x00533F38, 0)
        for k in range(8):
            img.u32(0x0056A3D8 + 4 * k, 0)
        img.u8(slab + GPF_PROFILE + 0x10, 1)
        for off in (0x2DD4, 0x2DF8, 0x2DD8, 0x30B8, 0x2E78):
            img.u32(slab + off, 0)
        axis = rng.randrange(0, 3)
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, 0)
        img.u32(slab + 0x3128, 0x10000000)
        img.u32(slab + 0x312C, 0)
        for k in range(3):
            img.u32(slab + 0x3118 + 4 * k, 0)
        if path < 0.25:
            # the slope at the edge of the steep limit: velocity 1227..1229 against a slope vector of -1.0
            img.u32(slab + 0x4C + 4 * axis, rng.choice([1227, 1228, 1229]))
            img.u32(slab + 0x3118 + 4 * axis, 0xFFFFF000)
            img.u32(slab + 0x2F2C, rng.choice([0, 1]))
            img.u16(slab + 0xF6, rng.choice([0, 5, 6, 7]))
            img.u32(slab + 0x2DC8, rng.choice([2431, 2432, 2433, 2434, 3000]))
        else:
            # the animations that push, with the speed at the push limit and the frame at its edges
            value = rng.choice([64, 640, 1280, 6400, 19968, 20032])
            img.u32(slab + 0x4C + 4 * axis, value)
            img.u32(slab + 0x2DC8, rng.choice([value - 1, value, value + 1, value + 64]))
            img.u16(slab + 0xF6, rng.choice([2, 3, 0x5E, 0x5E, 4, 1]))
            img.u16(slab + 0xF4, rng.choice([0xA, 0xB, 0xC, 0xF, 0x10, 0x11, 0x13, 0x14, 0x15]))
            img.u32(slab + 0x2F2C, rng.choice([3, 3, 0]))
            img.u32(slab + 0x30A8, rng.choice([0, 1]))
            img.u32(slab + 0x3118 + 4 * axis, rng.choice([0, 0x1000, 0xFFFFF000]))
    return [], img.pokes()


def trk_stubs(*targets):
    def install(fc, env):
        from core_specs_main import lock_scheduler
        from platform_specs import mark_under_test, stop_clock

        if getattr(fc, "_trk_installed", False):
            return
        fc._trk_installed = True
        mark_under_test()
        stop_clock(fc)
        lock_scheduler()
        for address, args in targets:
            ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    return install


GROUP_STUBS.update({"tri": trk_stubs(), "tmh": trk_stubs(), "tcr": trk_stubs(), "trs": trk_stubs(), "tdp": trk_stubs(), "tsp": trk_stubs(),
                    "tqs": trk_stubs((0x00497BB0, 2)), "tss": trk_stubs((0x00497BB0, 2)),
                    "tup": trk_stubs((0x00492190, 0), (0x004925E0, 0), (0x00492400, 0), (0x004BE450, 0))})

TRK_BUTTONS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xE, 0x10]


def trk_world(img, rng, slab, clock=None):
    """The skater's pad history and the globals the trick code reads."""
    clock = rng.choice([1000, 5000, 100000, rng.randrange(300, 200000), 0x7FFFFFF0, 0xFFFFFF00]) if clock is None else clock
    img.u32(0x005685F4, clock)
    img.u32(slab + 0x2B14, rng.randrange(0, 32))
    for k in range(32):
        at = slab + 0x2A14 + 8 * k
        img.u8(at, rng.choice([0, 0, 0] + TRK_BUTTONS))
        img.u8(at + 1, rng.choice([0, 1, 1]))
        img.u32(at + 4, (clock - rng.choice([0, 1, 5, 20, 60, 100, 0x200, 0x3FF, 0x400, rng.randrange(0, 0x800)])) & 0xFFFFFFFF)
    for b in range(0x11):
        img.u8(slab + 0x2B18 + b, rng.choice([0, 1]))
    img.u32(slab + 0x30B8, rng.choice([1, 1, 1, 2, 3, 0, 7, 2]))
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2F70, rng.choice([0, 0x800, 0x1000, 0x10000, rng.getrandbits(32)]))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    for off in range(0, 0xC0, 0x10):
        img.u8(slab + GPF_PROFILE + off, rng.choice([0, 0, 1]))
    stick = [0, 0x28, 0x29, 0x27, 0xD8, 0xD7, 0xD9, 0x7F, 0x80, rng.getrandbits(8)]
    img.u8(slab + 0x31A1, rng.choice(stick))
    img.u8(slab + 0x31A2, rng.choice(stick))
    img.u32(0x0056A858, slab if rng.random() < 0.6 else 0x12345678)
    img.u32(0x0056A894, rng.getrandbits(32))
    img.u32(0x00568658, rng.choice([0, 1]))
    return clock


def trk_table(img, rng, slab):
    """The generated trick table, [count][codes][script offset][flags]... ended by 0; two pointers (one may be the empty table)."""
    at = slab + TRK_TABLE
    entries = []
    for _ in range(rng.randrange(1, 6)):
        count = rng.choice([1, 2, 2, 2, 3, 4, 5])
        codes = [rng.choice(TRK_BUTTONS) for _ in range(count)]
        if count == 2 and rng.random() < 0.6:
            codes = [rng.choice(range(1, 9)), rng.choice([9, 0xA, 0xB, 0xC, 0xE, 0x10])]
        elif count == 2 and rng.random() < 0.3:
            codes = [rng.choice(range(1, 9)), rng.choice(range(1, 9))]
        flags = rng.choice([0, 0x14, 0x3C, 0x1E0, 0x3FF, 0xB4]) | rng.choice([0, 0, 0x400, 0x2000, 0x4000, 0x8000, 0x6000])
        entries.append((count, codes, flags & 0xFFFF))
    if rng.random() < 0.2:
        # an entry that starts at once and stays in the history, twice more: the later ones match again
        count, codes, flags = rng.choice(entries)
        entries = [(c, k, (f | 0x2000) if k == codes else f) for c, k, f in entries]
        for _ in range(2):
            entries.append((count, codes, flags | 0x2000))
    if rng.random() < 0.08:
        entries.append((12, [rng.choice(TRK_BUTTONS) for _ in range(12)], rng.choice([0x3FF, 0x23FF])))
    words = []
    for count, codes, flags in entries:
        words += [count] + codes + [rng.randrange(-0x8000, 0x8000) & 0xFFFF, flags]
    words.append(0)
    for i, w in enumerate(words):
        img.u16(at + 2 * i, w & 0xFFFF)
    img.u32(0x00568A50, slab + TRK_TABLE)
    img.u32(0x00568A4C, slab + (TRK_EMPTY if rng.random() < 0.3 else TRK_TABLE))
    img.u16(slab + TRK_EMPTY, 0)
    return entries


def trk_history_for(img, rng, slab, entry, clock):
    """Write an entry's codes into the history, newest last, pressed, as a player would have pressed them (or not quite: reversed, or the last two swapped)."""
    count, codes, flags = entry
    write = rng.randrange(0, 32)
    img.u32(slab + 0x2B14, write)
    index = (write - 1) % 32
    sequence = codes[::-1]
    mode = rng.random()
    if mode < 0.2:
        sequence = list(codes)
    elif mode < 0.4 and len(sequence) >= 2:
        sequence[0], sequence[1] = sequence[1], sequence[0]
    age = rng.choice([0, 0, 0, 0x1FF, 0x200, 0x3E0, 0x2F0])
    for i, code in enumerate(sequence):
        at = slab + 0x2A14 + 8 * index
        img.u8(at, code)
        img.u8(at + 1, 1)
        img.u32(at + 4, (clock - age - 2 * i - rng.choice([0, 1, 3])) & 0xFFFFFFFF)
        index = (index - 1) % 32
        if rng.random() < 0.15:
            img.u8(slab + 0x2A14 + 8 * index, 0)
            index = (index - 1) % 32


@spec(0x00492190, "TrickInput_RefreshHistory", "tri", this=lambda env: env.slab)
def gen_trick_refresh(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    trk_world(img, rng, slab)
    img.u32(slab + 0x2B14, rng.randrange(0, 32))
    for off in range(0, 0xC0, 0x10):
        img.u8(slab + GPF_PROFILE + off, rng.choice([0, 0, 1, 1, 1, 2] if rng.random() < 0.1 else [0, 1]))
    return [], img.pokes()


@spec(0x00492120, "TrickInput_DerivePattern", "tdp", this=lambda env: env.slab)
def gen_trick_pattern(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    trk_world(img, rng, slab)
    return [], img.pokes()


@spec(0x00491C90, "TrickInput_RecordState", "trs", this=lambda env: env.slab)
def gen_trick_record(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    trk_world(img, rng, slab)
    button = rng.choice(TRK_BUTTONS + [0, 0x10, 0x11, 0x12, 0xFF, 0x100, 0x10C, rng.getrandbits(8)])
    value = rng.choice([0, 1, 1, 0, 2, 0xFF, 0x100, 0x101, rng.getrandbits(8)])
    if (button & 0xFF) <= 0x10:
        img.u8(slab + 0x2B18 + (button & 0xFF), rng.choice([value & 0xFF, 0, 1]))
    if rng.random() < 0.2:
        img.u32(slab + 0x2B14, 31)
    return [button, value], img.pokes()


@spec(0x00492560, "TrickInput_MatchHistory", "tmh", this=lambda env: env.slab)
def gen_trick_match(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    trk_world(img, rng, slab)
    start = rng.choice([0, 0, 1, 31, rng.randrange(0, 32)])
    direction = rng.choice([0, 1, 1])
    stop = rng.choice([31, rng.randrange(0, 32), rng.randrange(0, 32)]) if (start == 0 and direction == 1) else rng.randrange(0, 32)
    return [start, direction, rng.choice([0, 20, 0x3C, 0x1E0, 0x3FF, rng.randrange(0, 0x800)]), stop, rng.choice([0, 0, 0x400])], img.pokes()


@spec(0x00492030, "TrickInput_ConsumeRecord", "tcr", this=lambda env: env.slab)
def gen_trick_consume(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    trk_world(img, rng, slab)
    action = rng.choice(TRK_BUTTONS + [0, 0x11, 0x100 + 3])
    pressed = rng.choice([0, 1, 1, 2, 0x101])
    if rng.random() < 0.5:
        # make sure a record of the button exists somewhere in the ring
        index = rng.randrange(0, 32)
        img.u8(slab + 0x2A14 + 8 * index, action & 0xFF)
        img.u8(slab + 0x2A14 + 8 * index + 1, pressed & 0xFF)
    return [action, pressed], img.pokes()


@spec(0x00492290, "TrickScript_QueueStart", "tqs", this=lambda env: env.slab)
def gen_trick_queue(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    clock = trk_world(img, rng, slab)
    img.u32(slab + 0x2BD8, rng.choice([0, 1, 5, 9, 10, 10, 0xFFFFFFFF]))
    img.u32(slab + 0x2BDC, rng.choice([0, 3, 9, 0xFFFFFFFF]))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 1]))
    img.u32(slab + 0x30B8, rng.choice([0, 1, 2, 7, 3]))
    action = rng.choice([0xC, 0xC, 1, 5, 9, 0xFFFF, 0x10])
    timestamp = rng.choice([clock, clock - 0x3C, clock - 0x3D, clock - 0x3B, clock - 100, clock + 5, rng.getrandbits(32)]) & 0xFFFFFFFF
    return [rng.getrandbits(32), action, timestamp, rng.choice([0, 0x4000]), rng.choice([0, 0x2000]), rng.choice([0, 0x8000])], img.pokes()


@spec(0x00492400, "TrickScript_StartPending", "tsp", this=lambda env: env.slab)
def gen_trick_pending(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    clock = trk_world(img, rng, slab)
    img.u32(slab + 0x2C68, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2BD8, rng.choice([0, 1, 1, 3, 10]))
    head = rng.randrange(0, 10)
    img.u32(slab + 0x2BDC, head)
    for k in range(10):
        at = slab + 0x2B38 + 16 * k
        img.u32(at, rng.getrandbits(32))
        img.u32(at + 4, (clock - rng.choice([0, 1, 100, 0xB3, 0xB4, 0xB5, 0x200, rng.randrange(0, 0x400)])) & 0xFFFFFFFF)
        img.u32(at + 8, rng.getrandbits(32))
        img.u32(at + 0xC, (clock - rng.choice([0, 5, 50, 70, 200])) & 0xFFFFFFFF)
    return [], img.pokes()


@spec(0x004925E0, "TrickInput_SelectSequence", "tss", this=lambda env: env.slab)
def gen_trick_select(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    clock = trk_world(img, rng, slab, clock=rng.choice([1000, 5000, 100000, rng.randrange(300, 200000)]))
    entries = trk_table(img, rng, slab)
    img.u32(slab + 0x2BD8, rng.choice([0, 0, 1, 9, 10]))
    img.u32(slab + 0x2BDC, rng.randrange(0, 10))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 1]))
    if rng.random() < 0.75:
        img.u32(slab + 0x2F64, rng.choice([0] * 9 + [1]))
        img.u32(0x0056A858, slab)
        img.u32(slab + 0x30B8, rng.choice([1, 2, 3, 1, 0, 7]))
        img.u32(slab + 0x2F70, rng.choice([0x1000, 0x10000, 0, rng.getrandbits(32)]))
        trk_history_for(img, rng, slab, rng.choice(entries), clock)
    return [], img.pokes()


@spec(0x00492EA0, "TrickScript_Update", "tup", this=lambda env: env.slab)
def gen_trick_update(env, n):
    img, rng = Image(), env.rng
    img.u32(env.slab + 0x29C8, rng.choice([0, 1, 1, 2, 0xFFFFFFFF]))
    return [], img.pokes()


def brake_limit(tilt_word):
    """The speed above which the brake wears the velocity down (Skater_UpdateBrakeMode)."""
    t = -(tilt_word - 0x10000 if tilt_word >= 0x8000 else tilt_word)
    t = (t << 12) & 0xFFFFFFFF
    t = (t - (1 << 32) if t >= (1 << 31) else t) >> 12
    t = max(t, 0x300)
    num = ((0x1000 - t) * 3 * 0x800) & 0xFFFFFFFF
    num = num - (1 << 32) if num >= (1 << 31) else num
    q = abs(num) // 208
    return max(q if num >= 0 else -q, 0xA000)


def brake_raw(tilt_word):
    t = -(tilt_word - 0x10000 if tilt_word >= 0x8000 else tilt_word)
    t = max(t, 0x300)
    return (0x1000 - t) * 3 * 0x800 // 208


BRAKE_EDGE_TILTS = [w for w in range(0x10000) if 0x9FFD <= brake_raw(w) <= 0xA003 or brake_raw(w) in (0xA000,)] or [0]


@spec(0x0049DF00, "Skater_UpdateBrakeMode", "gub", this=lambda env: env.slab)
def gen_brake(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.u32(0x0056865C, rng.choice([0x100, 0x100, 0x80, 0x1000, rng.randrange(0, 0x2000)]))
    for off in (0x29C8, 0x2DD0, 0x2DD4, 0x2F64):
        img.u32(slab + off, rng.choice([0, 0, 0, 0, 0, 1]))
    img.u32(slab + 0x30B8, rng.choice([0, 0, 0, 7, 7, 1, 3]))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    braking = rng.random() < 0.5
    img.u8(slab + GPF_PROFILE + 0xB0, 1 if braking else rng.choice([0, 1]))
    img.u8(slab + GPF_PROFILE + 0x80, 0 if braking else rng.choice([0, 1]))
    img.u8(slab + GPF_PROFILE + 0x90, 0 if braking else rng.choice([0, 1]))
    img.u8(slab + 0x31A2, rng.choice([0, 1, 0x10, 0x7F, 0x80, 0xFF, rng.getrandbits(8)]))
    img.u8(slab + 0x31A1, rng.choice([0, 0, 1, 0xFF, 0x28]))
    tilt = rng.choice([0, 0x400, 0xFC00, 0x100, 0x7FFF, 0x8000, 0xF000, rng.getrandbits(16)] + (BRAKE_EDGE_TILTS[:3] + BRAKE_EDGE_TILTS[-3:] + [rng.choice(BRAKE_EDGE_TILTS)]) * 2)
    img.u16(slab + 0x312A, tilt)
    limit = brake_limit(tilt)
    frame = rng.choice([0, 1, 5, 4, 4, 5, 0xFFFF, rng.getrandbits(16)])
    img.u16(slab + 0xF4, frame)
    fs = frame - 0x10000 if frame >= 0x8000 else frame
    edge = ((fs << 12) + limit)
    speed = rng.choice([limit - 64, limit, limit + 64, limit + 128, 0x30000 - 64, 0x30000, 0x30000 + 64, edge - 64, edge, edge + 64, 0, 64, 0xA000, rng.randrange(0, 0x40000)])
    speed = max(speed, 0) & ~63
    axis, sign = rng.randrange(3), rng.choice([1, -1])
    for k in range(3):
        img.u32(slab + 0x4C + 4 * k, (sign * speed if k == axis else rng.choice([0, 0, 5])) & 0xFFFFFFFF)
    if rng.random() < 0.3:
        # any velocity (not whole steps of 64), big enough to wear down
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, rng.randrange(-0x120000, 0x120000) & 0xFFFFFFFF)
        img.u32(slab + 0x30B8, 0)
        img.u8(slab + GPF_PROFILE + 0xB0, 1)
        img.u8(slab + GPF_PROFILE + 0x80, 0)
        img.u8(slab + GPF_PROFILE + 0x90, 0)
    img.u32(slab + 0x2DF8, rng.choice([0, 0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 0xFFFFFFFF]))
    img.u32(slab + 0x2DD8, rng.choice([0, 0, 1]))
    img.u16(slab + 0xF6, rng.choice([0x5D, 0x5F, 0, 0x31, 0x83, rng.getrandbits(16)]))
    img.u8(slab + 0x107, rng.choice([0, 1, 1]))
    img.u8(slab + 0x100, rng.getrandbits(8))
    img.u8(slab + 0x101, rng.getrandbits(8))
    img.u16(slab + 0x114, rng.getrandbits(16))
    img.u32(slab + 0x30B0, rng.getrandbits(32))
    img.u32(slab + 0x2F2C, rng.getrandbits(32))
    return [], img.pokes()


@spec(0x0049DB80, "Skater_PhysicsDispatcher", "gdp", this=lambda env: env.slab)
def gen_dispatcher(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    clock = rng.choice([1000, 5000, rng.randrange(300, 200000)])
    img.u32(0x005685F4, clock)
    img.u32(0x0056865C, rng.choice([0x100, 0x100, 0x80, 0x1000, rng.randrange(0, 0x2000)]))
    img.u32(0x0056A93C, rng.choice([0, 1, 2, 5]))
    img.u32(0x0056DD58, slab + GDP_TABLE)
    for k in range(9):
        img.u16(slab + GDP_TABLE + 44 * k, rng.choice([0, 1, 2, 4, 0x10, 0x20, 0x33, 0xFFFF, rng.getrandbits(16)]))
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x30B8, rng.choice([0, 0, 0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 8, 9]))
    img.u32(slab + 0x2DD4, rng.choice([0, 1]))
    for k in range(20):
        img.u32(slab + 0x2F74 + 4 * k, rng.choice([0, 1, 1, 0xFFFFFFFF]))
        img.u32(slab + 0x2FC4 + 4 * k, rng.randrange(0, 9))
    for off in (0x3018, 0x3014):
        img.u32(slab + off, rng.choice([0, 0, slab + GDP_GAP]))
    img.u16(slab + GDP_GAP + 6, rng.getrandbits(16))
    img.u32(slab + 0x3064, rng.choice([0, 1]))
    img.u32(slab + 0x3020, rng.getrandbits(32))
    img.u32(slab + 0x301C, rng.getrandbits(32))
    img.u32(slab + 0x29B0, slab + GDP_CAMERA)
    img.u16(slab + 0xF6, rng.choice([0x82, 0x82, 0, 5, rng.getrandbits(16)]))
    img.u32(slab + 0x2DD0, rng.choice([0, 1]))
    img.u32(slab + 0x3110, rng.choice([0x3E8, 0x3E9, 0x3E7, 0, 0x2000, 0xFFFFF000, rng.getrandbits(32)]))
    img.u32(slab + 0x3130, rng.choice([0x5000, 0x5001, 0x4FFF, 0, rng.getrandbits(32)]))
    scale = rng.choice([0x100, 0x100, 0x80, 0x1000])
    img.u32(0x0056865C, scale)
    recent = (scale * 6) >> 8
    img.u32(slab + 0x2D98, (clock - rng.choice([0, recent - 1, recent, recent + 1, 100, 0x7FFFFFFF])) & 0xFFFFFFFF)
    img.u32(slab + 0x2DA0, (clock - rng.choice([0, 0x18, 0x19, 0x1A, 0x1B, 100])) & 0xFFFFFFFF)
    img.u32(slab + 0x50, rng.choice([0, 1, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.u32(slab + 0x2EFC, rng.getrandbits(32))
    for off in (0x30C4, 0x2EC8, 0x2EF0, 0x2EF4, 0x2EF8, 0x2E88):
        img.u32(slab + off, rng.choice([1, 5, 0xFFFFFFFF, rng.getrandbits(32)]))
    for off in (0xBC, 0xC0, 0xC4):
        img.u32(slab + off, rng.getrandbits(32))
    return [], img.pokes()


@spec(0x0049DAD0, "Skater_GroundPreparation", "ggp", this=lambda env: env.slab)
def gen_ground_preparation(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.u32(slab + 0x2DF8, rng.choice([0, 0, 4, 4, 1, 2, 3, 0xFFFFFFFF]))
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 0, 1]))
    img.u16(slab + 0xF6, rng.choice([0x82, 0, 0, 5, rng.getrandbits(16)]))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    img.u8(slab + GPF_PROFILE + 0x71, rng.choice([0, 1, 1, 0xFF]))
    img.u32(slab + 0x2DD0, rng.getrandbits(32))
    img.u32(slab + 0x31F0, rng.getrandbits(32))
    return [], img.pokes()


@spec(0x0049D9C0, "Skater_RunGroundedBounceRecovery", "gbr", this=lambda env: env.slab)
def gen_bounce_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    q12_vector(rng, img, slab + 0x08)
    q12_vector(rng, img, slab + 0x2E0C)
    img.u32(slab + 0x2F64, rng.choice([0, 0, 1, 0xFFFFFFFF]))
    img.u32(0x005614C4, rng.choice([0, 1, 1, 0x100, 0xFFFFFF00]))
    return [], img.pokes()


@spec(0x00490730, "Skater_OuterFloorRecoveryCheck", "gof")
def gen_floor_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.u32(0x005614C4, 0)
    img.u32(slab + 0x2E90, rng.choice([0, 0, 0, 0, 1]))
    q12_vector(rng, img, slab + 0x08)
    q12_vector(rng, img, slab + 0xBC)
    q12_vector(rng, img, slab + 0x2E0C)
    img.u32(slab + 0x4C, rng.getrandbits(32))
    img.u32(slab + 0x54, rng.choice([0, 1, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFC, 3, 5, 0x80000000, 0x7FFFFFFF, rng.getrandbits(32)]))
    img.u32(slab + 0x2F40, rng.getrandbits(32))
    img.u32(slab + GOF_SURFACE + 0xC, rng.choice([0, 0x40000, 0xFFFFFFFF, rng.getrandbits(32)]))
    pos = lambda: rng.choice([0, 0x1000, 0xFFFFF000, rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF, rng.getrandbits(32)])      # noqa: E731
    return [slab, pos(), pos(), pos(), 0x00536F24], img.pokes()


def gic_component(rng, limit):
    """A line component whose (end - start) >> 12, sign-extended from 16 bits, is the first value (the low 12 bits and a whole wrap of 0x10000000 added at random)."""
    unit = rng.choice([0, 0, 1, -1, 2, -3, 0x7FFF, -0x8000, rng.randrange(-0x40, 0x41), rng.randrange(-limit, limit + 1)])
    unit = max(-limit, min(limit, unit))
    delta = (unit << 12) + rng.choice([0, 0xFFF, rng.randrange(0x1000)]) + rng.choice([0, 0, 0, 0x10000000, -0x10000000, 0x70000000])
    return unit, delta


@spec(0x004624D0, "M3D_InitCollisionQuery", "gic")
def gen_init_collision_query(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xA4, rng)
    img.garbage(0x00567BB0, 0x12, rng)
    img.garbage(0x006A3E10, 0x12, rng)
    img.garbage(0x006A3EB0, 0x14, rng)
    img.u32(0x006A3F04, rng.getrandbits(32))
    while True:      # (retail divides by zero once dx^2 + dy^2 + dz^2 passes 0x7fffffff)
        flat = rng.random() < 0.3
        limit = rng.choice([0x40, 0x2000, 0x6000, 0x8000])
        dx, ddx = gic_component(rng, limit)
        dy, ddy = gic_component(rng, 0x7FFF)
        dz, ddz = gic_component(rng, limit)
        if flat:
            dx, ddx = 0, rng.randrange(0x1000) + rng.choice([0, 0x10000000])
            dz, ddz = 0, rng.randrange(0x1000)
        if dx * dx + dz * dz + (dy * dy if dx or dz else 0) < 0x80000000:
            break
    start = [rng.choice([0, rng.randrange(-0x4000000, 0x4000000), rng.getrandbits(32) - 0x80000000]) for _ in range(3)]
    for k, (s, d) in enumerate(zip(start, (ddx, ddy, ddz))):
        img.u32(slab + 4 * k, s)
        img.u32(slab + 0xC + 4 * k, s + d)
    img.u16(0x00533AB4, rng.choice([0xFFFF, 0xFFFF, 0xFFFE, 0, 1, rng.getrandbits(16)]))
    for k, head in enumerate((0x0056DB28, 0x0056AF40)):
        objects = [slab + GIC_OBJECTS + 0x40 * (3 * k + i) for i in range(rng.randrange(4))]
        for i, obj in enumerate(objects):
            img.garbage(obj, 0x40, rng)
            img.u32(obj + 0x20, objects[i + 1] if i + 1 < len(objects) else 0)
        img.u32(head, objects[0] if objects else 0)
    return [slab], img.pokes()


GZQ_STATE = {"tiny": __import__("os").environ.get("GZQ_TINY") == "1"}


def gzq_case(env):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xA4, rng)
    width, depth = rng.choice([1, 2, 3, 20, rng.randrange(1, 21)]), rng.choice([1, 2, 5, 20, rng.randrange(1, 21)])
    size = rng.choice([0x400000, 0x800000, 0x100000, 0x1000, 0x123457, rng.randrange(0x1000, 0x1000000)])
    if GZQ_STATE["tiny"] and rng.random() < 0.8:      # (cells of a few units: a clipped end one unit off lands in another cell)
        width, depth, size = rng.randrange(8, 21), rng.randrange(8, 21), rng.choice([1, 2, 3, 7, 8])
    xmin = rng.choice([0, -width * size // 2, rng.randrange(-0x8000000, 0x8000000)])
    zmin = rng.choice([0, -depth * size // 2, rng.randrange(-0x8000000, 0x8000000)])
    xmax, zmax = xmin + width * size, zmin + depth * size
    img.u32(0x00567F80, rng.choice([1, 1, 1, 1, 1, 1, 1, 1, 1, 0, rng.getrandbits(32)]))
    for at, v in ((0x84, xmin), (0x88, zmin), (0x8C, xmax), (0x90, zmax), (0x94, size)):
        img.u32(0x00567F00 + at, v)
    img.u32(0x00567F98, rng.getrandbits(32))
    img.u16(0x00567F9C, width)
    img.u16(0x00567F9E, depth)
    img.garbage(0x00567FA0, 0x640, rng)
    img.garbage(0x00567A60, 12, rng)
    for k in range(5):
        img.u32(0x00567C74 + 4 * k, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(0x0056AF40, rng.getrandbits(32))

    def coord(lo, hi, cell):
        span = hi - lo
        return rng.choice([lo, hi, lo + cell, hi - cell, lo - 1, hi + 1, lo + rng.randrange(0, cell + 1), rng.randrange(lo, hi + 1), rng.randrange(lo - span, hi + span + 1),
                           rng.randrange(lo - 4 * span - 1, hi + 4 * span + 2)])

    x0, z0 = coord(xmin, xmax, size), coord(zmin, zmax, size)
    if GZQ_STATE["tiny"] and size < 0x10 and rng.random() < 0.7:      # (from far outside, so the clip's products pass 2^53)
        far = rng.choice([0x100, 0x10000, 0x1000000, 0x10000000])
        x0, z0 = xmin + rng.choice([-1, 1]) * rng.randrange(far, 2 * far), zmin + rng.randrange(-far, far)
    shape = rng.random()
    if shape < 0.15:
        x1, z1 = x0, z0
    elif shape < 0.25:
        x1, z1 = x0, coord(zmin, zmax, size)
    elif shape < 0.35:
        x1, z1 = coord(xmin, xmax, size), z0
    elif shape < 0.5:
        x1, z1 = x0 + rng.randrange(-size, size + 1), z0 + rng.randrange(-size, size + 1)
    else:
        x1, z1 = coord(xmin, xmax, size), coord(zmin, zmax, size)
    for k, v in enumerate((x0, rng.getrandbits(32), z0, x1, rng.getrandbits(32), z1)):
        img.u32(slab + 4 * k, v)
    return [slab, rng.choice([0, 1, 1, 0x100])], img.pokes()


@spec(0x004660B0, "M3D_ZoneCollisionBroadphase", "gzq")
def gen_zone_broadphase(env, n):
    return gzq_case(env)


@spec(0x00466090, "M3D_QueryZoneCollision", "gzq")
def gen_zone_query(env, n):
    return gzq_case(env)


def gcs_case(env):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xA4, rng)
    img.garbage(slab + GCS_BODY, 0x40, rng)
    img.garbage(slab + GCS_SURFACE, 0x10, rng)
    img.garbage(0x00567BB0, 0x12, rng)
    img.garbage(0x006A3E10, 0x12, rng)
    img.garbage(0x006A3E58, 8, rng)
    img.garbage(0x006A3EB0, 0x14, rng)
    img.garbage(0x0056B760, 0x90, rng)
    for k in range(3):
        img.u16(slab + GCS_BODY + 0x14 + 2 * k, rng.choice([0, 0x400, 0x800, 0xC00, 0xFFFF, rng.getrandbits(16), rng.randrange(0x1000)]))
        img.u32(0x00564390 + 4 * k, rng.choice([0, 0x1000, 0xFFFFF000, rng.randrange(-0x1000, 0x1001), rng.getrandbits(32)]))
    img.u32(slab + 0x68, rng.choice([0, slab + GCS_BODY, slab + GCS_BODY, slab + GCS_BODY]))
    img.u32(slab + 0x80, slab + GCS_SURFACE)
    img.u32(slab + GCS_SURFACE + 0xC, rng.choice([0, 0xFFFFFFFF, 0x400000, 0x1000000, 0x800000, 0x1E000000, rng.getrandbits(32)]))
    img.u32(slab + GCS_SURFACE, rng.choice([0, 0x80, rng.getrandbits(32)]))
    return [slab], img.pokes()


@spec(0x00463D50, "M3D_CommitCollisionSurface", "gcs")
def gen_commit_surface(env, n):
    return gcs_case(env)


@spec(0x0048EA80, "M3D_ReadSurfaceFlags", "gcs")
def gen_read_surface_flags(env, n):
    return gcs_case(env)


def gcc_model(rng, img, at):
    """A model at `at`: the two vertex counts (+2, +4), the face count (+6), the vertices from +0x1c (three shorts and a pad each), then the faces (a word whose top bits are the face's size in
    dwords and whose bit 0x10 marks a triangle, the four vertex bytes at +4)."""
    img.garbage(at, 0x1C, rng)
    a, b = rng.randrange(0, 5), rng.randrange(1, 6)
    faces = rng.choice([0, 1, 2, 3, 5, 8, rng.randrange(0, 9)])
    img.u16(at + 2, a)
    img.u16(at + 4, b)
    img.u16(at + 6, faces)
    v = at + 0x1C
    for _ in range(a + b):
        for k in range(3):
            img.u16(v + 2 * k, rng.choice([0, 1, 0xFFFF, 0x7FFF, 0x8000, rng.randrange(-0x400, 0x400), rng.getrandbits(16)]))
        img.u16(v + 6, rng.getrandbits(16))
        v += 8
    for _ in range(faces):
        size = rng.choice([2, 3, 4, 5])
        img.garbage(v, 4 * size, rng)
        img.u32(v, (size << 18) | (rng.getrandbits(18) & ~0x10) | rng.choice([0, 0x10]))
        for k in range(4):
            img.u8(v + 4 + k, rng.randrange(a + b))
        v += 4 * size
    assert v <= at + 0x200
    return faces


def gcc_cache(rng, img, items):
    img.garbage(0x00567A70, 0x140, rng)
    for k in range(20):
        img.u32(0x00567A70 + 0x10 * k, rng.choice([0, 0, rng.getrandbits(32)]))
    count = rng.choice([0, 0, 1, 2, 5, 19, 20, rng.randrange(0, 21)])
    if count == 0:
        img.u32(0x00533AB8, 0xFFFFFFFF)
        img.u32(0x00533ABC, 0xFFFFFFFF)
        img.u32(0x00567C88, rng.choice([0, 0, 7, 19]))
        img.u32(0x00567C8C, 0)
        return
    oldest = rng.randrange(20)
    face = rng.choice([0, 0, 3, 470, 480, 490])
    first = face
    for k in range(count):
        slot = (oldest + k) % 20
        size = rng.choice([0, 1, 2, 4, 8] if count <= 5 else [0, 1, 2])      # (the write position stays within the first 64 boxes or the last 40)
        if face + size > 500:
            face = 0
        e = 0x00567A70 + 0x10 * slot
        img.u32(e, rng.choice(items) if items and rng.random() < 0.3 else rng.getrandbits(32) | 1)
        img.u32(e + 8, size)
        img.u32(e + 0xC, face)
        face += size
    img.u32(0x00533AB8, oldest)
    img.u32(0x00533ABC, first)
    img.u32(0x00567C88, (oldest + count) % 20 if count < 20 else oldest)
    img.u32(0x00567C8C, face)


@spec(0x004638D0, "M3D_CollisionCell", "gcc")
def gen_collision_cell(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xA4, rng)
    gen = rng.choice([1, 2, 0x1234, 0xFFFF, rng.getrandbits(16)])
    img.u16(slab + 0x8A, gen)
    img.u32(slab + 0x44, rng.choice([0, 1, 0x1000, 0x12340000 | rng.getrandbits(16), rng.getrandbits(32)]))
    if rng.random() < 0.05:
        img.u32(slab + 0x44, 0)
    img.garbage(0x006A3E10, 0x12, rng)
    img.garbage(0x005643B0, 0x1C * 64, rng)
    img.garbage(0x005643B0 + 0x1C * 460, 0x1C * 40, rng)
    nmodels = 4
    regions = [rng.randrange(4) for _ in range(2)]
    for r in range(4):
        img.u32(0x0056D43C + 0x44 * r, slab + GCC_TABLE)
    for k in range(nmodels):
        img.u32(slab + GCC_TABLE + 4 * k, slab + GCC_MODELS + 0x200 * k)
        gcc_model(rng, img, slab + GCC_MODELS + 0x200 * k)
    items = [slab + GCC_ITEMS + 0x40 * k for k in range(rng.choice([0, 1, 1, 2, 3, 6, 8]))]
    for k, item in enumerate(items):
        img.garbage(item, 0x40, rng)
        img.u16(item + 6, gen if rng.random() < 0.2 else rng.getrandbits(16))
        for j in range(3):
            img.u32(item + 8 + 4 * j, rng.choice([0, rng.randrange(-0x4000000, 0x4000000), rng.getrandbits(32)]))
        img.u16(item + 0x1A, rng.randrange(nmodels))
        img.u8(item + 0x1F, rng.choice(regions))
    if items and rng.random() < 0.15:
        items.append(items[0])      # (the same item twice: the second is stamped)
    for k, item in enumerate(items):
        img.u32(slab + GCC_CELL + 4 * k, item)
    img.u32(slab + GCC_CELL + 4 * len(items), 0)
    gcc_cache(rng, img, items)
    cell = 0 if rng.random() < 0.04 else slab + GCC_CELL
    return [cell, slab], img.pokes()


@spec(0x004628F0, "M3D_PrepareCollisionObjectList", "gcp")
def gen_prepare_objects(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xA4, rng)
    gen = rng.choice([1, 0xFFFF, rng.getrandbits(16)])
    img.u16(slab + 0x8A, gen)
    img.u32(slab + 0x44, rng.choice([0, 1, 0x1000, rng.getrandbits(32), rng.getrandbits(32)]))
    img.garbage(0x006A3E10, 0x12, rng)
    objects = [slab + GCP_OBJECTS + 0x40 * k for k in range(rng.choice([0, 1, 2, 3, 6, rng.randrange(7)]))]
    for k, obj in enumerate(objects):
        img.garbage(obj, 0x40, rng)
        img.u16(obj + 6, gen if rng.random() < 0.3 else rng.getrandbits(16))
        img.u32(obj + 0x20, objects[k + 1] if k + 1 < len(objects) else 0)
    return [objects[0] if objects else 0, slab], img.pokes()

GCF_STATE = {"targeted": __import__("os").environ.get("GCF_TARGETED", "") in ("1", "2"), "mode": __import__("os").environ.get("GCF_TARGETED", "")}      # (1: the gcfB capture, box edges, plane
# thresholds, quad edges, stacked faces; 2: gcfC, a plane side of exactly one either side of zero, a box edge on the line's lowest y)


def gcf_face(rng, quad):
    """Corners (whole units) of a planar face: a triangle v0, v0 + u, v0 + w or a parallelogram whose corners go 0, 1, 3, 2 (sometimes bent out of its plane)."""
    v0 = [rng.randrange(-0x180, 0x180) for _ in range(3)]
    while True:
        u = [rng.choice([0, rng.randrange(-0x100, 0x100)]) for _ in range(3)]
        w = [rng.choice([0, rng.randrange(-0x100, 0x100)]) for _ in range(3)]
        n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
        if any(n):
            break
    corners = [v0, [a + b for a, b in zip(v0, u)], [a + b for a, b in zip(v0, w)]]
    if quad:
        v3 = [a + b + c for a, b, c in zip(v0, u, w)]
        if rng.random() < 0.15:
            v3 = [c + rng.randrange(-8, 9) for c in v3]
        corners.append(v3)
    length = sum(c * c for c in n) ** 0.5
    unit = [c / length for c in n]
    if rng.random() < 0.5:      # (the winding against the normal: the edge tests pass for one sign of the line's direction)
        unit = [-c for c in unit]
    return corners, u, w, unit


def gcf_stack(face, shift):
    corners, u, w, unit = face
    return [[c + round(unit[j] * shift) for j, c in enumerate(v)] for v in corners], u, w, unit


def gcf_line(rng, face, a, b, d0, d1, slant, origin):
    """A line through the point (a, b) of a face's parallelogram, from d0 units on its normal's side to d1 units past it (whole units, plus the low bits of a position)."""
    corners, u, w, unit = face
    hit = [corners[0][j] + a * u[j] + b * w[j] for j in range(3)]
    start = [round((hit[j] + unit[j] * d0 + slant[j] * d0) * 0x1000) + origin[j] + rng.randrange(0x1000) for j in range(3)]
    end = [round((hit[j] - unit[j] * d1 - slant[j] * d1) * 0x1000) + origin[j] + rng.randrange(0x1000) for j in range(3)]
    return start, end


def gcf_targeted(rng, origin):
    """(faces, normals as Q12 shorts or None for the faces' own, start, end, box edit) of a gcfB case."""
    tilted = GCF_STATE["mode"] == "2"
    kind = rng.choice(["box", "plane", "plane"] if tilted else ["box", "plane", "plane", "quad", "stack"])
    if kind == "plane":
        # a triangle in a plane of constant y, its normal (0, ny, 0) small or near half a unit: the start and the end whole units off the plane
        v0 = [rng.randrange(-0x100, 0x100) for _ in range(3)]
        size = rng.randrange(8, 0x40)
        u, w = [size, 0, 0], [0, 0, size]
        if rng.random() < 0.5:
            u, w = w, u
        corners = [v0, [v0[0] + u[0], v0[1], v0[2] + u[2]], [v0[0] + w[0], v0[1], v0[2] + w[2]]]
        ny = rng.choice([1, 1, -1, 2, 0x7FF, 0x800, 0x800, 0x801, 0x400, 0x1000])
        sy, ey = rng.choice([1, 1, 0, -1, 2]), rng.choice([-1, -1, 0, 1, -2])
        nx, sx, ex = 0, 0, 0
        if tilted:      # (the normal tilted by one in x and the line straight down one unit past corner 0 in x: a side of -1 at the start with the end half a unit past, or 1 at the end)
            ny = rng.choice([0x800, 0x1000])
            sy, ey, nx = rng.choice([(0, -1, -1), (1, 0, 1), (1, -1, 1), (0, -1, 1), (1, 0, -1)])
        x, z = v0[0] + rng.randrange(1, size // 3), v0[2] + rng.randrange(1, size // 3)
        if tilted:
            x = v0[0] + 1
        start = [((x + sx) << 12) + origin[0] + rng.randrange(0x1000), ((v0[1] + sy) << 12) + origin[1] + rng.randrange(0x1000), (z << 12) + origin[2] + rng.randrange(0x1000)]
        end = [((x + ex) << 12) + origin[0] + rng.randrange(0x1000), ((v0[1] + ey) << 12) + origin[1] + rng.randrange(0x1000), (z << 12) + origin[2] + rng.randrange(0x1000)]
        return [(corners, u, w, [0, 1, 0])], [[nx, ny, 0]], start, end, None
    if kind == "quad":
        # a parallelogram missed by one edge only (a or b just past 0 or 1)
        face = gcf_face(rng, True)
        a, b = rng.uniform(0.2, 0.8), rng.uniform(0.2, 0.8)
        past = rng.choice([-0.08, 1.08, rng.uniform(-0.2, 0), rng.uniform(1, 1.2)])
        if rng.random() < 0.5:
            a = past
        else:
            b = past
        start, end = gcf_line(rng, face, a, b, rng.uniform(2, 30), rng.uniform(2, 30), [0, 0, 0], origin)
        return [face], None, start, end, None
    if kind == "stack":
        # two or three parallel copies of a face between the start and the end, in either order
        face = gcf_face(rng, rng.random() < 0.5)
        d0 = rng.uniform(20, 50)
        faces = [gcf_stack(face, s) for s in rng.sample([0, 3, 8, 14], rng.choice([2, 2, 3]))]
        rng.shuffle(faces)
        start, end = gcf_line(rng, face, rng.uniform(0.3, 0.6), rng.uniform(0.2, 0.4), d0, rng.uniform(4, 30), [0, 0, 0], origin)
        return faces, None, start, end, None
    # box: a face the line crosses whose box has one side moved onto (or just past) the line's widened box
    face = gcf_face(rng, rng.random() < 0.5)
    start, end = gcf_line(rng, face, rng.uniform(0.2, 0.4), rng.uniform(0.2, 0.4), rng.uniform(1, 20), rng.uniform(1, 20), [rng.choice([0, rng.uniform(-1, 1)]) for _ in range(3)], origin)
    axis, side = rng.randrange(3), rng.choice(["low", "high"])
    delta = rng.choice([0, 0, -1, 1, rng.randrange(-0x1000, 0x1001), rng.randrange(1, 0xF00), -rng.randrange(1, 0xF00)])
    if tilted:
        axis, side, delta = 1, "high", rng.choice([0, 0, 0, -1])
    if side == "low":
        edge = (max(start[axis], end[axis]) & 0xFFFFF000) + 0x4000 + delta
    else:
        edge = (min(start[axis], end[axis]) & 0xFFFFF000) - 0x4000 + delta
    return [face], None, start, end, (axis, side, edge)


@spec(0x00462A20, "M3D_CollisionFaceTest", "gcf")
def gen_face_test(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    targeted = GCF_STATE["targeted"]
    record, entry, item, model = slab, slab + GCF_ENTRY, slab + GCF_ITEM, slab + GCF_MODEL
    img.garbage(record, 0xA4, rng)
    img.garbage(item, 0x40, rng)
    img.garbage(slab + GCF_LISTS, 0x80, rng)
    for k in range(3):
        img.u16(item + 0x14 + 2 * k, 0 if targeted or rng.random() < 0.98 else rng.getrandbits(16))
    index = rng.randrange(4)
    region = rng.randrange(4)
    img.u16(item + 0x1A, index)
    img.u8(item + 0x1F, region)
    origin = [rng.choice([0, rng.randrange(-0x4000000, 0x4000000)]) for _ in range(3)]
    if targeted:
        origin = [c & 0xFFFFF000 for c in origin]
    for k in range(3):
        img.u32(item + 8 + 4 * k, origin[k])
    for r in range(4):
        img.u32(0x0056D43C + 0x44 * r, slab + GCF_TABLE)
    for k in range(4):
        img.u32(slab + GCF_TABLE + 4 * k, model)
    img.u32(0x0056D41C, slab + GCF_LISTS)
    img.u32(0x0056D41C + 4, slab + GCF_LISTS + 0x40)
    img.garbage(0x0056438C, 0x10, rng)
    img.garbage(0x00567C80, 4, rng)
    img.garbage(0x0056DB2C, 4, rng)
    if targeted and rng.random() < 0.85:
        img.u32(0x00567A60, 0)
        img.u32(0x00567A68, 0xFFFFFFFF)
    else:
        img.u32(0x00567A60, rng.choice([0, 0, 0, 1 << rng.randrange(18, 32), rng.getrandbits(32) & ~0x3FFFF]))
        img.u32(0x00567A68, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, ~(1 << rng.randrange(18, 32)), 0xFFFCFFFF]))

    box_edit = None
    own_normals = None
    if targeted:
        faces, own_normals, start, end, box_edit = gcf_targeted(rng, origin)
    else:
        # the faces: real planes, some stacked along a normal so that the line meets two
        count = rng.choice([0, 1, 1, 2, 3, 4, 6, 8, 12, rng.randrange(1, 13)])
        faces = []
        for k in range(count):
            if faces and rng.random() < 0.25:
                stacked = gcf_stack(faces[rng.randrange(len(faces))], rng.choice([1, 2, 5, -3, rng.randrange(-40, 40)]))
                if rng.random() < 0.5 and len(stacked[0]) == 4:
                    stacked = (stacked[0][:3],) + stacked[1:]
                faces.append(stacked)
            else:
                faces.append(gcf_face(rng, rng.random() < 0.5))
        # the line: through a face (inside or near its edges), from the normal's side to the other, or anywhere
        if faces and rng.random() < 0.9:
            face = faces[rng.randrange(len(faces))]
            a, b = rng.choice([(rng.random(), rng.random()), (rng.uniform(-0.3, 1.3), rng.uniform(-0.3, 1.3))])
            if len(face[0]) == 3 and a + b > 1 and rng.random() < 0.7:
                a, b = 1 - a, 1 - b
            d0 = rng.choice([0, 0.3, 0.6, 1, 4, rng.uniform(0, 60)])
            d1 = rng.choice([0, 0.3, 0.6, 1, 4, rng.uniform(0, 60)])
            if rng.random() < 0.1:
                d0, d1 = -d0, -d1
            start, end = gcf_line(rng, face, a, b, d0, d1, [rng.choice([0, rng.uniform(-1, 1)]) for _ in range(3)], origin)
        else:
            start = [origin[j] + rng.randrange(-0x400000, 0x400000) for j in range(3)]
            end = [origin[j] + rng.randrange(-0x400000, 0x400000) for j in range(3)]
    count = len(faces)

    vertices = [v for f in faces for v in f[0]]
    normals = own_normals or [[round(c * 0x1000) for c in f[3]] for f in faces]
    normals = normals + [[rng.randrange(-0x1000, 0x1001) for _ in range(3)] for _ in range(rng.randrange(0, 3))]
    img.garbage(model, 0x1C, rng)
    img.u16(model + 2, len(vertices))
    img.u16(model + 4, len(normals))
    img.u16(model + 6, count if targeted or rng.random() < 0.98 else count + 1)
    at = model + 0x1C
    for v in vertices:
        for k in range(3):
            img.u16(at + 2 * k, v[k])
        img.u16(at + 6, rng.getrandbits(16))
        at += 8
    for nrm in normals:
        for k in range(3):
            img.u16(at + 2 * k, nrm[k])
        img.u16(at + 6, rng.getrandbits(16))
        at += 8
    first = rng.randrange(0, 56 - max(count, 1) + 1)
    box = 0x005643B0 + 0x1C * first
    base = 0
    for k, (corners, u, w, unit) in enumerate(faces):
        size = rng.choice([4, 4, 5, 6])
        img.garbage(at, 4 * size, rng)
        tri = len(corners) == 3
        img.u32(at, (size << 18) | (rng.getrandbits(18) & ~0x10) | (0x10 if tri else 0))
        for j in range(4):
            img.u8(at + 4 + j, base + j if j < len(corners) else rng.randrange(max(len(vertices), 1)))
        normal = k if targeted or rng.random() < 0.92 else rng.randrange(len(normals))
        kind = 0 if targeted and rng.random() < 0.8 else rng.choice([0, 0, 0, 0, 0x10000, 0x20000, 0x30000])
        high = rng.choice([0, 0, rng.getrandbits(14) << 18])
        img.u32(at + 0xC, high | kind | (normal << 3) | rng.randrange(8))
        # its box in world units (as the cell test builds it), sometimes off
        img.u32(box, rng.getrandbits(16))
        for j in range(3):
            low = (min(v[j] for v in corners) << 12) + origin[j]
            hi = (max(v[j] for v in corners) << 12) + origin[j]
            if not targeted and rng.random() < 0.04:
                low, hi = hi + (rng.randrange(1, 0x400) << 12), hi + (rng.randrange(0x400, 0x800) << 12)
            if box_edit is not None and box_edit[0] == j:
                if box_edit[1] == "low":
                    low = box_edit[2]
                else:
                    hi = box_edit[2]
            img.u32(box + 4 + 4 * j, low)
            img.u32(box + 0x10 + 4 * j, hi)
        box += 0x1C
        base += len(corners)
        at += 4 * size
    assert at <= model + 0x500

    for j in range(3):
        img.u32(record + 4 * j, start[j])
        img.u32(record + 0xC + 4 * j, end[j])
        img.u32(record + 0x18 + 4 * j, min(start[j], end[j]))
        img.u32(record + 0x24 + 4 * j, max(start[j], end[j]))
    img.u32(record + 0x44, rng.choice([0, 1, 0x40, 0x1000, rng.randrange(0, 0x2000), rng.getrandbits(32)]))
    img.u8(record + 0x88, rng.choice([0, 0, 1, rng.getrandbits(8)]))
    img.u32(record + 0x8C, 0x7FFFFFFF if targeted and rng.random() < 0.85 else rng.choice([0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, rng.randrange(0, 0x4000), 0x2000, 0, -5]))

    img.u32(entry, item if targeted or rng.random() < 0.98 else item + 0x40)
    img.u32(entry + 4, model if targeted or rng.random() < 0.98 else model + 4)
    img.u32(entry + 8, count)
    img.u32(entry + 0xC, first)
    return [item, record, entry], img.pokes()


# gsl: Skater_ApplyShadeLighting 0x0046d160 (thiscall on the skater, vtable slot 5; no calls). Slab: the skater at 0x0000 (its shade channels +0x3184..+0x3190, the lighting +0x31ac..+0x31e0 and the
# source pointer +0x31e0), the source record at 0x3300 (or overlapping the lighting itself).
GSL_SOURCE = 0x3300


def gsl_regions(slab):
    return merged([(slab + 0x3180, 0x80), (slab + GSL_SOURCE, 0x40)], gap=0)


def install_stubs_gsl(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gsl_installed", False):
        return
    fc._gsl_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()


GROUP_STUBS["gsl"] = install_stubs_gsl
EXTRA_REGIONS["gsl"] = gsl_regions
SLAB_BYTES["gsl"] = SLAB + 0x100
READ_ONLY["gsl"] = []


@spec(0x0046D160, "Skater_ApplyShadeLighting", "gsl", this=lambda env: env.slab)
def gen_apply_shade_lighting(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab + 0x3180, 0x80, rng)
    img.garbage(slab + GSL_SOURCE, 0x40, rng)
    channel = lambda: rng.choice([0x100, 0x100, 0, 0x1FF, 0x80, rng.randrange(0, 0x200), rng.getrandbits(32)])      # noqa: E731
    for k in range(4):
        img.u32(slab + 0x3184 + 4 * k, channel())
    source = slab + GSL_SOURCE
    if rng.random() < 0.06:
        source = slab + 0x31AC + rng.choice([-8, -4, 4, 8, 0x14])
    img.u32(slab + 0x31E0, source)
    for k in range(9):
        img.u16(source + 0x14 + 2 * k, rng.choice([0x1000, 0, 0x800, 0xF000, rng.getrandbits(16)]))
    for k in range(3):
        img.u32(source + 0x28 + 4 * k, rng.choice([0, 0x80, 0x1000, rng.randrange(0, 0x100), rng.getrandbits(32)]))
    img.u32(slab + 0x31E0, source)
    return [], img.pokes()


# gdg: Skater_DoGroundPhysics 0x00496550 (thiscall on the skater, a service of the dispatcher). Slab: the skater at 0x0000 (0x3600 bytes), its pad profile at 0x3700 (the byte +0xa0), two surface
# records at 0x3A00 / 0x3A40 (words +0 and +0xc). The vector helpers, the dot product, the root, the normalisation, the cross product, the arccosine, the basis copies (0x0049c7d0, 0x0049c850), the
# normal removal (0x00490610, 0x00490680), Skater_Nothing and M3D_ReadSurfaceFlags run as retail code; every other callee is a stub. The two zone queries (the move, then the floor) share the
# record (a stack local): each call of the query stub takes the next scripted hit.
GDG_PROFILE = 0x3700
GDG_SURFACE = 0x3A00
GDG_GLOBALS = [(0x005685F4, 4), (0x0056865C, 4), (0x00568658, 4), (0x00533F38, 4), (0x0056A3D8, 8), (0x0056A854, 4), (0x00568638, 4), (0x0056881C, 0x40), (0x00568804, 4),
               (0x0056AF28, 12), (0x0056B760, 0x90), (0x00567C7C, 4), (0x005614C4, 4), (0x0056A93C, 4), (0x006A3E00, 0x120)]
GDG_STATE = {"hits": [], "n": 0, "cur": None, "pw": None, "targeted": __import__("os").environ.get("GDG_TARGETED") == "1"}      # (targeted: the gdgB capture, aimed at the edges the first fixture's mutants survived)


def gdg_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GDG_PROFILE, 0x100), (slab + GDG_SURFACE, 0x80)] + list(GDG_GLOBALS), gap=0)


def gdg_next(a):
    state = GDG_STATE
    state["cur"] = state["hits"][min(state["n"], len(state["hits"]) - 1)]
    state["n"] += 1
    return state["cur"]["body"]


def gdg_field(name):
    return lambda a: GDG_STATE["cur"][name]


def install_stubs_gdg(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gdg_installed", False):
        return
    fc._gdg_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    for address, args in ((0x00495AF0, 0), (0x0048F7C0, 0), (0x0048F8F0, 0), (0x00496360, 0), (0x00496300, 0), (0x0049D080, 0), (0x004956F0, 0), (0x0048F5F0, 0), (0x0048F800, 1),
                          (0x004903F0, 2), (0x004900B0, 2), (0x00497BB0, 2), (0x00491780, 2), (0x0049BAD0, 3), (0x0049B500, 3)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    slab = env.slab

    def position(k):
        # Skater_PositionWritePath: the position it leaves (the target, the old one moved by about a unit on one axis, or unchanged), scripted
        def produce(a):
            if k == 0:
                from opentony.memory import mem

                before = [mem.u32(slab + 8 + 4 * i) for i in range(3)]
                pick = rng.random()
                if pick < 0.5:
                    out = list(a[:3])
                elif pick < 0.85:
                    out = list(before)
                    axis = rng.randrange(3)
                    out[axis] = (out[axis] - rng.choice([0x1000, 0x1000, -0x1000, 0xFFF, 0x1001, -0xFFF, 0x800, 0x2000])) & 0xFFFFFFFF
                else:
                    out = before
                GDG_STATE["pw"] = out
            return GDG_STATE["pw"][k]
        return produce
    ensure(fc, 0x00496060, 3, "engine_00496060", convention="thiscall", this=True, writes=[(slab + 8 + 4 * k, 4, position(k)) for k in range(3)])
    ensure(fc, 0x0046D970, 0, "engine_0046d970", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 1, 0x100]))     # CBruce__FlipToVelocity
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,                                                       # Skater_ProfileValue: a stat
           result=lambda a: rng.choice([0, 3, 5, 10, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11)]))
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0] * 6 + [1, 0x100]))   # Career_GetCheat (a bool in AL)
    ensure(fc, 0x004A12D0, 1, "engine_004a12d0")
    ensure(fc, 0x0046E1D0, 1, "engine_0046e1d0")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GPF_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, gdg_next, 0x68), (0, 4, gdg_field("x"), 0x6C), (0, 4, gdg_field("y"), 0x70), (0, 4, gdg_field("z"), 0x74), (0, 4, gdg_field("w78"), 0x78),
                  (0, 4, gdg_field("w7c"), 0x7C), (0, 4, gdg_field("distance"), 0x40), (0, 4, gdg_field("surface"), 0x80)])


GROUP_STUBS["gdg"] = install_stubs_gdg
EXTRA_REGIONS["gdg"] = gdg_regions
SLAB_BYTES["gdg"] = SLAB + 0x100
READ_ONLY["gdg"] = []

GDG_NORMALS = [(0, -0x1000, 0), (0, -0x1000, 0), (0, 0x1000, 0), (0x1000, 0, 0), (-0x1000, 0, 0), (0, 0, 0x1000), (0, 0, -0x1000), (0xB50, -0xB50, 0), (0, -0xB50, 0xB50), (0x800, -0xDDB, 0),
               (0x1000, -0x10, 0), (0x10, -0x1000, 0x7FF), (0x7FF, -0x7FF, 0x7FF)]


def gdg_s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def gdg_normal(rng, ground):
    """A hit normal: the ground's, one of the list (possibly slightly off), or random shorts."""
    pick = rng.random()
    if pick < 0.3:
        n = ground
    elif pick < 0.85:
        n = rng.choice(GDG_NORMALS)
        if rng.random() < 0.3:
            n = tuple(c + rng.randrange(-3, 4) for c in n)
    else:
        n = tuple(rng.randrange(-0x1000, 0x1001) for _ in range(3))
    return n


def gdg_hit(rng, slab, ground, near, distances):
    n = gdg_normal(rng, ground)
    high = rng.choice([0, 0, 0, rng.getrandbits(16)])
    offset = lambda: rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x40000, 0x40000)])      # noqa: E731
    return {"body": rng.choice([0, 1, 0x1234, 0x1234, 0x1234]),
            "x": (near[0] + offset()) & 0xFFFFFFFF, "y": (near[1] + offset()) & 0xFFFFFFFF, "z": (near[2] + offset()) & 0xFFFFFFFF,
            "w78": (n[0] & 0xFFFF) | ((n[1] & 0xFFFF) << 16), "w7c": (n[2] & 0xFFFF) | (high << 16),
            "distance": rng.choice(distances) & 0xFFFFFFFF, "surface": slab + GDG_SURFACE + rng.choice([0, 0x40])}


def gdg_surface_flags(rng):
    """Surface word +0xc: bit 22 the lip (0x56b768), bits 24 / 23 clear raise 0x56b7b8 / 0x56b7a8, bits 25..28 the class, bit 21 the +0x2db4 flag."""
    kind = rng.random()
    if kind < 0.35:
        flags = 0x01800000                                  # plain: no lip, both inverse bits set
    elif kind < 0.55:
        flags = 0x01800000 | 0x00400000                     # a lip
    elif kind < 0.7:
        flags = rng.choice([0x00800000, 0x01000000, 0])     # an inverse flag raised
    else:
        flags = rng.getrandbits(32) & 0x01C00000
    flags |= rng.choice([0, 3, 5, 6, 1, rng.randrange(0, 16)]) << 25
    flags |= rng.choice([0, 0x00200000]) | (rng.getrandbits(16) if rng.random() < 0.3 else 0)
    return flags


@spec(0x00496550, "Skater_DoGroundPhysics", "gdg", this=lambda env: env.slab)
def gen_ground_physics(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    small = lambda: rng.choice([0, 0, 1, 2, 5, 0xFFFFFFFF, word(rng)])      # noqa: E731
    state = rng.choice([0, 0, 0, 0, 1, 2, 2, 2, 7, 3])
    img.u32(slab + 0x30B8, state)
    img.u32(slab + 0x2C60, rng.choice([0, 0, 1, 2, 5]))
    clock = rng.choice([0x1000, 0x10000, rng.getrandbits(31)])
    img.u32(0x005685F4, clock)
    ts = rng.choice([0x100, 0x100, 0x100, 0x1000, 0x80, 1, rng.randrange(0, 0x2000)])
    img.u32(0x0056865C, ts)
    img.u32(slab + 0x2D98, (clock - rng.choice([0, 4, 5, 6, 19, 20, 21, 100, 0x1000, rng.getrandbits(32)])) & 0xFFFFFFFF)
    img.u32(0x00568658, rng.choice([0, 1, 1, 2]))
    img.u32(0x00533F38, rng.choice([0, 1, 7, 7]))
    img.u32(slab + 0x2CC4, rng.choice([0, 1]))
    img.u32(0x0056A854, rng.choice([0, 1]))
    img.u32(0x0056A3D8, rng.choice([0, 1]))
    img.u32(0x0056A3DC, rng.choice([0, 1]))
    img.garbage(0x0056B760, 0x90, rng)
    img.u32(0x0056B7EC, rng.choice([0, 0, 0, 1]))
    step = rng.choice([0x100, 0x1000, 0x80, rng.randrange(0, 0x400)])
    img.u32(0x00568804, step)
    # the position, the old position (mostly the same: the assertion holds), the velocity and the acceleration
    pos = [rng.choice([0, rng.randrange(-0x400000, 0x400000)]) & 0xFFFFFFFF for _ in range(3)]
    old = list(pos) if rng.random() < 0.7 else [(p + rng.choice([0, 1, -1, 0x1000, -0x30000, rng.randrange(-0x40000, 0x40000)])) & 0xFFFFFFFF for p in pos]
    vel = [0, 0, 0]
    acc = [0, 0, 0]
    if rng.random() < 0.85:
        size = rng.choice([0x100, 0x2000, 0x10000, 0x40000, 0x100000])
        vel = [rng.choice([0, rng.randrange(-size, size)]) for _ in range(3)]
        acc = [rng.choice([0, 0, rng.randrange(-0x4000, 0x4000), rng.getrandbits(32)]) for _ in range(3)]
    for k in range(3):
        img.u32(slab + 0x08 + 4 * k, pos[k])
        img.u32(slab + 0xBC + 4 * k, old[k])
        img.u32(slab + 0x4C + 4 * k, vel[k] & 0xFFFFFFFF)
        img.u32(slab + 0x58 + 4 * k, acc[k] & 0xFFFFFFFF)
    # the move as retail computes it, for the comparison with 0x56af28 (made equal now and then)
    moved = []
    for k in range(3):
        pushed = int(gdg_s32(acc[k] * step) >> 8)
        pushed = -((-pushed) // 2) if pushed < 0 else pushed // 2
        drift = gdg_s32(vel[k] * ts) >> 8
        moved.append((pos[k] + drift + pushed - old[k]) & 0xFFFFFFFF)
    zero = moved if rng.random() < 0.05 else ([0, 0, 0] if rng.random() < 0.95 else [word(rng) for _ in range(3)])
    for k in range(3):
        img.u32(0x0056AF28 + 4 * k, zero[k])
    img.u16(slab + 0xF6, rng.choice([0xC, 0xC, 0, 5, 0x2E, rng.getrandbits(16)]))
    for field in (0x2F64, 0x2C74):
        img.u32(slab + field, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2DD4, rng.choice([0, 1]))
    img.u32(slab + 0x2C8C, small())
    img.u32(slab + 0x2D9C, rng.choice([0, -0x3FE, -0x400, -0x3FC, 0x10, -5, 3, rng.randrange(-0x800, 0x800)]) & 0xFFFFFFFF)
    for field in (0x2C08, 0x29C8, 0x3074, 0x3130, 0x2D94, 0x29E0, 0x3068, 0x306C, 0x2D88, 0x2DB4, 0x3128, 0x312C):
        img.u32(slab + field, small())
    img.garbage(slab + 0x2E18, 12, rng)
    img.garbage(slab + 0x3118, 12, rng)
    img.garbage(slab + 0x3134, 12, rng)
    img.garbage(slab + 0x2A14, 0x100, rng)
    # the basis: the integer vectors (read before the end) and the orientation shorts they are rebuilt from
    q12_vector(rng, img, slab + 0x30F4, unit=True)
    q12_vector(rng, img, slab + 0x3100, unit=True)
    q12_vector(rng, img, slab + 0x310C, unit=True)
    for at in (0x2E58, 0x2E5A, 0x2E5C, 0x2E5E, 0x2E60, 0x2E62, 0x2E64, 0x2E66, 0x2E68):
        img.u16(slab + at, rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1001)]) & 0xFFFF)
    up = rng.choice([(0, -0x1000, 0), (0, -0x1000, 0), (0, 0x1000, 0), (0x1000, 0, 0), (0xB50, -0xB50, 0), tuple(rng.randrange(-0x1000, 0x1001) for _ in range(3))])
    for k, at in enumerate((0x2E5A, 0x2E60, 0x2E66)):
        img.u16(slab + at, up[k] & 0xFFFF)
    ground = rng.choice(GDG_NORMALS[:8])
    for k in range(3):
        img.u16(slab + 0x80 + 2 * k, ground[k] & 0xFFFF)
    da4 = rng.choice([0, 0x14, 0x2C, 1, rng.randrange(0, 0x100)])
    img.u32(slab + 0x2DA4, da4)
    for k in range(3):
        img.u32(slab + 0x2DA8 + 4 * k, rng.choice([0, 0x1964, -0x1964, rng.randrange(-0x2000, 0x2000)]) & 0xFFFFFFFF)
    img.u32(slab + 0x2D8C, rng.choice([0, 0, (clock - 5) & 0xFFFFFFFF, (clock - 4) & 0xFFFFFFFF, (clock - 3) & 0xFFFFFFFF, (clock - 100) & 0xFFFFFFFF, word(rng)]))
    img.u32(slab + 0x2DDC, rng.choice([0, 1]))
    img.u32(slab + 0x30B0, rng.choice([0, 1, 3, 5, 6, rng.randrange(0, 16)]))
    img.u32(slab + 0x2DFC, rng.choice([0, 8, 0x40, 0x1000, rng.randrange(-0x100, 0x100) & 0xFFFFFFFF]))
    img.u32(slab + 0x2CCC, slab + GDG_PROFILE)
    img.u8(slab + GDG_PROFILE + 0xA0, rng.choice([0, 1]))
    img.u32(0x00567C7C, word(rng))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u32(0x0056A93C, rng.choice([0, 1, 0x100, rng.getrandbits(16)]))
    for at in (GDG_SURFACE, GDG_SURFACE + 0x40):
        img.u32(slab + at, rng.choice([0, 0x80, rng.getrandbits(32)]))
        img.u32(slab + at + 0xC, gdg_surface_flags(rng))
    # the two hits: the move's (near the old position, distances about the move's length in units) and the floor's (about the reaches 0xa0, +0x2da4 + 0x74 and twice those)
    first = gdg_hit(rng, slab, ground, old, [0, 0, 1, 2, 3, 4, 0x10, 0x100, rng.randrange(-4, 0x20)])
    base = 0xA0 if state == 2 else da4 + 0x74
    floor = gdg_hit(rng, slab, ground, pos, [0, 0x50, base - 1, base, base + 1, 2 * base - 1, 2 * base, 2 * base + 1, 0x200, rng.randrange(0, 0x300)])
    if rng.random() < 0.15:
        first["body"] = 0
    if rng.random() < 0.1:
        floor["body"] = 0
    if GDG_STATE["targeted"]:
        gdg_target(img, rng, slab, clock, first, floor)
    GDG_STATE["hits"] = [first, floor]
    GDG_STATE["n"] = 0
    return [], img.pokes()


def gdg_set_normal(hit, n):
    hit["w78"] = (n[0] & 0xFFFF) | ((n[1] & 0xFFFF) << 16)
    hit["w7c"] = n[2] & 0xFFFF


def gdg_target(img, rng, slab, clock, first, floor):
    """One narrow path, built on purpose: the bail dot of a plain hit at 0xb50, the wall's speed of 7 units, the floor's doubled reach at the slope 0xed8, the lip's angle sum at +-2 and at
    -0x400, the take-off's halved upward speed, the standing recovery's agreement at 0xbb8 and the upward speed 0."""
    ground = (0, -0x1000, 0)
    for k in range(3):
        img.u16(slab + 0x80 + 2 * k, ground[k] & 0xFFFF)
    img.u32(slab + 0x2F64, 0)
    img.u32(slab + 0x2C74, 0)
    img.u16(slab + 0xF6, 0)
    img.u32(0x0056865C, 0x100)
    first_surface, floor_surface = slab + GDG_SURFACE, slab + GDG_SURFACE + 0x40
    first["surface"], floor["surface"] = first_surface, floor_surface
    target = rng.randrange(6)
    if target < 2:
        # the move's hit: a plain surface met head on (target 0) or a wall at a speed of about 7 units (target 1)
        first["body"] = 0x1234
        vel = (rng.choice([0x7000, 0x7FFF, 0x6FFF, 0x8000, 0x7800]), 0, 0) if target == 1 else (0x10000, 0, 0)
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, vel[k] & 0xFFFFFFFF)
            img.u32(slab + 0x58 + 4 * k, 0)
            img.u32(slab + 0x08 + 4 * k, img_u32(img, slab + 0xBC + 4 * k))
        img.u32(first_surface + 0xC, 0x01800000 if target == 0 else 0x00800000)
        img.u32(first_surface, 0)
        gdg_set_normal(first, (0x1000, 0, 0))
        forward = (rng.choice([0xB50, 0xB50, 0xB4F, 0xB51]) if target == 0 else rng.choice([0x1000, 0xB50, 0xB51]), 0, 0)
        for k in range(3):
            img.u32(slab + 0x30F4 + 4 * k, forward[k] & 0xFFFFFFFF)
        return
    first["body"] = 0
    floor["body"] = 0x1234
    if target == 2:
        # the floor beyond the reach (0x74), within twice it when the pad flag is on and the floor's slope from the ground is below 0xed8
        img.u32(slab + 0x30B8, 0)
        img.u32(slab + 0x2DA4, 0)
        img.u8(slab + GDG_PROFILE + 0xA0, 1)
        img.u32(floor_surface + 0xC, rng.choice([0x01C00000, 0x00800000]))
        gdg_set_normal(floor, (rng.choice([0x5F5, -0x5F5, 0]), -rng.choice([0xED8, 0xED8, 0xED7, 0xED9]), 0))
        floor["distance"] = rng.choice([0x75, 0x80, 0xE8, 0xE9])
    elif target == 3:
        # skating over a lip: the angle between the velocity and the floor near +-2, the sum near -0x400
        img.u32(slab + 0x30B8, 2)
        img.u32(floor_surface + 0xC, 0x01C00000)
        gdg_set_normal(floor, ground)
        floor["distance"] = 0
        y = rng.randrange(-320, 321)
        for k, v in enumerate((0x10000, y, rng.choice([0, 0, 0x400]))):
            img.u32(slab + 0x4C + 4 * k, v & 0xFFFFFFFF)
        img.u32(slab + 0x2D9C, rng.choice([-0x400 + rng.randrange(-3, 4), rng.randrange(-6, 7), 0x10, -0x10]) & 0xFFFFFFFF)
    elif target == 4:
        # off a lip into the air: the upward speed halved (odd and negative)
        img.u32(slab + 0x30B8, rng.choice([0, 3]))
        img.u32(floor_surface + 0xC, 0x01C00000)
        img.u32(floor_surface, 0)
        img.u32(slab + 0x2D8C, (clock - 5) & 0xFFFFFFFF)
        floor["distance"] = 0
        img.u32(slab + 0x50, rng.choice([-1, -3, -0x1001, 1, 3]) & 0xFFFFFFFF)
    else:
        # standing on a floor that faces away: the agreement with the ground at 0xbb8, the upward speed 0
        img.u32(slab + 0x30B8, 0)
        img.u32(floor_surface + 0xC, 0x00800000)
        floor["distance"] = 0
        for k, at in enumerate((0x2E5C, 0x2E62, 0x2E68)):
            img.u16(slab + at, (-0x1000, 0, 0)[k] & 0xFFFF)
        gdg_set_normal(floor, (0x800, -rng.choice([0xBB8, 0xBB8, 0xBB7, 0xBB9]), 0))
        img.u32(slab + 0x50, rng.choice([0, 0, -1, 1]) & 0xFFFFFFFF)


def img_u32(img, address):
    return sum(img.b.get(address + i, 0) << (8 * i) for i in range(4))


# ghb: Platform_HandleBounce 0x004a12d0 (cdecl, the skater). Slab: the skater at 0x0000 (its position +8, old position +0xbc, 0xd0 bytes), up to six game-list objects at 0x0400 (0x200 apart; the
# fields +4, +8, +0x1a, +0x1f, +0x20, +0x178), the region's model-pointer arrays at 0x1000 (0x40 apart) and the models at 0x1400 (0x40 apart). CPlatform__Bounce 0x0049f4c0 is a stub.
GHB_NODES, GHB_ARRAYS, GHB_MODELS = 0x0400, 0x1000, 0x1400
GHB_GLOBALS = [(0x0056AF40, 4), (0x0056A960, 4), (0x0056D43C, 0x44 * 4)]


def ghb_regions(slab):
    return merged([(slab, 0xD0), (slab + GHB_NODES, 0x200 * 6), (slab + GHB_ARRAYS, 0x40 * 4), (slab + GHB_MODELS, 0x40 * 8)] + list(GHB_GLOBALS), gap=0)


def install_stubs_ghb(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_ghb_installed", False):
        return
    fc._ghb_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x0049F4C0, 2, "engine_0049f4c0", convention="thiscall", this=True)       # CPlatform__Bounce(owner, skater + 0x4c)


GROUP_STUBS["ghb"] = install_stubs_ghb
EXTRA_REGIONS["ghb"] = ghb_regions
SLAB_BYTES["ghb"] = SLAB + 0x100
READ_ONLY["ghb"] = []


def ghb_signed(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


@spec(0x004A12D0, "Platform_HandleBounce", "ghb")
def gen_platform_bounce(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, 0xD0, rng)
    img.garbage(slab + GHB_NODES, 0x200 * 6, rng)
    img.garbage(slab + GHB_ARRAYS, 0x40 * 4, rng)
    img.garbage(slab + GHB_MODELS, 0x40 * 8, rng)
    img.garbage(0x0056D43C, 0x44 * 4, rng)
    img.u32(0x0056A960, rng.choice([0, slab + 0x40, word(rng)]))
    pos = [rng.choice([0, rng.randrange(-0x400000, 0x400000), rng.choice([0x7FFE0000, 0x7FFF0000, -0x7FFE0000, 0x7FFEC000])]) for _ in range(3)]
    old = [p + rng.choice([0, 0, 0, 1, -1, 0x1000, -0x30000, 0x14000, rng.randrange(-0x40000, 0x40000)]) for p in pos]
    for k in range(3):
        img.u32(slab + 8 + 4 * k, pos[k])
        img.u32(slab + 0xBC + 4 * k, old[k])
    low = [ghb_signed(min(ghb_signed(old[k]), ghb_signed(pos[k])) - 0x14000) for k in range(3)]
    high = [ghb_signed(max(ghb_signed(old[k]), ghb_signed(pos[k])) + 0x14000) for k in range(3)]
    count = rng.choice([0, 1, 1, 2, 3, 4, 6])
    models = {}
    img.u32(0x0056AF40, slab + GHB_NODES if count else 0)
    for node in range(count):
        at = slab + GHB_NODES + 0x200 * node
        img.u32(at + 0x20, at + 0x200 if node + 1 < count else 0)
        word4 = rng.getrandbits(16)
        word4 = (word4 | 0x100) if rng.random() < 0.85 else (word4 & ~0x100)
        img.u16(at + 4, word4)
        flags = rng.getrandbits(8)
        flags = (flags | 0x10) if rng.random() < 0.85 else (flags & ~0x10)
        flags = (flags | 0x20) if rng.random() < 0.25 else (flags & ~0x20)
        img.u8(at + 0x178, flags)
        region, index = rng.choice([0, 0, 1, 2, 3]), rng.randrange(0, 8)
        img.u8(at + 0x1F, region)
        img.u16(at + 0x1A, index)
        if (region, index) not in models:
            model = slab + GHB_MODELS + 0x40 * len(models)
            shorts = []
            for k in range(3):
                bottom = rng.randrange(-6, 4)
                top = bottom + rng.choice([0, 1, 2, 5, 6]) if rng.random() < 0.95 else rng.randrange(-8, 8)
                shorts.append((bottom, top))
            for k, (bottom, top) in enumerate(shorts):             # (+0xe/+0x12/+0x16 are the low ends, +0xc/+0x10/+0x14 the high ones)
                img.u16(model + (0xE, 0x12, 0x16)[k], bottom)
                img.u16(model + (0xC, 0x10, 0x14)[k], top)
            models[(region, index)] = (model, shorts)
            img.u32(slab + GHB_ARRAYS + 0x40 * region + 4 * index, model)
            img.u32(0x0056D43C + 0x44 * region, slab + GHB_ARRAYS + 0x40 * region)
        model, shorts = models[(region, index)]
        tight = rng.choice([0, 0, 0, 1, 1, 1, 1, 2])
        tight_axes = rng.sample(range(3), tight) if tight else []
        place = []
        for k in range(3):
            bottom, top = shorts[k]
            reach_low, reach_high = low[k] - (top << 12), high[k] - (bottom << 12)           # (the positions that touch the box)
            if k in tight_axes:
                kind = rng.choice(["hi", "hi", "lo", "lo", "out_hi", "out_lo"])
                d = rng.choice([-1, 0, 1])
                p = {"hi": reach_high + d, "lo": reach_low + d, "out_hi": reach_high + rng.randrange(2, 0x40000), "out_lo": reach_low - rng.randrange(2, 0x40000)}[kind]
            else:
                p = rng.randrange(reach_low, reach_high + 1) if reach_low <= reach_high else reach_low
            place.append(p)
        img.u32(at + 8, place[0])
        img.u32(at + 0xC, place[1])
        img.u32(at + 0x10, place[2])
    return [slab], img.pokes()


# gsr: Skater_GroundSurfaceResponseStep 0x00496360 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and the record its word +0x2ccc points to at 0x3700. The profile stat 0x0048f3a0,
# the level value 0x0048cb60, 0x0049c060 (it writes +0x3124), the turn phase 0x0049b500 and 0x0048ddc0 are stubs.
GSR_RECORD = 0x3700
GSR_GLOBALS = [(0x0056865C, 4)]
GSR_STATE = {"bounce": 0}


def gsr_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GSR_RECORD, 0x40)] + list(GSR_GLOBALS), gap=0)


def install_stubs_gsr(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gsr_installed", False):
        return
    fc._gsr_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    slab = env.slab
    ensure(fc, 0x0048CB60, 1, "engine_0048cb60", convention="thiscall", this=True,
           result=lambda a: rng.choice([0, 1, 2, 5, 9, 10, 11, 12, rng.randrange(0, 15), 0xFFFFFFFF, rng.getrandbits(32)]))
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,
           result=lambda a: rng.choice([0, 3, 5, 10, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11), rng.getrandbits(32)]))
    ensure(fc, 0x0049C060, 1, "engine_0049c060", convention="thiscall", this=True,
           writes=[(slab + 0x3124, 4, lambda a: rng.choice([0, 1, 0xFFFFFFFF, 5, 0x100, 0xFFFFFFF0, rng.getrandbits(32)]))])
    ensure(fc, 0x0049B500, 3, "engine_0049b500", convention="thiscall", this=True)
    ensure(fc, 0x0048DDC0, 0, "engine_0048ddc0", convention="thiscall", this=True)


GROUP_STUBS["gsr"] = install_stubs_gsr
EXTRA_REGIONS["gsr"] = gsr_regions
SLAB_BYTES["gsr"] = SLAB + 0x100
READ_ONLY["gsr"] = []


@spec(0x00496360, "Skater_GroundSurfaceResponseStep", "gsr", this=lambda env: env.slab)
def gen_ground_surface_response(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(slab + GSR_RECORD, 0x40, rng)
    nonzero = lambda: rng.choice([1, 2, 0x80, rng.getrandbits(32) | 1])      # noqa: E731
    zeroish = lambda: 0 if rng.random() < 0.8 else nonzero()                  # noqa: E731
    img.u32(slab + 0x30B8, rng.choice([0, 1, 1, 2, 2, 3, 7, 0xFFFFFFFF]))
    img.u32(slab + 0x30C4, rng.choice([0, 0, 1, 5]))
    for at in (0x2C88, 0x2E80, 0x2C80):
        img.u32(slab + at, zeroish())
    img.u32(slab + 0x2858, rng.choice([0, 0, 1, 2, 3, rng.randrange(1, 11), 0xFFFFFFFF]))
    img.u32(slab + 0x306C, rng.choice([0, 1, 2, 5, 0x7FFFFFFF, rng.randrange(0, 10), rng.getrandbits(32)]))
    units = rng.choice([0, 1, -1, 2, 7, 0x10, 0x100, 0x1000, rng.randrange(-0x400, 0x400), rng.randrange(-0x40000, 0x40000), rng.getrandbits(20) - (1 << 19)])
    img.u32(slab + 0x3144, (units << 12) + rng.choice([0, 0, 0xFFF, 1, 0x800]) if rng.random() < 0.9 else rng.getrandbits(32))
    img.u32(slab + 0x2D90, rng.choice([0, 0, 1, 2, 3, 4, 5, 6, rng.randrange(0, 40), 0xFFFFFFFF, 0x80000000]))
    img.u32(slab + 0x2CCC, slab + GSR_RECORD)
    for at in (0x10, 0x20):
        img.u8(slab + GSR_RECORD + at, rng.choice([0, 0, 0, 1, 0x80]))
    img.u32(slab + 0x2DD4, rng.choice([0, 0, 1]))
    img.u32(slab + 0x3068, rng.choice([0, 0x7FF, 0x800, 0x801, -0x7FF, -0x800, -0x801, 0x7FFFFFFF, 0x80000000, rng.randrange(-0x1000, 0x1000), rng.getrandbits(32)]))
    img.u32(slab + 0x2C84, rng.choice([0, 0, 1]))
    img.u32(0x0056865C, rng.choice([0x100, 0x100, 0x100, 0x80, 0x1000, 0x200, rng.randrange(0, 0x2000), rng.getrandbits(32)]))
    return [], img.pokes()


# gtp: Skater_ApplyTurnVelocityPhase 0x0049b500 (thiscall on the skater: the angle, whether the velocity follows, a second angle). Slab: the skater at 0x0000 (0x3600 bytes). Everything it calls (the matrix of
# three angles, RT x IR, the transpose, the basis copy, the dot product, the root and the vector operators) runs as retail code, so the GTE words are compared.
GTP_GLOBALS = [(0x006A3E00, 0x120)]


def gtp_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES)] + list(GTP_GLOBALS), gap=0)


def install_stubs_gtp(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gtp_installed", False):
        return
    fc._gtp_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()


GROUP_STUBS["gtp"] = install_stubs_gtp
EXTRA_REGIONS["gtp"] = gtp_regions
SLAB_BYTES["gtp"] = SLAB + 0x100
READ_ONLY["gtp"] = []


def gtp_matrix(img, rng, address):
    """Nine shorts of a rotation matrix (mostly a legal one's range, now and then any short)."""
    kind = rng.random()
    for k in range(9):
        if kind < 0.7:
            value = rng.choice([0, 0x1000, -0x1000, 0x800, -0x800, 0xB50, -0xB50, rng.randrange(-0x1000, 0x1001)])
        elif kind < 0.85:
            value = rng.randrange(-0x2000, 0x2001)
        else:
            value = rng.getrandbits(16)
        img.u16(address + 2 * k, value)


def gtp_vector(img, rng, address, scales=(0, 0x400, 0x1000, 0x10000, 0x40000, 0x100000, 0x7FFFFFFF)):
    scale = rng.choice(scales)
    for k in range(3):
        img.u32(address + 4 * k, 0 if rng.random() < 0.1 else rng.randrange(-scale, scale + 1) if scale else 0)


@spec(0x0049B500, "Skater_ApplyTurnVelocityPhase", "gtp", this=lambda env: env.slab)
def gen_turn_velocity_phase(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 0, 0, 0, 0, 0, 1, 0x80000000, rng.getrandbits(32) | 1]))
    gtp_matrix(img, rng, slab + 0x2E58)
    gtp_matrix(img, rng, slab + 0x2E38)
    gtp_vector(img, rng, slab + 0x4C)
    amount = rng.choice([0, 1, -1, 0x7FF, 0x800, 0xFFF, 0x1000, 0x1001, -0x800, 0x400, 0x200, rng.randrange(-0x1000, 0x1000), rng.randrange(-0x20000, 0x20000), rng.getrandbits(32)])
    follow = rng.choice([1, 1, 1, 1, 0, 0, 2, 0x100, 0xFFFFFFFF])
    second = rng.choice([0, 0, 0, 0, 0, 1, 0x800, 0xFFF, -0x400, rng.randrange(-0x1000, 0x1000), rng.getrandbits(32)])
    return [amount & 0xFFFFFFFF, follow, second & 0xFFFFFFFF], img.pokes()


# gsc: Skater_ApplySurfaceResponseCorrection 0x0049c060 (thiscall on the skater, one argument). Slab: the skater at 0x0000 (0x3600 bytes). Stat 3 (0x0048f3a0) is a stub (four calls at most, each answered
# separately); the normalisation, the cross product, the dot product and the root run as retail code.
GSC_GLOBALS = [(0x005685F4, 4), (0x006A3E00, 0x120)]
GSC_STATE = {"p": 0, "targeted": __import__("os").environ.get("GSC_TARGETED") == "1"}      # (targeted: the gscB capture, aimed at the edges the first fixture's mutants survived)


def gsc_limit(stat):
    """The stat's limit as the compiler computes it: 45 * (stat + 0x186) * 0x1000 divided by 280 through the multiplier 0xea0ea0eb."""
    x = (((stat + 0x186) * 45) << 12) & 0xFFFFFFFF
    x = x - (1 << 32) if x & 0x80000000 else x
    high = (-0x15F15F15 * x) >> 32
    q = ((high + x + (1 << 31)) % (1 << 32) - (1 << 31)) >> 8
    return q + (1 if q < 0 else 0)


def gsc_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES)] + list(GSC_GLOBALS), gap=0)


def install_stubs_gsc(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gsc_installed", False):
        return
    fc._gsc_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,
           result=lambda a: GSC_STATE["p"] if GSC_STATE["targeted"] else rng.choice([0, 3, 5, 10, 10, 50, 100, 0x1F4, 0xFFFFFFFF, 0xFFFFFF9C, rng.randrange(0, 120), rng.randrange(0, 11)]))


GROUP_STUBS["gsc"] = install_stubs_gsc
EXTRA_REGIONS["gsc"] = gsc_regions
SLAB_BYTES["gsc"] = SLAB + 0x100
READ_ONLY["gsc"] = []


@spec(0x0049C060, "Skater_ApplySurfaceResponseCorrection", "gsc", this=lambda env: env.slab)
def gen_surface_response_correction(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    clock = rng.choice([0, 100, 0x1000, rng.randrange(0, 0x100000), 0x7FFFFFF0, 0xFFFFFFF0])
    img.u32(0x005685F4, clock)
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 0, 0, 0, 0, 1, rng.getrandbits(32) | 1]))
    gtp_vector(img, rng, slab + 0x4C, scales=(0, 0x1000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000))
    gtp_vector(img, rng, slab + 0x3118, scales=(0, 0x400, 0x1000, 0x1000, 0x1000, 0x7000, 0x10000, 0x7FFFFFFF))
    gtp_vector(img, rng, slab + 0x30F4, scales=(0x1000, 0x1000, 0x1000, 0x400, 0x10000))
    normal = [rng.choice([0, 0x1000, -0x1000, 0xB50, -0xB50, 0x800, -0x800, rng.randrange(-0x1000, 0x1001)]) for _ in range(3)]
    img.u32(slab + 0x3128, (normal[0] & 0xFFFF) | ((normal[1] & 0xFFFF) << 16))
    img.u32(slab + 0x312C, (normal[2] & 0xFFFF) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0))
    img.u32(slab + 0x2E24, rng.choice([0, 0, 1, 1, 2, 2, 3, rng.getrandbits(32)]))
    img.u32(slab + 0x2E28, (clock + rng.choice([0, -0xA, -0xB, -9, -1, 1, -100, 0x20, rng.randrange(-0x40, 0x40), rng.getrandbits(32)])) & 0xFFFFFFFF)
    argument = rng.choice([0x28, 0x28, 0x28, 0, 1, 5, -3, 0x100, rng.randrange(-0x80, 0x80), rng.getrandbits(32)])
    if GSC_STATE["targeted"]:
        # a stat whose limit is a multiple of 64 and a velocity along an axis whose speed is the limit or a step off it; the turn vector along z, the normal's diagonal (0, 0x1000, z) so that the cross
        # product is (0x1000, 0, 0) and the side is the slope vector's x, put on and around +-0x29
        stat = rng.choice([s for s in range(0, 200) if gsc_limit(s) % 64 == 0])
        GSC_STATE["p"] = stat
        img.u32(slab + 0x2F64, 0)
        steps = max(0, gsc_limit(stat) // 64 + rng.choice([-2, -1, 0, 0, 0, 0, 1, 2, 5, -40, 100]))
        velocity = [0, 0, 0]
        velocity[rng.randrange(3)] = 64 * steps * rng.choice([1, 1, -1])
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, velocity[k])
        img.u32(slab + 0x3118, 0)
        img.u32(slab + 0x311C, 0)
        img.u32(slab + 0x3120, rng.choice([0x800, 0x1000, 0x2000, 0x4000, 0x7000, rng.randrange(0x600, 0x7000)]))
        img.u32(slab + 0x3128, 0x1000 << 16)
        img.u32(slab + 0x312C, rng.getrandbits(16))
        side = rng.choice([0x27, 0x28, 0x29, 0x2A, 0x2B, -0x27, -0x28, -0x29, -0x2A, -0x2B, 0, 0x100, -0x100])
        img.u32(slab + 0x30F4, side)
        img.u32(slab + 0x30F8, rng.randrange(-0x100, 0x100))
        img.u32(slab + 0x30FC, rng.randrange(-0x100, 0x100))
        img.u32(slab + 0x2E24, rng.choice([0, 1, 2, 0, 1, 2, 3]))
        argument = rng.choice([0x28, 0x28, -0x28, -1, -3, 0, 1, -0x10, 0x10, rng.randrange(-0x40, 0x40)])
    return [argument & 0xFFFFFFFF], img.pokes()


# gpw: Skater_PositionWritePath 0x00496060 (thiscall on the skater: x, y, z). Slab: the skater at 0x0000 (0x3600 bytes). The query init 0x004624d0 and the zone query 0x00466090 are stubs; the query's
# record is a stack local, handed on at GPF_QUERY with its first 0x18 bytes recorded, and the body the line met (+0x68) is scripted: a case draws how often a query meets something.
GPW_GLOBALS = [(0x00567C7C, 4)]
GPW_STATE = {"hit": 0.5}


def gpw_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES)] + list(GPW_GLOBALS), gap=0)


def install_stubs_gpw(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpw_installed", False):
        return
    fc._gpw_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GPF_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, lambda a: rng.choice([1, 0x1234, rng.getrandbits(32) | 1]) if rng.random() < GPW_STATE["hit"] else 0, 0x68)])


GROUP_STUBS["gpw"] = install_stubs_gpw
EXTRA_REGIONS["gpw"] = gpw_regions
SLAB_BYTES["gpw"] = SLAB + 0x100
READ_ONLY["gpw"] = []


@spec(0x00496060, "Skater_PositionWritePath", "gpw", this=lambda env: env.slab)
def gen_position_write_path(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.u32(0x00567C7C, rng.choice([0, 1, 0, 2, rng.getrandbits(32)]))
    img.u32(slab + 0x3200, rng.choice([0, 0, 0, 0, 0, 0, 0, 1, rng.getrandbits(32) | 1]))
    GPW_STATE["hit"] = rng.choice([0.0, 0.3, 0.6, 0.8, 0.9, 1.0])
    return [word(rng) for _ in range(3)], img.pokes()


# ---- 10g session 11: the collision response, the orientation recovery, the exit from the ground, the air hit and the landing ----

def q12_direction(rng, scale=0x1000):
    """A direction as three Q12 components of about `scale` in length (axes and diagonals now and then, any shorts rarely)."""
    import math

    pick = rng.random()
    if pick < 0.25:
        axis = rng.randrange(3)
        v = [0, 0, 0]
        v[axis] = scale * rng.choice([1, -1])
        return v
    if pick < 0.35:
        return [rng.choice([-1, 1]) * int(scale * 0.7071) for _ in range(2)] + [0]
    if pick < 0.95:
        while True:
            c = [rng.gauss(0, 1) for _ in range(3)]
            length = math.sqrt(sum(x * x for x in c))
            if length > 0.05:
                return [int(round(x / length * scale)) for x in c]
    return [rng.randrange(-0x2000, 0x2000) for _ in range(3)]


def put_normal_args(rng, normal):
    """The normal as the two stack arguments of the response and recovery functions: x and y packed, z (the high half is ignored)."""
    xy = (normal[0] & 0xFFFF) | ((normal[1] & 0xFFFF) << 16)
    z = (normal[2] & 0xFFFF) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)
    return xy, z


def put_vector(img, address, v):
    for k in range(3):
        img.u32(address + 4 * k, v[k])


def install_stubs_flat(group):
    """A group whose callees all run as retail code."""
    def install(fc, env):
        from core_specs_main import lock_scheduler
        from platform_specs import mark_under_test, stop_clock

        if getattr(fc, "_%s_installed" % group, False):
            return
        setattr(fc, "_%s_installed" % group, True)
        mark_under_test()
        stop_clock(fc)
        lock_scheduler()
    return install


# gcr: Skater_ApplyCollisionResponse 0x0049bad0 (thiscall on the skater: the normal as two arguments, a bump angle). Slab: the skater at 0x0000 (0x3600 bytes). Everything it calls (the dot product, the Q12
# product, the root, the matrix of three angles, RT x IR, the normalisations and the cross products, the basis conversions) runs as retail code, so the GTE words are compared.
GROUP_STUBS["gcr"] = install_stubs_flat("gcr")
EXTRA_REGIONS["gcr"] = gtp_regions
SLAB_BYTES["gcr"] = SLAB + 0x100
READ_ONLY["gcr"] = []


@spec(0x0049BAD0, "Skater_ApplyCollisionResponse", "gcr", this=lambda env: env.slab)
def gen_collision_response(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    normal = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x1000, 0x800, 0x1010, 0x400]))
    # the velocity: into the surface (a negative dot with the normal), away from it, or along it
    scale = rng.choice([0, 0x400, 0x1000, 0x10000, 0x40000, 0x100000])
    velocity = q12_direction(rng, scale) if scale else [0, 0, 0]
    if rng.random() < 0.1:
        velocity = [rng.randrange(-0x1000000, 0x1000000) for _ in range(3)]
    put_vector(img, slab + 0x4C, velocity)
    # the forward vector (+0x30f4) against the reversed normal: mostly along the normal (a negative dot with the reversed one), so that the turn runs; the right vector (+0x3100) decides the side
    if rng.random() < 0.55:
        forward = [int(c * rng.uniform(0.2, 1.0)) + rng.randrange(-0x300, 0x300) for c in normal]
    else:
        forward = q12_direction(rng)
    put_vector(img, slab + 0x30F4, forward)
    put_vector(img, slab + 0x3100, q12_direction(rng))
    put_vector(img, slab + 0x310C, q12_direction(rng))
    gtp_matrix(img, rng, slab + 0x2E58)
    xy, z = put_normal_args(rng, normal)
    bump = rng.choice([0, 0, 0x19, 0x19, 0x1, 0x400, 0x3FF, 0x401, 0xFFF, 0x1000, 0x1019, rng.randrange(0, 0x800), rng.getrandbits(32)])
    return [xy, z, bump], img.pokes()


# grr: Skater_RebuildOrientationRecovery 0x0049d080 (thiscall on the skater). Slab: the skater at 0x0000. The normalisation and the cross products run as retail code (the GTE words are compared).
GROUP_STUBS["grr"] = install_stubs_flat("grr")
EXTRA_REGIONS["grr"] = gtp_regions
SLAB_BYTES["grr"] = SLAB + 0x100
READ_ONLY["grr"] = []


@spec(0x0049D080, "Skater_RebuildOrientationRecovery", "grr", this=lambda env: env.slab)
def gen_rebuild_orientation(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    progress = rng.choice([0x18000, 0x18000, 0x18000, 0x17FFF, 0x10000, 0, 0x1000, 0x18001, 0x20000, -1, -0x18000, 0x7FFFFFFF, 0x80000000, rng.randrange(0, 0x18000), rng.getrandbits(32)])
    img.u32(slab + 0x3130, progress)
    ground = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x800, 0x1400, 0x10]))
    for k in range(3):
        img.u16(slab + 0x80 + 2 * k, ground[k])
    saved = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x400, 0x2000]))
    if rng.random() < 0.1:
        saved = [rng.getrandbits(32) for _ in range(3)]
    put_vector(img, slab + 0x3134, saved)
    if rng.random() < 0.7:
        gtp_matrix(img, rng, slab + 0x2E58)
    return [], img.pokes()


# gxr: Skater_ExitGroundCollisionRecovery 0x004956f0 (thiscall on the skater). Slab: the skater at 0x0000. The state transition, the take-off and the animation request are stubs; the trick script's start
# (0x00491b80, flat) runs as retail code.
def install_stubs_gxr(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gxr_installed", False):
        return
    fc._gxr_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004900B0, 2, "engine_004900b0", convention="thiscall", this=True)
    ensure(fc, 0x0048F5F0, 0, "engine_0048f5f0", convention="thiscall", this=True)
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)


GROUP_STUBS["gxr"] = install_stubs_gxr
EXTRA_REGIONS["gxr"] = lambda slab: merged([(slab, GSU_SKATER_BYTES)], gap=0)
SLAB_BYTES["gxr"] = SLAB + 0x100
READ_ONLY["gxr"] = []


@spec(0x004956F0, "Skater_ExitGroundCollisionRecovery", "gxr", this=lambda env: env.slab)
def gen_exit_ground_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.u32(slab + 0x30B8, rng.choice([2, 2, 0, 1, 1, 3, 4, rng.getrandbits(32)]))
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x29D0, rng.choice([0, 0x536BC0, 0x1234, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x2DD4, rng.choice([0, 1, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x2DD8, rng.choice([0, 1, rng.getrandbits(32)]))
    return [], img.pokes()


# gar: Skater_AirCollisionOrientationRecovery 0x00491780 (thiscall on the skater: the normal as two arguments). Slab: the skater at 0x0000. The normal removal (0x00490610), the vector helpers, the
# normalisations, the cross products and the basis publication run as retail code; the flip test, the bail, the cheat and the gap definition are stubs.
GAR_GLOBALS = [(0x0056A890, 4), (0x0056B7EC, 8), (0x006A3E00, 0x120)]


def install_stubs_gar(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gar_installed", False):
        return
    fc._gar_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x0046D970, 0, "engine_0046d970", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 0x100]))
    ensure(fc, 0x004904D0, 2, "engine_004904d0", convention="thiscall", this=True)
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0] * 6 + [1, 0x100]))
    ensure(fc, 0x0048F690, 4, "engine_0048f690", convention="thiscall", this=True)


GAR_TARGETED = __import__("os").environ.get("GAR_TARGETED") == "1"      # (the garB capture, aimed at the edges the first fixture's mutants survived)
GROUP_STUBS["gar"] = install_stubs_gar
EXTRA_REGIONS["gar"] = lambda slab: merged([(slab, GSU_SKATER_BYTES)] + list(GAR_GLOBALS), gap=0)
SLAB_BYTES["gar"] = SLAB + 0x100
READ_ONLY["gar"] = []


def cross3(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def scaled(v, scale):
    import math

    length = math.sqrt(sum(x * x for x in v))
    if length == 0:
        return [0, 0, 0]
    return [int(round(x / length * scale)) for x in v]


@spec(0x00491780, "Skater_AirCollisionOrientationRecovery", "gar", this=lambda env: env.slab)
def gen_air_collision_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    normal = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x1000, 0x800, 0x1010]))
    xy, z = put_normal_args(rng, normal)
    speed = rng.choice([0x2000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x7FFF0000])
    velocity = q12_direction(rng, speed)
    put_vector(img, slab + 0x4C, velocity)
    # the forward vector against the velocity once the normal is off both: parallel (a cosine over 0xfc1), at right angles (under 0x666), or free
    kind = rng.random()
    if kind < 0.35:
        forward = scaled(cross3(normal, velocity), 0x1000)
        forward = [c + rng.choice([0, 0, rng.randrange(-0x40, 0x40), rng.randrange(-0x180, 0x180)]) for c in forward]
    elif kind < 0.55:
        forward = scaled(velocity, 0x1000)
        forward = [c + rng.choice([0, 0, rng.randrange(-0x10, 0x10)]) for c in forward]
    else:
        forward = q12_direction(rng)
    put_vector(img, slab + 0x30F4, forward)
    put_vector(img, slab + 0x3100, q12_direction(rng))
    img.u32(slab + 0x3074, rng.choice([0, 0, 0, 1, rng.getrandbits(32)]))
    img.u32(0x0056A890, rng.choice([0, 0, 0, 0, 1, rng.getrandbits(32)]))
    img.u32(slab + 0xD8, rng.choice([0, 2, 3, 0xFFFFFFFF, rng.getrandbits(32)]))
    # the move since the saved position: small, large (past the 0x8000 sum), zero
    saved = [rng.randrange(-0x100000, 0x100000) for _ in range(3)]
    move = rng.choice([[0, 0, 0], [rng.randrange(-0x1000, 0x1000) for _ in range(3)], [rng.randrange(-0x20000, 0x20000) for _ in range(3)], [rng.randrange(-0x2000, 0x2000), 0, 0],
                       [0x2000, 0x3000, 0x3000], [0x2000, 0x3000, 0x2FFF], [0x2000, 0x3000, 0x3001], [rng.getrandbits(32) for _ in range(3)]])
    put_vector(img, slab + 0x2E00, saved)
    put_vector(img, slab + 0xBC, [saved[k] + move[k] for k in range(3)])
    # the tail: the height table
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 0, 0, 1]))
    img.u32(slab + 0x30C4, rng.choice([0, 0, 0, 0, 1]))
    img.u32(0x0056B7EC, rng.choice([0, 0, 0, 0, 1]))
    img.u32(0x0056B7F0, rng.choice([0, 0, 0, 0, 1]))
    level = rng.choice([0, 0, 1, 5, 10, 11, 12, 13, -1, rng.randrange(0, 12), rng.getrandbits(32)])
    img.u32(slab + 0x2DE8, level)
    base = rng.randrange(-0x3000, 0x3000)
    signed_level = (level & 0xFFFFFFFF) - (1 << 32) if level & 0x80000000 else level
    limit = base + (11 - signed_level) * 60 if level else base
    height = rng.choice([limit, limit + 1, limit - 1, base, base + 1, base - 1, limit + 0x100, base - 0x100, rng.randrange(-0x4000, 0x4000), (limit + base) // 2])
    total = (height << 12) + rng.choice([0, 0, 0xFFF, 0x800, 1])
    part = rng.choice([0, 0, rng.randrange(-0x100000, 0x100000)])
    img.u32(slab + 0x2F3C, base)
    img.u32(slab + 0x2F38, total - part)
    img.u32(slab + 0x2F40, part)
    if GAR_TARGETED:
        # the normal straight up, the velocity along x (its speed on the 0x1e000 edge now and then), the forward vector in the plane with its cosine on one of the three edges; the gates before the turn open
        import math

        xy, z = 0x1000 << 16, 0
        speed = rng.choice([0x1E000, 0x1E000, 0x1E001, 0x1E03F, 0x1E040, 0x1DFFF, 0x1DFC0]) if rng.random() < 0.4 else rng.choice([0x40000, 0x80000])
        put_vector(img, slab + 0x4C, [speed * rng.choice([1, -1]), 0, 0])
        edge = rng.choice([0xFC0, 0xFC1, 0xFC2, 0xB4E, 0xB4F, 0xB50, 0xB51, 0x665, 0x666, 0x667])
        cosine = edge + rng.choice([0, 0, 0, 1, -1])
        side = int(round(math.sqrt(0x1000 * 0x1000 - cosine * cosine)))
        put_vector(img, slab + 0x30F4, [cosine * rng.choice([1, -1]), rng.choice([0, 0, rng.randrange(-0x800, 0x800)]), side * rng.choice([1, -1])])
        img.u32(slab + 0x3074, 0)
        img.u32(0x0056A890, 0)
    return [xy, z], img.pokes()


# glc: Skater_LandingCleanup 0x004914d0 (thiscall on the skater). Slab: the skater at 0x0000; the byte the ground flag pointer (+0x2ccc) names is inside it. The rail check, the trick sequence selection, the grind
# and the bail are stubs (the stubs write nothing: the landing trick +0x3064 is the case's).
def install_stubs_glc(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_glc_installed", False):
        return
    fc._glc_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004AAF70, 1, "engine_004aaf70")
    ensure(fc, 0x004925E0, 0, "engine_004925e0", convention="thiscall", this=True)
    ensure(fc, 0x00490EF0, 1, "engine_00490ef0", convention="thiscall", this=True)
    ensure(fc, 0x004904D0, 2, "engine_004904d0", convention="thiscall", this=True)


GLC_TARGETED = __import__("os").environ.get("GLC_TARGETED") == "1"      # (the glcB capture, aimed at the edges the first fixture's mutants survived)
GROUP_STUBS["glc"] = install_stubs_glc
EXTRA_REGIONS["glc"] = lambda slab: merged([(slab, GSU_SKATER_BYTES)], gap=0)
SLAB_BYTES["glc"] = SLAB + 0x100
READ_ONLY["glc"] = []


@spec(0x004914D0, "Skater_LandingCleanup", "glc", this=lambda env: env.slab)
def gen_landing_cleanup(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    flag = slab + 0x3500 + rng.randrange(0x40)
    img.u32(slab + 0x2CCC, flag)
    img.u8(flag, rng.choice([1, 1, 1, 1, 0, 0xFF, 0xFE]))
    img.u32(slab + 0x2E90, rng.choice([0, 0, 0, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x2EC4, rng.choice([0, 1, 5, rng.getrandbits(32)]))
    img.u32(slab + 0x2EC0, rng.choice([0, 0, 0, 0, 0, -1, 1, 2, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 0, 0, 0, 1]))
    state = rng.choice([0, 0, 1, 2, 2, 3, rng.getrandbits(32)])
    img.u32(slab + 0x30B8, state)
    img.u32(slab + 0x50, rng.choice([-0x1000, -1, 0, 1, 0x1000, rng.randrange(-0x20000, 0x20000), rng.getrandbits(32)]))
    img.u32(slab + 0x2E34, rng.choice([0, 0, 0, 0, 1]))
    img.u32(slab + 0x2E94, rng.choice([1, 1, 1, 0, rng.getrandbits(32)]))
    img.u32(slab + 0x2C88, rng.choice([0, 1, 1, 2, rng.getrandbits(32)]))
    phase_edge = rng.choice([0x12C, 0xED4])
    img.u32(slab + 0x2C8C, rng.choice([phase_edge, phase_edge - 1, phase_edge + 1, phase_edge + 0x1000, phase_edge - 0x1000, 0, 0xFFF, rng.getrandbits(32)]))
    img.u32(slab + 0x2C90, rng.choice([0, 1, -1, 5, 0x80000000, rng.getrandbits(32)]))
    img.u32(slab + 0x2C68, rng.choice([0, 1, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x2C6C, rng.choice([0, 0, 1, rng.getrandbits(32) | 1]))
    window = rng.choice([0, 0, 1, 2, 5, 0x10, -1, -3, -0x10, rng.randrange(-0x40, 0x40), rng.getrandbits(32)])
    img.u32(slab + 0x2E2C, window)
    mode = rng.choice([0, 1, 1, 2, 0xFF])
    img.u8(slab + 0x100, mode)
    edge = rng.randrange(-128, 128)
    img.u8(slab + 0x101, edge & 0xFF)
    signed_window = (window & 0xFFFFFFFF) - (1 << 32) if window & 0x80000000 else window
    target = edge - signed_window if mode == 1 else edge + signed_window
    img.u16(slab + 0xF4, rng.choice([target, target - 1, target + 1, target, edge, rng.getrandbits(16), rng.randrange(-200, 200)]))
    img.u32(slab + 0x3064, rng.choice([0, 1, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x3110, rng.choice([0, 0xBB7, 0xBB8, -0xBB7, -0xBB8, 0x100, -0x100, 0x80000000, rng.getrandbits(32)]))
    img.u32(slab + 0x30F8, rng.choice([0x800, 0x801, 0x7FF, 0x1000, -0x1000, 0, rng.getrandbits(32)]))
    img.u16(slab + 0x82, rng.choice([0x5DB, 0x5DC, -0x5DB, -0x5DC, 0, 0x100, 0x8000, rng.getrandbits(16)]))
    img.u32(slab + 0x3104, rng.choice([0x7FF, 0x800, -0x7FF, -0x800, 0, 0x100, 0x80000000, rng.getrandbits(32)]))
    if GLC_TARGETED:
        # every gate open up to the grind flag (or the turn phase on its edge, with the bail certain): one of the flag's four tests sits on its edge, the others pass
        img.u32(slab + 0x2E90, 0)
        img.u32(slab + 0x2EC0, rng.choice([0, 0, -1, 0x80000000]))
        img.u32(slab + 0x2F64, 0)
        img.u32(slab + 0x2E34, 0)
        img.u32(slab + 0x2E94, 1)
        img.u32(slab + 0x30B8, rng.choice([0, 1, 3]))
        img.u8(flag, 1)
        img.u32(slab + 0x3064, 1)
        if rng.random() < 0.25:
            edge_phase = rng.choice([0xED4, 0x12C])
            img.u32(slab + 0x2C88, 1)
            img.u32(slab + 0x2C90, 1 if edge_phase == 0xED4 else rng.choice([-1, 0, -5]))
            img.u32(slab + 0x2C8C, edge_phase + rng.choice([0, 0, 1, -1]) + rng.choice([0, 0x1000, -0x1000]))
            img.u32(slab + 0x2C68, 0)
            img.u32(slab + 0x2E2C, 0)
        else:
            img.u32(slab + 0x2C68, 0)
            img.u32(slab + 0x2C88, 0)
        img.u32(slab + 0x50, rng.choice([-1, -0x1000, -0x20000]))
        values = {"turn": (0x3110, [0xBB7, 0xBB8, 0xBB9, -0xBB7, -0xBB8, -0xBB9, 0, 0x100]),
                  "slope": (0x30F8, [0x7FF, 0x800, 0x801, 0x1000, 0x80000001, 0xFFFFF000, -0x1000, 0]),
                  "lean": (0x82, [0x5DB, 0x5DC, 0x5DD, -0x5DB, -0x5DC, -0x5DD, -0x1000, 0, 0x7000]),
                  "side": (0x3104, [0x7FF, 0x800, 0x801, -0x7FF, -0x800, -0x801, -0x1000, 0, 0x80000000])}
        passing = {"turn": 0x100, "slope": 0x1000, "lean": 0x100, "side": 0x100}
        focus = rng.choice(list(values))
        for name, (offset, choices) in values.items():
            value = rng.choice(choices) if name == focus else (passing[name] if rng.random() < 0.9 else rng.choice(choices))
            (img.u16 if name == "lean" else img.u32)(slab + offset, value)
    return [], img.pokes()


# ---- 10g session 12: the sway after a ground collision, the grounded bounce probe and the air landing's orientation recovery ----

# gpq: Skater_GroundPostCollision 0x00495cc0 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes) and the pad profile at 0x3700 (the bytes +0xa0 and +0xb0). The point timer, the stat, the cheat
# query, the random numbers, the bail, the pending landing and the animation request are stubs; the trick script's start (0x00491b80, flat) runs as retail code. A case draws one stat for the whole call
# (now and then another) so that the speed edges of the sway can be built; a time scale of 0 keeps the balance where the case put it, for the 4000 edge.
GPQ_PROFILE = 0x3700
GPQ_GLOBALS = [(0x0056865C, 4), (0x0056A93C, 4)]
GPQ_STATE = {"stat": 5}


def install_stubs_gpq(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gpq_installed", False):
        return
    fc._gpq_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    ensure(fc, 0x0048F720, 1, "engine_0048f720", convention="thiscall", this=True)
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,
           result=lambda a: GPQ_STATE["stat"] if rng.random() < 0.9 else rng.choice([0, 3, 5, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11)]))
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0] * 6 + [1, 0x100]))
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.choice([0, 1, 2, 3, 0xFFFFFFFF, rng.randrange(0, 40), rng.getrandbits(32)]))
    ensure(fc, 0x004904D0, 2, "engine_004904d0", convention="thiscall", this=True)
    ensure(fc, 0x0048F7C0, 0, "engine_0048f7c0", convention="thiscall", this=True)
    ensure(fc, 0x004903F0, 2, "engine_004903f0", convention="thiscall", this=True)


GROUP_STUBS["gpq"] = install_stubs_gpq
EXTRA_REGIONS["gpq"] = lambda slab: merged([(slab, GSU_SKATER_BYTES), (slab + GPQ_PROFILE, 0x100)] + list(GPQ_GLOBALS), gap=0)
SLAB_BYTES["gpq"] = SLAB + 0x100
READ_ONLY["gpq"] = []


@spec(0x00495CC0, "Skater_GroundPostCollision", "gpq", this=lambda env: env.slab)
def gen_ground_post_collision(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(slab + GPQ_PROFILE, 0x100, rng)
    stat = rng.choice([0, 3, 5, 5, 10, 10, 0x1F4, -1, -30, 100, 1000, 1, 11, 21, 103, 203, rng.randrange(0, 11)])      # (103 and 203: the products 3300 * stat and 3301 * stat cross a multiple of 10000; 1, 11: the limit's 200 and 201)
    GPQ_STATE["stat"] = stat & 0xFFFFFFFF
    limit = int((200 - stat) * 1000 / 10000)
    img.u32(slab + 0x2DD4, rng.choice([1, 1, 1, 2, 2, 2, 0, 3, rng.getrandbits(32)]))
    img.u32(slab + 0x2F24, rng.choice([0, 0, 100, 199, 200, 201, 5000, 0x80000000, rng.getrandbits(32)]))
    img.u32(slab + 0x2F20, rng.choice([0, 0x1000, 0x3000, 0x100000, -0x1000, rng.randrange(-0x80000, 0x80000), rng.getrandbits(32)]))
    img.u32(0x0056A93C, rng.choice([0, 0x1000, 0x100, rng.getrandbits(16), rng.getrandbits(32)]))
    img.u32(0x0056865C, rng.choice([0, 0, 0, 0x100, 0x80, 0x1000, 0x200, -0x100, rng.randrange(-0x400, 0x400)]))
    balance = rng.choice([0, 0x1000, -0x1000, 0xFA0, 0xFA1, 0xF9F, -0xFA0, -0xFA1, -0xF9F, 0x10000, -0x10000, 0x3000, -0x3000, rng.randrange(-0x40000, 0x40000), rng.getrandbits(32), 0x80000000, 0x7FFFFFFF])
    img.u32(slab + 0x2F14, balance)
    sway = rng.choice([0, 0, 1, -1, 5, limit - 1, limit, limit + 1, -(limit - 1), -limit, -(limit + 1), 100, -100, 0x1000, rng.randrange(-0x400, 0x400), rng.getrandbits(32), 0x80000000])
    img.u32(slab + 0x2F18, sway)
    img.u32(slab + 0x2F10, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(slab + 0x2F00, rng.getrandbits(32))
    img.u32(slab + 0x2CCC, slab + GPQ_PROFILE)
    flags = rng.random()
    img.u8(slab + GPQ_PROFILE + 0xA0, 0 if flags < 0.7 else rng.choice([1, 0xFF, 0x80]))
    img.u8(slab + GPQ_PROFILE + 0xB0, 0 if flags > 0.3 else rng.choice([1, 0xFF, 0x80]))
    img.u8(slab + 0x31A2, rng.choice([-41, -40, -39, 0, 39, 40, 41, -128, 127, rng.getrandbits(8)]) & 0xFF)
    img.u16(slab + 0x29F0, rng.choice([0, 0, 5, 0xFFF0, 0x8000, rng.getrandbits(16)]))
    img.u32(slab + 0x29D0, rng.choice([0, 0, 0x536BC0, 0x1234, rng.getrandbits(32) | 1]))
    return [], img.pokes()


# gbp: Skater_GroundedBounceProbe 0x004957c0 (thiscall on the skater: the side, the distance, whether to respond). Slab: the skater at 0x0000 and two surface records at 0x3a00 / 0x3a40 (words +0 and +0xc).
# The vector length, the dot product and M3D_ReadSurfaceFlags run as retail code (the GTE words and the surface flags are compared); the zone queries (the record is a stack local, recorded at the scratch
# address the port uses, with the hit's fields scripted: two hits per case), the response, the turn phase, the animation, the sound and the log are stubs.
GBP_SURFACE = 0x3A00
GBP_GLOBALS = [(0x0056B760, 0x90), (0x005614C4, 4), (0x006A3E00, 0x120)]
GBP_STATE = {"hits": [], "n": 0, "cur": None}


def gbp_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GBP_SURFACE, 0x80)] + list(GBP_GLOBALS), gap=0)


def gbp_next(a):
    state = GBP_STATE
    state["cur"] = state["hits"][min(state["n"], len(state["hits"]) - 1)]
    state["n"] += 1
    return state["cur"]["body"]


def gbp_field(name):
    return lambda a: GBP_STATE["cur"][name]


def install_stubs_gbp(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gbp_installed", False):
        return
    fc._gbp_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    for address, args in ((0x0048F7C0, 0), (0x00496300, 0), (0x004903F0, 2), (0x0049BAD0, 3), (0x0049B500, 3)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x0046E1D0, 1, "engine_0046e1d0")
    ensure(fc, 0x004CF850, 2, "engine_004cf850")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GPF_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GPF_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, gbp_next, 0x68), (0, 4, gbp_field("x"), 0x6C), (0, 4, gbp_field("y"), 0x70), (0, 4, gbp_field("z"), 0x74), (0, 4, gbp_field("w78"), 0x78),
                  (0, 4, gbp_field("w7c"), 0x7C), (0, 4, gbp_field("distance"), 0x40), (0, 4, gbp_field("surface"), 0x80)])


GROUP_STUBS["gbp"] = install_stubs_gbp
EXTRA_REGIONS["gbp"] = gbp_regions
SLAB_BYTES["gbp"] = SLAB + 0x100
READ_ONLY["gbp"] = []


def gbp_hit(rng, slab, normal, hit):
    """One scripted hit: the normal as the record holds it (x and y packed, z with garbage above); `hit` False leaves the body (+0x68, the test the probe makes) 0, and a hit now and then has a normal with x and y 0."""
    xy = (normal[0] & 0xFFFF) | ((normal[1] & 0xFFFF) << 16)
    if rng.random() < 0.06:
        xy = 0
    z = (normal[2] & 0xFFFF) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)
    offset = lambda: rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x40000, 0x40000)])      # noqa: E731
    return {"body": rng.choice([1, 0x1234, 0x1234, rng.getrandbits(32) | 1, 0x10000, 0x80000000, 0x12340000]) if hit else 0, "x": offset() & 0xFFFFFFFF, "y": offset() & 0xFFFFFFFF, "z": offset() & 0xFFFFFFFF, "w78": xy, "w7c": z,
            "distance": rng.choice([0, 1, 0x10, 0x100, rng.getrandbits(32)]), "surface": slab + GBP_SURFACE + rng.choice([0, 0x40])}


@spec(0x004957C0, "Skater_GroundedBounceProbe", "gbp", this=lambda env: env.slab)
def gen_grounded_bounce_probe(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    img.u32(0x005614C4, rng.choice([0, 1, 1, 0x100, 0x01000000, 0x00ABCD01, 0x00ABCD00]))      # (the flag is a byte: the bytes above it are not part of it)
    pos = [rng.choice([0, rng.randrange(-0x400000, 0x400000)]) & 0xFFFFFFFF for _ in range(3)]
    for k in range(3):
        img.u32(slab + 0x08 + 4 * k, pos[k])
        img.u32(slab + 0xBC + 4 * k, rng.choice([pos[k], rng.getrandbits(32)]))
    kind = rng.random()
    aimed = 0.3 <= kind < 0.6           # (the head-on test: the gates open, the speed past 6 units)
    sign = rng.choice([1, -1])
    if kind < 0.3:
        # the dot of the normal with the up vector on its two edges (up straight down: the dot is minus the normal's y)
        up, normal = (0, -0x1000, 0), (0x1000 * sign, rng.choice([0, 0x3FF, 0x400, 0x401, -0x3FF, -0x400, -0x401, rng.randrange(-0x500, 0x500)]), rng.choice([0, 0, 0x200]))
        forward = q12_direction(rng)
    elif kind < 0.6:
        # the normal's dot with the forward vector on the edge 0xb50 (the normal along an axis)
        up, normal = (0, -0x1000, 0), rng.choice([(0x1000 * sign, 0, 0), (0, 0, 0x1000 * sign), (0x1000 * sign, 0, 0)])
        axis = 0 if normal[0] else 2
        forward = [rng.randrange(-0x200, 0x200), rng.randrange(-0x200, 0x200), rng.randrange(-0x200, 0x200)]
        forward[axis] = sign * rng.choice([0xB4E, 0xB4F, 0xB50, 0xB51, 0xB52, 0x1000, 0xC00, 0x800])
    else:
        up, forward = q12_direction(rng), q12_direction(rng)
        normal = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x800, 0x1010]))
        if rng.random() < 0.4:
            normal = scaled(cross3(up, q12_direction(rng)), 0x1000)
    put_vector(img, slab + 0x310C, [c & 0xFFFFFFFF for c in up])
    put_vector(img, slab + 0x30F4, [c & 0xFFFFFFFF for c in forward])
    put_vector(img, slab + 0x3100, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    # the velocity: its length in whole units on the edge 6 now and then
    if rng.random() < (0.85 if aimed else 0.5):
        speed = rng.choice([7, 7, 8] if aimed and rng.random() < 0.6 else [5, 6, 7]) << 12
        velocity = [speed + rng.choice([0, 0, -1, 0xFFF, rng.randrange(0, 0x100)]), rng.choice([0, 0, rng.randrange(-0xFF, 0x100)]), rng.choice([0, 0, rng.randrange(-0xFF, 0x100)])]
        if rng.random() < 0.3:
            velocity = [velocity[1], velocity[2], velocity[0]]
    else:
        velocity = rng.choice([[0, 0, 0], q12_direction(rng, rng.choice([0x2000, 0x10000, 0x80000])), [rng.getrandbits(32) for _ in range(3)]])
    put_vector(img, slab + 0x4C, [c & 0xFFFFFFFF for c in velocity])
    img.u16(slab + 0xF6, rng.choice([0xC, 0xC, 0, 0x82, 5, 0x10C, 0xFF0C, rng.getrandbits(16)]))
    open_gates = rng.random() < (0.9 if aimed else 0.6)
    img.u32(slab + 0x2F64, 0 if open_gates or rng.random() < 0.7 else rng.choice([1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x2C74, 0 if open_gates or rng.random() < 0.7 else rng.choice([1, rng.getrandbits(32) | 1]))
    if open_gates:
        img.u16(slab + 0xF6, rng.choice([0, 5, 0x82, 0xB, 0xD, 0x10C, 0xFF0C, 0x80C]))
    for at in (GBP_SURFACE, GBP_SURFACE + 0x40):
        img.u32(slab + at, rng.choice([0, 0x80, rng.getrandbits(32)]))
        img.u32(slab + at + 0xC, gdg_surface_flags(rng))
    first = gbp_hit(rng, slab, normal, rng.random() > 0.15)
    second = gbp_hit(rng, slab, q12_direction(rng), rng.random() < 0.5)
    GBP_STATE["hits"] = [first, second]
    GBP_STATE["n"] = 0
    side = rng.choice([1, 0xFFFFFFFF, 1, 0xFFFFFFFF, 2, 0, rng.getrandbits(32)])
    distance = rng.choice([10, 10, 0x46, 0, 1, 0xFFFFFFF6, rng.randrange(0, 200), rng.getrandbits(32)])
    respond = rng.choice([1, 1, 1, 1, 1, 0, 2, 0x100, rng.getrandbits(32) | 1] if aimed else [1, 1, 1, 1, 0, 2, 0x100, rng.getrandbits(32) | 1])
    return [side, distance, respond], img.pokes()


# gal: Skater_AirLandingOrientationRecovery 0x00497960 (thiscall on the skater: the normal as two arguments). Slab: the skater at 0x0000. The dot product, the vector negation and product and the basis
# publication run as retail code; the bail is a stub.
def install_stubs_gal(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gal_installed", False):
        return
    fc._gal_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    ensure(fc, 0x004904D0, 2, "engine_004904d0", convention="thiscall", this=True)


GROUP_STUBS["gal"] = install_stubs_gal
EXTRA_REGIONS["gal"] = lambda slab: merged([(slab, GSU_SKATER_BYTES)], gap=0)
SLAB_BYTES["gal"] = SLAB + 0x100
READ_ONLY["gal"] = []


@spec(0x00497960, "Skater_AirLandingOrientationRecovery", "gal", this=lambda env: env.slab)
def gen_air_landing_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 0, 1, rng.getrandbits(32) | 1]))
    img.u32(slab + 0x30B8, rng.choice([0, 1, 1, 3, 2, rng.getrandbits(32)]))
    put_vector(img, slab + 0x30F4, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    if rng.random() < 0.7:
        # the normal along x: the dot with the up vector is its x (times the sign); the edges 0xc8 and 0x7d0, the up y's -0x7d0 and the vertical speed's 0x249f0 are tried on their own
        sign = rng.choice([1, -1])
        normal = (0x1000 * sign, 0, 0)
        d = rng.choice([0xC7, 0xC8, 0xC9, 0x7CF, 0x7D0, 0x7D1, -0x7D0, 0, 0x1000, -0x1000, rng.randrange(-0x800, 0x800)])
        y = rng.choice([-0x7D0, -0x7CF, -0x7D1, -0x1000, 0x1000, 0, 0x200, rng.randrange(-0x1000, 0x1000)])
        put_vector(img, slab + 0x310C, [(d * sign) & 0xFFFFFFFF, y & 0xFFFFFFFF, rng.choice([0, 0x100, -0x100, rng.getrandbits(32)]) & 0xFFFFFFFF])
        img.u32(slab + 0x50, rng.choice([0x249EF, 0x249F0, 0x249F1, 0, -1, 0x7FFFFFFF, 0x80000000, rng.randrange(-0x40000, 0x40000)]))
        if rng.random() < 0.7:
            img.u32(slab + 0x2F64, 0)
            img.u32(slab + 0x30B8, rng.choice([0, 1, 3]))
    else:
        normal = q12_direction(rng, rng.choice([0x1000, 0x1000, 0x800, 0x1010]))
        put_vector(img, slab + 0x310C, [c & 0xFFFFFFFF for c in q12_direction(rng)])
        img.u32(slab + 0x50, rng.choice([0x249F0, 0x249F1, 0, 0x30000, rng.getrandbits(32)]))
    xy, z = put_normal_args(rng, normal)
    return [xy, z], img.pokes()


# ---- 10g session 18: the air physics ----

# gda: Skater_DoPhysicsInAir 0x00497f40 (thiscall on the skater, a service of the dispatcher). Slab: the skater at 0x0000 (0x3600 bytes), its pad profile at 0x3700 (bytes +0, +0x30, +0x40, +0x60,
# +0xa0, +0xb0, dwords +4, +8), two surface records at 0x3A00 / 0x3A40 (words +0 and +0xc). The vector helpers, the dot product, the Q12 product, the root, the normalisation, the GTE (cross product,
# rtir, the angle matrix, the matrix-vector product, the translation), the basis publication 0x0049c850, the normal removal 0x00490610, the vector comparison and M3D_ReadSurfaceFlags run as retail
# code; every other callee is a stub. The query record is a stack local, handed on at GDA_QUERY (the port's scratch record): the three queries (the move, a lip's second cast, the wall rider's line
# down) share it and each call of the query stub takes the next scripted hit. GDA_TARGETED=1 (the gdaB capture) aims each case at one narrow path.
GDA_PROFILE = 0x3700
GDA_SURFACE = 0x3A00
GDA_QUERY = 0x7FF00800 + 0x4000
GDA_GLOBALS = [(0x005685F4, 4), (0x0056865C, 4), (0x00568804, 4), (0x0056B760, 0x94), (0x00567C7C, 4), (0x00568638, 4), (0x0056881C, 0x40), (0x006A3E00, 0x120)]
GDA_STATE = {"hits": [], "n": 0, "cur": None, "pw": None, "targeted": __import__("os").environ.get("GDA_TARGETED") == "1",
             "speed": __import__("os").environ.get("GDA_SPEED") == "1", "stats": []}      # (speed: the gdaC capture, the stat-3 speed edge and the wall and count gates)


def gda_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + GDA_PROFILE, 0x100), (slab + GDA_SURFACE, 0x80)] + list(GDA_GLOBALS), gap=0)


def gda_next(a):
    state = GDA_STATE
    state["cur"] = state["hits"][min(state["n"], len(state["hits"]) - 1)]
    state["n"] += 1
    return state["cur"]["body"]


def gda_field(name):
    return lambda a: GDA_STATE["cur"][name]


def install_stubs_gda(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gda_installed", False):
        return
    fc._gda_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    for address, args in ((0x00497DF0, 0), (0x00496360, 0), (0x0048DDC0, 0), (0x0049C330, 0), (0x004914D0, 0), (0x0049D080, 0), (0x0046D970, 0), (0x004916D0, 1),
                          (0x004900B0, 2), (0x00491780, 2), (0x00497960, 2), (0x00497BB0, 2), (0x004904D0, 2), (0x004903F0, 2), (0x0049B500, 3), (0x0049BAD0, 3), (0x0048F690, 4)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x00497AA0, 1, "engine_00497aa0", convention="thiscall", this=True, stack_to=GDA_QUERY)      # Skater_AirCollisionRecovery(record)
    slab = env.slab

    def position(k):
        # Skater_PositionWritePath: the position it leaves (the target, the old one moved by about a unit on one axis, or unchanged), scripted
        def produce(a):
            if k == 0:
                from opentony.memory import mem

                before = [mem.u32(slab + 8 + 4 * i) for i in range(3)]
                pick = rng.random()
                if pick < 0.6:
                    out = list(a[:3])
                elif pick < 0.85:
                    out = list(before)
                    axis = rng.randrange(3)
                    out[axis] = (out[axis] - rng.choice([0x1000, -0x1000, 0xFFF, 0x800])) & 0xFFFFFFFF
                else:
                    out = before
                GDA_STATE["pw"] = out
            return GDA_STATE["pw"][k]
        return produce
    ensure(fc, 0x00496060, 3, "engine_00496060", convention="thiscall", this=True, writes=[(slab + 8 + 4 * k, 4, position(k)) for k in range(3)])
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True,                                                       # Skater_ProfileValue: a stat
           result=lambda a: GDA_STATE["stats"].pop(0) if GDA_STATE["stats"] else rng.choice([0, 3, 5, 10, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11)]))
    ensure(fc, 0x0048CB60, 1, "engine_0048cb60", convention="thiscall", this=True,                                                       # TrickScore_ChainValue (on the panel)
           result=lambda a: rng.choice([0, 0, 1, 2, 3, 5, 11, 12, 0xFFFFFFFF, rng.getrandbits(32)]))
    ensure(fc, 0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0] * 6 + [1, 0x100]))   # Career_GetCheat (a bool in AL)
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.randrange(a[0]) if 0 < a[0] < 0x10000 else 0)   # Rnd
    ensure(fc, 0x004A12D0, 1, "engine_004a12d0")
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004624D0, 1, "engine_004624d0", stack_to=GDA_QUERY)
    ensure(fc, 0x00466090, 2, "engine_00466090", stack_to=GDA_QUERY, blob=(0, 0x18),
           pokes=[(0, 4, gda_next, 0x68), (0, 4, gda_field("x"), 0x6C), (0, 4, gda_field("y"), 0x70), (0, 4, gda_field("z"), 0x74), (0, 4, gda_field("w78"), 0x78),
                  (0, 4, gda_field("w7c"), 0x7C), (0, 4, gda_field("distance"), 0x40), (0, 4, gda_field("surface"), 0x80)])


GROUP_STUBS["gda"] = install_stubs_gda
EXTRA_REGIONS["gda"] = gda_regions
SLAB_BYTES["gda"] = SLAB + 0x100
READ_ONLY["gda"] = []

GDA_NORMALS = [(0x1000, 0, 0), (-0x1000, 0, 0), (0, 0, 0x1000), (0, 0, -0x1000), (0, -0x1000, 0), (0, 0x1000, 0), (0xB50, 0, 0xB50), (0xF00, 0xCCD, 0), (0xF00, 0xCCE, 0),
               (0xF00, 0xCCC, 0), (0, -0xCCD, 0xF00), (0x800, -0xDDB, 0), (0x7FF, 0x7FF, 0x7FF)]


def gda_surface_flags(rng, kind=None):
    """Surface word +0xc: bit 22 the lip, bits 24 / 23 clear raise 0x56b7b8 / 0x56b7a8, bits 25..28 the class (7 and 9 have sounds), bit 21 the +0x2db4 flag."""
    pick = rng.random() if kind is None else kind
    if pick < 0.25:
        flags = 0x01000000                                   # a wall: bit 24 set, bit 23 clear
    elif pick < 0.45:
        flags = 0x00800000 | rng.choice([0, 0x00400000])     # landing: bit 24 clear (a lip now and then)
    elif pick < 0.6:
        flags = 0x01800000                                   # plain: thrown back
    elif pick < 0.75:
        flags = 0x01C00000 | rng.choice([0, 0x00800000])     # a lip
    else:
        flags = rng.getrandbits(32) & 0x01C00000
    flags |= rng.choice([0, 7, 9, 7, 9, 1, 3, rng.randrange(0, 16)]) << 25
    flags |= rng.choice([0, 0x00200000]) | (rng.getrandbits(16) if rng.random() < 0.3 else 0)
    return flags


def gda_hit(rng, slab, near, normal=None):
    if normal is None:
        pick = rng.random()
        if pick < 0.75:
            normal = rng.choice(GDA_NORMALS)
            if rng.random() < 0.3:
                normal = tuple(c + rng.randrange(-3, 4) for c in normal)
        else:
            normal = tuple(rng.randrange(-0x1000, 0x1001) for _ in range(3))
    high = rng.choice([0, 0, 0, rng.getrandbits(16)])
    offset = lambda: rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x40000, 0x40000)])      # noqa: E731
    return {"body": rng.choice([0, 1, 0x1234, 0x1234, 0x1234, 0x1234]),
            "x": (near[0] + offset()) & 0xFFFFFFFF, "y": (near[1] + offset()) & 0xFFFFFFFF, "z": (near[2] + offset()) & 0xFFFFFFFF,
            "w78": (normal[0] & 0xFFFF) | ((normal[1] & 0xFFFF) << 16), "w7c": (normal[2] & 0xFFFF) | (high << 16),
            "distance": rng.getrandbits(16), "surface": slab + GDA_SURFACE + rng.choice([0, 0x40])}


@spec(0x00497F40, "Skater_DoPhysicsInAir", "gda", this=lambda env: env.slab)
def gen_air_physics(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    small = lambda: rng.choice([0, 0, 1, 2, 5, 0xFFFFFFFF, word(rng)])      # noqa: E731
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(slab + GDA_PROFILE, 0x100, rng)
    img.garbage(0x0056B760, 0x94, rng)
    img.garbage(0x0056881C, 0x40, rng)
    state = rng.choice([1, 1, 1, 1, 3, 3, 3, 0, 2, 5, 7])
    img.u32(slab + 0x30B8, state)
    clock = rng.choice([0x1000, 0x10000, rng.getrandbits(31)])
    img.u32(0x005685F4, clock)
    ts = rng.choice([0x100, 0x100, 0x100, 0x80, 0x200, 1, rng.randrange(0, 0x2000)])
    img.u32(0x0056865C, ts)
    img.u32(0x00568804, rng.choice([0x100, 0x1000, 0x80, rng.randrange(0, 0x400)]))
    img.u32(0x00567C7C, word(rng))
    img.u32(0x00568638, word(rng))
    img.u32(0x0056B7F0, rng.choice([0, 0, 1, 1, word(rng)]))
    for k, c in enumerate(rng.choice([(0, 0x1000, 0), (0x1000, 0, 0), (0, 0, 0x1000), (0, -0x1000, 0), tuple(rng.randrange(-0x1000, 0x1001) for _ in range(3))])):
        img.u32(0x0056B7C0 + 4 * k, (c & 0xFFFF) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else (0xFFFF0000 if c < 0 else 0)))
    # the position, the old position (mostly the same: the assertion holds), the velocity and the acceleration
    pos = [rng.choice([0, rng.randrange(-0x400000, 0x400000)]) & 0xFFFFFFFF for _ in range(3)]
    old = list(pos) if rng.random() < 0.8 else [(p + rng.choice([1, -1, 0x1000, -0x30000, rng.randrange(-0x40000, 0x40000)])) & 0xFFFFFFFF for p in pos]
    vel, acc = [0, 0, 0], [0, 0, 0]
    if rng.random() < 0.9:
        size = rng.choice([0x100, 0x2000, 0x10000, 0x40000, 0x100000])
        vel = [rng.choice([0, rng.randrange(-size, size)]) for _ in range(3)]
        acc = [rng.choice([0, 0, rng.randrange(-0x4000, 0x4000), rng.getrandbits(32)]) for _ in range(3)]
    for k in range(3):
        img.u32(slab + 0x08 + 4 * k, pos[k])
        img.u32(slab + 0xBC + 4 * k, old[k])
        img.u32(slab + 0x4C + 4 * k, vel[k] & 0xFFFFFFFF)
        img.u32(slab + 0x58 + 4 * k, acc[k] & 0xFFFFFFFF)
        img.u32(slab + 0x2DA8 + 4 * k, rng.choice([0, 0x1964, -0x1964, rng.randrange(-0x2000, 0x2000)]) & 0xFFFFFFFF)
    img.u32(slab + 0x2DAC, rng.choice([0, 0x100, 0x1000, 0x3000, rng.randrange(-0x4000, 0x4000), word(rng)]) & 0xFFFFFFFF)
    # the basis (the up vector's y on the wall ride's edge -0x5dc now and then)
    forward, right, up = q12_direction(rng), q12_direction(rng), q12_direction(rng)
    if rng.random() < 0.4:
        up[1] = rng.choice([-0x5DC, -0x5DB, -0x5DD, -0x1000, -0x800])
    put_vector(img, slab + 0x30F4, [c & 0xFFFFFFFF for c in forward])
    put_vector(img, slab + 0x3100, [c & 0xFFFFFFFF for c in right])
    put_vector(img, slab + 0x310C, [c & 0xFFFFFFFF for c in up])
    # the pad profile
    img.u32(slab + 0x2CCC, slab + GDA_PROFILE)
    for at in (0x00, 0x30, 0x40, 0x60, 0xA0, 0xB0):
        img.u8(slab + GDA_PROFILE + at, rng.choice([0, 0, 1, 0x80]))
    img.u32(slab + GDA_PROFILE + 4, rng.choice([0, 1, 2, 3, 4, 0xFFFFFFFF, word(rng)]))
    img.u32(slab + GDA_PROFILE + 8, rng.choice([0x13, 0x14, 0x15, 0, 0x100, 0xFFFFFFFF, word(rng)]))
    img.u32(slab + 0x3204, rng.choice([0, 0, 1, word(rng)]))
    img.u32(slab + 0x30C4, rng.choice([0, 0, 0, 1]))
    # the lean and the orientation (the up row's middle short on the edge 1)
    img.u32(slab + 0x2E84, rng.choice([0, 0, 0, 0, 0, rng.randrange(1, 0x1000), 0x400, 0xC00, 0x10000, word(rng)]))
    for at in range(0x2E58, 0x2E6A, 2):
        img.u16(slab + at, rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1001)]) & 0xFFFF)
    img.u16(slab + 0x2E60, rng.choice([0, 1, -1, 2, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1001)]) & 0xFFFF)
    # the spin
    img.u32(slab + 0x3144, rng.choice([0, 0, 0x1000, -0x1000, 0x10000, -0x30000, 0x5A000, rng.randrange(-0xA0000, 0xA0000), word(rng)]) & 0xFFFFFFFF)
    img.u32(slab + 0x2858, rng.choice([0, 0, 1, 2, 5]))
    img.u32(slab + 0x306C, rng.choice([0, 1, 2, 3, 11, 12, rng.getrandbits(32)]))
    img.u32(slab + 0x3068, rng.choice([0, 0x7FF, 0x800, 0x7F0, -0x7FF, -0x800, -0x7F0, rng.randrange(-0x1000, 0x1000)]) & 0xFFFFFFFF)
    img.u32(slab + 0x2C84, rng.choice([0, 0, 1]))
    # the gates of the collision branches
    img.u32(slab + 0x2F64, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0x2F50, (clock - rng.choice([0x28, 0x29, 0x27, 0x29, 0, 100, 0x1000, rng.getrandbits(32)])) & 0xFFFFFFFF)
    img.u32(slab + 0x2F34, (clock - rng.choice([0x1D, 0x1E, 0x1F, 0, 100, 0x1000, rng.getrandbits(32)])) & 0xFFFFFFFF)
    for field in (0x2C68, 0x2C6C):
        img.u32(slab + field, rng.choice([0, 0, 1]))
    img.u32(slab + 0x2EC0, rng.choice([0, 1, 5]))
    img.u32(slab + 0x2E90, rng.choice([0, 0, 0, 1]))
    img.u32(slab + 0xD8, rng.choice([0, 2, rng.getrandbits(32)]))
    for field in (0x3034, 0x29C8, 0x29DC, 0x2DB4, 0x2CBC, 0x2DDC, 0x30B0, 0x3130, 0x2D98):
        img.u32(slab + field, small())
    for at in (GDA_SURFACE, GDA_SURFACE + 0x40):
        img.u32(slab + at, rng.choice([0, 0, 0x80, rng.getrandbits(32)]))
        img.u32(slab + at + 0xC, gda_surface_flags(rng))
    hits = [gda_hit(rng, slab, old), gda_hit(rng, slab, old), gda_hit(rng, slab, pos)]
    if rng.random() < 0.1:
        hits[0]["body"] = 0
    GDA_STATE["stats"] = []
    if GDA_STATE["targeted"]:
        gda_target(img, rng, slab, clock, hits)
    if GDA_STATE["speed"]:
        gda_speed(img, rng, slab)
    GDA_STATE["hits"] = hits
    GDA_STATE["n"] = 0
    return [], img.pokes()


def gda_target(img, rng, slab, clock, hits):
    """One narrow path, built on purpose: the wall ride (the steepness 0xccd, the side-on dot 0xddb, the up y -0x5dc, the timing 0x28), the landing (the wall rider's 0x1e), the throw-back, a lip's
    second cast and the wall rider's line down."""
    img.u32(slab + 0x2F64, 0)
    img.u32(0x0056B7F0, 0)
    first = hits[0]
    first["body"] = 0x1234
    first["surface"] = slab + GDA_SURFACE
    target = rng.randrange(5)
    if target == 0:
        # a wall ride: a horizontal normal, the velocity across it at an angle whose cosine is about 0xddb, falling
        img.u32(slab + 0x30B8, rng.choice([1, 1, 0, 5]))
        img.u32(slab + GDA_SURFACE + 0xC, 0x01000000 | (rng.choice([0, 7, 9]) << 25))
        img.u32(slab + GDA_SURFACE, rng.choice([0, 0, 0x80]))
        img.u8(slab + GDA_PROFILE, rng.choice([1, 0]))
        img.u32(slab + GDA_PROFILE + 8, rng.choice([0x13, 0, 0x14]))
        img.u32(slab + 0x2F50, (clock - rng.choice([0x29, 0x29, 0x28, 0x100])) & 0xFFFFFFFF)
        img.u32(slab + 0x3110, rng.choice([-0x5DD, -0x5DD, -0x5DC, -0x1000]) & 0xFFFFFFFF)
        ny = rng.choice([0, 0, 0xCCD, 0xCCE, -0x800])
        first["w78"] = 0x1000 | ((ny & 0xFFFF) << 16)
        first["w7c"] = 0
        c = rng.choice([0xDDA, 0xDDB, 0xDDC, 0xDD0, 0x800, 0x1000, -0xDDA, -0xDDB])
        import math

        s = int(round(math.sqrt(max(0, 0x1000 * 0x1000 - c * c))))
        speed = rng.choice([0x10, 0x40, 0x100])
        for k, v in enumerate((c * speed, rng.randrange(-0x100000, 0), s * speed * rng.choice([1, -1]))):
            img.u32(slab + 0x4C + 4 * k, v & 0xFFFFFFFF)
    elif target == 1:
        # a landing: bit 24 clear (a lip now and then), the wall rider's timing at 0x1e
        img.u32(slab + 0x30B8, rng.choice([1, 3, 3, 0]))
        img.u32(slab + GDA_SURFACE + 0xC, 0x00800000 | rng.choice([0, 0x00400000]) | rng.choice([0, 0x00200000]))
        img.u32(slab + 0x2F34, (clock - rng.choice([0x1D, 0x1E, 0x1F])) & 0xFFFFFFFF)
        img.u32(slab + 0x2E90, rng.choice([0, 0, 1]))
    elif target == 2:
        # thrown back: a plain surface, the classes with sounds, the turn phase
        img.u32(slab + GDA_SURFACE + 0xC, 0x01800000 | (rng.choice([7, 9, 8, 6]) << 25))
        img.u32(slab + 0x2EC0, rng.choice([1, 1, 0]))
        first["w78"] = (rng.choice([3, -3, 0x1001, -0x1001, 0x7FF]) & 0xFFFF) | ((rng.choice([-0x1000, -5, 7]) & 0xFFFF) << 16)
        first["w7c"] = rng.choice([1, -1, 0x803, -0x803]) & 0xFFFF
    elif target == 3:
        # a lip that is not a face: the second cast (and its hit) with the owner set
        img.u32(slab + 0x30B8, rng.choice([1, 0, 5]))
        img.u32(slab + GDA_SURFACE + 0xC, 0x01C00000)
        img.u32(slab + GDA_SURFACE, 0)
        hits[1]["surface"] = slab + GDA_SURFACE + 0x40
        hits[1]["body"] = rng.choice([0, 0x1234, 0x1234])
    else:
        # the wall rider's line down: no hit on the move, falling, within 0x1e ticks of the start, a lip below
        first["body"] = 0
        img.u32(slab + 0x30B8, 3)
        img.u32(slab + 0x50, rng.choice([-1, -0x1000, 0, 1]) & 0xFFFFFFFF)
        img.u32(slab + 0x2F34, (clock - rng.choice([0x1D, 0x1E, 0, 0x1F])) & 0xFFFFFFFF)
        hits[1]["surface"] = slab + GDA_SURFACE + 0x40
        img.u32(slab + GDA_SURFACE + 0x40 + 0xC, rng.choice([0x00400000, 0x01C00000, 0]))


def gda_speed(img, rng, slab):
    """The move's speed on the stat-3 edge (the second Skater_ProfileValue(3) call shows only past it): stat 2 makes the limit 0x3f000 = 4032 << 6 exactly, stat 10 makes it 263314 (4115 << 6
    lies between it and the limit of stat 11); the wall flag +0x30c4 and the spin count gate +0x2c84 take values other than 0 and 1."""
    img.u32(slab + 0x30C4, rng.choice([0, 0, 2, 0x100, word(rng)]))
    img.u32(slab + 0x2C84, rng.choice([0, 2, 0x100, word(rng)]))
    if rng.random() < 0.8:
        stat, length = rng.choice([(2, 4032), (2, 4031), (2, 4033), (10, 4115), (10, 4114), (11, 4125), (11, 4124)])
        GDA_STATE["stats"] = [stat]
        img.u32(0x0056B7F0, 0)
        img.u32(slab + 0x2E84, 0)
        axis = rng.randrange(3)
        for k in range(3):
            img.u32(slab + 0x4C + 4 * k, 0)
            img.u32(slab + 0x58 + 4 * k, 0)
            old = img_u32(img, slab + 0xBC + 4 * k)
            img.u32(slab + 0x08 + 4 * k, (old + ((length << 6) * rng.choice([1, -1]) if k == axis else 0)) & 0xFFFFFFFF)


# ---- 10g session 24: the start of a grind ----

# gsg: Skater_StartGrind 0x00490ef0 (thiscall on the skater, one argument: the geometry hint; called by Skater_LandingCleanup). Slab: the skater at 0x0000 (0x3600 bytes). The vector helpers (negate,
# subtract, scale, divide, add), the dot product, the Q12 product and TrickScript_Start run as retail code; every other callee is a stub: the animation request, Skater_ProfileValue (stat 7 twice, stat 3;
# scripted), the stick's pattern, the grind trick start, the script interpreter (which clears +0x2e90 and sets +0x2c0c now and then: scripted writes), the sound, the state change and the assertion.
# GSG_TARGETED=1 (the gsgB capture) puts the dots and the clock on the edges of the tests that depend on them.
GSG_GLOBALS = [(0x005685F4, 4), (0x00568638, 4), (0x0056881C, 0x40)]
GSG_STATE = {"n7": 0, "targeted": __import__("os").environ.get("GSG_TARGETED") == "1"}


def gsg_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES)] + list(GSG_GLOBALS), gap=0)


def install_stubs_gsg(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_gsg_installed", False):
        return
    fc._gsg_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    slab = env.slab
    for address, args in ((0x004903F0, 2), (0x004900B0, 2), (0x00491C10, 1)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004011E0, 2, "assert", when=lambda a: (a[0] & 0xFF) == 0, keep=slice(1, 2))

    def divisor_zero(stat):
        product = ((stat + 0x15E) * 2000) & 0xFFFFFFFF
        product = product - (1 << 32) if product & 0x80000000 else product
        return int(product / 10000) == 0

    def profile(a):
        # Skater_ProfileValue: stat 7 is read twice (the second one is a divisor: never one that makes it 0, retail would fault), stat 3 once
        if a[0] == 7:
            GSG_STATE["n7"] += 1
        while True:
            value = rng.choice([0, 3, 5, 10, 10, 0x1F4, 0xFFFFFFFF, rng.randrange(0, 11), rng.randrange(-0x400, 0x400) & 0xFFFFFFFF, word(rng)])
            if not (a[0] == 7 and GSG_STATE["n7"] == 2 and divisor_zero(value if value < 0x80000000 else value - (1 << 32))):
                return value
    ensure(fc, 0x0048F3A0, 1, "engine_0048f3a0", convention="thiscall", this=True, result=profile)
    ensure(fc, 0x00492120, 0, "engine_00492120", convention="thiscall", this=True, result=lambda a: rng.choice(list(range(9)) + [0xFFFFFFFF, 9, word(rng)]))      # TrickInput_DerivePattern
    ensure(fc, 0x004BE450, 0, "engine_004be450", convention="thiscall", this=True,                                                            # TrickScript_Execute
           writes=[(slab + 0x2E90, 4, lambda a: rng.choice([1, 1, 1, 0, 0, 2])), (slab + 0x2C0C, 4, lambda a: rng.choice([0, 0, 0, 1, 0xFFFF, rng.getrandbits(16)]))])


GROUP_STUBS["gsg"] = install_stubs_gsg
EXTRA_REGIONS["gsg"] = gsg_regions
SLAB_BYTES["gsg"] = SLAB + 0x100
READ_ONLY["gsg"] = []


@spec(0x00490EF0, "Skater_StartGrind", "gsg", this=lambda env: env.slab)
def gen_start_grind(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    GSG_STATE["n7"] = 0
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x0056881C, 0x40, rng)
    clock = rng.choice([0x1000, 0x10000, rng.getrandbits(31), 0, 0x7FFFFFFF])
    img.u32(0x005685F4, clock)
    img.u32(0x00568638, word(rng))
    hint = rng.choice([0, 0, 1, 1, 1, 2, 0x100, word(rng)])
    # the rail direction (+0x2eb4), the bases (forward +0x30f4, right +0x3100), the rail point (+0x2ea8), its predecessor (+0x2e18) and the position
    direction = q12_direction(rng) if rng.random() < 0.9 else [rng.getrandbits(32) for _ in range(3)]
    put_vector(img, slab + 0x2EB4, [c & 0xFFFFFFFF for c in direction])
    put_vector(img, slab + 0x30F4, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    put_vector(img, slab + 0x3100, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    point = [rng.choice([0, rng.randrange(-0x400000, 0x400000)]) & 0xFFFFFFFF for _ in range(3)]
    before = [(c + rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x40000, 0x40000)])) & 0xFFFFFFFF for c in point] if rng.random() < 0.7 else [rng.getrandbits(32) for _ in range(3)]
    put_vector(img, slab + 0x2EA8, point)
    put_vector(img, slab + 0x2E18, before)
    # the nodes (the same one half of the time), the rail speed and its change
    node = rng.choice([0, 1, 5, 0x1234, rng.getrandbits(32)])
    img.u32(slab + 0x2E9C, node)
    img.u32(slab + 0x2E98, node if rng.random() < 0.5 else (node + rng.choice([1, -1, 0x100])) & 0xFFFFFFFF)
    img.u32(slab + 0x2EC8, node if rng.random() < 0.4 else (node + rng.choice([1, -1, 0x100])) & 0xFFFFFFFF)
    img.u32(slab + 0x2EFC, rng.choice([0, 0x28000, 0x27FFF, 0x28001, 0x50000, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF, word(rng)]))
    img.u32(slab + 0x2EF4, rng.choice([0, 1, 5, 100, -100 & 0xFFFFFFFF, rng.randrange(-0x2000, 0x2000) & 0xFFFFFFFF, word(rng)]))
    img.u32(slab + 0xD8, rng.choice([0, 2, 3, 1, rng.getrandbits(32)]))
    for field in (0x2BF0, 0x2BF4, 0x2BF8, 0x2BEC, 0x2BE8, 0x2C04, 0x2ED8, 0x2C68, 0x2E88, 0x3070, 0x304C, 0x2DE0, 0x2C78, 0x2C7C, 0x2E94):
        img.u32(slab + field, rng.choice([0, 1, 2, word(rng)]))
    for at in range(0x2C94, 0x2CB8, 4):
        img.u32(slab + at, rng.choice([0, 1, word(rng)]))
    img.u32(slab + 0x2EEC, word(rng))
    img.u32(slab + 0x2BFC, rng.choice([0, 0, 1, 2, 3, word(rng)]))
    img.u32(slab + 0x2C00, rng.choice([0, 1, 2, 3, word(rng)]))
    img.u32(slab + 0x2C08, rng.choice([0, 0, 1, 2, word(rng)]))
    img.u32(slab + 0x2C0C, word(rng))
    # the waiting script: within 0x1e ticks of its press or not, the grind flag against +0x29e4
    img.u32(slab + 0x29DC, rng.choice([0, 0, 0x1234, 0x40, word(rng)]))
    img.u32(slab + 0x2BE0, (clock - rng.choice([0, 1, 0x1D, 0x1E, 0x1F, 0x100, 0x7FFFFFFF, rng.getrandbits(32)])) & 0xFFFFFFFF)
    img.u32(slab + 0x29E4, rng.choice([0, 0, 1, word(rng)]))
    img.u32(slab + 0x2E8C, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0x100, 0xFFFFFFFF, rng.getrandbits(32)]))
    put_vector(img, slab + 0x4C, [rng.choice([0, rng.randrange(-0x100000, 0x100000)]) & 0xFFFFFFFF for _ in range(3)])
    put_vector(img, slab + 0x08, [rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF for _ in range(3)])
    if GSG_STATE["targeted"]:
        gsg_target(img, rng, slab, clock)
    return [hint], img.pokes()


def gsg_target(img, rng, slab, clock):
    """The edges of the three dot tests: the direction along an axis, so that each dot is one component of the other vector: the right vector's x on 0xb33 and the tangent's on 0, the vector across
    (the rail point minus +0x2e18, z and -x) on 0 and 1; the clock on the 0x1e window with the grind flag and +0x29e4 both ways."""
    axis = rng.choice([(0x1000, 0, 0), (-0x1000, 0, 0)])
    sign = 1 if axis[0] > 0 else -1
    put_vector(img, slab + 0x2EB4, [(-axis[0]) & 0xFFFFFFFF, 0, 0])      # the entry negates it: the direction in use is `axis`
    put_vector(img, slab + 0x3100, [rng.choice([0xB32, 0xB33, 0xB34, -0xB32, -0xB33, -0xB34, 0, 1, -1, 0x1000, -0x1000]) * sign & 0xFFFFFFFF, rng.choice([0, 5]) & 0xFFFFFFFF, 0])
    put_vector(img, slab + 0x30F4, [rng.choice([0, 1, -1, 0x10, -0x10]) * sign & 0xFFFFFFFF, 0, 0])
    point = [rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF for _ in range(3)]
    img.u32(slab + 0x2EA8, point[0])
    img.u32(slab + 0x2EA8 + 8, point[2])
    img.u32(slab + 0x2E18, (point[0] - rng.choice([0, 3, -3])) & 0xFFFFFFFF)
    img.u32(slab + 0x2E18 + 8, (point[2] - rng.choice([0, 1, -1, 0x1000, -0x1000]) * sign) & 0xFFFFFFFF)
    img.u32(slab + 0x29DC, rng.choice([0x1234, 0x40]))
    img.u32(slab + 0x2BE0, (clock - rng.choice([0x1D, 0x1E, 0x1F, 0])) & 0xFFFFFFFF)
    img.u32(slab + 0x2E8C, rng.choice([0, 1, 2, 3]))


# ---- 10g session 20: the trick script interpreter ----

# tse: TrickScript_Execute 0x004be450 (thiscall on the skater). Slab: the skater at 0x0000 (0x3600 bytes), its pad profile at 0x3700 (byte +0x10), the script at 0x3800 (0x800 bytes, TricksBinaryBase points at
# it). The stream reads (0x004be3b0, 0x004be3c0), the frame operand (0x004be3e0), the history query (0x004924f0), the dot product, the root, the normalisation, the angle matrix, the GTE's rtir, the vector
# helpers and the basis from the matrix (0x0049c7d0) run as retail code; every other callee is a stub. Each case is a random script of legal commands whose branches only go forward (to a later command,
# the end, or a subroutine after the end that returns), ending in 0x07 or 0x16, so retail returns. TSE_FOCUS=0xNN,0xMM (a capture aimed at some opcodes) makes about half the commands one of them.
TSE_PROFILE = 0x3700
TSE_SCRIPT = 0x3800
TSE_SCRIPT_BYTES = 0x800
TSE_GLOBALS = [(0x005685F4, 4), (0x0056865C, 4), (0x0056A894, 4), (0x00568638, 4), (0x0056881C, 0x40), (0x006A3E00, 0x120)]
TSE_STATE = {"focus": [int(x, 16) for x in __import__("os").environ.get("TSE_FOCUS", "").split(",") if x], "history": __import__("os").environ.get("TSE_HISTORY") == "1", "edges": __import__("os").environ.get("TSE_EDGES") == "1"}   # (history: the ring edges; edges: the frames within a few of each other, a subroutine that waits)


def tse_regions(slab):
    return merged([(slab, GSU_SKATER_BYTES), (slab + TSE_PROFILE, 0x100), (slab + TSE_SCRIPT, TSE_SCRIPT_BYTES)] + list(TSE_GLOBALS), gap=0)


def install_stubs_tse(fc, env):
    from core_specs_main import lock_scheduler
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_tse_installed", False):
        return
    fc._tse_installed = True
    mark_under_test()
    stop_clock(fc)
    lock_scheduler()
    rng = env.rng
    for address, args in ((0x0048F5F0, 0), (0x0048F8F0, 0), (0x0046C630, 0), (0x0046C5F0, 0), (0x00490C70, 0), (0x0048F720, 1), (0x004903F0, 2), (0x004900B0, 2), (0x004904D0, 2),
                          (0x0046DD20, 2), (0x00431260, 2), (0x0048FFC0, 3), (0x0048F690, 4), (0x00490480, 5)):
        ensure(fc, address, args, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x004CF850, 1, "printf_log")
    ensure(fc, 0x004011E0, 2, "assert", when=lambda a: (a[0] & 0xFF) == 0, keep=slice(1, 2))
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=lambda a: rng.randrange(a[0]) if 0 < a[0] < 0x10000 else 0)   # Rnd


GROUP_STUBS["tse"] = install_stubs_tse
EXTRA_REGIONS["tse"] = tse_regions
SLAB_BYTES["tse"] = SLAB + 0x100
READ_ONLY["tse"] = []

# opcode: the operands after it ("w" a word, "b" a byte, "t" a branch target word, "s" a string, "T" Rnd's table)
TSE_OPERANDS = {0x01: "w", 0x02: "", 0x03: "", 0x07: "", 0x09: "w", 0x0A: "w", 0x0B: "s", 0x0C: "", 0x0D: "w", 0x0E: "w", 0x0F: "www", 0x10: "w", 0x11: "w", 0x12: "t", 0x13: "", 0x14: "",
                0x15: "t", 0x16: "", 0x17: "ww", 0x18: "w", 0x19: "w", 0x1A: "", 0x1B: "w", 0x1C: "", 0x1D: "w", 0x1E: "", 0x1F: "w", 0x20: "", 0x21: "w", 0x22: "", 0x23: "w", 0x24: "t",
                0x25: "t", 0x26: "t", 0x27: "t", 0x28: "t", 0x29: "w", 0x2A: "w", 0x2B: "www", 0x2C: "", 0x2D: "w", 0x2E: "w", 0x2F: "t", 0x30: "", 0x31: "", 0x32: "", 0x33: "", 0x34: "t",
                0x35: "", 0x36: "w", 0x37: "ww", 0x38: "www", 0x39: "T", 0x3A: "t", 0x3B: "", 0x3C: "", 0x3D: "G", 0x3E: "", 0x3F: "w", 0x40: "w", 0x41: "t", 0x42: "w", 0x43: "t", 0x44: "w",
                0x45: "www", 0x46: "", 0x47: "wt", 0x48: "wt", 0x49: "w", 0x4A: "", 0x4B: "t", 0x4C: "bt", 0x4D: "", 0x4E: "t", 0x4F: "wwww", 0x50: "s", 0x51: "w", 0x52: "b", 0x53: "t", 0x54: "w",
                0x55: "b", 0x56: "", 0x57: "", 0x58: "w", 0x59: "b"}
TSE_UNDEFINED = [0x00, 0x04, 0x05, 0x06, 0x08, 0x5A, 0x80, 0xFF]
TSE_FRAMES = [0, 1, 2, 5, 10, 0x1F, 0x2711, -1, -2, -5, -0x20, 0x7FFF, -0x8000]


def tse_operand_word(rng, opcode, index):
    """A word operand an opcode reads, from the values that matter to it."""
    if TSE_STATE["edges"] and (opcode in (0x09, 0x0D, 0x10, 0x18) or (opcode == 0x48 and index == 0)):
        return rng.choice([-3, -2, -1, 0, 1, 2, 3, 0x2711] if opcode != 0x09 else [-2, -1, 0, 1, 2])
    if TSE_STATE["edges"] and opcode == 0x47 and index == 0:
        return TSE_STATE["n"] + rng.choice([-1, 0, 0, 1])
    if opcode in (0x0D, 0x10, 0x18) or (opcode == 0x48 and index == 0):
        return rng.choice(TSE_FRAMES + [rng.randrange(-0x40, 0x40)])
    if opcode == 0x1B:
        return rng.choice([1, -1, 1, -1, 0, 2, -0x8000])
    if opcode == 0x2B and index == 0:
        return rng.choice([0, 1, 2, 0, 1, 2, 3, -1])
    if opcode == 0x2B and index == 2:
        return rng.choice([0, 0, 1, 0x100, -5, rng.getrandbits(16)])
    if opcode == 0x45:
        return [rng.choice([0, 1, -1, 2, -0x8000, 0x7FFF]), rng.choice([0, 1, 0, 1, 2, -1]), rng.choice([0, 0xFF, 0x80, 0x100, -1, 0x7FFF])][index]
    if opcode == 0x47 and index == 0:
        return rng.choice([0, 1, 0x40, 0x100, 0x400, -1, 0x7FFF, rng.randrange(0, 0x800)])
    if opcode in (0x19, 0x2A):
        return rng.randrange(0, TSE_SCRIPT_BYTES)
    if opcode == 0x21:
        return rng.choice([0, 0x400, 0x800, 0xC00, 0x1000, 0xF000, rng.getrandbits(16)])
    if opcode == 0x42:
        return rng.choice([0, 1, -1, 0x10, 0x100, -0x100, rng.getrandbits(16)])
    return rng.choice([0, 1, -1, 2, 0x10, 0x100, 0x7FFF, -0x8000, rng.getrandbits(16)])


def tse_script(rng, focus):
    """The commands of a script as (opcode, operands) with symbolic branch targets: ("cmd", k) the k-th command, ("sub", k) the k-th subroutine. Forward only."""
    count = rng.choice([1, 2, 3, 5, 8, 12, 20])
    opcodes = [op for op in TSE_OPERANDS if op not in (0x07, 0x16, 0x3E)]
    main = []
    for k in range(count):
        if focus and rng.random() < 0.5:
            op = rng.choice(focus)
        elif rng.random() < 0.04:
            op = rng.choice(TSE_UNDEFINED)
        else:
            op = rng.choice(opcodes)
        main.append(op)
    main.append(rng.choice([0x07, 0x16]))
    subs = [[rng.choice([0x01, 0x0A, 0x13, 0x14, 0x22, 0x35, 0x3B, 0x57, 0x29])] * rng.randrange(0, 3) + [0x3E] for _ in range(2)]
    if TSE_STATE["edges"] and rng.random() < 0.5:
        subs[0].insert(len(subs[0]) - 1, rng.choice([0x16, 0x07]))      # a subroutine that returns to the frame loop before its 0x3e: the gosub's stamp shows
    if 0x3E in focus:
        subs.append([0x3E])

    def build(ops, k, inside):
        out = []
        for i, op in enumerate(ops):
            operands = []
            for j, kind in enumerate(TSE_OPERANDS.get(op, "")):
                if kind == "w":
                    operands.append(("w", tse_operand_word(rng, op, j)))
                elif kind == "b":
                    operands.append(("b", rng.choice([0, 1, 2, 3, 4, 5, 8, 0xFF]) if op == 0x4C else rng.choice([0, 1, 1, 2, 0x80, 0xFF])))
                elif kind == "s":
                    operands.append(("s", bytes(rng.randrange(0x41, 0x5B) for _ in range(rng.randrange(0, 12)))))
                elif kind == "t":
                    operands.append(("t", tse_forward(rng, k + i, len(main), inside)))
                elif kind == "T":
                    n = rng.choice([1, 2, 3, 4])
                    operands.append(("w", n))
                    operands.extend(("t", tse_forward(rng, k + i, len(main), inside)) for _ in range(n))
                elif kind == "G":
                    operands.append(("t", ("sub", rng.randrange(len(subs))) if not inside else ("cmd", len(main) - 1)))
            out.append((op, operands))
        return out

    return build(main, 0, False), [build(s, 0, True) for s in subs]


def tse_forward(rng, here, total, inside):
    if inside:
        return ("end", 0)
    return ("cmd", rng.randrange(here + 1, total))


def tse_layout(commands, subs, base):
    """The script's bytes, starting at `base` (an offset from TricksBinaryBase), and where each command and subroutine starts."""
    sizes = {"w": 2, "b": 1, "t": 2}

    def size(cmd):
        return 1 + sum(len(v) + 1 if kind == "s" else sizes[kind] for kind, v in cmd[1])

    starts, at = [], base
    for cmd in commands:
        starts.append(at)
        at += size(cmd)
    sub_starts, sub_ends = [], []
    for sub in subs:
        sub_starts.append(at)
        for cmd in sub:
            at += size(cmd)
        sub_ends.append(at - 1)          # each subroutine ends in its 0x3e
    out = bytearray()

    def emit(cmd, sub_index):
        out.append(cmd[0])
        for kind, v in cmd[1]:
            if kind == "w":
                out.extend((v & 0xFFFF).to_bytes(2, "little"))
            elif kind == "b":
                out.append(v & 0xFF)
            elif kind == "s":
                out.extend(v + b"\0")
            else:
                where, k = v
                offset = starts[k] if where == "cmd" else (sub_starts[k] if where == "sub" else sub_ends[sub_index])
                out.extend((offset & 0xFFFF).to_bytes(2, "little"))

    for cmd in commands:
        emit(cmd, None)
    for s, sub in enumerate(subs):
        for cmd in sub:
            emit(cmd, s)
    return bytes(out), starts


@spec(0x004BE450, "TrickScript_Execute", "tse", this=lambda env: env.slab)
def gen_trick_script_execute(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    small = lambda: rng.choice([0, 0, 1, 2, 5, 0xFFFFFFFF, word(rng)])      # noqa: E731
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(slab + TSE_PROFILE, 0x100, rng)
    img.garbage(slab + TSE_SCRIPT, TSE_SCRIPT_BYTES, rng)
    img.garbage(0x0056881C, 0x40, rng)
    img.u32(0x00568638, word(rng))
    img.u32(0x005685F4, rng.choice([0, 0x1000, rng.getrandbits(32)]))
    img.u32(0x0056865C, rng.choice([0x100, 0x100, 0x80, 0x200, 1, rng.randrange(0, 0x2000), word(rng)]))
    base = slab + TSE_SCRIPT
    img.u32(0x0056A894, base)
    focus = TSE_STATE["focus"]
    TSE_STATE["n"] = rng.choice([1, 2, 4, 8])
    commands, subs = tse_script(rng, focus)
    start = rng.choice([0, 0, 1, 0x10, 0x40])
    code, starts = tse_layout(commands, subs, start)
    if start + len(code) > TSE_SCRIPT_BYTES:
        commands, subs = [(0x07, [])], []
        code, starts = tse_layout(commands, subs, start)
    img.put(base + start, code)
    img.u32(slab + 0x29CC, base + start)
    # the animation (frame, id, mode, direction, end and start frames, frame count, finished)
    img.u16(slab + 0xF4, rng.choice([0, 1, 5, 10, 0x1F, -1, rng.getrandbits(16)]))
    img.u8(slab + 0x100, rng.choice([1, 1, -1, -1, 0, 2, -2, 0x7F, 0x80]))
    img.u8(slab + 0x101, rng.choice([0, 1, 5, 10, 0x1F, 0x7F, -1, 0x80, rng.getrandbits(8)]))
    img.u8(slab + 0x106, rng.choice([0, 1, 2, 11, 0x20, 0xFF, rng.getrandbits(8)]))
    img.u8(slab + 0x107, rng.choice([0, 0, 0, 1, 0x80]))
    img.u16(slab + 0x114, rng.choice([0, 1, 5, 0x1F, -1, rng.getrandbits(16)]))
    if TSE_STATE["edges"]:
        img.u16(slab + 0xF4, rng.randrange(-2, 4))
        img.u16(slab + 0x114, rng.randrange(-2, 4))
        img.u8(slab + 0x101, rng.randrange(-2, 4))
        img.u8(slab + 0x106, rng.randrange(0, 5))
        img.u8(slab + 0x100, rng.choice([1, -1]))
        img.u8(slab + 0x107, rng.choice([0, 0, 0, 1]))
    # the history ring and the selected button
    img.u32(slab + 0x2B14, rng.randrange(0, 32))
    button = rng.randrange(0, 8)
    img.u32(slab + 0x2A0C, rng.choice([button, button, button, 0x100 + button]))
    when = rng.getrandbits(31)
    img.u32(slab + 0x2A10, when)
    for k in range(32):
        e = slab + 0x2A14 + 8 * k
        img.u8(e, rng.choice([button, button, rng.randrange(0, 8)]))
        img.u8(e + 1, rng.choice([0, 0, 1, 0x80]))
        img.u32(e + 4, (when + rng.choice([1, 0, -1, 0x100, -0x100, rng.getrandbits(32)])) & 0xFFFFFFFF)
    if rng.random() < 0.3:
        for k in range(32):
            img.u8(slab + 0x2A14 + 8 * k + 1, 1)
    if TSE_STATE["history"]:
        # the ring's edges: the write index at 0 (the search starts at entry 31) or 31, one entry a newer release of the button, every other one not
        write = rng.choice([0, 0, 0, 1, 31])
        img.u32(slab + 0x2B14, write)
        for k in range(32):
            img.u8(slab + 0x2A14 + 8 * k + 1, 1)
        k = rng.choice([31, 31, 0, (write - 1) % 32, rng.randrange(32)])
        e = slab + 0x2A14 + 8 * k
        img.u8(e, button)
        img.u8(e + 1, 0)
        img.u32(e + 4, (when + rng.choice([1, 1, 0])) & 0xFFFFFFFF)
        img.u32(slab + 0x2A0C, button)
    # the fields the commands test
    img.u32(slab + 0x30B8, rng.choice([0, 1, 2, 3, 4, 4, 5, 8, rng.randrange(0, 0x10)]))
    for field in (0x3064, 0x2DD4, 0x2E90, 0x2BE8, 0x2BEC, 0x2BF0, 0x2BF4, 0x2BF8, 0x2BFC, 0x29E8, 0x2C04, 0x31F8, 0x2DD8, 0x306C, 0x29EC, 0x2CA0, 0x2CA4, 0x2CA8):
        img.u32(slab + field, rng.choice([0, 0, 0, 1, word(rng)]))
    img.u32(slab + 0x29D8, rng.choice([0, 0, 0, 0, base + starts[-1]]))
    img.u32(slab + 0x2E8C, rng.randrange(0, 4))
    img.u32(slab + 0x2CCC, slab + TSE_PROFILE)
    img.u8(slab + TSE_PROFILE + 0x10, rng.choice([0, 0, 1, 0x80]))
    img.u32(slab + 0xD8, rng.choice([0, 2, 3, rng.getrandbits(32)]))
    img.u8(slab + 0x163, rng.choice([0, 1, 0, 1, 2, 0xFF]))
    img.u32(slab + 0x2F54, rng.choice([0, 0x1000, 0x5000, 0x10000, -0x1000, rng.getrandbits(32)]) & 0xFFFFFFFF)
    for field in (0x30B0, 0x2C10, 0x2F64):
        img.u32(slab + field, small())
    # the velocity, the position, the rail's saved point and the basis, the orientation
    size = rng.choice([0x100, 0x1000, 0x10000, 0x100000, 0x1000000])
    put_vector(img, slab + 0x4C, [rng.choice([0, rng.randrange(-size, size)]) & 0xFFFFFFFF for _ in range(3)])
    put_vector(img, slab + 0x08, [rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF for _ in range(3)])
    put_vector(img, slab + 0x2EA8, [rng.randrange(-0x400000, 0x400000) & 0xFFFFFFFF for _ in range(3)])
    for at in (0x30F4, 0x3100, 0x310C):
        put_vector(img, slab + at, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    for at in range(0x2E58, 0x2E6A, 2):
        img.u16(slab + at, rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x1000, 0x1001)]) & 0xFFFF)
    if TSE_STATE["edges"] and rng.random() < 0.6:
        # the speed exactly n (0x47's threshold is n - 1, n or n + 1) and the forward dot exactly -0x800 (or one off) for 0x4b; a state above 0xff for 0x4c's byte
        n = TSE_STATE["n"]
        put_vector(img, slab + 0x4C, [n << 12, 0, 0])
        put_vector(img, slab + 0x30F4, [(-0x800 // n + rng.choice([0, 0, 1, -1])) & 0xFFFFFFFF, 0, 0])
        if rng.random() < 0.3:
            img.u32(slab + 0x30B8, rng.choice([0x100, 0x101, 0x103, 0xFF04]))
    return [], img.pokes()


# ---- gab (12 s3): the air collision recovery and the air basis ------------------------------------------------------------------------------------------------------------------------------------------
def gab_short_pair(rng):
    """A packed pair of shorts (a normal's x and y) with the edges of the sign and the 16-bit range now and then."""
    pick = lambda: rng.choice([0, 0x1000, -0x1000, 0x7FFF, -0x8000, 0x0800, -1, rng.randrange(-0x1000, 0x1001), rng.getrandbits(16)]) & 0xFFFF
    return pick() | (pick() << 16)


@spec(0x00497AA0, "Skater_AirCollisionRecovery", "gab", this=lambda env: env.slab)
def gen_air_recovery(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(slab + GAB_RECORD, 0xA0, rng)
    clock = rng.choice([1000, 5000, rng.randrange(300, 200000)])
    img.u32(0x005685F4, clock)
    img.u32(slab + 0x2DB8, rng.choice([0, 1, 1, 2, rng.getrandbits(32)]))
    # the latest launch frame: 0 (never), the clock minus 3 / 4 / 5 (the "more than 4 frames ago" edge), or any word (a wrapped difference)
    launch = rng.choice([0, 0, clock - 3, clock - 4, clock - 4, clock - 5, clock - 6, clock, clock + 1, 1, rng.randrange(1, clock + 1), rng.getrandbits(32)])
    img.u32(slab + 0x2D8C, launch & 0xFFFFFFFF)
    img.u32(0x0056B7AC, rng.choice([0, 0, 0x80, 1, rng.getrandbits(32)]))
    img.u32(slab + 0x2CCC, slab + GPF_PROFILE)
    img.u8(slab + GPF_PROFILE + 0xA0, rng.choice([0, 0, 1, 0xFF, rng.getrandbits(8)]))
    img.u8(slab + 0x31A2, rng.choice([0xD7, 0xD8, 0xD8, 0xD9, 0, 0x7F, 0x80, 0xFF, 1, rng.getrandbits(8)]))
    size = rng.choice([0x100, 0x1000, 0x10000, 0x100000, 0x7FFFFF])
    put_vector(img, slab + 0x4C, [rng.choice([0, rng.randrange(-size, size)]) & 0xFFFFFFFF for _ in range(3)])
    img.u32(slab + 0x2DDC, rng.choice([0, 0, 1, 5]))
    img.u32(slab + 0x2C68, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    img.u32(slab + 0x2DB4, rng.getrandbits(32))
    img.u32(slab + GAB_RECORD + 0x78, gab_short_pair(rng))
    img.u32(slab + GAB_RECORD + 0x7C, rng.choice([0, 0x1000, 0xFFFFF000, 0x7FFF, 0x8000, 0xFFFF8000, rng.getrandbits(32)]))
    return [slab + GAB_RECORD], img.pokes()


@spec(0x00497DF0, "Skater_UpdateAirBasis", "gab", this=lambda env: env.slab)
def gen_air_basis(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    img.garbage(slab, GSU_SKATER_BYTES, rng)
    img.garbage(0x006A3E00, 0x120, rng)
    # the up vector's y: the reset edge (below -3596 it is put back to -0x1000), the -500 step, the wrap
    img.u32(slab + 0x3110, rng.choice([0xFFFFF1F3, 0xFFFFF1F4, 0xFFFFF1F4, 0xFFFFF1F5, 0xFFFFF000, 0, 0x1000, 500, 0xFFFFFE0C, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))
    put_vector(img, slab + 0x30F4, [c & 0xFFFFFFFF for c in q12_direction(rng)])
    put_vector(img, slab + 0x3100, [rng.getrandbits(32) for _ in range(3)])
    img.u32(slab + 0x310C, rng.choice([q12_direction(rng)[0] & 0xFFFFFFFF, 0, 0x1000, rng.getrandbits(32)]))
    img.u32(slab + 0x3114, rng.choice([q12_direction(rng)[2] & 0xFFFFFFFF, 0, 0xFFFFF000, rng.getrandbits(32)]))
    if rng.random() < 0.2:
        # parallel forward and up (a zero cross product), or a zero forward vector
        put_vector(img, slab + 0x30F4, rng.choice([[0, 0, 0], [0, 0x1000, 0], [0, 0xFFFFF000, 0]]))
    for at in range(0x2E58, 0x2E6A, 2):
        img.u16(slab + at, rng.getrandbits(16))
    return [], img.pokes()
