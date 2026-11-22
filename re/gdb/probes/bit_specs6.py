"""Phase 4.15d probe: Bit_Display (group bdp: 0x00407310), one list of bits at a time.

The function writes primitives into the renderer's polygon arena and links them into the buckets of the renderer's depth table; the probe lays a renderer record, a viewport record, an arena and a
bucket table (4096 entries of 8 bytes) out in the slab and points the engine's globals at them. A case fills one list (the others are empty) with fake bits, a view (matrix, camera position) and the
GTE registers the projection reads, and runs the function under the render window's 24-bit x87 precision (the cases are captured through a trampoline that loads the control word).

Slab layout (offsets): fake vtable 0x0000, bits 0x0100 (24 of 0x100), arrays 0x2000 (0x2000), arena 0x4000 (0x2000), bucket table 0x8000 (0x8000), viewport 0x10000 (0x20), renderer 0x10100 (0x100).
"""

from __future__ import annotations

import struct

from bit_specs import BIT_STRIDE, FAKE_VTABLE, LISTS, bit, merged, noise, word
from bit_specs2 import scattered, vector
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from math_specs import CW_PC24, trampoline

GROUPS = ("bdp",)

BITS6 = 0x0100      # (slot 0 of the slab is the fake vtable)
ARRAYS = 0x2000
ARENA6 = 0x4000
ARENA6_BYTES = 0x2000
TABLE = 0x8000
VIEWPORT = 0x10000
RENDERER = 0x10100
SLAB6 = 0x10400

WORLD = [(0x0055F6C0, 0x74), (0x0055F7C8, 0x148), (0x0055FA3C, 4), (0x00560FD0, 8), (0x00563A38, 4), (0x00568628, 4), (0x005620E0, 4), (0x029DA394, 0x10), (0x006A3E00, 0x120),
         (0x006A6D6C, 0x30)]
STATE = {"project": [0], "corner_a": [0], "corner_b": [0], "at_project": 0, "at_corner_a": 0, "at_corner_b": 0}


OFF = 0x2000      # (the 24-bit trampoline lives at slab + 0x1000: the captured window starts past it)


def base(env):
    return env.slab + OFF


def regions(slab):
    return merged([(slab + OFF, SLAB6)] + WORLD, gap=0)


def seed(env, rng):
    return {base(env) + FAKE_VTABLE: struct.pack("<4I", 0x004AD950, 0x004AD9A0, 0x004AD9A0, 0x004AD9A0)}


for _group in GROUPS:
    SLAB_BYTES[_group] = OFF + SLAB6 + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = [(0x00518484, 4)]
    SEEDS[_group] = seed


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_bit6_installed", False):
        return
    fc._bit6_installed = True
    mark_under_test()
    ensure(fc, 0x004AD9A0, 0, "engine_004ad9a0", convention="thiscall", this=True)                    # the special display's method (slot 2 of the fake vtable)
    ensure(fc, 0x004F3F30, 2, "engine_004f3f30", result=lambda a: pop("project", None),
           writes=[(0x006A6D84, 4, lambda a: pop("corner_a", None)), (0x006A6D88, 4, lambda a: pop("corner_b", None))])      # Camera_ProjectBoxCorners(box, camera)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def dp(address, name):
    return bit(address, name, "bdp", None)


def bit_at(env, index):
    return base(env) + BITS6 + BIT_STRIDE * index


def put_bit(img, env, rng, index, size=0x70):
    at = bit_at(env, index)
    img.put(at, noise(rng, size))
    img.u32(at, base(env) + FAKE_VTABLE)
    return at


def link(img, env, rng, list_name, count, size=0x70, first=0):
    """`count` bits (slots first..) linked into the list; the head is written."""
    addresses = [put_bit(img, env, rng, first + k, size) for k in range(count)]
    for k, at in enumerate(addresses):
        img.u32(at + 4, addresses[k - 1] if k > 0 else 0)
        img.u32(at + 8, addresses[k + 1] if k + 1 < count else 0)
    img.u32(LISTS[list_name], addresses[0] if addresses else 0)
    return addresses


