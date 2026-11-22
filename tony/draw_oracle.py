"""Function-level oracle of the draw path (Phase 5, milestone 8d).

Milestone 7c checked `DrawKit` against retail's primitive stream on one screen (114 of 114 draws, the memory image after 60 ticks). That cannot see a word the screen does not
move: the Options frame's last primitive is the mouse cursor, which is also the last primitive of the start image. This oracle runs retail's own functions (hooks off, in the
hooked process: `D3DPoly_StartScene`, `Db_ClearOrderingTable`, `D3D_DrawPolygonList` on one-packet lists, `D3D_PresentFrame`) and the natives of `DrawKit` (in
`opentony_spike.exe`) on the same packets, and compares every word the draw path writes: the scene words, the device-state caches, the polygon counters, the texture bindings,
the packet itself (the viewport packet stores its wrapped top) and the two D3DTLVERTEX staging buffers the D3D_Draw* handlers fill (0x00549dc0 textured, 0x00549e30
untextured), which are the words 7c's header named as not written. The pictures are not compared here (the stream oracle, `tony.standalone_frame`, does that).
"""

from __future__ import annotations

import random
import struct

from . import texture_oracle as base

OUT_ROOT = base.ROOT / "build/oracle/draw"

SCENE_ACTIVE = 0x006A04C4
SCENE_LIST_DRAWN = 0x006A04C5
SCENE_COUNT = 0x006A04B8
BOUND_TEXTURE = 0x006A04C0
STAGES_SET = 0x006A04B4
TEXPAGE_BLEND = 0x006A04BC
CACHES = 0x00549DA0                 # seven dwords: A, colour blend, C, depth, alpha stage, F, G
POLY_ALPHA = 0x0069FC60
PACKET_FLAGS = 0x0065FC5C
PACKET_RAW = 0x006A0064
PRIMITIVES = 0x006A0068
DESCRIPTOR = 0x006A0080
BINDINGS = 0x006A0084
FOG_YON = 0x00549D98
FOG_THRESHOLD = 0x00549D9C
STAGING = 0x00549DC0                # 0xC0 bytes: the textured buffer (4 x 0x1c) and the untextured one (4 x 0x14) that follows it
STAGING_SIZE = 0xC0

START_SCENE = 0x004D3B40
DRAW_LIST = 0x004D3160
CLEAR_TABLE = 0x004D3B00
PRESENT = 0x004D0C30
ATTACH_TO_MATERIAL = 0x004DA3E0
CONTROLFP = 0x005015A6
DISPATCH_TABLE = 0x0065F82C           # D3DPrimitiveDispatchTable: the handler of a command is the dword at the table + 4 * command


def f32(value: float) -> bytes:
    return struct.pack("<f", value)


def s16(value: int) -> bytes:
    return struct.pack("<h", value)


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


# kind: (vertex count, z offset, texture offset or 0, xy offsets, rgb offsets, uv offsets): the order the vertices are submitted in, as the Kit's table (src/runtime/draw_kit.cpp)
LAYOUTS = {
    0x20: (3, 0x14, 0, (0x08, 0x0C, 0x10), (4,), ()),
    0x24: (3, 0x20, 0x24, (0x08, 0x10, 0x18), (4,), (0x0C, 0x14, 0x1C)),
    0x28: (4, 0x18, 0, (0x08, 0x0C, 0x14, 0x10), (4,), ()),
    0x2C: (4, 0x28, 0x2C, (0x08, 0x10, 0x20, 0x18), (4,), (0x0C, 0x14, 0x24, 0x1C)),
    0x30: (3, 0x1C, 0, (0x08, 0x10, 0x18), (4, 0x0C, 0x14), ()),
    0x34: (3, 0x28, 0x2C, (0x08, 0x14, 0x20), (4, 0x10, 0x1C), (0x0C, 0x18, 0x24)),
    0x38: (4, 0x24, 0, (0x08, 0x10, 0x20, 0x18), (4, 0x0C, 0x1C, 0x14), ()),
    0x3C: (4, 0x34, 0x38, (0x08, 0x14, 0x2C, 0x20), (4, 0x10, 0x28, 0x1C), (0x0C, 0x18, 0x30, 0x24)),
    0x40: (2, 0x10, 0, (0x08, 0x0C), (4,), ()),
    0x48: (3, 0x18, 0, (0x08, 0x0C, 0x10), (4,), ()),
    0x4C: (4, 0x1C, 0, (0x08, 0x0C, 0x14, 0x10), (4,), ()),
    0x50: (2, 0x14, 0, (0x08, 0x10), (4, 0x0C), ()),
}
TILES = (0x60, 0x68, 0x70, 0x78)
# the D3D_Draw* handlers are what the walk dispatches to: thiscalls on the packet, read from D3DPrimitiveDispatchTable the way the walk reads them
HANDLERS = {kind: f"[{DISPATCH_TABLE + 4 * kind:#x}]" for kind in (*LAYOUTS, 0x60, 0xB0)}


