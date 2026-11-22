"""Phase 4.14c probe: main.cpp (group mn): the display, the frame loop, the level set-up and tear-down (0x00466b60..0x0046a8cf).

The functions work on the game's globals (the player objects, the polygon arena and ordering tables, the replay buffers, the pad flags, the clocks) and call a lot of the rest of the game. Everything outside
`main.cpp` is a stub that reports its call (`engine_<address>`, with the arguments the code pushes; a method of an object records `this` first) and answers a scripted value; the functions of main.cpp that
call each other (Display -> M3D_RenderStage -> FlipPrimitives, Game_LevelLoop -> Game_Init -> PlayAway_Init ..., SkaterObjects_Create -> Skater_SetupAppearance -> Skater_LoadBitmap) run for real.

The player objects are 0x3300 bytes each in the scratch slab but only the fields the code touches are captured (sparse windows), like the render state, the ordering tables and the polygon arena.

Slab layout (offsets): player 0 at 0x0000 and player 1 at 0x3300, the render state 0x6600, the ordering tables 0x6700 / 0x6a00, the polygon arena 0x6d00, a pointer object 0x7100, a material 0x7140, its block 0x7160,
a texture-set entry 0x7170, the replay buffers 0x7200 / 0x7480 / 0x74c0, texts 0x7600 (0x40 each), a fake vtable 0x7700, a font 0x7710, the allocation arena 0x7900..0x8900, cameras 0x8900 / 0x8e10, an owner
record 0x9400, a trick config 0x9600, the list objects 0x9800..
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

GROUPS = ("mn",)

SLAB = 0xA000
P0 = 0x0000
P1 = 0x3300
PLAYER_BYTES = 0x3300
RENDER = 0x6600
OT_A = 0x6700
OT_B = 0x6A00
POLY = 0x6D00
POLY_BYTES = 0x400
PTROBJ = 0x7100
MATERIAL = 0x7140
MATBLOCK = 0x7160
ENTRY = 0x7170
REPLAY_A = 0x7200
REPLAY_B = 0x7480
STREAM_START = 0x74C0
TEXT = 0x7600
VTABLE = 0x7700
FONT = 0x7710
ARENA = 0x7900
ARENA_SIZE = 0x1000
CAM0 = 0x8900
CAM1 = 0x8E10
OWNER = 0x9400
CONFIG = 0x9600
LISTOBJ = 0x9800

SCRATCH = 0x7FF00800                  # pc_platform.hpp kScratchBase
NORMALIZE = SCRATCH + 0x500
NAME_BUFFER = SCRATCH + 0x520
STACK_OBJECT = SCRATCH + 0x580
STACK_POINTER = SCRATCH + 0x5C0

SLOT0 = 0x004E87B0

GTE = 0x006A3E00

PLAYER_WINDOWS = [(0x00, 0x170), (0x29B0, 0x20), (0x2CC0, 0x20), (0x2DD4, 4), (0x2E30, 4), (0x2E58, 0x20), (0x2F14, 4), (0x2F64, 4), (0x30B8, 4), (0x30F4, 0x10), (0x317C, 4), (0x31F0, 4), (0x3210, 4)]

# the globals main.cpp reads and writes (addresses, widths): found by build/p414c/accesses.py plus the computed ones
GLOBALS = [
    (0x00524A40, 12), (0x00527D50, 8), (0x0052870C, 4), (0x0052BE20, 8), (0x0052E61C, 4), (0x0052E9D0, 4), (0x0052EB14, 4), (0x00533F38, 8), (0x00533F90, 8), (0x00533FA8, 0xD0),
    (0x00534084, 4), (0x00534110, 4), (0x00537C80, 4), (0x0053A514, 0x5C0), (0x0053C290, 0x1AC * 3 + 4), (0x0055F6B0, 4), (0x0055F6BC, 4), (0x0055FA38, 8), (0x0055FA3C, 4), (0x00560FD0, 8), (0x0056114C, 4),
    (0x00561150, 4), (0x005613BC, 4), (0x005614C4, 4), (0x005618C8, 4), (0x00561990, 4), (0x005619E0, 0x14C), (0x00561B9C, 4), (0x00561BFC, 4), (0x00561C04, 4), (0x00563A38, 4),
    (0x00563A4C, 4), (0x00563A58, 12), (0x00564368, 6), (0x00564378, 8), (0x005685E4, 4), (0x005685F4, 4), (0x00568604, 0x10), (0x00568614, 12), (0x00568624, 8), (0x00568630, 4),
    (0x00568648, 8), (0x00568658, 8), (0x0056865C, 4), (0x0056868C, 12), (0x00568804, 0x10), (0x00568A24, 4), (0x00568A58, 8), (0x0056A2DC, 4), (0x0056A386, 1), (0x0056A3A2, 1),
    (0x0056A688, 4), (0x0056A838, 4), (0x0056A854, 0x14), (0x0056A868, 8), (0x0056A88C, 4), (0x0056A898, 0x14), (0x0056A8C4, 0x10), (0x0056A8D0, 0x14), (0x0056A8E0, 12), (0x0056A8EC, 4),
    (0x0056A900, 12), (0x0056A920, 12), (0x0056A934, 0x20), (0x0056A94C, 4), (0x0056A954, 4), (0x0056A960, 4), (0x0056A970, 4), (0x0056ACF8, 4), (0x0056AF40, 8), (0x0056B018, 4),
    (0x0056B028, 4), (0x0056B038, 4), (0x0056B048, 4), (0x0056B0A8, 4), (0x0056B0D8, 4), (0x0056B0E8, 4), (0x0056B184, 4), (0x0056B194, 4), (0x0056B214, 4), (0x0056B245, 4),
    (0x0056B255, 4), (0x0056B7F4, 4), (0x0056B830, 4), (0x0056B8A8, 0x280), (0x0056B8EC, 0x10), (0x005615D8, 4), (0x0056152C, 4), (0x0056A908, 4), (0x0056E28C, 0x28), (0x0056DB28, 4), (0x0056DB40, 4), (0x0056DB5C, 12), (0x0056DB8C, 4), (0x0056E310, 4),
    (0x0056E31C, 8), (0x0056E454, 4), (0x0056E470, 4), (0x0056E4A0, 8), (0x0056E4A4, 0x28), (0x0056E5D8, 12), (0x0056E5F8, 4), (0x0056E624, 0x34), (0x0056E774, 4), (0x0056E840, 4),
    (0x006A0590, 0x10), (0x006A1400, 4), (0x006A3D14, 4), (0x006A3D26, 4), (0x029DA394, 4), (0x029DA398, 4), (0x029DA3A0, 4), (GTE, 0x120)]

STATE = {"cheat": {}, "cam": 0, "region": -1, "playing": 0, "material": 0, "bits": 0, "size": 0x40, "fmv": [], "logo": [], "frames": [], "vmode": 0, "ai": 0, "storage": [], "last": 0, "sprintf": b"\0" * 8,
         "normal": b"", "counts": [], "unlock": 0, "dims": (0, 0), "slab": 0}


def mn_regions(slab):
    windows = [(slab + RENDER, 0x100), (slab + OT_A, 0x300), (slab + OT_B, 0x300), (slab + POLY, POLY_BYTES), (slab + PTROBJ, 0x40), (slab + MATERIAL, 0x40), (slab + REPLAY_A, 0x300),
               (slab + REPLAY_B, 0x30), (slab + STREAM_START, 0x50), (slab + TEXT, 0x100), (slab + VTABLE, 0x10), (slab + FONT, 0x150), (slab + ARENA, ARENA_SIZE), (slab + CAM0 + 8, 0x10),
               (slab + CAM0 + 0x504, 4), (slab + CAM1 + 8, 0x10), (slab + CAM1 + 0x504, 4), (slab + OWNER, 0x40), (slab + CONFIG, 0x100), (slab + LISTOBJ, 0x400)]
    for base in (P0, P1):
        windows += [(slab + base + off, size) for off, size in PLAYER_WINDOWS]
    windows += list(GLOBALS)
    return merged(windows, gap=0)


def mn_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    STATE["slab"] = env.slab
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = mn_regions
    READ_ONLY[_group] = []
    SEEDS[_group] = mn_seed


def u(value):
    return value & 0xFFFFFFFF


def mn(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "mn", this=resolve)


# ---- the stubs -----------------------------------------------------------------------------------------------------------------------------------------------------------------------

def pop(key, default=0):
    return STATE[key].pop(0) if STATE[key] else default


def cheat_value(args):
    return STATE["cheat"].get(args[0], 0)


def arena_storage(args):
    """TRG_AllocObjectStorage: the player and camera objects the slab holds, in order of request."""
    size = args[0]
    slab = STATE["slab"]
    if STATE.get("alloc_null") and size == 0x54:
        return 0
    if size == 0x3538:
        base = [P0, P1][min(STATE["storage"].count(0x3538), 1)]
        STATE["storage"].append(0x3538)
        STATE["last"] = slab + base
    elif size == 0x674:
        base = [CAM0, CAM1][min(STATE["storage"].count(0x674), 1)]
        STATE["storage"].append(0x674)
        STATE["last"] = slab + base
    else:
        STATE["last"] = slab + LISTOBJ
    return STATE["last"]


def last_storage(args):
    return STATE["last"]


def sprintf_fill(args):
    return STATE["sprintf"]


def normalize_fill(args):
    return STATE["normal"][:12]


def scripted(key):
    return lambda a: STATE[key]


# (address, number of stack arguments, keyword arguments of the stub); thiscall services record `this` first
SERVICES = []


def svc(address, args=0, **kw):
    SERVICES.append((address, args, kw))


def tsvc(address, args=0, **kw):
    SERVICES.append((address, args, dict(convention="thiscall", this=True, **kw)))


svc(0x00416980, 1, result=cheat_value)
svc(0x00473580, 4)
svc(0x004734F0, 1)
svc(0x004734B0, 1)
svc(0x004739E0, 4)
svc(0x0042FC70, 0)
tsvc(0x0040F1F0, 0, result=scripted("cam"))
svc(0x00480FF0, 1)
svc(0x004B3230, 1, result=scripted("region"))
svc(0x00461CF0, 1)
svc(0x004DBEA0, 0, writes=[(0x0056A928, 4, lambda a: STATE["vmode"])])
svc(0x0048D090, 0)
svc(0x00489CD0, 0)
svc(0x0044EAC0, 0)
svc(0x00489E70, 0)
svc(0x0044CA60, 0)
svc(0x004737C0, 0)
svc(0x0048AC60, 7)
svc(0x004499B0, 0)
svc(0x0045E8E0, 3)
svc(0x004328C0, 2)
svc(0x0045F530, 1)
tsvc(0x0048EC00, 0)
tsvc(0x0046DA70, 0)
tsvc(0x0048FC90, 1)
svc(0x004CDE70, 0, result=scripted("playing"))
svc(0x00460A90, 1)
tsvc(0x0048F590, 0)
svc(0x00407310, 0)
svc(0x00460CA0, 1)
svc(0x00465F60, 1, stack_to=NORMALIZE, fill=(0, normalize_fill))
tsvc(0x00456580, 0)
svc(0x0045F3F0, 0)
svc(0x00461BF0, 1)
svc(0x0042FC60, 0, result=scripted("otsize"))
svc(0x004B2030, 1, result=scripted("material"))
svc(0x00449030, 1, stack_to=NAME_BUFFER, text=True, result=scripted("size"))
svc(0x00449230, 1, pokes=[(0, 2, lambda a: STATE["bits"], 0x1C)])
svc(0x00449660, 0)
svc(0x004D8C60, 6, stack_to=NAME_BUFFER, text=3)
svc(0x0047FD30, 1, result=arena_storage)
tsvc(0x004564B0, 2, result=last_storage)
svc(0x004B8380, 0)
svc(0x00489610, 0)
svc(0x004629F0, 0)
svc(0x00492A90, 0)
svc(0x004BD1E0, 0)
tsvc(0x004B1430, 1, result=scripted("record"))
tsvc(0x004B14A0, 1)
svc(0x004B18D0, 1)
svc(0x00423BE0, 2)
tsvc(0x0046D940, 1, stack_to=STACK_POINTER, text=True)
svc(0x004B47A0, 5, stack_to=STACK_POINTER, pokes=[(3, 4, lambda a: pop("counts", 1))], result=lambda a: STATE["slab"] + LISTOBJ + 0x300)
tsvc(0x00480D90, 4, stack_to=STACK_POINTER)
tsvc(0x0048FBE0, 1, result=scripted("last"))
svc(0x004B4E70, 1, result=scripted("model"))
svc(0x004547F0, 2, result=lambda a: STATE["slab"] + ENTRY)
svc(0x005002A1, 3, stack_to=STACK_POINTER, fill=(0, sprintf_fill))
svc(0x004C4570, 0)
tsvc(0x0046C720, 0, result=last_storage)
tsvc(0x004B0C60, 1, result=scripted("slotres"))
svc(0x004DDF30, 1)
tsvc(0x0040B650, 2, result=last_storage)
tsvc(0x004B0CD0, 1, result=scripted("costume"))
svc(0x004B1700, 2, result=scripted("modelname"))
tsvc(0x004B1630, 1, result=scripted("selectable"))
tsvc(0x0048F1D0, 0)
svc(0x00419420, 0, result=scripted("replay_flag"))
tsvc(0x00410BB0, 0)
svc(0x00452DE0, 0, result=scripted("replaying"))
svc(0x004CDC20, 0, result=scripted("last"))
svc(0x0047B6E0, 3, result=scripted("card_status"))
svc(0x00414650, 1, result=lambda a: STATE["goals"].get(a[0], 0))
tsvc(0x004B0C30, 0, result=scripted("last"))
svc(0x00416050, 1, result=lambda a: STATE["slab"] + CONFIG)
svc(0x004B1980, 2, result=scripted("last"))
svc(0x004CBCE0, 4)
svc(0x004CBC40, 0)
svc(0x004497E0, 0)
svc(0x0042F2F0, 0)
svc(0x0042F300, 0)
svc(0x004BE350, 0)
svc(0x00489A10, 0)
svc(0x004895C0, 1)
svc(0x0048B620, 8)
svc(0x0044B320, 0)
svc(0x00454E70, 1)
svc(0x004ABE50, 0)
svc(0x0042FFC0, 0)
svc(0x0042FED0, 0)
svc(0x004301B0, 0)
svc(0x0044BDE0, 0)
svc(0x00492B20, 0)
svc(0x004B32F0, 3)
svc(0x00454D10, 1)
svc(0x004302C0, 4)
svc(0x004B37A0, 4)
svc(0x004B3DF0, 0)
svc(0x00452A80, 1, result=scripted("unlock"))
tsvc(0x00480730, 4)
svc(0x004D0EC0, 0)
svc(0x004E3F80, 1)
svc(0x004DBD90, 0)
svc(0x004DEB00, 0)
svc(0x004ABCB0, 1)
svc(0x004ABC80, 0)
svc(0x004F7CE0, 0)
svc(0x004F5FF0, 0)
svc(0x004DAE00, 0)
svc(0x00469DE0, 0)
svc(0x004E02D0, 0, writes=[(0x0056A8D0, 4, lambda a: pop("frames", 7))])
svc(0x004E7370, 0)
svc(0x004DBE60, 0)
svc(0x004DEB50, 0)
svc(0x0042F370, 0)
tsvc(0x0044A070, 4, stack_to={-1: STACK_OBJECT})
tsvc(0x0044A060, 0, stack_to={-1: STACK_OBJECT})
svc(0x004304B0, 0)
svc(0x0044B3A0, 1)
svc(0x004E5EC0, 1)
svc(0x00430510, 0)
tsvc(SLOT0, 1)

# the call sites with a scripted side effect (a stub on the `call` instruction reports the call as the same service and writes the flags the callee sets)
def fmv_writes():
    """The three skip flags a movie leaves, popped as a triple per call: the writes of one stub call read the triple's three members in order."""
    state = {"triple": (0, 0, 0), "index": 0}

    def make(index):
        def produce(args):
            if index == 0:
                state["triple"] = pop("fmv", (0, 0, 0))
            return state["triple"][index]

        return produce

    return [(0x0056B0D8, 1, make(0)), (0x0056B038, 1, make(1)), (0x0056B048, 1, make(2))]


