"""Function-level oracle for the PSX region parser (Phase 5.4a, 7b continued, part 4).

The same script (`src/hooks/oracle_script.h`) runs in the hooked retail process with hooks off, where `PSX_ParseEnvironment` (0x004b2450) and everything under it is retail
code, and in `opentony_spike.exe`, where it is `PsxEnvKit` (src/runtime/psx_env_kit.*) with the Kits underneath. Each scenario builds a region image the way the spooler
holds one after loading it (header, object records, model offsets, models, the tag stream, the trailing tables), points a free spool slot at it, calls the parser and prints
what it left: the slot record, the object table, every model byte that is not a pointer, the scene materials the checksum list and the faces now point at (their checksum
and use count stand for the pointer), the palettes in the source lookup, and the globals. Heap pointers never print; each is checked by a `same` against what it must be.

The regions are random but fixed by their seed (`random.Random`), so the script is one deterministic text. Their shape follows the parser's own reads (build/s5/asm_parse_env.txt,
asm_finalize.txt), not a file format: a face's flag word and length, a model's three counts, a wib list, palette groups of 16 and 256 colours.
"""

from __future__ import annotations

import random
import struct
from dataclasses import dataclass, field

from . import texture_oracle as base

OUT_ROOT = base.ROOT / "build/oracle/psx_env"

SLOT = 18                                  # a free slot of the 20 (state 0, no name)
SLOT_RECORD = 0x0056D428 + SLOT * 0x44
BUCKETS = 0x0056CC18                       # the scene material hash table: 512 dwords
PALETTE_BASE = 0x0056DB3C
PALETTE_FLAG = 0x0056DB40
QUEUE_READ = 0x0056DB30
QUEUE_KIND0 = 0x0056D9D6                   # the kind byte of queue entry 0

TAG_WIB, TAG_PULSE, TAG_ANIM, TAG_ANIM_ALT, TAG_HIER, TAG_RGB = 6, 7, 0x2A, 0x2C, 0x52454948, 0x73424752
TAG_ZONE, TAG_NAMED = 10, 0x45             # the zone blockmap (M3D_LoadZoneBlockmap) and the named resources of the bits PSX (BITS_ParseNamedResourceTag)

BITS_LIST = 0x0056DB38                     # the list of named-resource tags (one node per parsed tag)
CHEAT_BYTE = 0x0056A376 + 4 * 0xD          # Career_GetCheat(0xd) answers this byte when nothing else decides
ENV_REGIONS = 0x00537C80                   # the slots of the two environments (Spool_GetEnvIndex(slot) is 0 for the first)
ZONE_TABLE = 0x00567F80                    # the zone blockmap of environment 0

CACHE_HEAD = 0x006A0524                    # the D3D palette cache list: head, tail, count
CACHE_TAIL = 0x006A0528
CACHE_COUNT = 0x006A0518
LOOKUP_HEAD = 0x0056B2F0                  # Palette_FindSourceLookupNode's list
FREE_256 = 0x0056B2F4                      # 256 flag bytes each: the slot is free
FREE_16 = 0x0056B3F4

MATERIAL_BLOCKED = 0xA61814C4             # a material PSX_LoadTextureReferences always marks "tiled"


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def u16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


@dataclass
class Face:
    offset: int                  # in the region
    has_material: bool
    special: bool                # (flags & 3) == 3 and flags & 0x20: +0x1c is read from the material record, which holds heap leftovers


@dataclass
class Region:
    data: bytes
    objects: int
    models: list[tuple[int, int]]                    # (start, end) of each model body
    faces: list[Face]
    models_at: int                                   # the model count; the offsets follow
    tags_at: int
    tag_bodies: dict[int, int]                       # tag -> offset of its data
    anim: int | None                                 # where the last animation tag (0x2a or 0x2c) put its data
    trailing: int                                    # the first word after the stream's terminator
    checksums: list[int]                             # the material checksums as the file holds them
    small: list[int]                                 # the 16-colour palettes' checksums
    wide: list[int]
    checks_at: int = 0
    palette_at: int = 0
    small_data: list[int] = field(default_factory=list)   # where each palette's colours are in the region
    wide_data: list[int] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    textures: list[TextureRecord] = field(default_factory=list)
    zone_cells: list[tuple[int, int, int, list[tuple[int, int]]]] = field(default_factory=list)   # (column, row, cell offset, [(item offset, object index)])
    zone_counts: tuple[int, int] = (0, 0)
    named: list[tuple[int, bytes, int, list[tuple[int, int]]]] = field(default_factory=list)      # (entry offset, name, frames, [(frame offset, material index)])


