"""Phase 4.15e probe: the start of a view (group rview: M3D_BeginRenderView 0x0045e8e0, M3d_RenderSetupCamera 0x0045ef80) and the sky / background objects (group rbgd: M3d_RenderBackground 0x00460ca0).

The slab is the one of render_specs5: view record 0x0100 (0x90), viewport edges 0x01a0 (0x20), bucket words 0x3f00, primitives 0x3000, objects 0x0200 (0x80 each), matrix 0x0700, region tables 0x0800.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from render_specs import BUFFER_A, BUFFER_B, SCRATCH, noise, word
from render_specs5 import REGION_RECORDS, REGION_STRIDE, STATIC, regions, seed, install_stubs as install_stubs5

GROUPS = ("rview", "rbgd")

for _group in GROUPS:
    SLAB_BYTES[_group] = 0xD000
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(STATIC)
    SEEDS[_group] = seed


def install_stubs(fc, env):
    install_stubs5(fc, env)
    if getattr(fc, "_render8_installed", False):
        return
    fc._render8_installed = True
    ensure(fc, 0x00462170, 1, "engine_00462170")
    ensure(fc, 0x00461CF0, 1, "engine_00461cf0")


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def view_record(img, rng, view):
    """A view record: the matrices at +0x34 / +0x54 / +0x74 and a position (small values keep the planes' lengths away from zero)."""
    img.put(view, noise(rng, 0x90))
    for axis in range(3):
        img.u32(view + 4 + 4 * axis, rng.randrange(-300, 300) & 0xFFFFFFFF)


def edges_record(img, rng, edges):
    left = rng.choice([640, 320, 800, rng.randrange(100, 1024)])
    right = rng.choice([0, 0, 16, rng.randrange(0, 100)])
    bottom = rng.choice([480, 240, 600, rng.randrange(100, 768)])
    top = rng.choice([0, 0, 8, rng.randrange(0, 100)])
    img.put(edges, noise(rng, 0x20))
    img.u16(edges, left)
    img.u16(edges + 2, bottom)
    img.u16(edges + 4, right)
    img.u16(edges + 6, top)
    img.u16(edges + 8, rng.randrange(0, 0x1000))
    img.u16(edges + 0xC, rng.choice([320, 256, 400, rng.randrange(100, 800)]))


def projection_state(img, rng):
    img.u32(0x00563A6C, rng.choice([0x1000, 0x0F00, 0x1200, rng.randrange(0x800, 0x2000)]))
    img.u32(0x00563A70, rng.choice([0x1000, 0x0F00, 0x1200, rng.randrange(0x800, 0x2000)]))
    img.u32(0x00563A3C, rng.choice([0x100, 0x400, 0x1000, rng.randrange(0, 0x2000)]))
    img.put(0x006A3E00, noise(rng, 0x120))


@spec(0x0045E8E0, "M3D_BeginRenderView", "rview", this=None)
def gen_begin_view(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    view, edges, buckets = slab + 0x100, slab + 0x1A0, rng.choice([slab + 0x3F00, slab + 0x3F10])
    view_record(img, rng, view)
    edges_record(img, rng, edges)
    projection_state(img, rng)
    img.put(0x00563A90, noise(rng, 0x50))
    img.put(0x005620A0, noise(rng, 0x40))
    img.u16(0x005620A0, rng.choice([512, 640, 320, rng.randrange(1, 1024)]))
    img.u16(0x005620B8, rng.choice([240, 480, 400, rng.randrange(1, 800)]))
    img.u32(0x00563AF0, rng.choice([0, 100, rng.getrandbits(32)]))
    img.u32(0x0056E320, rng.choice([0, 100, 100, rng.getrandbits(32)]))
    img.u32(0x00564370, rng.getrandbits(32))
    img.u32(0x00564374, rng.getrandbits(32))
    img.u32(0x029DA3A0, rng.choice([0, 0, 1]))
    img.u32(0x029DA394, rng.choice([0x200, 0x2AA, 0x400, rng.randrange(0x100, 0x500)]))
    img.u32(0x029DA398, rng.choice([0xF0, 0x1E0, 0x168, rng.randrange(0x80, 0x400)]))
    img.u32(0x00560FD4, rng.choice([BUFFER_A, BUFFER_B]))
    img.u32(0x00560FD0, slab + 0x3000 + 4 * rng.randrange(0, 8))
    for base in (BUFFER_A, BUFFER_B):
        img.u32(base + 0x84, slab + 0x3F00)
    img.put(slab + 0x3F00, noise(rng, 0x20))
    img.u32(0x00537C80, rng.randrange(0, 20))
    img.u32(0x00537C84, rng.randrange(0, 20))
    img.put(0x0056DCA8, noise(rng, 0x10))
    img.u32(0x00533F90, word(rng))
    return [view, edges, buckets], img.pokes()


@spec(0x0045EF80, "M3d_RenderSetupCamera", "rview", this=None)
def gen_setup_camera(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    view, edges = slab + 0x100, slab + 0x1A0
    view_record(img, rng, view)
    edges_record(img, rng, edges)
    projection_state(img, rng)
    img.u32(0x00563A38, edges)
    img.put(0x005620A0, noise(rng, 0x40))
    return [view], img.pokes()


@spec(0x00460CA0, "M3d_RenderBackground", "rbgd", this=None)
def gen_background(env, n):
    img, rng = Image(), env.rng
    slab = env.slab
    view = slab + 0x100
    view_record(img, rng, view)
    img.put(0x005620A0, noise(rng, 0x60))
    img.u32(0x005620E0, view)
    img.put(0x00563A00, noise(rng, 0x140))
    img.put(0x00564340, noise(rng, 0x48))
    img.put(0x006A3E00, noise(rng, 0x120))
    img.u32(0x0056437C, rng.choice([0, 0, 1]))
    img.u32(0x00564380, rng.choice([0, 1, 5]))
    table, models = slab + 0x800, slab + 0x840
    for k in range(3):
        img.u32(table + 4 * k, models + 0x40 * k)
    for region in range(3):
        record = REGION_RECORDS + REGION_STRIDE * region
        img.put(record, noise(rng, REGION_STRIDE))
        img.u8(record + 0xA, 1 if rng.random() < 0.9 else 0)
        img.u32(record + 0x14, table)
        img.u32(record + 0x28, word(rng))
    objects = [slab + 0x200 + 0x80 * k for k in range(rng.choice([1, 2, 3]))]
    for k, at in enumerate(objects):
        img.put(at, noise(rng, 0x80))
        img.u32(at + 0x20, objects[k + 1] if k + 1 < len(objects) else 0)
        img.u16(at + 4, rng.choice([0, 0, 0, 1, 0x8000, rng.getrandbits(16)]))
        img.u8(at + 5, rng.choice([0, 0, 2, 2, rng.getrandbits(8)]) & 0x7F if rng.random() < 0.9 else 0xFF)
        for angle in (0x14, 0x16, 0x18):
            img.u16(at + angle, 0 if rng.random() < 0.5 else rng.getrandbits(16))
        img.u16(at + 0x1A, rng.randrange(0, 3))
        img.u8(at + 0x1F, rng.randrange(0, 3))
        for axis in range(3):
            img.u16(at + 0x28 + 2 * axis, rng.choice([0x1000, 0x800, 0x2000, rng.getrandbits(16)]))
    img.u16(0x00563A44, rng.getrandbits(16))
    return [objects[0] if rng.random() < 0.95 else 0], img.pokes()
