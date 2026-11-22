"""Phase 4.10e2 case generators, part 2: the trick-list screen (tricksel.cpp 0x004bf860..0x004c2000, trickwin.cpp 0x004c2000..0x004c4400).

The slab is the 4.10e1 one (frontend_specs_screens.py: the menu group's fake vtables, fonts and strings, the pages and lists of 4.10d, the fake object
pool, the real ConfirmBox state builders) with more room behind it:

  0x1c000  the trick menu object (0x2200)      0x1e200  the trick screen window (0x300)      0x1e500  the trick panel (0x100)
  0x1e600  four fake lists of 0x1c0 bytes      0x1ed00  a combination record (0x20)          0x1ee00  out buffers (0x100)
  0x1f000  widgets: 0x100 bytes each, 120 of them    0x26800  the allocation arena (blocks start with the fake vtable VT_ANY)

The trick menu owns the trick manager at +0xd44: its records (0x28 bytes, from +4) and count (+0x1404) are poked per case, with distinct source offsets and
names; the binding block is the character's career record at +0xcc.  The lists are fake (their methods are reported services) and the combination and trick
widgets fake rows whose second column is set through the reported method 9; constructors build the real rows in the arena.  The career's purchase test and
spending, the cheat test and the trick manager's constructor and unlock state are seams.
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES
from frontend_specs_menu import (ELEM_HOOK, CHILD_DRAW, CHILD_UPDATE, FIND_FONTS, FONT_SIZE, PROPS, VIRTUAL_DELETE, coords, poke_props, props_at, sprite_at)
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_screens import (ANIM, CAMERA, CACHED_DATABASE, DATABASE, EDITOR_MODE, FRONT_FLAG, LAST_TICK, OBJ, PRE_MANAGER, POOL, POOL_N, POOL_STRIDE, RENDERER,
                                    RENDERER_POINTER, SKATER, SNAP_X, SNAP_Y, SPIN_LEFT, SPIN_RIGHT, TEXT_POOL, TEXT_STRIDE, TEXTS, TICK, VT_ANY, ANIM_SCALE,
                                    install_screen_stubs, READ_ONLY_COMMON, regions as screens_regions)
from frontend_specs_screens_cases import Pool, common, confirm_state, font_object, gspec, skater, text_ptr, window
from frontend_specs_skstat import text_element
from frontend_specs_skwin import (CAREER, CUSTOM, MANAGER, PADS, VT_WINDOW, VT_WITEM, A, new_image, world)
from frontend_specs_skwin import CUSTOM as _CUSTOM  # noqa: F401
from frontend_specs_skwin import READ_ONLY_RANGES as SKWIN_READ_ONLY, seed_skwin
from frontend_specs_menu import ARENA as _MENU_ARENA  # noqa: F401
from frontend_specs_menu import Image  # noqa: F401

TSLAB = 0x30000
TMENU = 0x1C000
TSCREEN = 0x1E200
TPANEL = 0x1E500
TLISTS = 0x1E600
TLIST_STRIDE = 0x1C0
TCOMBO = 0x1ED00
TOUT = 0x1EE00
TWID = 0x1F000
TWID_STRIDE = 0x100
TWID_N = 120
TARENA = 0x26800
TARENA_SIZE = TSLAB - TARENA

# the trick menu's fields (create_screens.hpp / trick_screens.hpp)
M_FONTS = (0xAC, 0x1F8, 0x344, 0x490, 0x5DC, 0x728)
M_CATEGORY_LIST, M_CATEGORY_ROWS = 0x874, 0x878
M_COMBO_LIST, M_COMBO_ROWS = 0x9F8, 0x9FC
M_PAGE_LIST, M_PAGE_ROWS = 0xB7C, 0xB80
M_STYLES = (0xD00, 0xD04, 0xD08, 0xD0C, 0xD10, 0xD14)
M_DIALOG, M_WARNING, M_ARROW_A, M_ARROW_B, M_PANEL, M_HEADER_A, M_HEADER_B = 0xD18, 0xD1C, 0xD20, 0xD24, 0xD28, 0xD2C, 0xD30
M_PROMPTS = 0xD34
M_MANAGER = 0xD44
M_STATE, M_BLOCK = 0x2150, 0x2154
MENU_SIZE = 0x2158

COMBO_TABLE = 0x00540CB8      # the ordinary combinations: 43 records of 12 bytes
SPECIAL_TABLE = 0x00540E30    # the special combinations (zero-terminated)
N_COMBOS = 43                 # the ordinary slots of a binding block
N_ORDINARY = 30               # the ordinary combinations table's records (the retail table ends with a zero count)
N_SPECIAL_TABLE = 36          # the special combinations table's records
N_SPECIAL = 5                 # the special slots of a binding block

# the retail globals this group reads and writes beyond the screens' set
TRICK_KEYS = 0x00568688       # the character key table (DAT_00568688)
PLAYER_TABLES = 0x00568A4C    # PlayerTwoTrickScriptTable, PlayerOneTrickScriptTable
SPECIAL_BLOCK = 0x0056A690    # the first character's special costume's binding block
ROWS = 0x0053A3B8             # the character rows (0x170 bytes each)

for _group in ("trkscr", "trkloop"):
    SLAB_BYTES[_group] = TSLAB


def tregions(slab):
    base = [r for r in screens_regions(slab) if r[0] != slab + 0x18000]
    # the screens' slab windows end at 0x18000; this slab is bigger: capture from there to the end
    return base + [(slab + 0x18000, TSLAB - 0x18000), (TRICK_KEYS, 4), (PLAYER_TABLES, 8), (SPECIAL_BLOCK, 0x40), (ROWS, 0x1D80)]


# the character rows are mutable here (RequirementsMet reads their flags and unlocked words): not part of the static data
STATIC_EXTRA = [(0x0052E2F0, 0x30), (0x00541830, 0x20), (0x005252C0, 0x10), (0x00541900, 0x40), (0x00540CB8, 0x340), (0x005417A0, 0x60), (0x00527C10, 0x150), (0x00541860, 0x40), (0x005417D0, 0x40), (0x005418A0, 0x50)]


def _static():
    out = []
    for address, size in READ_ONLY_COMMON:
        if address == ROWS:
            continue
        out.append((address, size))
    return out + STATIC_EXTRA


def seed_trick(env, rng):
    from opentony.fcall import write_chunks

    writes = seed_skwin(env, rng)
    env.alloc_base = TARENA
    env.alloc_size = TARENA_SIZE
    vt = bytearray(0x40)
    from frontend_specs_menu import ITEM_DRAW, ITEM_HIGHLIGHT, ITEM_NAVIGATE, ITEM_SET_POSITION, ITEM_SNAP, SPRITE_DRAW

    for slot, entry in ((0, VIRTUAL_DELETE), (1, CHILD_UPDATE), (2, CHILD_DRAW), (3, SPRITE_DRAW), (5, SNAP_X), (6, SNAP_Y), (7, ELEM_HOOK)):
        struct.pack_into("<I", vt, 4 * slot, entry)
    write_chunks(env.slab + VT_ANY, bytes(vt))
    block = struct.pack("<I", env.slab + VT_ANY)
    for offset in range(0, TARENA_SIZE, 16):
        write_chunks(env.slab + TARENA + offset, block)
    for i in range(POOL_N):
        write_chunks(env.slab + POOL + POOL_STRIDE * i, block)
    for i, text in enumerate(TEXT_POOL):
        write_chunks(env.slab + TEXTS + TEXT_STRIDE * i, text + b"\0")
    write_chunks(env.slab + SKATER, block)
    write_chunks(env.slab + ANIM, block)
    return writes


for _group in ("trkscr", "trkloop"):
    EXTRA_REGIONS[_group] = tregions
    READ_ONLY[_group] = _static()
    SEEDS[_group] = seed_trick

# ---- stubs -----------------------------------------------------------------------------------------------------------------------------------
# (address, stack arguments, thiscall) of the engine functions the screen reaches beyond the 4.10e1 set; the first three also give a result
TRICK_ENGINE = [(0x004BBD70, 2, True), (0x004BB790, 0, True), (0x004A1540, 1, True)]


def install_trick_stubs(fc, env):
    if getattr(fc, "_trick_installed", False):      # (a run with several groups installs the shared stubs once)
        return
    fc._trick_installed = True
    rng = env.rng
    install_screen_stubs(fc, env)
    # Tricks_InitializeManager: the menu's constructor calls it on a member, before any allocation (the 4.10e1 stub hands back the last block)
    for probe in fc._probes:
        if getattr(probe, "name", "") == "engine_004bb4f0":
            probe.result = lambda a: env.alloc_log[-1][0] if env.alloc_log else 0
    # the 4.10e1 seams for the trick menu's own functions (the editor's trick page: construct, highlight-first, update) must not intercept them here
    for probe in fc._probes:
        if getattr(probe, "name", "") in ("engine_004c0430", "engine_004c29d0", "engine_004c3d30"):
            probe.enabled = False
    # the fake rows' ninth method (a row's second column; the 4.10d pages' stub)
    fc.stub(0x004AD020, 4, "item_set_column", convention="thiscall", this=True, text=True, keep=slice(1, 4))
    fc.stub(0x00416940, 1, "engine_00416940", result=lambda a: rng.choice([0, 0, 1]))
    fc.stub(0x00416070, 2, "engine_00416070", result=lambda a: rng.choice([0, 1]))
    fc.stub(0x00416140, 3, "engine_00416140", result=lambda a: rng.choice([0, 1, 1]))
    for address, count, thiscall in TRICK_ENGINE:
        fc.stub(address, count, "engine_%08x" % address, convention="thiscall" if thiscall else "cdecl", this=thiscall)
    fc.stub(0x004A1440, 4, "engine_004a1440", convention="thiscall", this=True, result=lambda a: env.alloc_log[-1][0])


def install_trick_loop_stubs(fc, env):
    """The loop: the clock advances at every Game_FrameTick, and the screen's step, update, draw and delete are seams with scripted answers."""
    from opentony.memory import mem

    rng = env.rng
    install_trick_stubs(fc, env)
    fc.stub(0x004F7CE0, 0, "engine_004f7ce0", writes=[(TICK, 4, lambda a: (mem.u32(TICK) + rng.choice([1, 2, 3])) & 0xFFFFFFFF)])
    step, update, draw, delete = 0x004C0230, 0x004C0200, 0x004C0120, 0x004BFA90
    fc.stub(step, 0, "engine_%08x" % step, convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 1, 2]))
    fc.stub(update, 0, "engine_%08x" % update, convention="thiscall", this=True)
    fc.stub(draw, 2, "engine_%08x" % draw, convention="thiscall", this=True)
    fc.stub(delete, 1, "engine_%08x" % delete, convention="thiscall", this=True)


