"""Phase 4.14b probe: the save-card screen (group mmui; mmu.cpp 0x00476560..0x00479f60).

The screen is a window of the front end (the 4.10 harness: frontend_specs_screens.py lays out one scratch slab, the toolkit and the window base are native and run for real, everything else is a
stub). On top of that harness this module lays out the screen object (0xdd0 bytes at OBJ[0] and OBJ[1]), its confirm box (OBJ[2]) and text box (OBJ[3]), a real name entry (the 4.10c helper),
a fake file list (its methods are reported services), the prompt group (the SKATER area) and text elements; the card manager inside the screen is not under test here: its methods, and the
two functions around the labels, are stubs that answer from the case's random generator (the recorded answers are what the native replay is scripted with).
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES
from frontend_specs_menu_screens import entry_object
from frontend_specs_screens import (OBJ, SKATER, TEXT_POOL, VT_REAL, install_screen_stubs, register, regions as base_regions, READ_ONLY_COMMON, SLAB as BASE_SLAB)
from frontend_specs_screens_cases import Pool, box_state, common, confirm_state, font_object, font_ptr, gspec, text_ptr, window
from frontend_specs_front import check_disjoint, merged
from frontend_specs_menu import VT_REAL as MENU_VT_REAL, Image, coords, poke_props, props_at, string_at
from frontend_specs_skstat import text_element
from frontend_specs_skwin import A, item_row, list_object, new_image

GROUPS = ("mmui",)
UI = gspec("mmui")

# the globals the screen reads and writes besides the harness's own (see mmu_specs.py)
WINDOWS = [(0x0056AB18, 0x1F4), (0x006A141C, 8), (0x00561BFC, 4), (0x00527D54, 4)]
STATICS = [(0x005252F0, 2), (0x00525400, 43), (0x0052544C, 12), (0x005254E0, 13), (0x00527C68, 5), (0x0052A56C, 7), (0x0052E298, 4), (0x0052E364, 24), (0x0052E710, 4), (0x0052E9A0, 96),
           (0x0053146C, 510), (0x00531E28, 28), (0x00531EDC, 17), (0x00532B30, 115), (0x00532BE0, 67), (0x00532FD0, 50), (0x00534178, 7), (0x00535458, 102), (0x0053550C, 24), (0x00535584, 13),
           (0x0053D7B4, 34), (0x0055FE10, 1)]

SB = 0x7FF00800
KDESC = SB + 0x980
KBLOCKS = SB + 0x9C0
KLABEL = SB + 0x900
KNAME = SB + 0xB00

GROUP_SLAB = BASE_SLAB


def regions(slab):
    result = merged(base_regions(slab) + WINDOWS, gap=0)
    return result


for _group in GROUPS:
    register(_group)
    EXTRA_REGIONS[_group] = regions
    READ_ONLY[_group] = list(READ_ONLY_COMMON) + STATICS


# ---- the stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def stub_once(fc, address, count, name, **kw):
    """One stub per address: an earlier group's stub of the same function is turned off."""
    for probe in [p for p in fc._probes if getattr(p, "address", None) == address and p.enabled]:
        probe.enabled = False
    fc.stub(address, count, name, **kw)


