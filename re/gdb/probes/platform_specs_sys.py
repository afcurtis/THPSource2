"""Phase 4.13a probe: the game side of pc\\winmain.cpp, pc\\thstub.cpp, the PC file backends and the game-logic helpers of pc\\d3d*.cpp (groups winm, vopt, thst, d3dm).

Same shape as platform_specs.py: the functions work on the game's own globals (windows of the real address space poked per case) with the C runtime, the options file and the
engine calls stubbed. Strings the case needs (names, paths) go in the scratch slab. The options file's answers come from the case's queue (platform_specs.SETTINGS).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from platform_specs import SETTINGS, BindingTables, WINDOWS as PAD_WINDOWS, mark_under_test, setting_answer, stop_clock, u

KSTACK = 0x7FF00700            # what an argument that points at a stack local is recorded as
SCRATCH_A = 0x7FF00800
SCRATCH_B = 0x7FF00900

WINDOWS = [
    (0x029D8370, 0x110),        # the options file's name (0x029d8378)
    (0x029D8470, 0x800),        # the display modes DirectDraw listed
    (0x029DA370, 0x360),        # video memory, the screen size, the renderer flag, the three install paths
    (0x006A71B0, 0x420),        # the install and disc directories
    (0x0056E780, 0x80),         # the video options: mode names, the built flag, renderer, mode index, gamma, quality
    (0x00548580, 0x24),         # the pointers that name the resolutions, and the fog scale
    (0x00549F20, 0x10),         # the texture root flag
    (0x006A05A0, 0x40),         # the timer state and the clocks the frame-rate meter reads
    (0x0054A210, 0x10),         # the frame rate (a double)
    (0x0056E310, 0x20),         # the simulation tick (0x0056e31c)
    (0x0056E840, 0x10),         # the tick of the last scene
    (0x0056A890, 0x10),         # the current level (0x0056a898)
    (0x006A6D00, 0x60),         # the file slots: states (0x006a6d10..) and FILE pointers (0x006a6d1c..)
    (0x00560FD0, 0x10),         # the renderer pointer
    (0x006A0070, 0x10),         # the polygon heap: start, next cell, end, head of the reversed list
    (0x006A04C0, 0x20),         # the scene-active byte (0x006a04c4) and the texture record count (0x006a04d4)
    (0x006A0510, 0x20),         # the texture record list head and tail (0x006a051c, 0x006a0520)
    (0x006A00B4, 0x400), (0x005DD340, 0x400), (0x0069FC64, 0x400),          # the gamma tables
] + PAD_WINDOWS                 # (and the bindings the options file also holds: LoadOptions reads and writes them)
TABLES = [
    (0x005256C0, 0xC0), (0x00531000, 0x1000), (0x00540C30, 0x20), (0x005486B0, 0x60), (0x00549FC0, 0x20), (0x0055BD00, 0x300), (0x00548560, 0x20), (0x005485A4, 0x40),
    (0x00519720, 0x20), (0x00519930, 0x40), (0x00519980, 0x20), (0x00518D70, 0x10),
]



class SysTables:
    """The read-only tables of the system groups plus the key names the bindings read."""

    def __iter__(self):
        return iter(TABLES + list(BindingTables()))


for _group in ("winm", "vopt", "thst", "d3dm", "vwin"):
    SLAB_BYTES[_group] = 0x2000
    EXTRA_REGIONS[_group] = lambda slab: [(slab, 0x2000)] + WINDOWS
    READ_ONLY[_group] = SysTables()
    SEEDS[_group] = lambda env, rng: {}


def winm(address, name):
    return spec(address, name, "winm")


def vopt(address, name):
    return spec(address, name, "vopt")


def thst(address, name):
    return spec(address, name, "thst")


def d3dm(address, name, this=None, edx=None):
    return spec(address, name, "d3dm", this=(lambda env: env.slab + this) if this is not None else None, edx=edx)


def c_string(text):
    return text.encode("latin-1") + b"\0"


RESOLUTIONS = [(640, 480), (800, 600), (1024, 768), (1280, 1024), (320, 240), (512, 384)]
NAME_VARIABLES = [0x0054859C, 0x00548598, 0x00548594, 0x00548590, 0x0054858C, 0x00548588]      # the pointer variables that hold "320x240" ... "1280x1024"
PATHS = ["C:\\Games\\THPS2", "D:", "C:\\Program Files\\Activision\\Tony Hawk's Pro Skater 2", "", ".", "\\\\server\\share\\thps2"]


def mode_world(img, rng):
    """The display modes DirectDraw listed and the video memory."""
    count = rng.choice([0, 1, 3, 6, 6, 8])
    img.u32(0x029D8478, count)
    for k in range(8):
        width, height = rng.choice(RESOLUTIONS)
        base = 0x029D8484 + 0x7C * k
        img.u32(base, height)
        img.u32(base + 4, width)
    img.u32(0x029DA380, rng.choice([0, 0x100000, 0x400000, 0x800000, 0x2000000, 0xFFFFFFFF, rng.getrandbits(32)]))


def video_world(img, rng, *, built=None):
    """The video options variables: renderer, the mode name table, mode index, gamma, quality, fog scale, the names the file's text is compared with."""
    img.u32(0x0056E7C0, rng.choice([0, 1]))
    img.u8(0x0056E7B8, rng.choice([0, 1]) if built is None else built)
    img.u32(0x0056E7D8, rng.choice([0, 3, -3 & 0xFFFFFFFF, 15, -15 & 0xFFFFFFFF, rng.randrange(-16, 17) & 0xFFFFFFFF]))
    img.u32(0x0056E7E4, rng.choice([0, 1]))
    img.u32(0x0056E7AC, 0x00531420)         # "Normal"
    img.u32(0x0056E7B0, 0x005486C8)         # "High"
    from opentony.memory import mem

    names = [mem.u32(variable) for variable in NAME_VARIABLES]          # (the table holds the strings the pointer variables name)
    count = rng.choice([1, 2, 3, 4, 5])
    for k in range(6):
        img.u32(0x0056E788 + 4 * k, 0 if k >= count else rng.choice(names))
    img.u32(0x0056E7CC, rng.randrange(0, count) if img.b.get(0x0056E7B8, 1) else 0)          # (a table the loader rebuilds only keeps its first name for sure)
    img.u32(0x005485A0, struct.unpack("<I", struct.pack("<f", rng.choice([1.5, 0.5, 2.0, 3.37, 4.99, 0.0, 10.0, rng.uniform(0.1, 6.0)])))[0])
    mode_world(img, rng)


