"""Phase 4.15f probe: the keyboard bindings screen and the career loading screen (group cscr: Game_MainLoop, Create_LoadPSH and the other KeyConfig_* methods, Career_LoadingScreen).

The bindings table the screen shows (15 rows of 0x14 bytes from 0x005600a4, the page word before it) is rewritten by the seed to point into the slab: labels, the NULL-ended list of page
titles, the two key bytes of every row and its pad button cell. The engine objects the screen builds (window elements, the list, the rows, the style) are stubbed constructors that hand
out arena blocks; the list, rows and style get a fake vtable whose ten slots are retail functions the harness stubs.

Slab layout (offsets): 0x0100 the screen object (0x444), 0x0600 a fake list, 0x0640 fifteen fake rows (0x40 each), 0x0a00 a fake style, 0x0a40 the fake vtable (ten slots),
0x0b00 key bytes (2 per row), 0x0b40 pad button cells (15 dwords), 0x0c00 row labels (0x10 each), 0x0d00 the page titles list and 0x0d20 their strings, 0x0e00 key names (0x10 each),
0x1000 two font records (0x150 each), 0x1300 a fake renderer (0x100), 0x1400 result cells, 0x1500 high score texts (0x10 each); the arena from 0x2000.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_front import ensure, merged
from frontend_specs_menu import Image

import cobj_specs
from cobj_specs import SCRATCH, noise, r, u32, word
from cobj_world import FAKE_SLOTS

GROUPS = ("cscr",)

SCREEN = 0x0100
SCREEN_BYTES = 0x444
LIST = 0x0600
ROWS = 0x0640
STYLE = 0x0A00
VTABLE = 0x0A40
KEYS = 0x0B00
BUTTONS = 0x0B40
LABELS = 0x0C00
TITLES = 0x0D00
TITLE_STRINGS = 0x0D20
KEY_NAMES = 0x0E00
FONT_RECORDS = 0x1000
RENDERER = 0x1300
RESULT = 0x1400
SCORE_TEXTS = 0x1500
ARENA = 0x2000
ARENA_BYTES = 0x4000
SLAB = ARENA + ARENA_BYTES + 0x100

TABLE = 0x005600A0                      # the page word, then the rows
TABLE_BYTES = 0x13C
LABEL_BUFFER = (0x00560088, 0x18)
PADS = (0x0056AFF8, 0xE8)
RENDERER_POINTER = (0x00560FD4, 4)
CLOCK = (0x0056E31C, 4)
LAST_TICK = (0x00568624, 4)
FRONT_FLAG = (0x0056114C, 4)
DIRTY = (0x006A13FE, 4)
PROMPT_FILE = (0x00527D54, 4)
LEVEL = (0x0056A898, 4)
LEVEL_TABLE = (0x0053C1F8, 0x1AC * 3)
LOADING_GLOBALS = (0x00562058, 0x24)
FONT_LOCAL = SCRATCH + 0x800            # the loading screen's own Font object (createobj::kScratch + 0x800)
LITERALS = [(0x005252C0, 0x6F0), (0x00518744, 0x10)]       # the screens' string literals and the screen class's vtable
ROWS_COUNT = 15


def regions(slab):
    return merged([(slab + SCREEN, SCREEN_BYTES), (slab + LIST, 0x1000 - 0x600 + 0x100), (slab + FONT_RECORDS, 0x600), (slab + SCORE_TEXTS, 0x100), (slab + ARENA, ARENA_BYTES),
                   (TABLE, TABLE_BYTES), LABEL_BUFFER, PADS, RENDERER_POINTER, CLOCK, LAST_TICK, FRONT_FLAG, DIRTY, LEVEL, LEVEL_TABLE, LOADING_GLOBALS, PROMPT_FILE], gap=0)


def seed(env, rng):
    env.alloc_base = ARENA
    env.alloc_size = ARENA_BYTES
    img = Image()
    slab = env.slab
    # the fake vtable
    img.put(slab + VTABLE, b"".join(u32(a) for a in FAKE_SLOTS))
    for k in range(ROWS_COUNT):
        img.u32(slab + ROWS + 0x40 * k, slab + VTABLE)
    img.u32(slab + LIST, slab + VTABLE)
    img.u32(slab + STYLE, slab + VTABLE)
    # the bindings table
    names = ["Keyboard", "Pad one", "Pad two", "Pad three"]
    for i, title in enumerate(names):
        img.put(slab + TITLE_STRINGS + 0x10 * i, title.encode() + b"\0")
    count = rng.choice([2, 3, 4])
    img.put(slab + TITLES, b"".join(u32(slab + TITLE_STRINGS + 0x10 * i) for i in range(count)) + u32(0))
    img.u32(TABLE, rng.randrange(0, count))
    for k in range(ROWS_COUNT):
        row = TABLE + 4 + 0x14 * k
        img.put(slab + LABELS + 0x10 * k, f"Action {k}".encode() + b"\0")
        img.u32(row, slab + LABELS + 0x10 * k)
        img.u32(row + 4, slab + TITLES if k == 0 else 0)
        img.u32(row + 8, slab + KEYS + 2 * k)
        img.u32(row + 0xC, slab + BUTTONS + 4 * k if k % 5 != 3 else 0)
        img.u32(row + 0x10, 0)
        img.put(slab + KEYS + 2 * k, bytes([rng.randrange(1, 0x70), rng.choice([0, rng.randrange(1, 0x70)])]))
        img.u32(slab + BUTTONS + 4 * k, rng.choice([0xFFFFFFFF, rng.randrange(0, 16)]))
    for k in range(16):
        img.put(slab + KEY_NAMES + 0x10 * k, f"key{k}".encode() + b"\0")
    # the font records (random fields) and the renderer
    img.put(slab + FONT_RECORDS, noise(rng, 0x2A0))
    img.put(slab + RENDERER, noise(rng, 0x100))
    img.u32(RENDERER_POINTER[0], slab + RENDERER)
    img.u32(PROMPT_FILE[0], rng.getrandbits(32) & 0xFFFFFF00)
    # the level table of the loading screen: three levels with names, ten goals each (a count, a default text and an override text)
    for level in range(3):
        base = LEVEL_TABLE[0] + 0x1AC * level
        img.put(base, f"Level {level}".encode() + b"\0")
        for i in range(10):
            img.put(slab + SCORE_TEXTS + 0x10 * (i % 4) + 0x40 * level, f"HS {level}.{i}".encode() + b"\0")
            at = base + 0xC4 + 0x18 * i
            img.u32(at, rng.randrange(0, 100000))
            img.u32(at + 4, slab + SCORE_TEXTS + 0x10 * (i % 4) + 0x40 * level)
            img.u32(at + 8, rng.choice([0, slab + SCORE_TEXTS + 0x10 * ((i + 1) % 4) + 0x40 * level]))
    return dict(img.pokes())


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(LITERALS)
    SEEDS[_group] = seed


def pad_edge(env, which):
    """What the input update leaves in the back (0) and confirm (1) edges: the plan of the case gives the frame's pair, then nothing."""
    if which == 0:
        env.pad_frame = env.pad_plan.pop(0) if getattr(env, "pad_plan", None) else (0, 0)
    return env.pad_frame[which]