def install_mmui_stubs(fc, env):
    if getattr(fc, "_mmui_installed", False):
        return
    fc._mmui_installed = True
    install_screen_stubs(fc, env)
    rng = env.rng

    def text_answer(args):
        # a display name: one of the toolkit's short strings (the card keeps 32 characters of a name)
        return string_at(env, rng.randrange(15))

    def thiscall(address, count, name=None, **kw):
        stub_once(fc, address, count, name or "engine_%08x" % address, convention="thiscall", this=True, **kw)

    thiscall(0x00473E50, 0)
    thiscall(0x00473F10, 0)
    thiscall(0x00475480, 1, pokes=[(0, 4, lambda a: rng.choice([0, 0, 1]))])
    thiscall(0x00475620, 0, result=lambda a: rng.choice([0, 0, 0, 0, 1, 2, 3]))
    thiscall(0x00475640, 0, result=lambda a: rng.choice([0, 1, 1]))
    thiscall(0x00475660, 1, result=lambda a: rng.choice([0, 1, 1]))
    thiscall(0x00474D70, 1, result=lambda a: rng.choice([0, 0, 0xFFFFFFFF]))
    thiscall(0x00474D50, 0, result=lambda a: rng.randrange(0, 16))
    thiscall(0x004741D0, 0, result=lambda a: rng.randrange(0, 13))
    thiscall(0x004741F0, 1, result=lambda a: rng.choice([0, 1, 2, 5]))
    thiscall(0x004742E0, 1, result=text_answer)
    thiscall(0x00474A50, 1, result=lambda a: rng.choice([1, 2, 3, 4, 1, 2, 3]))
    thiscall(0x00474AE0, 3, pokes=[(2, 4, lambda a: rng.choice([1, 2, 3, 6, 3, 1]))],
             fill=(1, lambda a: rng.choice([b"THPS2 CAREER", b"REPLAY", b"park", b"", b"a very long description that is cut"])[:31].ljust(32, bytes(1))),
             stack_to={1: KDESC, 2: KBLOCKS})
    thiscall(0x00474380, 1, result=lambda a: 1 if rng.random() < 0.35 else 0, stack_to={0: KNAME})
    thiscall(0x004744A0, 2, result=lambda a: rng.choice([0, 0, 1]))
    thiscall(0x004746D0, 1, result=lambda a: rng.choice([0xFFFFFFFF, 0, 1, 2, 3]))
    thiscall(0x004758E0, 3, result=lambda a: rng.choice([0, 1, 1]))
    thiscall(0x00476200, 2, result=lambda a: rng.choice([0, 1, 1]))
    # a real list's per-frame hook (slot 7 of the ScrollList vtable, an engine function): a fake list reports it as `list_v7` already
    stub_once(fc, 0x004DC480, 0, "list_v7", convention="thiscall", this=True)
    stub_once(fc, 0x0046F480, 0, "grid_v1", convention="thiscall", this=True)
    stub_once(fc, 0x00473D20, 2, "engine_00473d20", result=lambda a: rng.choice([1, 1, 1, 2, 3, 6, 0, 0xFFFFFFFF]))
    stub_once(fc, 0x0047B8E0, 2, "engine_0047b8e0", fill=(0, lambda a: (rng.choice([b"THPS2 CAREER- ", b"THPS2 REPLAY- ", b"THPS2 PARK- ", b"X"]) + b"\0").ljust(36, b"\0")[:36]), stack_to={0: KLABEL})
    stub_once(fc, 0x004E41D0, 1, "engine_004e41d0", result=lambda a: rng.choice([0, 0, 0, 0xD3, 5]))
    stub_once(fc, 0x004DC090, 1, "engine_004dc090")
    stub_once(fc, 0x004E8CF0, 1, "engine_004e8cf0")
    stub_once(fc, 0x00489670, 1, "engine_00489670")


TEXT_STRIDE_BASE = 0x1AE00      # frontend_specs_screens.TEXTS
GROUP_STUBS = {group: install_mmui_stubs for group in GROUPS}


# ---- the world --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def prompt_group_state(env, img, rng, pool, base):
    window(env, img, rng, base, 0x208, pool, children=rng.randrange(0, 3))
    font_object(env, img, rng, base + 0xBC)
    for i in range(4):
        img.u32(base + 0xAC + 4 * i, pool.take())
    return base


def mmu_globals(env, img, rng):
    for address in (0x0056ACF9, 0x0056ACFA, 0x0056ACFB):
        img.u8(address, rng.choice([0, 0, 1]))
    img.u32(0x0056ACFC, rng.choice([0, 1, 2]))
    img.u32(0x006A141C, rng.choice([0, A(env, OBJ[3])]))
    img.u32(0x006A1420, rng.choice([0, 0, 0, 0x100]))
    img.u32(0x00561BFC, rng.choice([0x12, 0, 3, 0x12]))
    for address in (0x0056ABD8, 0x0056ABF8, 0x0056AC78):
        img.put(address, rng.choice([b"", b"OLD", b"THPS2 CAREER- OLD.SAV", b"HAWK"]) + b"\0")


def screen_state(env, img, rng, pool, *, state=None, saving=None, kind=None, busy=None, result=None, prompts=None, hint=None, rows=None, delete=False):
    """The save-card screen at OBJ[0]: a window with a font, the banner and counter texts, a fake file list over `rows` item records, two styles, the 16 row slots, the confirm box, the
    name entry and the message box; the prompt group (a save) or the delete hint (a load)."""
    base = A(env, OBJ[0])
    window(env, img, rng, base, 0xDD0, pool, children=rng.randrange(0, 3))
    font_object(env, img, rng, base + 0xAC)
    count = rng.randrange(0, 11) if rows is None else rows
    item_rows = [item_row(env, img, rng, slot, slot, selectable=1) for slot in range(count)]
    list_at = list_object(env, img, rng, 0, item_rows, highlighted=rng.randrange(0, count + 1) if count else 0)
    saving = rng.choice([0, 1]) if saving is None else saving
    with_prompts = (saving == 1) if prompts is None else prompts
    img.u32(base + 0xD44, text_element(env, img, rng, 0))
    img.u32(base + 0xD48, text_element(env, img, rng, 1))
    img.u32(base + 0xD4C, text_element(env, img, rng, 2))
    img.u32(base + 0xD50, text_element(env, img, rng, 3) if (hint if hint is not None else not with_prompts and rng.random() < 0.7) else 0)
    img.u32(base + 0xD54, 0 if delete and rng.random() < 0.15 else list_at)
    poke_props(env, img, 0, rng)
    poke_props(env, img, 1, rng)
    img.u32(props_at(env, 0), MENU_VT_REAL["props"])
    img.u32(props_at(env, 1), MENU_VT_REAL["props"])
    img.u32(base + 0xD58, props_at(env, 0))
    img.u32(base + 0xD5C, props_at(env, 1))
    for i in range(16):
        img.u32(base + 0xD60 + 4 * i, 0 if i >= count or (delete and rng.random() < 0.2) else item_rows[i])
    img.u32(base + 0xDA0, rng.choice([0, 1]) if busy is None else busy)
    img.u32(base + 0xDA4, rng.randrange(0, 0x10) if state is None else state)
    img.u32(base + 0xDA8, rng.choice([1, 2, 3]) if kind is None else kind)
    confirm = A(env, OBJ[2])
    confirm_state(env, img, rng, confirm, pool, complete=not delete)
    img.u32(base + 0xDAC, confirm if not (delete and rng.random() < 0.15) else 0)
    box = A(env, OBJ[3])
    box_state(env, img, rng, box, vtable=VT_REAL["textbox"])
    img.u32(base + 0xDB0, box if not (delete and rng.random() < 0.15) else 0)
    img.u32(base + 0xDB4, rng.choice([0, 1, 2, 3, -1 & 0xFFFFFFFF]))
    entry = entry_object(env, img, rng)[0]
    img.u32(base + 0xDB8, entry if not (delete and rng.random() < 0.15) else 0)
    img.u32(base + 0xDBC, saving)
    img.u32(base + 0xDC0, rng.choice([0, 1, 0x2D, -1 & 0xFFFFFFFF, rng.randrange(-5, 50) & 0xFFFFFFFF]))
    img.u32(base + 0xDC4, rng.choice([0, 0, 0, 0, 1, 2]) if result is None else result)
    group = prompt_group_state(env, img, rng, pool, A(env, SKATER)) if with_prompts else 0
    img.u32(base + 0xDC8, group)
    img.u32(base + 0xDCC, rng.choice([0, 1, 2, 4, 5, 8, 0xD, 0xF]))
    return base


