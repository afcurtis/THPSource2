"""Phase 4.10h probe, part 3: the case generators of the window groups: the level information window's methods (gvwn), the level picture window (glvp), the TRG type 500 node's record
(gprl), the light editor (gled), the loading screen (glds) and the goals checklist screen (ggls).

The windows are built by hand in the screens' scratch slab the way the other screens' probes build theirs (frontend_specs_screens_cases.py): the window part with a fake vtable and fake
children, the pictures as fake objects whose virtual methods are reported services, fonts copied from the slab's seeded ones.  The career world (for Career_LevelOpen and the level needs)
comes from the 4.8 sampler (career_specs.base_state).
"""

from __future__ import annotations

import struct

import career_specs as cspecs
import career_state as cs
from create_specs import spec
from frontend_specs_goals import VT_PIC
from frontend_specs_goals_cases import cenv, players
from frontend_specs_menu import FIND_FONTS, FONT_SIZE, props_at
from frontend_specs_screens import OBJ, RENDERER, TEXTS, TEXT_POOL, TEXT_STRIDE, TICK, VT_ANY
from frontend_specs_screens_cases import Pool, font_object, window
from frontend_specs_skwin import A, item_row, list_object, new_image

GAME_MODE = 0x00533F38
CURRENT_LEVEL = 0x0056A898
RENDERER_POINTER = 0x00560FD4
SPOOL = 0x0056DB4C


def gvwn(address, name, this=None):
    return spec(address, name, "gvwn", this=(lambda env: env.slab + this) if this is not None else None)


def glvp(address, name, this=None):
    return spec(address, name, "glvp", this=(lambda env: env.slab + this) if this is not None else None)


def gprl(address, name, this=None):
    return spec(address, name, "gprl", this=(lambda env: env.slab + this) if this is not None else None)


def gled(address, name, this=None):
    return spec(address, name, "gled", this=(lambda env: env.slab + this) if this is not None else None)


def glds(address, name):
    return spec(address, name, "glds")


def ggls(address, name):
    return spec(address, name, "ggls")


def career_world(env, img, rng, *, level=None, mode=None):
    """The career in a random legal state (the records, the manager, the level, the mode); returns the level."""
    state, lvl = cspecs.base_state(cenv(env), level=level, mode=mode)
    for address, data in state.pokes.items():
        img.put(address, data)
    return lvl


def screen_world(env, img, rng):
    """The globals the windows read: the screen size, the renderer, the software flag, the clock."""
    img.u32(0x029DA394, rng.choice([320, 512, 640, 800]))
    img.u32(0x029DA398, rng.choice([240, 448, 480, 600]))
    img.u32(0x029DA3A0, rng.choice([0, 1]))
    img.u32(RENDERER_POINTER, A(env, RENDERER))
    img.u32(A(env, RENDERER) + 0x88, rng.randrange(1, 0x10000))
    img.u16(A(env, RENDERER) + 0x90, rng.randrange(0, 40))
    img.u32(0x0056E31C, rng.randrange(1, 100000))
    img.u32(TICK, rng.randrange(1, 100000))


def picture(env, img, rng, pool, *, load=False):
    """A picture object: the pool's fake object (the fake vtable's draw and delete), or one with the loading method too."""
    base = pool.take()
    if load:
        img.u32(base, A(env, VT_PIC))
    img.u32(base + 0xC, rng.choice([0x1000, 0x1000, rng.randrange(0, 0x3000)]))
    return base


# ---- the level information window (videowin.cpp) ---------------------------------------------------------------------------------------------------------------

VW_SIZE = 0x14C


def video_state(env, img, rng, base, pool, *, state=None, destroy=False, level=None):
    """A level information window: the window part, the level, the state, the sprite per level, the movie rectangle packet."""
    window(env, img, rng, base, VW_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + 0xAC, rng.choice([0, 1, 5, 9, 10, 12, 13, rng.randrange(0, 14)]) if level is None else level)
    img.u32(base + 0xB0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 14)]))
    img.u32(base + 0xB4, rng.choice([0, 1, 2, 3, 3, 4, 5]) if state is None else state)
    img.u32(base + 0xB8, rng.choice([0, 0, 1, 7, 30]))
    img.u32(base + 0xBC, rng.randrange(0, 0x200))
    img.u32(base + 0xC0, rng.randrange(0, 0x200))
    img.u32(base + 0xC4, rng.randrange(0, 0x100))
    img.u32(base + 0xC8, rng.randrange(0, 0x80))
    packet = A(env, OBJ[1])
    img.garbage(packet, 0x30, rng)
    img.u32(base + 0xCC, packet)
    for k in range(14):
        img.u32(base + 0xD0 + 4 * k, 0 if destroy and rng.random() < 0.15 else pool.take())
    img.u32(base + 0x140, rng.randrange(-20, 100) & 0xFFFFFFFF)
    img.u32(base + 0x144, rng.randrange(-20, 100) & 0xFFFFFFFF)
    img.u8(base + 0x148, rng.choice([0, 1]))
    return base


