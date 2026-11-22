"""Phase 4.10g case generators, part 2: the options window's pages (frontend_specs_opt.py has the slab, the stubs and the window): the gap checklist (optgap), the high
scores (optscore), the movie list (optmov) and the sound levels page with the memory card and replay pages (optsnd).

Each page is built by hand: the window part, fonts, the lists (fake: their virtual methods are reported services), item records and fake pool objects for what the page
only holds. The page's constructor is called on garbage and builds its objects for real in the arena (the toolkit and the window base are native); the other classes'
constructors are seams that hand back arena blocks.
"""

from __future__ import annotations

import struct

from create_specs import spec
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props
from frontend_specs_menu_items import dots_object
from frontend_specs_opt import (opt_image, BACK, CONFIRM, CURRENT_LEVEL, GROUP_STUBS, LEFT, M_SIZE, OPT_OBJ, PAD_STRIDE, PLAYING_MOVIE, RIGHT, SCRATCH, Items, install_opt_common,
                                opt_world, pad_set, register_opt, reinstall)
from frontend_specs_screens import OBJ
from frontend_specs_screens_cases import Pool, font_object, font_ptr, window
from frontend_specs_skwin import A

from frontend_specs_playsel import disable


def page_spec(group, address, name, this=True):
    return spec(address, name, group, this=(lambda env: env.slab + OBJ[0]) if this else None)


for _group in ("optgap", "optscore", "optmov", "optsnd"):
    register_opt(_group)

# the elements a page holds that its update changes: fake text elements that own a string
TEXT_POOL_AT = 0x1AE00      # the screens' text pool (frontend_specs_screens.TEXTS)


def element(items, env, img, rng):
    """A fake text element (reported virtual calls) that owns a string and a font."""
    base = A(env, 0x27E00 + 0x100 * items.next_item)
    items.next_item += 1
    img.garbage(base, 0x40, rng)
    img.u32(base, A(env, 0x1A100))        # VT_ANY
    img.u32(base + 0x14, rng.choice([0, 1]))
    img.u32(base + 0x28, A(env, TEXT_POOL_AT + 0x100 * rng.randrange(8)))
    img.u32(base + 0x2C, A(env, 0x3D00))   # a font
    return base


def level_ok(rng, *, safe=True):
    """A level the pages may sit on: not a locked one in a loop that needs the current level to be valid (the checklist's draw walks on from it)."""
    return rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 3, 5, 8, 9] if not safe else [0, 1, 2, 3, 4, 5, 6, 7])


# ---- the gap checklist (class at 0x005193e4) ----------------------------------------------------------------------------------------------------------------------

GAP = lambda address, name, this=True: page_spec("optgap", address, name, this)
G_SIZE = 0x504


def gap_install(fc, env):
    install_opt_common(fc, env)
    # the checklist's own functions are replayed here, not stubbed
    disable(fc, "engine_004840b0")
    rng = env.rng
    # (the checklist's row array holds 0x60 rows and the retail code asserts past it, then overruns into the style slots: at most a few dozen of the 415 gaps are active)
    fc.stub(0x00414770, 1, "engine_00414770", result=lambda a: 1 if rng.random() < 0.12 else 0)
    fc.stub(0x00414B10, 1, "engine_00414b10", result=lambda a: rng.choice([0, 1]))


def gaps_state(env, img, rng, base, pool, items, *, destroy=False, shown=None, wanted=None, with_rows=True):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, G_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + 0xAC, font_ptr(env, rng))
    font_object(env, img, rng, base + 0xB0)
    font_object(env, img, rng, base + 0x1FC)
    count = rng.randrange(0, 10) if with_rows else 0
    ids = [rng.randrange(0, 40) for _ in range(count)]
    rows = [items.item(i) for i in ids]
    img.put(base + 0x34C, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0x34C + 4 * i, row)
    img.u32(base + 0x348, keep(items.fake_list(rows, pick=rng.randrange(-1, len(rows)) if rows else -1)))
    for slot in (0x4CC, 0x4D0, 0x4D4):
        img.u32(base + slot, keep(pool.take()))
    img.u32(base + 0x4D8, (rng.choice([-1, rng.randrange(0, 14)]) if shown is None else shown) & 0xFFFFFFFF)
    img.u32(base + 0x4DC, level_ok(rng) if wanted is None else wanted)
    img.u32(base + 0x4E0, keep(element(items, env, img, rng)))
    img.u32(base + 0x4E4, keep(element(items, env, img, rng)))
    img.u32(base + 0x4E8, 0)
    img.u32(base + 0x4EC, keep(pool.take()))
    for slot in (0x4F0, 0x4F4, 0x4F8):
        img.u32(base + slot, keep(pool.take()))
    for slot in (0x4FC, 0x500):
        img.u32(base + slot, keep(pool.take()))
    img.u32(0x0056DB4C, rng.getrandbits(32))


