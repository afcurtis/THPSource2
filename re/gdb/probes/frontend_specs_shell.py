"""Phase 4.10i2 probe, part 1: the shell (frontend2.cpp): FrontEnd_Main and the functions around it.

Groups: fe2 (the helpers: the unlock flags, the replay selection swaps, Front_LaunchGameLevel, the shell music, the skater slots, the texture-set entries) and fmain
(FrontEnd_Main, the screen loop). They share the front end's world (frontend_specs_front: the statics at 0x005616c0, the career records, the pads) and its stubs; this module
adds the shell's own statics, the services the shell reaches and a driver for the screen stubs.

FrontEnd_Main runs until the main menu is left with its quit answer (or the quit flag is set). Every screen it starts is a stub whose answers come from a script: random
answers while the loop count (the driver's iteration counter, bumped by each screen call) is under the case's limit, and the answers that lead back to the main menu (a cancel,
the quit answer) after it, so the loop always ends. A stub that changes memory the shell reads afterwards (the level's start-screen request, the options screen's flags) does it
through `writes`, which the native port applies from the script.
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import (ARENA, SCRATCH_LINE, SLAB, VT_SPRITE, FRONT_WORLD, LazyRanges, check_disjoint, engine, ensure, front_read_only, front_regions, merged,
                                  seed_front, without)
from frontend_specs_front_cases import CURRENT_LEVEL, GAME_MODE, PLAYER_COUNT, new_image
from frontend_specs_front_ld import character_flags

GROUPS = ("fe2", "fmain")

# the shell's mutable statics beyond the front end's
SHELL_WORLD = [
    (0x005614B8, 0x28),            # the map flag, the park flag
    (0x00561450, 0x10),            # the park-modified flag
    (0x0052CB38, 4),               # the shell music's handle
    (0x0052BE44, 8),               # the music volume cells
    (0x006A0548, 0x40),            # the level name the log prints
    (0x0055FAF8, 0x48),            # the skater slot tables
    (0x0056A688, 4), (0x0056A2DC, 4), (0x0056A2E0, 4),
    (0x0056B8D4, 0x10),            # the replay header
    (0x0056B928, 0x200),           # the replay's per-skater data
    (0x005361D0, 4), (0x0056DD58, 4), (0x005613BC, 4),
    (0x0056A870, 0x40),            # the editor mode, the active player, the level
    (0x005601CC, 4),               # the options screen's requests
    (0x00533F38, 8),               # the game mode and the player count
]
# the custom skater info blocks (the checksum words the replay swap checks)
SHELL_WORLD += [(0x0056A488 + slot * 0x80 + 0x60, 0x10) for slot in range(21)]

SHELL_TEXT = [(0x0052BE20, 0x20), (0x0052CBA0, 0x300), (0x0052B6A0, 0x20)]

DATABASE_BLOCK = 0x8000            # a database object that already exists (a block with the fake delete vtable)
SMALL_BLOCK = 0x40

DRIVE = {"iter": 0, "limit": 5}


def over():
    return DRIVE["iter"] > DRIVE["limit"]


def tick():
    DRIVE["iter"] += 1
    if os.environ.get("P410_STUBTRACE"):
        with open(os.environ.get("P410_OUT", "/dev/null") + ".trace", "a") as fh:
            fh.write(f"    drive iter={DRIVE['iter']} limit={DRIVE['limit']}" + chr(10))


def shell_regions(slab):
    out = front_regions(slab)
    result = without(merged(list(out) + SHELL_WORLD, gap=0x40), [(0x00568638, 4), (0x0056881C, 0x20)])
    check_disjoint(result, _RO)
    return result


def shell_read_only():
    def build():
        base = list(front_read_only())
        mutable = FRONT_WORLD + SHELL_WORLD
        return merged(without(base + SHELL_TEXT, mutable), gap=0)

    return LazyRanges(build)


_RO = shell_read_only()


def seed_shell(env, rng):
    """The front groups' seed; every block of the arena starts with the fake delete vtable (the shell deletes the databases it built)."""
    from opentony.fcall import write_chunks

    from frontend_specs_front import ARENA_SIZE

    writes = seed_front(env, rng)
    block = struct.pack("<I", env.slab + VT_SPRITE)
    for offset in range(0, ARENA_SIZE, 16):
        write_chunks(env.slab + ARENA + offset, block)
    return writes


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = shell_regions
    READ_ONLY[_group] = _RO
    SEEDS[_group] = seed_shell