@dataclass
class TextureRecord:
    start: int                   # in the region
    flags: int
    colours: int
    palette: int                 # the palette's checksum, the way the record names it
    material: int                # index into the checksum list
    width: int
    height: int


def zone_payload(rng: random.Random, objects: int) -> tuple[bytes, int, int, list[tuple[int, int, int, list[tuple[int, int]]]]]:
    """The tag-10 data: the five-dword header (a bounding range, the cell counts), then per row and column a cell [8 bytes, item count, items, 1 dword]."""
    columns = rng.randrange(1, 4)
    rows = rng.randrange(1, 4)
    out = bytearray()
    for value in (rng.randrange(-200, 200), rng.randrange(-100, 100), rng.randrange(-4000, 4000), rng.randrange(0, 4000)):   # the range's ends; the cell width is (third - first) / columns
        out += u32(value)
    out += u32(columns | (rows << 16))
    cells: list[tuple[int, int, int, list[tuple[int, int]]]] = []
    for row in range(rows):
        for column in range(columns):
            start = len(out)
            out += rng.randbytes(8)
            items = rng.randrange(0, 4) if objects else 0
            out += u32(items)
            listed: list[tuple[int, int]] = []
            for _ in range(items):
                listed.append((len(out), rng.randrange(objects)))
                out += u32(listed[-1][1])
            out += rng.randbytes(4)
            cells.append((column, row, start + 12, listed))
    return bytes(out), columns, rows, cells


def named_payload(rng: random.Random, pictures: int) -> tuple[bytes, list[tuple[int, bytes, int, list[tuple[int, int]]]]]:
    """The tag-0x45 data: a count, then per resource [8-character name, frame count, frames of (a dword, a picture index)]."""
    out = bytearray()
    entries: list[tuple[int, bytes, int, list[tuple[int, int]]]] = []
    count = rng.randrange(1, 4)
    out += u32(count)
    used: set[bytes] = set()
    for _ in range(count):
        while True:
            name = bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ") for _ in range(rng.randrange(3, 9))).ljust(8, b"\0")
            if name not in used:
                used.add(name)
                break
        frames = rng.randrange(0, 5)
        entry_at = len(out)
        out += name + u32(frames)
        listed: list[tuple[int, int]] = []
        for _ in range(frames):
            listed.append((len(out), rng.randrange(pictures)))
            out += u32(rng.getrandbits(32)) + u32(listed[-1][1])
        entries.append((entry_at, name, frames, listed))
    return bytes(out), entries


def wib_data(rng: random.Random) -> bytes:
    out = bytearray()
    for _ in range(rng.randrange(3)):
        count = rng.randrange(1, 4)
        header = bytearray(rng.randbytes(16))
        header[0:4] = u32(rng.randrange(1, 1 << 32))
        header[0xC:0xE] = u16(count)
        out += header + rng.randbytes(16 * count)
    return bytes(out) + u32(0)