@GAP(0x004840B0, "GapChecklist_Construct")
def gen_gap_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng, level=rng.choice([0, 3, 5, 7, 8, 9, 11, 13, 14, 25, 0xFFFFFFF0]))
    img.garbage(A(env, OBJ[0]), G_SIZE, rng)
    return [font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@GAP(0x00484AB0, "GapChecklist_LevelLocked", this=False)
def gen_gap_locked(env, n):
    img, rng = opt_image(env)
    return [rng.choice([0, 3, 7, 8, 9, 8, 9, 10, 13, 0xFFFFFFFF, rng.getrandbits(32)])], img.pokes()


@GAP(0x00484AF0, "GapChecklist_ScalarDelete")
def gen_gap_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    gaps_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@GAP(0x00484B10, "GapChecklist_Destroy")
def gen_gap_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    gaps_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@GAP(0x00484C10, "GapChecklist_Update")
def gen_gap_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    base = A(env, OBJ[0])
    pool = Pool(env, img, rng)
    items = Items(env, img, rng)
    wanted = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13])
    gaps_state(env, img, rng, base, pool, items, wanted=wanted, shown=rng.choice([-1, wanted, (wanted + 1) % 14, wanted]) if rng.random() < 0.3 else -1)
    # the styles are real MenuProperties (rows are built with them): the 4.10c layout's four
    for slot, index in ((0x4CC, 0), (0x4D0, 1), (0x4D4, 2)):
        img.u32(base + slot, A(env, 0x0400 + 0x180 * index))
    img.u32(CURRENT_LEVEL, rng.randrange(0, 14))
    return [], img.pokes()


@GAP(0x004850A0, "GapChecklist_StripBrackets", this=False)
def gen_gap_strip(env, n):
    img, rng = opt_image(env)
    target = A(env, OBJ[1])
    text = "".join(rng.choice("abc [] XY[z]") for _ in range(rng.randrange(0, 30)))
    img.put(target, text.encode() + b"\0")
    return [target], img.pokes()


@GAP(0x004850F0, "GapChecklist_Draw")
def gen_gap_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["left"], ["right"], ["up"], ["down"], ["released"], []]), chance=0.7)
    gaps_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), wanted=rng.choice([0, 1, 2, 3, 4, 5, 6, 7]))
    return [coords(rng), coords(rng)], img.pokes()


@GAP(0x00485290, "GapChecklist_SortRows")
def gen_gap_sort(env, n):
    img, rng = opt_image(env)
    base = A(env, OBJ[0])
    items = Items(env, img, rng)
    count = rng.randrange(2, 12)
    rows = [items.item(rng.choice([rng.randrange(0, 30), rng.randrange(0, 5), 0xFFFFFFFE if rng.random() < 0.04 else 7])) for _ in range(count)]
    img.garbage(base, G_SIZE, rng)
    img.put(base + 0x34C, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0x34C + 4 * i, row)
    lo = rng.randrange(0, count - 1)
    hi = rng.randrange(lo, count)
    return [lo, hi], img.pokes()


@GAP(0x004853B0, "GapChecklist_SortAll")
def gen_gap_sort_all(env, n):
    img, rng = opt_image(env)
    base = A(env, OBJ[0])
    items = Items(env, img, rng)
    count = rng.randrange(0, 14)
    rows = [items.item(rng.choice([0xFFFFFFFE, rng.randrange(0, 30), rng.randrange(0, 30), rng.randrange(0, 30)])) for _ in range(count)]
    img.garbage(base, G_SIZE, rng)
    img.put(base + 0x34C, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0x34C + 4 * i, row)
    return [], img.pokes()


GROUP_STUBS["optgap"] = gap_install

# ---- the high scores (class at 0x00519404) --------------------------------------------------------------------------------------------------------------------------

SCORE = lambda address, name, this=True: page_spec("optscore", address, name, this)
H_SIZE = 0x7AC


def score_install(fc, env):
    install_opt_common(fc, env)
    disable(fc, "engine_00485400")


def scores_state(env, img, rng, base, pool, items, *, destroy=False, shown=None, wanted=None):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, H_SIZE, pool, children=rng.randrange(0, 3))
    for slot in (0xAC, 0x1F8, 0x344, 0x490, 0x5DC):
        font_object(env, img, rng, base + slot)
    for i in range(8):
        img.u32(base + 0x728 + 4 * i, keep(pool.take()))
        img.u32(base + 0x748 + 4 * i, keep(element(items, env, img, rng)))
        img.u32(base + 0x768 + 4 * i, keep(element(items, env, img, rng)))
    img.u32(base + 0x788, (rng.choice([-1, rng.randrange(0, 14)]) if shown is None else shown) & 0xFFFFFFFF)
    img.u32(base + 0x78C, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 13]) if wanted is None else wanted)
    img.u32(base + 0x790, keep(element(items, env, img, rng)))
    img.u32(base + 0x794, 0)
    img.u32(base + 0x798, keep(pool.take()))
    for slot in (0x79C, 0x7A0, 0x7A4, 0x7A8):
        img.u32(base + slot, keep(pool.take()))
    img.u32(0x0056DB4C, rng.getrandbits(32))