def world(img, env, rng, depth_range=(1, 20000)):
    """The renderer, viewport, arena and view of a case; returns the camera position."""
    slab = base(env)
    for name in LISTS:
        img.u32(LISTS[name], 0)
    img.u32(0x0055F6D0, word(rng))
    img.u32(0x0055F6D4, word(rng))
    # the viewport (edges) record
    img.put(slab + VIEWPORT, noise(rng, 0x20))
    img.u16(slab + VIEWPORT + 8, rng.choice([1, 10, 0x100, 0x200, 100]))
    img.u16(slab + VIEWPORT + 0xA, rng.choice([0x4000, 0x4E20, 0x2000, 0x7FFF, 20000]))
    img.u16(slab + VIEWPORT + 0xE, rng.choice([0x100, 0x200, 0x140, 0x400, 0x1000]))
    img.u16(slab + VIEWPORT + 0x10, rng.choice([0, 160, 320, 0xFF00, rng.getrandbits(16)]))
    img.u16(slab + VIEWPORT + 0x12, rng.choice([0, 120, 240, 0xFF00, rng.getrandbits(16)]))
    img.u32(0x00563A38, slab + VIEWPORT)
    # the renderer record: the bucket table in use and the first one (equal: no winding value)
    renderer = slab + RENDERER
    img.put(renderer, noise(rng, 0x100))
    table = slab + TABLE
    img.u32(renderer + 0x8C, table)
    img.u32(renderer + 0x84, table if rng.random() < 0.25 else 0x12345678)
    img.u32(0x00560FD4, renderer)
    # the buckets start with arbitrary links
    for k in range(0, 0x1000, 0x40):
        img.u32(table + 8 * k, rng.choice([0, slab + ARENA6, rng.getrandbits(32)]))
    # the arena
    cursor = slab + ARENA6 + 4 * rng.randrange(0, 8)
    end = rng.choice([slab + ARENA6 + ARENA6_BYTES, slab + ARENA6 + ARENA6_BYTES, cursor + rng.choice([0, 0x10, 0x20, 0x40, 0x100])])
    img.u32(0x00560FD0, cursor)
    img.u32(0x00568628, end)
    img.u32(0x005620E0, word(rng))
    img.u32(0x029DA394, rng.choice([640, 320, 512, 800, 100]))
    img.u32(0x029DA398, rng.choice([480, 240, 384, 600, 100]))
    img.u32(0x029DA3A0, rng.choice([0, 0, 1, 1, 1]))
    # the view: matrix and camera position (the current view index is 0 or 1)
    view = rng.choice([0, 0, 1])
    img.u32(0x0055FA3C, view)
    view_at = 0x0055F7C8 + 0xA4 * view
    img.put(view_at, noise(rng, 0xA4))
    if rng.random() < 0.88:
        diagonal = [0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000]
        entries = [d if rng.random() < 0.97 else rng.randrange(-0x1000, 0x1001) for d in diagonal]
    else:
        entries = [rng.randrange(-0x1000, 0x1001) for _ in range(9)]
    for k, v in enumerate(entries):
        img.u16(view_at + 0x74 + 2 * k, v & 0xFFFF)
    camera = [rng.randrange(-0x400, 0x400) for _ in range(3)]
    for k, v in enumerate(camera):
        img.u32(view_at + 4 + 4 * k, v & 0xFFFFFFFF)
    # the GTE: the projection constants
    img.put(0x006A3E00, noise(rng, 0x120))
    img.u32(0x006A3E00, rng.choice([0x100, 0x100, 0x200, 0x200, 0x400, 0x1000, 0x80]))
    img.u32(0x006A3EE8, rng.randrange(-0x10000, 0x10000) & 0xFFFFFFFF)
    img.u32(0x006A3EEC, rng.randrange(-0x10000, 0x10000) & 0xFFFFFFFF)
    img.u32(0x006A3EF0, rng.choice([0, 0x155, 0xFFFFFEAB]))
    img.u32(0x006A3EF4, rng.choice([0, 0x155, 0x1000000]))
    img.put(0x006A6D6C, noise(rng, 0x30))
    return camera