def tick(rng):
    from opentony.memory import mem

    return (mem.u32(CLOCK[0]) + rng.choice([1, 2, 3])) & 0xFFFFFFFF


def install_stubs(fc, env):
    from platform_specs import mark_under_test

    if getattr(fc, "_cobj5_installed", False):
        return
    fc._cobj5_installed = True
    mark_under_test()
    rng = env.rng
    last = lambda a: env.alloc_log[-1][0]        # (a constructor answers the block the allocation just before it handed out)
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)      # (the clock at 0x0056e31c must not tick while a case runs)
    ensure(fc, 0x00466B70, 1, "operator_new", result=env.alloc)
    ensure(fc, 0x00466BB0, 1, "operator_delete")
    ensure(fc, 0x005002FE, 1, "engine_005002fe", result=env.alloc)
    ensure(fc, 0x005002F3, 1, "engine_005002f3")
    ensure(fc, 0x00448DD0, 2, "engine_00448dd0", text=True, result=lambda a: rng.getrandbits(32))
    # the window, fonts and elements
    for address, count in ((0x004AC5B0, 4), (0x00454930, 0), (0x004AC670, 1), (0x00454970, 0), (0x004AC610, 0), (0x004AC770, 2), (0x0044AB70, 3), (0x004D78C0, 0), (0x004D7990, 3)):
        ensure(fc, address, count, "engine_%08x" % address, convention="thiscall", this=True)
    for address, count in ((0x00449CA0, 0), (0x0044A060, 0)):
        ensure(fc, address, count, "engine_%08x" % address, convention="thiscall", this=True, stack_to=FONT_LOCAL)
    ensure(fc, 0x0044ABB0, 1, "engine_0044abb0", convention="thiscall", this=True, stack_to=FONT_LOCAL + 4)
    ensure(fc, 0x0044A070, 4, "engine_0044a070", convention="thiscall", this=True, text=2, stack_to={-1: FONT_LOCAL, 2: SCRATCH + 0x600})
    for address, count, text in ((0x004D74A0, 3, False), (0x004ACBE0, 7, True), (0x00457420, 4, True), (0x004A1B40, 7, True), (0x0046F770, 0, False), (0x00470C60, 8, False), (0x0046FC50, 6, False)):
        kw = {"text": text} if text is not False else {}
        if address == 0x004A1B40:
            kw["stack_to"] = {2: FONT_LOCAL}                     # (the loading screen's prompts take its local font)
        ensure(fc, address, count, "engine_%08x" % address, convention="thiscall", this=True, result=last, **kw)
    ensure(fc, 0x0044ADA0, 1, "engine_0044ada0", text=True, result=lambda a: env.slab + FONT_RECORDS + (0 if a[0] == 0x00525458 else 0x150))
    ensure(fc, 0x0044B000, 1, "engine_0044b000", text=True, result=lambda a: env.slab + FONT_RECORDS)
    ensure(fc, 0x0044B0A0, 1, "engine_0044b0a0", text=True)
    # the fake vtable: the slots' functions and the numbers of arguments the screen passes
    for address, count in ((0x00421040, 1), (0x004203C0, 2), (0x004214D0, 0), (0x00420860, 0), (0x0041FE50, 0), (0x0041F3D0, 1), (0x00425200, 1), (0x00425270, 0)):
        ensure(fc, address, count, "engine_%08x" % address, convention="thiscall", this=True)
    ensure(fc, 0x00424A90, 4, "engine_00424a90", convention="thiscall", this=True, text=True)
    ensure(fc, 0x0041D0F0, 0, "engine_0041d0f0", convention="thiscall", this=True)             # the window's step (slot 1 of the screen's vtable)
    # input and the frame
    ensure(fc, 0x004E4230, 1, "engine_004e4230", result=lambda a: rng.choice([0, 0x41, 0x61, 0x30]))
    ensure(fc, 0x004E4250, 1, "engine_004e4250", result=lambda a: env.slab + KEY_NAMES + 0x10 * (a[0] % 16))
    ensure(fc, 0x004AD620, 3, "engine_004ad620")
    ensure(fc, 0x0046F550, 3, "engine_0046f550", result=lambda a: rng.choice([0, 1, 2, 3, 0xFFFFFFFF]))
    ensure(fc, 0x00414030, 1, "engine_00414030", result=lambda a: rng.choice([0, 0, 1, 1, 5]))
    ensure(fc, 0x004F7CE0, 0, "engine_004f7ce0", writes=[(CLOCK[0], 4, lambda a: tick(rng))])
    ensure(fc, 0x00489A10, 0, "engine_00489a10", writes=[(0x0056AFF9, 1, lambda a: pad_edge(env, 0)), (0x0056B029, 1, lambda a: pad_edge(env, 1))])      # Input_UpdateActionStates
    for address, count in ((0x0042FC70, 0), (0x004B2390, 0), (0x0048A420, 0), (0x004E4D10, 0), (0x004E42B0, 1), (0x004F5FF0, 0), (0x004DAE00, 0), (0x00418150, 0), (0x004304B0, 0),
                           (0x00466BE0, 0), (0x004E8740, 1), (0x004C9300, 1), (0x00430510, 0), (0x004E5460, 0), (0x00489670, 1)):
        ensure(fc, address, count, "engine_%08x" % address)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def cs(address, name, this=None):
    return r(address, name, "cscr", this)


