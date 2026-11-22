"""Case generators for the Phase 4.8 probe: the skater manager, cheats, locked characters and options,
and the career save image."""

from __future__ import annotations

import career_state as cs
from career_specs import SPECS, Skip, Spec, base_state, spec
from career_state import u8, u16, u32

MANAGER = cs.MANAGER
ROWS = cs.ROWS
ROW_STRIDE = cs.ROW_STRIDE
FLAG_BITS = [0x02, 0x08, 0x10, 0x40, 0x80, 0x100, 0x200, 0x20]
OUT_BUFFER = 0x0055FE80  # scratch text inside the live-globals region


def rows_state(env, s, unlocked=True):
    """Vary the skater table the way a career's unlocks do: flags and the 'unlocked' dwords."""
    rng = env.rng
    for k in range(20):
        base = ROWS + k * ROW_STRIDE
        flags = env.u32(base + 0x15C)
        if rng.random() < 0.35:
            flags ^= rng.choice(FLAG_BITS)
        s.put(base + 0x15C, u32(flags))
        if unlocked and rng.random() < 0.3:
            s.put(base + 0x158, u32(rng.choice([0, 1])))


def sk_state(env, loaded=False, rows=True):
    s, lvl = base_state(env, loaded=loaded)
    from career_specs_more import custom_records
    custom_records(s, env.rng)
    if rows and env.rng.random() < 0.5:
        rows_state(env, s)
    return s


def skater_spec(address, name, nargs, gen_args, this=True, loaded=False, rows=True):
    def gen(env, n):
        s = sk_state(env, loaded=loaded, rows=rows)
        return gen_args(env), s.pokes
    SPECS.append(Spec(address, name, "skater", gen, this=MANAGER if this else None))


def slot(env):
    return env.rng.randrange(0, 8)


def character(env):
    return env.rng.choice([env.rng.randrange(0, 20)] * 8 + [20])


skater_spec(0x004B0BC0, "SkaterManager_SetActiveSlot", 1, lambda e: [e.rng.choice([slot(e)] * 5 + [-1 + 9, 8])])
skater_spec(0x004B0C30, "SkaterManager_CurrentCharacter", 0, lambda e: [])
skater_spec(0x004B0C40, "SkaterManager_CurrentCostume", 0, lambda e: [])
skater_spec(0x004B0C50, "SkaterManager_CurrentCustomSlot", 0, lambda e: [])
skater_spec(0x004B0C60, "Skater_GetResourceSlot", 1, lambda e: [e.rng.choice([slot(e)] * 5 + [8])])
skater_spec(0x004B0CD0, "SkaterManager_GetCostume", 1, lambda e: [e.rng.choice([slot(e)] * 5 + [8])])
skater_spec(0x004B0D40, "SkaterManager_SelectCurrent", 1, lambda e: [character(e)])
skater_spec(0x004B0E60, "SkaterManager_SelectCharacter", 2, lambda e: [slot(e), character(e)])
skater_spec(0x004B0F80, "Skater_CountCostumeNames", 1, lambda e: [character(e)], this=False)
skater_spec(0x004B0FE0, "SkaterManager_SelectCostume", 3, lambda e: [slot(e), e.rng.randrange(0, 6), e.rng.choice([0, 1])])
skater_spec(0x004B1070, "SkaterManager_SetCustomSlot", 2, lambda e: [slot(e), e.rng.choice([0, 1, 2, 3, 3, 4])])
skater_spec(0x004B1130, "SkaterManager_GetRememberedCostume", 1, lambda e: [e.rng.randrange(0, 20)])
skater_spec(0x004B1140, "SkaterManager_Nop", 0, lambda e: [], this=False)
skater_spec(0x004B1150, "SkaterManager_ReleaseAllLoaded", 0, lambda e: [], loaded=True)
skater_spec(0x004B1180, "SkaterManager_ReleaseActiveLoaded", 0, lambda e: [], loaded=True)
skater_spec(0x004B11C0, "SkaterManager_LoadCustomIntoActive", 1, lambda e: [e.rng.randrange(0, 4)], loaded=True)
skater_spec(0x004B1240, "SkaterManager_FormatName", 2, lambda e: [slot(e), e.rng.choice([OUT_BUFFER] * 9 + [0])], loaded=True)
skater_spec(0x004B1360, "SkaterManager_FaceBitmap", 1, lambda e: [slot(e)], loaded=True)
skater_spec(0x004B1430, "Skater_GetSlotRecord", 1, lambda e: [slot(e)], loaded=True)
skater_spec(0x004B14A0, "SkaterManager_SwapCustomRecord", 1, lambda e: [slot(e)], loaded=True)
skater_spec(0x004B1570, "SkaterManager_LoadedBlock", 1, lambda e: [slot(e)], loaded=True)
skater_spec(0x004B1630, "SkaterManager_SlotIsSelectable", 1, lambda e: [slot(e)], loaded=True)
skater_spec(0x004B1700, "Skater_SelectModelName", 2, lambda e: [character(e), e.rng.choice([0, 1, 2, 3, 3, -1, 4])], this=False)
skater_spec(0x004B1810, "Skater_CustomSlotIndex", 1, lambda e: [character(e)], this=False)
skater_spec(0x004B1890, "Skater_ResetActivePlayer", 0, lambda e: [], this=False)
skater_spec(0x004B18A0, "Skater_SelectManagerSlot", 1, lambda e: [e.rng.choice([slot(e)] * 5 + [-1])], this=False)
skater_spec(0x004B18D0, "SkaterManager_SelectCharacterChecked", 2, lambda e: [e.rng.choice([e.rng.randrange(0, 20)] * 6 + [20, 21]), slot(e)], this=False)
skater_spec(0x004B1980, "Skater_MapEnabledIndex", 1, lambda e: [e.rng.choice([0, 1, 2, 3, 3, 4, 5])], this=False)
skater_spec(0x004B19F0, "Skater_CustomWithFlag", 1, lambda e: [character(e)], this=False)
skater_spec(0x004B1A30, "Skater_DisplayName", 1, lambda e: [e.rng.choice([e.rng.randrange(0, 20)] * 6 + [-1 + 21, 20])], this=False)