GROUP_STUBS = {"trkscr": install_trick_stubs, "trkloop": install_trick_loop_stubs}

TRICK = None


def tspec(group):
    def make(address, name, this=None):
        from create_specs import spec

        resolve = (lambda env: env.slab + this) if this is not None else None
        return spec(address, name, group, this=resolve)
    return make


# ---- state builders --------------------------------------------------------------------------------------------------------------------------

def props_ready(env, img, rng):
    for i in range(4):
        poke_props(env, img, i, rng)


def widget(env, img, rng, index, ident, *, selectable=1):
    """A fake row (its second column is set through the reported method 9): id at +8."""
    from frontend_specs_menu import item_fields

    base = A(env, TWID + TWID_STRIDE * index)
    item_fields(env, img, base, rng, vtable=A(env, VT_WITEM), id=ident & 0xFFFFFFFF, selectable=selectable, props=rng.randrange(4))
    return base


def tlist(env, img, rng, index, items, *, highlighted=None):
    """A fake list (VT_LIST of the menu group: its methods are reported) holding `items`; one is highlighted."""
    base = A(env, TLISTS + TLIST_STRIDE * index)
    img.garbage(base, TLIST_STRIDE, rng)
    img.u32(base, A(env, 0x00C0))
    for i in range(0x60):
        img.u32(base + 0x14 + 4 * i, items[i] if i < len(items) else 0)
    img.u32(base + 0x194, len(items))
    pick = rng.randrange(0, max(len(items), 1)) if highlighted is None else highlighted
    img.u32(base + 0x198, max(pick, 0))
    for i, item in enumerate(items):
        img.u8(item + 0x0E, 1 if i == pick else 0)
    return base


