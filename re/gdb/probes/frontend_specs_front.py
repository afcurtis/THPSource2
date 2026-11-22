"""Phase 4.10i probe, part 1: the groups of the in-level front end (front.cpp), their regions, seeds and stubs (the generators are in frontend_specs_front_cases*.py).

Groups: fmenu (the CMenu class), fscr (the small screens, Front_Init and the menu deletion), fdisp (Front_Display), fst (the status changes and the state helpers),
fupd (Front_Update) and fld (the level loader and the player loading).

The state lives in a few hundred statics: the front end's own (0x005616c0..0x00561d00), the career world (the 4.8 probe's regions and sampler: the character records, the level
table, the high scores, the competition object), the pad bytes, the horse game's word and restart names and the two player objects the running level has. The menus are real
CMenu objects built in the scratch slab by the generators (the class is replayed on its own by the fmenu group); the name entry and the paused-menu bitmap are fake objects whose
virtual methods are reported services.

Slab layout (offsets from the scratch slab):
  0x0000 fake sprite vtable (delete, -, -, draw)   0x0040 fake name-entry vtable (delete, -, draw)   0x0100 the string pool (32 strings of 0x40 bytes)
  0x0a00 the paused-menu bitmap (a sprite object)  0x0a80 the name entry object (0x100 bytes)       0x0c00 fake player extras (0x400)
  0x1000 twelve menus of 0x200 bytes               0x4000 the allocation arena (zeroed: retail's class new zeroes its blocks, 0xc000 bytes)
  0x10000 the fake second player (0x3600 bytes)
"""

from __future__ import annotations

import struct

import career_state as cs
from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import Image, SPRITE_DRAW, VIRTUAL_DELETE

SLAB = 0x14000
FAKE_PLAYER = 0x10000            # the second player of the two-player cases (the running level has one player): 0x3600 bytes, only its fields are captured
VT_SPRITE = 0x0000
VT_ENTRY = 0x0040
STRINGS = 0x0100
STRING_STRIDE = 0x40
STRING_COUNT = 32
SPRITE = 0x0A00
ENTRY = 0x0A80
MENUS = 0x1000
MENU_STRIDE = 0x200
ARENA = 0x4000
ARENA_SIZE = FAKE_PLAYER - ARENA
NAME_ENTRY_DRAW = 0x004AD950       # an unreached engine function: the name entry's draw slot
TICK = 0x0056E31C
SCRATCH_LINE = 0x7FF00400

GROUPS = ("fmenu", "fscr", "fdisp", "fst", "fupd", "fld")

STRING_POOL = [b"Continue", b"Retry", b"Quit", b"End Run", b"Sound Levels", b"yes", b"no", b"Restart Competition", b"Level Goals", b"Level Select", b"Back to Editor",
               b"Main Menu", b"Watch replay", b"Save Career", b"Play Replay", b"Highlights", b"Save Replay", b"Next Heat", b"Next Player", b"End Competition", b"A",
               b"abcdefghijklmnopqrstuvwxyz01234", b"MUSIC", b"TRACK", b"TEMP", b"Spend Some Cash", b"One Minute", b"Two Minutes", b"Update save file", b"x", b"X", b"Done",
               b"de_ramp", b"HO_start", b"restart one", b"de_"]


def menu_at(env, index):
    return env.slab + MENUS + MENU_STRIDE * index


def string_at(env, index):
    return env.slab + STRINGS + STRING_STRIDE * index