def fe2(address, name):
    return spec(address, name, "fe2")


def fmain(address, name):
    return spec(address, name, "fmain")


# ---- the worlds ---------------------------------------------------------------------------------------------------------------------------------------------------

def shell_world(env, img, rng, *, replay=None):
    """The shell's statics: the character table, the replay header, the music, the park and editor flags, the skater slots."""
    character_flags(img, rng)
    img.u32(GAME_MODE, rng.choice([1, 2, 3, 4, 7, 9, 10]))
    img.u32(PLAYER_COUNT, rng.choice([1, 2]))
    img.u32(CURRENT_LEVEL, rng.randrange(0, 15))
    img.u8(0x005614C4, rng.choice([0, 0, 1]))
    img.u32(0x005614BC, rng.choice([0, 1]))
    img.u8(0x00561450, rng.choice([0, 1]))
    img.u8(0x00561CA8, rng.choice([0, 0, 0, 1]))
    img.u8(0x00561CBD, 0)
    img.u8(0x00561CBC, rng.choice([0, 0, 0, 1]))
    img.u8(0x00561990, rng.choice([0, 0, 1]))
    img.u32(0x0056A8A8, rng.randrange(1, 16))
    img.u32(0x0056A888, rng.choice([0, 0, 1, 2]))
    img.u8(0x00561C70, rng.choice([0, 0, 1]))
    img.u32(0x00561C74, rng.choice([1, 2, 3, 8, 10, 4]))
    img.u32(0x00561C90, rng.randrange(0, 14))
    img.u32(0x00561B9C, 0)
    img.u32(0x00561C98, rng.choice([0, 1]))
    img.u32(0x00561CB8, rng.choice([0, 3]))
    img.u32(0x0052CB38, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 7, 0x1234]))
    img.u32(0x0052BE44, rng.choice([0x3FFF, 0x1000, rng.randrange(0, 0x4000)]))
    img.u32(0x0052BE48, rng.choice([0x3FFF, 0x2000, 0x100, rng.randrange(0, 0x8000)]))
    img.u32(0x00561C84, rng.choice([0, 0x3FFF, 0x2000, rng.randrange(0, 0x4000)]))
    img.u32(0x0056B834, rng.randrange(0x600000, 0x680000) & ~3)
    img.u32(0x005613BC, rng.randrange(0, 12))
    img.u32(0x005361D0, rng.getrandbits(32))
    # the options screen's requests, the credits and the video menu's
    img.u8(0x005601CC, rng.choice([0, 0, 0, 1]))
    img.u8(0x005601CD, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561CDC, rng.choice([0, 0, 0, 3]))
    img.u32(0x00561CE0, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561CA0, rng.choice([0, 1]))
    img.u32(0x00561CA4, rng.choice([0, 1]))
    # the database: none, or one that already exists
    img.u32(0x00561CB4, rng.choice([0, env.slab + ARENA + DATABASE_BLOCK]))
    # the skater slot tables
    for k in range(0x48):
        img.u8(0x0055FAF8 + k, rng.choice([0, 1, 2, 3, 5, 9, 0x1F, 0x80, 0xFF, rng.randrange(256)]))
    for k in range(4):
        img.u8(0x0056A688 + k, rng.randrange(256))
        img.u8(0x0056A2DC + k, rng.randrange(256))
        img.u8(0x0056A2E0 + k, rng.randrange(256))
    # the replay header: the skaters, the characters and costumes, the level and mode, the per-skater data
    count = rng.choice([0, 0, 1, 2, 2, 1, -1, -2, 3]) if replay is None else replay
    img.put(0x0056B8D4, struct.pack("<h", count))
    img.u8(0x0056B8D6, rng.randrange(0, 20))
    img.u8(0x0056B8D7, rng.randrange(0, 20))
    img.u8(0x0056B8D8, rng.randrange(0, 5))
    img.u8(0x0056B8D9, rng.randrange(0, 5))
    img.put(0x0056B8DA, struct.pack("<hh", rng.randrange(0, 14), rng.choice([1, 2, 3, 4, 7, 9, 10])))
    img.garbage(0x0056B928, 0x200, rng)
    # the custom skater info checksum words (equal ones make the replay swap's check fail)
    for slot in range(21):
        a = rng.getrandbits(32)
        img.u32(0x0056A488 + slot * 0x80 + 0x64, a)
        img.u32(0x0056A488 + slot * 0x80 + 0x68, rng.choice([a, rng.getrandbits(32), rng.getrandbits(32)]))
    DRIVE["iter"] = 0
    DRIVE["small"] = 0
    DRIVE["limit"] = rng.randrange(0, 9)