def logo_writes():
    state = {"quad": (0, 0, 0, 0)}

    def make(index):
        def produce(args):
            if index == 0:
                state["quad"] = pop("logo", (0, 0, 0, 0))
            return state["quad"][index]

        return produce

    return [(0x0056B0D9, 1, make(0)), (0x0056B029, 1, make(1)), (0x0056B245, 1, make(2)), (0x0056B195, 1, make(3))]


STATE["goals"] = {}
STATE["used"] = 0
STATE["otsize"] = 4
STATE["model"] = 0
STATE["record"] = 0
STATE["selectable"] = 0
STATE["replay_flag"] = 0
STATE["replaying"] = 0
STATE["card_status"] = 0


def disable_movie_breakpoints():
    """tony-skip-movies keeps breakpoints on StartBinkMovie and PlayStartupMedia: they would intercept the stubs of the movie calls."""
    import gdb

    for point in gdb.breakpoints() or []:
        try:
            where = str(point.location or "")
        except gdb.error:
            continue
        if any(f"0x{a:x}" in where.lower() for a in (0x004E5EC0, 0x004E6590)):
            point.enabled = False


def lock_scheduler():
    """A forced call that runs many stubs must not share the processor with the timer thread: a breakpoint hit by two threads at once trips gdb (finish_step_over)."""
    import gdb

    try:
        gdb.execute("set scheduler-locking on")
    except gdb.error as exc:
        gdb.write("scheduler-locking unavailable: %s" % exc + chr(10))


def install_mn_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_mn_installed", False):
        return
    fc._mn_installed = True
    mark_under_test()
    stop_clock(fc)
    disable_movie_breakpoints()
    lock_scheduler()
    STATE["slab"] = env.slab
    for address, count, kw in SERVICES:
        ensure(fc, address, count, "engine_%08x" % address, **kw)
    fc.stub_site(0x004685A3, 5, 1, "engine_00480ff0", writes=[(0x0056A900, 4, lambda a: STATE["ai"])])
    fc.stub_site(0x00466BBC, 5, 1, "mem_delete", writes=[(0x0056A970, 4, lambda a: STATE["used"])])
    for address in (0x0046A51C, 0x0046A541, 0x0046A573):
        fc.stub_site(address, 5, 1, "engine_00454d10", writes=fmv_writes())
    for address in (0x0046A58F, 0x0046A5BA):
        fc.stub_site(address, 5, 0, "engine_00489a10", writes=logo_writes())
    fc.stub_site(0x00468F4C, 5, 2, "engine_004b1980", result=scripted("last"))
    fc.stub_site(0x00469182, 5, 1, "engine_004b1980", result=scripted("last"))


GROUP_STUBS = {group: install_mn_stubs for group in GROUPS}