def position(img, rng, at, camera, depth_range=(1, 20000), spread=0x800):
    """A Q12 world position near the camera, at a depth the identity matrix leaves as z."""
    spread = min(spread, rng.choice([0x10, 0x30, 0x30, 0x80, 0x80, 0x100, 0x200, 0x800]))      # (a point far off the axis clips out of the screen: most cases stay near it)
    x = camera[0] + rng.randrange(-spread, spread)
    y = camera[1] + rng.randrange(-spread, spread)
    z = camera[2] + rng.randrange(*depth_range)
    for k, v in enumerate((x, y, z)):
        img.u32(at + 4 * k, (v << 12) & 0xFFFFFFFF)


# ---- the glows ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def make_glow(img, env, rng, camera, index, sections=None, fringes=None):
    sections = rng.choice([2, 4, 6, 8, 3]) if sections is None else sections
    fringes = rng.choice([0, 1, 2, 3]) if fringes is None else fringes
    at = put_bit(img, env, rng, index, 0xA8)
    position(img, rng, at + 0x10, camera, rng.choice([(1, 800), (1, 800), (500, 5000), (1, 20000), (-50, 20500)]))
    array = base(env) + ARRAYS + 0x400 * index
    img.put(array, noise(rng, (fringes + 1) * sections * 8))
    for k in range(sections):
        img.u32(array + 8 * k, rng.choice([rng.randrange(0, 0x2000), rng.randrange(0, 0x40000), 0x1000]))
    for f in range(fringes):
        for k in range(sections):
            img.u32(array + sections * 8 * (f + 1) + 8 * k, rng.choice([rng.randrange(0, 0x1000), rng.randrange(-0x400, 0x400) & 0xFFFFFFFF, 0]))
    img.u32(at + 0x3C, array)
    img.u32(at + 0x40, array + sections * 8)
    img.u32(at + 0x44, sections)
    img.u32(at + 0x48, fringes)
    img.u16(at + 0x50, rng.choice([0x1000 // max(1, sections), rng.getrandbits(16), 0x200, 0xFE00]))
    img.u16(at + 0x54, rng.choice([0, 0x400, rng.getrandbits(16)]))
    img.u8(at + 0x52, rng.choice([0, 0, 0, 1]))
    img.u32(at + 0x58, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0x55555555, rng.getrandbits(32), 0]))
    return at


def pop(name, rng):
    values = STATE[name]
    value = values[STATE["at_" + name] % len(values)]
    STATE["at_" + name] += 1
    return value


def script_box(rng, count):
    # (the answer is the depth bucket the line is linked into: an index past the table reads memory the probe does not capture)
    STATE["project"] = [rng.choice([-1, -1, 0, 0, 1, 7, 100, 0xFFF, rng.randrange(0, 0x1000)]) & 0xFFFFFFFF for _ in range(count + 1)]
    STATE["corner_a"] = [rng.getrandbits(32) for _ in range(count + 1)]
    STATE["corner_b"] = [rng.getrandbits(32) for _ in range(count + 1)]
    for name in ("project", "corner_a", "corner_b"):
        STATE["at_" + name] = 0


def array_at(env, index):
    return base(env) + ARRAYS + 0x400 * index


def make_polyline(img, env, rng, camera, index, gouraud):
    at = put_bit(img, env, rng, index, 0x60)
    count = rng.choice([0, 1, 2, 3, 5])
    array = array_at(env, index)
    img.put(array, noise(rng, 16 * 8))
    for k in range(count):
        for j in range(3):
            img.u32(array + 16 * k + 4 * j, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF)
        img.u32(array + 16 * k + 12, rng.choice([0, 0x40FFFFFF, 0x42FFFFFF, 0x02000000, 0x50808080, rng.getrandbits(32), 0x52010203]))
    if gouraud:
        img.u32(at + 0x3C, count)
        img.u32(at + 0x40, array)
        for j in range(3):
            img.u32(at + 0x44 + 4 * j, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF)
        img.u32(at + 0x50, rng.choice([0, 0x50FFFFFF, 0x52FFFFFF, 0x02000000, rng.getrandbits(32)]))
        img.u8(at + 0x54, rng.choice([0, 0, 5, 0xFF, rng.getrandbits(8)]))
    else:
        img.u32(at + 0x40, count)
        img.u32(at + 0x44, array)
        for j in range(3):
            img.u32(at + 0x48 + 4 * j, rng.randrange(-0x100000, 0x100000) & 0xFFFFFFFF)
        img.u8(at + 0x3C, rng.choice([0, 0, 5, 0xFF, rng.getrandbits(8)]))
    script_box(rng, count)
    return at


def make_glass(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x70)
    for corner in range(3):
        position(img, rng, at + 0x3C + 12 * corner, camera, (1, 3000), 0x400)
    return at


def make_gline(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x70)
    position(img, rng, at + 0x44, camera, rng.choice([(1, 800), (-50, 20500), (500, 5000)]), 0x400)
    position(img, rng, at + 0x50, camera, rng.choice([(1, 800), (-50, 20500), (500, 5000)]), 0x400)
    return at


def make_quad(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x70)
    depth = rng.choice([(1, 800), (1, 800), (-50, 20500), (500, 5000)])
    for offset in (0x10, 0x3C, 0x48, 0x54):
        position(img, rng, at + offset, camera, depth, 0x400)
    texture = array_at(env, index)
    img.put(texture, noise(rng, 0x10))
    img.u32(at + 0x64, texture if rng.random() < 0.95 else 0)
    img.u8(at + 0x68, rng.choice([0, 4, 0xFF, rng.getrandbits(8)]))
    img.u16(at + 0x38, rng.choice([0, 1, 100, 0xFFFF, rng.getrandbits(16)]))
    return at


def make_chunk(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0xC0)
    depth = rng.choice([(1, 800), (1, 800), (-50, 20500), (500, 5000)])
    spread = rng.choice([0x30, 0x80, 0x100, 0x400])
    centre = [camera[0] + rng.randrange(-spread, spread), camera[1] + rng.randrange(-spread, spread), camera[2] + rng.randrange(*depth)]
    for offset in (0x5C, 0x68, 0x74, 0x80):     # (the four corners near one another: all of them have to be on the screen for the faces to be drawn)
        x = centre[0] + rng.randrange(-spread, spread)
        y = centre[1] + rng.randrange(-spread, spread)
        z = centre[2] + rng.randrange(-spread, spread)
        for j, v in enumerate((x, y, z)):
            img.u32(at + offset + 4 * j, v & 0xFFFFFFFF)
    img.u32(at + 0xB0, rng.choice([0, 0, 0x12345678, rng.getrandbits(32)]))
    img.u16(at + 0x38, rng.choice([0, 1, 100, 0xFFFF, rng.getrandbits(16)]))
    return at


def make_pixel(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x50)
    position(img, rng, at + 0x10, camera, rng.choice([(1, 800), (-50, 20500), (500, 5000)]), 0x400)
    img.u32(at + 0x44, rng.choice([1, 0x61, 0x60, 0x68, 0x6A, rng.getrandbits(32)]))
    img.u16(at + 0x38, rng.choice([0, 1, 100, 0xFFFF, rng.getrandbits(16)]))
    return at


def frame_entry(img, env, rng, index):
    """A frame entry (offset x, offset y, width, height, pointer to a texture record of three dwords and a short) in the slab."""
    entry = array_at(env, index) + 0x200
    record = entry + 0x20
    img.put(entry, noise(rng, 8))
    for k in range(4):
        img.u8(entry + k, rng.choice([0, 1, 8, 0x10, 0x20, 0x40, 0x7F, 0x80, 0xFF, rng.getrandbits(8)]))
    img.u32(entry + 4, record)
    img.put(record, noise(rng, 0x10))
    # (a colour byte of the record is lowered by the bit: keep the bytes the asserts and decrements read nonzero most of the time)
    img.u8(record + 1, rng.choice([1, 2, 0x80, rng.randrange(1, 256)]))
    return entry


def make_flat(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x70)
    position(img, rng, at + 0x10, camera, rng.choice([(1, 800), (1, 800), (-50, 20500), (200, 4000)]), 0x400)
    img.u32(at + 0x48, frame_entry(img, env, rng, index))
    img.u8(at + 0x4C, rng.choice([0, 1, 8, 9, rng.getrandbits(8)]))
    img.u16(at + 0x52, rng.choice([0x190, 400, 0x80, 0x1000, 0x7FFF, 0x8000, rng.getrandbits(16)]))
    img.u16(at + 0x54, rng.choice([0, 0, 0, 0x400, 0x800, 0xFC00, rng.getrandbits(16)]))
    img.u32(at + 0x5C, rng.choice([0x10001000, 0x08000800, rng.getrandbits(32), 0x20001000]))
    img.u8(at + 0x60, rng.choice([0, 0, 0, 5, 0xFF, rng.getrandbits(8)]))
    img.u8(at + 0x61, rng.getrandbits(8))
    img.u16(at + 0x62, rng.choice([0, 0, 0x1234, rng.getrandbits(16)]))
    img.u16(at + 0x38, rng.choice([0, 1, 100, 0xFFFF, rng.getrandbits(16)]))
    img.u32(at + 0x40, rng.getrandbits(32))
    return at


def make_linked(img, env, rng, camera, index):
    at = put_bit(img, env, rng, index, 0x70)
    depth = rng.choice([(1, 800), (1, 800), (-50, 20500), (200, 4000)])
    position(img, rng, at + 0x54, camera, depth, 0x400)
    position(img, rng, at + 0x60, camera, depth, 0x400)
    img.u32(at + 0x48, frame_entry(img, env, rng, index))
    img.u8(at + 0x4C, rng.choice([0x10, 0x10, 0, 0, 0x20, 0x30, 1, 2, 3, 0x11, 0x12, 0x13, rng.getrandbits(8)]))
    img.u16(at + 0x52, rng.choice([0x190, 400, 0x80, 0x1000, 0x7FFF, 0x8000, rng.getrandbits(16)]))
    img.u16(at + 0x38, rng.choice([0, 1, 100, 0xFFFF, rng.getrandbits(16)]))
    img.u32(at + 0x40, rng.getrandbits(32))
    return at


MAKERS = {
    "glow": ("glow", lambda img, env, rng, camera, k: make_glow(img, env, rng, camera, k)),
    "polyline": ("polyline", lambda img, env, rng, camera, k: make_polyline(img, env, rng, camera, k, False)),
    "gpolyline": ("gpolyline", lambda img, env, rng, camera, k: make_polyline(img, env, rng, camera, k, True)),
    "glass": ("glass", make_glass),
    "gline": ("gline", make_gline),
    "quad": ("quad", make_quad),
    "chunk": ("chunk", make_chunk),
    "pixel": ("pixel", make_pixel),
    "flat": ("flat", make_flat),
    "linked": ("linked", make_linked),
}


MAKERS["special"] = ("special", lambda img, env, rng, camera, k: put_bit(img, env, rng, k, 0x70))
KINDS = list(MAKERS) + ["glow", "glow", "linked", "flat", "pixel"]


@dp(0x00407310, "Bit_Display")
def gen_display(env, n):
    img, rng = Image(), env.rng
    camera = world(img, env, rng)
    # (a fifth of the cases fill several lists at once, to see the order the lists are drawn in and the arena they share)
    kinds = [rng.choice(KINDS)] if rng.random() < 0.8 else rng.sample(list(MAKERS), rng.choice([2, 3]))
    slot = 0
    lines = False
    for kind in kinds:
        name, make = MAKERS[kind]
        count = rng.choice([0, 1, 1, 2, 3])
        if kind in ("polyline", "gpolyline"):
            count = rng.choice([0, 1, 1, 2])
            lines = True
        count = min(count, 8 - slot)
        addresses = [make(img, env, rng, camera, slot + k) for k in range(count)]
        slot += count
        for k, at in enumerate(addresses):
            img.u32(at + 4, addresses[k - 1] if k > 0 else 0)
            img.u32(at + 8, addresses[k + 1] if k + 1 < count else 0)
        img.u32(LISTS[name], addresses[0] if addresses else 0)
    # (the box projector is asked once per segment of every line: the scripted answers cycle)
    script_box(rng, 12 if lines else 0)
    return [], img.pokes()


# every case runs under the render window's precision
DISPLAY = [s for s in __import__("create_specs").SPECS if s.address == 0x00407310 and s.group == "bdp"]
for _spec in DISPLAY:
    _spec.via = lambda env: trampoline(env, 0x00407310, CW_PC24)
