"""Phase 4.15d probe: the cheat codes (group bchz: 0x004af660, 0x004af6b0).

The held buttons (eight flags between 0x0056aff9 and 0x0056b0a9) choose a pair of seed words and the hash mixes them into 0x0056c574 / 0x0056c578. A generator picks the hash it wants after the mix
(a code's constants) and works the words the hash must start from backwards, so every branch of the code table is reached. The career, the skater and the front end are stubs; the players are two
fake records in the slab.

Slab layout: trick-config record 0x100 (0x20), player 0 at 0x1000 (only +0x16c and +0x2858 are captured).
"""

from __future__ import annotations

from bit_specs import bit, merged, noise, word
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure
from frontend_specs_menu import Image

GROUPS = ("bchz",)

PLAYER = 0x1000
CONFIG = 0x100
SLAB5 = PLAYER + 0x2900

# (a, b, flag meaning) of every code: the hash after the mix and the second word the code needs (None: not looked at)
CODES = [
    (0x6DC02D1E, None), (0x33523575, 0x34AB2931), (0x2AB86343, 0xE94E5FA1), (0x17B941B1, None), (0x0517F426, None), (0x0E2B0DC3, None), (0x209FB3AD, None), (0x2B85A8DF, None),
    (0x317F1F9D, 0x553DC48B), (0x31E3487B, None), (0x5DF28F1E, 0x20C49738), (0x5EE13E0D, None), (0x6083C9B6, None), (0x5B445BB0, None), (0x46F27698, None), (0x39BEDF78, 0xACAE70A1),
    (0x45A930EF, None), (0x522A8B6C, None), (0xA9411EE9, None), (0x943ED118, 0xDDBA0D98), (0x87AE8F70, None), (0x8112BF6A, 0x1A348CCC), (0x70153315, None), (0x763A0DAE, None),
    (0x9B70C632, None), (0xA3E2DCE2, 0x7A6115CE), (0xA8A606AD, 0x8786EF73), (0xC0A8F966, 0xDFA4B839), (0xC0998C91, None), (0xAB93B12C, None), (0xB130BFC4, 0x7CFF03F2),
    (0xD7DDA9E0, 0x8F37160D), (0xDEBF473A, None), (0xF4301A14, None), (0xFB29F028, None),
]
BUTTONS = [
    (0x0056B099, 0x01234567, 0x34859F3A), (0x0056B089, 0xAB432901, 0x88D3A109), (0x0056B0A9, 0x776643D1, 0x0901D3E8), (0x0056B079, 0x93FE1682, 0x92551072),
    (0x0056AFF9, 0x31415926, 0x7720DE42), (0x0056B019, 0xDEADBEEF, 0xFE3010F3), (0x0056B029, 0xB87610DB, 0xDE098401), (0x0056B009, 0x03185332, 0x80FE4187),
]
INVERSE = pow(0x209, -1, 1 << 32)
WORLD = [(0x0056C574, 8), (0x00533F38, 4), (0x0056AFF8, 0xB8), (0x0056B7EC, 0x10), (0x00533FA0, 4), (0x0056A86C, 4), (0x0056A8E4, 4), (0x0056A904, 4), (0x0056B760, 4), (0x0056A948, 4),
         (0x0056A858, 8), (0x00568658, 4), (0x00568818, 4), (0x0053A510, 0x1CC4), (0x0056A23C, 0x84)]