# ---- cheats, locked characters, options -------------------------------------------------------

def cheat_state(env, records=False):
    rng = env.rng
    s, lvl = base_state(env, all_characters=records)
    rows_state(env, s)
    entries = b""
    for _ in range(17):
        entries += u8(rng.choice([0, 1, 1, 0])) + u8(rng.randrange(0, 20)) + u8(rng.choice([0, 1, 1, 0, 2])) + u8(0)
    s.put(0x0056A374, entries)
    s.put(0x0056A68C, u8(rng.getrandbits(8)))
    s.put(0x0055FE0C, u32(rng.randrange(0, 20)))
    # the effect flags SetCheat switches
    for address in (0x0056A8EC, 0x0056A890, 0x0056B7F4, 0x0056437C, 0x0056DB8C, 0x0056A88C):
        s.put(address, u32(rng.choice([0, 1, 10])))
    return s


def cheat_index(env):
    return env.rng.choice([env.rng.randrange(0, 17)] * 8 + [17])


def simple(address, name, group, gen_args, state=cheat_state):
    def gen(env, n):
        s = state(env)
        return gen_args(env), s.pokes
    SPECS.append(Spec(address, name, group, gen))


simple(0x004168B0, "Career_UnlockCheat", "cheats", lambda e: [e.rng.randrange(0, 17), e.rng.randrange(0, 20)])
simple(0x00416940, "Career_CheatUnlocked", "cheats", lambda e: [cheat_index(e)])
simple(0x00416960, "Career_CheatState", "cheats", lambda e: [cheat_index(e)])
simple(0x00416980, "Career_GetCheat", "cheats", lambda e: [e.rng.choice([e.rng.randrange(0, 17)] * 8 + [-1 + 18, 17])])
simple(0x00416A00, "Career_SetCheat", "cheats", lambda e: [e.rng.randrange(0, 17), e.rng.choice([0, 1, 1, 0, 2, 255]), e.rng.choice([0, 1, 1])])
simple(0x00416C90, "Career_ToggleCheat", "cheats", lambda e: [e.rng.randrange(0, 17), e.rng.choice([0, 1])])
simple(0x00416CD0, "Career_NewCheat", "cheats", lambda e: [])
simple(0x00416DE0, "Career_CountUnlockedCheats", "cheats", lambda e: [])
simple(0x00416E30, "Career_CheatName", "cheats", lambda e: [e.rng.choice([e.rng.randrange(0, 17)] * 8 + [17, 20])])
simple(0x00416F50, "Career_CheatType", "cheats", lambda e: [e.rng.choice([e.rng.randrange(0, 17)] * 4 + [-1, 8, 2, 3])])
simple(0x00416F80, "Career_UnlockCharacter", "cheats", lambda e: [e.rng.choice([0x40, 0x80, 0x100, 0x200, 0x2, 0x8, 0x10, 0x18, 0])])
simple(0x00416FB0, "Career_CharacterIsUnlocked", "cheats", lambda e: [e.rng.choice([0x40, 0x80, 0x100, 0x200, 0x2, 0x8, 0x10])])
simple(0x00417060, "Career_ApplyCheats", "cheats", lambda e: [])
simple(0x00417300, "Career_CaptureSkaterDefaults", "cheats", lambda e: [])
simple(0x00417330, "Career_ResetRuntimeState", "cheats", lambda e: [])


def options_state(env):
    s, _ = base_state(env)
    return s


simple(0x00413F30, "Options_ResetControllerDefaults", "cheats", lambda e: [e.rng.choice([0, 1])], state=options_state)
simple(0x00413F70, "Options_Init", "cheats", lambda e: [], state=options_state)


# ---- the save image ---------------------------------------------------------------------------