def video_case(env, **kw):
    img, rng = new_image(env)
    career_world(env, img, rng)
    screen_world(env, img, rng)
    pool = Pool(env, img, rng)
    video_state(env, img, rng, A(env, OBJ[0]), pool, **kw)
    return img, rng


@gvwn(0x0045B3A0, "VideoWindow_ScalarDelete", this=OBJ[0])
def gen_video_delete(env, n):
    img, rng = video_case(env, destroy=True)
    return [rng.choice([0, 1, 1, 2, 3])], img.pokes()


@gvwn(0x0045B3C0, "VideoWindow_Destroy", this=OBJ[0])
def gen_video_destroy(env, n):
    img, rng = video_case(env, destroy=True)
    return [], img.pokes()


@gvwn(0x0045B470, "VideoWindow_Draw", this=OBJ[0])
def gen_video_draw(env, n):
    img, rng = video_case(env)
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@gvwn(0x0045B750, "VideoWindow_Update", this=OBJ[0])
def gen_video_update(env, n):
    img, rng = video_case(env)
    return [], img.pokes()


@gvwn(0x0045B890, "VideoWindow_SetNext", this=OBJ[0])
def gen_video_set_next(env, n):
    img, rng = video_case(env)
    return [rng.randrange(-1, 14) & 0xFFFFFFFF], img.pokes()


@gvwn(0x0045B8A0, "VideoWindow_LevelAvailable")
def gen_video_available(env, n):
    img, rng = new_image(env)
    career_world(env, img, rng)
    return [rng.randrange(-1, 15) & 0xFFFFFFFF, rng.choice([1, 1, 2, 3, 4, 0])], img.pokes()


# ---- the level picture window (goalwin.cpp) --------------------------------------------------------------------------------------------------------------------

LP_SIZE = 0x27C


def levelpic_state(env, img, rng, base, pool, *, destroy=False, state=None):
    window(env, img, rng, base, LP_SIZE, pool, children=0)
    img.u32(base + 0x28, rng.choice([1, 1, 2, 3, 4]))
    font_object(env, img, rng, base + 0x2C)
    img.u8(base + 0x178, rng.choice([0, 1]))
    img.put(base + 0x179, b"need 100 dollars\0" + bytes(0x10))
    img.put(base + 0x199, b"to unlock level\0" + bytes(0x10))
    for k in range(4):
        img.u32(base + 0x1BC + 4 * k, rng.randrange(0, 200))
    img.u32(base + 0x1CC, rng.choice([0x40, 0x80, 0x100]))
    img.u32(base + 0x1D0, rng.choice([0x20, 0x40, 0x80]))
    img.u32(base + 0x1D4, rng.choice([0, 1, 2]) if state is None else state)
    img.u32(base + 0x1D8, rng.randrange(0, 14))
    img.u32(base + 0x1DC, rng.randrange(0, 14))
    img.u32(base + 0x1E0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 14)]))
    img.u32(base + 0x1E4, rng.choice([0, 1, 2]))
    for k in range(14):
        img.u32(base + 0x1E8 + 4 * k, 0 if destroy and rng.random() < 0.15 else picture(env, img, rng, pool, load=True))
        img.u32(base + 0x224 + 4 * k, rng.randrange(0, 0x100))
    img.u32(base + 0x25C, rng.randrange(0, 0x80))
    img.u32(base + 0x260, rng.randrange(0, 0x40))
    img.u32(base + 0x264, rng.randrange(-400, 400) & 0xFFFFFFFF)
    img.u32(base + 0x268, rng.randrange(-60, 60) & 0xFFFFFFFF)
    img.u32(base + 0x26C, rng.randrange(-60, 60) & 0xFFFFFFFF)
    for slot in (0x270, 0x274):
        img.u32(base + slot, 0 if destroy and rng.random() < 0.2 else pool.take())
    img.u32(base + 0x278, rng.choice([0, 1, 0x28, rng.randrange(0, 0x30)]))
    img.u32(SPOOL, rng.randrange(0x1000, 0x100000))