@SCORE(0x00485400, "HighScores_Construct")
def gen_scores_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng, level=rng.choice([0, 3, 5, 7, 8, 9, 11, 13, 14, 25]))
    img.garbage(A(env, OBJ[0]), H_SIZE, rng)
    return [font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF], img.pokes()


@SCORE(0x00485C90, "HighScores_ScalarDelete")
def gen_scores_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    scores_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SCORE(0x00485CB0, "HighScores_Destroy")
def gen_scores_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    scores_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@SCORE(0x00485D80, "HighScores_Update")
def gen_scores_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    wanted = rng.randrange(0, 14)
    scores_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), wanted=wanted,
                 shown=(wanted if rng.random() < 0.3 else rng.randrange(0, 14)))
    return [], img.pokes()


@SCORE(0x00485F90, "HighScores_Draw")
def gen_scores_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["left"], ["right"], []]), chance=0.7)
    scores_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), wanted=rng.choice([0, 1, 2, 3, 4, 5, 6, 7]))
    return [coords(rng), coords(rng)], img.pokes()


GROUP_STUBS["optscore"] = score_install

# ---- the movie list (class at 0x00519424) ----------------------------------------------------------------------------------------------------------------------------

MOVIE = lambda address, name, this=True: page_spec("optmov", address, name, this)
V_SIZE = 0x238


def movie_install(fc, env):
    install_opt_common(fc, env)
    rng = env.rng
    disable(fc, "engine_004860d0", "engine_00486830", "item_highlight")
    # the movie step calls the engine function 0x004ad9f0, which is also the fake items' highlight slot: here it is the engine call
    fc.stub(0x004AD9F0, 1, "engine_004ad9f0")
    config = env.slab + 0x24000 + 0x2000       # a character config record the career lookup hands back
    reinstall(fc, env, 0x00416050, 1, result=lambda a: config + 0x40 * rng.randrange(0, 4))
    reinstall(fc, env, 0x00416530, 1, result=lambda a: rng.choice([0, 1, 2, 3, 4]))
    reinstall(fc, env, 0x004164B0, 1, result=lambda a: rng.choice([0, 1, 2, 3, 4]))
    reinstall(fc, env, 0x004B1980, 1, result=lambda a: rng.randrange(0, 20))


def movies_state(env, img, rng, base, pool, items, *, destroy=False, ids=None, pick=None):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, V_SIZE, pool, children=rng.randrange(0, 3))
    use = ids if ids is not None else [(-1 - k) & 0xFFFFFFFF for k in range(rng.randrange(0, 8))] + [0]
    rows = [items.item(i) for i in use]
    img.put(base + 0xB4, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0xB4 + 4 * i, row)
    img.u32(base + 0xB0, keep(items.fake_list(rows, pick=rng.randrange(0, len(rows)) if pick is None else pick)))
    img.u32(base + 0x234, keep(pool.take()))
    # the config records the career lookup hands back (bit 6 of byte +1)
    for k in range(4):
        img.put(A(env, 0x24000 + 0x2000 + 0x40 * k), bytes(rng.randrange(256) for _ in range(0x40)))


@MOVIE(0x004860D0, "MovieList_Construct")
def gen_movie_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.garbage(A(env, OBJ[0]), V_SIZE, rng)
    for k in range(4):
        img.put(A(env, 0x24000 + 0x2000 + 0x40 * k), bytes(rng.randrange(256) for _ in range(0x40)))
    pool = Pool(env, img, rng)
    return [font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, pool.take(), pool.take()], img.pokes()


@MOVIE(0x004866E0, "MovieList_ScalarDelete")
def gen_movie_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    movies_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@MOVIE(0x00486700, "MovieList_Destroy")
def gen_movie_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    movies_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@MOVIE(0x00486790, "MovieList_Update")
def gen_movie_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    movies_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@MOVIE(0x004867A0, "MovieList_Draw")
def gen_movie_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, ["up", "down"])
    movies_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@MOVIE(0x00486830, "MovieList_Step")
def gen_movie_step(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["confirm"], ["back"], ["confirm", "back"], []]), chance=0.7)
    pick_id = rng.choice([0, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFF0])
    ids = [(-1 - k) & 0xFFFFFFFF for k in range(4)] + [0]
    rows = None
    movies_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), ids=ids, pick=rng.randrange(0, len(ids)))
    return [], img.pokes()