@fe2(0x00452E40, "Front_GetUnlockFlags")
def gen_unlock_flags(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    out = env.slab + 0xF00
    img.put(out, bytes(rng.randrange(256) for _ in range(4)))
    return [out], img.pokes()


@fe2(0x00452EC0, "Front_ReselectAfterUnlockChange")
def gen_reselect(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    old = env.slab + 0xF00
    img.put(old, bytes(rng.choice([0, 1]) for _ in range(4)))
    return [old], img.pokes()


@fe2(0x00452FE0, "Front_ShellNothing")
def gen_nothing(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@fe2(0x00454130, "Front_ReplaySwapSkatersIn")
def gen_swap_in(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng, replay=rng.choice([1, 2, 1, 2, 0, 3, -1]))
    return [], img.pokes()


@fe2(0x00454300, "Front_ReplaySwapSkatersOut")
def gen_swap_out(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng, replay=rng.choice([-1, -2, -1, -2, 0, -3, 1]))
    return [], img.pokes()


@fe2(0x004544A0, "Front_LaunchGameLevel")
def gen_launch(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    return [rng.randrange(0, 14), rng.choice([1, 2, 3, 4, 5, 7, 9, 10])], img.pokes()


@fe2(0x00454710, "Front_StartShellMusic")
def gen_music(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    return [], img.pokes()


@fe2(0x00454760, "Front_SyncSkaterSlots")
def gen_sync(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    return [], img.pokes()


@fe2(0x004547F0, "Player_GetTextureSetEntry")
def gen_texture_entry(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    return [rng.choice([rng.randrange(0, 20), rng.randrange(0, 20), 0, 19]), rng.choice([rng.randrange(0, 8), rng.randrange(0, 20), rng.randrange(0, 22), 0xFFFFFFFF])], img.pokes()


@fe2(0x004AC3F0, "Front_ClearGVideoRestartData")
def gen_clear_restart(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    return [], img.pokes()


@fmain(0x00452FF0, "FrontEnd_Main")
def gen_frontend_main(env, n):
    img, rng = new_image(env)
    shell_world(env, img, rng)
    if rng.random() < 0.08:
        img.u8(0x00561CBD, 1)                  # the window was closed before the loop started
    if rng.random() < 0.2:
        img.u8(0x0056B0D8, 1)                  # the quit combination
        img.u8(0x0056B0E8, 1)
        clock = rng.randrange(1000, 100000)
        img.u32(0x0056E31C, clock)
        img.u32(0x00561CC0, (clock - rng.choice([0, 0x100, 0xDD, 0xDC])) & 0xFFFFFFFF)
    return [], img.pokes()


# ---- stubs --------------------------------------------------------------------------------------------------------------------------------------------------------

def start_screen_answer(rng):
    return 0 if over() else rng.choice([0, 0, 1, 2, 6, 7, 3, 5])


def silence_harness():
    """The harness that brought the game to the level left breakpoints inside FrontEnd_Main (the main-menu answer, the screen automation that arms key loops): a forced call of the
    shell runs through them, and one that creates breakpoints from a stop handler breaks gdb (finish_step_over). They have done their job: disable them."""
    import gc

    from opentony.breakpoint import TonyBreakpoint

    for obj in gc.get_objects():
        if isinstance(obj, TonyBreakpoint) and not hasattr(obj, "owner"):
            address = getattr(obj, "address", None)
            if address is not None and 0x00452FF0 <= address < 0x00454130:
                obj.enabled = False


def install_shell_stubs(fc, env):
    """The services the shell reaches: everything the front stubs have plus the skater manager, the screens FrontEnd_Main starts and the level launch."""
    from frontend_specs_front import install_front_stubs

    silence_harness()
    install_front_stubs(fc, env)
    rng = env.rng
    this_stub = dict(thiscall=True)

    def small_block(a):
        at = env.slab + ARENA + 0x9000 + SMALL_BLOCK * DRIVE.get("small", 0)
        DRIVE["small"] = DRIVE.get("small", 0) + 1
        env.alloc_log.append((at, a[0]))
        return at

    ensure(fc, 0x00466B70, 1, "operator_new", result=small_block)
    # the skater manager (thiscall on 0x0056c580)
    engine(fc, 0x00416FB0, 1, result=lambda a: rng.choice([0, 1]))                               # Career_CharacterIsUnlocked
    engine(fc, 0x004B0E60, 2, **this_stub)                                                        # SkaterManager_SelectCharacter
    engine(fc, 0x004B0FE0, 3, **this_stub)                                                        # SkaterManager_SelectCostume
    engine(fc, 0x004B0BC0, 1, **this_stub)                                                        # SkaterManager_SetActiveSlot
    engine(fc, 0x004B0D40, 1, **this_stub)                                                        # SkaterManager_SelectCurrent
    engine(fc, 0x004B0C30, 0, result=lambda a: rng.randrange(0, 20), **this_stub)                 # SkaterManager_CurrentCharacter
    engine(fc, 0x004B0C40, 0, result=lambda a: rng.randrange(0, 5), **this_stub)                  # SkaterManager_CurrentCostume
    engine(fc, 0x004B0C60, 1, result=lambda a: rng.randrange(0, 20), **this_stub)                 # Skater_GetResourceSlot
    engine(fc, 0x004B1810, 1, result=lambda a: rng.randrange(0, 20))                              # Skater_CustomSlotIndex
    engine(fc, 0x004B18D0, 2)                                                                     # SkaterManager_SelectCharacterChecked(character, slot)
    engine(fc, 0x004B18A0, 1, result=lambda a: rng.randrange(0, 20))                              # Skater_SelectManagerSlot
    engine(fc, 0x004B1890, 0)                                                                     # Skater_ResetActivePlayer
    engine(fc, 0x004B1150, 0, **this_stub)                                                        # SkaterManager_ReleaseAllLoaded
    engine(fc, 0x0046F3E0, 3)                                                                     # Mem_Swap
    # the launch
    engine(fc, 0x004B5190, 0)
    engine(fc, 0x004327A0, 0)
    engine(fc, 0x0046A8D0, 1, writes=[(0x00561B9C, 4, lambda a: start_screen_answer(rng))])      # LaunchGame: the level sets the start-screen request
    engine(fc, 0x004B32F0, 3)                                                                     # PSX_ClearRegion
    engine(fc, 0x004586F0, 2)                                                                     # Init_Restart
    engine(fc, 0x004326A0, 0)
    ensure(fc, 0x005002A1, 2, "sprintf", text=1)
    engine(fc, 0x004B50E0, 0)
    engine(fc, 0x0041DB50, 0, result=lambda a: env.alloc_log[-1][0], **this_stub)                  # TextureSetManager_Initialize
    ensure(fc, 0x004AD620, 3, "sfx", result=lambda a: rng.randrange(1, 0x7FFF))                   # SFX_PlayX
    engine(fc, 0x004301F0, 3)                                                                     # Db_Init
    engine(fc, 0x004236D0, 0)                                                                     # CustomSkaters_InitAll
    engine(fc, 0x004DBD90, 0)
    engine(fc, 0x004DBE60, 0)
    engine(fc, 0x004F6510, 0)                                                                     # CD_Recheck
    engine(fc, 0x004C8F10, 2)                                                                     # Utils_StartFade
    engine(fc, 0x00432980, 2, text=True)
    engine(fc, 0x004F6E60, 1)                                                                     # WINMAIN_SwitchResolution
    engine(fc, 0x00432860, 2)                                                                     # (no out pointer here: the editor passes 0, 0)
    engine(fc, 0x004DE300, 1, result=lambda a: rng.randrange(0, 14))
    engine(fc, 0x0047B2D0, 1, text=True)                                                          # Replay_LoadHeader
    engine(fc, 0x004CE030, 0)
    engine(fc, 0x0041C2D0, 0)                                                                     # Game_MainLoop
    engine(fc, 0x00417060, 0)                                                                     # Career_ApplyCheats
    engine(fc, 0x0047CBB0, 1, result=lambda a: -1 if over() or rng.random() < 0.4 else rng.choice([0, 1, 0x16, 0x15, 0x11, 9, 0xE, 0xF, 0x10, 0x13, 0x17, 3, 0xA]))      # NetMenu_Main (the screen to show next: the second capture pass reaches the screens the main menu does not)

    # the screens: (0, &answer) with the answer a scripted dword; the ones that take a request through a pointer record its value
    def screen(address, answer, **kw):
        engine(fc, address, 2, stack_to=SCRATCH_LINE, pokes=[(1, 4, lambda a: (tick(), answer())[1])], **kw)

    def main_menu():
        if over():
            return 0xFF
        return rng.choice([0x20, 0x21, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x100, 0x20, 0x26, 0x2B, 0xFF])

    def cancel_or(values):
        return 0xFFFFFFFF if over() or rng.random() < 0.45 else rng.choice(values)

    screen(0x0046AEE0, main_menu)
    screen(0x00417400, lambda: cancel_or([0, 1, 2]))
    screen(0x004588F0, lambda: cancel_or(list(range(14))))
    screen(0x0042CD50, lambda: cancel_or([0, 1, 2]))
    screen(0x0042BE70, lambda: cancel_or([0, 1]))
    screen(0x004B5BA0, lambda: 0)
    screen(0x004BF860, lambda: 0)
    screen(0x0047BA60, lambda: cancel_or([0, 1]))
    screen(0x00454EB0, lambda: cancel_or([0, 1]))
    # the options screen: its answer and the requests it leaves in memory
    engine(fc, 0x00481200, 2, stack_to=SCRATCH_LINE,
           pokes=[(1, 4, lambda a: (tick(), 0)[1])],
           writes=[(0x005601CC, 1, lambda a: 0 if over() else rng.choice([0, 0, 1])), (0x005601CD, 1, lambda a: 0 if over() else rng.choice([0, 0, 1])),
                   (0x00561CDC, 4, lambda a: 0 if over() else rng.choice([0, 0, 2])), (0x00561CE0, 4, lambda a: 0 if over() else rng.choice([0, 0, 1]))])
    # the select screens that take a request: PlayerSelect(&mode in, &answer out), EquipSelect(&{mode, player} in, &answer out), CreateEditor(&request, &answer)
    engine(fc, 0x004A2010, 2, stack_to=SCRATCH_LINE, deref=[0],
           pokes=[(1, 4, lambda a: (tick(), cancel_or([0, 1, 2]))[1])])
    engine(fc, 0x00444590, 2, stack_to=SCRATCH_LINE, blob=(0, 8), pokes=[(1, 4, lambda a: (tick(), cancel_or([0, 1, 5]))[1])])
    engine(fc, 0x004271A0, 2, stack_to=SCRATCH_LINE, deref=[0], pokes=[(1, 4, lambda a: (tick(), cancel_or([0, 1]))[1])])
    engine(fc, 0x00426430, 2, deref=[0])


GROUP_STUBS = {group: install_shell_stubs for group in GROUPS}