class Draw:
    def __init__(self) -> None:
        self.s = base.Script()
        self.rng = random.Random(0x8D5)
        self.cases = 0

    def add(self, *parts: object) -> None:
        self.s.add(*parts)

    # ---- the textures the packets name ----
    def textures(self) -> None:
        s = self.s
        s.device()
        # an opaque 8x8 texture (no palette entry is zero) and one with transparent entries (a zero entry clears the record's "no transparent pixel" flag)
        s.image("tex_image_a", base.bitmap8(8, 8, base.gradient_palette(256, key_at=None, zero_at=None), base.picture(8, 8, 256)))
        s.image("tex_image_b", base.bitmap8(20, 12, base.gradient_palette(256, key_at=5, zero_at=0), base.picture(20, 12, 4)))
        for label in ("a", "b"):
            s.add("call", f"tex_record_{label}=0x4d9950", "c", f"tex_image_{label}", 0, 1, 0)
            s.add("alloc", f"tex_material_{label}", 0x30)
            s.add("call", hex(ATTACH_TO_MATERIAL), "c", f"tex_material_{label}", f"tex_record_{label}")
            s.add("alloc", f"tex_object_{label}", 0x20)
            s.add("set32", f"tex_object_{label}+0x14", f"tex_record_{label}")
            s.add("mem", f"tex_{label}_flags", f"tex_record_{label}+0x10", 4)
            s.add("mem", f"tex_{label}_size", f"tex_record_{label}+0x14", 4)

    # ---- the state a case starts from ----
    def reset(self, label: str) -> None:
        s, rng = self.s, self.rng
        for index in range(7):
            s.add("set32", CACHES + 4 * index, rng.choice([0xFFFFFFFF, 0, 1, 2, 3]))
        s.add("set32", BOUND_TEXTURE, 0)
        s.add("set32", BINDINGS, 0)
        s.add("set32", PRIMITIVES, 0)
        s.add("set32", DESCRIPTOR, 0)
        s.add("set32", PACKET_FLAGS, 0)
        s.add("set8", PACKET_RAW, 0)
        s.add("set32", POLY_ALPHA, 0)
        s.add("set32", TEXPAGE_BLEND, rng.randrange(4))
        s.add("set8", STAGES_SET, rng.randrange(2))
        s.add("pattern", STAGING, STAGING_SIZE, rng.randrange(1 << 20))

    def dump(self, label: str, packet: str, packet_size: int, records: list[str]) -> None:
        s = self.s
        s.add("mem", f"{label}_scene", SCENE_ACTIVE, 2)
        s.add("mem", f"{label}_caches", CACHES, 0x1C)
        s.add("mem", f"{label}_stages", STAGES_SET, 1)
        s.add("mem", f"{label}_texpage", TEXPAGE_BLEND, 4)
        s.add("mem", f"{label}_alpha", POLY_ALPHA, 4)
        s.add("mem", f"{label}_flags", PACKET_FLAGS, 4)
        s.add("mem", f"{label}_raw", PACKET_RAW, 1)
        s.add("val", f"{label}_primitives", f"[{PRIMITIVES}]")
        s.add("val", f"{label}_bindings", f"[{BINDINGS}]")
        for name, address in (("bound", BOUND_TEXTURE), ("descriptor", DESCRIPTOR)):      # (heap pointers: said by which record they are)
            s.add("same", f"{label}_{name}_none", f"[{address}]", 0)
            for record in records:
                s.add("same", f"{label}_{name}_is_{record}", f"[{address}]", record)
        s.add("mem", f"{label}_staging", STAGING, STAGING_SIZE)
        if packet_size:
            s.add("mem", f"{label}_packet", packet, packet_size)

    def run(self, label: str, packet: str, packet_size: int, handler: str | None = None) -> None:
        """Draw the one-packet list, or (with `handler`) call that D3D_Draw* function on the packet with the state words a walk would have left.

        `packet_size` is how much of the packet to compare afterwards (0: none: the packet holds a texture pointer, which is a heap address)."""
        s, rng = self.s, self.rng
        if handler is None:
            s.add("call", hex(DRAW_LIST), "c", packet)
        else:
            s.add("set32", POLY_ALPHA, rng.choice([0xFF000000, 0x80000000, 0x40000000, 0]))
            s.add("set32", CACHES + 4, rng.choice([1, 2, 4]))
            s.add("set32", CACHES + 12, rng.choice([0, 1, 2]))
            s.add("set8", PACKET_RAW, rng.randrange(2))
            # the walk runs the handlers at the single precision it selects (_controlfp(0x20000, 0x30000), and 0x9001f again afterwards); a handler called alone runs at the
            # precision the game thread has, which rounds the texture coordinates another way
            s.add("call", hex(CONTROLFP), "c", 0x20000, 0x30000)
            s.add("call", handler, "t", packet)
            s.add("call", hex(CONTROLFP), "c", 0x9001F, 0xFFFFF)
        self.dump(label, packet, packet_size, ["tex_record_a", "tex_record_b"])
        self.cases += 1
        if self.cases % 8 == 0:
            s.add("flush")

    # ---- packets ----
    def coordinates(self, flavour: str, count: int) -> list[tuple[int, int]]:
        rng = self.rng
        if flavour == "wide":                        # wider than 1024 or taller than 512: the size guard drops it
            return [(rng.randrange(-700, 700), rng.randrange(-300, 300)) for _ in range(count)]
        return [(rng.randrange(-40, 680), rng.randrange(-40, 500)) for _ in range(count)]

    def psx(self, label: str, kind: int, *, semi: int, raw: int, flavour: str, texture: str, negative_z: bool, direct: bool = False) -> None:
        s, rng = self.s, self.rng
        count, z_at, texture_at, xy, rgb, uv = LAYOUTS[kind]
        packet = f"{label}_p"
        size = 0x40
        s.add("alloc", packet, size)
        s.add("pattern", f"{packet}+4", size - 4, rng.randrange(1 << 20))
        s.add("set32", packet, 0)
        for (x, y), at in zip(self.coordinates(flavour, count), xy):
            s.add("bytes", f"{packet}+{at}", (s16(x) + s16(y)).hex())
        colours = list(rgb) * 1
        for at in colours:
            s.add("bytes", f"{packet}+{at}", bytes(rng.randrange(256) for _ in range(3)).hex())
        command = kind | (semi << 1) | raw
        s.add("bytes", f"{packet}+7", bytes([command]).hex())
        for at in uv:
            s.add("bytes", f"{packet}+{at}", bytes((rng.randrange(256), rng.randrange(256))).hex())
        if texture_at:
            tpage_at = 0x1A if kind & 0x10 else 0x16
            s.add("bytes", f"{packet}+{tpage_at}", (rng.randrange(1 << 16)).to_bytes(2, "little").hex())
            s.add("set32", f"{packet}+{texture_at}", {"a": "tex_object_a", "b": "tex_object_b", "none": "0"}[texture])
        z = -1.0 if negative_z else rng.choice([0.5, 1.0, 100.0, 4096.0, 0.0])
        s.add("bytes", f"{packet}+{z_at}", f32(z).hex())
        self.reset(label)
        self.run(label, packet, 0 if texture_at and texture != "none" else size, HANDLERS[kind] if direct else None)      # (a texture pointer in the packet is a heap address: not compared)

    def tile(self, label: str, kind: int, *, semi: int, flavour: str, negative_z: bool, direct: bool = False) -> None:
        s, rng = self.s, self.rng
        packet = f"{label}_p"
        s.add("alloc", packet, 0x20)
        s.add("pattern", f"{packet}+4", 0x1C, rng.randrange(1 << 20))
        s.add("set32", packet, 0)
        x, y = self.coordinates(flavour, 1)[0]
        width = rng.randrange(1, 300) if flavour != "wide" else rng.randrange(900, 1200)
        s.add("bytes", f"{packet}+8", (s16(x) + s16(y) + s16(width) + s16(rng.randrange(1, 200))).hex())
        s.add("bytes", f"{packet}+4", bytes(rng.randrange(256) for _ in range(3)).hex())
        s.add("bytes", f"{packet}+7", bytes([kind | (semi << 1)]).hex())
        s.add("bytes", f"{packet}+0x10", f32(-1.0 if negative_z else rng.choice([0.5, 1.0, 8.0])).hex())
        self.reset(label)
        self.run(label, packet, 0x20, HANDLERS[kind] if direct else None)

    def general(self, label: str, *, flags: int, texture: str, count: int, direct: bool = False) -> None:
        """0xB0: a polygon of pre-transformed vertices. Its handler stages nothing; the words it leaves are the caches, the bindings and the texture it bound."""
        s, rng = self.s, self.rng
        stride = 28 if texture != "none" else 20
        packet = f"{label}_p"
        size = 0x18 + 66 * stride
        s.add("alloc", packet, size)
        s.add("pattern", f"{packet}+4", 0x14, rng.randrange(1 << 20))
        s.add("set32", packet, 0)
        s.add("bytes", f"{packet}+7", bytes([0xB0 | rng.randrange(4)]).hex())
        s.add("bytes", f"{packet}+8", u32(flags).hex())
        s.add("set32", f"{packet}+0x10", {"a": "tex_object_a", "b": "tex_object_b", "none": "0"}[texture])
        s.add("set32", f"{packet}+0x14", count)
        for k in range(min(count, 8)):
            vertex = f32(rng.uniform(-50, 700)) + f32(rng.uniform(-50, 500)) + f32(rng.choice([0.25, 0.5, 0.9])) + f32(rng.choice([0.01, 0.1, 0.5, 1.0]))
            vertex += bytes(rng.randrange(256) for _ in range(4))
            if texture != "none":
                vertex += f32(rng.uniform(0, 1)) + f32(rng.uniform(0, 1))
            s.add("bytes", f"{packet}+{0x18 + k * stride}", vertex.hex())
        self.reset(label)
        self.run(label, packet, 0, HANDLERS[0xB0] if direct else None)

    def state_packets(self, label: str) -> None:
        """0xE1 (texture page and window) and 0xE3 (the viewport)."""
        s, rng = self.s, self.rng
        for texture in ("a", "none"):
            packet = f"{label}_e1{texture}_p"
            s.add("alloc", packet, 0x20)
            s.add("pattern", f"{packet}+4", 0x1C, rng.randrange(1 << 20))
            s.add("set32", packet, 0)
            s.add("bytes", f"{packet}+7", "e1")
            s.add("set32", f"{packet}+0xc", "tex_object_a" if texture == "a" else "0")
            self.reset(f"{label}_e1{texture}")
            self.run(f"{label}_e1{texture}", packet, 0x20 if texture == "none" else 0)
        for index, (x, top, w, h) in enumerate(((0, 0, 512, 240), (64, 300, 256, 120), (-8, 20, 600, 300), (300, 100, 400, 100))):
            packet = f"{label}_e3_{index}_p"
            s.add("alloc", packet, 0x20)
            s.add("pattern", f"{packet}+4", 0x1C, rng.randrange(1 << 20))
            s.add("set32", packet, 0)
            s.add("bytes", f"{packet}+8", (s16(x) + s16(top) + s16(w) + s16(h)).hex())
            s.add("bytes", f"{packet}+7", "e3")
            self.reset(f"{label}_e3_{index}")
            self.run(f"{label}_e3_{index}", packet, 0x20)

    def unknown(self, label: str) -> None:
        s = self.s
        for command in (0x80, 0xE2, 0x04, 0x64):
            packet = f"{label}_{command:x}_p"
            s.add("alloc", packet, 0x20)
            s.add("pattern", f"{packet}+4", 0x1C, command)
            s.add("set32", packet, 0)
            s.add("bytes", f"{packet}+7", bytes([command]).hex())
            self.reset(f"{label}_{command:x}")
            self.run(f"{label}_{command:x}", packet, 0x20)

    def ordering_table(self) -> None:
        s = self.s
        s.comment("Db_ClearOrderingTable: entry k points at entry k - 1, entry 0 at nothing, the rest of the block is untouched")
        for index, count in enumerate((0, 1, 2, 7)):
            table = f"ot{index}"
            s.add("alloc", table, count * 8 + 32)
            s.add("pattern", table, count * 8 + 32, 30 + index)
            s.add("call", hex(CLEAR_TABLE), "c", table, count)
            for k in range(count):
                s.add("same", f"{table}_link{k}", f"[{table}+{8 * k}]", f"{table}+{8 * k}-8" if k else "0")
                s.add("val", f"{table}_tag{k}", f"[{table}+{8 * k + 4}]")
            s.add("mem", f"{table}_tail", f"{table}+{count * 8}", 32)

    def multi_packet_list(self) -> None:
        """Several packets in one list: the walk follows the links, skips a packet whose second word is zero, and each handler sees the state the one before left."""
        s, rng = self.s, self.rng
        s.comment("a list of five packets")
        names = []
        for k, kind in enumerate((0x2C, 0x30, 0x60, 0x24, 0x38)):
            packet = f"list_p{k}"
            s.add("alloc", packet, 0x40)
            s.add("pattern", f"{packet}+4", 0x3C, 700 + k)
            count, z_at, texture_at, xy, _rgb, _uv = LAYOUTS.get(kind, (0, 0x10, 0, (), (), ()))
            s.add("bytes", f"{packet}+7", bytes([kind | (k & 2)]).hex())
            for (x, y), at in zip(self.coordinates("narrow", count), xy):
                s.add("bytes", f"{packet}+{at}", (s16(x) + s16(y)).hex())
            if kind == 0x60:
                s.add("bytes", f"{packet}+8", (s16(10) + s16(20) + s16(30) + s16(40)).hex())
            s.add("bytes", f"{packet}+{z_at}", f32(1.0).hex())
            if texture_at:
                tpage_at = 0x1A if kind & 0x10 else 0x16
                s.add("bytes", f"{packet}+{tpage_at}", (rng.randrange(1 << 16)).to_bytes(2, "little").hex())
                s.add("set32", f"{packet}+{texture_at}", "tex_object_b" if k % 2 else "tex_object_a")
            names.append(packet)
        for k, packet in enumerate(names):
            s.add("set32", packet, names[k + 1] if k + 1 < len(names) else "0")
        s.add("set32", "list_p3+4", 0)                      # the walk skips a packet whose second word is zero (the command byte lives in it, so this one is dead)
        self.reset("list")
        self.run("list", names[0], 0)