# ---- pc\winmain.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------------

@winm(0x004F6660, "WINMAIN_CanDoMode")
def gen_can_do_mode(env, n):
    img, rng = Image(), env.rng
    mode_world(img, rng)
    width, height = rng.choice(RESOLUTIONS + [(1600, 1200), (100, 100)])
    return [width, height, rng.choice([0, 1, 0x100, 0x101, 2])], img.pokes()


@winm(0x004F7D80, "WINMAIN_Nothing")
def gen_winmain_nothing(env, n):
    return [], {}


@winm(0x004F6D00, "WINMAIN_ScreenDimensions")
def gen_screen_dimensions(env, n):
    img, rng = Image(), env.rng
    img.u32(0x029DA394, rng.choice([640, 800, 1024, 320, rng.getrandbits(32)]))
    img.u32(0x029DA398, rng.choice([480, 600, 768, 240, rng.getrandbits(32)]))
    out = env.slab + 0x100
    img.u32(out, rng.getrandbits(32))
    img.u32(out + 4, rng.getrandbits(32))
    return [out, out + 4], img.pokes()


@winm(0x004F86F0, "WINMAIN_ShutDown")
def gen_shut_down(env, n):
    return [], {}


def path_world(img, rng):
    img.put(0x006A71BC, c_string(rng.choice(PATHS)))
    img.put(0x006A73BC, c_string(rng.choice(PATHS)))


@winm(0x004F6850, "Setup_ProbeConfiguredFile")
def gen_probe_file(env, n):
    img, rng = Image(), env.rng
    directory, name = env.slab + 0x100, env.slab + 0x300
    img.put(directory, c_string(rng.choice(PATHS)))
    img.put(name, c_string(rng.choice(["All.pkr", "LTIX30.dat", "Intro.dat", "", "x"])))
    return [directory, name], img.pokes()


@winm(0x004F6920, "WINMAIN_InstallSetup")
def gen_install_setup(env, n):
    img, rng = Image(), env.rng
    path_world(img, rng)
    SETTINGS.clear()
    SETTINGS.extend(rng.choice(PATHS) for _ in range(3))
    for address in (0x029DA3C4, 0x029DA4C4, 0x029DA5C4):
        img.put(address, c_string(rng.choice(PATHS)))
    return [], img.pokes()


def options_answers(rng):
    """The VERSION entry, then the five VIDEO entries (renderer, resolution, gamma, quality, fog)."""
    out = [rng.choice(["6.0", "6.0", "0.0", "5.5", ""])]
    out.append(rng.choice(["Hardware", "Software", "hardware", "Bogus", "SOFTWARE"]))
    out.append(rng.choice(["640x480", "800x600", "1024x768", "Bogus", "320x240", "512x384"]))
    out.append(rng.choice(["0", "5", "-5", "15", "16", "-15", "-16", "x", ""]))
    out.append(rng.choice(["Normal", "High", "normal", "Bogus"]))
    out.append(rng.choice(["150", "50", "49", "500", "501", "abc", "75", "300"]))
    return out


@winm(0x004F6C60, "WINMAIN_SaveOptions")
def gen_save_options(env, n):
    img, rng = Image(), env.rng
    video_world(img, rng)
    from platform_specs import chord_world, named_codes

    chord_world(img, rng, named_codes())
    for variable in (0x0054AE04, 0x0054AE08, 0x0054AE0C, 0x006A6984, 0x0054AE10, 0x0054AE18, 0x0054AE14, 0x0054AE1C, 0x0054AE20, 0x0054AE24):
        img.u32(variable, rng.choice([0xFFFFFFFF, 0, 1, 31, rng.randrange(0, 32)]))
    return [], img.pokes()


@winm(0x004F77F0, "D3D_DumpDeviceCaps")
def gen_dump_caps(env, n):
    img, rng = Image(), env.rng
    caps = env.slab + 0x200
    img.put(caps, bytes(rng.randrange(256) for _ in range(0xA0)))
    return [caps], img.pokes()


# ---- the video options -----------------------------------------------------------------------------------------------------------------------------------------------------

@vopt(0x004CF420, "WINMAIN_BuildDisplayModeList")
def gen_build_modes(env, n):
    img, rng = Image(), env.rng
    video_world(img, rng, built=rng.choice([0, 0, 0, 1]))
    return [], img.pokes()


@vopt(0x004CF300, "VideoOptions_Save")
def gen_save_video(env, n):
    img, rng = Image(), env.rng
    video_world(img, rng, built=1)
    return [], img.pokes()


