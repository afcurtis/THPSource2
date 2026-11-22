"""Phase 5 / 10g s15 probe: the base class's script commands (group wqs: 0x004015e0 TRG_ExecuteBaddyScriptOpcode).

The object is a platform (vtable 0x005194f8: slot 2 the die method, slot 8 the field handler 0x004a1060, slot 9 the variable handler 0x004a1270) at the slab's start; every callee is a service.
The variable handler's stub moves the script cursor by a scripted number of words (`variable_cursor`: a variable that reads operand words), so the arithmetic commands stage words;
the field handler's stub records the staged words the cursor points at (a stack buffer in retail).

Slab layout (offsets): object 0x000 (0x230 bytes), stream 0x300 (0x100), node pointers 0x400 (8) and their records 0x420 (8 x 4 bytes), career record 0x480 (0x40), power-up and bit 0x700,
the skaters' fields at 0x600 (+0x29b0, the camera) and 0x910 (+0x2cc0, the character).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import OBJ, fill_object, noise

GROUPS = ("wqs", "wqt", "wqf", "wqv", "wqp", "wqk", "wqa")

SLAB = 0xA00
PLATFORM_VTABLE = 0x005194F8
STREAM, NODES, RECORDS, CAREER, BLOCK = 0x300, 0x400, 0x420, 0x480, 0x700
CAMERA_AT, CHARACTER_AT = 0x600, 0x910
SKATER_A = CAMERA_AT - 0x29B0          # (a skater's +0x29b0 and +0x2cc0 land in the slab)

NODE_TABLE, GAME_MODE, SKATERS, LEVEL, REPLAY_FLAGS = 0x0056E210, 0x00533F38, 0x0056A858, 0x0056A898, 0x0056A838
SHAKE_ON, CLOCK, BIT_COUNT, SPRITE_FLAG, TARGET_SKATER = 0x0055FA38, 0x0056E320, 0x0055F72C, 0x005244CC, 0x0056A960
CAMERA_HEIGHTS, WORD_42A2, SIGNAL_LIST, ACTIVE, LINE, FILE = 0x00533F90, 0x00568684, 0x0055F6BC, 0x0056AF40, 0x00568638, 0x0056881C
WORD_2136 = 0x00561CFC
SOUND_REFERENCE = 0x0056BED8
LINKS = 0x7C0
HIGH_RECORDS = 0x840
GLOBALS = [(NODE_TABLE, 4), (GAME_MODE, 4), (SKATERS, 8), (LEVEL, 4), (REPLAY_FLAGS, 4), (SHAKE_ON, 4), (CLOCK, 4), (BIT_COUNT, 4), (SPRITE_FLAG, 4), (TARGET_SKATER, 4),
           (CAMERA_HEIGHTS, 8), (WORD_42A2, 4), (SIGNAL_LIST, 4), (ACTIVE, 4), (LINE, 4), (FILE, 0x40), (WORD_2136, 4), (SOUND_REFERENCE, 4)]

SCRATCH = 0x7FF00800 + 0xC00              # worldkit::kScratch
NODE_LOCAL, CONE_LOCAL, SIGHT_LOCAL = SCRATCH + 0x2C0, SCRATCH + 0x2E0, SCRATCH + 0x2F0

MODE = {"small": False}                     # the variable handler's answers: small node indices (C_SET_SCRIPT_NODE) or any 16-bit value


def regions(slab):
    return merged([(slab, SLAB)] + GLOBALS, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(PLATFORM_VTABLE, 0x30)]


def install_stubs(fc, env):
    from opentony.memory import mem
    from world_specs_platform import install_stubs as install_platform_stubs

    if getattr(fc, "_wqs_installed", False):
        return
    fc._wqs_installed = True
    install_platform_stubs(fc, env)
    rng = env.rng
    cursor = lambda: env.slab + OBJ + 0x17C

    def variable(a):
        if MODE["small"]:
            return rng.randrange(0, 8) | rng.choice([0, 0, 0x10000, 0xFFFF0000])
        return rng.choice([0, 1, 2, 0xFFFF, 0x8000, 0x7FFF, 0xFFFE, rng.getrandbits(16), rng.getrandbits(16)]) | rng.choice([0, 0, 0, 0x10000, 0xFFFF0000])

    ensure(fc, 0x004A1270, 1, "engine_004a1270", convention="thiscall", this=True, masks={0: 0xFFFF}, result=variable,
           writes=[(cursor(), 4, lambda a: mem.u32(cursor()) + 2 * rng.choice([0, 0, 0, 1, 1, 2, 3]), "variable_cursor")])
    staged = lambda ecx, real, esp: mem.bytes(mem.u32(ecx + 0x17C), 2 * (mem.u32(esp + 0x18) + 1))     # (the count the arithmetic command keeps at [its esp + 0x10])
    ensure(fc, 0x004A1060, 1, "engine_004a1060", convention="thiscall", this=True, masks={0: 0xFFFF}, blob=[(staged, None)])
    ensure(fc, 0x004014A0, 0, "engine_004014a0", convention="thiscall", this=True)                      # TRG_SkipConditionalBlock
    ensure(fc, 0x004C8650, 2, "engine_004c8650", stack_to=NODE_LOCAL, result=lambda a: rng.getrandbits(32))   # TRG_DecodeNodePosition (the script point's record)
    ensure(fc, 0x004CDE70, 0, "engine_004cde70", result=lambda a: rng.choice([0, 0, 0, 1, 0x100, 0xFFFFFF00]))   # Video_Playing (AL)
    ensure(fc, 0x00416050, 1, "engine_00416050", result=lambda a: env.slab + CAREER)                   # TrickConfig_FindByCharacter
    ensure(fc, 0x0047FE30, 1, "engine_0047fe30", convention="thiscall", this=True)                      # Object_SelectNamedPsxRegion
    ensure(fc, 0x00480890, 2, "engine_00480890", convention="thiscall", this=True, masks={1: 0xFFFF})  # Animation_Cycle (the direction is a word in edx: the caller's upper half)
    ensure(fc, 0x00480730, 4, "engine_00480730", convention="thiscall", this=True)                      # Animation_Start
    ensure(fc, 0x0040BD40, 2, "engine_0040bd40", convention="thiscall", this=True)                      # Camera_ApplyShake
    ensure(fc, 0x004ADC50, 1, "engine_004adc50")
    ensure(fc, 0x004ADC30, 1, "engine_004adc30")
    ensure(fc, 0x00406070, 1, "engine_00406070")                                                        # the spark trajectory, cone, size, colour, fade
    ensure(fc, 0x00406090, 1, "engine_00406090", stack_to=CONE_LOCAL, blob=(0, 6))
    ensure(fc, 0x00406160, 1, "engine_00406160")
    ensure(fc, 0x004061C0, 3, "engine_004061c0")
    ensure(fc, 0x004061E0, 3, "engine_004061e0")
    ensure(fc, 0x0046F2C0, 1, "engine_0046f2c0", result=lambda a: rng.choice([0x100000, 0x2711, 0x2710, 0x270F, 5, 0xFFFFFFFF]))   # Mem_GetFree
    ensure(fc, 0x00403D30, 1, "engine_00403d30", result=lambda a: rng.choice([0, env.slab + BLOCK, env.slab + BLOCK]))             # CBit_New
    ensure(fc, 0x00406200, 4, "engine_00406200", convention="thiscall", this=True)                      # CSpark_Construct
    ensure(fc, 0x004A8E50, 5, "engine_004a8e50", result=lambda a: rng.choice([0, env.slab + BLOCK]))  # Powerup_CreateFromTrigger
    ensure(fc, 0x004A8260, 2, "engine_004a8260", convention="thiscall", this=True)                      # Powerup_SetFields
    ensure(fc, 0x004C8550, 1, "engine_004c8550", result=lambda a: rng.choice([env.slab + LINKS, env.slab + LINKS + 4]))   # TRG_GetNodeLinks (a link record: its count, then the first)
    ensure(fc, 0x004C95E0, 2, "engine_004c95e0", result=lambda a: rng.choice([0, 1, 0x1FFE, 0x1FFF, 0x2000, 0x2001, 0xFFFFFFFF, 0x80000000, rng.getrandbits(14), rng.getrandbits(32)]))   # Utils_XZDist
    ensure(fc, 0x004CA0C0, 2, "engine_004ca0c0", stack_to=SIGHT_LOCAL, blob=(0, 12), result=lambda a: rng.choice([0, 1, 0x10001, rng.getrandbits(32)]))   # Utils_LineOfSight
    ensure(fc, 0x004015E0, 1, "engine_004015e0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 1, 0x10001]))   # the base commands (stubbed while 0x00403410 is tested)
    ensure(fc, 0x00402BB0, 1, "engine_00402bb0", convention="thiscall", this=True, result=lambda a: rng.getrandbits(32))   # TRG_GetScriptVariable
    ensure(fc, 0x004C5B60, 1, "engine_004c5b60")
    ensure(fc, 0x004C5B00, 1, "engine_004c5b00")
    ensure(fc, 0x004C5B30, 2, "engine_004c5b30")
    ensure(fc, 0x004C58B0, 1, "engine_004c58b0")
    ensure(fc, 0x00449810, 6, "engine_00449810")                                                        # Fade_Start
    ensure(fc, 0x00414650, 1, "engine_00414650", result=lambda a: rng.choice([0, 0, 1, 0x100]))       # Career_GotGoalType
    ensure(fc, 0x0048D8A0, 1, "engine_0048d8a0")                                                        # Panel_CollectibleCounted


GROUP_STUBS = {group: install_stubs for group in GROUPS}

COMMANDS = ([0x4106] * 3 + [0x4104] * 2 + [0x4105] * 3 + [0x4101] * 2 + [0x4102] * 2 + [0x4115, 0x4116] * 2 + [0x4121] * 3 + [0x4122] * 3 + [0x4123] * 3 + [0x4110, 0x4111] * 4
            + [0x4112, 0x4113, 0x4114] * 2 + [0x4107, 0x4120, 0x4200, 0x4200, 0x4201, 0x4202, 0x4203, 0x4204, 0x4205, 0x4226, 0x4227, 0x4227, 0x4298, 0x4298, 0x4298, 0x4296, 0x4297,
                                            0x4293, 0x4293, 0x4294, 0x4280, 0x4280, 0x4280, 0x4281, 0x4281, 0x4240, 0x4290, 0x4291, 0x4292, 0x4292, 0x4292, 0x4295, 0x4299, 0x42A0,
                                            0x42A0, 0x429E, 0x429E, 0x42A2, 0x429A, 0x429A, 0x42A6, 0x42A6, 0x429D, 0x42B1, 0x42B2, 0x42B3, 0x42B4, 0x42B0, 0x42B0, 0x42A3, 0x429C,
                                            0x429C, 0x42C0, 0x42C0, 0x42C0, 0x450A]
            + [0x4100, 0x4103, 0x4108, 0x4117, 0x4124, 0x4206, 0x4225, 0x4228, 0x427F, 0x429B, 0x42A1, 0x42B5, 0x42BF, 0x42C1, 0x4509, 0x450B, 0x0000, 0x2123, 0xFFFF])


def operand(rng):
    """A stream word: a plain value or one that names a variable (bit 13)."""
    return rng.choice([0, 1, 2, 3, 7, 8, 0xFFFF, 0x8000, 0x7FFF, 0x1234, 0x2000, 0x2001, 0x2105, 0x21FF, 0x3000, 0xA000, rng.getrandbits(16)])


AIMED = [0x4106, 0x4106, 0x4104, 0x4104, 0x4121, 0x4121, 0x4122, 0x4123, 0x4123, 0x4112, 0x4113, 0x4114, 0x4112, 0x4227, 0x4227, 0x4290, 0x4290, 0x42A0, 0x42A0]


@spec(0x004015E0, "TRG_ExecuteBaddyScriptOpcode", "wqs", this=lambda env: env.slab + OBJ)
def gen_base_command(env, n):
    return base_case(env, False)


@spec(0x004015E0, "TRG_ExecuteBaddyScriptOpcode", "wqt", this=lambda env: env.slab + OBJ)
def gen_base_command_edges(env, n):
    """The edges the first fixture's surviving mutants need: node indices above 0xff, labels 8 and 9, flag 7, goals 10 and 11, equal operands, a cleared flash, negative sounds, a variable power-up kind."""
    return base_case(env, True)