def build_region(rng: random.Random, *, objects: int, models: int, checksums: list[int], small: list[int], wide: list[int], tags: list[int], records: int = 0) -> Region:
    out = bytearray(12)
    for _ in range(objects):
        record = bytearray(rng.randbytes(0x24))
        record[0x16:0x18] = u16(rng.randrange(models))
        out += record
    models_at = len(out)
    out += u32(models) + bytes(4 * models)
    bodies: list[tuple[int, int]] = []
    faces: list[Face] = []
    for index in range(models):
        start = len(out)
        struct.pack_into("<I", out, models_at + 4 + 4 * index, start)
        entries = rng.randrange(4)
        face_count = rng.randrange(5)
        total = face_count + entries if rng.random() < 0.6 else rng.randrange(6)
        header = bytearray(rng.randbytes(0x1C))
        header[0] |= 0x08
        header[2:4] = u16(entries)
        header[4:6] = u16(total)
        header[6:8] = u16(face_count)
        if rng.random() < 0.3:
            header[0x18:0x1A] = u16(0xFFFF)
        if rng.random() < 0.3:
            header[0x1A:0x1C] = u16(0xFFFF)
        out += header + rng.randbytes(8 * (entries + total))
        for _ in range(face_count):
            length = rng.randrange(8, 13)
            flags = rng.getrandbits(18) if rng.random() < 0.5 else rng.getrandbits(18) & rng.getrandbits(18) & rng.getrandbits(18)   # dense and sparse flag words
            if not checksums:
                flags &= ~1
            word = (length << 18) | flags
            at = len(out)
            body = bytearray(rng.randbytes(4 * length))
            body[0:4] = u32(word)
            if flags & 1:
                body[0x10:0x14] = u32(rng.randrange(len(checksums)))
            out += body
            faces.append(Face(at, bool(flags & 1), (flags & 3) == 3 and bool(flags & 0x20)))
        bodies.append((start, len(out)))

    tags_at = len(out)
    bodies_of: dict[int, int] = {}
    anim: int | None = None
    zone_cells: list[tuple[int, int, int, list[tuple[int, int]]]] = []
    zone_counts = (0, 0)
    named: list[tuple[int, bytes, int, list[tuple[int, int]]]] = []
    for tag in tags:
        if tag == TAG_WIB:
            payload = wib_data(rng)
        elif tag == TAG_ZONE:
            payload, columns, rows, cells = zone_payload(rng, objects)
            zone_counts = (columns, rows)
            zone_cells = [(column, row, len(out) + 8 + offset, items) for column, row, offset, items in cells]
            zone_cells = [(column, row, offset, [(len(out) + 8 + item, index) for item, index in items]) for column, row, offset, items in zone_cells]
        elif tag == TAG_NAMED:
            payload, entries = named_payload(rng, len(checksums))
            named = [(len(out) + 8 + entry, name, frames, [(len(out) + 8 + frame, index) for frame, index in listed]) for entry, name, frames, listed in entries]
        else:
            payload = rng.randbytes(4 * rng.randrange(2, 9))
        out += u32(tag) + u32(len(payload))
        bodies_of[tag] = len(out)
        if tag in (TAG_ANIM, TAG_ANIM_ALT):
            anim = len(out)
        out += payload
    out += u32(0xFFFFFFFF)
    struct.pack_into("<II", out, 4, tags_at, objects)       # the header: where the tag stream starts, how many objects
    trailing = len(out)
    out += rng.randbytes(4 * models)                    # the model name checksums
    checks_at = len(out) + 4
    out += u32(len(checksums)) + b"".join(u32(value) for value in checksums)
    palette_at = len(out)
    small_data: list[int] = []
    wide_data: list[int] = []
    out += u32(len(small))
    for value in small:
        out += u32(value)
        small_data.append(len(out))
        out += rng.randbytes(32)
    out += u32(len(wide))
    for value in wide:
        out += u32(value)
        wide_data.append(len(out))
        out += rng.randbytes(512)
    # the texture records: a count, the offsets of the records, the records (flags, colours, the palette's checksum, the material's index, width and height, the pixels)
    textures: list[TextureRecord] = []
    if records and checksums and (small or wide):
        out += u32(records)
        list_at = len(out)
        out += bytes(4 * records)
        seen: set[int] = set()
        for k in range(records):
            colours = 0x10 if small and (not wide or rng.random() < 0.5) else 0x100
            palette = rng.choice(small if colours == 0x10 else wide)
            material = rng.randrange(len(checksums))
            width = rng.randrange(1, 20)
            height = rng.randrange(1, 20)
            flags = 0 if material in seen else rng.getrandbits(1)       # a second record for a material must not ask to be its owner
            seen.add(material)
            start = len(out)
            struct.pack_into("<I", out, list_at + 4 * k, start)
            row = ((width + 3) & ~3) // 2 if colours == 0x10 else (width + 1) & ~1
            out += u32(flags) + u32(colours) + u32(palette) + u32(material) + u16(width) + u16(height) + rng.randbytes(row * height + 8)
            textures.append(TextureRecord(start, flags, colours, palette, material, width, height))
    else:
        out += u32(0)
    return Region(bytes(out), objects, bodies, faces, models_at, tags_at, bodies_of, anim, trailing, checksums, small, wide, checks_at, palette_at, small_data, wide_data,
                  textures=textures, zone_cells=zone_cells, zone_counts=zone_counts, named=named)


def distinct(rng: random.Random, count: int, used: set[int]) -> list[int]:
    values: list[int] = []
    while len(values) < count:
        value = rng.randrange(1, 1 << 32)
        if value not in used:
            used.add(value)
            values.append(value)
    return values