def build_script() -> str:
    d = Draw()
    s = d.s
    s.comment("function-level oracle of the draw path: tony/draw_oracle.py")
    s.device()
    d.textures()
    s.comment("D3DPoly_StartScene: the scene begins (or does not, when one is running)")
    # what the fog threshold and the clear colour are made from, and the counter, are the process's: set them, so both processes start the scene from the same words
    s.add("alloc", "viewport_edges", 0x20)
    s.add("set16", "viewport_edges+0xa", 4096)
    s.add("set16", "viewport_edges+0xe", 8192)
    s.add("set32", 0x00563A38, "viewport_edges")
    s.add("set32", 0x00563A44, 3000)                         # DpqMin
    s.add("set32", 0x005485A0, 0x44FA0000)                  # the video's fog yon scale, 2000.0
    s.add("set32", 0x006A6964, 0)                           # texture mode
    s.add("set32", SCENE_COUNT, 0)
    s.add("set8", SCENE_ACTIVE, 0)
    s.add("call", hex(START_SCENE), "c", 0x00808080, 0x00102030)
    s.add("mem", "scene_after", SCENE_ACTIVE, 2)
    s.add("mem", "scene_count", SCENE_COUNT, 4)
    s.add("mem", "scene_fog", FOG_YON, 8)
    s.add("mem", "scene_caches", CACHES, 0x1C)
    s.add("mem", "scene_flags", STAGES_SET, 1)
    s.add("mem", "scene_word", BOUND_TEXTURE, 4)
    s.add("call", hex(START_SCENE), "c", 0x00404040, 0x00FFFFFF)      # a second call in the same scene does nothing
    s.add("mem", "scene_again", SCENE_ACTIVE, 2)
    s.add("mem", "scene_again_fog", FOG_YON, 8)
    d.ordering_table()
    s.comment("D3D_DrawPolygonList: the PSX polygons, lines and tiles, one packet each")
    for kind in LAYOUTS:
        textured = LAYOUTS[kind][2] != 0
        for variant in range(4 if textured else 3):
            semi = d.rng.randrange(2)
            raw = d.rng.randrange(2)
            texture = ["a", "b", "none", "a"][variant] if textured else "none"
            flavour = "wide" if variant == 2 and not textured or variant == 3 and textured and d.rng.random() < 0.5 else "narrow"
            d.psx(f"k{kind:02x}_{variant}", kind, semi=semi, raw=raw, flavour=flavour, texture=texture, negative_z=variant == 1)
    for kind in TILES:
        for variant in range(3):
            d.tile(f"k{kind:02x}_{variant}", kind, semi=d.rng.randrange(2), flavour="wide" if variant == 2 else "narrow", negative_z=variant == 1)
    s.comment("D3D_DrawGeneralPolygon")
    for index in range(30):
        flags = d.rng.getrandbits(32) & ~0x30000000 if index % 5 else d.rng.getrandbits(32) | 0x30000000       # (the outline passes are not drawn by the Kit)
        d.general(f"gp{index}", flags=flags, texture=d.rng.choice(["a", "b", "none"]), count=d.rng.choice([3, 4, 5, 8]))
    d.general("gp_count0", flags=0x40, texture="a", count=0)
    # (a count above 64 is not here: the Kit refuses it to protect its own vertex array; retail's handler does not look, and a packet like that never occurs)
    s.comment("the D3D_Draw* handlers called on their own, with the state a walk leaves (the natives of milestone 8c)")
    for kind in LAYOUTS:
        textured = LAYOUTS[kind][2] != 0
        for variant in range(3):
            d.psx(f"h{kind:02x}_{variant}", kind, semi=d.rng.randrange(2), raw=d.rng.randrange(2), flavour="wide" if variant == 2 else "narrow",
                  texture=["a", "b", "none"][variant] if textured else "none", negative_z=variant == 1, direct=True)
    for variant in range(2):       # (the handlers of the three sized rectangles, 0x4d6090, 0x4d60c0 and 0x4d60f0, are reached only through the walk, which the Kit replaces)
        d.tile(f"h60_{variant}", 0x60, semi=d.rng.randrange(2), flavour="wide" if variant else "narrow", negative_z=variant == 1, direct=True)
    for index in range(8):
        d.general(f"hg{index}", flags=d.rng.getrandbits(32) & ~0x30000000, texture=d.rng.choice(["a", "b", "none"]), count=d.rng.choice([3, 4, 6]), direct=True)
    d.state_packets("st")
    d.unknown("un")
    d.multi_packet_list()
    s.comment("D3D_PresentFrame ends the scene; D3D_DrawPolygonList with no scene running draws nothing")
    s.add("call", hex(PRESENT), "c")
    s.add("mem", "ended_scene", SCENE_ACTIVE, 2)
    d.psx("after", 0x2C, semi=0, raw=0, flavour="narrow", texture="a", negative_z=False)
    return "\n".join(s.lines) + "\n"


def oracle_command(_args=None) -> int:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = OUT_ROOT / "draw.oracle"
    script.write_text(build_script())
    print(f"script: {script} ({len(script.read_text().splitlines())} lines)")
    standalone = base.run_standalone(script, OUT_ROOT / "standalone.out")
    retail = base.run_retail(script, label="draw-oracle")
    (OUT_ROOT / "retail.out").write_text(retail)
    problems = base.compare(retail, standalone)
    for problem in problems[:60]:
        print(problem)
    print(f"{len(retail.splitlines())} lines compared, {len(problems)} differ")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(oracle_command())