# the front end's mutable globals beyond the career world
FRONT_WORLD = [
    (0x005614C0, 0x10),           # the park-editor level flag
    (0x005616C0, 0x640),          # the front end's statics: menus, status, buffers, counters
    (0x00537B3C, 4),              # the paused-bitmap flag
    (0x005477EC, 4),              # the replay restart bookkeeping
    (0x0052BF30, 0x10),           # the demo blink, the status anchors and the high-score row being edited
    (0x00568628, 0x70),           # the horse restart bookkeeping (0x00568638 is the assertion's line word: cut out below)
    (0x00568810, 0x10),
    (0x00568A20, 0x48),
    (0x005685F4, 0x10), (0x00568650, 0x10), (0x0056E770, 8),
    (0x0056A850, 0x20),           # the two player pointers, the horse turn, the level
    (0x0056A940, 0x40),
    (0x0056AFF8, 0x2C8),          # the pad bytes
    (0x0056B700, 0x50), (0x0056B790, 0x60), (0x0056B830, 8), (0x0056B8D4, 4), (0x0056BB38, 8),
    (0x0056A840, 0x10), (0x0056A9A0, 0x40),          # the model regions of the players, the mess font object
    (0x0056D3F0, 0x750), (0x0056DB40, 0x28), (0x0056DCAC, 0x10), (0x0056DD5C, 4),          # the PSX region tables, the loading flags
    (0x0052DD10, 8),
    (0x0056C574, 8),
    (0x0056DB64, 4),
    (0x0056E160, 0xB0), (0x0056E310, 0x14), (0x0056E5D8, 4), (0x0056E698, 4),
    (0x006A3B21, 4), (0x006A18BC, 4),
    (0x029DA394, 0x10),
    (0x00560FD0, 8),
]
# the flags dword of each of the twenty characters in the selection table (bit 0 the steamer sounds, bit 1 a custom skater, bit 7 not a regular character): the loader and the
# unlock checks read them, so a case may poke them
FRONT_WORLD += [(0x0053A514 + character * 0x170, 4) for character in range(20)]
# the assertion's line and file words: the port reports only a failed check
CUT = [(0x00568638, 4), (0x0056881C, 0x20)]

# the player objects: the fields the screens read and write (the 4.8 probe's PLAYER_FIELDS plus the statistics, the seat and the record)
PLAYER_EXTRA = [(0x16C, 0x1E0), (0x2A8, 4), (0x2CC0, 0x28), (0x2E30, 8), (0x2DD4, 4), (0x29B0, 4), (0x30B8, 4), (0x31E4, 8), (0x20, 4)]

# the static texts and formats the screens read
FRONT_TEXT = [
    (0x0052E280, 0xD00),          # the pointer cells of the screens' texts
    (0x0052C600, 0x600),          # front.cpp's messages and formats
    (0x005256C0, 0x240), (0x005252C0, 0x10), (0x0052A560, 0x20), (0x00527C60, 0x10), (0x00527D54, 4), (0x00525438, 0x10),
    (0x0055FE10, 0x10),
]


def merged(items, gap=0):
    items = sorted(items)
    out = []
    for base, size in items:
        if out and base <= out[-1][0] + out[-1][1] + gap:
            out[-1][1] = max(out[-1][0] + out[-1][1], base + size) - out[-1][0]
        else:
            out.append([base, size])
    return [(b, s) for b, s in out]


def without(static, holes):
    out = []
    for base, size in static:
        pieces = [(base, base + size)]
        for hole, length in holes:
            cut = []
            for lo, hi in pieces:
                if hole < hi and lo < hole + length:
                    if lo < hole:
                        cut.append((lo, hole))
                    if hole + length < hi:
                        cut.append((hole + length, hi))
                else:
                    cut.append((lo, hi))
            pieces = cut
        out += [(lo, hi - lo) for lo, hi in pieces]
    return out


def check_disjoint(result, static):
    for base, size in result:
        for sbase, ssize in static:
            if base < sbase + ssize and sbase < base + size:
                raise RuntimeError(f"mutable region {base:#x}+{size:#x} overlaps the read-only {sbase:#x}+{ssize:#x}")


def career_read_only():
    out = list(cs.READ_ONLY)
    cursor = cs.SKATER_TABLE[0]
    for base, size in cs.SKATER_WINDOWS:
        if base > cursor:
            out.append((cursor, base - cursor))
        cursor = base + size
    if cursor < cs.SKATER_TABLE[1]:
        out.append((cursor, cs.SKATER_TABLE[1] - cursor))
    return out


