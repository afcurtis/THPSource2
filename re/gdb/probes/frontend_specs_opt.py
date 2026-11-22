"""Phase 4.10g case generators, part 1: what the options screens share, and the options window (options.cpp, 0x00481200..0x00483ef0).  The pages (gap checklist, high
scores, movies, sound, cards, controls, cheats) are in frontend_specs_opt_pages.py and frontend_specs_opt_ctl.py.

The screens work on the 4.10e1 slab (frontend_specs_screens.py: the 4.10d layout below 0x18000, then four main objects, a renderer, the fake vtable VT_ANY, a pool of
48 fake objects, texts, and the allocation arena at 0x1c000) extended here to 0x2b000:

  0x24000  four objects of 0xc00 bytes (the options window is 0xa00 bytes)       0x27000  eight fake lists of 0x1c0 bytes       0x27e00  forty item records of 0x100
  0x2a600  the fake vtables of this module: items (0x2a600), lists (0x2a680)

A virtual call on a fake list or item is a reported service (the vtables hold unreached engine entry points, one stub each); an object a stubbed constructor "builds"
is an arena block whose vtable is VT_ANY.  The pages the options window holds are fake pool objects: their own functions are replayed in their own groups.

Groups: optmenu (the window), optloop (the screen loop and its callback), optvid (the level information window, the video menu's destructor and the tiny stubs).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import (CHILD_DRAW, CHILD_UPDATE, ELEM_HOOK, ITEM_DRAW, ITEM_HIGHLIGHT, ITEM_NAVIGATE, ITEM_SET_POSITION, ITEM_SNAP, LIST_V2, LIST_V4, LIST_V5,
                                 LIST_V6, LIST_V7, VIRTUAL_DELETE, coords, item_fields)
from frontend_specs_screens import (ARENA2, ARENA2_SIZE, OBJ, TICK, install_screen_stubs, regions as screens_regions, seed_screens, READ_ONLY_COMMON)
from frontend_specs_screens_cases import Pool, common, font_object, gspec, window
from frontend_specs_skwin import ITEM_SET_COLUMN, LIST_V3, A, new_image

# ---- the slab ----------------------------------------------------------------------------------------------------------------------------------------------
SLAB_OPT = 0x2B000
OPT_OBJ = [0x24000, 0x24C00, 0x25800, 0x26400]
OLISTS = 0x27000
OLIST_STRIDE = 0x1C0
OITEMS = 0x27E00
OITEM_STRIDE = 0x100
VT_OITEM = 0x2A600
VT_OLIST = 0x2A680
LIST_V1 = 0x004ADCC0        # slot 1 of the fake list (set the arrows): an unreached engine function
ITEM_SET_TEXT = 0x004ADC70  # slot 8 of the fake item (set text)

# ---- retail globals ----------------------------------------------------------------------------------------------------------------------------------------
PAD_STRIDE = 0x16C
BACK, CONFIRM, UP_A, UP_B, LEFT, RIGHT, RELEASED = 0x0056AFF9, 0x0056B029, 0x0056B099, 0x0056B0A9, 0x0056B079, 0x0056B089, 0x0056B069
SECOND_PAD = 0x0056B2B0
CURRENT_LEVEL = 0x0056A898
RETURN_PLAYER, RETURN_DISPLAY = 0x005601CC, 0x005601CD
RETURN_CREDITS, RETURN_SAVED, LEAVE_FLAG = 0x00561CDC, 0x00561CE0, 0x00561CBD
TIPS_OPTION, PANEL_OPTION = 0x0055FCC0, 0x0055FCC4
VOLUMES = 0x0055FB3C
CONTROLLER_TABLE = 0x0055FC0C
VOLUME_A, VOLUME_B = 0x00561C84, 0x00561C94
SOUND_SCALE = 0x0052BE44
PLAYING_MOVIE = 0x0052CB38
TEXTURE_PAGE = 0x0056AF58
SPOOL = 0x0056DB4C
PARK_LOCKED = 0x00561450
GAP_TABLE_POINTER = 0x0056DD58    # (the checklist's update leaves the gap table's address here)
SOFTWARE_RENDERER = 0x029DA3A0      # (the fills are clipped in another order with the software renderer)
SCORES = 0x00569EBC

# the words and tables the options functions read and write (beyond the slab and the 4.10d / 4.10e1 regions)
OPT_WORDS = [(RETURN_PLAYER, 4), (LEAVE_FLAG - 1, 4), (RETURN_CREDITS, 12), (TIPS_OPTION, 8), (VOLUMES, 8), (CONTROLLER_TABLE, 0x30), (VOLUME_A, 4), (VOLUME_B, 4),
             (SOUND_SCALE, 8), (PLAYING_MOVIE, 4), (TEXTURE_PAGE, 8), (CURRENT_LEVEL, 4), (SPOOL, 4), (PARK_LOCKED, 4), (SCORES, 0x380), (SOFTWARE_RENDERER, 4), (GAP_TABLE_POINTER, 4)]

# the static data they read: the text pointer variables and strings, the tables of levels, skaters, movies and gaps, the file names
OPT_STATIC = [
    (0x0052CF98, 0x880),        # the movie table (35 entries of 0x24 bytes) and its labels
    (0x0052D4A8, 4),            # the pointer to it
    (0x0052E300, 0x4E0),        # the text pointer variables
    (0x0052EAE0, 0x20),
    (0x00531100, 0x100), (0x00532400, 0x1000),      # the strings they point at (0x531a00..0x532400 is the 4.10d range)
    (0x005252E0, 0x610),        # the file names, formats and fonts
    (0x00527C00, 0x80), (0x0052DFA0, 0x20),
    (0x00535C70, 0x280), (0x00535EF0, 0x40),
    (0x0053A3F8, 0x1CE0),       # the skater rows
    (0x0053C1F8, 0x1780),       # the level table
    (0x0053DF00, 0x840),        # the levels video names (the rows point into 0x53df3c..0x53e714)
    (0x0053E8A0, 0x1320),       # the skaters' face file names
    (0x00541990, 0x4760),       # the gap table (415 entries of 0x2c bytes)
]


def opt_regions(slab):
    return screens_regions(slab) + [(slab + 0x24000, SLAB_OPT - 0x24000)] + OPT_WORDS


def register_opt(group, *, arena_items=True):
    SLAB_BYTES[group] = SLAB_OPT
    EXTRA_REGIONS[group] = opt_regions
    READ_ONLY[group] = list(READ_ONLY_COMMON) + OPT_STATIC
    SEEDS[group] = (lambda env, rng: seed_opt(env, rng, arena_items=arena_items))


def seed_opt(env, rng, arena_items=True):
    from opentony.fcall import write_chunks

    writes = seed_screens(env, rng)
    ivt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, ITEM_SET_POSITION), (2, ITEM_DRAW), (3, ITEM_HIGHLIGHT), (5, ITEM_NAVIGATE), (6, ITEM_SNAP), (8, ITEM_SET_TEXT),
                        (9, ITEM_SET_COLUMN)):
        struct.pack_into("<I", ivt, 4 * slot, entry)
    write_chunks(env.slab + VT_OITEM, bytes(ivt))
    lvt = bytearray(0x40)
    for slot, entry in ((0, VIRTUAL_DELETE), (1, LIST_V1), (2, LIST_V2), (3, LIST_V3), (4, LIST_V4), (5, LIST_V5), (6, LIST_V6), (7, LIST_V7)):
        struct.pack_into("<I", lvt, 4 * slot, entry)
    write_chunks(env.slab + VT_OLIST, bytes(lvt))
    # the arena's blocks (retail's class new zeroes them; the stub does not) start as items: the window's lists lay out the rows its seams construct, which
    # takes their set-position slot with two arguments (the screens' shared vtable has the children's update there); the screen loop's group keeps the shared vtable:
    # its window (an arena block, the constructor being a seam) is updated and drawn through the children's slots
    if arena_items:
        block = struct.pack("<I", env.slab + VT_OITEM)
        for offset in range(0, ARENA2_SIZE, 16):
            write_chunks(env.slab + ARENA2 + offset, block)
    return writes


class Items:
    """The item records and fake lists of one case (each is handed out once)."""

    def __init__(self, env, img, rng):
        self.env, self.img, self.rng = env, img, rng
        self.next_item = 0
        self.next_list = 0

    def item(self, ident, *, value=None, selectable=1, highlighted=0, vtable=None, text=None):
        base = A(self.env, OITEMS + OITEM_STRIDE * self.next_item)
        self.next_item += 1
        item_fields(self.env, self.img, base, self.rng, vtable=A(self.env, VT_OITEM) if vtable is None else vtable, id=ident & 0xFFFFFFFF, selectable=selectable,
                    highlighted=highlighted, value=value, props=self.rng.randrange(4), text=text)
        return base

    def fake_list(self, rows, *, pick=None):
        """A fake list holding the item records `rows`; the item at index `pick` is highlighted (none when pick is -1)."""
        base = A(self.env, OLISTS + OLIST_STRIDE * self.next_list)
        self.next_list += 1
        self.img.garbage(base, 0x1C0, self.rng)
        self.img.u32(base, A(self.env, VT_OLIST))
        for i in range(0x60):
            self.img.u32(base + 0x14 + 4 * i, rows[i] if i < len(rows) else 0)
        self.img.u32(base + 0x194, len(rows))
        chosen = self.rng.randrange(len(rows)) if pick is None else pick
        self.img.u32(base + 0x198, max(chosen, 0))
        for i, row in enumerate(rows):
            self.img.u8(row + 0x0E, 1 if i == chosen else 0)
        return base


def opt_image(env):
    """A fresh image whose clock words are fixed (the running game moves them between the base snapshot and a forced call)."""
    img, rng = new_image(env)
    value = rng.randrange(8, 50000)
    img.u32(0x0056E320, value)
    img.u32(TICK, value)
    img.u32(0x00568624, value - rng.choice([0, 1, 2, 3, 5]))
    return img, rng


def pad_set(img, rng, names, *, chance=0.6, other=0.08):
    """Set the named edges (on either pad); every other edge is rarely set."""
    table = {"back": BACK, "confirm": CONFIRM, "up": UP_A, "down": UP_B, "left": LEFT, "right": RIGHT, "released": RELEASED}
    for name, address in table.items():
        for player in range(2):
            value = 1 if (name in names and rng.random() < chance) else (1 if rng.random() < other else 0)
            img.u8(address + PAD_STRIDE * player, value)
    img.u8(SECOND_PAD, rng.choice([0, 1, 1]))


def opt_world(env, img, rng, *, level=None):
    """The globals the options functions read and write: the common set of the screens, the options and the tables."""
    common(env, img, rng)
    img.u32(CURRENT_LEVEL, rng.randrange(0, 14) if level is None else level)
    img.u8(RETURN_PLAYER, rng.choice([0, 0, 1]))
    img.u8(RETURN_DISPLAY, rng.choice([0, 0, 1]))
    img.u32(RETURN_CREDITS, rng.choice([0, 0, 1, 2]))
    img.u32(RETURN_SAVED, rng.choice([0, 0, 0, 1]))
    img.u8(LEAVE_FLAG, rng.choice([0, 1]))
    img.u32(TIPS_OPTION, rng.choice([0, 1]))
    img.u32(PANEL_OPTION, rng.choice([0, 1]))
    img.u32(VOLUMES, rng.choice([0, 1, 5, 12, 11, rng.randrange(0, 13)]))
    img.u32(VOLUMES + 4, rng.choice([0, 1, 5, 12, 11, rng.randrange(0, 13)]))
    for i in range(12):
        img.u32(CONTROLLER_TABLE + 4 * i, rng.randrange(0, 4) if i < 8 else rng.choice([0, 1]))
    img.u32(VOLUME_A, rng.choice([0, 0x3FFF, rng.randrange(-5, 0x4000) & 0xFFFFFFFF]))
    img.u32(VOLUME_B, rng.choice([0, 0x3FFF, rng.randrange(-5, 0x4000) & 0xFFFFFFFF]))
    img.u32(SOUND_SCALE, rng.getrandbits(32))
    img.u32(SOUND_SCALE + 4, rng.getrandbits(32))
    img.u32(PLAYING_MOVIE, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 6)]))
    img.u32(TEXTURE_PAGE, rng.choice([0, 0]))
    img.u32(TEXTURE_PAGE + 4, 0)
    img.u32(SPOOL, rng.getrandbits(32))
    img.u8(PARK_LOCKED, rng.choice([0, 1]))
    img.u32(SOFTWARE_RENDERER, rng.choice([0, 1]))
    for entry in range(14 * 8):
        base = SCORES + 8 * entry
        img.put(base, bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ .") for _ in range(3)))
        img.u8(base + 3, rng.randrange(0, 20))
        img.u32(base + 4, rng.choice([0, rng.randrange(0, 100000), rng.randrange(0, 2000000000)]))


# ---- stubs -------------------------------------------------------------------------------------------------------------------------------------------------

OPT_THISCALLS = [
    # (address, stack arguments): the other classes' constructors and methods the window calls
    (0x004885D0, 7), (0x00486B10, 7), (0x00485400, 5), (0x004840B0, 5), (0x004860D0, 7), (0x004872B0, 8), (0x004873B0, 7),
    (0x00489240, 0), (0x004892A0, 0), (0x004891A0, 0), (0x00486830, 0), (0x004871E0, 0), (0x00487390, 0), (0x00487470, 0), (0x00488160, 0),
    (0x00418FC0, 7), (0x004189C0, 5), (0x00419290, 5), (0x004A1ED0, 7), (0x0045B0A0, 6), (0x0045B890, 1), (0x004B52B0, 0), (0x00457810, 0), (0x004193C0, 3),
    (0x00454970, 0),
]
# (InitialsWindow_SetLetters is handed three bytes pushed as dwords: the upper bits are the caller's register garbage)
THISCALL_OPTIONS = {0x004193C0: {"masks": {0: 0xFF, 1: 0xFF, 2: 0xFF}}}
# (Career_SetCheat gets the weight as a byte pushed as a dword)
CDECL_OPTIONS = {0x00416A00: {"masks": {1: 0xFF}}}
OPT_CDECLS = [
    # (address, stack arguments)
    (0x00418200, 1), (0x00413C10, 0), (0x004ABC80, 0), (0x004ABE20, 1), (0x004ABCB0, 1), (0x004AB9E0, 0), (0x004ABE50, 0), (0x00454710, 0), (0x00454D10, 1),
    (0x0047A6C0, 3), (0x0047A8A0, 1), (0x00413F30, 1), (0x00416A00, 3), (0x00489440, 8),
    (0x00416050, 1), (0x00416530, 1), (0x004164B0, 1), (0x004B1980, 1),
]


def install_opt_stubs(fc, env):
    """The seams of the options screens: every other class's constructor and methods, and the engine and career calls."""
    if getattr(fc, "_opt_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._opt_installed = True
    rng = env.rng
    install_screen_stubs(fc, env)
    fc.stub(LIST_V1, 3, "list_v1", convention="thiscall", this=True)
    fc.stub(ITEM_SET_TEXT, 1, "item_v8", convention="thiscall", this=True, stack_to=SCRATCH)
    for address, count in OPT_THISCALLS:
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall", this=True, **THISCALL_OPTIONS.get(address, {}))
    for address, count in OPT_CDECLS:
        fc.stub(address, count, "engine_%08x" % address, **CDECL_OPTIONS.get(address, {}))


def reinstall(fc, env, address, count, *, convention="cdecl", this=False, **options):
    """Replace the stub at `address` with one carrying other options (the group's function under test, or a scripted answer)."""
    from frontend_specs_playsel import disable

    name = "engine_%08x" % address
    disable(fc, name)
    fc.stub(address, count, name, convention=convention, this=this, **options)


def scripted(fc, env, name, fn):
    from frontend_specs_playsel import set_result

    set_result(fc, name, fn)


def install_opt_common(fc, env):
    """The answers the window's seams give: constructors hand back the block just allocated; the polls answer 0, 1 or 2; the career's counts are scripted."""
    install_opt_stubs(fc, env)
    if getattr(fc, "_opt_answers", False):
        return
    fc._opt_answers = True
    rng = env.rng
    block = lambda a: env.alloc_log[-1][0]
    for address, count in ((0x004885D0, 7), (0x00486B10, 7), (0x00485400, 5), (0x004840B0, 5), (0x004860D0, 7), (0x004872B0, 8), (0x004873B0, 7), (0x00418FC0, 7),
                           (0x004189C0, 5), (0x00419290, 5), (0x004A1ED0, 7), (0x0045B0A0, 6)):
        options = {"text": True} if address == 0x00418FC0 else {}
        if address == 0x004189C0:
            options = {"stack_to": SCRATCH}
        reinstall(fc, env, address, count, convention="thiscall", this=True, result=block, **options)
    for address in (0x004891A0, 0x00486830, 0x004871E0, 0x00487390, 0x00487470, 0x00488160):
        reinstall(fc, env, address, 0, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 0, 1, 2]))
    for address in (0x00489350, 0x00489390):
        reinstall(fc, env, address, 0, convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    scripted(fc, env, "prompt_window_construct", lambda a: env.alloc_log[-1][0])
    # (the level window builds each picture's file name in a local: the stub records the scratch line the native code builds it at)
    from frontend_specs_playsel import disable

    disable(fc, "sprite_construct")
    fc.stub(0x00457420, 4, "sprite_construct", convention="thiscall", this=True, text=True, stack_to=SCRATCH, result=block)
    fc.stub(0x00416DE0, 0, "engine_00416de0", result=lambda a: rng.choice([-2, -1, 0, 1, 3, 7]))
    fc.stub(0x00487280, 0, "engine_00487280", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(0x004872A0, 0, "engine_004872a0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(0x00488590, 0, "engine_00488590", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(0x004885C0, 0, "engine_004885c0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(0x0046F650, 0, "engine_0046f650", result=lambda a: rng.choice([-1, 0, 1, 2, 3]))
    reinstall(fc, env, 0x004159D0, 2, result=lambda a: rng.choice([0, 1]))


SCRATCH = 0x7FF00400      # where the native code builds the strings retail keeps in locals (ScreenKit's kScratchLine)

GROUP_STUBS = {}

# ---- the window's fields ---------------------------------------------------------------------------------------------------------------------------------------
M_BG, M_VIDEO, M_FONT_A, M_FONT_B = 0xAC, 0xB0, 0xB4, 0x200
M_LISTS = {"main": 0x34C, "display": 0x350, "movies": 0x354, "saved": 0x358}
M_STYLE_A, M_STYLE_B = 0x35C, 0x360
M_ROWS_MAIN, M_ROWS_DISPLAY, M_ROWS_MOVIES, M_ROWS_SAVED = 0x364, 0x4E4, 0x664, 0x7E4
M_TIPS, M_SCORE = 0x974, 0x978
M_ARROW_A, M_ARROW_B = 0x964, 0x968
M_STATE, M_WANTED = 0x96C, 0x970
M_PAGES = {"gaps": 0x97C, "scores": 0x980, "movies": 0x984, "sound": 0x988, "card": 0x98C, "replay": 0x990, "controls": 0x994}
M_MODELS = (0x998, 0x99C, 0x9A0)
M_CHEATS = 0x9A4
M_PROMPTS = 0x9A8
M_SIZE = 0xA00
M_STATES = list(range(0x14, 0x20))
PAGE_OF_STATE = {0x18: "gaps", 0x19: "scores", 0x1A: "movies", 0x1B: "sound", 0x1D: "card", 0x1E: "replay", 0x1F: "controls"}
LIST_OF_STATE = {0x14: "main", 0x16: "display", 0x17: "movies", 0x1C: "saved"}
MAIN_IDS = [1, 2, 3, 4, 5, 0x11, 0xC, 0xD, 0x12, 0x13]
DISPLAY_IDS = [0, 6, 7, 0x13]
MOVIES_IDS = [0, 8, 9, 0xA, 0x13]
SAVED_IDS = [0, 0xE, 0xF, 0x10, 0x13]


def menu_state(env, img, rng, base, pool, items, *, state=None, wanted=None, destroy=False, empty_pages=(), pick=None, ids=None):
    """An options window: the window part, the two fonts, the four lists (fake, holding fake rows), the rows' arrays, the pages, the models and the prompts.
    `empty_pages` names the pages whose slot is left empty; `pick` names the id the list of the state highlights."""
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, M_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + M_BG, keep(pool.take()))
    img.u32(base + M_VIDEO, keep(pool.take()))
    font_object(env, img, rng, base + M_FONT_A)
    font_object(env, img, rng, base + M_FONT_B)
    now = rng.choice(M_STATES) if state is None else state
    img.u32(base + M_STATE, now)
    img.u32(base + M_WANTED, now if wanted is None else wanted)
    # the rows: ids and the item records the lists hold
    shape = {"main": (MAIN_IDS, M_ROWS_MAIN, 0x10), "display": (DISPLAY_IDS, M_ROWS_DISPLAY, 0x0), "movies": (MOVIES_IDS, M_ROWS_MOVIES, 0), "saved": (SAVED_IDS, M_ROWS_SAVED, 0)}
    rows_of = {}
    for name, (id_list, array, _skip) in shape.items():
        use = id_list if (ids is None or name not in ids) else ids[name]
        records = []
        for position, ident in enumerate(use):
            value = rng.choice([1, 2]) if ident in (6, 7) else None
            records.append(items.item(ident, value=value, selectable=0 if (position == 0 and name != "main") else 1))
        rows_of[name] = records
    # (the display list's trick tips and score display rows are also held at +0x974 and +0x978)
    tips, score = rows_of["display"][1], rows_of["display"][2]
    for name, (id_list, array, _skip) in shape.items():
        slots = 0x60
        img.put(base + array, bytes(4 * slots))
        positions = {"main": [0, 1, 2, 3, 4, 5, 6, 7, 8, 10], "display": [0, 1, 2, 3], "movies": [0, 1, 2, 3, 4], "saved": [0, 1, 2, 3, 4]}[name]
        for pos, record in zip(positions, rows_of[name]):
            img.u32(base + array + 4 * pos, record)
    img.u32(base + M_TIPS, keep(tips))
    img.u32(base + M_SCORE, keep(score))
    pick_name = LIST_OF_STATE.get(now)
    for name, slot in M_LISTS.items():
        first = 0 if name == "main" else 1
        chosen = rng.randrange(first, len(rows_of[name]))
        if name == pick_name and pick is not None:
            targets = [i for i, record in enumerate(rows_of[name]) if env_item_id(img, record) == pick]
            chosen = targets[0] if targets else -1
        img.u32(base + slot, keep(items.fake_list(rows_of[name], pick=chosen)))
    img.u32(base + M_STYLE_A, keep(pool.take()))
    img.u32(base + M_STYLE_B, keep(pool.take()))
    img.u32(base + M_ARROW_A, keep(pool.take()))
    img.u32(base + M_ARROW_B, keep(pool.take()))
    for name, slot in M_PAGES.items():
        img.u32(base + slot, 0 if name in empty_pages else keep(pool.take()))
    for slot in M_MODELS:
        img.u32(base + slot, keep(pool.take()))
    img.u32(base + M_CHEATS, keep(pool.take()))
    for i in range(4):
        img.u32(base + M_PROMPTS + 4 * i, keep(pool.take()))
    return now


def env_item_id(img, record):
    return struct.unpack("<I", bytes(img.b[record + 8 + i] for i in range(4)))[0]


# ---- the window ---------------------------------------------------------------------------------------------------------------------------------------------------
MENU = gspec("optmenu")
LOOP = gspec("optloop")
VID = gspec("optvid")
for _group in ("optmenu", "optloop", "optvid", "optstub"):
    register_opt(_group, arena_items=_group != "optloop")
EXTRA_REGIONS["optstub"] = lambda slab: [(slab, 0x100)]
READ_ONLY["optstub"] = []


def spec_at(group, offset):
    """A spec whose `this` is the window at OPT_OBJ[offset] (the module-level specs use the screens' OBJ list: the options window is bigger)."""
    from create_specs import spec

    def make(address, name, this=True):
        resolve = (lambda env: env.slab + OPT_OBJ[0]) if this else None
        return spec(address, name, group, this=resolve)
    return make


def menu_spec(address, name, this=True):
    from create_specs import spec

    return spec(address, name, "optmenu", this=(lambda env: env.slab + OPT_OBJ[0]) if this else None)


def loop_spec(address, name, this=False):
    from create_specs import spec

    return spec(address, name, "optloop", this=(lambda env: env.slab + OPT_OBJ[0]) if this else None)


def vid_spec(address, name, this=None):
    from create_specs import spec

    resolve = (lambda env: env.slab + this) if this is not None else None
    return spec(address, name, "optvid", this=resolve)


def install_menu_group(fc, env):
    install_opt_common(fc, env)


def install_loop_group(fc, env):
    """The loop: the clock advances at every Game_FrameTick; the window's constructor, sync, step, update, draw, apply and delete are seams."""
    from opentony.memory import mem

    install_opt_common(fc, env)
    if getattr(fc, "_opt_loop", False):
        return
    fc._opt_loop = True
    rng = env.rng
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    fc.stub(0x00481210, 0, "engine_00481210", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])
    fc.stub(0x00482770, 0, "engine_00482770", convention="thiscall", this=True)
    fc.stub(0x00482870, 0, "engine_00482870", convention="thiscall", this=True)
    fc.stub(0x00482E60, 0, "engine_00482e60", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2, 2]))
    fc.stub(0x00482A40, 0, "engine_00482a40", convention="thiscall", this=True)
    fc.stub(0x00482950, 2, "engine_00482950", convention="thiscall", this=True)
    fc.stub(0x00482460, 1, "engine_00482460", convention="thiscall", this=True)


