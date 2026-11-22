"""Phase 4.15c probe: the trigger-record writers (group wcm): the small class family whose objects write one record of a `_TRG` file (0x004c87c0..0x004c8f30).

A writer object (0x40 bytes), the record a link points at (0x20 bytes) and the buffer a writer fills (0x90 bytes, at an address that is odd, even and aligned in turn) sit in the slab; the editor's grid
scale and size are globals. The formatter (`sprintf`, called from two sites of the node writer) writes eight bytes of text.

Slab layout (offsets): writer 0x000, record 0x100, buffer 0x200.
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image
from world_specs import word

GROUPS = ("wcm",)

WRITER = 0x000
RECORD = 0x100
BUFFER = 0x200
SLAB = 0x2C0

GRID = [(0x00528398, 8), (0x00561488, 8), (0x005614C8, 4), (0x0056A920, 4), (0x0056A970, 4), (0x0056E230, 0x24)]
GATES = [(0x005286F0, 0x80)]


def wcm_regions(slab):
    return merged([(slab, SLAB)] + GRID, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = wcm_regions
    READ_ONLY[_group] = GATES


def wcm(address, name, this=None):
    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "wcm", this=resolve)


def install_stubs(fc, env):
    from platform_specs import mark_under_test, stop_clock

    if getattr(fc, "_wcm_installed", False):
        return
    fc._wcm_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng

    def text(args):
        return bytes(rng.randrange(0x30, 0x7B) for _ in range(7)) + b"\0"

    # the formatter's two call sites in the node writer (three arguments, then two); it writes eight bytes at its destination
    fc.stub_site(0x004C89AB, 5, 3, "engine_005002a1", fill=(0, text))
    fc.stub_site(0x004C89C7, 5, 2, "engine_005002a1", fill=(0, text))
    # CClass_OpDelete runs as retail code with its Mem_Delete call site stubbed (it reports the free and leaves the heap's counter in a scripted word)
    fc.stub_site(0x00466BBC, 5, 1, "mem_delete", writes=[(0x0056A970, 4, lambda a: rng.getrandbits(32))])


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def noise(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def writer_state(img, env, rng, high=0x40):
    img.put(env.slab + WRITER, noise(rng, high))
    img.u32(0x0056A920, word(rng))
    img.u32(0x0056A970, word(rng))


def buffer_at(env, rng, img):
    """The buffer at an offset that makes its address aligned, even or odd in turn; filled with noise."""
    img.put(env.slab + BUFFER, noise(rng, 0xA0))
    return env.slab + BUFFER + rng.choice([0, 1, 2, 3, 4, 5, 6, 7])


# ---- construction and destruction ---------------------------------------------------------------------------------------------------------------------------------------------------

@wcm(0x004C87C0, "TrgWriter_Construct", WRITER)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    return [], img.pokes()


def delete_case(address, name):
    @wcm(address, name, WRITER)
    def gen(env, n):
        img, rng = Image(), env.rng
        writer_state(img, env, rng)
        return [rng.choice([0, 1, 2, 3, 0x100, word(rng)])], img.pokes()
    return gen


def destruct_case(address, name):
    @wcm(address, name, WRITER)
    def gen(env, n):
        img, rng = Image(), env.rng
        writer_state(img, env, rng)
        return [], img.pokes()
    return gen


for _kind, (_delete, _destruct) in enumerate(((0x004C87E0, 0x004C8800), (0x004C88C0, 0x004C88E0), (0x004C8A50, 0x004C8A70), (0x004C8BF0, 0x004C8C10), (0x004C8C60, 0x004C8C80),
                                              (0x004C8D00, 0x004C8D20), (0x004C8DD0, 0x004C8DF0), (0x004C8E80, 0x004C8EA0))):
    delete_case(_delete, "TrgWriter_ScalarDelete_%08X" % _delete)
    destruct_case(_destruct, "TrgWriter_Destruct_%08X" % _destruct)


@wcm(0x004C8810, "TrgWriter_Size", WRITER)
def gen_size(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    if rng.random() < 0.3:
        img.u32(env.slab + WRITER + 8, 0)
    return [], img.pokes()


def construct_case(address, name, args):
    @wcm(address, name, WRITER)
    def gen(env, n):
        img, rng = Image(), env.rng
        writer_state(img, env, rng)
        return [word(rng) for _ in range(args)], img.pokes()
    return gen


construct_case(0x004C8870, "TrgNode_Construct", 7)
construct_case(0x004C8A20, "TrgLink_Construct", 1)
construct_case(0x004C8BD0, "TrgEnd_Construct", 0)
construct_case(0x004C8C30, "TrgCommandC_Construct", 1)
construct_case(0x004C8CD0, "TrgCommandE_Construct", 3)
construct_case(0x004C8DA0, "TrgCommandF_Construct", 2)
construct_case(0x004C8E50, "TrgCommandG_Construct", 1)


@wcm(0x004C8A80, "TrgLink_SetEnds", WRITER)
def gen_set_ends(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    pick = [0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 5, word(rng)]
    return [rng.choice(pick), rng.choice(pick)], img.pokes()


# ---- the writers ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@wcm(0x004C88F0, "TrgNode_Write", WRITER)
def gen_node_write(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    img.put(0x00528398, noise(rng, 8))
    img.put(0x00561488, noise(rng, 8))
    img.u32(env.slab + WRITER + 0x24, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 7, word(rng)]))
    img.u32(env.slab + WRITER + 0x20, rng.choice([0, 1, word(rng)]))
    img.u32(env.slab + WRITER + 8, rng.choice([0x26, 0x26, word(rng)]))
    return [buffer_at(env, rng, img)], img.pokes()


@wcm(0x004C8AB0, "TrgLink_Write", WRITER)
def gen_link_write(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    first = rng.choice([0xFFFFFFFF, 0, 3, word(rng)])
    second = rng.choice([0xFFFFFFFF, 1, 9, word(rng)])
    count = (first != 0xFFFFFFFF) + (second != 0xFFFFFFFF)
    img.u32(env.slab + WRITER + 0x14, first)
    img.u32(env.slab + WRITER + 0x1C, second)
    img.u8(env.slab + WRITER + 0x10, count if rng.random() < 0.9 else rng.getrandbits(2))
    img.u32(env.slab + WRITER + 0x18, env.slab + RECORD)
    img.put(env.slab + RECORD, noise(rng, 0x20))
    img.u8(env.slab + RECORD + 0x10, rng.choice([0, 1, 5, 0xC, 0xD, 0xE, 0xF, 0x10, 0xFF, rng.getrandbits(8)]))
    img.u32(0x005614C8, rng.choice([0, 1, 2, 4, 8]))
    img.u32(env.slab + WRITER + 8, word(rng))
    return [buffer_at(env, rng, img)], img.pokes()


@wcm(0x004C8C20, "TrgEnd_Write", WRITER)
def gen_end_write(env, n):
    img, rng = Image(), env.rng
    writer_state(img, env, rng)
    return [buffer_at(env, rng, img)], img.pokes()


def write_case(address, name):
    @wcm(address, name, WRITER)
    def gen(env, n):
        img, rng = Image(), env.rng
        writer_state(img, env, rng)
        img.u32(env.slab + WRITER + 8, rng.choice([0x0C, 0x16, 0x10, word(rng)]))
        return [buffer_at(env, rng, img)], img.pokes()
    return gen


write_case(0x004C8C90, "TrgCommandC_Write")
write_case(0x004C8D30, "TrgCommandE_Write")
write_case(0x004C8E00, "TrgCommandF_Write")


@wcm(0x004C8EB0, "TrgCommandG_Write")
def gen_g_write(env, n):
    img, rng = Image(), env.rng
    return [buffer_at(env, rng, img)], img.pokes()


@wcm(0x004C8ED0, "TrgCommandNone_Write")
def gen_none_write(env, n):
    img, rng = Image(), env.rng
    return [word(rng)], img.pokes()


def clear_case(address, name):
    @wcm(address, name)
    def gen(env, n):
        img, rng = Image(), env.rng
        img.put(0x0056E230, noise(rng, 0x24))
        return [], img.pokes()
    return gen


clear_case(0x004C8EE0, "Editor_ClearWordsThunk")
clear_case(0x004C8EF0, "Editor_ClearWords")