def levelpic_case(env, **kw):
    img, rng = new_image(env)
    career_world(env, img, rng)
    screen_world(env, img, rng)
    pool = Pool(env, img, rng)
    levelpic_state(env, img, rng, A(env, OBJ[0]), pool, **kw)
    return img, rng


@glvp(0x0045B950, "LevelPic_Construct", this=OBJ[0])
def gen_levelpic_construct(env, n):
    img, rng = new_image(env)
    career_world(env, img, rng)
    screen_world(env, img, rng)
    img.garbage(A(env, OBJ[0]), LP_SIZE, rng)
    img.u32(SPOOL, rng.randrange(0x1000, 0x100000))
    return [rng.randrange(0, 14), rng.choice([1, 1, 2, 3, 4]), rng.randrange(0, 300), rng.randrange(0, 200), rng.choice([0xFFFFFFFF, 0, 40]), rng.choice([0xFFFFFFFF, 0, 40])], img.pokes()


@glvp(0x0045BD30, "LevelPic_ScalarDelete", this=OBJ[0])
def gen_levelpic_delete(env, n):
    img, rng = levelpic_case(env, destroy=True)
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@glvp(0x0045BD50, "LevelPic_Destroy", this=OBJ[0])
def gen_levelpic_destroy(env, n):
    img, rng = levelpic_case(env, destroy=True)
    return [], img.pokes()


@glvp(0x0045BE00, "LevelPic_Update", this=OBJ[0])
def gen_levelpic_update(env, n):
    img, rng = levelpic_case(env)
    return [], img.pokes()


@glvp(0x0045C050, "LevelPic_SetLockText", this=OBJ[0])
def gen_levelpic_lock_text(env, n):
    img, rng = levelpic_case(env)
    return [], img.pokes()


@glvp(0x0045C1E0, "LevelPic_Draw", this=OBJ[0])
def gen_levelpic_draw(env, n):
    img, rng = levelpic_case(env)
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


@glvp(0x0045C360, "LevelPic_Select", this=OBJ[0])
def gen_levelpic_select(env, n):
    img, rng = levelpic_case(env)
    return [rng.randrange(0, 14), rng.randrange(-5, 6) & 0xFFFFFFFF], img.pokes()


# ---- the TRG type 500 node's record ------------------------------------------------------------------------------------------------------------------------------

def record_state(env, img, rng, base, pool, *, light=None, active=None, near=None):
    img.garbage(base, 0x58, rng)
    img.u32(base, 0x00518B70)
    camera = A(env, OBJ[3])
    img.garbage(camera, 0x40, rng)
    img.u32(0x0056A960, camera)
    cx, cz = rng.randrange(-0x40000, 0x40000), rng.randrange(-0x40000, 0x40000)
    img.u32(camera + 8, cx & 0xFFFFFFFF)
    img.u32(camera + 0x10, cz & 0xFFFFFFFF)
    near = rng.random() < 0.6 if near is None else near
    spread = 0x30000 if near else 0x200000
    img.u32(base + 0x10, (cx + rng.randrange(-spread, spread)) & 0xFFFFFFFF)
    img.u32(base + 0x18, (cz + rng.randrange(-spread, spread)) & 0xFFFFFFFF)
    img.u32(base + 0x50, rng.choice([20, 60, 100, 200]))
    img.u8(base + 0x4A, rng.choice([0, 1, 1, 1]) if active is None else active)
    have = rng.random() < 0.5 if light is None else light
    img.u32(base + 0x54, pool.take() if have else 0)
    for offset in (0x3C, 0x40, 0x4C):
        img.u32(base + offset, rng.getrandbits(32))
    for offset in range(0x44, 0x4A):
        img.u8(base + offset, rng.randrange(256))


@gprl(0x0045C390, "TRG_InitializeExtendedRecord", this=OBJ[0])
def gen_record_construct(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[0]), 0x58, rng)
    vector = A(env, OBJ[1]) + 0x100
    img.garbage(vector, 0x10, rng)
    return [vector, rng.getrandbits(16), rng.randrange(10, 400), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(8), rng.getrandbits(8),
            rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8)], img.pokes()


@gprl(0x0045C420, "TRG_ExtendedRecord_ScalarDelete", this=OBJ[0])
def gen_record_delete(env, n):
    img, rng = new_image(env)
    record_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@gprl(0x0045C440, "TRG_ExtendedRecord_Destroy", this=OBJ[0])