@vopt(0x004CF030, "VideoOptions_Load")
def gen_load_video(env, n):
    img, rng = Image(), env.rng
    video_world(img, rng, built=rng.choice([0, 1]))
    answers = options_answers(rng)[1:]
    # the resolution answer names a mode of the table the loader reads (when it can)
    SETTINGS.clear()
    SETTINGS.extend(answers)
    return [], img.pokes()


@winm(0x004F6B90, "WINMAIN_LoadOptions")
def gen_load_options(env, n):
    from platform_specs import chord_world, key_names, named_codes, joystick_numbers, keyboard_text, number_text

    img, rng = Image(), env.rng
    video_world(img, rng, built=rng.choice([0, 1]))
    names = key_names()
    codes = named_codes()
    half = len(codes) // 2
    chords = chord_world(img, rng, codes[:half])
    for k in range(14):
        img.u32(0x0054ACE8 + 4 * k, rng.getrandbits(32) & 0x00FFFFFF)
    for variable in (0x0054AE04, 0x0054AE08, 0x0054AE0C, 0x006A6984, 0x0054AE10, 0x0054AE18, 0x0054AE14, 0x0054AE1C, 0x0054AE20, 0x0054AE24):
        img.u32(variable, rng.choice([0xFFFFFFFF, 1, 2, 3, rng.randrange(0, 32)]))
    img.u32(0x006A6964, rng.choice([0, 1, 2]))
    img.put(0x029D8378, c_string("C:\\Games\\THPS2\\TH2_OPT.CFG"))
    SETTINGS.clear()
    SETTINGS.extend(options_answers(rng))
    SETTINGS.extend(number_text(rng, v) for v in joystick_numbers(rng, rng.choice(["ok", "ok", "ok", "dup", "range"])))
    taken = [img.b[chords[k] + j] for k in range(14) for j in range(3) if img.b.get(chords[k] + j, 0)]
    scenario = rng.choice(["clean", "clean", "clean", "case", "conflict", "unknown", "none"])
    for k in range(14):
        SETTINGS.append(keyboard_text(rng, names, codes[half:], scenario if rng.random() < 0.5 else "clean", taken))
    return [], img.pokes()


# ---- pc\thstub.cpp and the PC file backends ----------------------------------------------------------------------------------------------------------------------------

CUSTOM_NAMES = ["MYPARK", "park1.prk", "!SPECIAL", "a.b.c", "Level_2", "PARK.PRK", ""]


def files_world(img, rng):
    img.put(0x006A71BC, c_string(rng.choice(PATHS)))
    for slot in range(10):
        img.u8(0x006A6D10 + slot, rng.choice([0, 0, 0, 2]))
        img.u32(0x006A6D1C + 4 * slot, rng.choice([0, 0x12340000 + slot, rng.getrandbits(32)]))


