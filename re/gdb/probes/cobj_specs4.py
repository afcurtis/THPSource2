"""Phase 4.15f probe: the create-a-skater objects, part 4: the part database and the skater object's construction (group cpart: MySkater__ProcessSpoolerEntry, MySkater__Free2,
Create_SpoolTextures (the part database's constructor) and MySkater__Construct).

The part manifest the database reads (PlayerSpool_LoadPartManifest) is a stub that writes the names it was told to: env.manifest holds, for the two kinds of call (the one that also asks
for texture names and the animation one), the count and the pointers of the strings (the part names and the texture group names of cobj_world's MANIFEST area).
"""

from __future__ import annotations

import cobj_world as W
from cobj_world import u16, u32
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
from cobj_specs import SCRATCH, noise, r, word

GROUPS = ("cpart",)

NAME_BUFFER = SCRATCH + 0x200                 # createobj::kNameBuffer: "sk2def%d" is built here
NAMES_LOCAL = SCRATCH + 0x1000                # createobj's locals for the manifest: names, texture names and the count
TEXTURES_LOCAL = SCRATCH + 0x1200
COUNT_LOCAL = SCRATCH + 0x1400
LITERALS = [(0x00525DB0, 0x60), (0x00525F08, 0xD8), (0x00526210, 0x10), (0x00525FF8, 0x20), (0x00518764, 0x18)]       # the part name tables, "NONE", "sk2def" and "anims_fe"


def regions(slab):
    return merged([(slab + W.DB, W.DB_BYTES), (slab + W.SKATERS, 0x1000), (slab + W.SRC_TABLE, 0x100), (slab + W.BUILD_TABLE, 0x100), (slab + W.TABLES, 0x100),
                   (slab + W.MATERIALS, 0x100), (slab + W.TEXTURES, 0x100), (slab + W.SCENE, 0x100), (slab + W.LIST, 0x200), (slab + W.SRC_MODELS, W.MODEL_STRIDE * W.PARTS),
                   (slab + W.MANIFEST, 0x1000), (slab + W.ARENA, 0x4000), *W.region_windows()], gap=0)


def seed(env, rng):
    env.alloc_base = W.ARENA
    env.alloc_size = W.ARENA_BYTES
    env.manifest = {}
    env.region_seq = []
    return W.seed_scene(env, rng)


for _group in GROUPS:
    SLAB_BYTES[_group] = W.SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def find_nocase(args):
    """String_FindNoCase(haystack, needle): the first place the needle occurs in the haystack, upper case letters seen as lower case; 0 if it does not (or the needle is empty)."""
    from opentony.memory import mem

    def lower(data):
        return bytes(c + 0x20 if 0x41 <= c <= 0x5A else c for c in data)

    haystack = lower(mem.cstring(args[0], 256).encode("latin-1"))
    needle = lower(mem.cstring(args[1], 256).encode("latin-1"))
    if not needle:
        return 0
    at = haystack.find(needle)
    return 0 if at < 0 else args[0] + at


def find_body_group(args):
    """TextureSet_FindBodyGroup(name, group) on the part database (ecx): the index of the name among the group's names, -1 if it is not there."""
    import gdb
    from opentony.memory import mem

    database = int(gdb.parse_and_eval("$ecx")) & 0xFFFFFFFF
    name, group = args[0], args[1]
    if not 0 <= group <= 3 or name == 0:
        return 0xFFFFFFFF
    wanted = mem.cstring(name, 0x40)
    for i in range(mem.u32(database + 0x1C68 + 4 * group)):
        if mem.cstring(database + 0x1868 + i * 0x40 + group * 0x10, 0x10) == wanted:
            return i
    return 0xFFFFFFFF


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj4_installed", False):
        return
    fc._cobj4_installed = True
    mark_under_test()
    rng = env.rng
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x004B20F0, 1, "engine_004b20f0", result=lambda a: env.slab + W.TABLES)
    ensure(fc, 0x004B2030, 1, "engine_004b2030", result=lambda a: env.materials.get(a[0], 0))
    ensure(fc, 0x004B82B0, 2, "engine_004b82b0", text=True, result=find_nocase)
    ensure(fc, 0x0041FE50, 2, "engine_0041fe50", convention="thiscall", this=True, result=find_body_group)
    ensure(fc, 0x004B3230, 1, "engine_004b3230", text=True, stack_to=NAME_BUFFER, result=lambda a: region_lookup(env))

    def manifest_kind(a):
        return env.manifest["process" if a[2] else "anim"]

    def count_of(a):
        return manifest_kind(a)["count"]

    def names_of(a):
        return b"".join(u32(p) for p in manifest_kind(a)["names"])

    def textures_of(a):
        return b"".join(u32(p) for p in manifest_kind(a)["textures"])

    ensure(fc, 0x004B47A0, 5, "engine_004b47a0", text=True, stack_to={1: NAMES_LOCAL, 2: TEXTURES_LOCAL, 3: COUNT_LOCAL},
           pokes=[(3, 4, count_of), (1, None, names_of), (2, None, textures_of)], result=lambda a: env.slab + W.MANIFEST_BLOCK)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def region_lookup(env):
    """PSX_FindLoadedRegionByName: the scripted answers in turn, then the source region."""
    return env.region_seq.pop(0) if env.region_seq else W.World.source


