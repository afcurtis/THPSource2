"""Phase 4.10g case generators, part 3: the controller page (optctl, class at 0x005194a4) and the cheats page (optchk, class at 0x005194c4).  See frontend_specs_opt.py for
the slab, the stubs and the helpers.

The controller page's button rows are real text rows (the page changes their text through a virtual call, which the toolkit runs natively); its steppers, list and
styles are fake objects. The cheats page's rows are fake item records; the pictures are fake pool objects.
"""

from __future__ import annotations

from create_specs import spec
from frontend_specs_menu import VT_REAL as MENU_REAL
from frontend_specs_menu import coords, poke_props
from frontend_specs_opt import opt_image, CONTROLLER_TABLE, GROUP_STUBS, Items, install_opt_common, reinstall, opt_world, pad_set, register_opt
from frontend_specs_playsel import disable
from frontend_specs_screens import OBJ
from frontend_specs_screens_cases import Pool, font_object, font_ptr, window
from frontend_specs_skwin import A

for _group in ("optctl", "optchk"):
    register_opt(_group)

PAD_STRIDE = 0x16C
AUTOKICK_TABLE = 0x0055FC2C
VIBRATION_TABLE = 0x0055FC34
TEXT_VARS = [b"Short message", b"Weight", b"Cheat name", b"Another long cheat name", b"Slo-mo"]


def ctl_spec(address, name, this=True):
    return spec(address, name, "optctl", this=(lambda env: env.slab + OBJ[0]) if this else None)


def chk_spec(address, name, this=True):
    return spec(address, name, "optchk", this=(lambda env: env.slab + OBJ[0]) if this else None)


# ---- the controller page ------------------------------------------------------------------------------------------------------------------------------------------

C_SIZE = 0x25C


def ctl_install(fc, env):
    install_opt_common(fc, env)
    rng = env.rng
    disable(fc, "engine_00488160", "engine_00488590", "engine_004885c0")
    fc.stub(0x00489690, 1, "engine_00489690", result=lambda a: rng.choice([0, 1]))


def controls_state(env, img, rng, base, pool, items, *, destroy=False, player=None, pick=None, values=None):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    for i in range(4):
        poke_props(env, img, i, rng)
    window(env, img, rng, base, C_SIZE, pool, children=rng.randrange(0, 3))
    img.u32(base + 0xAC, rng.choice([0, 1]) if player is None else player)
    # the four buttons by id (0, 2, 1, 3 in the list), real text rows
    texts = [b"Spin left: Nollie", b"Switch", b"X", b"Autokick"]
    buttons = {}
    for ident in (0, 2, 1, 3):
        buttons[ident] = items.item(ident, value=rng.randrange(0, 4) if values is None else values[ident], vtable=MENU_REAL["text"], text=rng.choice(texts))
    autokick = items.item(4, value=rng.choice([1, 2]))
    vibration = items.item(7, value=rng.choice([1, 2]))
    reset = items.item(8)
    back = items.item(9)
    title = items.item(0, selectable=0, vtable=MENU_REAL["scaled"], text=b"Player 1 controls")
    rows = [title, buttons[0], buttons[2], buttons[1], buttons[3], autokick, vibration, reset, back]
    img.put(base + 0xBC, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0xBC + 4 * i, row)
    ids_in_list = [0, 0, 1, 2, 3, 4, 7, 8, 9]
    chosen = rng.randrange(1, len(rows)) if pick is None else pick
    img.u32(base + 0xB8, keep(items.fake_list(rows, pick=chosen)))
    img.u32(base + 0xB0, rng.choice([0, 1, 2, 3, 4, 8]))
    img.u32(base + 0x23C, keep(pool.take()))
    img.u32(base + 0x240, keep(pool.take()))
    img.u32(base + 0x244, keep(autokick))
    img.u32(base + 0x248, keep(vibration))
    for slot, ident in ((0x24C, 0), (0x250, 1), (0x254, 2), (0x258, 3)):
        img.u32(base + slot, keep(buttons[ident]))
    return buttons