@thst(0x004F62C0, "THSTUB_SaveCustomLevel")
def gen_save_custom(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    name, data = env.slab + 0x100, env.slab + 0x400
    img.put(name, c_string(rng.choice(CUSTOM_NAMES)))
    img.put(data, bytes(rng.randrange(256) for _ in range(0x40)))
    return [name, rng.choice([0, 1, 0x100, 0x101]), data, rng.choice([0, 1, 0x40, 0x1000])], img.pokes()


@thst(0x004F6140, "THSTUB_LoadCustomLevel")
def gen_load_custom(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    name, buffer = env.slab + 0x100, env.slab + 0x1000
    img.put(name, c_string(rng.choice(CUSTOM_NAMES)))
    img.put(buffer, bytes(rng.randrange(256) for _ in range(0x80)))
    return [name, rng.choice([0, 1, 1, 2]), rng.choice([0, 1, 0x100, 0x101]), buffer], img.pokes()


@thst(0x004F6400, "THSTUB_DeleteCustomLevel")
def gen_delete_custom(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    name = env.slab + 0x100
    img.put(name, c_string(rng.choice(CUSTOM_NAMES)))
    return [name, rng.choice([0, 1, 0x100, 0x101])], img.pokes()


@thst(0x004E7900, "PCcreate")
def gen_pc_create(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    name = env.slab + 0x100
    img.put(name, c_string(rng.choice(CUSTOM_NAMES + ["data\\x.bin"])))
    return [name, rng.getrandbits(8)], img.pokes()


@thst(0x004E7B60, "PCwrite")
def gen_pc_write(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    for slot in range(10):
        img.u8(0x006A6D10 + slot, rng.choice([0, 2, 2]))          # (a package slot returns a stack word)
    data = env.slab + 0x400
    img.put(data, bytes(rng.randrange(256) for _ in range(0x40)))
    return [rng.choice([-1 & 0xFFFFFFFF, 0, 1, 5, 9, rng.randrange(0, 10)]), data, rng.choice([0, 1, 0x40, 0x1000])], img.pokes()


@thst(0x004E7500, "PCremove")
def gen_pc_remove(env, n):
    img, rng = Image(), env.rng
    files_world(img, rng)
    name = env.slab + 0x100
    img.put(name, c_string(rng.choice(CUSTOM_NAMES + ["data\\x.bin"])))
    return [name], img.pokes()


@thst(0x004E74F0, "gameShutdown")
def gen_game_shutdown(env, n):
    return [], {}


@thst(0x004E7C50, "PCnothing")
def gen_pc_nothing(env, n):
    return [], {}


# ---- pc\d3d*.cpp and the timers ------------------------------------------------------------------------------------------------------------------------------------------

@d3dm(0x004D0A20, "D3D_BitCount")
def gen_bit_count(env, n):
    rng = env.rng
    return [rng.choice([0, 1, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32), rng.getrandbits(16), rng.getrandbits(8) << 8, 0xF800, 0x7E0, 0x1F])], {}


@d3dm(0x004D0A40, "D3D_LowestBit")
def gen_lowest_bit(env, n):
    rng = env.rng
    return [rng.choice([0, 1, 0x80000000, rng.getrandbits(32), rng.getrandbits(16) << rng.randrange(0, 16), 0xF800, 0x7E0, 0x1F, 0x8000])], {}


@d3dm(0x004DAB30, "D3DTIMER_IsActive")
def gen_timer_active(env, n):
    img, rng = Image(), env.rng
    img.u32(0x006A05A0, rng.choice([0, 1, 0x1234, rng.getrandbits(32)]))
    return [], img.pokes()


@d3dm(0x004DAE00, "FrameRate_Update")
def gen_fps(env, n):
    img, rng = Image(), env.rng
    img.u32(0x006A05C0, rng.choice([0, 1, 5, 60, -1 & 0xFFFFFFFF, -50 & 0xFFFFFFFF, 0x7FFFFFFF]))
    img.u32(0x006A05AC, rng.choice([1000, rng.getrandbits(32), rng.randrange(0, 100000)]))
    img.u32(0x006A05B4, rng.choice([0, 500, rng.getrandbits(32), rng.randrange(0, 100000)]))
    img.u32(0x0054A218, rng.getrandbits(32))
    img.u32(0x0054A21C, rng.getrandbits(32))
    return [], img.pokes()


@d3dm(0x004EC930, "SDXMouse_SetBounds", this=0x100)
def gen_mouse_bounds(env, n):
    img, rng = Image(), env.rng
    obj = env.slab + 0x100
    for off in range(0, 0x30, 4):
        img.u32(obj + off, rng.choice([0, 100, rng.randrange(-200, 700) & 0xFFFFFFFF, rng.getrandbits(32)]))
    left, top = rng.randrange(-50, 300), rng.randrange(-50, 200)
    values = [left, top, left + rng.randrange(-20, 500), top + rng.randrange(-20, 400)]
    return [value & 0xFFFFFFFF for value in values], img.pokes()


@d3dm(0x004D3600, "D3D_SubmitViewportRect")
def gen_submit_rect(env, n):
    img, rng = Image(), env.rng
    rect = env.slab + 0x100
    fields = [rng.choice([0, 1, 100, 255, 256, 511, 512, -1, -100, rng.randrange(-300, 600), 0xFF, 0x1FE, 239, 240, 241]) for _ in range(4)]
    img.put(rect + 8, struct.pack("<4h", *fields))
    img.u32(0x029DA394, rng.choice([640, 800, 1024, 320, 512, 1280]))
    img.u32(0x029DA398, rng.choice([480, 600, 768, 240, 1024, 960]))
    return [rect], img.pokes()


@d3dm(0x004D0BC0, "D3D_BeginScene")
def gen_begin_scene(env, n):
    img, rng = Image(), env.rng
    tick = rng.choice([0, 10, 100, rng.randrange(0, 1000), 0xFFFFFFF0])
    img.u32(0x0056E31C, tick)
    # (the loop waits a few ticks: never a wrap below zero, which would take four billion)
    img.u32(0x0056E840, (tick + max(rng.choice([-5, -2, -1, 0, 1, 2, 3, 5, 8]), -tick)) & 0xFFFFFFFF)
    img.u32(0x006A6964, rng.choice([0, 0, 1, 2]))
    img.u32(0x0056A898, rng.choice([0, 6, 6, 3]))
    return [rng.choice([0, 1, 5, u(rng)]), rng.choice([0, 0xFF000000, u(rng)])], img.pokes()


# ---- pc\d3dpoly.cpp: the reversed list, the free list, the scene, the rectangles, the polygon buffer ----------------------------------------------------------------------------

HEAP = 0x800                  # the reversed-list heap inside the slab


def heap_world(img, env, rng, room=None):
    start = env.slab + HEAP
    room = rng.choice([0, 8, 16, 24, 0x40, 0x100]) if room is None else room
    img.u32(0x006A0070, start)
    img.u32(0x006A0074, start + rng.choice([0, 8, 0x10]))
    img.u32(0x006A0078, start + room)
    img.u32(0x006A007C, rng.choice([0, start + 8, rng.getrandbits(32)]))
    img.put(start, bytes(rng.randrange(256) for _ in range(0x120)))


@d3dm(0x004D3A70, "D3D_ReversePush")
def gen_reverse_push(env, n):
    img, rng = Image(), env.rng
    heap_world(img, env, rng)
    return [rng.choice([0, 1, rng.getrandbits(32), env.slab + 0x300])], img.pokes()


@d3dm(0x004D3A40, "D3D_BuildReverseCommandList")
def gen_reverse_list(env, n):
    img, rng = Image(), env.rng
    heap_world(img, env, rng)
    count = rng.choice([0, 1, 2, 3, 5, 8, 20])
    nodes = [env.slab + 0x300 + 8 * k for k in range(count)]
    for k, node in enumerate(nodes):
        img.u32(node, nodes[k + 1] if k + 1 < count else 0)
        img.u32(node + 4, rng.getrandbits(32))
    return [nodes[0] if nodes else 0], img.pokes()


@d3dm(0x004D3AB0, "D3D_InitListHeap")
def gen_init_list_heap(env, n):
    img, rng = Image(), env.rng
    base = env.slab + 0x400
    img.put(base, bytes(rng.randrange(256) for _ in range(0x100)))
    return [base, rng.choice([1, 2, 3, 8, 16, 30])], img.pokes()


@d3dm(0x004D40D0, "D3DPOLY_EndScene")
def gen_end_scene(env, n):
    img, rng = Image(), env.rng
    img.u8(0x006A04C4, rng.choice([0, 1, 1, 2]))
    img.u32(0x029DA3A0, rng.choice([0, 0, 1, 2]))
    return [], img.pokes()


def rectangle_world(env, rng, img):
    record = env.slab + 0x300
    img.put(record, bytes(rng.randrange(256) for _ in range(20)))
    return record


@d3dm(0x004D6090, "D3D_DrawUnitRectangle", this=0x300)
def gen_rect_1(env, n):
    img, rng = Image(), env.rng
    rectangle_world(env, rng, img)
    return [], img.pokes()


@d3dm(0x004D60C0, "D3D_DrawEightRectangle", this=0x300)
def gen_rect_8(env, n):
    img, rng = Image(), env.rng
    rectangle_world(env, rng, img)
    return [], img.pokes()


@d3dm(0x004D60F0, "D3D_DrawSixteenRectangle", this=0x300)
def gen_rect_16(env, n):
    img, rng = Image(), env.rng
    rectangle_world(env, rng, img)
    return [], img.pokes()


@d3dm(0x004D7430, "Init_PolyBuf")
def gen_init_polybuf(env, n):
    img, rng = Image(), env.rng
    img.u32(0x006A0070, rng.choice([0, rng.getrandbits(32)]))
    img.u32(0x006A0078, rng.getrandbits(32))
    return [], img.pokes()


@d3dm(0x004D7480, "Free_PolyBuf")
def gen_free_polybuf(env, n):
    img, rng = Image(), env.rng
    img.u32(0x006A0070, rng.choice([0, 0, env.slab + 0x500]))
    return [], img.pokes()


# ---- pc\d3dtex.cpp --------------------------------------------------------------------------------------------------------------------------------------------------------

BMP_QUEUE: list = []          # the header and info bytes the next file reads answer with
RECORD = 0x600                # the texture record AllocRecord hands out (inside the slab, 0x2c bytes)


def records(img, env, rng, count):
    out = [env.slab + 0x700 + 0x40 * k for k in range(count)]
    for k, record in enumerate(out):
        img.put(record, bytes(rng.randrange(256) for _ in range(0x2C)))
        img.u32(record + 0x24, out[k + 1] if k + 1 < count else 0)
        img.u32(record + 0x28, out[k - 1] if k else 0)
    return out


@d3dm(0x004D8B20, "D3DTex_DestroyAll")
def gen_destroy_all(env, n):
    img, rng = Image(), env.rng
    chain = records(img, env, rng, rng.choice([0, 1, 2, 5]))
    img.u32(0x006A051C, chain[0] if chain else 0)
    img.u32(0x006A0520, rng.getrandbits(32))
    img.u32(0x006A04D4, rng.getrandbits(32))
    return [], img.pokes()


@d3dm(0x004DAB20, "D3DTex_Reset")
def gen_texture_reset(env, n):
    return gen_destroy_all(env, n)


@d3dm(0x004D8C60, "D3DTex_LoadBitmap")
def gen_load_bitmap(env, n):
    img, rng = Image(), env.rng
    record = env.slab + 0x700
    material = env.slab + 0x800 + 0x40 * rng.randrange(0, 2)
    img.put(record, bytes(rng.randrange(256) for _ in range(0x2C)))
    img.put(material, bytes(rng.randrange(256) for _ in range(0x28)))
    return [record, rng.getrandbits(32), material if rng.random() < 0.8 else 0, rng.choice([0, 1, rng.getrandbits(32)]), rng.choice([0, 1, 2, 3, 0x101]), rng.choice([0, 1, 0x100])], img.pokes()


@d3dm(0x004D9F10, "D3DTex_Bmp4Unsupported")
def gen_bmp4(env, n):
    rng = env.rng
    return [rng.getrandbits(32), rng.getrandbits(32), 0, rng.choice([0, env.slab + 0x100])], {}


@d3dm(0x004D9F40, "D3DTex_RecordField20")
def gen_field20(env, n):
    img, rng = Image(), env.rng
    record = env.slab + 0x700
    img.put(record, bytes(rng.randrange(256) for _ in range(0x2C)))
    return [record], img.pokes()


@d3dm(0x004DA3E0, "D3DTex_AttachToSceneMaterial")
def gen_attach(env, n):
    img, rng = Image(), env.rng
    material, record = env.slab + 0x800, env.slab + 0x700
    img.put(material, bytes(rng.randrange(256) for _ in range(0x28)))
    img.put(record, bytes(rng.randrange(256) for _ in range(0x2C)))
    return [material if rng.random() < 0.8 else 0, record if rng.random() < 0.8 else 0], img.pokes()


@d3dm(0x004DA400, "D3DTex_ReleaseMaterialTexture")
def gen_release(env, n):
    img, rng = Image(), env.rng
    chain = records(img, env, rng, 3)
    img.u32(0x006A051C, chain[rng.randrange(0, 3)])
    return [chain[rng.randrange(0, 3)] if rng.random() < 0.8 else 0], img.pokes()


def image_world(img, env, rng):
    """An image header (width and height at +0x10, +0x12), a scene material (the bitmap name at +0x18) and a fresh record for AllocRecord to hand out."""
    image, material = env.slab + 0x200, env.slab + 0x280
    img.put(image, bytes(rng.randrange(256) for _ in range(0x30)))
    img.put(material, bytes(rng.randrange(256) for _ in range(0x28)))
    img.put(env.slab + RECORD, bytes(0x2C))
    return image, material


def bmp_bytes(rng, good):
    """The 14-byte file header and the 40-byte info header a texture file starts with: a 24-bit uncompressed one (width and height in the low words) or a spoiled one."""
    header = bytes(rng.randrange(256) for _ in range(14)) + b"\0\0"
    info = bytearray(40)
    info[0:4] = struct.pack("<I", 40)
    info[4:8] = struct.pack("<I", rng.choice([8, 16, 32, 64, 128, 256]))
    info[8:12] = struct.pack("<I", rng.choice([8, 16, 32, 64, 128, 256]))
    info[12:14] = struct.pack("<H", 1)
    info[14:16] = struct.pack("<H", 24 if good else rng.choice([8, 16, 32, 4]))
    info[16:20] = struct.pack("<I", 0 if good else rng.choice([0, 1, 3, 0x10000]))
    return header, bytes(info)


@d3dm(0x004D8CD0, "D3DTex_PrepareTexture")
def gen_prepare(env, n):
    img, rng = Image(), env.rng
    image, material = image_world(img, env, rng)
    BMP_QUEUE.clear()
    header, info = bmp_bytes(rng, rng.random() < 0.75)
    BMP_QUEUE.extend([header, info])
    return [image, rng.choice([0, 1, 0xFFFFFFFF, rng.getrandbits(32)]), material, rng.choice([0, 1, 0x100])], img.pokes()


@d3dm(0x004D98D0, "D3DTex_CreateExternalRecord")
def gen_external(env, n):
    img, rng = Image(), env.rng
    image, material = image_world(img, env, rng)
    return [env.slab + 0x1000, rng.choice([8, 64, 128, 256, 0x10040]), rng.choice([8, 64, 128, 256, 0x10040]), rng.getrandbits(32), rng.getrandbits(32), material if rng.random() < 0.8 else 0], img.pokes()


# ---- the software frame copy and the small wrappers ----------------------------------------------------------------------------------------------------------------------

@d3dm(0x004D7DB0, "D3D_CopyScreen", this=0x100)
def gen_copy_screen(env, n):
    img, rng = Image(), env.rng
    width, height = rng.choice([1, 2, 3, 8, 16, 31]), rng.choice([0, 1, 2, 5])
    img.u32(0x029DA394, width)
    img.u32(0x029DA398, height)
    img.u32(0x029DA3A0, rng.choice([0, 1, 1, 2]))
    obj, source, dest = env.slab + 0x100, env.slab + 0x1000, env.slab + 0x1400
    img.u32(obj + 0x18, source)
    img.put(source, bytes(rng.randrange(256) for _ in range(width * 2 * height)))
    img.put(dest, bytes(rng.randrange(256) for _ in range(0x300)))
    pitch = rng.choice([width * 2, width * 2 + 4, 0x40, 0x7C, 0, width * 2 + 2]) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)
    return [dest, pitch], img.pokes()


@d3dm(0x0040B5C0, "Camera_ResetA")
def gen_camera_reset_a(env, n):
    return [], {}


@d3dm(0x0040B610, "Camera_ResetB")
def gen_camera_reset_b(env, n):
    return [], {}


@d3dm(0x0044B1C0, "FallingText_ConstructStatic")
def gen_falling_text(env, n):
    return [], {}


@d3dm(0x004BB220, "Resource_VersionTag")
def gen_version_tag(env, n):
    img, rng = Image(), env.rng
    record = env.slab + 0x100
    text = rng.choice([b"V1.0", b"V2.5", b"Vab.", b"V.0.", b"v1.0", b"V1,0", b"X..."]) + bytes(rng.randrange(256) for _ in range(8))
    img.put(record, bytes(rng.randrange(256) for _ in range(4)) + text)
    return [record], img.pokes()


# ---- the gamma tables and the polygon extent ---------------------------------------------------------------------------------------------------------------------------------

def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


@d3dm(0x004D4120, "D3D_BuildGammaTable")
def gen_gamma_table(env, n):
    rng = env.rng
    img = Image()
    for base in (0x006A00B4, 0x005DD340, 0x0069FC64):
        img.put(base, bytes(rng.randrange(256) for _ in range(0x400)))
    gamma = rng.choice([1.0, 0.5, 2.2, 1.8, 0.8, 3.0, 1.25, 2.0, 4.0, 0.35, rng.uniform(0.3, 3.5), rng.uniform(0.3, 3.5)])
    return [float_bits(gamma)], img.pokes()


POLYGON = {"count": 1}


@d3dm(0x004D44D0, "D3D_PolygonTooLarge", this=0x300, edx=lambda env: POLYGON["count"])
def gen_polygon_extent(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([0, 1, 2, 3, 4, 6, 8])
    POLYGON["count"] = count
    lit = rng.choice([0, 1, 0x100, 0x101])
    stride = 0x1C if lit & 0xFF else 0x14
    vertices = env.slab + 0x300
    spread = rng.choice([50, 300, 700, 5000])
    for k in range(max(count, 1)):
        img.put(vertices + k * stride, struct.pack("<ff", rng.uniform(-spread, spread), rng.uniform(-spread, spread)))
    img.put(vertices + max(count, 1) * stride, bytes(8))
    return [lit, count], img.pokes()          # (the count rides as a second stack argument too, which the function ignores: the fixture then records it)


# ---- the video options window ------------------------------------------------------------------------------------------------------------------------------------------------

def vwin(address, name, this=0x100):
    return spec(address, name, "vwin", this=lambda env: env.slab + this)


# the objects of one window in the slab: the window 0x100 (the sprite object at +0x238, the list at +0x388, the three rows' elements at +0x38c.., the flag holder at +0x398,
# a value at +0x3b0, a flag byte at +0xe4), the list object 0x600 and its vtable 0x700, the elements 0x800.. with their vtable 0x740, the holder 0x8c0, the renderer 0x900
def vwin_world(img, env, rng):
    from opentony.memory import mem
    from pspool_specs import FAKE_SLOTS

    slab = env.slab
    window, list_obj, list_vt, element_vt, holder, renderer = slab + 0x100, slab + 0x600, slab + 0x700, slab + 0x740, slab + 0x8C0, slab + 0x900
    img.put(window, bytes(rng.randrange(256) for _ in range(0x400)))
    img.u32(window + 0x238, slab + 0x980)
    img.u32(window + 0x388, list_obj)
    for k in range(3):
        img.u32(window + 0x38C + 4 * k, slab + 0x800 + 0x40 * k if rng.random() < 0.85 else 0)
        img.put(slab + 0x800 + 0x40 * k, struct.pack("<I", element_vt) + bytes(0x3C))
    img.u32(window + 0x398, holder if rng.random() < 0.85 else 0)
    img.u32(window + 0x3B0, rng.getrandbits(32))
    img.put(holder, bytes(rng.randrange(256) for _ in range(0x10)))
    img.put(list_obj, struct.pack("<I", list_vt) + bytes(0x3C))
    vt = [0] * 12
    vt[3], vt[4], vt[5] = FAKE_SLOTS[0], FAKE_SLOTS[1], FAKE_SLOTS[2]
    img.put(list_vt, struct.pack("<12I", *vt))
    img.put(element_vt, struct.pack("<12I", *([0] * 9 + [FAKE_SLOTS[3], 0, 0])))
    img.u32(0x00560FD4, renderer)
    img.u32(renderer + 0x88, rng.getrandbits(32))
    # the option rows: renderer (value, list), resolution (value, the mode name table), gamma (value)
    names = [mem.u32(variable) for variable in NAME_VARIABLES]
    count = rng.choice([1, 2, 3, 4, 5])
    for k in range(6):
        img.u32(0x0056E788 + 4 * k, 0 if k >= count else rng.choice(names))
    renderers = slab + 0xA00
    img.put(renderers, struct.pack("<3I", mem.u32(0x00548564), mem.u32(0x00548568), 0))
    img.u32(0x0056E7C0, rng.choice([0, 1, 1]))
    img.u32(0x0056E7C8, renderers)
    img.u32(0x0056E7CC, rng.randrange(0, count))
    img.u32(0x0056E7D4, 0x0056E788)
    img.u32(0x0056E7D8, rng.choice([0, 5, -5 & 0xFFFFFFFF, 15, -15 & 0xFFFFFFFF, rng.randrange(-15, 16) & 0xFFFFFFFF]))
    for address in (0x0056B029, 0x0056B0D9, 0x0056AFF9, 0x0056B079, 0x0056B089):
        img.u8(address, rng.choice([0, 1]))
    mode_world(img, rng)
    return window


@vwin(0x004CEBB0, "VideoWin_SetOption")
def gen_vwin_set_option(env, n):
    img, rng = Image(), env.rng
    vwin_world(img, env, rng)
    option = rng.choice([0, 1, 2])
    return [option, rng.choice([0, 0, 1, -1 & 0xFFFFFFFF, 2, -3 & 0xFFFFFFFF, 20]), rng.choice([0xFFFF, 0xFFFF, 0, 1, 2, 7, 0xFFFFFFFF, 12])], img.pokes()


@vwin(0x004CEC70, "VideoWin_Draw")
def gen_vwin_draw(env, n):
    img, rng = Image(), env.rng
    vwin_world(img, env, rng)
    return [], img.pokes()


@vwin(0x004CEFB0, "VideoWin_PadChoice")
def gen_vwin_pad_choice(env, n):
    img, rng = Image(), env.rng
    vwin_world(img, env, rng)
    out = env.slab + 0xB00
    img.u32(out, rng.getrandbits(32))
    return [out], img.pokes()


@vwin(0x004CECE0, "VideoWin_Step")
def gen_vwin_step(env, n):
    img, rng = Image(), env.rng
    vwin_world(img, env, rng)
    return [], img.pokes()


@vwin(0x004CED80, "VideoWin_BuildModeNames")
def gen_vwin_build_names(env, n):
    img, rng = Image(), env.rng
    vwin_world(img, env, rng)
    return [rng.choice([0, 0, 1])], img.pokes()


# ---- stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def install_sys_stubs(fc, env):
    """The C runtime's file calls, the options file, the engine calls the system code reaches and the clock a begin-scene waits on."""
    if getattr(fc, "_sys_installed", False):
        return
    fc._sys_installed = True
    mark_under_test()
    stop_clock(fc)
    from opentony.memory import mem

    rng = env.rng
    ensure(fc, 0x004F6CA0, 4, "engine_004f6ca0", text=2, stack_to={2: SCRATCH_A, 3: SCRATCH_B}, fill=(3, setting_answer))
    ensure(fc, 0x004F6CD0, 3, "engine_004f6cd0", text=2, stack_to={2: SCRATCH_A})
    ensure(fc, 0x005013F9, 2, "crt_fopen", text=True, stack_to=KSTACK, result=lambda a: rng.choice([0, 0x00DB0000 + rng.randrange(0x1000), 0x00DB0000 + rng.randrange(0x1000)]))
    ensure(fc, 0x00500E7B, 1, "crt_fclose")
    ensure(fc, 0x00501FA0, 1, "crt_remove", text=True, stack_to=KSTACK)
    ensure(fc, 0x0050195D, 4, "crt_fwrite", result=lambda a: a[2] if rng.random() < 0.8 else rng.randrange(0, a[2] + 1) if a[2] else 0)
    for address, args in ((0x004DDB70, 0), (0x004DAB80, 0), (0x004E4A60, 0), (0x004E7140, 0), (0x004E7370, 0), (0x004D0F80, 1), (0x004F7A70, 0), (0x004CE260, 0),
                          (0x004E79F0, 3), (0x004E7BC0, 1), (0x004E9B80, 1), (0x004D36C0, 4), (0x004D3B40, 2)):
        ensure(fc, address, args, f"engine_{address:08x}")
    # the polygon heap, the scene and the texture records
    ensure(fc, 0x005015A6, 2, "crt_controlfp")
    ensure(fc, 0x004F23D0, 0, "engine_004f23d0")
    ensure(fc, 0x004D5E40, 0, "engine_004d5e40", convention="thiscall", this=True, blob=(-1, 20), stack_to=KSTACK)
    ensure(fc, 0x005002FE, 1, "crt_new", result=lambda a: rng.getrandbits(32) & 0xFFFFFFF0)
    ensure(fc, 0x005002F3, 1, "crt_delete")
    ensure(fc, 0x004D8B50, 1, "engine_004d8b50",
           writes=[(0x006A051C, 4, lambda a: mem.u32(a[0] + 0x24) if mem.u32(0x006A051C) == a[0] else mem.u32(0x006A051C))])
    ensure(fc, 0x004DA550, 0, "engine_004da550")
    ensure(fc, 0x004D9950, 4, "engine_004d9950")
    ensure(fc, 0x004D9880, 0, "engine_004d9880", result=lambda a: env.slab + RECORD)
    ensure(fc, 0x004D8DF0, 2, "engine_004d8df0", result=lambda a: rng.choice([0xFFFFFFFF, 3, 5, 0xFFFFFFFF]))
    ensure(fc, 0x004E7AD0, 3, "engine_004e7ad0", stack_to=KSTACK,
           fill=(1, lambda a: (BMP_QUEUE.pop(0) if BMP_QUEUE else bytes(a[2]))[:a[2]].ljust(a[2], b"\0") if a[2] <= 0x100 else bytes(rng.randrange(256) for _ in range(64))))
    ensure(fc, 0x004E75B0, 3, "engine_004e75b0", text=True, stack_to=KSTACK, result=lambda a: rng.choice([0xFFFFFFFF, 2, 4, 7]))
    ensure(fc, 0x00466B70, 1, "operator_new", result=lambda a: rng.choice([0, env.slab + 0x1800]))
    ensure(fc, 0x004329D0, 4, "engine_004329d0", convention="thiscall", this=True)
    ensure(fc, 0x00432F60, 1, "engine_00432f60", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    ensure(fc, 0x00432980, 2, "engine_00432980")
    ensure(fc, 0x004D8F10, 4, "engine_004d8f10")
    ensure(fc, 0x0040E000, 4, "engine_0040e000", convention="thiscall", this=True)
    ensure(fc, 0x004B9730, 0, "engine_004b9730", convention="thiscall", this=True)
    # the video options window: its list and rows are fake objects whose methods are unreached retail entry points (pspool_specs.FAKE_SLOTS)
    from pspool_specs import FAKE_SLOTS

    ensure(fc, FAKE_SLOTS[0], 0, f"engine_{FAKE_SLOTS[0]:08x}", convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[1], 0, f"engine_{FAKE_SLOTS[1]:08x}", convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[2], 1, f"engine_{FAKE_SLOTS[2]:08x}", convention="thiscall", this=True)
    ensure(fc, FAKE_SLOTS[3], 4, f"engine_{FAKE_SLOTS[3]:08x}", convention="thiscall", this=True, text=True)
    ensure(fc, 0x00470AB0, 0, "engine_00470ab0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 2]))
    ensure(fc, 0x004AC730, 0, "engine_004ac730", convention="thiscall", this=True)
    ensure(fc, 0x004AC770, 2, "engine_004ac770", convention="thiscall", this=True)
    ensure(fc, 0x0046F550, 3, "engine_0046f550", result=lambda a: rng.choice([0, 1, 2, 5]))
    ensure(fc, 0x004D7990, 3, "engine_004d7990", convention="thiscall", this=True)
    ensure(fc, 0x004AD620, 3, "sfx")
    # Sleep(0): the simulation clock moves on while a begin-scene waits for it
    ensure(fc, 0x004F6550, 1, "engine_004f6550", writes=[(0x0056E31C, 4, lambda a: mem.u32(0x0056E31C) + rng.choice([1, 1, 2]))])


GROUP_STUBS = {"winm": install_sys_stubs, "vopt": install_sys_stubs, "thst": install_sys_stubs, "d3dm": install_sys_stubs, "vwin": install_sys_stubs}