def screen_state(img, env, rng, complete=True):
    """A screen object with its members filled in: the fake list, rows and style, a sprite and a prompt."""
    slab = env.slab
    base = slab + SCREEN
    img.put(base, noise(rng, SCREEN_BYTES))
    img.u32(base, 0x00518744)
    img.u32(base + 0xBC, rng.choice([0, 0, 1, 2]))
    img.u32(base + 0xC0, rng.randrange(0, 0x20))
    img.u8(base + 0xFC, rng.choice([0, 1]))
    img.u32(base + 0x250, rng.choice([0, 0x2000 + slab, slab + ARENA]))
    img.u32(base + 0x3A0, slab + LIST if complete else rng.choice([0, slab + LIST]))
    for k in range(ROWS_COUNT):
        img.u32(base + 0x3A4 + 4 * k, slab + ROWS + 0x40 * k if complete or rng.random() < 0.7 else 0)
    img.u32(base + 0x420, slab + STYLE if complete else rng.choice([0, slab + STYLE]))
    return img


def pads(img, rng, probability=0.5):
    for address in (0x0056B029, 0x0056B0D9, 0x0056AFF9):
        img.u8(address, 1 if rng.random() < probability else 0)


@cs(0x0041C4A0, "KeyConfig_Done", lambda env: env.slab + SCREEN)
def gen_done(env, n):
    img = Image()
    screen_state(img, env, env.rng)
    return [], img.pokes()