def cp(address, name, this=None):
    return r(address, name, "cpart", this)


def part_names(rng, count):
    return [env_name(rng.randrange(W.PARTS)) for _ in range(count)]


def name_pointer(env, index):
    return env.slab + W.MANIFEST + W.NAME_STRIDE * index


def texture_pointer(env, index):
    return env.slab + W.TEXTURE_NAMES + W.NAME_STRIDE * index


def process_manifest(env, rng, count=None, vocabulary_size=None, distinct=False):
    """The manifest a database reads: `count` parts (names from the 19, some repeated) with texture group names from a small vocabulary."""
    count = rng.choice([19, 19, 19, 12, 5, 1, 0]) if count is None else count
    vocabulary = rng.sample(range(len(W.TEXTURE_VOCABULARY)), rng.choice([2, 4, 8, 12]) if vocabulary_size is None else vocabulary_size)
    if vocabulary_size == 1:
        vocabulary = [0]                                  # (the one name the texture sets carry)
    names = [name_pointer(env, rng.randrange(W.PARTS)) for _ in range(count)]
    if distinct:
        order = list(range(W.PARTS))
        rng.shuffle(order)
        names = [name_pointer(env, order[k % W.PARTS]) for k in range(count)]
    textures = [texture_pointer(env, rng.choice(vocabulary)) for _ in range(count)]
    return {"count": count, "names": names, "textures": textures}


def anim_manifest(env, rng):
    """The animation manifest: the 19 names in another order, rarely a wrong count or a name that matches no part."""
    order = list(range(W.PARTS))
    rng.shuffle(order)
    count = rng.choice([0x13, 0x13, 0x13, 0x13, 0x12, 0x14])
    names = [name_pointer(env, order[k % W.PARTS]) for k in range(count)]
    if rng.random() < 0.15:
        names[rng.randrange(count)] = texture_pointer(env, 0)
    return {"count": count, "names": names, "textures": []}


def db_object(env):
    return env.slab + W.DB


@cp(0x0041F4B0, "MySkater__ProcessSpoolerEntry", W.DB)
def gen_process(env, n):
    rng = env.rng
    img = Image()
    env.manifest = {"process": process_manifest(env, rng), "anim": anim_manifest(env, rng)}
    img.put(env.slab + W.DB + 0x1868, noise(rng, 0x400))                # (the group names the manifest overwrites)
    img.u32(env.slab + W.DB + 0x1864, rng.choice([0, 19, 5]))
    img.put(env.slab + W.STRINGS, b"sk2def\0")
    return [env.slab + W.STRINGS], img.pokes()


@cp(0x0041F7C0, "MySkater__Free2", W.DB)
def gen_free2(env, n):
    rng = env.rng
    img = Image()
    env.manifest = {"process": process_manifest(env, rng), "anim": anim_manifest(env, rng)}
    img.u32(env.slab + W.DB + 0x1864, rng.choice([19, 19, 19, 7]))
    for i in range(W.PARTS):
        img.u32(env.slab + W.DB + 4 + 0x34 * i + 0x14, rng.choice([0, 0xFFFFFFFF, i]))
    img.put(env.slab + W.STRINGS, b"anims_fe\0")
    return [env.slab + W.STRINGS], img.pokes()


@cp(0x0041F1E0, "Create_SpoolTextures", W.DB)
def gen_database_construct(env, n):
    rng = env.rng
    img = Image()
    env.manifest = {"process": process_manifest(env, rng, rng.choice([19, 19, 19, 19, 12])), "anim": anim_manifest(env, rng)}
    env.region_seq = [W.World.source]
    index = rng.choice([0, 1, 5, 9, 9, 0, 3, 10, 0xFFFFFFFF])
    img.put(env.slab + W.DB + 0x1C68, noise(rng, 0x10))
    return [index, rng.choice([0, word(rng)])], img.pokes()


@cp(0x0041FF50, "MySkater__Construct", lambda env: env.slab + W.SKATERS + W.SKATER_STRIDE * 2)
def gen_skater_construct(env, n):
    rng = env.rng
    img = Image()
    env.manifest = {"process": process_manifest(env, rng, 19), "anim": anim_manifest(env, rng)}
    env.region_seq = [W.World.source]
    skater = env.slab + W.SKATERS + W.SKATER_STRIDE * 2
    img.put(skater, noise(rng, 0x31C))
    buffer = rng.choice([0, 0, env.slab + W.LIST])
    return [rng.choice([0, 1, 4, 9]), rng.choice([0, word(rng)]), buffer], img.pokes()