def install_vid_group(fc, env):
    from frontend_specs_playsel import disable

    install_opt_common(fc, env)
    disable(fc, "engine_0045b0a0")      # (the constructor under test; the options window's own call to it is a seam)


GROUP_STUBS.update({"optmenu": install_menu_group, "optloop": install_loop_group, "optvid": install_vid_group, "optstub": lambda fc, env: None})


@menu_spec(0x00481210, "OptionsMenu_Construct")
def gen_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    img.put(A(env, OPT_OBJ[0]), bytes(M_SIZE))      # (retail's class new zeroes the block: the constructor relies on it for the fields it never sets)
    return [], img.pokes()


@menu_spec(0x00482460, "OptionsMenu_ScalarDelete")
def gen_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@menu_spec(0x00482480, "OptionsMenu_Destroy")
def gen_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@menu_spec(0x004826F0, "OptionsMenu_SetPrompts")
def gen_set_prompts(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [rng.choice([0, 1, 2, 0x100, rng.getrandbits(32)]), rng.choice([0, 1, 2, 4, 8, 15, 5, 10, rng.getrandbits(32)])], img.pokes()


@menu_spec(0x00482770, "OptionsMenu_Sync")
def gen_sync(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@menu_spec(0x00482870, "OptionsMenu_Apply")
def gen_apply(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@menu_spec(0x00482950, "OptionsMenu_Draw")
def gen_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng), state=rng.choice(M_STATES + [0x18, 0x19]))
    return [coords(rng), coords(rng)], img.pokes()


@menu_spec(0x00482A40, "OptionsMenu_Update")
def gen_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, ["up", "down"])
    state = rng.choice(M_STATES + [0x13, 0x20])
    wanted = state if rng.random() < 0.7 else rng.choice(M_STATES)
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng), state=state, wanted=wanted)
    return [], img.pokes()