@MOVIE(0x00486910, "MovieList_Unlocked", this=False)
def gen_movie_unlocked(env, n):
    img, rng = opt_image(env)
    for k in range(4):
        img.put(A(env, 0x24000 + 0x2000 + 0x40 * k), bytes(rng.randrange(256) for _ in range(0x40)))
    kind = rng.choice([0, 1, 2, 3, 4, 5, 6, 1, 4, 2, 6, 7, 0x10002])
    if kind & 0xFFFF in (1, 4):
        index = rng.choice([-18 - k for k in range(17)]) & 0xFFFFFFFF
    elif kind & 0xFFFF == 2:
        index = rng.choice([-18 - k for k in range(8)] + [0, -90]) & 0xFFFFFFFF
    else:
        index = rng.getrandbits(32)
    return [kind, index], img.pokes()


GROUP_STUBS["optmov"] = movie_install

# ---- the sound levels page (class at 0x00519444) and the memory card and replay pages ------------------------------------------------------------------------------

SOUND = lambda address, name, this=True: page_spec("optsnd", address, name, this)
S_SIZE = 0x244


def sound_install(fc, env):
    install_opt_common(fc, env)
    disable(fc, "engine_00486b10", "engine_004871e0", "engine_00487280", "engine_004872a0", "engine_004872b0", "engine_004873b0", "engine_00487390", "engine_00487470")


def sound_state(env, img, rng, base, pool, items, *, destroy=False, pick=None):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, S_SIZE, pool, children=rng.randrange(0, 3))
    rows = [items.item(0), items.item(1), items.item(0xFFFFFFFF)]
    img.put(base + 0xB4, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0xB4 + 4 * i, row)
    img.u32(base + 0xB0, keep(items.fake_list(rows, pick=rng.randrange(-1, 3) if pick is None else pick)))
    img.u32(base + 0x234, keep(pool.take()))
    img.u32(base + 0x238, keep(pool.take()))
    for slot in (0x23C, 0x240):
        dots = A(env, 0x24000 + 0x2200 + (0x40 if slot == 0x240 else 0))
        dots_object(env, img, rng, dots)
        img.u32(base + slot, keep(dots))


@SOUND(0x00486B10, "SoundLevels_Construct")
def gen_sound_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.garbage(A(env, OBJ[0]), S_SIZE, rng)
    pool = Pool(env, img, rng)
    return [font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, pool.take(), pool.take()], img.pokes()


@SOUND(0x00486E80, "SoundLevels_ScalarDelete")
def gen_sound_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SOUND(0x00486EA0, "SoundLevels_Destroy")
def gen_sound_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@SOUND(0x00486F80, "SoundLevels_Update")
def gen_sound_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["left"], ["right"], []]), chance=0.7)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487130, "SoundLevels_Draw")
def gen_sound_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, ["up", "down"])
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@SOUND(0x004871E0, "SoundLevels_Step")
def gen_sound_step(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["confirm"], ["back"], []]), chance=0.7)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487280, "SoundLevels_HasSelection")
def gen_sound_has(env, n):
    img, rng = opt_image(env)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@SOUND(0x004872A0, "SoundLevels_NoSelection")
def gen_sound_none(env, n):
    img, rng = opt_image(env)
    sound_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


def card_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, 0xB0, pool, children=rng.randrange(0, 3))


@SOUND(0x004872B0, "CardPage_Construct")
def gen_card_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0xB0, rng)
    return [rng.choice([0, 1, 1, 2, 0x100]), 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0], img.pokes()


@SOUND(0x00487330, "CardPage_ScalarDelete")
def gen_card_delete(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SOUND(0x00487350, "CardPage_Destroy")
def gen_card_destroy(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487360, "CardPage_Update")
def gen_card_update(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487370, "CardPage_Draw")
def gen_card_draw(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@SOUND(0x00487390, "CardPage_Step", this=False)
def gen_card_step(env, n):
    img, rng = opt_image(env)
    return [], img.pokes()


@SOUND(0x004873B0, "ReplayPage_Construct")
def gen_replay_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), 0xAC, rng)
    return [0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0], img.pokes()


@SOUND(0x00487410, "ReplayPage_ScalarDelete")
def gen_replay_delete(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@SOUND(0x00487430, "ReplayPage_Destroy")
def gen_replay_destroy(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487440, "ReplayPage_Update")
def gen_replay_update(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@SOUND(0x00487450, "ReplayPage_Draw")
def gen_replay_draw(env, n):
    img, rng = opt_image(env)
    card_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@SOUND(0x00487470, "ReplayPage_Step", this=False)
def gen_replay_step(env, n):
    img, rng = opt_image(env)
    return [], img.pokes()


GROUP_STUBS["optsnd"] = sound_install