def text(rng, length):
    return bytes(rng.randrange(0x41, 0x5B) for _ in range(length)) + b"\0"


def save_state(env, reset=False, image_magic=None, valid=None):
    rng = env.rng
    s, lvl = base_state(env, loaded=False)
    rows_state(env, s)
    from career_specs_more import custom_records
    custom_records(s, rng)
    # the live side
    s.put(0x0055FAF8, bytes([rng.randrange(0, 20), rng.randrange(0, 20)]))
    s.put(0x0055FAFA, bytes(rng.randrange(0, 4) for _ in range(20)))
    s.put(0x0055FB0E, bytes(rng.randrange(0, 20) for _ in range(20)))
    s.put(0x0055FB22, bytes(rng.randrange(0, 2) for _ in range(20)))
    s.put(0x0055FB36, bytes(rng.randrange(0, 4) for _ in range(6)))
    s.put(0x0055FB3C, u32(rng.randrange(0, 16)) + u32(rng.randrange(0, 16)))
    s.put(0x0055FC0C, b"".join(u32(rng.choice([0, 1, 2, 3])) for _ in range(8)))
    s.put(0x0055FC2C, u32(rng.choice([0, 1])) + u32(rng.choice([0, 1])))
    s.put(0x0055FC34, u32(rng.choice([0, 1])) + u32(rng.choice([0, 1])))
    s.put(0x0055FC5C, text(rng, rng.randrange(0, 5)))
    s.put(0x0055FCC0, u32(rng.randrange(0, 3)) + u32(rng.randrange(0, 3)))
    length = rng.randrange(0, 11)
    s.put(0x0055FCC8, u32(rng.choice([length, length, 0xB, 14])))
    s.put(0x0055FCCC, text(rng, length))
    # the image side
    s.put(0x0056A2D8, bytes(rng.randrange(0, 20) for _ in range(2)) + bytes(rng.randrange(0, 4) for _ in range(2)) + bytes(
        rng.randrange(0, 4) for _ in range(2)))
    s.put(0x0056A2E0, bytes(rng.randrange(0, 2) for _ in range(2)))
    s.put(0x0056A2E8, u32(rng.randrange(0, 16)) + u32(rng.randrange(0, 16)))
    s.put(0x0056A688, bytes(rng.randrange(0, 20) for _ in range(2)))
    s.put(0x0056A3B8, b"".join(u32(rng.choice([0, 1, 2, 3])) for _ in range(8)))
    s.put(0x0056A3D8, u32(rng.choice([0, 1])) + u32(rng.choice([0, 1])))
    s.put(0x0056A3E0, u32(rng.choice([0, 1])) + u32(rng.choice([0, 1])))
    s.put(0x0056A408, text(rng, rng.randrange(0, 5)))
    s.put(0x0056A46C, u32(rng.randrange(0, 3)) + u32(rng.randrange(0, 3)))
    length = rng.randrange(0, 11)
    s.put(0x0056A474, u32(rng.choice([length, length, 0xB, 14])))
    s.put(0x0056A478, text(rng, length))
    s.put(0x00568A68, u32(image_magic if image_magic is not None else 0x101500))
    s.put(0x0056A4AE, u8(valid if valid is not None else rng.choice([0, 1])))
    s.put(0x0056A6D0, bytes(rng.randrange(0, 256) for _ in range(0x58 * 4)))
    return s


simple(0x00413ED0, "Career_RebuildCareerPointers", "save", lambda e: [], state=save_state)
simple(0x00413C10, "Career_SerializeLiveState", "save", lambda e: [], state=save_state)
simple(0x00413D60, "Career_ApplySerializedState", "save", lambda e: [], state=save_state)
simple(0x004166F0, "Career_PrepareSave", "save", lambda e: [], state=save_state)
def apply_loaded_gen(env, n):
    """Mostly the good-file path. Case 3 takes the old-format path, which ends in
    Career_ResetSaveImage: that runs the trick database build, tens of thousands of stubbed
    assertions, about twenty minutes per case under the debugger (the corrupt-file path ends the
    same way and is left to the unit test)."""
    rng = env.rng
    if n == 3:
        s = save_state(env, image_magic=rng.choice([0x101400, 0]))
    else:
        s = save_state(env)
    return [], s.pokes


SPECS.append(Spec(0x004165D0, "Career_ApplyLoadedSave", "save", apply_loaded_gen))
simple(0x004171E0, "Career_SaveCustomAppearance", "save", lambda e: [e.rng.choice([0, 1, 2, 3, 3, 3])], state=save_state)
simple(0x00417260, "Career_LoadCustomAppearance", "save", lambda e: [e.rng.choice([0, 1, 2, 3, 3, 3])], state=save_state)
def reset_gen(env, n):
    if n >= 1:
        raise Skip()   # a case takes about twenty minutes (see apply_loaded_gen)
    return [], save_state(env).pokes


SPECS.append(Spec(0x004138F0, "Career_ResetSaveImage", "reset", reset_gen))