def sources(rng, count):
    """Distinct source offsets (u16)."""
    out = set()
    while len(out) < count:
        out.add(rng.randrange(0x40, 0x4000, 2))
    return list(out)


WORDS = ["kickflip", "heelflip", "ollie", "nollie", "fakie", "indy", "melon", "nosegrab", "tailgrab", "lipslide", "boardslide", "noseslide", "crooked",
         "feeble", "smith", "5-0", "manual", "nose manual", "casper", "darkslide", "impossible", "varial", "hardflip", "pop shuvit", "frontside", "backside"]


def manager_state(env, img, rng, menu, *, count=None, classes=None, specials=1):
    """The trick manager inside the menu: `count` records with distinct names and source offsets, costs and class flags."""
    count = rng.randrange(4, 36) if count is None else count
    base = menu + M_MANAGER
    src = sources(rng, count)
    img.u32(base, 0x00519828)
    img.u32(base + 0x1404, count)
    img.u32(base + 0x1408, rng.randrange(0x1000, 0x100000))
    records = []
    for i in range(count):
        name = ("%s %d" % (rng.choice(WORDS), i)).encode()[:31]
        flags = rng.choice([0x100, 0x200, 0x800, 0x2000, 0x4000, 0x8000, 0x8800, 0x4800, 0x8100, 0x300, 0x100, 0x200, 0x800, 0x4000]) | rng.choice(
            [0, 0, 0x01, 0x08, 0x10, 0x20, 0x40, 0x80, 0x88, 0x90])
        cost = rng.choice([0, 0, rng.randrange(1, 12), rng.randrange(1, 500), rng.randrange(1, 9000)])
        record = base + 4 + 0x28 * i
        img.garbage(record, 0x28, rng)
        img.put(record, name + b"\0")
        img.u16(record + 0x20, src[i])
        img.u16(record + 0x22, cost)
        img.u16(record + 0x24, flags)
        img.u16(record + 0x26, rng.randrange(0, 0x10000))
        records.append({"name": name, "source": src[i], "cost": cost, "flags": flags})
    return records


def block_state(env, img, rng, menu, count, *, character=None, page_combos=None):
    """The binding block the menu points at (a character's career record's +0xcc, or the special costume's): ordinary slots, five special slots and combos."""
    state = {}
    use_special = rng.random() < 0.1
    block = SPECIAL_BLOCK if use_special else CAREER + 0x104 * (character if character is not None else rng.randrange(0, 20)) + 0xCC
    img.u32(menu + M_BLOCK, block)
    for i in range(N_COMBOS):
        img.u8(block + i, 0xFF if rng.random() < 0.5 else rng.randrange(0, count))
    used = set()
    for slot in range(N_SPECIAL):
        if rng.random() < 0.4:
            record = rng.randrange(0, count)
            while record in used:
                record = rng.randrange(0, count)
            used.add(record)
            img.u8(block + 0x2B + slot, record)
            img.u8(block + 0x30 + slot, rng.choice(page_combos) if page_combos and rng.random() < 0.7 else rng.randrange(0, N_SPECIAL_TABLE))
        else:
            img.u8(block + 0x2B + slot, 0xFF)
            img.u8(block + 0x30 + slot, 0xFF if rng.random() < 0.7 else rng.randrange(0, N_SPECIAL_TABLE))
    state["block"] = block
    return block