def base_case(env, aimed):
    img, rng = Image(), env.rng
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.u32(o, PLATFORM_VTABLE)
    command = rng.choice(AIMED if aimed else COMMANDS)
    if rng.random() < 0.15:
        command |= rng.getrandbits(16) << 16     # (the high half of the argument is the caller's)
    op = command & 0xFFFF
    MODE["small"] = op == 0x4106
    stream = env.slab + STREAM
    words = [operand(rng) for _ in range(0x40)]
    words[-8:] = [0x4100] * 8                      # (a backstop for the label scan)
    cursor = stream + rng.choice([0, 0, 2, 4, 6, 8]) + (1 if rng.random() < 0.1 and op in (0x4200, 0x42B0, 0x4293) else 0)
    first = (cursor - stream) // 2
    if op == 0x4106:
        words[first] = rng.randrange(0, 8) | (0x2000 if rng.random() < 0.3 else 0)
    elif op in (0x4104, 0x4101, 0x4102):
        words[first] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9])
    elif op == 0x4105:
        at = first
        for _ in range(rng.choice([0, 1, 2, 3, 5])):
            if rng.random() < 0.6:
                words[at], words[at + 1] = 0x4104, rng.choice([0, 1, 2, 5, 7, 8, 9])
                at += 2
            else:
                words[at] = rng.choice([0x4101, 0x4102, 0x2123, 0x4280, 0x0003, 0x4204, 0x4104 ^ 0x0001])
                at += 1
        words[at] = 0x4100
    elif op in (0x4121, 0x4122, 0x4123):
        words[first] = rng.choice([0, 1, 2, 3, 5, 7, 8, 9, 10, 11, 12, 15, 31, 32, 33, rng.getrandbits(16)])
    elif op in (0x4110, 0x4111):
        words[first] = rng.choice([0x2000, 0x2105, 0x2127, 0x2142, 0x2001, 0xA000, 0x6000, 0x0105, 0x2000 | rng.getrandbits(13)])
    elif op in (0x4200, 0x42B0):
        text = bytes(rng.randrange(1, 256) for _ in range(rng.choice([0, 1, 2, 3, 4, 7, 8, 12])))
        raw = bytearray(b"".join(w.to_bytes(2, "little") for w in words))
        offset = cursor - stream
        raw[offset:offset + len(text) + 1] = text + b"\0"
        words = [int.from_bytes(raw[k:k + 2], "little") for k in range(0, len(raw), 2)]
    elif op in (0x4227, 0x4292):
        words[first] = rng.choice([0, 0, 1, 2, 3, 4, 0x100, 0xFFFF, rng.getrandbits(16)]) if op == 0x4227 else rng.choice([0, 1, 2, 3, 4])
    elif op == 0x4298:
        words[first] = rng.choice([0, 1, 2, 0x10000 - 1, 0x2000, 0x2001, 0x0100, 0x0101])
    elif op in (0x429A, 0x42A6):
        words[first + (1 if op == 0x429A else 0)] = rng.choice([0, 1, 2, 2, 2, 3, 0xFFFF, 0x8000, 0x100])
    if aimed:
        if op == 0x4106:
            words[first] = 0x100 + rng.randrange(0, 8) if rng.random() < 0.7 else rng.randrange(0, 8)
        elif op == 0x4104:
            words[first] = rng.choice([7, 8, 8, 9])
        elif op in (0x4121, 0x4122):
            words[first] = rng.choice([6, 7, 7, 8])
        elif op == 0x4123:
            words[first] = rng.choice([9, 10, 11, 11])
        elif op in (0x4112, 0x4113, 0x4114):
            words[first] = rng.choice([0, 1, 5, 0x1234, 0x7FFF, 0x8000, 0xFFFF]) & 0xDFFF
            words[first + 1] = words[first] if rng.random() < 0.6 else (words[first] + rng.choice([1, 0xFFFF])) & 0xDFFF
        elif op == 0x4227:
            words[first] = rng.choice([0, 0, 1])
        elif op == 0x4290:
            words[first] = rng.choice([0x8000, 0xFFFF, 0xFFFE, 0x8001, 5])
        elif op == 0x42A0:
            words[first] = rng.choice([0x2000, 0x2105, 0x2001])
    for k, w in enumerate(words):
        img.u16(stream + 2 * k, w)
    img.u32(o + 0x17C, cursor)
    # the object's fields the commands read
    for k in range(10):
        img.u32(o + 0x180 + 4 * k, rng.choice([0, 0, stream + 2 * rng.randrange(0, 0x30), rng.getrandbits(32)]))
    img.u16(o + 0x4, rng.choice([0, 0x100, 0x1, 0x41, 0xFFFF, rng.getrandbits(16)]))
    img.u32(o + 0x178, rng.choice([0, 0xFFFFFFFF, 2, 0x10, rng.getrandbits(32), rng.getrandbits(32) & 0xFFFF]))
    img.u8(o + 0x174, rng.choice([0, 1, 2, 3]))
    target = rng.choice([0, 0, env.slab + CAMERA_AT, rng.getrandbits(32)])
    img.u32(o + 0x1D8, target)
    img.u32(TARGET_SKATER, rng.choice([target, target, rng.getrandbits(32)]))
    # the node table and its records (type 1000 is a script point)
    img.u32(NODE_TABLE, env.slab + NODES)
    for k in range(8):
        img.u32(env.slab + NODES + 4 * k, env.slab + RECORDS + 4 * k)
        img.u16(env.slab + RECORDS + 4 * k, rng.choice([1000, 1000, 1000, 999, 1001, 0, rng.getrandbits(16)]))
        img.u16(env.slab + RECORDS + 4 * k + 2, rng.getrandbits(16))
        if aimed:        # (nodes 0x100..0x107: their entries 0x400 bytes on, records of another type than their low byte's)
            img.u32(env.slab + NODES + 0x400 + 4 * k, env.slab + HIGH_RECORDS + 4 * k)
            img.u16(env.slab + HIGH_RECORDS + 4 * k, rng.choice([1000, 999, 0]))
            img.u16(env.slab + HIGH_RECORDS + 4 * k + 2, rng.getrandbits(16))
    # the career, the skaters, the level
    img.put(env.slab + CAREER, noise(rng, 0x40))
    img.u32(LEVEL, rng.choice([0, 1, 2, 5, 9, 10, 15]))
    img.u8(REPLAY_FLAGS, rng.getrandbits(8))
    img.u32(GAME_MODE, rng.choice([1, 1, 1, 0, 2]))
    skaters = [rng.choice([env.slab + SKATER_A, env.slab + SKATER_A, 0]), rng.choice([env.slab + SKATER_A + 4, 0, 0])]
    if skaters[0] == 0 and op in (0x4121, 0x4122, 0x4123):
        skaters[0] = env.slab + SKATER_A        # (the career lookup reads the first skater)
    img.u32(SKATERS, skaters[0])
    img.u32(SKATERS + 4, skaters[1])
    for at in (CAMERA_AT, CAMERA_AT + 4, CHARACTER_AT, CHARACTER_AT + 4):
        img.u32(env.slab + at, rng.getrandbits(32))
    img.u32(SHAKE_ON, rng.choice([0, 1, 1]))
    img.u32(CLOCK, rng.getrandbits(32))
    img.u32(BIT_COUNT, rng.choice([0, 0xF9, 0xFA, 0xFB, 0x10, 0xFFFFFFFF]))
    img.u32(SPRITE_FLAG, rng.choice([0, 1, 7]))
    img.u32(SIGNAL_LIST, rng.getrandbits(32))
    img.u32(ACTIVE, rng.getrandbits(32))
    for address, size in ((CAMERA_HEIGHTS, 8), (WORD_42A2, 4), (LINE, 4), (FILE, 0x40)):
        img.put(address, noise(rng, size))
    return [command], img.pokes()