@ctl_spec(0x00487490, "ControlsMenu_Construct")
def gen_ctl_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.garbage(A(env, OBJ[0]), C_SIZE, rng)
    pool = Pool(env, img, rng)
    return [rng.choice([0, 1]), font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, pool.take(), pool.take()], img.pokes()


@ctl_spec(0x00487C10, "ControlsMenu_ScalarDelete")
def gen_ctl_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=False)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@ctl_spec(0x00487C30, "ControlsMenu_Destroy")
def gen_ctl_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=False)
    return [], img.pokes()


@ctl_spec(0x00487DC0, "ControlsMenu_Update")
def gen_ctl_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@ctl_spec(0x00487DD0, "ControlsMenu_Draw")
def gen_ctl_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@ctl_spec(0x00487F60, "ControlsMenu_ControlText", this=False)
def gen_ctl_text(env, n):
    img, rng = opt_image(env)
    buffer = A(env, OBJ[1])
    img.put(buffer, rng.choice([b"seed", b"", b"A longer starting text"]) + b"\0")
    return [buffer, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4]), rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 9])], img.pokes()


@ctl_spec(0x00488160, "ControlsMenu_Step")
def gen_ctl_step(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["left"], ["right"], ["confirm"], ["back"], ["up"], ["down"], ["left", "confirm"], []]), chance=0.8, other=0.2)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), pick=rng.randrange(1, 9))
    return [], img.pokes()


@ctl_spec(0x00488410, "ControlsMenu_WrapValue", this=False)
def gen_ctl_wrap(env, n):
    img, rng = opt_image(env)
    items = Items(env, img, rng)
    row = items.item(rng.randrange(0, 4), value=rng.randrange(-3, 8) & 0xFFFFFFFF)
    return [row, rng.choice([0, 0, 1, 0xFFFFFFFF]), rng.choice([3, 3, 4, 2])], img.pokes()


@ctl_spec(0x00488480, "ControlsMenu_ValueUnique")
def gen_ctl_unique(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    buttons = controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [buttons[rng.choice([0, 1, 2, 3])]], img.pokes()


@ctl_spec(0x004884C0, "ControlsMenu_FixDuplicates")
def gen_ctl_fix(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    values = {k: rng.randrange(0, 4) for k in range(4)}
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), values=values)
    img.u32(A(env, OBJ[0]) + 0xB0, rng.choice([0, 1, 2, 3, 3, 0, 1, 2, 3, 8]))
    return [], img.pokes()


@ctl_spec(0x00488590, "ControlsMenu_Selectable")
def gen_ctl_selectable(env, n):
    img, rng = opt_image(env)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), pick=rng.randrange(0, 9))
    return [], img.pokes()


@ctl_spec(0x004885C0, "ControlsMenu_NotSelectable")
def gen_ctl_not_selectable(env, n):
    img, rng = opt_image(env)
    controls_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), pick=rng.randrange(0, 9))
    return [], img.pokes()


GROUP_STUBS["optctl"] = ctl_install

# ---- the cheats page -------------------------------------------------------------------------------------------------------------------------------------------------

K_SIZE = 0x288


def chk_install(fc, env):
    install_opt_common(fc, env)
    rng = env.rng
    disable(fc, "engine_004885d0", "engine_00489240", "engine_004892a0", "engine_004891a0", "engine_00489350", "engine_00489390")
    # (the cheat names are menu strings: under 32 characters, or the toolkit's assert fires and the copy overruns the row)
    from frontend_specs_screens import TEXT_POOL, TEXT_STRIDE, TEXTS

    names = [env.slab + TEXTS + TEXT_STRIDE * i for i, text in enumerate(TEXT_POOL) if len(text) < 32]
    fc.stub(0x00416940, 1, "engine_00416940", result=lambda a: rng.choice([0, 1, 1, 1]))
    fc.stub(0x00416F50, 1, "engine_00416f50", result=lambda a: rng.choice([0, 0, 1, 1, 2]))
    fc.stub(0x00416E30, 1, "engine_00416e30", result=lambda a: names[rng.randrange(len(names))])
    fc.stub(0x00416980, 1, "engine_00416980", result=lambda a: rng.choice([0, 1, 5, 100, 255, rng.getrandbits(32)]))
    fc.stub(0x00416740, 1, "engine_00416740", result=lambda a: rng.choice([0, 1]))
    reinstall(fc, env, 0x004B1980, 1, result=lambda a: rng.randrange(0, 20))


