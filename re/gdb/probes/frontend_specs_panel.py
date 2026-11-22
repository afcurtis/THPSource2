"""Phase 4.10b case generators: the in-game HUD (panel.cpp) and the trick score stack.

The panel's data (CPanelData) is embedded in the skater object at skater + 0x168, so these cases run in a live level
(the probe's Warehouse run) on the real player-one skater: a case pokes a random legal state into the skater, the
globals and a small scratch slab, calls the retail function, and records the bytes it wrote and the outside services
it reached.  Everything outside panel.cpp is a stub that reports the call (the career, the replay recorder, sound, the
history of tricks, the font's width, the text effects); a virtual method call goes through a fake vtable whose slots
hold the entry points of retail functions nothing here reaches, stubbed one per slot, so each call is a named service
that carries the object.

Slab layout (offsets from the scratch slab, the whole slab is captured):
  0x0000  fake effect vtable (slot 1 bind, 2 unbind, 3 init, 4 update, 7 finished)   0x0040  fake font vtable (slot 1 draw)
  0x0080  fake sprite vtable (slot 0 delete, slot 3 draw)
  0x0100  eight message nodes of 0x20 bytes (a u16 at +6 is the node's total age)
  0x0200  a gap record (its name string at +8)      0x0300  twenty trick name strings of 0x40 bytes
  0x0800  four fake font objects of 0x14c bytes     0x1000  scratch
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, Pokes, spec
from opentony.memory import mem

SKATER_POINTER = 0x0056A960
SLAB = 0x2000
EFFECT_VT = 0x0000
FONT_VT = 0x0040
SPRITE_VT = 0x0080
NODES = 0x0100
GAP_RECORD = 0x0200
NAMES = 0x0300
NAME_STRIDE = 0x40
FONTS = 0x0800
SCRATCH = 0x1000

PANEL = 0x168
CURRENT_LEVEL = 12

# retail entry points nothing a HUD call reaches; one per fake vtable slot
VIRTUAL_DELETE = 0x004AD9B0
EFFECT_BIND = 0x004AD9D0
EFFECT_UNBIND = 0x004AD9E0
EFFECT_INIT = 0x004AD9F0
EFFECT_UPDATE = 0x004ADA10
EFFECT_FINISHED = 0x004AD870
SPRITE_DRAW = 0x004AD890
FONT_DRAW = 0x004AD5E0

TRICK_NAMES = [b"Kickflip", b"Heelflip", b"Pop Shove-It", b"Impossible", b"360 Flip", b"Nollie Hardflip", b"Boardslide",
               b"50-50 Grind", b"Manual", b"Revert", b"Indy Grab", b"Mute", b"Superman", b"Christ Air", b"720 Spin", b"FS Noseslide",
               b"Fakie Tailslide", b"Bluntslide to Fakie", b"Casper Slide", b"X"]

# the mutable scratch regions the group captures besides the skater and the slab (each is restored after every case)
GLOBALS = [
    (0x00533F38, 0x70),    # game mode, players, 0x533f9c
    (0x0056A470, 4),       # career flag
    (0x0056A89C, 4),       # a flag the sprite loader tests
    (0x0056A8E0, 4),       # paused
    (0x0056A928, 4),       # viewport mode
    (0x0056A93C, 4),       # the point timer's step
    (0x0056B710, 0x60),    # the sprite pointers, the holding flash, the tape timers, the tenfold flag
    (0x0056B7EC, 4),
    (0x0056E320, 4),       # simulation time
    (0x0056E5D8, 4),       # replay state
    (0x0056E5F8, 0x38),    # replay frames
    (0x005685F4, 4),       # animation clock
    (0x0056865C, 4),       # animation scale
    (0x005361D0, 0x0C),    # replay hint limit and the tape slide positions
    (0x00537B3C, 4),
    (0x00561990, 4),       # replay video restart
    (0x00561BFC, 4),       # game status
    (0x00561C04, 4),       # paused hud
    (0x005614C4, 4),
    (0x0056DB5C, 0x10),    # versus state and the skater who is it
    (0x006A3B21, 4),       # network flag
    (0x006A3D2C, 4),       # network local player
    (0x029DA3A4, 4),       # count-up flag
    (0x00560FD0, 8),       # arena cursor and the render state pointer
    (0x00568628, 4),       # arena end
    (0x005619E0, 0x150),   # the HUD copy of the tricks font
    (0x0056DB80, 8),       # trick object flags
]
LEVEL_RECORD = 0x0053C240 + CURRENT_LEVEL * 0x1AC
LEVEL_GOALS = 0x0053C2B0 + CURRENT_LEVEL * 0x1AC
SPRITE_POINTERS = (0x0056B73C, 0x0056B710, 0x0056B744, 0x0056B748, 0x0056B74C, 0x0056B750)
TEXTS = 0x0C00                 # slab: level message list (pointers) at TEXTS, strings at TEXTS + 0x40..


def skater_address():
    return mem.u32(SKATER_POINTER)


def regions(slab):
    out = [(slab, SLAB), (skater_address(), 0x3300), *GLOBALS, (LEVEL_RECORD, 0x1AC)]
    state = mem.u32(0x00560FD4)
    out.append((state, 0x90))                       # the render state (+0x84: the bucket heads pointer)
    out.append((mem.u32(state + 0x84), 0x20))       # the bucket heads
    out.append((mem.u32(0x00560FD0), 0x100))        # the polygon arena from the cursor
    for pointer in SPRITE_POINTERS:
        out.append((mem.u32(pointer), 0x20))        # the sprite objects the HUD draws with
    return out


SLAB_BYTES["panel"] = SLAB
READ_ONLY["panel"] = [
    (0x00536160, 0x70),    # the score multiplier tables
    (0x0052E32C, 8),       # pointers to "Perfect" and "Sloppy"
    (0x00532CC0, 0x40),    # those strings
    (0x0056A898, 4),       # the current level
    (0x0056A960, 4),       # the player-one skater
    (0x00527D54, 4),       # the screen height
    (0x029DA394, 8),       # the screen scale
    (0x0052E5B4, 0x38),    # pointers to the steals and replay hint texts
    (0x00532280, 0x180),   # those texts
    (0x005194E0, 0x20),    # the S K A T E letters
    (0x005361DC, 0x84),    # sprite and font names
    (0x005254E0, 0x10),    # s2tricks.fnt
    (0x005287A4, 0x10),    # ingame1.fnt
    (0x00536AD0, 0x10),    # OLLIE
]
EXTRA_REGIONS["panel"] = regions


def seed_panel(env, rng):
    from opentony.fcall import write_chunks

    env.skater = skater_address()
    for slot, entry in ((0, VIRTUAL_DELETE), (1, EFFECT_BIND), (2, EFFECT_UNBIND), (3, EFFECT_INIT), (4, EFFECT_UPDATE), (7, EFFECT_FINISHED)):
        write_chunks(env.slab + EFFECT_VT + 4 * slot, struct.pack("<I", entry))
    write_chunks(env.slab + FONT_VT + 4, struct.pack("<I", FONT_DRAW))
    write_chunks(env.slab + SPRITE_VT, struct.pack("<I", VIRTUAL_DELETE))
    write_chunks(env.slab + SPRITE_VT + 12, struct.pack("<I", SPRITE_DRAW))
    for i, name in enumerate(TRICK_NAMES):
        write_chunks(env.slab + NAMES + NAME_STRIDE * i, name + b"\0")
    write_chunks(env.slab + GAP_RECORD + 8, b"Gap name\0")
    # the level message list: four pointers to strings, then a null
    messages = [b"Get the goal", b"Skate fast", b"Careful", b"Wow"]
    for i, text in enumerate(messages):
        write_chunks(env.slab + TEXTS + 0x40 + 0x20 * i, text + b"\0")
        write_chunks(env.slab + TEXTS + 4 * i, struct.pack("<I", env.slab + TEXTS + 0x40 + 0x20 * i))
    return {}


SEEDS["panel"] = seed_panel


def install_panel_stubs(fc, env):
    rng = env.rng
    # the simulation clock is a live timer: keep its callback from ticking while a forced call runs
    fc.stub(0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)
    # the career, the replay recorder and sound
    fc.stub(0x00414D90, 1, "career_award_trick_gap")
    fc.stub(0x00414BD0, 1, "career_award_goal_gap")
    fc.stub(0x004CB580, 1, "replay_highlight_start")
    fc.stub(0x004CB5C0, 2, "replay_highlight_end")
    fc.stub(0x004AD620, 3, "sfx_play")
    # string search, the trick history and the skater's own helpers
    fc.stub(0x004B82B0, 2, "string_find_nocase", text=True, result=lambda a: rng.choice([0, 0, 1]))
    fc.stub(0x0046CE70, 2, "trick_history_add", convention="thiscall", this=True, result=lambda a: rng.randrange(1, 7))
    fc.stub(0x0046CF20, 2, "trick_history_remove", convention="thiscall", this=True)
    fc.stub(0x0046CE60, 0, "trick_history_reset", convention="thiscall", this=True)
    fc.stub(0x0048F3A0, 1, "skater_profile_value", convention="thiscall", this=True, result=lambda a: rng.randrange(0, 11))
    fc.stub(0x004904D0, 2, "skater_bail", convention="thiscall", this=True)
    fc.stub(0x004904C0, 0, "trick_reset_buffers", convention="thiscall", this=True)
    # the font's width of a line (a thiscall on the font object; the line is the call's text)
    fc.stub(0x0044A4D0, 1, "font_text_width", convention="thiscall", this=True, keep=slice(0, 0), text=True, result=env.text_width)
    # the text effects
    fc.stub(0x004BA780, 0, "zoom_start", convention="thiscall", this=True)
    fc.stub(0x004B9720, 0, "text_effect_disable", convention="thiscall", this=True)
    # virtual methods of the effects
    fc.stub(VIRTUAL_DELETE, 1, "virtual_delete", convention="thiscall", this=True)
    fc.stub(EFFECT_BIND, 1, "effect_bind", convention="thiscall", this=True)
    fc.stub(EFFECT_UNBIND, 1, "effect_unbind", convention="thiscall", this=True)
    fc.stub(EFFECT_INIT, 0, "effect_init", convention="thiscall", this=True)
    fc.stub(EFFECT_UPDATE, 0, "effect_update", convention="thiscall", this=True)
    fc.stub(EFFECT_FINISHED, 0, "effect_finished", convention="thiscall", this=True, result=lambda a: rng.choice([0, 1]))
    fc.stub(SPRITE_DRAW, 3, "sprite_draw", convention="thiscall", this=True)
    # the font's draw method: the string pointer is often a stack buffer, so only the font, x and y are recorded
    fc.stub(FONT_DRAW, 4, "font_draw", convention="thiscall", this=True, keep=slice(0, 2), text=2)
    # the career
    fc.stub(0x00416980, 1, "career_get_cheat", result=lambda a: rng.choice([0, 0, 1]))
    fc.stub(0x00414280, 0, "career_check_score")
    fc.stub(0x004142E0, 1, "career_give_goal")
    fc.stub(0x00414540, 1, "career_goal_index", result=lambda a: rng.choice([-1, 0, 1, 2, 3]))
    fc.stub(0x00414570, 1, "career_give_goal_type")
    fc.stub(0x0044B570, 1, "horse_display_words")
    # the message layer
    fc.stub(0x00473580, 4, "mess_set_rgb")
    fc.stub(0x004734B0, 1, "mess_set_justify")
    fc.stub(0x004734F0, 1, "mess_set_scale")
    fc.stub(0x004735B0, 4, "mess_draw_panel_text", text=2)
    fc.stub(0x004736C0, 1, "mess_delete_by_text", text=True)
    fc.stub(0x004736F0, 5, "mess_add_message", text=True, masks={3: 0xFFFF, 4: 0xFFFF})
    # fonts and text effects
    fc.stub(0x0044AB70, 3, "font_set_rgb", convention="thiscall", this=True)
    fc.stub(0x0044A540, 1, "font_text_extent", convention="thiscall", this=True, keep=slice(0, 0), text=True,
            result=lambda a: rng.randrange(4, 60))
    fc.stub(0x004B9AC0, 0, "falling_clear_clip", this=True)
    fc.stub(0x004B9AE0, 1, "falling_clear_clip_for", convention="stdcall")
    fc.stub(0x004B9E00, 0, "falling_blood_update", convention="thiscall", this=True)
    fc.stub(0x004B9780, 1, "falling_set_blood", convention="thiscall", this=True)
    fc.stub(0x004B9D50, 1, "falling_set_scale", convention="thiscall", this=True)
    # other units' per-frame calls
    fc.stub(0x004C4420, 0, "trick_tips_display")
    fc.stub(0x004B8B40, 0, "versus_update")


GROUP_STUBS = {"panel": install_panel_stubs}


# ---- helpers --------------------------------------------------------------------------------------------


class Image:
    """Bytes to poke, merged into runs of consecutive addresses (so overlapping puts never raise)."""

    def __init__(self):
        self.b: dict[int, int] = {}

    def put(self, address, data):
        for i, value in enumerate(data):
            self.b[address + i] = value

    def u32(self, address, value):
        self.put(address, struct.pack("<I", value & 0xFFFFFFFF))

    def u16(self, address, value):
        self.put(address, struct.pack("<H", value & 0xFFFF))

    def u8(self, address, value):
        self.put(address, bytes([value & 0xFF]))

    def pokes(self):
        out = Pokes()
        run_start = None
        run = bytearray()
        for address in sorted(self.b):
            if run_start is not None and address == run_start + len(run):
                run.append(self.b[address])
            else:
                if run_start is not None:
                    out.put(run_start, bytes(run))
                run_start = address
                run = bytearray([self.b[address]])
        if run_start is not None:
            out.put(run_start, bytes(run))
        return out.pokes


def pspec(address, name, this=None):
    if this == "panel":
        resolve = lambda env: env.skater + PANEL  # noqa: E731
    elif this == "skater":
        resolve = lambda env: env.skater  # noqa: E731
    else:
        resolve = this
    return spec(address, name, "panel", this=resolve)


def name_ptr(env, rng):
    return env.slab + NAMES + NAME_STRIDE * rng.randrange(len(TRICK_NAMES))


def entry_at(panel, i):
    return panel + 0x7B0 + 0x28 * i


def put_entries(env, rng, img, panel, count, kinds=(0, 0, 1, 2), nodes=False, name_pool=None):
    for i in range(count):
        e = entry_at(panel, i)
        img.u32(e + 0x00, name_ptr(env, rng))
        img.u32(e + 0x04, rng.choice([rng.randrange(1, 100), rng.randrange(100, 6000), 1, 250]))
        img.u32(e + 0x08, rng.choice([0, 0x12, 5, 30]))
        img.u32(e + 0x0C, rng.choice([0, 0, 1, 2, 3]))
        img.u32(e + 0x10, rng.choice(kinds))
        img.u32(e + 0x14, rng.choice([0, 1, 1]))
        img.u32(e + 0x18, rng.choice([0, 1, 2, 3, 4, 5, 6, 9]))
        img.u32(e + 0x1C, rng.choice([0, 1]))
        img.u32(e + 0x20, rng.getrandbits(32))
        img.u32(e + 0x24, env.slab + NODES + 0x20 * rng.randrange(8) if nodes else 0)
    img.u32(panel + 0x26F0, count)


def landing(env, rng, img, skater, quality=None):
    img.u32(skater + 0x306C, rng.choice([0, 0, 1, 2, 3]) if quality is None else quality)
    img.u32(skater + 0x3078, rng.choice([0, 0, 1]))
    img.u32(skater + 0x307C, rng.choice([0, 0, 1]))
    img.u32(skater + 0x30B8, rng.choice([0, 1, 2, 4]))
    img.u32(skater + 0x30C0, rng.choice([0, 0, 2]))


def globals_state(env, rng, img, mode=None, players=None, viewport=None):
    img.u32(0x00533F38, rng.choice([1, 2, 3, 4, 7, 9, 0, 8]) if mode is None else mode)
    img.u32(0x00533F3C, rng.choice([1, 1, 2]) if players is None else players)
    img.u32(0x0056A928, rng.choice([0, 0, 1]) if viewport is None else viewport)
    img.u32(0x0056A470, rng.choice([0, 1, 1]))
    img.u32(0x0056E320, rng.randrange(0, 5000))
    img.u32(0x0056B760, rng.choice([0, 0, 0, 1]))


def panel_scalars(env, rng, img, panel, skater):
    img.u32(panel + 0x004, rng.choice([0, 100, rng.randrange(0, 100000)]))
    img.u32(panel + 0x010, rng.choice([0, 10, rng.randrange(0, 90000)]))
    img.u32(panel + 0x140, rng.choice([0, rng.randrange(0, 50000)]))
    img.u32(panel + 0x144, rng.choice([0, rng.randrange(0, 50000)]))
    img.u32(panel + 0x148, rng.choice([0, 3]))
    img.u32(panel + 0x14C, rng.choice([0, 1]))
    img.u32(panel + 0x15C, rng.choice([0, 1]))
    img.u32(panel + 0x2844, rng.choice([0, 0x10]))
    img.u16(panel + 0x00A, rng.randrange(0, 30))
    img.u16(panel + 0x00C, rng.randrange(0, 30))
    img.u16(panel + 0x00E, rng.randrange(0, 30))
    img.u32(skater + 0x314C, rng.choice([0, 0, 1]))
    img.u32(skater + 0xD8, rng.getrandbits(32))
    img.u8(skater + 0x163, rng.getrandbits(1))


def effect_objects(env, rng, img, panel):
    """The three text effects embedded in the panel get the fake effect vtable; the fonts the fake font vtable."""
    for off in (0x548, 0x66C, 0x68C):
        img.u32(panel + off, env.slab + EFFECT_VT)
    for off in (0x164, 0x3FC, 0x2B0):
        img.u32(panel + off, env.slab + FONT_VT)
    img.u32(panel + 0x688, rng.choice([0, panel + 0x66C, panel + 0x548]))


# ---- the score stack ------------------------------------------------------------------------------------


@pspec(0x0048CAF0, "TrickScore_SumEntryValues", this="panel")
def gen_sum_values(env, n):
    rng = env.rng
    img = Image()
    put_entries(env, rng, img, env.skater + PANEL, rng.choice([0, 1, 2, 3, 5, 8, 15]))
    return [], img.pokes()


@pspec(0x0048CB60, "TrickScore_ChainValue", this="panel")
def gen_chain_value(env, n):
    rng = env.rng
    img = Image()
    count = rng.choice([1, 2, 3, 5, 8, 12])
    put_entries(env, rng, img, env.skater + PANEL, count)
    return [rng.randrange(0, count + 1)], img.pokes()


@pspec(0x0048CB10, "TrickScore_ChainValueIfFirst", this="panel")
def gen_chain_first(env, n):
    rng = env.rng
    img = Image()
    count = rng.choice([1, 2, 3, 5, 8, 12])
    put_entries(env, rng, img, env.skater + PANEL, count)
    return [rng.randrange(0, count)], img.pokes()


@pspec(0x0048CBC0, "TrickScore_ComputeStackSum", this="panel")
def gen_stack_sum(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    put_entries(env, rng, img, skater + PANEL, rng.choice([0, 1, 2, 3, 4, 6, 9]))
    landing(env, rng, img, skater)
    return [], img.pokes()


@pspec(0x0048CD40, "TrickScore_ComputeLandedValue", this="panel")
def gen_landed_value(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    put_entries(env, rng, img, skater + PANEL, rng.choice([0, 1, 2, 5, 10, 14, 15, 16, 20]))
    landing(env, rng, img, skater)
    return [], img.pokes()


@pspec(0x0048D960, "TrickScore_InitEntry", this=lambda env: env.slab + SCRATCH)
def gen_init_entry(env, n):
    rng = env.rng
    img = Image()
    img.put(env.slab + SCRATCH, bytes(rng.randrange(256) for _ in range(0x28)))
    return [rng.getrandbits(32), name_ptr(env, rng), rng.randrange(-5, 5000) & 0xFFFFFFFF], img.pokes()


def refresh_state(env, rng, img, count, kinds=(0, 0, 1, 2), nodes=False):
    skater = env.skater
    panel = skater + PANEL
    put_entries(env, rng, img, panel, count, kinds, nodes)
    landing(env, rng, img, skater)
    panel_scalars(env, rng, img, panel, skater)
    globals_state(env, rng, img)
    effect_objects(env, rng, img, panel)
    img.u32(skater + 0x170, rng.choice([0, 0, 1]))
    return skater, panel


@pspec(0x0048E3B0, "TrickScore_PushEntry", this="panel")
def gen_push_entry(env, n):
    rng = env.rng
    img = Image()
    count = rng.choice([0, 0, 1, 2, 3, 5, 8, 199, 200] if n % 9 == 0 else [0, 0, 1, 2, 3, 5, 8])
    skater, panel = refresh_state(env, rng, img, min(count, 8), kinds=(0, 0, 1, 2))
    if count >= 199:
        # a nearly full stack: only the head and tail are written (the rest is whatever the game holds)
        for i in range(count - 2, count):
            e = entry_at(panel, i)
            img.u32(e + 0x00, name_ptr(env, rng))
            img.u32(e + 0x04, 10)
            img.u32(e + 0x10, rng.choice([0, 1, 2]))
        img.u32(panel + 0x26F0, count)
    points = rng.choice([rng.randrange(1, 3000), 1, 0, 50, rng.randrange(-20, 5)])
    return [name_ptr(env, rng), points & 0xFFFFFFFF, rng.choice([0, 1, 2]), rng.choice([0, 1]), rng.getrandbits(32)], img.pokes()


@pspec(0x0048D710, "TrickScore_AdjustCurrentStackEntry")
def gen_adjust(env, n):
    rng = env.rng
    img = Image()
    skater, panel = refresh_state(env, rng, img, rng.choice([0, 1, 2, 3, 6]))
    return [skater, rng.randrange(-300, 3000) & 0xFFFFFFFF], img.pokes()


@pspec(0x0048E620, "TrickScore_ExpireEntries", this="panel")
def gen_expire(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    panel = skater + PANEL
    put_entries(env, rng, img, panel, rng.choice([0, 1, 3, 6]), nodes=True)
    for i in range(8):
        img.put(env.slab + NODES + 0x20 * i + 6, struct.pack("<H", rng.randrange(0, 40)))
    return [], img.pokes()


def commit_state(env, rng, img, count):
    skater, panel = refresh_state(env, rng, img, count)
    # the strings the best stack copies from
    for i in range(3):
        text = rng.choice(TRICK_NAMES)[:20]
        img.put(panel + 0x26F4 + 0x64 * i, text + b"\0")
    gap = rng.choice([0, 0, env.slab + GAP_RECORD])
    img.u32(skater + 0x3024, gap)
    img.u32(skater + 0x3028, rng.choice([0, 0, env.slab + GAP_RECORD + 0x40]))
    # the level's goal list: ten goals of 0x18 bytes, kind at +0 and a trick name pointer at +8
    five = rng.randrange(0, 13)    # slots 10 and above: no goal of kind 5 (the retail loop runs off the list)
    for g in range(10):
        base = LEVEL_GOALS + 0x18 * g
        img.u32(base + 0, 5 if g == five else rng.choice([1, 2, 7, 3, 0]))
        img.u32(base + 8, rng.choice([0, name_ptr(env, rng), name_ptr(env, rng)]))
    return skater, panel


@pspec(0x0048E680, "TrickScore_CommitLandedStack", this="panel")
def gen_commit(env, n):
    rng = env.rng
    img = Image()
    commit_state(env, rng, img, rng.choice([0, 1, 2, 3, 5, 8, 16]))
    return [], img.pokes()


@pspec(0x0048D7A0, "TrickScore_CommitForSkater")
def gen_commit_skater(env, n):
    rng = env.rng
    img = Image()
    skater, _ = commit_state(env, rng, img, rng.choice([0, 1, 3, 5]))
    return [skater], img.pokes()


@pspec(0x0048E8A0, "TrickScore_RollbackStack", this="panel")
def gen_rollback(env, n):
    rng = env.rng
    img = Image()
    refresh_state(env, rng, img, rng.choice([0, 1, 2, 4, 9]))
    return [], img.pokes()


@pspec(0x0048D7B0, "Panel_Bail")
def gen_bail(env, n):
    rng = env.rng
    img = Image()
    skater, _ = refresh_state(env, rng, img, rng.choice([0, 1, 2, 4]))
    return [skater], img.pokes()


@pspec(0x0048DDC0, "Panel_RefreshTrickText", this="panel")
def gen_refresh(env, n):
    rng = env.rng
    img = Image()
    count = rng.choice([0, 1, 1, 2, 3, 4, 6, 9])
    skater, panel = refresh_state(env, rng, img, count, kinds=rng.choice([(0, 0, 1, 2), (1, 2), (0,), (1,)]))
    img.u32(skater + 0x170, rng.choice([0, 1]))
    img.u32(0x0056A928, rng.choice([0, 1]))
    return [], img.pokes()


@pspec(0x0048E320, "Panel_ShowStackText", this="panel")
def gen_show_text(env, n):
    rng = env.rng
    img = Image()
    refresh_state(env, rng, img, rng.choice([0, 1, 2, 4]))
    return [], img.pokes()


# ---- the skater side (physics.cpp) ---------------------------------------------------------------------


@pspec(0x0048F720, "TrickScore_AdvancePointTimer", this="skater")
def gen_advance(env, n):
    rng = env.rng
    img = Image()
    skater, panel = refresh_state(env, rng, img, rng.choice([0, 1, 2, 3]))
    img.u32(0x0056A93C, rng.choice([0x1000, 0x2000, 0x1800, 0x555]))
    img.u32(skater + 0x2F54, rng.choice([0, 0x1000 * rng.randrange(0, 60), rng.randrange(0, 0x400000)]))
    img.u32(skater + 0x2F58, rng.choice([0, 5, 20, 100, 300]))
    img.u32(skater + 0x50, rng.getrandbits(8))
    img.u16(skater + 0x29F0, rng.randrange(0, 2))
    return [rng.choice([0x1000, 0x800, 0x2000, -0x400 & 0xFFFFFFFF, 0, rng.getrandbits(13)])], img.pokes()


@pspec(0x0048F690, "Gap_ApplyDefinitionState", this="skater")
def gen_apply_gap(env, n):
    """10g s23: a bonus scored mid-trick (+0x3064 open) pushed on the stack; the pending-script clear runs under +0x2dd4."""
    rng = env.rng
    img = Image()
    count = rng.choice([0, 0, 1, 2, 3, 5, 8, 199, 200] if n % 9 == 0 else [0, 0, 1, 2, 3, 5, 8])
    skater, panel = refresh_state(env, rng, img, min(count, 8), kinds=(0, 0, 1, 2))
    if count >= 199:
        for i in range(count - 2, count):
            e = entry_at(panel, i)
            img.u32(e + 0x00, name_ptr(env, rng))
            img.u32(e + 0x04, 10)
            img.u32(e + 0x10, rng.choice([0, 1, 2]))
        img.u32(panel + 0x26F0, count)
    img.u32(skater + 0x3064, rng.choice([1, 1, 1, 0, 2, 0x100, 0xFFFFFFFF]))
    img.u32(skater + 0x2DD4, rng.choice([0, 0, 1, 2, 0xFFFFFF00]))
    img.u32(skater + 0x2CC8, rng.choice([0, 1, rng.getrandbits(32)]))
    img.u32(skater + 0x2F24, rng.getrandbits(32))
    img.u32(skater + 0x29E0, rng.getrandbits(32))
    img.put(skater + 0x2A14, bytes(rng.randrange(256) for _ in range(0x100)))
    value = rng.choice([rng.randrange(1, 3000), 1, 0, 50, 250, 0x7FFFFFFF, 0x80000000, 0x80000001, rng.randrange(-3000, 0), -1, rng.getrandbits(32)])
    return [name_ptr(env, rng), value & 0xFFFFFFFF, rng.choice([0, 1, 2]), rng.choice([0, 1, 0xFFFFFFFF, rng.getrandbits(32)])], img.pokes()


@pspec(0x0048F8A0, "Trick_LandingQuality", this="skater")
def gen_landing_quality(env, n):
    rng = env.rng
    img = Image()
    skater = env.skater
    img.u32(skater + 0x306C, rng.randrange(0, 4))
    img.u32(skater + 0x2DD4, rng.choice([0, 0, 1]))
    img.u32(skater + 0x2C84, rng.choice([0, 0, 1]))
    img.u32(skater + 0x3068, rng.choice([0, 100, -100 & 0xFFFFFFFF, 0x5DC, 0x5DD, -0x5DD & 0xFFFFFFFF, 3000, -3000 & 0xFFFFFFFF, 0x80000000]))
    return [], img.pokes()