def script_object(env, rng, img):
    """The platform at the slab's start, with a stream of random operand words at 0x300 (the cursor somewhere in its first words)."""
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.u32(o, PLATFORM_VTABLE)
    stream = env.slab + STREAM
    for k in range(0x80):
        img.u16(stream + 2 * k, operand(rng))
    cursor = stream + rng.choice([0, 0, 2, 4, 6, 1, 3])
    img.u32(o + 0x17C, cursor)
    for address, size in ((LINE, 4), (FILE, 0x40)):
        img.put(address, noise(rng, size))
    return o, cursor


FIELDS = [0x2135, 0x212F, 0x2140, 0x2141, 0x2142, 0x2131, 0x212C, 0x212D, 0x2100, 0x2101, 0x2125, 0x2125, 0x212E, 0x2114, 0x2115, 0x2120, 0x2120, 0x2121, 0x2122,
          0x2102, 0x2113, 0x2116, 0x211F, 0x2123, 0x2124, 0x2130, 0x2136, 0x213F, 0x2143, 0x20FF, 0x2200, 0x0000, 0x6123, 0xFFFF]


@spec(0x004027F0, "TRG_ApplyScriptFieldWord", "wqf", this=lambda env: env.slab + OBJ)
def gen_base_field(env, n):
    img, rng = Image(), env.rng
    o, cursor = script_object(env, rng, img)
    MODE["small"] = False
    field = rng.choice(FIELDS)
    if rng.random() < 0.15:
        field |= rng.getrandbits(16) << 16
    if field & 0xFFFF == 0x2125:
        img.u16(cursor, rng.choice([0, 1, 2, 3, 4, 5, 5, 6, 7]))
    elif field & 0xFFFF == 0x2120:
        img.u16(cursor, rng.choice([0, 1, 2, 5, 6, 7]) | rng.choice([0, 0, 0x100, 0xFF00]))     # (the low byte indexes)
    elif field & 0xFFFF == 0x2131:
        img.u16(cursor, rng.choice([0, 0, 1, 0x100, 0x8000, 0xFFFF]))
    img.u16(o + 0x4, rng.choice([0, 8, 0xFFFF, rng.getrandbits(16)]))
    return [field], img.pokes()


