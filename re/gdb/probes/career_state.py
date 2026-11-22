"""Memory regions and state samplers for the Phase 4.8 career / skater-manager forced-call probe.

The regions are the retail addresses the native career code (src/runtime/career*.cpp,
skater_manager.cpp) reads or writes.  `regions()` also adds the ones that hang off pointers (the
skater objects, the gap table, the trigger node list, TRICKS.BIN).  The samplers produce pokes
(address -> bytes) that put the career in a random but legal state.
"""

from __future__ import annotations

import struct

from opentony.memory import mem

# (base, size): addresses are the retail (runtime) addresses.
#
# READ_ONLY regions are captured once: the code under test never writes them (and they are the
# bulk of the bytes, so skipping them after every call roughly halves a case's memory traffic).
READ_ONLY = [
    (0x005185D8, 0x200),   # competition judging rows
    (0x00524ED8, 0x140),   # default high-score initials
    (0x0052E2E8, 4),       # pointer variables of texts
    (0x0052E3A4, 0x50),    # cheat name pointers
    (0x0052E98C, 4),
    (0x0052EA98, 4),
    (0x0053127C, 8),       # "HORSE"
    (0x00532E38, 0x14),    # "Cash Icons Cleared"
    (0x00537B84, 4),
    (0x00541990, 0x4780),  # the early levels' gap definitions (415 records of 0x2c and the terminator, up to the late table at 0x546110): the gapc spec points the table pointer here
    (0x0053C1F8, 0x53E800 - 0x0053C1F8),   # the level table and the text it points at
]
# Mutable: the code writes them.
STATIC = [
    (0x00525774, 0x10),    # competition score anchors
    (0x0052BE44, 8),       # mixer volumes derived from the options
    (0x0052CB3C, 4),
    (0x00533F38, 0x70),    # game mode, 0x533f9c / 0x533fa0
    (0x005614C4, 4),       # the custom park flag
]
LEVELS = 0x0053C1F8

# The skater table: only each row's unlocked dword and flags (+0x158, +0x15c) are ever written;
# they are the mutable windows, the rest of the table is captured once.
SKATER_TABLE = (0x0053A3A4, 0x0053C1F8)
SKATER_WINDOWS = [(0x0053A3B8 + k * 0x170 + 0x158, 8) for k in range(21)]
LEVEL_STRIDE = 0x1AC
LIVE = [
    (0x0055FAF0, 0x5A0),   # live career globals (goal masks, gap counts, options, scratch text) and the competition object
    (0x00561CE4, 4),
    (0x00564378, 8),
    (0x00568608, 0x20),    # cash icons got while a movie plays
    (0x00568A68, 0x1E00),  # career save image: records, cheats, options, high scores
    (0x0056A868, 0xE0),    # cheat flags, current level, all-levels cheat
    (0x0056ACF8, 4),
    (0x0056B760, 4),
    (0x0056B7F4, 4),
    (0x0056B154, 0x17C),   # controller view pointers
    (0x0056BB34, 4),
    (0x0056C580, 0x6A0),   # skater manager
    (0x0056DB5C, 4),       # the tag game's "it" skater
    (0x0056DB8C, 4),
    (0x0056DD58, 4),       # gap table pointer
    (0x0056E210, 8),       # node list pointer and count
]

RECORDS = 0x00568A6C
RECORD_STRIDE = 0x104
MANAGER = 0x0056C580
CUSTOM = 0x0056A488
PLAYERS = 0x0056A858
CURRENT_LEVEL = 0x0056A898
GAME_MODE = 0x00533F38
ROWS = 0x0053A3B8
ROW_STRIDE = 0x170
PLAYER_SIZE = 0x3538
# The skater object fields the ported code reads or writes: the +0x28 scale words, the score
# (+0x16c) and the competition counters after it, the character index and the tape counters.
PLAYER_FIELDS = [(0x28, 8), (0x16C, 0x10), (0x2CC0, 4), (0x3024, 8), (0x31E4, 8)]   # (+0x3024 / +0x3028: the gap definitions a completed gap parks on the player)
TRICKS_BASE_POINTER = 0x0056A894
TRICKS_SIZE = 33168


NODE_SAMPLE = 32  # the money specs poke the node count down to this many, so only these nodes are captured


def read_only_regions(groups=()) -> list[tuple[int, int]]:
    out = list(READ_ONLY)
    tricks = mem.u32(TRICKS_BASE_POINTER)
    if tricks and ("reset" in groups or "save" in groups):
        # TRICKS.BIN as loaded: Career_ResetSaveImage builds the trick database from it (and only
        # re-lowercases names that are already lowercase)
        out.append((tricks, TRICKS_SIZE))
    # the skater table between its mutable windows
    cursor = SKATER_TABLE[0]
    for base, size in SKATER_WINDOWS:
        if base > cursor:
            out.append((cursor, base - cursor))
        cursor = base + size
    if cursor < SKATER_TABLE[1]:
        out.append((cursor, SKATER_TABLE[1] - cursor))
    return merge(out, gap=0)