def scenario(s: base.Script, name: str, rng: random.Random, *, objects: int, models: int, checksums: int, small: int, wide: int, tags: list[int], base_value: int = 0,
             preload: list[int] | None = None, saturate: bool = False, used: set[int], records: int = 0, cheat: bool = False, premake: bool = False) -> None:
    sums = distinct(rng, checksums, used)
    if preload:
        sums[: len(preload)] = preload[: len(sums)]
    region = build_region(rng, objects=objects, models=models, checksums=sums, small=distinct(rng, small, used), wide=distinct(rng, wide, used), tags=tags, records=records)
    s.comment(f"scenario {name}: {objects} objects, {models} models, {checksums} checksums, {small}+{wide} palettes, {len(region.textures)} texture records, "
              f"tags {[hex(t) for t in tags]}, palette base {base_value:#x}{', cheat 0xd' if cheat else ''}")
    s.add("set32", PALETTE_BASE, base_value)
    s.add("set8", PALETTE_FLAG, 1 if cheat else 0)
    s.add("set8", CHEAT_BYTE, 1 if cheat else 0)
    if cheat:
        s.add("call", f"{name}_cheat=0x416980", "c", 0xD)
        s.add("val", f"{name}_cheat_answer", f"{name}_cheat")
    s.add("fill", SLOT_RECORD, 0, 0x44)
    s.add("bytes", SLOT_RECORD, b"oracle\0".hex())
    s.add("alloc", "raw", len(region.data) + 16)
    s.add("bytes", "raw", region.data.hex())
    s.add("set32", SLOT_RECORD + 0x18, "raw")
    if saturate and sums:
        # a material that exists already with the largest use count: the parser must keep it there
        s.add("call", "pre=0x4b1f70", "c", sums[0] + base_value)
        s.add("set16", "pre+0x10", 0xFFFF)
    if premake:
        # the materials exist before the parse, with the bytes the named-resource tag and the texture records change set to zero (a fresh record holds allocator leftovers there)
        for k, value in enumerate(sums):
            s.add("call", f"pm{k}=0x4b1f70", "c", (value + base_value) & 0xFFFFFFFF)
            s.add("fill", f"pm{k}+4", 0, 8)
    if TAG_ZONE in tags:
        s.add("let", "old_env", f"[{ENV_REGIONS}]")
        s.add("set32", ENV_REGIONS, SLOT)                 # Spool_GetEnvIndex(slot) answers 0, the only index M3D_LoadZoneBlockmap accepts
    if TAG_NAMED in tags:
        s.add("set32", BITS_LIST, 0)
    s.add("echo", f"{name}:parse")
    s.add("flush")
    s.add("call", "0x4b2450", "c", SLOT)
    s.add("echo", f"{name}:dump")
    s.add("flush")
    dump(s, name, region, base_value)
    s.add("flush")
    if TAG_ZONE in tags:
        s.add("set32", ENV_REGIONS, "old_env")
    if cheat:
        s.add("set8", PALETTE_FLAG, 0)
        s.add("set8", CHEAT_BYTE, 0)


def same_or_zero(s: base.Script, label: str, word: str, expected: str | None) -> None:
    s.add("same", label, word, expected if expected is not None else "0")