VARIABLES = [0x2136, 0x2132, 0x2132, 0x2133, 0x2133, 0x212E, 0x212C, 0x212D, 0x2140, 0x2141, 0x2142, 0x2150, 0x2151, 0x2152, 0x2100, 0x2129, 0x2120, 0x2120, 0x212A, 0x212A,
             0x212B, 0x212B, 0x212B, 0x2101, 0x2131, 0x2135, 0x2153, 0x20FF, 0x2200, 0x0000, 0xFFFF]


@spec(0x00402BB0, "TRG_GetScriptVariable", "wqv", this=lambda env: env.slab + OBJ)
def gen_base_variable(env, n):
    img, rng = Image(), env.rng
    o, cursor = script_object(env, rng, img)
    MODE["small"] = False
    variable = rng.choice(VARIABLES)
    if rng.random() < 0.15:
        variable |= rng.getrandbits(16) << 16
    if variable & 0xFFFF == 0x2120:
        img.u16(cursor, rng.choice([0, 1, 2, 5, 6, 7]) | rng.choice([0, 0, 0x100, 0xFF00]))
    elif variable & 0xFFFF == 0x212B:
        img.u16(cursor, rng.choice([0, 3, 0x2000, 0x2105, 0x1FFF, 0xFFFF]))
    elif variable & 0xFFFF == 0x212A:
        img.u16(o + 0xB0, rng.choice([0xFFFF, 0, 5, rng.getrandbits(16)]))
    skater = rng.choice([0, env.slab + CAMERA_AT, env.slab + CAMERA_AT])
    img.u32(TARGET_SKATER, skater)
    img.put(env.slab + CAMERA_AT, noise(rng, 0x20))
    img.put(env.slab + LINKS, bytes([0, 0]) + noise(rng, 2) + noise(rng, 8))
    img.u32(WORD_2136, rng.getrandbits(32))
    return [variable], img.pokes()


