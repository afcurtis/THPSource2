"""Function-level oracles for the natives milestone 7c added to the standalone (Phase 5, milestone 8d).

The same script language and the same two backends as the texture and PSX oracles (`src/hooks/oracle_script.h`): the real retail functions in the hooked process with hooks
off, the natives in `opentony_spike.exe`. Each function gets cases that a screen run never tells apart: `PRE_Unload` (which slot, what the manager keeps), `FileIO_Exists`
(the base directory, the sub directory, the loose-file overlay, the package), `Animation_Cycle` (the fields it writes, the byte truncations), and
`SimulationTimeTimerCallback` (the x87 arithmetic of the clock, with and without a pause), and `NetMouse_HitRect` (the rectangle test every pick-by-mouse method ends in).
The pick-by-mouse methods themselves (vtable slot 7 of the window-graph classes) need window objects with children and the mouse state a screen leaves: not here.
"""

from __future__ import annotations

import struct

from . import texture_oracle as base

OUT_ROOT = base.ROOT / "build/oracle/natives"

PRE_SLOTS = 16
PRE_SIZE = 0x44 + 0x10 * PRE_SLOTS + 0x10
FILE_NAME = 0x00561590
DIR_LENGTH = 0x0056151C
SUB_DIR = 0x00561574
REGION_TABLES = 0x0056D444
TIMER_STATE = 0x006A05A0
MOUSE_X = 0x006A13F0                      # the pointer in the 320 x 240 menu space
MOUSE_Y = 0x006A13F4


def double(value: float) -> str:
    return struct.pack("<d", value).hex()


def pre_unload(s: base.Script) -> None:
    s.comment("PRE_Unload: the first slot with the name is blanked and its buffer freed")
    names = ["alpha.pre", "beta.pre", "gamma.pre", "alpha.pre", "delta"]
    for case, (loaded, wanted) in enumerate((
        ([0, 1, 2], "beta.pre"),            # a slot in the middle
        ([0, 1, 2], "alpha.pre"),           # the first slot
        ([3, 4], "alpha.pre"),              # a name in the last used slot of two
        ([0, 3], "alpha.pre"),              # the name in two slots: the first one only
        ([15], "delta"),                    # the last slot
    )):
        prefix = f"pu{case}"
        s.add("alloc", f"{prefix}_pre", PRE_SIZE)
        buffers: dict[int, str] = {}
        for slot in loaded:
            s.add("bytes", f"{prefix}_pre+{0x44 + 0x10 * slot}", (names[slot] if slot < len(names) else "delta").encode().hex() + "00")
            if slot != 4:                    # (a loaded name with no buffer is allowed: the free is skipped)
                s.add("call", f"{prefix}_b{slot}={base.ALLOC_LOADED_BUFFER:#x}", "c", 64, 0, 1, 0)
                s.add("set32", f"{prefix}_pre+{4 + 4 * slot}", f"{prefix}_b{slot}")
                buffers[slot] = f"{prefix}_b{slot}"
        s.text(f"{prefix}_name", wanted)
        s.add("call", "0x4a9330", "t", f"{prefix}_pre", f"{prefix}_name")
        for slot in range(PRE_SLOTS):
            s.add("mem", f"{prefix}_slot{slot}_name", f"{prefix}_pre+{0x44 + 0x10 * slot}", 16)
            s.add("same", f"{prefix}_slot{slot}_buffer", f"[{prefix}_pre+{4 + 4 * slot}]", buffers.get(slot, "0"))      # (the buffer kept, or ...)
            s.add("same", f"{prefix}_slot{slot}_cleared", f"[{prefix}_pre+{4 + 4 * slot}]", "0")                        # (... nothing: a slot that was unloaded)
    # the manager's first dword and the bytes between the tables are not touched
    s.add("mem", "pu_head", "pu4_pre", 4)