@menu_spec(0x00482E60, "OptionsMenu_Step")
def gen_step(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["confirm"], ["back"], ["up", "down"], ["confirm", "up"], []]), chance=0.7)
    state = rng.choice(M_STATES + [0x13, 0x20])
    # the entries that build a page need its slot empty (a full slot is the "Possible memory leak" path, tried now and then)
    empty = [name for name in M_PAGES if rng.random() < 0.85]
    if state in PAGE_OF_STATE and rng.random() < 0.9:
        empty = [name for name in empty if name != PAGE_OF_STATE[state]]
    pick = None
    if state == 0x14:
        pick = rng.choice(MAIN_IDS + [0xB, 6])
    elif state == 0x16:
        pick = rng.choice(DISPLAY_IDS + [1, 0x13])
    elif state == 0x17:
        pick = rng.choice(MOVIES_IDS + [0xB, 0x10])
    elif state == 0x1C:
        pick = rng.choice(SAVED_IDS + [0x11, 0x12])
    menu_state(env, img, rng, A(env, OPT_OBJ[0]), Pool(env, img, rng), Items(env, img, rng), state=state, empty_pages=empty, pick=pick)
    return [], img.pokes()


@menu_spec(0x00482450, "Options_CheatsAvailable", this=False)
def gen_cheats_available(env, n):
    img, rng = opt_image(env)
    return [], img.pokes()