@cs(0x0041C490, "KeyConfig_Nothing", lambda env: env.slab + SCREEN)
def gen_nothing(env, n):
    img = Image()
    screen_state(img, env, env.rng)
    return [word(env.rng)], img.pokes()


@cs(0x0041D6F0, "KeyConfig_Poll", lambda env: env.slab + SCREEN)
def gen_poll(env, n):
    rng = env.rng
    img = Image()
    screen_state(img, env, rng)
    pads(img, rng, 0.5)
    img.u32(env.slab + RESULT, rng.getrandbits(32))
    return [env.slab + RESULT], img.pokes()


@cs(0x0041D4F0, "KeyConfig_SetButton")
def gen_set_button(env, n):
    rng = env.rng
    img = Image()
    for k in range(ROWS_COUNT):
        img.u32(env.slab + BUTTONS + 4 * k, rng.choice([0xFFFFFFFF, rng.randrange(0, 5), rng.randrange(0, 5)]))
    row = rng.choice([0, 1, 2, 3, 4, 7, 14, rng.randrange(0, ROWS_COUNT)])
    value = rng.choice([0xFFFFFFFF, rng.randrange(0, 5), rng.randrange(0, 5), 99])
    return [row, value], img.pokes()


@cs(0x0041D580, "KeyConfig_Assign")
def gen_assign(env, n):
    rng = env.rng
    img = Image()
    keys = [rng.randrange(1, 12) for _ in range(ROWS_COUNT * 2)]
    for k in range(ROWS_COUNT):
        img.put(env.slab + KEYS + 2 * k, bytes([keys[2 * k], rng.choice([0, keys[2 * k + 1]])]))
    row = rng.choice([1, 2, 5, 11, 12, 14, rng.randrange(1, ROWS_COUNT)])
    column = rng.choice([1, 2, 1, 2])
    key = rng.choice([0, 0x3B, 0x44, 0x45, 0x57, 0x66, 0x67, rng.randrange(1, 12), rng.randrange(1, 12), 0x30])
    return [row, column, key], img.pokes()