def file_exists(s: base.Script) -> None:
    s.comment("FileIO_Exists: the base directory (and the sub directory) and the name are joined in FileName, then the backend opens it")
    s.add("let", "fe_old_length", f"[{DIR_LENGTH}]")
    s.add("fill", FILE_NAME, 0, 0x40)
    s.add("bytes", FILE_NAME, (b"data\\").hex() + "00")
    s.add("set32", DIR_LENGTH, 5)
    s.add("fill", SUB_DIR, 0, 16)
    for label, sub, name in (
        ("mod", "", "NEWBMP\\o8.bmp"),                 # a file only the loose-file overlay has
        ("missing", "", "NEWBMP\\nothere.bmp"),
        ("sub", "NEWBMP\\", "o24.bmp"),                # the sub directory in front of the name
        ("sub_missing", "NEWBMP\\", "nothere.bmp"),
        ("package", "", "SKWARE_T.TRG"),               # a file the package has
        ("package_absent", "", "NOT_THERE.TRG"),
    ):
        s.add("fill", SUB_DIR, 0, 16)
        if sub:
            s.add("bytes", SUB_DIR, sub.encode().hex() + "00")
        s.add("fill", f"{FILE_NAME}+5", 0, 0x20)
        s.text(f"fe_{label}_name", name)
        s.add("call", f"fe_{label}_result=0x449460", "c", f"fe_{label}_name")
        s.add("val", f"fe_{label}_answer", f"fe_{label}_result")
        s.add("mem", f"fe_{label}_filename", FILE_NAME, 0x30)
    s.add("fill", SUB_DIR, 0, 16)
    s.add("set32", DIR_LENGTH, "fe_old_length")


def animation_cycle(s: base.Script) -> None:
    s.comment("Animation_Cycle: the animation id, the direction byte and the fields the object is reset to")
    s.add("let", "ac_old0", f"[{REGION_TABLES}]")
    s.add("let", "ac_old1", f"[{REGION_TABLES + 0x44}]")
    s.add("alloc", "ac_t0", 0x60)
    s.add("pattern", "ac_t0", 0x60, 3)
    s.add("set32", "ac_t0", 6)
    s.add("alloc", "ac_t1", 0x60)
    s.add("pattern", "ac_t1", 0x60, 9)
    s.add("set32", "ac_t1", 3)
    s.add("set32", REGION_TABLES, "ac_t0")
    s.add("set32", REGION_TABLES + 0x44, "ac_t1")
    for case, (region, animation, direction) in enumerate(((0, 0, 0), (0, 5, 1), (1, 2, 0xFF), (0, 3, 0x1FF), (1, 0, 0x80), (0, 0x10003 - 0x10000, 2))):
        s.add("alloc", f"ac{case}_self", 0x140)
        s.add("pattern", f"ac{case}_self", 0x140, 40 + case)
        s.add("set8", f"ac{case}_self+0x1f", region)
        s.add("call", "0x480890", "t", f"ac{case}_self", animation, direction)
        s.add("mem", f"ac{case}_object", f"ac{case}_self", 0x140)
    s.add("set32", REGION_TABLES, "ac_old0")
    s.add("set32", REGION_TABLES + 0x44, "ac_old1")