# ---- the screen loop and its callback ---------------------------------------------------------------------------------------------------------------------------

@loop_spec(0x00481200, "Options_ScreenCall")
def gen_screen_call(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    img.u32(A(env, OPT_OBJ[3]), rng.getrandbits(32))
    return [A(env, OPT_OBJ[3]) + 8, A(env, OPT_OBJ[3])], img.pokes()


@loop_spec(0x00483EF0, "Options_Run")
def gen_run(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    return [], img.pokes()


# ---- the level information window and the video menu --------------------------------------------------------------------------------------------------------------

@vid_spec(0x0045B0A0, "VideoWindow_Construct", this=OPT_OBJ[0])
def gen_video_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    img.garbage(A(env, OPT_OBJ[0]), 0x14C, rng)
    return [rng.randrange(0, 14), rng.choice([0, 1]), rng.choice([0x11A, 0x80, 0, coords(rng)]), rng.choice([0xFFFFFFFA, 0, 5]), 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@vid_spec(0x004CEA70, "VideoMenu_Destroy", this=OPT_OBJ[0])
def gen_video_menu_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    base = A(env, OPT_OBJ[0])
    pool = Pool(env, img, rng)
    window(env, img, rng, base, 0x3C0, pool, children=rng.randrange(0, 3))
    keep = lambda value: 0 if rng.random() < 0.2 else value
    img.u32(base + 0x238, keep(pool.take()))
    img.u32(base + 0x388, keep(pool.take()))
    img.u32(base + 0x3B0, keep(pool.take()))
    for i in range(4):
        img.u32(base + 0x38C + 4 * i, keep(pool.take()))
    font_object(env, img, rng, base + 0x23C)
    font_object(env, img, rng, base + 0xE8)
    return [], img.pokes()


def stub_spec(address, name, group="optstub", *, returns=False):
    from create_specs import spec

    return spec(address, name, group)


for _address, _name in ((0x00485280, "Options_ReturnZeroA"), (0x004860C0, "Options_ReturnZeroB"), (0x004893B0, "Options_ReturnZeroC"), (0x004893C0, "Options_NothingA"),
                        (0x004893D0, "Options_ReturnZeroD"), (0x004893E0, "Options_ReturnZeroE"), (0x004893F0, "Options_ReturnZeroF"), (0x00489400, "Options_NothingB"),
                        (0x00489410, "Options_NothingC"), (0x00489420, "Options_NothingD"), (0x00489430, "Options_NothingE"), (0x00489440, "Options_ReturnZeroG"),
                        (0x00489450, "Options_NothingF"), (0x00489460, "Options_ReturnZeroH"), (0x00489470, "Options_ReturnZeroI"), (0x00489480, "Options_NothingG"),
                        (0x00489490, "Options_NothingH")):
    def _make(address=_address, name=_name):
        @stub_spec(address, name)
        def gen(env, n):
            img, rng = new_image(env)      # (no clock words: the running game moves them while the call runs, and these functions read nothing)
            img.u32(A(env, 0), rng.getrandbits(32))
            return [], img.pokes()
        return gen
    _make()