# ---- the prompt group --------------------------------------------------------------------------------------------------------------------------------------------------------------------

@UI(0x00476560, "MmuPrompts_Construct", this=OBJ[0])
def gen_prompts_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0x208, rng)
    return [], img.pokes()


@UI(0x004769A0, "MmuPrompts_ScalarDelete", this=SKATER)
def gen_prompts_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    prompt_group_state(env, img, rng, Pool(env, img, rng), A(env, SKATER))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@UI(0x004769D0, "MmuPrompts_Destroy", this=SKATER)
def gen_prompts_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    prompt_group_state(env, img, rng, Pool(env, img, rng), A(env, SKATER))
    return [], img.pokes()


@UI(0x00476A30, "MmuPrompts_Set", this=SKATER)
def gen_prompts_set(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    prompt_group_state(env, img, rng, Pool(env, img, rng), A(env, SKATER))
    img.u32(0x00527D54, rng.choice([480, 240, 600, 448, 0x10000]))
    return [rng.choice([0, 1, 2, 0x100, rng.getrandbits(8)]) | (rng.getrandbits(24) << 8), rng.choice([0, 1, 2, 4, 8, 0xF, 0x10, 0x15, rng.getrandbits(32)])], img.pokes()


# ---- the screen ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@UI(0x00476B00, "MmuScreen_Construct", this=OBJ[0])
def gen_screen_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0xDD0, rng)
    img.u32(0x00527D54, rng.choice([480, 240, 600, 448]))
    return [rng.choice([1, 2, 3]), rng.choice([0, 1, 5, 0x12]), rng.choice([0, 1])], img.pokes()


@UI(0x00477910, "MmuScreen_ScalarDelete", this=OBJ[0])
def gen_screen_delete(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng), delete=True)
    mmu_globals(env, img, rng)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@UI(0x00477940, "MmuScreen_Destroy", this=OBJ[0])
def gen_screen_destroy(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng), delete=True)
    mmu_globals(env, img, rng)
    return [], img.pokes()


@UI(0x00477B40, "MmuScreen_RebuildList", this=OBJ[0])
def gen_rebuild(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng), delete=True)
    mmu_globals(env, img, rng)
    img.u32(0x00527D54, rng.choice([480, 240, 600, 448]))
    return [], img.pokes()


@UI(0x004780D0, "Front_SaveCardStateMachine", this=OBJ[0])
def gen_state_machine(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng), result=rng.choice([0, 0, 0, 0, 0, 1, 2]))
    mmu_globals(env, img, rng)
    img.u32(0x00527D54, rng.choice([480, 240, 600, 448]))
    return [], img.pokes()


@UI(0x00478F00, "MmuScreen_Browse", this=OBJ[0])
def gen_browse(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng), state=0)
    mmu_globals(env, img, rng)
    return [], img.pokes()


@UI(0x004792F0, "MmuScreen_ShowBanner", this=OBJ[0])
def gen_banner(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@UI(0x004793A0, "MmuScreen_Display", this=OBJ[0])
def gen_display(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng))
    mmu_globals(env, img, rng)
    return [], img.pokes()


@UI(0x00479F60, "MmuScreen_Draw", this=OBJ[0])
def gen_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    screen_state(env, img, rng, Pool(env, img, rng))
    mmu_globals(env, img, rng)
    return [coords(rng), coords(rng)], img.pokes()