@cs(0x0041CEB0, "KeyConfig_SetRow", lambda env: env.slab + SCREEN)
def gen_set_row(env, n):
    rng = env.rng
    img = Image()
    screen_state(img, env, rng)
    img.u32(TABLE, rng.choice([0, 0, 1, 2, 3]))
    for k in range(ROWS_COUNT):
        img.u32(env.slab + BUTTONS + 4 * k, rng.choice([0xFFFFFFFF, rng.randrange(0, 20), 12345]))
    return [rng.choice([0, 0, 1, 2, 5, 14]), rng.choice([0, 0, 1, 0xFFFFFFFF])], img.pokes()


@cs(0x0041D060, "KeyConfig_Update", lambda env: env.slab + SCREEN)
def gen_update(env, n):
    img = Image()
    screen_state(img, env, env.rng)
    return [], img.pokes()


@cs(0x0041CDC0, "KeyConfig_Destruct", lambda env: env.slab + SCREEN)
def gen_destruct(env, n):
    img = Image()
    screen_state(img, env, env.rng, complete=False)
    return [], img.pokes()


@cs(0x0041CDA0, "KeyConfig_ScalarDelete", lambda env: env.slab + SCREEN)
def gen_scalar_delete(env, n):
    img = Image()
    screen_state(img, env, env.rng, complete=False)
    return [cobj_specs.flags(env.rng)], img.pokes()


def arena_blocks(sizes, start=0):
    """The addresses env.alloc hands out for a series of allocations (16-byte steps, at least 8 bytes)."""
    out = []
    at = start
    for size in sizes:
        out.append(at)
        at += (max(size, 8) + 15) & ~15
    return out


def vtable_pokes(img, env, first_size_extra):
    """The list and the rows the constructor allocates need their fake vtable: poke it at the arena addresses they will get."""
    sizes = ([first_size_extra] if first_size_extra else []) + [0x1C] + [0x2C] * 6 + [0x5C] * 2 + [0xB0] * 4 + [0x17C, 0x1BC] + [0x100] * ROWS_COUNT
    blocks = arena_blocks(sizes)
    offset = 1 if first_size_extra else 0
    list_block = blocks[offset + 1 + 6 + 2 + 4 + 1]
    img.u32(env.slab + ARENA + list_block, env.slab + VTABLE)
    for k in range(ROWS_COUNT):
        img.u32(env.slab + ARENA + blocks[offset + 1 + 6 + 2 + 4 + 2 + k], env.slab + VTABLE)
    img.u32(env.slab + ARENA + blocks[offset + 1 + 6 + 2 + 4], env.slab + VTABLE)         # the style
    return img


@cs(0x0041C4B0, "Create_LoadPSH", lambda env: env.slab + SCREEN)
def gen_construct(env, n):
    img = Image()
    img.put(env.slab + SCREEN, noise(env.rng, SCREEN_BYTES))
    vtable_pokes(img, env, 0)
    return [], img.pokes()


@cs(0x0041C2D0, "Game_MainLoop")
def gen_main_loop(env, n):
    rng = env.rng
    img = Image()
    vtable_pokes(img, env, 0x444)
    pads(img, rng, 0.0)
    if rng.random() < 0.4:
        img.u8(0x0056B0D9, 1)                                                          # (the second pad's confirm edge, which the input update leaves alone: the screen ends in its first frame)
        env.pad_plan = [(0, 0)]
    else:
        env.pad_plan = [(0, 0)] * rng.choice([1, 2, 3]) + [rng.choice([(1, 0), (0, 1), (1, 1)])]       # (the input update brings the edge in a later frame)
    img.u32(FRONT_FLAG[0], word(rng))
    img.u32(CLOCK[0], rng.randrange(0, 1000))
    img.u32(LAST_TICK[0], rng.randrange(0, 1000))
    img.u32(env.slab + RESULT, 0)
    return [], img.pokes()