def regions(groups=()) -> list[tuple[int, int]]:
    """The regions the code under test may write (captured before and after every call)."""
    out = list(STATIC)
    out += SKATER_WINDOWS
    out += LIVE
    for slot in range(2):
        pointer = mem.u32(PLAYERS + slot * 4)
        if pointer:
            out += [(pointer + offset, size) for offset, size in PLAYER_FIELDS]
    table = mem.u32(0x0056DD58)
    if table:
        count = 0
        while mem.s16(table + count * 0x2C + 4) != -1:
            count += 1
        out.append((table, (count + 1) * 0x2C))
    nodes = mem.s32(0x0056E214)
    array = mem.u32(0x0056E210)
    if array and nodes > 0:
        sample = min(nodes, NODE_SAMPLE)
        out.append((array, sample * 4))
        for i in range(sample):
            out.append((mem.u32(array + i * 4), 8))
    return merge(out)


def merge(items, gap: int = 0x40):
    """Sort and merge regions that touch, overlap or lie within `gap` bytes (one memory read each)."""
    items = sorted(items)
    merged: list[list[int]] = []
    for base, size in items:
        if merged and base <= merged[-1][0] + merged[-1][1] + gap:
            end = max(merged[-1][0] + merged[-1][1], base + size)
            merged[-1][1] = end - merged[-1][0]
        else:
            merged.append([base, size])
    return [(b, s) for b, s in merged]


# ---- pokes --------------------------------------------------------------------------------------

def u32(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFFFFFF)


def u16(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


def u8(v: int) -> bytes:
    return struct.pack("<B", v & 0xFF)


class Sampler:
    """Random legal career state as pokes."""

    def __init__(self, rng, levels_contest):
        self.rng = rng
        self.contest = levels_contest  # list of 13 bools: the level table's contest flags
        self.pokes: dict[int, bytes] = {}

    def put(self, address: int, data: bytes) -> None:
        self.pokes[address] = data

    def record(self, character: int) -> None:
        rng = self.rng
        rec = RECORDS + character * RECORD_STRIDE
        self.put(rec + 0x00, u8(rng.choice([0, rng.randrange(0, 40), 0x32, rng.randrange(0, 256)])))
        self.put(rec + 0x01, u8(rng.choice([0, 0x40, rng.randrange(0, 256)])))
        self.put(rec + 0x03, u8(rng.choice([0, 1, 2, 3, 3, rng.randrange(0, 256)])))
        self.put(rec + 0x04, u32(rng.choice([0, rng.randrange(0, 2_000_000), 900000])))
        self.put(rec + 0x08, u32(rng.choice([0, rng.randrange(0, 2_000_000), 500000])))
        masks = b""
        for level in range(13):
            m = rng.getrandbits(10) if rng.random() < 0.6 else rng.choice([0, 0x3FF])
            if self.contest[level] or rng.random() < 0.3:
                m |= rng.choice([0, 0x400, 0x800, 0x1000, 0x400 | 0x800, 0x1C00])
            if rng.random() < 0.25:
                m |= 0x8000
            masks += u16(m)
        self.put(rec + 0x0C, masks)
        self.put(rec + 0x26, bytes(rng.randrange(0, 256) for _ in range(13)))
        self.put(rec + 0x34, u32(rng.choice([1, 0x3FF, rng.getrandbits(10)])))
        self.put(rec + 0x38, bytes(rng.choice([0, rng.randrange(0, 10), rng.randrange(0, 13), 0xFF]) for _ in range(10)))
        money = b""
        for _ in range(26):
            money += u32(rng.choice([0, 0xFFFFFFFF, rng.getrandbits(32), rng.getrandbits(32) & rng.getrandbits(32)]))
        self.put(rec + 0x44, money)
        self.put(rec + 0xAC, b"".join(u32(rng.getrandbits(32)) for _ in range(4)))

    def all_records(self, count: int = 21, sample: int = 3) -> None:
        """Random state for a handful of characters (the rest keep their base state: poking every
        record would be thousands of memory writes per case)."""
        for character in self.rng.sample(range(count), min(sample, count)):
            self.record(character)

    def manager(self, loaded: bool = False) -> int:
        """Random slot entries; returns the active slot's character."""
        rng = self.rng
        active = rng.randrange(0, 8)
        self.put(MANAGER + 0x64, u32(active))
        chars = []
        for slot in range(8):
            ch = rng.randrange(0, 20)
            chars.append(ch)
            self.put(MANAGER + 4 + slot * 12, u32(ch) + u32(rng.randrange(0, 4)) + u32(rng.randrange(0, 4)))
        self.put(MANAGER + 0x68, b"".join(u32(rng.randrange(0, 4)) for _ in range(20)))
        if loaded:
            # loaded-skater flags for every slot, loaded records for a few (each is 0x80 bytes of pokes)
            for slot in range(8):
                self.put(MANAGER + 0xB8 + slot * 0xBC, u32(rng.choice([0, 0, 1])))
            for slot in rng.sample(range(8), 3):
                self.put(MANAGER + 0xF4 + slot * 0xBC, bytes(rng.randrange(0, 256) for _ in range(0x80)))
        return chars[active]

    def level(self, value: int | None = None) -> int:
        value = self.rng.randrange(0, 13) if value is None else value
        self.put(CURRENT_LEVEL, u32(value))
        return value

    def game_mode(self, value: int = 1) -> None:
        self.put(GAME_MODE, u32(value))