def dump(s: base.Script, name: str, region: Region, base_value: int) -> None:
    slot = SLOT_RECORD
    s.add("mem", f"{name}_slot_head", slot, 12)
    same_or_zero(s, f"{name}_slot_checksums", f"[{slot + 0xC}]", f"raw+{region.trailing}")
    s.add("same", f"{name}_slot_objects_null", f"[{slot + 0x10}]", 0)
    same_or_zero(s, f"{name}_slot_models", f"[{slot + 0x14}]", f"raw+{region.models_at + 4}")
    same_or_zero(s, f"{name}_slot_raw", f"[{slot + 0x18}]", "raw")
    for label, offset, tag in (("hier", 0x20, TAG_HIER), ("rgb", 0x28, TAG_RGB), ("wib", 0x2C, TAG_WIB), ("pulse", 0x30, TAG_PULSE)):
        body = region.tag_bodies.get(tag)
        same_or_zero(s, f"{name}_slot_{label}", f"[{slot + offset}]", f"raw+{body}" if body is not None else None)
    same_or_zero(s, f"{name}_slot_anim", f"[{slot + 0x1C}]", f"raw+{region.anim}" if region.anim is not None else None)
    s.add("mem", f"{name}_slot_tail", slot + 0x34, 16)
    wib = region.tag_bodies.get(TAG_WIB)
    same_or_zero(s, f"{name}_cursor", "[0x56db1c]", f"raw+{wib - 8}" if wib is not None else None)
    s.add("val", f"{name}_bits_a", "[0x56af58]")
    s.add("val", f"{name}_bits_b", "[0x56af5c]")
    s.add("val", f"{name}_material_bytes", "[0x56db44]")

    # the object table
    if region.objects:
        s.add("let", "objs", f"[{slot + 0x10}]")
        s.add("val", f"{name}_object_count", "[objs-4]")
        for i in range(region.objects):
            at = 0x4C * i
            s.add("mem", f"{name}_object{i}_head", f"objs+{at}", 0x20)
            same_or_zero(s, f"{name}_object{i}_next", f"[objs+{at + 0x20}]", f"objs+{at + 0x4C}" if i < region.objects - 1 else None)
            s.add("mem", f"{name}_object{i}_tail", f"objs+{at + 0x24}", 0x28)

    # the scene materials the checksum list now holds
    for i in range(len(region.checksums)):
        entry = f"[raw+{region.checks_at + 4 * i}]"
        s.add("val", f"{name}_material{i}_checksum", f"[{entry}+0x18]")
        s.add("mem", f"{name}_material{i}_uses", f"{entry}+0x10", 2)

    # the zone blockmap (tag 10): the table of environment 0, then each cell's pointer into the tag and each item turned into an object pointer
    if region.zone_cells:
        s.add("mem", f"{name}_zone_flag", ZONE_TABLE, 4)
        s.add("mem", f"{name}_zone_range", ZONE_TABLE + 4, 0x14)
        s.add("mem", f"{name}_zone_counts", ZONE_TABLE + 0x1C, 4)
        for column, row, offset, items in region.zone_cells:
            s.add("same", f"{name}_zone_cell_{row}_{column}", f"[{ZONE_TABLE + 0x20 + row * 4 + column * 0x50}]", f"raw+{offset}")
            for item, index in items:
                s.add("same", f"{name}_zone_item_{row}_{column}_{item}", f"[raw+{item}]", f"objs+{0x4C * index}")

    # the named resources (tag 0x45): the tag joins the list, every frame's picture index became the scene material, which counts the use; the lookup finds each name
    if region.named:
        body = region.tag_bodies[TAG_NAMED]
        s.add("same", f"{name}_named_node", f"[[{BITS_LIST}]]", f"raw+{body}")
        s.add("same", f"{name}_named_end", f"[[{BITS_LIST}]+4]", 0)
        touched: set[int] = set()
        for entry, text, frames, listed in region.named:
            for frame, index in listed:
                s.add("same", f"{name}_named_frame_{frame}", f"[raw+{frame + 4}]", f"[raw+{region.checks_at + 4 * index}]")
                touched.add(index)
        for index in sorted(touched):
            s.add("mem", f"{name}_named_picture{index}", f"[raw+{region.checks_at + 4 * index}]+4", 8)
        for k, (entry, text, frames, listed) in enumerate(region.named):
            s.text(f"{name}_n{k}", text.rstrip(b"\0").decode())
            s.add("call", f"{name}_hit{k}=0x4b3680", "c", f"{name}_n{k}")
            s.add("same", f"{name}_named_find{k}", f"{name}_hit{k}", f"raw+{entry + 12}")
        s.text(f"{name}_absent", "ZZZZZZZZ")
        s.add("call", f"{name}_miss=0x4b3680", "c", f"{name}_absent")
        s.add("val", f"{name}_named_absent", f"{name}_miss")

    # the texture records: PSX_LoadTextureReferences gave the scene material its rectangle, page and palette node, and built a texture record from the pixels
    for k, texture in enumerate(region.textures):
        label = f"{name}_texture{k}"
        s.add("let", "mat", f"[raw+{region.checks_at + 4 * texture.material}]")
        s.add("call", "tnode=0x48a400", "c", (texture.palette + base_value) & 0xFFFFFFFF)
        s.add("mem", f"{label}_material_head", "mat", 6)
        s.add("maskval", f"{label}_material_page", "[mat+6]", 0xFFF0)       # the low nibble of the page word is a stack leftover
        s.add("mem", f"{label}_material_coords", "mat+8", 0xC)
        s.add("val", f"{label}_material_checksum", "[mat+0x18]")
        s.add("same", f"{label}_material_node", "[mat+0x1c]", "tnode")
        s.add("mem", f"{label}_material_flags", "mat+0x22", 2)
        s.add("maskval", f"{label}_node_uses", "[tnode+4]", 0xFFFF)
        s.add("let", "tex", "[mat+0x14]")
        s.dump_record(label, "tex")
        s.add("val", f"{label}_cache_checksum", "[[tex+0x20]]")                 # the palette cache record the upload found, and the colours it converted with
        s.add("val", f"{label}_cache_count", "[[tex+0x20]+4]")
        s.add("mem", f"{label}_cache_colours", "[[tex+0x20]+8]", 32)
        s.add("mem", f"{label}_source_pixels", "[tex+8]", 16)

    # the models, byte for byte, except the dwords that became pointers
    masked: dict[int, str] = {}
    for face in region.faces:
        if face.has_material:
            masked[face.offset + 0x10] = "pointer"
        if face.special:
            masked[face.offset + 0x1C] = "leftover"
    for k, (start, end) in enumerate(region.models):
        at = start
        for off in sorted(o for o in masked if start <= o < end):
            if off > at:
                s.add("mem", f"{name}_model{k}_{at}", f"raw+{at}", off - at)
            if masked[off] == "pointer":
                s.add("val", f"{name}_model{k}_{off}_checksum", f"[[raw+{off}]+0x18]")
                s.add("mem", f"{name}_model{k}_{off}_uses", f"[raw+{off}]+0x10", 2)
            at = off + 4
        if end > at:
            s.add("mem", f"{name}_model{k}_{at}", f"raw+{at}", end - at)

    # the palettes: the colours in the region (rewritten in place), found in the source lookup, converted
    for kind, sums, size, offsets in (("small", region.small, 32, region.small_data), ("wide", region.wide, 512, region.wide_data)):
        for i, value in enumerate(sums):
            s.add("mem", f"{name}_{kind}{i}_colours", f"raw+{offsets[i]}", size)
            s.add("call", "node=0x48a400", "c", (value + base_value) & 0xFFFFFFFF)
            s.add("same", f"{name}_{kind}{i}_missing", "node", 0)
            s.add("mem", f"{name}_{kind}{i}_flags", "node+3", 1)
            s.add("val", f"{name}_{kind}{i}_tag", "[[node+0xc]]")
            s.add("val", f"{name}_{kind}{i}_count", "[[node+0xc]+4]")
            s.add("same", f"{name}_{kind}{i}_source", "[[node+0xc]+0xc]", f"raw+{offsets[i]}")
            s.add("mem", f"{name}_{kind}{i}_converted", "[[node+0xc]+8]", size)


