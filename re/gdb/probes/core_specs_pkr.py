"""Phase 4.14c probe: the relocatable-module loader, the PKR2 package wrappers, the stream object and the explosion object (group pkr).

0x004ac020..0x004ac330 (the module loader), 0x004e9150..0x004e9b80 (the run-length and zlib wrappers, the compression dispatch, the package header load and unload), 0x004ebf10..0x004ebf80 (a small
stream object), 0x00448860..0x00448ac0 (`exp.cpp`) and the file head (0x00448c60, 0x00448c90). The data lives in a scratch slab; the services outside the group are stubbed: the C runtime's
malloc / free / sprintf, zlib's compress and uncompress (the stub writes a scripted length and bytes), the package stream calls, the loader's `FileIO_OpenLoad`, the base-class calls of the explosion
object and the shatter routines. The stubs on the call sites of the error print `0x00502688` (it takes one or two arguments by site).

Slab layout (offsets): sources 0x000 (0x400), texts 0x400.. (0x40 each), the length word 0x4c0, a package entry 0x500, the explosion object 0x600 and its entries 0x700, the environment objects 0x800,
the region table 0x900, the model descriptions 0xa00, the module nodes 0xb00, the module code 0xc00 and its relocation table 0xd00, the allocation arena 0x1000..0x3000, the stream object 0x3000.
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import check_disjoint, merged
from frontend_specs_menu import Image

GROUPS = ("pkr",)

SLAB = 0x5500
SRC = 0x000
TEXT = 0x400
OUTLEN = 0x4C0
ENTRY = 0x500
EXPOBJ = 0x600
EXPENT = 0x700
ENVOBJ = 0x800
REGTAB = 0x900
DESCS = 0xA00
NODES = 0xB00
BIN = 0xC00
REL = 0xD00
ARENA = 0x1000
ARENA_SIZE = 0x1000                  # the loader's allocator (env.alloc) takes ARENA..ARENA+0x1000, the C runtime stand-ins the 0x1000 bytes after it
STREAM = 0x3000

SCRATCH = 0x7FF00800                  # pc_platform.hpp kScratchBase
L_DESTLEN = SCRATCH + 0x60           # stack locals the port keeps in the scratch
L_HEADER = SCRATCH + 0x90
L_TEXT = SCRATCH + 0xC0

WINDOWS = [(0x0056DB28, 4), (0x0056D432, 0xD8), (0x0056B89C, 4), (0x006A7578, 0x108), (0x005615D8, 4), (0x0056152C, 4)]
TABLES = [(0x0054B408, 0x18), (0x0055FE10, 4)]

STATE = {"fail": set(), "calls": 0, "arena": 0, "slab": 0, "rng": None,
         "zdest": 0, "zbytes": b"", "zrc": 0, "stream": 0, "hdr": bytes(16), "opens": [], "mallocs": 0}


def pkr_regions(slab):
    windows = [(slab, STREAM + 0x10), (slab + STREAM + 0x2458, 8)] + WINDOWS
    result = merged(windows, gap=0)
    check_disjoint(result, TABLES)
    return result


def pkr_seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    return {}


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = pkr_regions
    READ_ONLY[_group] = TABLES
    SEEDS[_group] = pkr_seed


def u(value):
    return value & 0xFFFFFFFF


def pkr(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "pkr", this=resolve)


def hash_name(text: bytes) -> int:
    h = 0xFFFFFFFF
    for c in text:
        cl = c ^ (h & 0xFF)
        for _ in range(8):
            h = ((h << 1) | (h >> 31)) & 0xFFFFFFFF
            if cl & 1:
                h ^= 0xEDB88320
            cl = ((cl if cl < 0x80 else cl - 0x100) >> 1) & 0xFF
    return h


# ---- the stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------------

def arena_malloc(args):
    """CRT_malloc / operator new: slab addresses from a bump pointer; a call number in STATE["fail"] answers 0 (malloc only)."""
    index = STATE["mallocs"]
    STATE["mallocs"] += 1
    if index in STATE["fail"]:
        return 0
    size = max(args[0], 8)
    at = STATE["slab"] + ARENA + ARENA_SIZE + STATE["arena"]
    STATE["arena"] += (size + 15) & ~15
    if STATE["arena"] > 0x1000:
        raise RuntimeError("the C runtime arena is exhausted")
    return at


def zlib_destlen(args):
    return STATE["zdest"]


def zlib_fill(args):
    return STATE["zbytes"]


def read_fill(args):
    size, count = args[1], args[2]
    if size == 0x10 and count == 1:
        return STATE["hdr"]
    rng = STATE["rng"]
    return bytes(rng.getrandbits(8) for _ in range(size * count))


def next_open(args):
    return STATE["opens"].pop(0) if STATE["opens"] else 0


# the call instructions of the error print (0x00502688) inside the functions under test: (address, arguments)
LOG_SITES = [(0x004E9305, 2), (0x004E9370, 1), (0x004E95DE, 2), (0x004E95F3, 1), (0x004E9608, 1), (0x004E961D, 1), (0x004E96C7, 1)]


def install_pkr_stubs(fc, env):
    from frontend_specs_front import ensure
    from platform_specs import mark_under_test

    if getattr(fc, "_pkr_installed", False):
        return
    fc._pkr_installed = True
    mark_under_test()
    STATE["slab"] = env.slab
    ensure(fc, 0x00501BB4, 1, "engine_00501bb4", result=arena_malloc)
    ensure(fc, 0x00501B6C, 1, "engine_00501b6c")
    ensure(fc, 0x005002A1, 3, "engine_005002a1", stack_to={0: L_TEXT})
    ensure(fc, 0x004F9D30, 4, "engine_004f9d30", stack_to={1: L_DESTLEN}, pokes=[(1, 4, zlib_destlen)], fill=(0, zlib_fill), result=lambda a: STATE["zrc"])
    ensure(fc, 0x004F9D50, 4, "engine_004f9d50", stack_to={1: L_DESTLEN}, pokes=[(1, 4, zlib_destlen)], fill=(0, zlib_fill), result=lambda a: STATE["zrc"])
    for address, count in LOG_SITES:
        fc.stub_site(address, 5, count, "engine_00502688")
    ensure(fc, 0x005002FE, 1, "engine_005002fe", result=arena_malloc)
    ensure(fc, 0x005002F3, 1, "engine_005002f3")
    ensure(fc, 0x00500E7B, 1, "engine_00500e7b")
    ensure(fc, 0x005013F9, 2, "engine_005013f9", result=lambda a: STATE["stream"])
    ensure(fc, 0x00502358, 4, "engine_00502358", stack_to={0: L_HEADER}, fill=(0, read_fill))
    ensure(fc, 0x00448DD0, 2, "engine_00448dd0", stack_to={0: L_TEXT}, text=True, result=next_open)
    fc.stub_site(0x004AC198, 3, 1, "engine_%08x" % (env.slab + BIN))          # the call of a loaded module's entry point (`call [edi+4]`), reported as a service
    ensure(fc, 0x004AF0B0, 3, "engine_004af0b0")
    ensure(fc, 0x004AE090, 7, "engine_004ae090")
    ensure(fc, 0x0040A740, 1, "engine_0040a740", convention="thiscall", this=True)
    ensure(fc, 0x0040A900, 0, "engine_0040a900", convention="thiscall", this=True)
    ensure(fc, 0x0040A880, 0, "engine_0040a880", convention="thiscall", this=True)
    ensure(fc, 0x00403DC0, 1, "engine_00403dc0")
    ensure(fc, 0x00403E40, 0, "engine_00403e40", convention="thiscall", this=True)   # CBit_Die (Exp_Move)
    ensure(fc, 0x004EBFE0, 0, "engine_004ebfe0", convention="thiscall", this=True)


GROUP_STUBS = {group: install_pkr_stubs for group in GROUPS}


def begin(env):
    STATE["fail"] = set()
    STATE["mallocs"] = 0
    STATE["arena"] = 0
    STATE["rng"] = env.rng
    STATE["opens"] = []
    STATE["zrc"] = 0
    STATE["zdest"] = 0
    STATE["zbytes"] = b""
    STATE["stream"] = 0
    STATE["hdr"] = bytes(16)


# ---- random data ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

def runs(rng, limit, long_run=False):
    """Bytes made of runs of equal bytes (so the run-length coder has runs to code): mostly short, now and then 255 / 256 / 300 long."""
    out = bytearray()
    while len(out) < limit:
        roll = rng.random()
        if roll < 0.55:
            n = rng.randrange(1, 6)
        elif roll < 0.85:
            n = rng.randrange(1, 40)
        elif long_run:
            n = rng.choice([254, 255, 256, 257, 300, 511, 512])
        else:
            n = rng.randrange(1, 100)
        out += bytes([rng.choice([0, 1, 0x7F, 0x80, 0xFF, rng.getrandbits(8)])]) * min(n, limit - len(out))
        if rng.random() < 0.3:
            out += bytes(rng.getrandbits(8) for _ in range(min(rng.randrange(1, 8), max(0, limit - len(out)))))
    return bytes(out[:limit])


def text_for(rng, minimum=1, maximum=20, ascii_only=False):
    # (a name that is recorded as the text of a service call must be ASCII: the probe decodes it)
    alphabet = b"abcdefXYZ_09./\\-" if ascii_only else b"abcdefXYZ_09./\\-\x80\xff"
    return bytes(rng.choice(alphabet) for _ in range(rng.randrange(minimum, maximum + 1)))


def put_text(img, env, slot, data):
    address = env.slab + TEXT + 0x40 * slot
    img.put(address, data + b"\0")
    return address


# ---- the relocatable-module loader --------------------------------------------------------------------------------------------------------------------------------------------

@pkr(0x004AC020, "RelocatableModule_HashName")
def gen_hash(env, n):
    img, rng = Image(), env.rng
    begin(env)
    return [put_text(img, env, 0, text_for(rng, 0, 30))], img.pokes()


@pkr(0x004AC0B0, "RelocatableModule_Nothing")
def gen_reloc_nothing(env, n):
    img, rng = Image(), env.rng
    begin(env)
    return [], img.pokes()


def put_relocations(img, env, rng, count):
    base = env.slab + BIN
    words = []
    for _ in range(count):
        offset = rng.randrange(0, 0x3C) * 4
        kind = rng.randrange(4)
        words.append(offset | kind)
        if kind == 1:
            words.append(rng.choice([0, 0x8000, 0xFFFF, 0x12345678, rng.getrandbits(32)]))
    words.append(0xFFFFFFFF)
    img.put(env.slab + REL, b"".join(struct.pack("<I", w) for w in words))
    img.put(env.slab + BIN, bytes(rng.getrandbits(8) for _ in range(0x100)))
    return base


@pkr(0x004AC2E0, "RelocatableModule_ApplyRelocations")
def gen_apply_relocations(env, n):
    img, rng = Image(), env.rng
    begin(env)
    base = put_relocations(img, env, rng, rng.choice([0, 1, 2, 5, 12]))
    return [env.slab + REL, base], img.pokes()


@pkr(0x004AC0C0, "RelocatableModule_LoadPair")
def gen_load_pair(env, n):
    img, rng = Image(), env.rng
    begin(env)
    name = text_for(rng, 1, 18, ascii_only=True)
    address = put_text(img, env, 0, name)
    count = rng.choice([0, 0, 1, 2, 3])
    found = rng.random() < 0.25
    nodes = []
    for k in range(count):
        node = env.slab + NODES + 0x34 * k
        digest = hash_name(name) if (found and k == rng.randrange(count)) else rng.getrandbits(32)
        img.put(node, bytes(rng.getrandbits(8) for _ in range(0x34)))
        img.u32(node + 8, digest)
        nodes.append(node)
    for a, b in zip(nodes, nodes[1:] + [0]):
        img.u32(a + 0x2C, b)
    img.u32(0x0056B89C, nodes[0] if nodes else 0)
    put_relocations(img, env, rng, rng.choice([0, 1, 3, 8]))
    STATE["opens"] = [env.slab + BIN, env.slab + REL]
    return [address, rng.getrandbits(32)], img.pokes()


# ---- the run-length wrappers -------------------------------------------------------------------------------------------------------------------------------------------------

def compress_world(env, rng, wide):
    img = Image()
    begin(env)
    limit = 0x300 if not wide else 0x200
    length = rng.choice([1, 2, 3, 7, 40, rng.randrange(1, limit), rng.randrange(1, limit), limit]) if rng.random() < 0.6 else rng.randrange(1, 60)
    data = runs(rng, length, long_run=(not wide) and length > 300)
    img.put(env.slab + SRC, data)
    img.u32(env.slab + OUTLEN, rng.getrandbits(32))
    if rng.random() < 0.12:
        STATE["fail"] = {rng.choice([0, 1])}
    return img, data, env.slab


@pkr(0x004E9150, "PKR2_CompressBIBD")
def gen_compress_bibd(env, n):
    rng = env.rng
    img, data, slab = compress_world(env, rng, False)
    return [slab + SRC, len(data), rng.choice([slab + OUTLEN, slab + OUTLEN, 0])], img.pokes()


@pkr(0x004E9200, "PKR2_CompressWIBD")
def gen_compress_wibd(env, n):
    rng = env.rng
    img, data, slab = compress_world(env, rng, True)
    return [slab + SRC, len(data), rng.choice([slab + OUTLEN, slab + OUTLEN, 0])], img.pokes()


def script_zlib(rng, length, ok=None):
    bound = (length * 3) // 2
    STATE["zdest"] = rng.randrange(0, max(1, bound) + 1) if rng.random() < 0.8 else rng.randrange(0, 4)
    STATE["zbytes"] = bytes(rng.getrandbits(8) for _ in range(STATE["zdest"]))
    STATE["zrc"] = u(ok if ok is not None else rng.choice([0, 0, 0, 0, -5, -4, -3, -2, -1, 1, 2]))


@pkr(0x004E92C0, "PKR2_CompressZlib")
def gen_compress_zlib(env, n):
    img, rng = Image(), env.rng
    begin(env)
    length = rng.choice([1, 2, 3, 40, rng.randrange(1, 0x300)])
    img.put(env.slab + SRC, bytes(rng.getrandbits(8) for _ in range(length)))
    img.u32(env.slab + OUTLEN, rng.getrandbits(32))
    script_zlib(rng, length)
    if rng.random() < 0.12:
        STATE["fail"] = {1}          # (only the second allocation: with no buffer at all the stubbed compressor would write through a null pointer)
    return [env.slab + SRC, length, rng.choice([env.slab + OUTLEN, env.slab + OUTLEN, 0])], img.pokes()


def make_rle_stream(rng, width):
    pairs = []
    for _ in range(rng.randrange(0, 8)):
        count = rng.choice([0, 1, 2, 3, 4, 5, 7, 17, 100, 255]) if width == 1 else rng.choice([0, 1, 2, 3, 4, 5, 7, 17, 300, 1000])
        pairs.append((count, rng.getrandbits(8)))
    stream = bytearray()
    for count, value in pairs:
        stream += (bytes([count]) if width == 1 else struct.pack("<H", count)) + bytes([value])
    total = sum(c for c, _ in pairs)
    return bytes(stream), total


def decompress_spec(address, name, width):
    @pkr(address, name)
    def generate(env, n):
        img, rng = Image(), env.rng
        begin(env)
        stream, total = make_rle_stream(rng, width)
        img.put(env.slab + SRC, stream)
        size = rng.choice([total, total, total + rng.randrange(1, 40), max(0, total - rng.randrange(1, 40)), rng.randrange(0, 600)])
        size = min(size, 0x600)
        length = len(stream)
        if rng.random() < 0.1:
            length = rng.choice([0, -1, length + 1])
        if rng.random() < 0.08:
            STATE["fail"] = {0}
        return [env.slab + SRC, u(length), size, env.slab + TEXT + 0x80], img.pokes()

    return generate


decompress_spec(0x004E9390, "PKR2_DecompressBIBD", 1)
decompress_spec(0x004E9480, "PKR2_DecompressWIBD", 2)


@pkr(0x004E9570, "PKR2_DecompressZlib")
def gen_decompress_zlib(env, n):
    img, rng = Image(), env.rng
    begin(env)
    size = rng.choice([1, 4, 40, rng.randrange(1, 0x500)])
    img.put(env.slab + SRC, bytes(rng.getrandbits(8) for _ in range(0x40)))
    script_zlib(rng, size)
    STATE["zdest"] = min(STATE["zdest"], size)
    STATE["zbytes"] = STATE["zbytes"][:STATE["zdest"]]
    if rng.random() < 0.08:
        STATE["fail"] = {0}
    return [env.slab + SRC, rng.randrange(1, 0x40), size, env.slab + TEXT + 0x80], img.pokes()


@pkr(0x004E96B0, "PKR2_CompressResource")
def gen_compress_resource(env, n):
    img, rng = Image(), env.rng
    begin(env)
    slab = env.slab
    length = rng.choice([1, 5, 40, rng.randrange(1, 0x200)])
    data = runs(rng, length)
    img.put(slab + SRC, data)
    method = rng.choice([-2, -2, -3, -7, 0, 0, 1, 1, 2, 2])
    img.put(slab + ENTRY, bytes(rng.getrandbits(8) for _ in range(0x40)))
    img.u32(slab + ENTRY + 0x20, u(method))
    img.u32(slab + ENTRY + 0x28, length)
    if method == 2:
        script_zlib(rng, length)
    if rng.random() < 0.1:
        STATE["fail"] = {1} if method == 2 else {rng.choice([0, 1])}
    return [slab + ENTRY, slab + SRC, rng.choice([0, 1])], img.pokes()


# ---- the package header ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def package_world(env, rng):
    img = Image()
    begin(env)
    name = text_for(rng, 1, 20)
    other = text_for(rng, 1, 20)
    current = rng.choice(["empty", "same", "other", "other"])
    cur = b"" if current == "empty" else (name if current == "same" else other)
    img.put(0x006A7578, cur + b"\0" + bytes(rng.getrandbits(8) for _ in range(0x20)))
    img.u32(0x006A7678, rng.choice([0, env.slab + ARENA + 0x400, rng.getrandbits(32)]))
    img.u32(0x006A767C, rng.choice([0, env.slab + ARENA + 0x440, rng.getrandbits(32)]))
    return img, name


@pkr(0x004E9780, "PKR_PreLoad")
def gen_preload(env, n):
    rng = env.rng
    img, name = package_world(env, rng)
    address = put_text(img, env, 0, name)
    STATE["stream"] = rng.choice([0, 0x1234, 0x7F000000])
    magic = rng.choice([0x32524B50, 0x32524B50, 0x32524B50, 0x31524B50])
    STATE["hdr"] = struct.pack("<IIII", magic, rng.getrandbits(32), rng.choice([0, 1, 2, 3]), rng.choice([0, 1, 2, 3]))
    return [address], img.pokes()


@pkr(0x004E9B80, "PKR_UnLoad")
def gen_unload(env, n):
    rng = env.rng
    img, name = package_world(env, rng)
    address = put_text(img, env, 0, name if rng.random() < 0.6 else text_for(rng, 1, 20))
    return [address], img.pokes()


@pkr(0x004E9930, "PKR_Log")
def gen_pkr_log(env, n):
    img, rng = Image(), env.rng
    begin(env)
    return [rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


# ---- the stream object -----------------------------------------------------------------------------------------------------------------------------------------------------------------

def stream_world(env, rng):
    img = Image()
    begin(env)
    base = env.slab + STREAM
    img.put(base, bytes(rng.getrandbits(8) for _ in range(0x10)))
    img.u32(base + 4, rng.choice([0, 0, 0x1234]))
    far = env.slab + STREAM + 0x2458
    img.put(far, bytes(rng.getrandbits(8) for _ in range(8)))
    return img, base


@pkr(0x004EBF10, "Stream_Construct", this=STREAM)
def gen_stream_construct(env, n):
    img, base = stream_world(env, env.rng)
    return [], img.pokes()


@pkr(0x004EBF50, "Stream_ScalarDelete", this=STREAM)
def gen_stream_delete(env, n):
    img, base = stream_world(env, env.rng)
    return [env.rng.choice([0, 1, 0x101, 0xFE])], img.pokes()


@pkr(0x004EBF70, "Stream_Destruct", this=STREAM)
def gen_stream_destruct(env, n):
    img, base = stream_world(env, env.rng)
    return [], img.pokes()


# ---- exp.cpp and the head file ---------------------------------------------------------------------------------------------------------------------------------------------------------

@pkr(0x00448860, "Exp_HitEnvItem")
def gen_hit_env_item(env, n):
    img, rng = Image(), env.rng
    begin(env)
    slab = env.slab
    count = rng.choice([0, 1, 2, 3, 4])
    objects = []
    for k in range(count):
        o = slab + ENVOBJ + 0x40 * k
        img.put(o, bytes(rng.getrandbits(8) for _ in range(0x40)))
        img.u8(o + 0x1F, rng.randrange(0, 3))
        img.u16(o + 0x1A, rng.randrange(0, 8))
        objects.append(o)
    for a, b in zip(objects, objects[1:] + [0]):
        img.u32(a + 0x20, b)
    img.u32(0x0056DB28, objects[0] if objects else 0)
    for region in range(3):
        img.u8(0x0056D432 + 0x44 * region, rng.choice([0, 1, 1, 1]))
        img.u32(0x0056D43C + 0x44 * region, slab + REGTAB)
    for k in range(8):
        img.u32(slab + REGTAB + 4 * k, slab + DESCS + 8 * k)
        img.u16(slab + DESCS + 8 * k, rng.choice([0, 2, 3, 0xFFFF, 1, rng.getrandbits(16)]))
        img.u16(slab + DESCS + 8 * k + 2, rng.getrandbits(16))
    pick = rng.choice(objects + [0, slab + ENVOBJ + 0x300, objects[0] if objects else 0])
    return [pick, rng.choice([0, 1, 5, rng.getrandbits(32)]), rng.choice([0xFFFF, 0xFFFF, 0, 1, 0x10000, rng.getrandbits(32)])], img.pokes()


@pkr(0x00448990, "Exp_Construct", this=EXPOBJ)
def gen_exp_construct(env, n):
    img, rng = Image(), env.rng
    begin(env)
    slab = env.slab
    img.put(slab + EXPOBJ, bytes(rng.getrandbits(8) for _ in range(0x100)))
    count = rng.choice([0, 0, 1, 2, 5, -1, -7])
    img.u32(slab + EXPOBJ + 0x40, u(count))
    img.u32(slab + EXPOBJ + 0x44, slab + EXPENT)
    img.put(slab + EXPENT, bytes(rng.getrandbits(8) for _ in range(0x100)))
    img.put(slab + TEXT, b"".join(struct.pack("<I", u(rng.choice([0, 1, -1, 0x1000, rng.getrandbits(32)]))) for _ in range(3)))
    return [slab + TEXT, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@pkr(0x00448AD0, "Exp_Move", this=EXPOBJ)
def gen_exp_move(env, n):
    """12 s5: an explosion of up to 16 entries (16 bytes each at +0x44): the age (+0x5c) grows by its step (+0x60) and at the limit (+0x58) the object dies; the sine and cosine are retail code."""
    img, rng = Image(), env.rng
    begin(env)
    slab = env.slab
    img.put(slab + EXPOBJ, bytes(rng.getrandbits(8) for _ in range(0x100)))
    count = rng.choice([1, 2, 3, 4, 5, 8, 16, 1, 6])
    img.u32(slab + EXPOBJ + 0x40, count)
    img.u32(slab + EXPOBJ + 0x44, slab + EXPENT)
    img.put(slab + EXPENT, bytes(rng.getrandbits(8) for _ in range(0x100)))
    limit = rng.choice([30, 60, 100, 0x100, 0x1000, rng.randrange(1, 0x2000)])
    age = rng.choice([0, 1, limit - 1, limit - 2, limit // 2, rng.randrange(0, limit)])
    step = rng.choice([0, 1, 2, 3, 10, rng.randrange(0, 0x100), 0x100000])
    img.u32(slab + EXPOBJ + 0x58, limit)
    img.u32(slab + EXPOBJ + 0x5C, age)
    img.u32(slab + EXPOBJ + 0x60, step)
    img.u32(slab + EXPOBJ + 0x64, rng.choice([0, 0xFFF, 0x1000, rng.randrange(0, 0x1000), rng.getrandbits(32)]))
    return [], img.pokes()


@pkr(0x00448AA0, "Exp_ScalarDelete", this=EXPOBJ)
def gen_exp_scalar_delete(env, n):
    img, rng = Image(), env.rng
    begin(env)
    img.put(env.slab + EXPOBJ, bytes(rng.getrandbits(8) for _ in range(0x10)))
    return [rng.choice([0, 1, 0x101, 0xFE])], img.pokes()


@pkr(0x00448AC0, "Exp_Destruct", this=EXPOBJ)
def gen_exp_destruct(env, n):
    img, rng = Image(), env.rng
    begin(env)
    img.put(env.slab + EXPOBJ, bytes(rng.getrandbits(8) for _ in range(0x10)))
    return [], img.pokes()


@pkr(0x00448C60, "FileIO_LoadHed")
def gen_load_hed(env, n):
    img, rng = Image(), env.rng
    begin(env)
    img.u32(0x005615D8, rng.choice([0, 0, env.slab + ARENA + 0x600]))
    img.u32(0x0056152C, rng.choice([0, 1, 0x7FF, 0x800, 0x801, rng.randrange(0, 0x1000)]))
    return [], img.pokes()


@pkr(0x00448C90, "FileIO_UnloadHed")
def gen_unload_hed(env, n):
    img, rng = Image(), env.rng
    begin(env)
    img.u32(0x005615D8, rng.choice([0, env.slab + ARENA + 0x600]))
    return [], img.pokes()


# ---- unlisted functions of the same files --------------------------------------------------------------------------------------------------------------------------------------

@pkr(0x00448CB0, "FileIO_HashName")
def gen_file_name_hash(env, n):
    img, rng = Image(), env.rng
    begin(env)
    return [put_text(img, env, 0, text_for(rng, 0, 30))], img.pokes()


@pkr(0x004AC1B0, "RelocatableModule_Empty")
def gen_module_empty(env, n):
    begin(env)
    return [], Image().pokes()


@pkr(0x004AC060, "RelocatableModule_Destroy")
def gen_module_destroy(env, n):
    begin(env)
    return [], Image().pokes()


@pkr(0x004E9630, "PKR2_CompressedSize")
def gen_compressed_size(env, n):
    img, rng = Image(), env.rng
    begin(env)
    length = rng.choice([1, 2, 3, 40, rng.randrange(1, 0x100)])
    img.put(env.slab + SRC, runs(rng, length))
    method = rng.choice([-5, -3, -2, -2, 0, 0, 1, 1, 2, 2])
    script_zlib(rng, length)
    if rng.random() < 0.1:
        STATE["fail"] = {1}
    return [env.slab + SRC, length, u(method)], img.pokes()


@pkr(0x004E9670, "PKR2_BestMethod")
def gen_best_method(env, n):
    img, rng = Image(), env.rng
    begin(env)
    length = rng.choice([1, 2, 3, 40, rng.randrange(1, 0x100)])
    img.put(env.slab + SRC, runs(rng, length))
    img.u32(env.slab + OUTLEN, rng.getrandbits(32))
    script_zlib(rng, length)
    return [env.slab + SRC, length, rng.choice([env.slab + OUTLEN, 0])], img.pokes()