def timer_callback(s: base.Script) -> None:
    s.comment("SimulationTimeTimerCallback: one delivery of the clock's tick on the game's TimerState")
    s.add("let", "tc_old_interval", f"[{TIMER_STATE + 4}]")
    s.add("let", "tc_old_sum", f"[{TIMER_STATE + 0xC}]")
    for case, (interval, total, frame, game, pause, pause2) in enumerate((
        (16, 1000, 0.0, 0.0, 0, 0),
        (16, 1016, 123.456, 98.7654321, 0, 0),
        (17, 77, 1.0e6 + 0.5, 3.0e5 + 0.25, 1, 0),          # paused: the game accumulator keeps its value
        (15, 5, 2.5, 7.75, 0, 1),
        (16, 0xFFFFFFF0, 4294967295.5, 2147483647.25, 0, 0),    # the sum wraps; the accumulators reach the limits of the int conversion
        (1, 0, 0.9999999, 0.5000001, 0, 0),
    )):
        s.add("set32", TIMER_STATE + 4, interval)
        s.add("set32", TIMER_STATE + 0xC, total)
        s.add("bytes", 0x006A0590, double(frame))
        s.add("bytes", 0x006A0598, double(game))
        s.add("set32", 0x00561C04, pause)
        s.add("set32", 0x0056A8E0, pause2)
        s.add("tick")
        s.add("val", f"tc{case}_sum", f"[{TIMER_STATE + 0xC}]")
        s.add("mem", f"tc{case}_step", 0x006A05B8, 8)
        s.add("mem", f"tc{case}_frame_accumulator", 0x006A0590, 8)
        s.add("mem", f"tc{case}_game_accumulator", 0x006A0598, 8)
        s.add("val", f"tc{case}_frame_clock", "[0x56e31c]")
        s.add("val", f"tc{case}_game_clock", "[0x56e320]")
    s.add("set32", TIMER_STATE + 4, "tc_old_interval")
    s.add("set32", TIMER_STATE + 0xC, "tc_old_sum")
    s.add("set32", 0x00561C04, 0)
    s.add("set32", 0x0056A8E0, 0)


def hit_rect(s: base.Script) -> None:
    s.comment("NetMouse_HitRect: the pointer (in the 320 x 240 menu space) inside a rectangle, edges included, signed compares; the answer is a byte")
    s.add("let", "hr_old_x", f"[{MOUSE_X}]")
    s.add("let", "hr_old_y", f"[{MOUSE_Y}]")
    for case, (x, y, rectangle) in enumerate((
        (100, 50, (100, 50, 0, 0)),             # the corner of an empty rectangle
        (100, 50, (90, 40, 20, 20)),
        (110, 60, (90, 40, 20, 20)),            # the far corner
        (111, 60, (90, 40, 20, 20)),            # one past it
        (89, 45, (90, 40, 20, 20)),             # one before
        (0, 0, (-5, -5, 10, 10)),               # a negative origin
        (-6, 0, (-5, -5, 10, 10)),
        (50, 50, (60, 40, -20, 30)),            # a negative width (signed: nothing is inside)
        (320, 240, (300, 220, 20, 20)),
        (0xFFFFFFF0, 10, (0xFFFFFFE0, 0, 0x30, 0x20)),   # a pointer left of the screen, as a signed dword
    )):
        s.add("set32", MOUSE_X, x & 0xFFFFFFFF)
        s.add("set32", MOUSE_Y, y & 0xFFFFFFFF)
        s.add("call", f"hr{case}=0x4dbf90", "c", *(value & 0xFFFFFFFF for value in rectangle))
        s.add("maskval", f"hr{case}_answer", f"hr{case}", 0xFF)
    s.add("set32", MOUSE_X, "hr_old_x")
    s.add("set32", MOUSE_Y, "hr_old_y")


def build_script() -> str:
    s = base.Script()
    s.comment("function-level oracle of the natives of milestone 7c: tony/native_oracle.py")
    pre_unload(s)
    file_exists(s)
    animation_cycle(s)
    timer_callback(s)
    hit_rect(s)
    return "\n".join(s.lines) + "\n"


def oracle_command(_args=None) -> int:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    script = OUT_ROOT / "natives.oracle"
    script.write_text(build_script())
    print(f"script: {script} ({len(script.read_text().splitlines())} lines)")
    mods = base.write_mod()
    standalone = base.run_standalone(script, OUT_ROOT / "standalone.out", mods=mods)
    retail = base.run_retail(script, label="native-oracle", mods=mods, deliveries_after=40)
    (OUT_ROOT / "retail.out").write_text(retail)
    problems = base.compare(retail, standalone)
    for problem in problems:
        print(problem)
    print(f"{len(retail.splitlines())} lines compared, {len(problems)} differ")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(oracle_command())