def build_script() -> str:
    s = base.Script()
    s.comment("function-level oracle of the PSX region parser: tony/psx_env_oracle.py")
    s.device()
    s.add("mem", "device_format", "0x6a04d8", 32)
    s.add("mem", "device_shifts", "0x6a04f8", 32)
    s.add("fill", BUCKETS, 0, 2048)
    s.add("set32", 0x0056DB44, 0)                       # the bytes of live materials: a delta, so both processes start at 0
    s.add("set32", QUEUE_READ, 0)
    s.add("set8", QUEUE_KIND0, 0)
    # the palette cache list of the D3D layer starts empty: a session image carries the list's head and tail but not the C runtime heap its records are in (the program with no
    # retail image would walk a chain of zero bytes), and the upload of a texture record searches this list by checksum
    s.add("set32", CACHE_HEAD, 0)
    s.add("set32", CACHE_TAIL, 0)
    s.add("set32", CACHE_COUNT, 0)
    # the palette slots the source lookup hands out depend on what the game had taken before the script ran: start from a lookup list with no nodes and every slot free
    s.add("set32", LOOKUP_HEAD, 0)
    s.add("fill", FREE_256, 1, 256)
    s.add("fill", FREE_16, 1, 256)
    used: set[int] = set()
    rng = random.Random(0x7B4)
    scenario(s, "a", rng, objects=2, models=2, checksums=3, small=1, wide=0, tags=[TAG_WIB, TAG_PULSE, TAG_ANIM, TAG_HIER, TAG_RGB], used=used)
    scenario(s, "b", rng, objects=0, models=1, checksums=0, small=0, wide=0, tags=[], used=used)
    scenario(s, "c", rng, objects=3, models=3, checksums=4, small=2, wide=1, tags=[TAG_RGB, TAG_ANIM_ALT, 0x99, 3, TAG_PULSE], base_value=0x1000, used=used)
    scenario(s, "d", rng, objects=1, models=2, checksums=2, small=0, wide=1, tags=[TAG_PULSE, TAG_WIB], saturate=True, used=used)
    for index in range(30):
        scenario(s, f"r{index}", rng, objects=rng.randrange(4), models=rng.randrange(1, 4), checksums=rng.randrange(5), small=rng.randrange(3), wide=rng.randrange(2),
                 tags=rng.sample([TAG_WIB, TAG_PULSE, TAG_ANIM, TAG_HIER, TAG_RGB, 0x99], rng.randrange(7)), base_value=rng.choice([0, 0, 0x2000]), used=used)
    # 8d: texture records, the palette cheat, the zone blockmap tag and the named-resource tag
    s.comment("8d: the parts the first version of this oracle left out")
    scenario(s, "t1", rng, objects=1, models=1, checksums=3, small=2, wide=1, records=4, tags=[], used=used)
    scenario(s, "t2", rng, objects=1, models=1, checksums=3, small=2, wide=1, records=4, tags=[TAG_PULSE], used=used)
    scenario(s, "t3", rng, objects=2, models=2, checksums=2, small=1, wide=1, records=3, tags=[], preload=[MATERIAL_BLOCKED], used=used)
    scenario(s, "c1", rng, objects=1, models=1, checksums=2, small=2, wide=1, tags=[], cheat=True, used=used)
    scenario(s, "t4", rng, objects=1, models=1, checksums=3, small=2, wide=1, records=4, tags=[], cheat=True, used=used)
    scenario(s, "z1", rng, objects=3, models=1, checksums=1, small=0, wide=0, tags=[TAG_ZONE], used=used)
    scenario(s, "z2", rng, objects=2, models=2, checksums=2, small=1, wide=0, tags=[TAG_PULSE, TAG_ZONE, TAG_WIB], used=used)
    scenario(s, "n1", rng, objects=0, models=0, checksums=3, small=0, wide=0, tags=[TAG_NAMED], premake=True, used=used)
    scenario(s, "n2", rng, objects=0, models=0, checksums=4, small=1, wide=0, tags=[TAG_WIB, TAG_NAMED], base_value=0x500, premake=True, used=used)
    more = random.Random(0x8D)
    for index in range(24):
        objects = more.randrange(4)
        models = more.randrange(1, 4)
        shape = more.choice(["textures", "zone", "named", "textures", "plain"])
        tags = more.sample([TAG_WIB, TAG_PULSE, TAG_ANIM, TAG_HIER, TAG_RGB, 0x99], more.randrange(4))
        records = 0
        if shape == "named":
            objects, models, tags = 0, 0, tags + [TAG_NAMED]
        if shape == "zone" and objects:
            tags = tags + [TAG_ZONE]
        if shape == "textures":
            records = more.randrange(1, 5)
        scenario(s, f"m{index}", rng, objects=objects, models=models, checksums=more.randrange(1, 5), small=more.randrange(1, 3), wide=more.randrange(2), tags=tags,
                 records=records, cheat=more.random() < 0.25, premake=shape == "named", base_value=0 if records else more.choice([0, 0, 0x2000]), used=used)
    # Not here, on purpose: texture records under a non-zero palette base. The upload looks the palette up by the record's own number, which is not the base-shifted one the
    # source lookup keeps ("Palette Checksum not found"); the retail process stops in the parse of such a scenario (an access violation; no cause found), so there is nothing
    # to compare it with. The standalone takes the same path to the head of the palette cache and goes on.
    s.add("set32", "0x29da3a0", 0)
    return "\n".join(s.lines) + "\n"


def oracle_command(_args=None) -> int:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = OUT_ROOT / "psx_env.oracle"
    script.write_text(build_script())
    print(f"script: {script} ({len(script.read_text().splitlines())} lines)")
    standalone = base.run_standalone(script, OUT_ROOT / "standalone.out")
    retail = base.run_retail(script, label="psx-env-oracle")
    (OUT_ROOT / "retail.out").write_text(retail)
    problems = base.compare(retail, standalone)
    for problem in problems:
        print(problem)
    print(f"{len(retail.splitlines())} lines compared, {len(problems)} differ")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(oracle_command())