def cheats_state(env, img, rng, base, pool, items, *, destroy=False, pick=None, hidden=None, ids=None, values=None):
    keep = (lambda value: 0 if rng.random() < 0.15 else value) if destroy else (lambda value: value)
    window(env, img, rng, base, K_SIZE, pool, children=rng.randrange(0, 3), hidden=hidden)
    ids = list(range(0x11)) + [0xFFFFFFFF] if ids is None else ids
    rows = []
    for k, ident in enumerate(ids):
        value = rng.choice([1, 2]) if ident != 8 else rng.choice([50, 60, 100, 150, 140, 40, 160])
        if values is not None and k < len(values):
            value = values[k]
        rows.append(items.item(ident, value=value))
    img.put(base + 0xB0, bytes(4 * 0x60))
    for i, row in enumerate(rows):
        img.u32(base + 0xB0 + 4 * i, row)
    img.u32(base + 0xAC, keep(items.fake_list(rows, pick=rng.randrange(0, len(rows)) if pick is None else pick)))
    img.u32(base + 0x230, keep(pool.take()))
    img.u32(base + 0x234, keep(pool.take()))
    for k in range(20):
        img.u32(base + 0x238 + 4 * k, keep(pool.take()) if rng.random() < 0.9 or not destroy else 0)


@chk_spec(0x004885D0, "CheatsPage_Construct")
def gen_chk_construct(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    img.garbage(A(env, OBJ[0]), K_SIZE, rng)
    pool = Pool(env, img, rng)
    return [font_ptr(env, rng), 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, pool.take(), pool.take()], img.pokes()


@chk_spec(0x00488D50, "CheatsPage_ScalarDelete")
def gen_chk_delete(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@chk_spec(0x00488D70, "CheatsPage_Destroy")
def gen_chk_destroy(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), destroy=True)
    return [], img.pokes()


@chk_spec(0x00488E30, "CheatsPage_Update")
def gen_chk_update(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), hidden=rng.choice([0, 1, 1]))
    return [], img.pokes()


@chk_spec(0x00488E40, "CheatsPage_Draw")
def gen_chk_draw(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["left"], ["right"], ["up"], []]), chance=0.7, other=0.15)
    # the weight and the two exclusive switches are the rows the draw acts on: highlight them often
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), hidden=rng.choice([0, 0, 0, 1]), pick=rng.choice([8, 0xB, 4, 8, 0xB, 4, rng.randrange(0, 18)]))
    return [coords(rng), coords(rng)], img.pokes()


@chk_spec(0x004891A0, "CheatsPage_Step")
def gen_chk_step(env, n):
    img, rng = opt_image(env)
    opt_world(env, img, rng)
    pad_set(img, rng, rng.choice([["confirm"], ["back"], []]), chance=0.7)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), pick=rng.choice([0, 3, 17, 17]))
    return [], img.pokes()


@chk_spec(0x00489240, "CheatsPage_Refresh")
def gen_chk_refresh(env, n):
    img, rng = opt_image(env)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@chk_spec(0x004892A0, "CheatsPage_Store")
def gen_chk_store(env, n):
    img, rng = opt_image(env)
    values = [rng.choice([1, 2, 1, 2, 0, 3, 77]) for _ in range(18)]
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng), values=values)
    return [], img.pokes()


@chk_spec(0x00489350, "CheatsPage_Switchable")
def gen_chk_switchable(env, n):
    img, rng = opt_image(env)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


@chk_spec(0x00489390, "CheatsPage_OnBack")
def gen_chk_on_back(env, n):
    img, rng = opt_image(env)
    cheats_state(env, img, rng, A(env, OBJ[0]), Pool(env, img, rng), Items(env, img, rng))
    return [], img.pokes()


GROUP_STUBS["optchk"] = chk_install