@spec(0x004A1270, "CPlatform__GetScriptVariable", "wqp", this=lambda env: env.slab + OBJ)
def gen_platform_variable(env, n):
    img, rng = Image(), env.rng
    script_object(env, rng, img)
    variable = rng.choice([0x2200, 0x2200, 0x2201, 0x21FF, 0x2132, 0x2100, 0x0000, 0xFFFF, rng.getrandbits(16)])
    if rng.random() < 0.3:
        variable |= rng.getrandbits(16) << 16
    return [variable], img.pokes()


@spec(0x004014A0, "TRG_SkipConditionalBlock", "wqk", this=lambda env: env.slab + OBJ)
def gen_skip_block(env, n):
    img, rng = Image(), env.rng
    o, cursor = script_object(env, rng, img)
    words = []
    for _ in range(rng.choice([0, 1, 2, 4, 8, 16])):
        words.append(rng.choice([0x4112, 0x4113, 0x4114, 0x4115, 0x4116, 0x4111, 0x4117, 0x4120, 0x4120, 0x0001, 0x2123, 0x4280, 0x4121, 0x411F]))
    words.append(rng.choice([0x4120, 0x4120, 0x4100]))
    words += [0x4120] * 24 + [0x4100] * 4         # (whatever nesting the random words opened is closed, then the terminator)
    for k, w in enumerate(words):
        img.u16(cursor + 2 * k, w)
    return [], img.pokes()


@spec(0x00403410, "TRG_ApplyScriptCommand", "wqa", this=lambda env: env.slab + OBJ)
def gen_script_object_command(env, n):
    img, rng = Image(), env.rng
    o, cursor = script_object(env, rng, img)
    command = rng.choice([0x4506, 0x4506, 0x429B, 0x42A1, 0x429F, 0x4503, 0x4504, 0x4505, 0x4507, 0x4507, 0x4508, 0x4508, 0x4509, 0x4700, 0x4700, 0x4700,
                          0x4100, 0x4101, 0x4280, 0x429A, 0x429C, 0x42A0, 0x4502, 0x450A, 0x46FF, 0x4701, 0xFFFF, rng.getrandbits(16)])
    if rng.random() < 0.15:
        command |= rng.getrandbits(16) << 16
    if command & 0xFFFF == 0x4700:
        img.u16(cursor, rng.choice([0, 0, 1, 0x100, 0x8000]))
    img.u32(o + 0x1E8, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 7, rng.getrandbits(32)]))
    img.u32(o + 0x178, rng.choice([0, 0x100, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(TARGET_SKATER, rng.choice([0, env.slab + CAMERA_AT]))
    img.put(env.slab + CAMERA_AT, noise(rng, 0x20))
    img.u32(SOUND_REFERENCE, rng.getrandbits(32))
    return [command], img.pokes()


# ---- 10g s16: the script object's update 0x004031a0 (groups wru, wrv) and CPlatform__Hit 0x004a0360 (group wrh) ----------------------------------------------------------------------------------

UPDATE_GROUPS = ("wru", "wrv")
SCRIPT_VTABLE = 0x005183B0
ELEMENTS, MODELS, OUTPUTS, SKATER_AT, CAMERA_RECORD = 0x300, 0x400, 0x700, 0x800, 0x8A0       # 8 elements of 0x10, 8 models of 0x60, 8 output vectors of 0x10
UPDATE_SLAB = 0x900
FRAME_TICKS, TICK, VOLUME, CAMERA_POINTER = 0x0056A93C, 0x0056A8D4, 0x0052BE44, 0x0055FA38
UPDATE_GLOBALS = [(FRAME_TICKS, 4), (TICK, 4), (VOLUME, 4), (TARGET_SKATER, 4), (CAMERA_POINTER, 4)]


def update_regions(slab):
    return merged([(slab, UPDATE_SLAB)] + UPDATE_GLOBALS, gap=0)


for _group in UPDATE_GROUPS:
    SLAB_BYTES[_group] = UPDATE_SLAB + 0x100
    EXTRA_REGIONS[_group] = update_regions
    READ_ONLY[_group] = [(SCRIPT_VTABLE, 0x30)]

HIT_GROUPS = ("wrh",)
HIT_SLAB = 0x300
for _group in HIT_GROUPS:
    SLAB_BYTES[_group] = HIT_SLAB + 0x100
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, HIT_SLAB)], gap=0)
    READ_ONLY[_group] = [(PLATFORM_VTABLE, 0x30)]