def gen_record_destroy(env, n):
    img, rng = new_image(env)
    record_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


@gprl(0x0045C4A0, "TRG_ExtendedRecord_Update", this=OBJ[0])
def gen_record_update(env, n):
    img, rng = new_image(env)
    record_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    return [], img.pokes()


# ---- the light editor ---------------------------------------------------------------------------------------------------------------------------------------------

LE_SIZE = 0x240
LIGHT_VALUES = 0x0056201C


def editor_state(env, img, rng, base, pool, *, destroy=False):
    window(env, img, rng, base, LE_SIZE, pool, children=0)
    img.u32(base, 0x00518B78)
    rows = []
    for k in range(14):
        rows.append(item_row(env, img, rng, k, k, selectable=1))
    for k in range(0x60):
        img.u32(base + 0xB0 + 4 * k, (rows[k] if k < 14 and not (destroy and rng.random() < 0.1) else 0))
    listing = list_object(env, img, rng, 0, rows, highlighted=rng.randrange(0, 14))
    img.u32(base + 0xAC, listing)
    if destroy:
        # the destructor deletes the properties object through its vtable: a fake one (the arena's blocks all start with VT_ANY)
        props = A(env, OBJ[3]) + 0x400
        img.garbage(props, 0x17C, rng)
        img.u32(props, A(env, VT_ANY))
        img.u32(base + 0x230, props)
    else:
        img.u32(base + 0x230, props_at(env, rng.randrange(4)))
    record = A(env, OBJ[2])
    img.garbage(record, 0x40, rng)
    img.u32(base + 0x234, record)
    for k in range(14):
        img.u32(LIGHT_VALUES + 4 * k, rng.choice([0, 0x40, 0x7C0, 0x2000, rng.randrange(0, 0x2100), rng.randrange(0, 0x1000)]))
    return record


@gled(0x0045C5E0, "LightEditor_Construct", this=OBJ[0])
def gen_editor_construct(env, n):
    img, rng = new_image(env)
    img.garbage(A(env, OBJ[0]), LE_SIZE, rng)
    record = A(env, OBJ[2])
    img.garbage(record, 0x40, rng)
    for k in range(14):
        img.u32(LIGHT_VALUES + 4 * k, rng.getrandbits(32))
    return [record, A(env, FIND_FONTS + FONT_SIZE * rng.randrange(4)), rng.randrange(0, 300), rng.randrange(0, 200)], img.pokes()


@gled(0x0045CC60, "LightEditor_ScalarDelete", this=OBJ[0])
def gen_editor_delete(env, n):
    img, rng = new_image(env)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 1, 2])], img.pokes()


@gled(0x0045CC80, "LightEditor_Destroy", this=OBJ[0])
def gen_editor_destroy(env, n):
    img, rng = new_image(env)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@gled(0x0045CD10, "LightEditor_Draw", this=OBJ[0])
def gen_editor_draw(env, n):
    img, rng = new_image(env)
    editor_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng))
    img.u8(0x0056B1E4, rng.choice([0, 0, 1]))
    img.u8(0x0056B1F4, rng.choice([0, 0, 1]))
    img.u8(0x0056B255, rng.choice([0, 1]))
    return [rng.randrange(-20, 300) & 0xFFFFFFFF, rng.randrange(-20, 300) & 0xFFFFFFFF], img.pokes()


# ---- the loading screen -----------------------------------------------------------------------------------------------------------------------------------------------

def text_at(env, index):
    return A(env, TEXTS + TEXT_STRIDE * index)


def caption_lines(env, img, rng, count=None):
    """An array of text pointers ended by an empty text (the screens' captions)."""
    count = rng.randrange(0, 4) if count is None else count
    cells = A(env, OBJ[2]) + 0x400
    for k in range(count):
        img.u32(cells + 4 * k, text_at(env, rng.randrange(len(TEXT_POOL))))
    img.u32(cells + 4 * count, 0x0055FE10)
    return cells