@cs(0x00415B30, "Career_LoadingScreen")
def gen_loading_screen(env, n):
    rng = env.rng
    img = Image()
    img.u32(LEVEL[0], rng.randrange(0, 3))
    img.put(LOADING_GLOBALS[0], noise(rng, LOADING_GLOBALS[1]))
    return [rng.choice([0, 1, 2, 3, 1, 2])], img.pokes()


# ---- group kcs (12 s8): the screen's step, slot 1 of its vtable (0x0041d0f0). A group of its own because group cscr stubs this method inside Game_MainLoop's capture; here it runs
# as retail code, and so do KeyConfig_SetRow, KeyConfig_SetButton and KeyConfig_Assign (their callees are the stubs of cscr). New stubs: the reset-bindings, first-button, first-key,
# highlighted-row and window-update calls and the fake vtable's slot 7 (an answer whose low byte is what the step reads).
KCS = "kcs"
MOUSE_MODE = 0x006A13FC
MOUSE_COLUMN = 0x006A1410
PAD_EDGES = (0x0056AFF8, 0x0056B028, 0x0056B078, 0x0056B079, 0x0056B089, 0x0056B099, 0x0056B0A9)
BUTTON_CHOICES = [0xFFFFFFFF, 0, 3, 5, 9, 15]
KEY_CHOICES = [0, 0, 0, 0xE, 0x1, 0x1C, 0xC8, 0xCB, 0xCD, 0xD0, 0x3B, 0x44, 0x57, 0x66, 0xD1, 0xFFFFFFFF, 0x80000000]


def kcs_regions(slab):
    return merged(list(regions(slab)) + [(MOUSE_MODE, 0x18)], gap=0)


SLAB_BYTES[KCS] = SLAB + 0x100
EXTRA_REGIONS[KCS] = kcs_regions
READ_ONLY[KCS] = list(LITERALS)
SEEDS[KCS] = seed


def install_kcs_stubs(fc, env):
    install_stubs(fc, env)
    if getattr(fc, "_kcs_installed", False):
        return
    fc._kcs_installed = True
    rng = env.rng
    ensure(fc, 0x00425270, 0, "engine_00425270", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1, 1, 0x100, 0xFF01]))          # slot 7: the low byte is the answer
    ensure(fc, 0x00470AB0, 0, "engine_00470ab0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2, 5, 11, 12, 13, 14, rng.randrange(0, ROWS_COUNT)]))
    ensure(fc, 0x004E3E80, 0, "engine_004e3e80")
    ensure(fc, 0x004E4260, 0, "engine_004e4260", result=lambda a: rng.choice(BUTTON_CHOICES))
    ensure(fc, 0x004E41D0, 1, "engine_004e41d0", result=lambda a: rng.choice(KEY_CHOICES) if rng.random() < 0.7 else rng.randrange(1, 0x70))
    ensure(fc, 0x004AC730, 0, "engine_004ac730", convention="thiscall", this=True)


GROUP_STUBS[KCS] = install_kcs_stubs


@r(0x0041D0F0, "KeyConfig_Step", KCS, lambda env: env.slab + SCREEN)
def gen_step(env, n):
    rng = env.rng
    img = Image()
    screen_state(img, env, rng)
    base = env.slab + SCREEN
    page = rng.choice([0, 0, 0, 1, 2, 3])
    img.u32(TABLE, page)
    img.u32(base + 0xB0, page if rng.random() < 0.85 else rng.choice([0, 1]))
    img.u32(base + 0xB4, rng.choice(BUTTON_CHOICES))
    img.u32(base + 0xBC, rng.choice([0, 1, 1, 2, 2]))
    img.u32(base + 0xC0, rng.choice([rng.randrange(0, 60), 5, 10, 15, 20, 25, 30]))
    img.u32(base + 0xC4, env.slab + STYLE)                                    # (the reset prompt: any object with the fake vtable)
    img.u8(MOUSE_MODE, rng.choice([0, 0, 1]))
    img.u32(MOUSE_COLUMN, rng.choice([0, 1, 2, 2, 1]))
    for address in PAD_EDGES:
        img.u8(address, 1 if rng.random() < 0.25 else 0)
    return [], img.pokes()