def install_update_stubs(fc, env):
    from opentony.memory import mem
    from platform_specs import mark_under_test
    from world_specs import install_stubs as install_base_stubs

    if getattr(fc, "_wru_installed", False):
        return
    fc._wru_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    rng = env.rng

    def roll(a):
        limit = a[0]
        return rng.choice([0, limit - 1, limit // 2, rng.randrange(limit), rng.randrange(limit), rng.randrange(limit), rng.randrange(limit), rng.getrandbits(32)])

    ensure(fc, 0x004013B0, 0, "engine_004013b0", convention="thiscall", this=True)                      # the die method (vtable slot 2)
    ensure(fc, 0x00401520, 1, "engine_00401520", convention="thiscall", this=True)                      # TRG_RunScriptStream
    ensure(fc, 0x004AD8B0, 3, "engine_004ad8b0", blob=[(lambda ecx, real, esp: mem.bytes(VOLUME, 4), None)])   # SFX_ModifyPos: the volume word the port sets for the call is recorded
    ensure(fc, 0x004C9340, 1, "engine_004c9340", result=roll)                                           # Rnd (the three jitter rolls)


def install_hit_stubs(fc, env):
    from platform_specs import mark_under_test
    from world_specs import install_stubs as install_base_stubs

    if getattr(fc, "_wrh_installed", False):
        return
    fc._wrh_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    ensure(fc, 0x004013B0, 0, "engine_004013b0", convention="thiscall", this=True)                      # the die method (vtable slot 2)
    ensure(fc, 0x004AF0B0, 3, "engine_004af0b0")                                                        # Shatter_Item
    ensure(fc, 0x0047FE90, 4, "engine_0047fe90", convention="thiscall", this=True)                      # CItem__Flash


GROUP_STUBS.update({group: install_update_stubs for group in UPDATE_GROUPS})
GROUP_STUBS.update({group: install_hit_stubs for group in HIT_GROUPS})

EDGE32 = [0, 1, 2, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x1000, 0xFFFFF000]


@spec(0x004031A0, "TRG_UpdateScriptObject", "wru", this=lambda env: env.slab + OBJ)
def gen_update(env, n):
    return update_case(env, False)


@spec(0x004031A0, "TRG_UpdateScriptObject", "wrv", this=lambda env: env.slab + OBJ)
def gen_update_edges(env, n):
    """The second fixture, aimed at the first one's surviving mutants."""
    return update_case(env, True)


def update_case(env, aimed):
    img, rng = Image(), env.rng
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.put(env.slab + ELEMENTS, noise(rng, UPDATE_SLAB - ELEMENTS))
    img.u32(o, SCRIPT_VTABLE)
    img.u16(o + 0x68, rng.choice([0, 1, 2, 3, 0x100, 0x101, 0xFFFF, 0xFFFE, rng.getrandbits(16)]))
    img.u8(o + 0x16C, rng.choice([0, 0, 1, 1, 1, 2, 0xFF, rng.getrandbits(8)]))
    img.u8(o + 0x170, rng.choice([0, 1, 0xFE, 0xFF, rng.getrandbits(8)]))
    delta = rng.choice([0, 1, 2, 3, 5, 0x10, 0xFFFF, 0xFFFFFFFF, 0x80000000, rng.getrandbits(16), rng.getrandbits(32)])
    img.u32(FRAME_TICKS, delta)
    img.u32(o + 0x1C0, rng.choice([0, 0, 0, delta, (delta + 1) & 0xFFFFFFFF, (delta - 1) & 0xFFFFFFFF, 1, 2, 0xFFFFFFFF, 0x80000000, rng.getrandbits(16), rng.getrandbits(32)]))
    img.u32(o + 0x17C, rng.getrandbits(32))
    # the positional sound (a period of 0 is a divide fault in retail)
    period = rng.choice([0xFFFFFFFF] * 3 + [1, 2, 3, 4, 5, 7, 0x10, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFD, rng.randrange(1, 0x100)])
    img.u32(o + 0x1EC, period)
    signed = period - (1 << 32) if period >= 1 << 31 else period
    img.u32(TICK, rng.choice(EDGE32 + [rng.getrandbits(32), rng.getrandbits(8), (signed * rng.randrange(0, 40)) & 0xFFFFFFFF, (signed * rng.randrange(0, 40)) & 0xFFFFFFFF]))
    img.u32(VOLUME, rng.choice([0, 100, 0x7F, 0x3FFF, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, rng.getrandbits(16), rng.getrandbits(32)]))
    img.u32(o + 0x1F0, rng.choice([0, 1, 50, 99, 100, 101, 200, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, rng.getrandbits(8), rng.getrandbits(32)]))
    img.u32(o + 0x1E8, rng.choice([0xFFFFFFFF, 0, 5, rng.getrandbits(32)]))
    # the attached models
    count = rng.choice([0, 1, 1, 2, 3, 4, 5, 8]) if not aimed else rng.choice([1, 1, 2, 3, 4])
    has = rng.random() < 0.85
    img.u32(o + 0x1E0, env.slab + ELEMENTS if has else 0)
    img.u16(o + 0x1DC, count)
    skater = env.slab + SKATER_AT
    threshold = rng.choice(EDGE32 + [rng.getrandbits(32)])
    img.u32(TARGET_SKATER, skater if has else rng.choice([skater, 0, rng.getrandbits(32)]))
    img.u32(skater + 0x94, threshold)
    for k in range(3):
        img.u32(skater + 8 + 4 * k, rng.choice([0, rng.getrandbits(32), rng.getrandbits(32), 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]))
    img.u32(CAMERA_POINTER, env.slab + CAMERA_RECORD)
    for k in range(3):
        img.u32(env.slab + CAMERA_RECORD + 0x4C + 4 * k, rng.choice([0, 1, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32), rng.getrandbits(32)]))
    for index in range(8):
        record = env.slab + ELEMENTS + 0x10 * index
        model = env.slab + MODELS + 0x60 * (index if rng.random() < 0.92 else rng.randrange(8))
        offset = [rng.choice([0, 1, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(-0x4000, 0x4000) & 0xFFFFFFFF]) for _ in range(3)]
        for k in range(3):
            img.u32(record + 4 * k, offset[k])
        img.u32(record + 0xC, model)
        img.u32(model + 0x40, env.slab + OUTPUTS + 0x10 * (index if rng.random() < 0.95 else rng.randrange(8)))
        # the model's position puts the offset position's y near the skater's +0x94 (above it the model jitters)
        want = (threshold + rng.choice([-1, 0, 1, 1, -1, 2, -2, 0x1000, -0x1000, rng.getrandbits(32), rng.randrange(-0x20000, 0x20000)])) & 0xFFFFFFFF
        img.u32(model + 0x48, (want - offset[1]) & 0xFFFFFFFF)
        for k in (0, 2):
            img.u32(model + 0x44 + 4 * k, rng.getrandbits(32))
    return [], img.pokes()


@spec(0x004A0360, "CPlatform__Hit", "wrh", this=lambda env: env.slab + OBJ)
def gen_hit(env, n):
    img, rng = Image(), env.rng
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.u32(o, PLATFORM_VTABLE)
    img.u8(o + 0x16F, rng.choice([0, 1, 0xFE, 0xFF, rng.getrandbits(8)]))
    img.u16(o + 0x1A2, rng.choice([0, 0, 1, 1, 1, 2, 0xFFFF, 0x100, 0x8000, 0xFF00, rng.getrandbits(16)]))
    points = rng.choice([0, 1, 2, 3, 5, 10, 100, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0xFFFE, rng.getrandbits(16), rng.randrange(1, 20)])
    img.u16(o + 0xB4, points)
    damage = rng.choice([0, 1, 2, 3, 5, points, (points - 1) & 0xFFFF, (points + 1) & 0xFFFF, 100, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16), rng.randrange(1, 20)])
    if rng.random() < 0.2:
        damage |= rng.getrandbits(16) << 16
    return [damage, rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


# ---- 10g s21: LevelEvent_InitializeAndDispatch 0x00466c10 (groups lev, lew) -------------------------------------------------------------------------------------------------------------------------

LEVEL_GROUPS = ("lev", "lew")
VERSUS, SIDE_BYTES, COUNTERS_AT = 0x0056DB64, 0x006A3D48, 0x0056B798
LATCH, COUNTDOWN, STEP = 0x00568658, 0x00568818, 0x00568610
LEVEL_SLAB = 0x500
FIRST_AT, SECOND_AT = 0x10, 0x20                # (the +0x2cdc words of the two skaters; the second's +0x3144 is at 0x488)
LEVEL_REGIONS = [(GAME_MODE, 4), (SKATERS, 8), (COUNTERS_AT, 0x4C), (LATCH, 4), (COUNTDOWN, 4), (STEP, 4), (SIDE_BYTES, 4), (VERSUS, 4)]
for _group in LEVEL_GROUPS:
    SLAB_BYTES[_group] = LEVEL_SLAB + 0x100
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, LEVEL_SLAB)] + LEVEL_REGIONS, gap=0)
    READ_ONLY[_group] = []


