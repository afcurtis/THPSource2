"""Phase 4.15c probe: the trigger file (group wtr): the loaded `_TRG` image and its table of records, the list of command points and its buckets, the restart nodes, the decoding of a record's
position and the links of a record (0x004c4b10..0x004c8650, the fade helpers 0x004c8f10 / 0x004c8f30).

The image is built in the slab the way the loader leaves it (a table of pointers to records of 0x60 bytes each); `TRG_LoadFile` is given the file form (offsets) by its stubbed loader. Two skater
objects (only the fields the restart code touches are captured) are chained through +0x20 as the player record's list.

Slab layout (offsets): image 0x1000 (header and table, records from 0x100, 0x60 each), command-point nodes 0x1900 (8 of 0x20), names 0x1a00, out vectors 0x1b00, arena 0x1c00 (0x600), skater 1 0x2400,
skater 2 0x5600 (0x3200 apart; the windows of each: 0..0xd0, +0x2cc4, +0x2e58, +0x30f4).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import word

GROUPS = ("wtr",)

IMG = 0x1000
RECORDS = 0x100
STRIDE = 0x60
NODES = 0x1900
NAMES = 0x1A00
OUT = 0x1B00
ARENA = 0x1C00
ARENA_SIZE = 0x600
SK = (0x2400, 0x5600)
SLAB = 0x5600 + 0x3200
SCRATCH = 0x7FF00800

MENU_TABLE = 0x0056DCB8
BUCKETS = 0x0056DD64
FILE_NAME = 0x0056DD60
FILE_SIZE = 0x0056DCB0
RESTART_NAMES = 0x0056E164
RESTART_COUNT = 0x0056E1B4
HOME_NAMES = 0x0056E1B8
HOME_COUNT = 0x0056E208
IMAGE = 0x0056E20C
RECORD_TABLE = 0x0056E210
RECORD_COUNT = 0x0056E214
MENU_FLAG = 0x0056E21C
COMMAND_POINTS = 0x0056E220
COMMAND_POINT_BYTES = 0x0056E224
DUAL_BUFFER = 0x0056E228
RESTART_INDEX = 0x005477EC
AUTOEXEC_FLAG = 0x0055FF94
DEFAULT_RESTART_FLAG = 0x005614C4
PLAYERS = 0x00533F3C
GAME_MODE = 0x00533F38
OWNER = 0x0056A954
PLAYER_RECORD = 0x0056A960
FADE = 0x0056E310
STATE = {"image": b""}
IMAGE_GLOBALS = (0x0056DCB0, 0x57C)         # 0x0056dcb0..0x0056e22c: every global of the trigger file

GLOBALS = [IMAGE_GLOBALS, (RESTART_INDEX, 4), (GAME_MODE, 8), (AUTOEXEC_FLAG, 4), (DEFAULT_RESTART_FLAG, 4), (OWNER, 0x10), (FADE, 0xC)]
READ = [(0x00547834, 8), (0x00547958, 8)]


def wtr_regions(slab):
    windows = [(slab + IMG, 0x900), (slab + NODES, 0x100), (slab + NAMES, 0x100), (slab + OUT, 0x40), (slab + ARENA, ARENA_SIZE)]
    for sk in SK:
        windows += [(slab + sk, 0xD0), (slab + sk + 0x2CC4, 4), (slab + sk + 0x2E58, 0x14), (slab + sk + 0x30F4, 0x28)]
    return merged(windows + GLOBALS, gap=0)


def wtr_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {BUCKETS: bytes(0x400)}         # (the base starts with empty buckets: a case only pokes the buckets that hold a command point)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = wtr_regions
    READ_ONLY[_group] = READ
    SEEDS[_group] = wtr_seed


def wtr(address, name):
    return spec(address, name, "wtr")


def install_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_wtr_installed", False):
        return
    fc._wtr_installed = True
    mark_under_test()
    stop_clock(fc)
    ensure(fc, 0x004C5DC0, 3, "engine_004c5dc0")                                          # TRG_InterpretCommandStream
    ensure(fc, 0x004DF250, 1, "engine_004df250")                                          # NetRing_Push
    ensure(fc, 0x0046F4E0, 2, "engine_0046f4e0")                                          # Mem_Shrink
    ensure(fc, 0x00449030, 2, "engine_00449030", result=lambda a: len(STATE["image"]), text=True, stack_to={0: SCRATCH + 0x400}, keep=[0])   # FileIO_OpenResource
    ensure(fc, 0x00449230, 1, "engine_00449230", fill=(0, lambda a: STATE["image"]))      # FileIO_LoadResource: the file's bytes
    ensure(fc, 0x00449660, 0, "engine_00449660")                                          # FileIO_SyncResource
    ensure(fc, 0x0044BDE0, 0, "engine_0044bde0")                                          # the display's fade stop


GROUP_STUBS = {group: install_stubs for group in GROUPS}


# ---- the image ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

NAME_POOL = ["Ack", "RE_2P", "re_2p", "Ho_a", "HO_b", "Hox", "Start", "Ho_", "Ho_Mid", "Restart1", "RE_2P1"]


def short_positions(rng):
    return [rng.randrange(-300, 300) & 0xFFFFFFFF for _ in range(3)]


def make_record(rng, base, kind, name=None):
    """A record of STRIDE bytes at the (absolute) address `base`: its type word and enough of the structure for TRG_DecodeNodePosition to find the position and for a node its name."""
    body = bytearray(rng.getrandbits(8) & 0xFE | 0x00 for _ in range(STRIDE))        # (no 0xff bytes: the scans stop at the ones placed)
    struct.pack_into("<H", body, 0, kind)
    if kind == 8:
        w2 = rng.choice([0, 1, 2, 3])
        struct.pack_into("<H", body, 2, w2)
        at = (base + 2 * w2 + 7) & ~3
        place(body, base, at, rng, name or rng.choice(NAME_POOL))
    elif kind in (3, 10, 11, 13, 0x3E8, 0x3E9):
        w2 = rng.choice([0, 1, 2, 3])
        struct.pack_into("<H", body, 2, w2)
        at = (base + 2 * w2 + 7) & ~3
        place(body, base, at, rng, name or rng.choice(NAME_POOL))
    elif kind in (1, 7):
        w6 = rng.choice([0, 1, 2, 3, 4])
        struct.pack_into("<H", body, 6, w6)
        stop = base + 2 * w6 + 8 + rng.choice([0, 0, 1, 2, 3])
        body[stop - base] = 0xFF
        at = (stop + 4) & ~3
        place(body, base, at, rng, name or rng.choice(NAME_POOL))
    elif kind == 5:
        w4 = rng.choice([0, 1, 2, 3])
        struct.pack_into("<H", body, 4, w4)
        at = (base + 2 * w4 + 9) & ~3
        place(body, base, at, rng, name or rng.choice(NAME_POOL))
    elif kind in (0x1F4, 0x1F5):
        at = (base + 5) & ~3
        place(body, base, at, rng, name or rng.choice(NAME_POOL))
    return bytes(body)


def place(body, base, at, rng, name):
    """The position (three dwords), the angles (three words) and the name at `at`."""
    offset = at - base
    for k, value in enumerate(short_positions(rng)):
        struct.pack_into("<I", body, offset + 4 * k, value)
    for k in range(3):
        struct.pack_into("<H", body, offset + 12 + 2 * k, rng.getrandbits(16) & 0xFEFE)
    text = name.encode() + b"\0"
    body[offset + 18:offset + 18 + len(text)] = text


KINDS = [8, 8, 8, 1, 7, 5, 3, 10, 11, 13, 0x1F4, 0x1F5, 0x3E8, 0x3E9, 2, 4, 6, 0xF, 12, 0]


def build_image(env, rng, count=None, kinds=None, absolute=True, terminator=True):
    """The image the loader leaves: header, table of pointers, records. Returns (bytes of the image, record addresses)."""
    count = count if count is not None else rng.choice([1, 2, 3, 5, 8, 12])
    base = env.slab + IMG
    kinds = kinds or [rng.choice(KINDS) for _ in range(count)]
    data = bytearray(RECORDS + STRIDE * (count + 1))
    struct.pack_into("<IIH", data, 0, 0x4752545F, 2, count)
    addresses = []
    for k in range(count):
        address = base + RECORDS + STRIDE * k
        addresses.append(address)
        data[RECORDS + STRIDE * k:RECORDS + STRIDE * (k + 1)] = make_record(rng, address, kinds[k])
        struct.pack_into("<I", data, 0xC + 4 * k, address if absolute else address - base)
    last = RECORDS + STRIDE * count
    if terminator:
        struct.pack_into("<H", data, last, 0xFF)
    return bytes(data), addresses


def put_image(img, env, rng, **kw):
    data, addresses = build_image(env, rng, **kw)
    img.put(env.slab + IMG, data)
    img.u32(IMAGE, env.slab + IMG)
    img.u32(RECORD_TABLE, env.slab + IMG + 0xC)
    img.u32(RECORD_COUNT, len(addresses))
    return addresses


def put_name(img, env, text, slot=0):
    img.put(env.slab + NAMES + 0x20 * slot, text.encode() + b"\0")
    return env.slab + NAMES + 0x20 * slot


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def globals_noise(img, rng):
    img.put(IMAGE_GLOBALS[0], noise(rng, 0x20))        # (the scalar words ahead of the tables)
    img.u32(RESTART_INDEX, word(rng))


# ---- the command points ------------------------------------------------------------------------------------------------------------------------------------------------------------

def put_command_points(img, env, rng, count=None, ids=None):
    """A chain of command points (+0x14), each in the bucket of its id (+0x10); returns the nodes."""
    count = count if count is not None else rng.choice([0, 1, 2, 4, 6, 8])
    nodes = [env.slab + NODES + 0x20 * k for k in range(count)]
    for k, node in enumerate(nodes):
        img.put(node, noise(rng, 0x20))
        img.u32(node + 0x14, nodes[k + 1] if k + 1 < count else 0)
        img.u32(node + 0x10, 0)
    pool = ids or [rng.choice([0, 1, 2, 0x101, 0x201, 5, 0xFF, 0x1FF, rng.getrandbits(32)]) for _ in range(3)]
    last = {}
    for node in nodes:
        key = rng.choice(pool)
        img.u32(node + 0xC, key)
        bucket = key & 0xFF
        img.u32(node + 0x10, last.get(bucket, 0))
        last[bucket] = node
        img.u16(node + 0xA, rng.choice([0, 1, 2, 3, 4, rng.getrandbits(16) & 0xF]))
    for bucket, node in last.items():
        img.u32(BUCKETS + 4 * bucket, node)
    img.u32(COMMAND_POINTS, nodes[0] if nodes else 0)
    img.u32(COMMAND_POINT_BYTES, word(rng))
    return nodes, pool


@wtr(0x004C4B10, "Trig_ClearTrigMenu")
def gen_clear_menu(env, n):
    img, rng = Image(), env.rng
    img.put(MENU_TABLE, noise(rng, 0xA0))
    img.u32(MENU_FLAG, word(rng))
    return [], img.pokes()


@wtr(0x004C4B30, "Trig_DeleteCommandPoints")
def gen_delete_command_points(env, n):
    img, rng = Image(), env.rng
    put_command_points(img, env, rng)
    return [], img.pokes()


@wtr(0x004C4B80, "Trig_ResetCPCollisionFlags")
def gen_reset_collision(env, n):
    img, rng = Image(), env.rng
    put_command_points(img, env, rng)
    return [rng.choice([0, 1, 2, 0xFF, 0xF0, rng.getrandbits(8), rng.getrandbits(32)])], img.pokes()


@wtr(0x004C4BA0, "Trig_ResetCPExecutedFlags")
def gen_reset_executed(env, n):
    img, rng = Image(), env.rng
    put_command_points(img, env, rng)
    return [rng.choice([0, 1, 2, 0xFF, 0xF0, rng.getrandbits(8), rng.getrandbits(32)])], img.pokes()


@wtr(0x004C4BD0, "Trig_TriggerCommandPoint")
def gen_trigger(env, n):
    img, rng = Image(), env.rng
    nodes, pool = put_command_points(img, env, rng)
    owner = env.slab + rng.choice(SK)
    img.u32(OWNER, owner)
    img.u32(owner + 0x2CC4, rng.choice([0, 1, 2, 3, 0xFE, 0xFF, 0x100, rng.getrandbits(32)]))
    img.put(env.slab + IMG, noise(rng, 0x40))
    wanted = rng.choice(pool + [rng.getrandbits(32)])
    return [wanted], img.pokes()


@wtr(0x004C5AC0, "TRG_FindCommandPointLink")
def gen_find_link(env, n):
    img, rng = Image(), env.rng
    addresses = put_image(img, env, rng)
    put_command_points(img, env, rng)
    index = rng.choice([0xFFFF, 0xFFFF, *range(len(addresses)), *range(len(addresses))])
    return [index], img.pokes()


# ---- the restart nodes --------------------------------------------------------------------------------------------------------------------------------------------------------------------

def image_with_nodes(img, env, rng):
    count = rng.choice([1, 2, 3, 5, 8, 12])
    kinds = [rng.choice([8, 8, 8, 8, 3, 1, 7, 5, 0x1F4, 2]) for _ in range(count)]
    return put_image(img, env, rng, count=count, kinds=kinds), kinds


@wtr(0x004C4C50, "Trig_FindRestartByName")
def gen_find_restart(env, n):
    img, rng = Image(), env.rng
    addresses, kinds = image_with_nodes(img, env, rng)
    globals_noise(img, rng)
    name = rng.choice(NAME_POOL + ["ACK", "ack", "zzz"])
    return [put_name(img, env, name)], img.pokes()


def skaters(img, env, rng, deep=False):
    for k, sk in enumerate(SK):
        base = env.slab + sk
        img.put(base, noise(rng, 0xD0))
        img.u32(base + 0x20, env.slab + SK[1] if k == 0 else 0)
        img.put(base + 0x2E58, noise(rng, 0x14))
        img.put(base + 0x30F4, noise(rng, 0x28))
        img.u32(base + 0x2CC4, word(rng))
    img.u32(PLAYER_RECORD, env.slab + SK[0])
    img.u32(OWNER, env.slab + SK[0])


@wtr(0x004C4D10, "FudgeAngles")
def gen_fudge(env, n):
    img, rng = Image(), env.rng
    skaters(img, env, rng)
    img.u16(env.slab + SK[0] + 0x16, rng.choice([0, 1, 0x800, 0xFFF, 0x7FF, 0x1000, 0xFFFF, rng.getrandbits(16), rng.randrange(0, 0x1000)]))
    return [env.slab + SK[0]], img.pokes()


@wtr(0x004C4E30, "Trig_ExecuteRestart")
def gen_execute_restart(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([1, 2, 3, 5, 8, 12])
    kinds = [rng.choice([8, 8, 8, 8, 3, 1, 7, 5, 0x1F4, 2]) for _ in range(count)]
    kinds[rng.randrange(count)] = 8
    put_image(img, env, rng, count=count, kinds=kinds)
    skaters(img, env, rng)
    index = rng.choice([k for k, kind in enumerate(kinds) if kind == 8])
    img.u32(RESTART_INDEX, index)
    img.u32(PLAYERS, rng.choice([1, 2, 2, 3]))
    img.u32(GAME_MODE, rng.choice([0, 1, 7, 7, 2, rng.getrandbits(32)]))
    if rng.random() < 0.1:
        img.u32(PLAYER_RECORD, 0)
        img.u32(PLAYERS, 1)           # (a second skater written to address 0 would fault in retail)
    return [], img.pokes()


@wtr(0x004C5100, "Trig_DeleteTrigFile")
def gen_delete_file(env, n):
    img, rng = Image(), env.rng
    img.u32(IMAGE, rng.choice([0, env.slab + IMG]))
    img.u32(RESTART_COUNT, word(rng))
    img.u32(HOME_COUNT, word(rng))
    return [], img.pokes()


def file_image(env, rng, good=True):
    count = rng.choice([1, 2, 3, 5])
    total = count + 1                                  # (the table lists the terminating record too: a file of 3 nodes has 4 entries)
    kinds = [rng.choice([8, 8, 3, 1, 5, 2, 4]) for _ in range(count)]
    # the image sits at the arena's first block: its records are placed for that address
    data = bytearray(RECORDS + STRIDE * (total + 1))
    base = env.slab + ARENA
    struct.pack_into("<IIH", data, 0, 0x4752545F if good else rng.getrandbits(32), 2 if good else rng.choice([1, 3, 0x10002]), total)
    for k in range(count):
        address = base + RECORDS + STRIDE * k
        data[RECORDS + STRIDE * k:RECORDS + STRIDE * (k + 1)] = make_record(rng, address, kinds[k])
        struct.pack_into("<I", data, 0xC + 4 * k, RECORDS + STRIDE * k)
    last = RECORDS + STRIDE * count
    struct.pack_into("<I", data, 0xC + 4 * count, last)
    struct.pack_into("<H", data, last, 0xFF if rng.random() < 0.85 else rng.choice([0, 8]))
    if not good and rng.random() < 0.5:
        struct.pack_into("<I", data, 4, 2 | (rng.getrandbits(16) << 16))
    return bytes(data[:last + 8])


@wtr(0x004C5130, "TRG_LoadFile")
def gen_load_file(env, n):
    img, rng = Image(), env.rng
    STATE["image"] = file_image(env, rng, good=rng.random() < 0.8)
    img.u32(IMAGE, 0 if rng.random() < 0.9 else env.slab + IMG)
    name = put_name(img, env, rng.choice(["level", "a", "park_1", "LONGERNAME", ""]))
    img.u32(FILE_SIZE, word(rng))
    return [name], img.pokes()


@wtr(0x004C5380, "Trig_InitDefaultFile")
def gen_init_default(env, n):
    img, rng = Image(), env.rng
    globals_noise(img, rng)
    img.u32(RESTART_COUNT, word(rng))
    img.u32(HOME_COUNT, word(rng))
    img.u32(IMAGE, word(rng))
    return [], img.pokes()


@wtr(0x004C5420, "TRG_FindObjectPreambleFlag")
def gen_preamble_flag(env, n):
    img, rng = Image(), env.rng
    length = rng.choice([0, 1, 2, 5, 9])
    stream = bytes(rng.choice([1, 2, 3, 4, 0x10, 0x20]) for _ in range(length)) + b"\xff" + noise(rng, 8)
    img.put(env.slab + NAMES, stream)
    flag = rng.choice([1, 2, 3, 4, 0x10, 0x20, 0xFF, rng.getrandbits(8), rng.getrandbits(32)])
    return [flag, env.slab + NAMES], img.pokes()


@wtr(0x004C5440, "TRG_FindObjectPreambleEnd")
def gen_preamble_end(env, n):
    img, rng = Image(), env.rng
    length = rng.choice([0, 1, 2, 5, 9, 20])
    img.put(env.slab + NAMES, bytes(rng.getrandbits(8) & 0x7F for _ in range(length)) + b"\xff" + noise(rng, 8))
    return [env.slab + NAMES], img.pokes()


@wtr(0x004C5D70, "TRG_SkipAlignedCString")
def gen_skip_string(env, n):
    img, rng = Image(), env.rng
    text = bytes(rng.randrange(1, 0x7F) for _ in range(rng.choice([0, 1, 2, 3, 5, 8])))
    img.put(env.slab + NAMES, text + b"\0" + noise(rng, 4))
    return [env.slab + NAMES + rng.choice([0, 0, 1])], img.pokes()


@wtr(0x004C5D90, "Trig_AllocateDualBuffer")
def gen_allocate_dual(env, n):
    return [], Image().pokes()


@wtr(0x004C5DA0, "Trig_RemoveDualBuffer")
def gen_remove_dual(env, n):
    img, rng = Image(), env.rng
    img.u32(DUAL_BUFFER, rng.choice([0, env.slab + IMG, rng.getrandbits(32)]))
    return [], img.pokes()


@wtr(0x004C7EA0, "Trig_ScanRestart")
def gen_scan_restart(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 3, 8, 12, 22, 22])
    kinds = [rng.choice([8, 8, 8, 3, 1, 2] if count < 22 else [8, 8, 8, 8, 8, 8, 8, 3]) for _ in range(count)]
    names = [rng.choice(["Ho_a", "HO_b", "Hox", "Ack", "Ho_", "H", "HO"]) for _ in range(count)]
    base = env.slab + IMG
    data = bytearray(RECORDS + STRIDE * (count + 1))
    struct.pack_into("<IIH", data, 0, 0x4752545F, 2, count)
    for k in range(count):
        address = base + RECORDS + STRIDE * k
        data[RECORDS + STRIDE * k:RECORDS + STRIDE * (k + 1)] = make_record(rng, address, kinds[k], names[k])
        struct.pack_into("<I", data, 0xC + 4 * k, address)
    img.put(base, bytes(data))
    img.u32(RECORD_TABLE, base + 0xC)
    img.u32(RECORD_COUNT, count)
    img.put(RESTART_NAMES, noise(rng, 0x4C))
    img.put(HOME_NAMES, noise(rng, 0x50))
    img.u32(RESTART_COUNT, word(rng))
    img.u32(HOME_COUNT, word(rng))
    img.u32(DEFAULT_RESTART_FLAG, rng.choice([0, 1]))
    return [], img.pokes()


@wtr(0x004C8050, "TRG_ExecuteAutoexec")
def gen_autoexec(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 3, 6, 10])
    kinds = [rng.choice([8, 0xF, 4, 4, 0xF, 3, 1]) for _ in range(count)]
    put_image(img, env, rng, count=count, kinds=kinds) if count else put_image(img, env, rng, count=1, kinds=[2])
    if not count:
        img.u32(RECORD_COUNT, 0)
    img.u32(PLAYERS, rng.choice([1, 2, 2]))
    img.u32(DEFAULT_RESTART_FLAG, rng.choice([0, 1]))
    img.u32(AUTOEXEC_FLAG, word(rng))
    img.u32(RESTART_INDEX, word(rng))
    return [], img.pokes()


@wtr(0x004C84D0, "TRG_CreateLinkRecord")
def gen_create_link(env, n):
    img, rng = Image(), env.rng
    for b in rng.sample(range(0x100), 6):
        img.u32(BUCKETS + 4 * b, env.slab + NODES)
    img.u32(COMMAND_POINTS, rng.choice([0, env.slab + NODES]))
    img.u32(COMMAND_POINT_BYTES, word(rng))
    return [rng.choice([0, 1, 0x1FF, 0xFF, rng.getrandbits(32)]), rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@wtr(0x004C8550, "TRG_GetNodeLinks")
def gen_get_links(env, n):
    img, rng = Image(), env.rng
    addresses = put_image(img, env, rng, count=rng.choice([3, 8, 14]), kinds=None)
    return [rng.choice([*range(len(addresses)), *range(len(addresses)), 0xFFFFFFFF, len(addresses), len(addresses) + 5, 0x80000000])], img.pokes()


@wtr(0x004C8650, "TRG_DecodeNodePosition")
def gen_decode(env, n):
    img, rng = Image(), env.rng
    addresses = put_image(img, env, rng, count=rng.choice([3, 8, 14]), kinds=None)
    img.put(env.slab + OUT, noise(rng, 0x20))
    return [env.slab + OUT, rng.choice([*range(len(addresses)), *range(len(addresses)), 0xFFFFFFFF, len(addresses)])], img.pokes()


@wtr(0x004C8F10, "Utils_StartFade")
def gen_start_fade(env, n):
    img, rng = Image(), env.rng
    img.put(FADE, noise(rng, 0xC))
    return [word(rng), word(rng)], img.pokes()


@wtr(0x004C8F30, "Utils_StopFade")
def gen_stop_fade(env, n):
    img, rng = Image(), env.rng
    img.put(FADE, noise(rng, 0xC))
    return [], img.pokes()