def menu_object(env, img, rng, pool, *, state=None, destroy=False, count=None, category=None, combo=None, with_page=True, with_widgets=True, dialog="none",
                special_page=None, character=None, real_pick=False):
    """A trick menu in a legal state: the window part, the manager and the block, the three lists with their widgets, styles, arrows, panel, texts, prompts
    and warning box.  Returns what the case needs to know."""
    base = A(env, TMENU)
    props_ready(env, img, rng)
    img.garbage(base, 0x100, rng)
    img.garbage(base + 0xD00, 0x48, rng)
    img.garbage(base + M_STATE, 8, rng)
    window(env, img, rng, base, 0xAC, pool, children=rng.randrange(0, 3))
    img.u32(base, A(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for slot in M_FONTS:
        font_object(env, img, rng, base + slot)
    info = {}
    records = manager_state(env, img, rng, base, count=count)
    n = len(records)
    ids = [5, 6, 7, 9, 10, 11]
    pick_category = rng.choice([6, 7, 9, 10, 10, 11]) if category is None else category
    pick_index = ids.index(pick_category) if pick_category in ids else rng.randrange(1, len(ids))
    special = ids[pick_index] == 10 if special_page is None else special_page
    combo_ids = sorted(rng.sample(range(N_SPECIAL_TABLE), rng.randrange(3, 12))) if special else sorted(rng.sample(range(N_ORDINARY), rng.randrange(3, 12)))
    block = block_state(env, img, rng, base, n, character=character, page_combos=combo_ids if special else None)
    chosen = rng.choice([0, 1, 2, 3, 4, 9]) if state is None else state
    img.u32(base + M_STATE, chosen)
    # the category list: title (id 5), the pages (6, 7, 9, 10) and "Continue" (11)
    rows = [widget(env, img, rng, i, ident) for i, ident in enumerate(ids)]
    img.u32(base + M_CATEGORY_LIST, keep(tlist(env, img, rng, 0, rows, highlighted=pick_index)))
    for i in range(0x60):
        img.u32(base + M_CATEGORY_ROWS + 4 * i, rows[i] if i < len(rows) else 0)
    info["category_id"] = ids[pick_index]
    # the combination list: one row per combination of the page (the special page's are the special table's), then the blank row
    combos = [widget(env, img, rng, 10 + i, ident) for i, ident in enumerate(combo_ids)] + [widget(env, img, rng, 10 + len(combo_ids), -1)]
    pick_combo = rng.randrange(0, len(combo_ids)) if combo is None else combo_ids.index(combo)
    if with_widgets:
        img.u32(base + M_COMBO_LIST, keep(tlist(env, img, rng, 1, combos, highlighted=pick_combo)))
        for i in range(0x60):
            img.u32(base + M_COMBO_ROWS + 4 * i, combos[i] if i < len(combos) else 0)
    info["combo_id"] = combo_ids[pick_combo]
    info["combo_ids"] = combo_ids
    # the trick list: one row per trick (id = its source offset), then the blank row
    if with_page:
        shown = rng.sample(range(n), min(n, rng.randrange(1, 8)))
        trows = [widget(env, img, rng, 60 + i, records[k]["source"]) for i, k in enumerate(shown)] + [widget(env, img, rng, 60 + len(shown), -1)]
        tpick = rng.randrange(0, len(trows) - (1 if real_pick else 0))      # (the last row is the blank one: a buy needs a trick)
        img.u32(base + M_PAGE_LIST, keep(tlist(env, img, rng, 2, trows, highlighted=tpick)))
        for i in range(0x60):
            img.u32(base + M_PAGE_ROWS + 4 * i, trows[i] if i < len(trows) else 0)
        info["page_pick"] = tpick
        info["shown"] = [records[k] for k in shown]
    for slot in M_STYLES:
        img.u32(base + slot, keep(pool.take()))
    box = A(env, OBJ[1])
    if dialog == "real":
        confirm_state(env, img, rng, box, pool, complete=not destroy)
        img.u32(base + M_DIALOG, box)
    elif dialog == "fake":
        img.u32(base + M_DIALOG, pool.take())
    else:
        img.u32(base + M_DIALOG, 0)
    # the warning text box: a pool object
    img.u32(base + M_WARNING, keep(pool.take()))
    img.u32(base + M_ARROW_A, keep(pool.take()))
    img.u32(base + M_ARROW_B, keep(pool.take()))
    img.u32(base + M_PANEL, panel_object(env, img, rng, pool))
    img.u32(base + M_HEADER_A, text_element(env, img, rng, 12))
    img.u32(base + M_HEADER_B, text_element(env, img, rng, 13))
    for i in range(4):
        img.u32(base + M_PROMPTS + 4 * i, pool.take())
    info.update({"records": records, "count": n, "base": base})
    return info


def panel_object(env, img, rng, pool, *, destroy=False, address=None):
    """The panel window: the cash counter and its label, the "n of 5" text and its label, the glyph text and its label, three timers and three counts."""
    base = A(env, TPANEL) if address is None else address
    window(env, img, rng, base, 0xDC, pool, children=rng.randrange(0, 3))
    img.u32(base, A(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + 0xAC, keep(pool.take()))
    img.u32(base + 0xB0, keep(pool.take()))
    img.u32(base + 0xB4, text_element(env, img, rng, 14))
    img.u32(base + 0xB8, keep(pool.take()))
    img.u32(base + 0xBC, text_element(env, img, rng, 15))
    img.u32(base + 0xC0, keep(pool.take()))
    for i in range(3):
        img.u32(base + 0xC4 + 4 * i, rng.choice([0, 1, 5, 11, 20, 0xFFFFFFFF]))
        img.u32(base + 0xD0 + 4 * i, rng.choice([0, 0, 1, 2, 4]))
    return base


def trick_state(env, img, rng, *, character=None):
    """The skater manager and the career records (the current character's cash is poked too)."""
    slot, chosen = skater(env, img, rng, character=character)
    return slot, chosen


# ---- more state builders -----------------------------------------------------------------------------------------------------------------------

from frontend_specs_screens import VT_REAL  # noqa: E402
from frontend_specs_screens_cases import box_state, custom_character, real_vtable  # noqa: E402

PAD0 = {"back": 0x0056AFF9, "square": 0x0056B009, "triangle": 0x0056B039, "confirm": 0x0056B029, "up": 0x0056B099, "up2": 0x0056B0A9}
PAD_STRIDE = 0x16C


def set_pad(img, rng, pad, *, only=None, noise=0.15):
    """The six edges of the active pad: `only` is set, the others are set now and then; the other pad gets random edges."""
    for name, address in PAD0.items():
        on = (rng.random() < 0.3) if only is None else (name == only or rng.random() < noise)
        img.u8(address + PAD_STRIDE * pad, 1 if on else 0)
        img.u8(address + PAD_STRIDE * (1 - pad), rng.choice([0, 0, 1]))


def trick_world(env, img, rng, *, character=None):
    """The clock, pads and the skater state; returns the active pad."""
    common(env, img, rng)
    pad = rng.choice([0, 0, 1])
    img.u32(0x0056A888, pad)
    slot, chosen = skater(env, img, rng, character=character)
    return pad


def style_objects(env, img, rng, base):
    """Real MenuProperties for the styles (a list row built by the toolkit reads its font)."""
    for i, slot in enumerate(M_STYLES):
        img.u32(base + slot, props_at(env, i % 4))


def screen_state(env, img, rng, pool, *, destroy=False, menu=None):
    base = A(env, TSCREEN)
    window(env, img, rng, base, 0x258, pool, children=rng.randrange(0, 3))
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    img.u32(base + 0xAC, keep(pool.take()))
    font_object(env, img, rng, base + 0xB0)
    img.u32(base + 0x1FC, pool.take())
    img.u32(base + 0x200, pool.take() if menu is None else menu)
    img.u32(base + 0x204, rng.choice([-1, 0, 7, 0x3F]) & 0xFFFFFFFF)
    img.u32(base + 0x208, rng.randrange(0, 0x40))
    anim = A(env, ANIM)
    img.garbage(anim + 4, 0x17C, rng)
    img.u32(anim, A(env, VT_ANY))
    img.u32(base + 0x20C, keep(anim))
    for i in range(7):
        img.u16(base + 0x210 + 2 * i, rng.randrange(0, 0x10000))
    return base


# ---- the screen (tricksel.cpp) -----------------------------------------------------------------------------------------------------------------

T = tspec("trkscr")
L = tspec("trkloop")


@T(0x004BF870, "TrickScreen_Construct", this=TSCREEN)
def gen_screen_construct(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    img.garbage(A(env, TSCREEN), 0x258, rng)
    return [], img.pokes()


@T(0x004BFA90, "TrickScreen_ScalarDelete", this=TSCREEN)
def gen_screen_delete(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    screen_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@T(0x004BFAB0, "TrickScreen_Destroy", this=TSCREEN)
def gen_screen_destroy(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    screen_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@T(0x004BFC30, "TrickScreen_LoadFont", this=TSCREEN)
def gen_screen_font(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, TSCREEN), 0x258, rng)
    return [], img.pokes()


@T(0x004BFD30, "TrickScreen_SetupScene", this=TSCREEN)
def gen_screen_scene(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    skater(env, img, rng, character=custom_character(env, rng, rng.random() < 0.5))
    img.garbage(A(env, TSCREEN), 0x258, rng)
    return [], img.pokes()


@T(0x004C0120, "TrickScreen_Draw", this=TSCREEN)
def gen_screen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@T(0x004C0200, "TrickScreen_UpdateFrame", this=TSCREEN)
def gen_screen_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@T(0x004C0230, "TrickScreen_Step", this=TSCREEN)
def gen_screen_step(env, n):
    img, rng = new_image(env)
    pad = trick_world(env, img, rng)
    pool = Pool(env, img, rng)
    state = rng.choice([0, 1, 2, 3, 4, 9])
    set_pad(img, rng, pad)
    menu = update_menu(env, img, rng, pool, state)
    screen_state(env, img, rng, pool, menu=menu)
    return [], img.pokes()


# ---- the menu (tricksel.cpp) -----------------------------------------------------------------------------------------------------------------

M = tspec("trkscr")


def update_menu(env, img, rng, pool, state, *, only=None):
    """A menu ready for one update in `state` (its dialog, lists, widgets and block legal for it); returns its address."""
    dialog = "real" if state == 3 else "none"
    info = menu_object(env, img, rng, pool, state=state, dialog=dialog, special_page=None, real_pick=state == 3)
    base = info["base"]
    style_objects(env, img, rng, base)
    if state == 4:
        img.u32(base + M_WARNING, pool.take())
    return base


@M(0x004C0430, "TrickMenu_Construct", this=TMENU)
def gen_menu_construct(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    img.garbage(A(env, TMENU), 0x100, rng)
    img.garbage(A(env, TMENU) + 0xD00, 0x48, rng)
    img.garbage(A(env, TMENU) + M_STATE, 8, rng)
    return [coords(rng), coords(rng)], img.pokes()


@M(0x004C0E90, "TrickMenu_ScalarDelete", this=TMENU)
def gen_menu_delete(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    menu_object(env, img, rng, Pool(env, img, rng), destroy=True, dialog=rng.choice(["none", "real", "fake"]))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@M(0x004C0EB0, "TrickMenu_Destroy", this=TMENU)
def gen_menu_destroy(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    menu_object(env, img, rng, Pool(env, img, rng), destroy=True, dialog=rng.choice(["none", "real", "fake"]))
    return [], img.pokes()


@M(0x004C1080, "TrickMenu_Draw", this=TMENU)
def gen_menu_draw(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    pool = Pool(env, img, rng)
    state = rng.choice([0, 1, 2, 3, 4, 9, 2, 4])
    info = menu_object(env, img, rng, pool, state=state)
    base = info["base"]
    img.u32(base + 0x14, rng.choice([0, 0, 0, 1]))
    if state == 4:
        # the warning box: a real text box now and then (the draw is its own method), else a fake one (a reported element update)
        if rng.random() < 0.6:
            box = A(env, OBJ[2])
            box_state(env, img, rng, box, vtable=VT_REAL["textbox"])
            img.u32(base + M_WARNING, box)
    return [coords(rng), coords(rng)], img.pokes()


@M(0x004C13C0, "TrickMenu_UpdateFrame", this=TMENU)
def gen_menu_update_frame(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    base = A(env, TMENU)
    window(env, img, rng, base, 0xAC, Pool(env, img, rng), hidden=rng.choice([0, 0, 1]), children=rng.randrange(0, 3))
    return [], img.pokes()


@M(0x004C13D0, "TrickMenu_BuildCategoryList", this=TMENU)
def gen_menu_categories(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    pool = Pool(env, img, rng)
    base = A(env, TMENU)
    img.garbage(base, 0x100, rng)
    img.garbage(base + M_CATEGORY_LIST, 0x4, rng)
    img.garbage(base + 0xD00, 0x48, rng)
    img.u32(base + M_ARROW_A, pool.take())
    img.u32(base + M_ARROW_B, pool.take())
    return [], img.pokes()


# ---- trickwin.cpp: the lists ---------------------------------------------------------------------------------------------------------------------

from frontend_specs_screens_cases import read_u32  # noqa: E402


def tricks_keys(env, img, rng, *, equal=0.12):
    """DAT_00568688 and the players' tables: each table pointer differs from keys + 2 * (the player's character) unless `equal` says otherwise (then the
    assertion fails and retail goes on)."""
    keys = rng.randrange(0x100000, 0x200000)
    img.u32(TRICK_KEYS, keys)
    for player in (0, 1):
        character = read_u32(img, MANAGER + 4 + player * 0xC)
        value = keys + 2 * character if rng.random() < equal else rng.randrange(0x300000, 0x400000)
        # the second player's table is at 0x568a4c, the first's at 0x568a50
        img.u32(PLAYER_TABLES + (4 if player == 0 else 0), value)


def character_rows(env, img, rng):
    """The character rows' flags and unlocked words: random marks and unlocks (RequirementsMet reads them)."""
    for character in range(20):
        row = ROWS + 0x170 * character
        flags = env.u32(row + 0x15C)
        flags &= ~0x1D8
        for bit in (0x100, 0x80, 0x40, 0x08, 0x10):
            if rng.random() < 0.15:
                flags |= bit
        img.u32(row + 0x15C, flags)
        img.u32(row + 0x158, rng.choice([0, 1, 1]))


def page_menu(env, img, rng, pool, *, state, category=None, dialog="none", real_pick=False, with_page=True, with_widgets=True):
    info = menu_object(env, img, rng, pool, state=state, dialog=dialog, category=category, with_page=with_page, with_widgets=with_widgets, real_pick=real_pick)
    base = info["base"]
    style_objects(env, img, rng, base)
    tricks_keys(env, img, rng)
    return info


@M(0x004C2000, "TrickMenu_RebuildComboList", this=TMENU)
def gen_rebuild(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    page_menu(env, img, rng, Pool(env, img, rng), state=1, category=rng.choice([6, 7, 9, 10, 10, 11]), with_page=False)
    return [], img.pokes()


@M(0x004C21A0, "TrickMenu_BuildPageList", this=TMENU)
def gen_build_page(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    character_rows(env, img, rng)
    page_menu(env, img, rng, Pool(env, img, rng), state=2, category=rng.choice([6, 7, 9, 10, 10, 7, 6, 9]))
    return [], img.pokes()


@M(0x004C2700, "TrickMenu_RequirementsMet")
def gen_requirements(env, n):
    img, rng = new_image(env)
    character_rows(env, img, rng)
    flags = 0
    for bit in (0x80, 0x40, 0x20, 0x10, 0x08):
        if rng.random() < 0.4:
            flags |= bit
    flags |= rng.choice([0, 0x100, 0x200, 0x8000, 0x4800])
    return [rng.randrange(0, 19), flags | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0)], img.pokes()


@M(0x004C2800, "TrickMenu_AssignedRecord", this=TMENU)
def gen_assigned(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    page_menu(env, img, rng, Pool(env, img, rng), state=1, category=rng.choice([6, 7, 9, 10, 10]), with_page=False)
    return [], img.pokes()


@M(0x004C2890, "TrickMenu_SortByCost", this=TMENU)
def gen_sort(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    info = page_menu(env, img, rng, Pool(env, img, rng), state=2)
    return [len(info["shown"])], img.pokes()


@M(0x004C29D0, "TrickMenu_HighlightFirst", this=TMENU)
def gen_highlight_first(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    info = page_menu(env, img, rng, Pool(env, img, rng), state=2)
    base = info["base"]
    for slot in (M_CATEGORY_LIST, M_COMBO_LIST, M_PAGE_LIST):
        if rng.random() < 0.3:
            img.u32(base + slot, 0)
    return [], img.pokes()


@M(0x004C2A10, "TrickMenu_BuildSpecialCombos", this=TMENU)
def gen_special_combos(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    info = page_menu(env, img, rng, Pool(env, img, rng), state=1, category=10, with_page=False, with_widgets=False)
    base = info["base"]
    img.u32(base + M_COMBO_LIST, tlist(env, img, rng, 1, [], highlighted=0))
    return [], img.pokes()


@M(0x004C2C10, "TrickMenu_BuildNormalCombos", this=TMENU)
def gen_normal_combos(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    info = page_menu(env, img, rng, Pool(env, img, rng), state=1, category=rng.choice([6, 7, 9, 6, 7, 9, 11]), with_page=False, with_widgets=False)
    base = info["base"]
    img.u32(base + M_COMBO_LIST, tlist(env, img, rng, 1, [], highlighted=0))
    return [], img.pokes()


@M(0x004C3350, "TrickMenu_CategoryMask", this=TMENU)
def gen_category_mask(env, n):
    img, rng = new_image(env)
    return [rng.choice([-2, 0, 5, 6, 7, 8, 9, 10, 11, 100, 6, 7, 8, 9, 10]) & 0xFFFFFFFF], img.pokes()


@M(0x004C1310, "TrickMenu_ButtonGlyphs")
def gen_glyphs(env, n):
    img, rng = new_image(env)
    combo = A(env, TCOMBO)
    out = A(env, TOUT)
    count = rng.choice([0, 1, 2, 3, 4, 1, 2, 3])
    words = [count] + [rng.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, rng.randrange(0, 20)]) for _ in range(7)]
    for i, word in enumerate(words):
        img.u16(combo + 2 * i, word)
    img.garbage(out, 0x20, rng)
    return [combo, out], img.pokes()


# ---- trickwin.cpp: the pad -------------------------------------------------------------------------------------------------------------------------

def update_case(env, rng, state, *, only=None, category=None, dialog=None, real_pick=False):
    img, _ = new_image(env)
    pad = trick_world(env, img, rng)
    pool = Pool(env, img, rng)
    set_pad(img, rng, pad, only=only)
    character_rows(env, img, rng)
    info = page_menu(env, img, rng, pool, state=state, category=category, dialog="real" if state == 3 else "none" if dialog is None else dialog,
                     real_pick=state == 3)
    return img, info


@M(0x004C2ED0, "TrickMenu_UpdateCategories", this=TMENU)
def gen_update_categories(env, n):
    rng = env.rng
    img, info = update_case(env, rng, 0, only=rng.choice(["confirm", "back", "up", None, "confirm", "confirm"]))
    return [], img.pokes()


@M(0x004C3050, "TrickMenu_UpdateCombos", this=TMENU)
def gen_update_combos(env, n):
    rng = env.rng
    img, info = update_case(env, rng, 1, only=rng.choice(["confirm", "back", "square", "up", None, "confirm", "square"]), category=rng.choice([6, 7, 9, 10, 10]))
    return [], img.pokes()


@M(0x004C33F0, "TrickMenu_UpdateTricks", this=TMENU)
def gen_update_tricks(env, n):
    rng = env.rng
    img, info = update_case(env, rng, 2, only=rng.choice(["confirm", "confirm", "back", "triangle", "up", None]), category=rng.choice([6, 7, 9, 10, 10]))
    return [], img.pokes()


@M(0x004C3A80, "TrickMenu_UpdateBuyDialog", this=TMENU)
def gen_update_buy(env, n):
    rng = env.rng
    img, info = update_case(env, rng, 3, only=rng.choice(["confirm", "back", "up", None, "confirm"]), category=rng.choice([6, 7, 9, 10, 10]))
    return [], img.pokes()


@M(0x004C3CA0, "TrickMenu_UpdateWarning", this=TMENU)
def gen_update_warning(env, n):
    rng = env.rng
    img, info = update_case(env, rng, 4, only=rng.choice(["confirm", None, "back", "confirm"]))
    return [], img.pokes()


@M(0x004C3D30, "TrickMenu_Update", this=TMENU)
def gen_update(env, n):
    rng = env.rng
    state = rng.choice([0, 1, 2, 3, 4, 9, 1, 2])
    img, info = update_case(env, rng, state, only=rng.choice(["confirm", "back", "up", None, "square", "triangle"]), category=rng.choice([6, 7, 9, 10, 10]))
    return [], img.pokes()


def combo_case(env, rng, *, special=None):
    img, _ = new_image(env)
    pad = trick_world(env, img, rng)
    pool = Pool(env, img, rng)
    category = rng.choice([6, 7, 9, 10, 10]) if special is None else (10 if special else rng.choice([6, 7, 9]))
    info = page_menu(env, img, rng, pool, state=1, category=category, with_page=False)
    return img, info


@M(0x004C36D0, "TrickMenu_BindSelected", this=TMENU)
def gen_bind(env, n):
    rng = env.rng
    img, info = combo_case(env, rng)
    return [rng.randrange(0, info["count"])], img.pokes()


@M(0x004C38F0, "TrickMenu_UnbindSelected", this=TMENU)
def gen_unbind(env, n):
    rng = env.rng
    img, info = combo_case(env, rng)
    return [], img.pokes()


@M(0x004C3CF0, "TrickMenu_SpecialSlotsFull", this=TMENU)
def gen_specials_full(env, n):
    rng = env.rng
    img, info = combo_case(env, rng, special=True)
    # now and then every special slot holds a trick
    if rng.random() < 0.4:
        block = read_u32(img, A(env, TMENU) + M_BLOCK)
        for slot in range(N_SPECIAL):
            img.u8(block + 0x2B + slot, rng.randrange(0, info["count"]))
    return [], img.pokes()


# ---- trickwin.cpp: the panel window ----------------------------------------------------------------------------------------------------------------

P = tspec("trkscr")


@P(0x004C3DD0, "TrickPanel_Construct", this=TPANEL)
def gen_panel_construct(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    base = A(env, TPANEL)
    img.garbage(base, 0xDC, rng)
    fonts = [A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)) for _ in range(3)]
    if rng.random() < 0.1:
        fonts[0] = 0
    if rng.random() < 0.1:
        fonts[1] = 0
    # (the function takes the window's start position too: seven stack arguments)
    return [fonts[0], fonts[1], fonts[2], coords(rng), coords(rng), 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@P(0x004C40B0, "TrickPanel_ScalarDelete", this=TPANEL)
def gen_panel_delete(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    panel_object(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@P(0x004C40D0, "TrickPanel_Destroy", this=TPANEL)
def gen_panel_destroy(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    panel_object(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@P(0x004C40E0, "TrickPanel_UpdateFrame", this=TPANEL)
def gen_panel_update(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    panel_object(env, img, rng, Pool(env, img, rng))
    # the current character's block: its special slots
    for character in range(20):
        block = CAREER + 0x104 * character + 0xCC
        if rng.random() < 0.3:
            for slot in range(N_SPECIAL):
                img.u8(block + 0x2B + slot, 0xFF if rng.random() < 0.5 else rng.randrange(0, 40))
    return [], img.pokes()


@P(0x004C4300, "TrickPanel_ShowMessage", this=TPANEL)
def gen_panel_message(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    panel_object(env, img, rng, Pool(env, img, rng))
    out = A(env, TOUT)
    img.put(out, rng.choice([b"ABC", b"A", b"GHEF", b""]) + b"\0")
    return [rng.choice([0, 1, 2, 0, 1, 2, 3, -1]) & 0xFFFFFFFF, rng.choice([0, out])], img.pokes()


# ---- the screen loop -----------------------------------------------------------------------------------------------------------------------------

@L(0x004BF860, "TrickScreen_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.u32(A(env, OBJ[3]), rng.getrandbits(32))
    return [A(env, OBJ[3]) + 8, A(env, OBJ[3])], img.pokes()


@L(0x004C0240, "TrickMenu_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    trick_world(env, img, rng)
    return [], img.pokes()