class LazyRanges(list):
    """The read-only ranges, computed when the probe first iterates them (the game is running then: the strings the pointer cells point to are read from it)."""

    def __init__(self, build):
        super().__init__()
        self._build = build
        self._done = False

    def _fill(self):
        if not self._done:
            self._done = True
            self.extend(self._build())

    def __iter__(self):
        self._fill()
        return super().__iter__()

    def __len__(self):
        self._fill()
        return super().__len__()


def front_read_only():
    def build():
        from opentony.memory import mem

        mutable = FRONT_WORLD + list(cs.LIVE) + list(cs.STATIC) + list(cs.SKATER_WINDOWS)
        # (the career's read-only block holds the pointer cells at 0x0052e2e8.. which FRONT_TEXT covers whole)
        base = [r for r in career_read_only() if not (0x0052E000 <= r[0] < 0x0052F000)]
        strings = []
        for cell in range(0x0052E280, 0x0052EF80, 4):
            value = mem.u32(cell)
            if 0x00520000 <= value < 0x00540000:
                strings.append((value, 0x60))
        # the strings the level table (14 records of 0x1ac bytes) and the character table (21 of 0x170) point to
        for start, stride, count in ((0x0053C1F8, 0x1AC, 14), (0x0053A3A4, 0x170, 21)):
            data = mem.bytes(start, stride * count)
            for offset in range(0, len(data) - 3, 4):
                value = int.from_bytes(data[offset:offset + 4], "little")
                if 0x00520000 <= value < 0x00540000:
                    strings.append((value, 0x60))
        return merged(without(base + FRONT_TEXT + strings, mutable), gap=0)

    return LazyRanges(build)


def front_regions(slab):
    """The mutable regions: the career world, the front end's, the players' fields (and the HUD skater's) and the slab."""
    from opentony.memory import mem

    out = list(cs.regions(["goals"])) + FRONT_WORLD
    pointers = []
    for slot in range(2):
        player = mem.u32(0x0056A858 + 4 * slot)
        if player:
            pointers.append(player)
    hud = mem.u32(0x0056A960)
    if hud:
        pointers.append(hud)
    for player in pointers:
        out += [(player + offset, size) for offset, size in PLAYER_EXTRA]
        record = mem.u32(player + 0x29B0)
        if record:
            out.append((record + 0x504, 4))
        other = mem.u32(player + 0x20)
        if other:
            out.append((other, 4))
    renderer = mem.u32(0x00560FD4)
    if renderer:
        out.append((renderer + 0x88, 4))
    out += [(slab + FAKE_PLAYER + offset, size) for offset, size in PLAYER_EXTRA]
    out.append((slab, FAKE_PLAYER))
    result = without(merged(out, gap=0x40), CUT)
    check_disjoint(result, READ_ONLY["fmenu"])
    return result


def seed_front(env, rng):
    """The arena starts zeroed (the slab is), the fake vtables and the strings are laid out."""
    from opentony.fcall import write_chunks

    env.alloc_base = ARENA
    env.alloc_size = ARENA_SIZE
    write_chunks(env.slab + VT_SPRITE, struct.pack("<IIII", VIRTUAL_DELETE, 0, 0, SPRITE_DRAW))
    write_chunks(env.slab + VT_ENTRY, struct.pack("<III", VIRTUAL_DELETE, 0, NAME_ENTRY_DRAW))
    for i, text in enumerate(STRING_POOL):
        write_chunks(env.slab + STRINGS + STRING_STRIDE * i, text + b"\0")
    write_chunks(env.slab + SPRITE, struct.pack("<I", env.slab + VT_SPRITE))
    write_chunks(env.slab + ENTRY, struct.pack("<I", env.slab + VT_ENTRY))
    return {}