def install_level_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_lev_installed", False):
        return
    fc._lev_installed = True
    mark_under_test()
    stop_clock(fc)
    ensure(fc, 0x004DEE50, 0, "engine_004dee50")                                        # NetGame_GameOver
    ensure(fc, 0x004AB9C0, 0, "engine_004ab9c0")                                        # Redbook_ResetVolume


GROUP_STUBS.update({group: install_level_stubs for group in LEVEL_GROUPS})


@spec(0x00466C10, "LevelEvent_InitializeAndDispatch", "lev")
def gen_level_event(env, n):
    return level_case(env, False)


@spec(0x00466C10, "LevelEvent_InitializeAndDispatch", "lew")
def gen_level_event_aimed(env, n):
    """The second fixture: the two skaters' words one apart and across the sign, the counters at their wrap edges, both initialised bytes and the sides in play."""
    return level_case(env, True)


def level_case(env, aimed):
    img, rng = Image(), env.rng
    first, second = env.slab + FIRST_AT - 0x2CDC, env.slab + SECOND_AT - 0x2CDC
    edge = lambda: rng.choice([0, 1, 2, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x1000, rng.getrandbits(32), rng.getrandbits(8), rng.randrange(-5, 5) & 0xFFFFFFFF])
    img.put(env.slab, noise(rng, LEVEL_SLAB))
    mode = rng.choice([8, 8, 8, 8, 9, 9, 9, 9, 0, 1, 7, 10, 0x108, 0x109, 0xFFFFFFFF, rng.getrandbits(32)] if not aimed else [8, 8, 8, 9, 9, 9, 7, 0x108, 0x10008, 0x10009, 0x80000008, 0x80000009])
    img.u32(GAME_MODE, mode)
    both = mode == 8 or rng.random() < 0.3
    pointers = [first, second]
    if rng.random() < 0.08:
        pointers = [first, first] if rng.random() < 0.5 else [second, second]
    img.u32(SKATERS, pointers[0] if both or rng.random() < 0.7 else rng.choice([0, rng.getrandbits(32)]))
    img.u32(SKATERS + 4, pointers[1] if both or rng.random() < 0.7 else 0)
    value_a = edge()
    img.u32(env.slab + FIRST_AT, value_a)
    around = [value_a, (value_a + 1) & 0xFFFFFFFF, (value_a - 1) & 0xFFFFFFFF, edge()]
    img.u32(env.slab + SECOND_AT, rng.choice(around + ([value_a] if aimed else [])))
    img.u32(env.slab + 0x488, edge())                                                       # (the second skater's +0x3144)
    img.u32(COUNTERS_AT + 0x00, edge())                                                     # 0x0056b798
    img.u32(COUNTERS_AT + 0x04, edge())                                                     # 0x0056b79c
    img.u32(COUNTERS_AT + 0x08, rng.choice([value_a, (value_a - 1) & 0xFFFFFFFF, (value_a + 1) & 0xFFFFFFFF, edge()]))     # 0x0056b7a0: the second's best
    img.u32(COUNTERS_AT + 0x0C, rng.choice([value_a, (value_a - 1) & 0xFFFFFFFF, (value_a + 1) & 0xFFFFFFFF, edge()]))     # 0x0056b7a4: the first's best
    for k in range(0x10, 0x4C, 4):
        img.u32(COUNTERS_AT + k, edge())
    img.u32(COUNTERS_AT + 0x18, rng.choice([0, 0, 1, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))   # 0x0056b7b0
    img.u32(COUNTERS_AT + 0x1C, rng.choice([0, 0, 1, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32)]))   # 0x0056b7b4
    flags = rng.choice([0, 0, 0, 1, 1, 0xFF, 0x80, 0x100, rng.getrandbits(8)])                                          # 0x006a3d48 (a byte; 0x100 in the upper half would be the next byte's)
    initialised = rng.choice([0, 0, 0, 0, 1, 1, 0xFF, 0x80, rng.getrandbits(8)] if not aimed else [0, 0, 0, 1, 0xFF])
    img.u32(SIDE_BYTES, (flags & 0xFF) | (initialised << 8) | (rng.getrandbits(16) << 16))
    img.u32(VERSUS, rng.choice([2, 2, 2, 2, 0, 1, 3, 0x102, 0xFFFFFFFE, rng.getrandbits(32)] if not aimed else [2, 2, 2, 2, 1, 3]))
    img.u32(LATCH, rng.choice([0, 0, 0, 1, 2, 0x100, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u32(COUNTDOWN, edge())
    img.u32(STEP, edge())
    return [], img.pokes()


# ---- 10g s21: CBaddy__Die 0x004013b0 (group wdi; the death pulse 0x004013d0 runs inside it as retail code) ------------------------------------------------------------------------------------

DIE_GROUPS = ("wdi",)
DIE_SLAB, DIE_TABLE, RECORD_TABLE = 0x400, 0x300, 0x0056E210
for _group in DIE_GROUPS:
    SLAB_BYTES[_group] = DIE_SLAB + 0x100
    EXTRA_REGIONS[_group] = lambda slab: merged([(slab, DIE_SLAB), (RECORD_TABLE, 4)], gap=0)
    READ_ONLY[_group] = []


def install_die_stubs(fc, env):
    from platform_specs import mark_under_test
    from world_specs import install_stubs as install_base_stubs

    if getattr(fc, "_wdi_installed", False):
        return
    fc._wdi_installed = True
    install_base_stubs(fc, env)
    mark_under_test()
    ensure(fc, 0x004C5B00, 1, "engine_004c5b00")                                        # TRG_ExecuteCommandList


GROUP_STUBS.update({group: install_die_stubs for group in DIE_GROUPS})


@spec(0x004013B0, "CBaddy__Die", "wdi", this=lambda env: env.slab + OBJ)
def gen_die(env, n):
    img, rng = Image(), env.rng
    o = env.slab + OBJ
    fill_object(img, env, rng, 0x230)
    img.u8(o + 0x4, rng.choice([0, 1, 0x40, 0x41, 0x80, 0xBE, 0xFF, rng.getrandbits(8)]))
    img.u8(o + 0x6A, rng.choice([0, 0, 0x40, 0x3F, 0xBF, 0xFF, 0x80, rng.getrandbits(8)]))
    img.u8(o + 0x171, rng.choice([0, 0, 0, 1, 0xFF, 0x80, rng.getrandbits(8)]))
    img.u32(o + 0xB0, rng.choice([0xFFFF, 0xFFFF, 0, 1, 3, 7, 0xFFFF0000 | rng.randrange(8), 0x10000 | rng.randrange(8), rng.randrange(8) | (rng.getrandbits(16) << 16), 0xFFFFFFFF]))
    img.u32(RECORD_TABLE, env.slab + DIE_TABLE)
    for k in range(8):
        img.u32(env.slab + DIE_TABLE + 4 * k, rng.choice([0, 0xFFFFFFFA, 0xFFFFFFFF, 0x1000, rng.getrandbits(32), rng.getrandbits(32)]))
    return [], img.pokes()