def regions(slab):
    return merged([(slab, 0x200), (slab + PLAYER + 0x16C, 4), (slab + PLAYER + 0x2858, 4)] + WORLD, gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB5 + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = []


def install_stubs(fc, env):
    if getattr(fc, "_bit5_installed", False):
        return
    fc._bit5_installed = True
    rng = env.rng
    ensure(fc, 0x004DE150, 0, "engine_004de150", result=lambda a: rng.choice([0, 0, 0, 0, 0, 1]))     # NetGame_ModeUsesNet
    ensure(fc, 0x00416C90, 2, "engine_00416c90", masks={1: 0xFF})                                       # Career_ToggleCheat (the flag is a byte: the rest of the pushed dword is a leftover)
    ensure(fc, 0x004B0C30, 0, "engine_004b0c30", result=lambda a: rng.randrange(0, 20))               # SkaterManager_CurrentCharacter
    ensure(fc, 0x00416050, 1, "engine_00416050", result=lambda a: env.slab + CONFIG)                   # TrickConfig_FindByCharacter
    ensure(fc, 0x004B18A0, 1, "engine_004b18a0", result=lambda a: rng.randrange(0, 4))                # Skater_SelectManagerSlot
    ensure(fc, 0x00416790, 1, "engine_00416790")                                                       # Career_ClearGame
    ensure(fc, 0x0048FD60, 3, "engine_0048fd60", convention="thiscall", this=True)                    # Skater_Fatten
    ensure(fc, 0x0044E0F0, 0, "engine_0044e0f0")                                                       # Front_SwitchOnEndLevel
    ensure(fc, 0x00416890, 0, "engine_00416890")                                                       # Career_ClearGameWithEveryone


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def rotl(v):
    return ((v << 1) | (v >> 31)) & 0xFFFFFFFF


def rotr(v):
    return ((v >> 1) | (v << 31)) & 0xFFFFFFFF


def pre_image(a_target, b_target, p1, p2):
    """The (hash a, hash b) the mix starts from to end at (a_target, b_target)."""
    mixed = a_target
    a_old = rotr((a_target * INVERSE) & 0xFFFFFFFF) ^ p1
    low = rotr(b_target)
    b_old = low ^ (mixed >> 8) ^ p2
    return a_old, b_old


def cd(address, name):
    return bit(address, name, "bchz", None)


@cd(0x004AF660, "Cheat_Hash")
def gen_hash(env, n):
    img, rng = Image(), env.rng
    img.put(0x0056C574, noise(rng, 8))
    return [word(rng), word(rng)], img.pokes()


@cd(0x004AF6B0, "Cheat_Code")
def gen_cheat(env, n):
    img, rng = Image(), env.rng
    held = rng.choice(list(range(8)) + [None]) if rng.random() < 0.97 else None
    for address, _, _ in BUTTONS:
        img.u8(address, 0)
    img.put(0x0056AFF8, bytes(0xB8))
    if held is not None:
        address, p1, p2 = BUTTONS[held]
        # (the lower-numbered buttons win: clear the earlier ones and leave later ones random)
        for k, (other, _, _) in enumerate(BUTTONS):
            img.u8(other, 1 if k == held else (rng.choice([0, 1]) if k > held else 0))
        code, wanted_b = rng.choice(CODES) if rng.random() < 0.85 else (rng.getrandbits(32), None)
        b_target = wanted_b if (wanted_b is not None and rng.random() < 0.8) else rng.getrandbits(32)
        a_old, b_old = pre_image(code, b_target, p1, p2)
        img.u32(0x0056C574, a_old)
        img.u32(0x0056C578, b_old)
    else:
        img.put(0x0056C574, noise(rng, 8))
    img.u32(0x00533F38, rng.choice([0, 1, 1, 2, 3, 7, 7 if rng.random() < 0.3 else 1]))
    for address in (0x0056B7F0, 0x0056B7F4, 0x0056B7EC, 0x00533FA0, 0x0056A86C, 0x0056A8E4, 0x0056A904, 0x0056B760, 0x0056A948):
        img.u32(address, rng.choice([0, 1, 5, 6, 7, 8, 9, 0xD, rng.getrandbits(32)]))
    img.u32(0x0056A858, env.slab + PLAYER)
    img.u32(0x0056A85C, rng.choice([0, env.slab + PLAYER + 0x100]))
    img.put(env.slab + CONFIG, noise(rng, 0x20))
    img.put(env.slab + PLAYER + 0x16C, noise(rng, 4))
    img.put(env.slab + PLAYER + 0x2858, noise(rng, 4))
    img.put(0x0053A510, noise(rng, 0x1CC4))
    img.put(0x0056A23C, noise(rng, 0x84))
    img.u32(0x00568658, word(rng))
    img.u32(0x00568818, word(rng))
    return [rng.choice([0, 1, 1, 2])], img.pokes()