_RO = front_read_only()
for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = front_regions
    READ_ONLY[_group] = _RO
    SEEDS[_group] = seed_front


# ---- stubs ------------------------------------------------------------------------------------------------------------------------------------------------------------

UNDER_TEST: set = set()      # the functions the selected groups call: a stub on one would intercept the forced call itself


def ensure(fc, address, args, name=None, **kw):
    if address in UNDER_TEST:
        return
    name = name or "engine_%08x" % address
    for probe in [p for p in fc._probes if getattr(p, "address", None) == address and p.enabled]:
        probe.enabled = False
    fc.stub(address, args, name, **kw)


def engine(fc, address, args, thiscall=False, **kw):
    kw.setdefault("convention", "thiscall" if thiscall else "cdecl")
    if thiscall:
        kw.setdefault("this", True)
    ensure(fc, address, args, **kw)


def install_front_stubs(fc, env):
    """The services the front end reaches: the text calls, sound, the engine's per-frame functions, the career and competition calls, the replay and save-card calls."""
    from opentony.memory import mem

    import os

    from create_specs import SPECS

    if getattr(fc, "_front_installed", False):
        return
    fc._front_installed = True
    selected = [g for g in os.environ.get("P410_GROUPS", "").split(",") if g]
    UNDER_TEST.clear()
    UNDER_TEST.update(s.address for s in SPECS if s.group in selected)
    rng = env.rng
    # the clock callback must not tick while a case runs
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    # allocation and files
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x005002FE, 1, "crt_new", result=env.alloc)
    ensure(fc, 0x005002F3, 1, "crt_delete")
    ensure(fc, 0x00448DD0, 2, "file_open", text=True, result=lambda a: 1 + rng.randrange(0x1000))
    # sound, fonts
    ensure(fc, 0x004AD620, 3, "sfx")
    ensure(fc, 0x00449CA0, 0, "font_construct", convention="thiscall", this=True)
    ensure(fc, 0x0044A060, 0, "font_destroy", convention="thiscall", this=True)
    engine(fc, 0x0044A070, 4, thiscall=True, text=2, stack_to=SCRATCH_LINE)                       # Font_Draw(x, y, text, 0)
    # the text calls (the colour and justification arguments keep only their low byte)
    engine(fc, 0x00473650, 0)
    engine(fc, 0x004734F0, 1)
    engine(fc, 0x004734B0, 1, masks={0: 0xFF})
    engine(fc, 0x00473510, 1)
    engine(fc, 0x00473580, 4, masks={0: 0xFF, 1: 0xFF, 2: 0xFF})
    engine(fc, 0x004739E0, 4, text=2, stack_to=SCRATCH_LINE)
    engine(fc, 0x004DF030, 0)
    engine(fc, 0x004DC0C0, 0)
    engine(fc, 0x004DC020, 1)
    engine(fc, 0x004DD520, 0)
    engine(fc, 0x0048B620, 8)                                                                      # Panel_DrawFlatShadedPoly
    # pads, the shake motors, the sound pause
    engine(fc, 0x00489670, 1)
    engine(fc, 0x00489850, 2)
    engine(fc, 0x004AD9C0, 0)
    engine(fc, 0x004CBF30, 1)                                                                      # Replay_ResetSkaterInput
    # the trigger restarts
    engine(fc, 0x004C4C50, 1, text=True)
    engine(fc, 0x004C4E30, 0)
    # the screen frame (Front_DisplayScreen, the loader's waits)
    engine(fc, 0x004F7CE0, 0, writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    engine(fc, 0x004F5FF0, 0)
    engine(fc, 0x004DAE00, 0)
    engine(fc, 0x004304B0, 0)
    engine(fc, 0x00466BE0, 0)
    engine(fc, 0x00430510, 0)
    engine(fc, 0x004E8740, 1)
    engine(fc, 0x004C9300, 1)
    engine(fc, 0x004E8730, 4, stack_to=SCRATCH_LINE, blob=(0, 8))
    engine(fc, 0x004D74A0, 3, thiscall=True, result=lambda a: env.alloc_log[-1][0])
    engine(fc, 0x004D78C0, 0, thiscall=True)
    engine(fc, 0x004D7990, 3, thiscall=True)
    # the fake objects' virtual methods
    ensure(fc, VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    ensure(fc, SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    ensure(fc, NAME_ENTRY_DRAW, 2, "name_entry_draw", convention="thiscall", this=True)
    # the competition, the career and the screens other files own (seams)
    engine(fc, 0x004DE010, 1, result=lambda a: rng.choice([0, 1]))                                  # the player's pad press (the winner pulse)
    engine(fc, 0x004AC010, 0, result=lambda a: rng.choice([-1, 0, 1, 2, 5]))                       # the music track count
    engine(fc, 0x0041B590, 0, thiscall=True)
    engine(fc, 0x0041AF40, 0, thiscall=True)
    engine(fc, 0x0041A780, 0, thiscall=True)
    engine(fc, 0x0041A270, 0, thiscall=True)
    engine(fc, 0x004B8EF0, 0)
    # the falling-text effect and the name entry
    engine(fc, 0x004B9820, 0, thiscall=True)
    engine(fc, 0x004B9D50, 1, thiscall=True)
    engine(fc, 0x004B9D60, 1, thiscall=True)
    engine(fc, 0x004B95A0, 1, thiscall=True)
    engine(fc, 0x004B9DE0, 0, thiscall=True, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004B9790, 0, thiscall=True)
    engine(fc, 0x004B9780, 1, thiscall=True)
    engine(fc, 0x00471E30, 11, thiscall=True, result=lambda a: env.alloc_log[-1][0])
    engine(fc, 0x00472930, 1, thiscall=True, text=True)
    engine(fc, 0x00472A70, 3, thiscall=True, text=True)
    engine(fc, 0x004DE040, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x00452A80, 1, result=lambda a: rng.choice([0, 0, 1]))                               # the unlock checks
    # the pause menu's services (the update group)
    engine(fc, 0x004DE0A0, 2, result=lambda a: rng.choice([0, 0, 0, 1]))
    engine(fc, 0x004DE150, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004F3980, 1, result=lambda a: rng.randrange(-0x1000, 0x1001))
    engine(fc, 0x004DF6D0, 0)
    engine(fc, 0x004AF6B0, 1, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x004DEFC0, 0)
    engine(fc, 0x00466C10, 0)
    engine(fc, 0x004195E0, 0)                                                                      # (the caller leaves garbage in ecx: not recorded)
    engine(fc, 0x004498A0, 2)
    engine(fc, 0x004498F0, 0)
    engine(fc, 0x00449800, 0, result=lambda a: rng.choice([0, 1, 1]))
    engine(fc, 0x004ABF10, 1)
    engine(fc, 0x0047A0B0, 3)
    engine(fc, 0x0047A480, 4)
    engine(fc, 0x00448C60, 0)
    engine(fc, 0x00448C90, 0)
    engine(fc, 0x00432840, 0)
    engine(fc, 0x00432850, 0)
    engine(fc, 0x00432860, 2, stack_to=SCRATCH_LINE, pokes=[(1, None, lambda a: struct.pack("<II", rng.randrange(0x100000, 0x200000), rng.randrange(0x1000, 0x8000)))])
    # the level-end screens
    engine(fc, 0x0044E0F0, 0)
    engine(fc, 0x0044E9C0, 0)
    # the update's other services
    engine(fc, 0x004140C0, 1, result=lambda a: rng.choice([0, 0, 1]))                               # Career_JustGot
    engine(fc, 0x00414030, 1, result=lambda a: rng.choice([0, 1, 1]))                               # Career_Got
    engine(fc, 0x00415A90, 2, result=lambda a: rng.randrange(0, 14))          # Career_HighestOpenLevel
    engine(fc, 0x004CA890, 3, result=lambda a: rng.randrange(0, 41))                                # Utils_Pulse
    engine(fc, 0x004AD9F0, 1)
    engine(fc, 0x004ABC80, 0)
    engine(fc, 0x004AD9E0, 0)
    engine(fc, 0x004AD9D0, 0)
    engine(fc, 0x00419CC0, 1, thiscall=True, masks={0: 0xFF})                                       # Competition_AiScore
    engine(fc, 0x00419FB0, 0, thiscall=True, result=lambda a: rng.choice([0, 1]))                   # Competition_NextHuman
    engine(fc, 0x0041B540, 0, thiscall=True)
    engine(fc, 0x0041A610, 2, thiscall=True, stack_to=SCRATCH_LINE)
    engine(fc, 0x0041A040, 2, thiscall=True, stack_to=SCRATCH_LINE)
    engine(fc, 0x0047AC60, 0, result=lambda a: rng.choice([0, 0, 1, 2, 0xD]))
    engine(fc, 0x0047ACE0, 0)
    engine(fc, 0x0047AD50, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x0047ADB0, 0, result=lambda a: rng.choice([0, 1]))
    engine(fc, 0x0047AE10, 1, result=lambda a: rng.choice([0xD, 0xE, 8, 1, 0xB]))
    engine(fc, 0x0047A220, 4)
    engine(fc, 0x0047B1A0, 1, text=True)
    engine(fc, 0x004CB6E0, 0)
    engine(fc, 0x004CBC90, 0)
    engine(fc, 0x004AB9E0, 0)
    engine(fc, 0x004ABCB0, 1)
    engine(fc, 0x004ABE20, 1)
    engine(fc, 0x004ABF80, 1)
    engine(fc, 0x004AC370, 0, result=lambda a: rng.choice([-1, 0, 0, 1]))
    engine(fc, 0x0046F650, 0, result=lambda a: rng.choice([0, 1, 2, 3]))
    engine(fc, 0x00472CC0, 1, thiscall=True, result=lambda a: rng.choice([0, 0, 1]))
    engine(fc, 0x00472920, 0, thiscall=True, result=lambda a: env.slab + STRINGS + STRING_STRIDE * rng.randrange(0, 20))
    engine(fc, 0x004DE570, 1)
    engine(fc, 0x00489A10, 0, writes=[(0x0056B028, 1, lambda a: 0), (0x0056B194, 1, lambda a: 0), (0x0056B0D8, 1, lambda a: 0), (0x0056B244, 1, lambda a: 0)])
    engine(fc, 0x00416DE0, 0, result=lambda a: rng.choice([0, 1, 3]))
    engine(fc, 0x00416CD0, 0, result=lambda a: rng.randrange(0, 5))
    engine(fc, 0x004B0C30, 0, thiscall=True, result=lambda a: rng.randrange(0, 21))
    engine(fc, 0x004168B0, 2)
    engine(fc, 0x00416E30, 1, result=lambda a: env.slab + STRINGS + STRING_STRIDE * rng.randrange(0, 20))
    engine(fc, 0x00452D00, 0)
    ensure(fc, 0x0046F4D0, 1, "mem_delete")
    # the level loader's services (the fld group)
    engine(fc, 0x004B0C60, 1, thiscall=True, result=lambda a: rng.randrange(0, 20))                # Skater_GetResourceSlot
    engine(fc, 0x004B1430, 1, thiscall=True, result=lambda a: rng.choice([0, 0, 0x1234]))          # Skater_GetSlotRecord
    engine(fc, 0x004B14A0, 1, thiscall=True)                                                       # SkaterManager_SwapCustomRecord
    engine(fc, 0x004B1980, 1, result=lambda a: rng.randrange(0, 20))                               # Skater_MapEnabledIndex
    engine(fc, 0x004B18D0, 1)                                                                      # SkaterManager_SelectCharacterChecked
    engine(fc, 0x004B50A0, 2, result=lambda a: rng.randrange(0, 0x100))
    engine(fc, 0x0042F880, 0, thiscall=True, result=lambda a: env.alloc_log[-1][0])                # CustomDefinition_Construct
    engine(fc, 0x0042F950, 3, thiscall=True)                                                       # CustomSkater_LoadDefinitionPsx
    engine(fc, 0x00423800, 6, result=lambda a: rng.randrange(0x100000, 0x200000))                  # CustomSkater_Create
    engine(fc, 0x004203C0, 0, thiscall=True, result=lambda a: rng.randrange(0, 12))                # MySkater__CheckedRegion
    engine(fc, 0x004B0CD0, 1, thiscall=True, result=lambda a: rng.randrange(0, 5))                 # SkaterManager_GetCostume
    engine(fc, 0x004B1700, 2, result=lambda a: rng.randrange(0x520000, 0x540000))                  # Skater_SelectModelName
    engine(fc, 0x004B43E0, 2, result=lambda a: rng.randrange(0, 12))                               # Skater_LoadModelFile
    engine(fc, 0x004B1810, 1, result=lambda a: rng.randrange(0, 20))                               # Skater_CustomSlotIndex
    engine(fc, 0x004B0C30, 0, thiscall=True, result=lambda a: rng.randrange(0, 21))
    engine(fc, 0x004B18A0, 1, result=lambda a: rng.randrange(0, 20))                               # Skater_SelectManagerSlot
    engine(fc, 0x004B37A0, 4, text=True, result=lambda a: rng.randrange(0, 12))                    # PSX_QueueResource
    engine(fc, 0x004B3DF0, 0)
    engine(fc, 0x0045D830, 1)
    engine(fc, 0x004DFE50, 0)
    engine(fc, 0x00469400, 0, result=lambda a: rng.choice([0, 0, 0, 1]))
    engine(fc, 0x00466A00, 0)
    engine(fc, 0x004301B0, 0)
    engine(fc, 0x004C9320, 1)
    engine(fc, 0x00448F40, 1)
    engine(fc, 0x004AD020, 1, text=True)
    engine(fc, 0x004AD180, 1, text=True)
    engine(fc, 0x004ADA20, 1)
    engine(fc, 0x004302C0, 4)
    engine(fc, 0x004C5130, 1, text=True)
    engine(fc, 0x004C8050, 0)
    engine(fc, 0x004C8130, 0)
    engine(fc, 0x004AA5D0, 0)
    engine(fc, 0x004CDDC0, 0)
    engine(fc, 0x004B46A0, 2, text=True, result=lambda a: rng.randrange(0, 12))
    engine(fc, 0x004A8EE0, 0)
    engine(fc, 0x004A90F0, 2, thiscall=True, text=True)
    engine(fc, 0x00457420, 4, thiscall=True, text=True, result=lambda a: env.alloc_log[-1][0])
    engine(fc, 0x00457120, 0, thiscall=True)
    engine(fc, 0x0048AD50, 0)
    engine(fc, 0x004A9330, 1, thiscall=True, text=True)
    engine(fc, 0x004A8F90, 0)
    engine(fc, 0x00403680, 0)
    engine(fc, 0x004691E0, 0)
    engine(fc, 0x00432330, 0)
    engine(fc, 0x00489610, 0)
    engine(fc, 0x0045DB40, 0)
    engine(fc, 0x0042FC70, 0)
    engine(fc, 0x00416740, 1, result=lambda a: rng.choice([0, 0, 1]))                              # Career_GameCleared
    engine(fc, 0x00415590, 0, result=lambda a: rng.choice([0, 1]))                                 # Career_GotAllGaps
    engine(fc, 0x00416F80, 1)                                                                      # Career_UnlockCharacter
    engine(fc, 0x0047AC40, 0, result=lambda a: rng.choice([0, 1]))


GROUP_STUBS = {group: install_front_stubs for group in GROUPS}