def loading_statics(env, img, rng):
    img.u32(0x0056205C, rng.randrange(0, 300))
    img.u32(0x00562060, rng.randrange(0, 400))
    img.u32(0x00562064, rng.randrange(0, 200))
    img.u32(0x00562054, rng.randrange(0, 20))
    img.u32(0x0056207C, rng.randrange(0, 100000))
    img.u32(0x00562080, rng.randrange(0, 5000))
    img.u32(0x00562084, rng.randrange(0, 5000))
    img.u32(0x00562088, rng.choice([0, 100, 600, 780, rng.randrange(0, 5000)]))
    img.u32(0x0056208C, rng.choice([0, 1, 1]))
    img.u8(0x00562090, rng.randrange(256))
    img.u32(0x005618C8, rng.choice([0, 0, 0, 1]))


@glds(0x0045D010, "LoadingScreen_Show")
def gen_loading_show(env, n):
    img, rng = new_image(env)
    screen_world(env, img, rng)
    return [text_at(env, rng.randrange(len(TEXT_POOL))), caption_lines(env, img, rng), rng.choice([0, 1, 2, 3, 0x100])], img.pokes()


@glds(0x0045D5E0, "LoadingScreen_BeginProgress")
def gen_loading_begin(env, n):
    img, rng = new_image(env)
    screen_world(env, img, rng)
    loading_statics(env, img, rng)
    return [rng.choice([0xA, 0xD, 0x10, 0x11, 0xC]), rng.choice([0xD2, 100, 200]), rng.choice([0xC8, 100, 255, 256]), rng.choice([3, 4, 8])], img.pokes()


@glds(0x0045D680, "LoadingScreen_EndProgress")
def gen_loading_end(env, n):
    img, rng = new_image(env)
    loading_statics(env, img, rng)
    return [], img.pokes()


@glds(0x0045D690, "LoadingScreen_UpdateProgress")
def gen_loading_update(env, n):
    img, rng = new_image(env)
    screen_world(env, img, rng)
    loading_statics(env, img, rng)
    return [], img.pokes()


@glds(0x0045D760, "LoadingScreen_Tick")
def gen_loading_tick(env, n):
    img, rng = new_image(env)
    screen_world(env, img, rng)
    loading_statics(env, img, rng)
    return [rng.choice([0, 1, 1, 0x100])], img.pokes()


@glds(0x0045D7A0, "LoadingScreen_CopyFrame")
def gen_loading_copy(env, n):
    img, rng = new_image(env)
    screen_world(env, img, rng)
    loading_statics(env, img, rng)
    return [], img.pokes()


@glds(0x0045D830, "LoadingScreen_Start")
def gen_loading_start(env, n):
    img, rng = new_image(env)
    career_world(env, img, rng, mode=rng.choice([1, 1, 2, 3, 4, 7, 9, 10]))
    screen_world(env, img, rng)
    loading_statics(env, img, rng)
    img.u8(0x00561CA9, rng.choice([0, 0, 0, 1]))
    img.u8(0x00561990, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056B8D4, rng.choice([0, 0, 0, 1, 0xFFFF, 2]))
    img.u8(0x00561CA8, rng.choice([0, 0, 0, 1]))
    img.u8(0x005614C4, rng.choice([0, 1]))
    img.u32(0x00561C98, rng.choice([0, 0, 1]))
    img.u32(0x0052E088, rng.getrandbits(32))
    return [rng.choice([0, 1, 2, 3, 0x100])], img.pokes()


# ---- the goals checklist screen ------------------------------------------------------------------------------------------------------------------------------------

@ggls(0x0045DB40, "GoalsScreen_Run")
def gen_goals_run(env, n):
    img, rng = new_image(env)
    career_world(env, img, rng, mode=rng.choice([1, 1, 2, 3]))
    screen_world(env, img, rng)
    pool = Pool(env, img, rng)
    img.u8(0x00562079, rng.choice([0, 1]))
    img.u32(0x00562068, caption_lines(env, img, rng))
    img.u32(0x00562070, pool.take())
    img.u32(0x00562074, pool.take())
    img.u32(0x0056206C, rng.choice([0, pool.take()]))
    img.u32(0x00562058, rng.choice([0, pool.take()]))
    img.u8(0x00562078, rng.choice([0, 1, 2]))
    img.u32(0x00561C98, rng.choice([0, 1]))
    img.u32(0x0056A8E8, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056B8D4, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056A89C, rng.choice([0, 0, 0, 1]))
    for address in (0x0056AFF9, 0x0056B029, 0x0056B028, 0x0056B194, 0x0056B195, 0x006A1417):
        img.u8(address, rng.choice([0, 0, 1]))
    img.u32(0x00533F3C, rng.choice([0, 1, 2]))
    loading_statics(env, img, rng)
    return [], img.pokes()
