"""Phase 4.10f2 case generators, part 2: the generators of each group (see frontend_specs_eqsel.py for the slab, the seeds and the stubs).

A function that works on an object's own state (destroy, draw, update) is given an object the generator builds by hand: its vtable is the fake page vtable (the
per-frame hook is a reported service), its children are fake pool objects whose virtual methods are reported services, a rack a window calls through its vtable
has the fake rack vtable. A constructor is called on garbage and builds its objects for real in the arena (the window base, the toolkit and the font code are
native); every class another constructor calls is a stub.
"""

from __future__ import annotations

from create_specs import spec
from frontend_specs_eqsel import (CUSTOM_ROWS, DECK_COUNT, DIALOG, DOTS, EQ_CAREER, EQ_CHAR, EQ_IDX, EQ_PLAYER, EQ_SET, EQ_TRUCK, HIGH_RES, ITEM_COUNT, ITEM_STRIDE,
                                  ITEMS, LOADING, LOGOS, NODE_COUNT, NODES, PAD_ALT, PAD_BACK, PAD_CONFIRM, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_SCROLL_DOWN,
                                  PAD_SCROLL_UP, PAD_STRIDE, PAD_TOGGLE, PAD_UP, PICTURES, PLAYER_IDX, PLAYER_SET, PLAYER_TRUCK, RECT, SET_COUNT, SLOT_TABLE,
                                  SPOOL, TRUCK_TEXTS, VT_DIALOG, VT_RACK)
from frontend_specs_menu import STRING_POOL, coords, sprite_at, string_at
from frontend_specs_menu_items import dots_object
from frontend_specs_playsel import PA, PS_ARENA, PS_OBJ, ROWS, real_text_element
from frontend_specs_screens import VT_ANY
from frontend_specs_screens_cases import Pool, common, font_object, window
from frontend_specs_skwin import CAREER, VT_WINDOW, new_image

VT_REAL_EQ = {"window": 0x00518914, "dots": 0x00518934, "cash": 0x00518954, "view": 0x00518974, "name": 0x00518994, "rack": 0x005189B4, "deck": 0x005189C4,
              "truck": 0x005189D8}


def espec(group):
    def make(address, name, this=None):
        resolve = (lambda env: env.slab + this) if this is not None else None
        return spec(address, name, group, this=resolve)
    return make


def obj(env, i=0):
    return PA(env, PS_OBJ[i])


def a32(env, offset):
    return env.slab + offset


class Items:
    """The deck items, spool nodes and logos of one case, handed out once each."""

    def __init__(self, env, img, rng):
        self.env, self.img, self.rng = env, img, rng
        self.next_item = 0
        self.next_node = 0

    def item(self, *, data=None, held=None):
        assert self.next_item < ITEM_COUNT
        base = self.env.slab + ITEMS + ITEM_STRIDE * self.next_item
        self.next_item += 1
        rng, img = self.rng, self.img
        img.garbage(base, ITEM_STRIDE, rng)
        img.u8(base, rng.choice([0, 1, 2, 3, 4, 0xFF, 0xFE, 5, 7]))
        img.u8(base + 1, rng.choice([0, 0, 1, 2, 3, 0xFF, 4]))
        if data is None:
            data = rng.choice([0, 0, PA(self.env, PS_ARENA + 0x10 * rng.randrange(0x100, 0xE00))])
        img.u32(base + 4, data)
        img.put(base + 8, (b"s2dBi%02da.bmp" % rng.randrange(1, 9)) + b"\0")
        img.u8(base + 0x18, 1 if data != 0 else rng.choice([0, 0, 1]))
        img.u8(base + 0x19, rng.choice([0, 0, 1]) if held is None else held)
        for k in (0x1A, 0x1B, 0x1C, 0x1D):
            img.u8(base + k, rng.choice([0, 1]))
        img.u32(base + 0x20, PA(self.env, PICTURES) + 0x40 * rng.randrange(4))
        img.u32(base + 0x24, rng.randrange(0, 400))
        return base

    def node(self, item, following=0):
        assert self.next_node < NODE_COUNT
        base = self.env.slab + NODES + 8 * self.next_node
        self.next_node += 1
        self.img.u32(base, item)
        self.img.u32(base + 4, following)
        return base


# ---- shared state ----------------------------------------------------------------------------------------------------------------------------------------

def eq_world(env, img, rng, *, character=None, player=None, career=None, set_=None, idx=None, owns_something=True, custom=None):
    """The globals of the screen, the characters' career records (cash and owned decks) and the player's choice."""
    player = rng.choice([0, 1]) if player is None else player
    character = rng.randrange(20) if character is None else character
    career = rng.choice([0, 1, 1]) if career is None else career
    img.u8(EQ_CAREER, career)
    img.u32(EQ_CHAR, character)
    img.u32(EQ_PLAYER, player)
    set_ = rng.choice([character, character, rng.randrange(20)]) if set_ is None else set_
    idx = rng.choice([rng.randrange(8), rng.randrange(8), rng.randrange(8), rng.randrange(20)]) if idx is None else idx
    img.u32(EQ_SET, set_)
    img.u32(EQ_IDX, idx)
    img.u32(EQ_TRUCK, rng.randrange(3))
    for i in range(3):
        img.u32(TRUCK_TEXTS + 4 * i, string_at(env, rng.randrange(0, 16)))
    for c in range(20):
        record = CAREER + 0x104 * c
        img.u32(record + 8, rng.choice([0, 100, 250, 1000, 5000, rng.randrange(0, 30000), 99999]))
        mask = rng.choice([0, 1, 0xFF, rng.getrandbits(32), rng.getrandbits(8), 0xFFFFF, (1 << rng.randrange(8)) | rng.getrandbits(8)])
        if c == character and owns_something:
            mask |= 1 << rng.randrange(8)
        img.u32(record + 0x34, mask)
    # the character rows' flags: 2 custom skater, 0x20 / 0x02 no logo
    for c in range(20):
        flags = 0x2 if c in CUSTOM_ROWS else rng.choice([0, 0, 0, 0x20, 0x100, 0x22])
        if custom is not None and c == character:
            flags = 0x2 if custom else rng.choice([0, 0, 0x20])
        img.u32(ROWS + 0x170 * c + 0x15C, flags)
    img.u8(PLAYER_SET + player, set_)
    img.u8(PLAYER_IDX + player, idx)
    img.u8(PLAYER_TRUCK + player, rng.randrange(3))
    env.dialog_result = rng.choice([0, 0, 1, 2, 3])
    return {"character": character, "player": player, "career": career, "set": set_, "idx": idx}


def pad_edges(img, rng, player, only=None, chance=0.12):
    names = {"left": PAD_LEFT, "right": PAD_RIGHT, "up": PAD_UP, "down": PAD_DOWN, "toggle": PAD_TOGGLE, "confirm": PAD_CONFIRM, "alt": PAD_ALT, "back": PAD_BACK}
    for name, address in names.items():
        value = 1 if name == only else (1 if (only is None and rng.random() < 0.3) or rng.random() < chance else 0)
        img.u8(address + PAD_STRIDE * player, value)
    img.u8(PAD_SCROLL_UP, rng.choice([0, 0, 0, 1]))
    img.u8(PAD_SCROLL_DOWN, rng.choice([0, 0, 0, 1]))


def rack_base(img, rng, base, *, vtable, single=None, active=None, state=None, size=0x20):
    img.garbage(base, size, rng)
    img.u32(base, vtable)
    img.u8(base + 4, rng.choice([0, 0, 1]) if single is None else single)
    img.u8(base + 6, rng.choice([0, 1]) if active is None else active)
    st = rng.choice([0, 1, 1, 2, 3]) if state is None else state
    img.u32(base + 8, st)
    img.u32(base + 0xC, rng.choice([0, 600, -600 & 0xFFFFFFFF, rng.randrange(-700, 700) & 0xFFFFFFFF, 0x37, 0x38, 0x70]))
    img.u32(base + 0x10, rng.choice([0, 0, 0xFFFFFDA8, rng.randrange(-700, 700) & 0xFFFFFFFF, 0x20]))
    return st


def slot_angles(env):
    return [env.u32(SLOT_TABLE + 8 * k) for k in range(5)]


def spool_state(env, img, rng, items, item_addresses, *, base=None, state=None):
    """A spool queue: nodes over some of the items, the node being loaded (state 1 and 2), the buffer."""
    base = env.slab + SPOOL if base is None else base
    state = rng.choice([0, 0, 1, 1, 2]) if state is None else state
    count = rng.randrange(0, min(7, len(item_addresses)) + 1)
    chosen = rng.sample(item_addresses, count)
    head = 0
    nodes = []
    for item in reversed(chosen):
        head = items.node(item, head)
        nodes.append(head)
    nodes.reverse()
    img.garbage(base, 0x18, rng)
    img.u32(base, rng.choice([0x18, 0x18, 0, 1, 2, 5]))
    img.u32(base + 4, head)
    current = 0
    if state != 0:
        current = nodes[rng.randrange(len(nodes))] if nodes and rng.random() < 0.85 else items.node(rng.choice(item_addresses), 0)
    img.u32(base + 8, current)
    img.u32(base + 0xC, rng.choice([0] + nodes) if nodes else 0)
    img.u32(base + 0x10, 0 if state == 0 or rng.random() < 0.5 else PA(env, PS_ARENA + 0x10 * rng.randrange(0x100, 0xE00)))
    img.u32(base + 0x14, state)
    return base


def deck_rack_state(env, img, rng, base, pool, items, *, vtable, nsets=None, state=None, with_spool=True, destroy=False):
    """A deck rack in a legal state: its sets (a character, a deck count, a remembered deck, the deck items), the angles, the heights, the spool."""
    img.garbage(base, 0x6E0, rng)
    st = rack_base(img, rng, base, vtable=vtable, state=state, size=0x20)
    img.u8(base + 0x14, rng.choice([0, 0, 0, 1]))
    for i in range(3):
        img.u32(base + 0x18 + 4 * i, pool.take())
    img.u32(base + 0x24, PA(env, PICTURES) + 0x100 * rng.randrange(4))
    img.u32(base + 0x28, rng.randrange(5))
    front = rng.randrange(5)
    img.u32(base + 0x2C, front)
    angles = slot_angles(env)
    near = angles[front]
    angle = rng.choice([near, near, near + rng.randrange(-0x90, 0x90), rng.randrange(0, 0x1000), near - 0x1000])
    img.u32(base + 0x34, angle & 0xFFFFFFFF)
    target = rng.choice([angle, angle, angle + 0x40, angle - 0x40, angle + 0x100, angle - 0x100, angle + 0x500, angle - 0x500, near, near + 0x1000])
    img.u32(base + 0x30, target & 0xFFFFFFFF)
    img.u32(base + 0x38, rng.choice([0x333, 0x333, 0x333, rng.randrange(0x200, 0x400)]))
    img.u32(base + 0x3C, rng.randrange(0, 8))
    n = rng.choice([1, 1, 2, 3]) if nsets is None else nsets
    characters = rng.sample(range(20), n)
    item_addresses = []
    for s in range(n):
        record = base + 0x1BC + 0x5C * s
        count = rng.choice([8, 8, 8, rng.randrange(1, 21)])
        img.u32(record, characters[s])
        img.u32(record + 4, count)
        img.u32(record + 8, rng.randrange(0, count))
        for i in range(count):
            item = items.item()
            item_addresses.append(item)
            img.u32(record + 0xC + 4 * i, item)
    current = rng.randrange(n)
    img.u32(base + 0x6C4, n)
    img.u32(base + 0x6C8, rng.randrange(n))
    img.u32(base + 0x6CC, current)
    y = 2000 * current
    img.u32(base + 0x6D4, rng.choice([y, y, y + rng.randrange(-300, 300), rng.randrange(-1500, 2000 * n + 1500), -1000 - rng.randrange(0, 200)]) & 0xFFFFFFFF)
    img.u32(base + 0x6D0, rng.choice([y, y, y + 2000, y - 2000, rng.randrange(-1500, 2000 * n + 1500)]) & 0xFFFFFFFF)
    spool = spool_state(env, img, rng, items, item_addresses) if with_spool else 0
    img.u32(base + 0x6D8, spool if not destroy or rng.random() > 0.1 else 0)
    img.u32(base + 0x6DC, rng.randrange(0, rng.choice([8, 8, 20]) if n else 8))
    return {"sets": n, "items": item_addresses, "spool": spool}


def truck_rack_state(env, img, rng, base, *, vtable, state=None):
    img.garbage(base, 0x2F8, rng)
    rack_base(img, rng, base, vtable=vtable, single=1, state=state)
    font_object(env, img, rng, base + 0x14)
    img.u32(base + 0x160, rng.randrange(3))
    img.u32(base + 0x164, rng.randrange(3))
    img.u32(base + 0x168, rng.choice([0, 1, 2, 3, 4, 4, 5]) if state is None else state)
    img.u32(base + 0x16C, rng.choice([-0x4B, -0x4A, -0x4B, 0, 3, 4, 5, -20, 10]) & 0xFFFFFFFF)
    img.u32(base + 0x170, rng.choice([0, 1, -1, 0x7F0, -0x800, rng.randrange(-0x800, 0x800), 0x10, 0x20]) & 0xFFFFFFFF)
    img.u32(base + 0x174, rng.choice([0, 0x9F, -0xA0, 0x7F0, 0x1000, -0x1000, rng.randrange(-0x2000, 0x2000)]) & 0xFFFFFFFF)
    img.u32(base + 0x178, rng.randrange(0, 8))


def view_state(env, img, rng, pool, items, base=None, *, rack_vtable=None, page=None, deck_state=None):
    """An EquipView: the page, a deck rack (legal) and a truck rack, four arrows. The racks have the fake rack vtable (the view calls them through it)."""
    base = obj(env, 1) if base is None else base
    img.garbage(base, 0x50, rng)
    img.u32(base, VT_REAL_EQ["view"])
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x20, rng.randrange(-20, 400) & 0xFFFFFFFF)
    img.u32(base + 0x24, rng.randrange(-20, 300) & 0xFFFFFFFF)
    img.u32(base + 0x28, rng.choice([0, 1]) if page is None else page)
    deck = obj(env, 2)
    truck = obj(env, 3)
    info = deck_rack_state(env, img, rng, deck, pool, items, vtable=a32(env, VT_RACK) if rack_vtable is None else rack_vtable, state=deck_state)
    truck_rack_state(env, img, rng, truck, vtable=a32(env, VT_RACK))
    img.u32(base + 0x2C, deck)
    img.u32(base + 0x30, truck)
    img.u32(base + 0x34, rng.choice([250, 250, rng.randrange(20, 400)]))
    img.u32(base + 0x38, rng.choice([180, 180, rng.randrange(20, 300)]))
    for i in range(4):
        img.u32(base + 0x3C + 4 * i, pool.take())
    img.u32(base + 0x4C, rng.choice([0, 1, 5, 10, 11, 20, 40, 0x28, 39]))
    return base, info


def name_state(env, img, rng, pool, base=None, *, destroy=False, text_index=30):
    """A NameWindow: the text element (real, with a string and a font), its font, the deck asked for and the one shown."""
    base = obj(env, 4) if base is None else base
    window(env, img, rng, base, 0x21C, pool, children=1 if not destroy else rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    text = real_text_element(env, img, rng, 8)
    img.u8(text + 0x1C, rng.choice([0, 1]))
    img.u32(text + 8, rng.randrange(-300, 300) & 0xFFFFFFFF)
    img.u32(base + 0xAC, text)
    font_object(env, img, rng, base + 0xB0)
    img.u32(base + 0x1FC, rng.randrange(-300, 300) & 0xFFFFFFFF)
    img.u32(base + 0x204, rng.randrange(0, 20))
    img.u32(base + 0x208, rng.randrange(0, 8))
    img.u32(base + 0x20C, rng.choice([0xFFFFFFFF, rng.randrange(0, 20)]))
    img.u32(base + 0x210, rng.choice([0xFFFFFFFF, rng.randrange(0, 8)]))
    img.u32(base + 0x214, rng.choice([0, 1, 3, 3, 0, 1, 2]))
    return base


def cash_state(env, img, rng, pool, base=None, *, destroy=False):
    base = obj(env, 5) if base is None else base
    window(env, img, rng, base, 0x36C, pool, children=0 if not destroy else rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    font_object(env, img, rng, base + 0xAC)
    font_object(env, img, rng, base + 0x1F8)
    img.u32(base + 0x344, rng.randrange(0, 20))
    img.u32(base + 0x348, rng.randrange(0, 8))
    img.u32(base + 0x34C, rng.choice([0, 100, 5000, rng.randrange(0, 30000), 0xFFFFFFFF]))
    img.u32(base + 0x350, rng.randrange(-100, 200) & 0xFFFFFFFF)
    img.u8(base + 0x354, rng.choice([0, 1]))
    for k in range(0x358, 0x36C, 4):
        img.u32(base + k, pool.take())
    return base


def dialog_object(env, img, rng):
    base = a32(env, DIALOG)
    img.u32(base, a32(env, VT_DIALOG))
    return base


def eq_window_state(env, img, rng, pool, items, *, destroy=False, dialog=None, page=None, view_page=None, deck_state=None):
    """The equipment window with everything it holds: the view and its racks, the name and cash windows, six prompts, the logos, the dialog."""
    base = obj(env, 0)
    window(env, img, rng, base, 0x3F4, pool, children=rng.randrange(0, 3))
    img.u32(base, PA(env, VT_WINDOW))
    keep = (lambda value: 0 if rng.random() < 0.12 else value) if destroy else (lambda value: value)
    font_object(env, img, rng, base + 0xC8)
    font_object(env, img, rng, base + 0x214)
    img.u32(base + 0xAC, rng.choice([0, 1]) if page is None else page)
    view, info = view_state(env, img, rng, pool, items, page=view_page, deck_state=deck_state)
    img.u32(base + 0xB0, view)
    img.u32(base + 0xB4, name_state(env, img, rng, pool))
    img.u32(base + 0xB8, cash_state(env, img, rng, pool))
    dialog = rng.choice([False, False, True]) if dialog is None else dialog
    img.u32(base + 0xBC, 1 if dialog else rng.choice([0, 0, 0]))
    img.u8(base + 0xC0, rng.choice([0, 0, 0, 1]))
    img.u32(base + 0xC4, dialog_object(env, img, rng) if dialog or rng.random() < 0.2 else 0)
    img.u32(base + 0x360, keep(pool.take()))
    for i in range(6):
        img.u32(base + 0x364 + 4 * i, pool.take())
    for i in range(20):
        logo = a32(env, LOGOS + 0x40 * i)
        img.put(logo, bytes(0x40))
        img.u32(base + 0x37C + 4 * i, 0 if rng.random() < 0.3 else logo)
    return base, info


# ---- the deck helpers ----------------------------------------------------------------------------------------------------------------------------------

H = espec("eqh")


@H(0x00444510, "Equip_ColoursMatch")
def gen_colours_match(env, n):
    img, rng = new_image(env)
    pair = lambda: rng.choice([0, 0x12, 0x21, 0x34, 0x43, rng.randrange(0, 256), rng.randrange(-128, 128) & 0xFFFFFFFF])
    first = pair()
    second = rng.choice([pair(), ((first & 0xF) << 4) | ((first >> 4) & 0xF), 0])
    return [first, second], img.pokes()


@H(0x00444550, "EquipSelect_NoOp")
def gen_no_op(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@H(0x00444560, "EquipSelect_InitCallbacks")
def gen_init_callbacks(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env, 1), 0x20, rng)
    return [obj(env, 1)], img.pokes()


@H(0x00444580, "EquipSelect_ExitCallback")
def gen_exit_callback(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@H(0x00444810, "Equip_DeckPrice")
def gen_deck_price(env, n):
    img, rng = new_image(env)
    world = eq_world(env, img, rng)
    return [rng.choice([world["character"], rng.randrange(20)]), rng.choice([rng.randrange(8), rng.randrange(20)])], img.pokes()


@H(0x00444880, "Equip_BuyDeck")
def gen_buy_deck(env, n):
    img, rng = new_image(env)
    world = eq_world(env, img, rng)
    return [rng.choice([world["character"], rng.randrange(20)]), rng.choice([rng.randrange(8), rng.randrange(20)])], img.pokes()


@H(0x004448C0, "Equip_OwnsDeck")
def gen_owns_deck(env, n):
    img, rng = new_image(env)
    eq_world(env, img, rng)
    return [rng.randrange(20), rng.choice([rng.randrange(8), rng.randrange(32), 31])], img.pokes()


@H(0x004448F0, "Equip_IsCurrentCharacter")
def gen_is_current(env, n):
    img, rng = new_image(env)
    world = eq_world(env, img, rng)
    return [rng.choice([world["character"], rng.randrange(20)])], img.pokes()


@H(0x00444900, "Equip_CanAfford")
def gen_can_afford(env, n):
    img, rng = new_image(env)
    world = eq_world(env, img, rng)
    return [rng.choice([world["character"], world["character"], rng.randrange(20)]), rng.choice([rng.randrange(8), rng.randrange(20)])], img.pokes()


@H(0x00444940, "Equip_AllocZeroed")
def gen_alloc_zeroed(env, n):
    img, rng = new_image(env)
    return [rng.choice([0, 1, 3, 4, 0x18, 0x28, 0x100, 0x2F8, 0x6E0])], img.pokes()


@H(0x00444970, "Equip_Free")
def gen_free(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32)], img.pokes()


@H(0x00445810, "Equip_DeckStat")
def gen_deck_stat(env, n):
    img, rng = new_image(env)
    eq_world(env, img, rng)
    return [rng.choice([0, 1, 2, 3, 4, 4, 5, 9, rng.randrange(0, 8)]), rng.randrange(20), rng.choice([rng.randrange(8), rng.randrange(20)])], img.pokes()


@H(0x004458B0, "Equip_CountOwnedDecks")
def gen_count_owned(env, n):
    img, rng = new_image(env)
    eq_world(env, img, rng, owns_something=rng.random() > 0.15)
    return [], img.pokes()


@H(0x004468A0, "Equip_CharacterShortName")
def gen_short_name(env, n):
    img, rng = new_image(env)
    img.garbage(a32(env, 0x24800), 0x40, rng)
    return [rng.randrange(20), a32(env, 0x24800)], img.pokes()


@H(0x004468E0, "RackBase_Init", this=PS_OBJ[0])
def gen_rack_init(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), 0x20, rng)
    return [], img.pokes()


@H(0x00446900, "RackBase_Slot2")
def gen_rack_slot2(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32), rng.getrandbits(32)], img.pokes()


@H(0x00446910, "RackBase_Slot3")
def gen_rack_slot3(env, n):
    img, rng = new_image(env)
    return [rng.getrandbits(32)], img.pokes()


@H(0x00446920, "RackBase_ScalarDelete", this=PS_OBJ[0])
def gen_rack_delete(env, n):
    img, rng = new_image(env)
    rack_base(img, rng, obj(env), vtable=VT_REAL_EQ["rack"])
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@H(0x00446940, "RackBase_Destroy", this=PS_OBJ[0])
def gen_rack_destroy(env, n):
    img, rng = new_image(env)
    rack_base(img, rng, obj(env), vtable=VT_REAL_EQ["deck"])
    return [], img.pokes()


@H(0x00446950, "RackBase_SetActive", this=PS_OBJ[0])
def gen_rack_set_active(env, n):
    img, rng = new_image(env)
    rack_base(img, rng, obj(env), vtable=VT_REAL_EQ["deck"])
    return [rng.choice([0, 1, 0x100, 0xFF, rng.getrandbits(32)])], img.pokes()


@H(0x00446990, "RackBase_Step", this=PS_OBJ[0])
def gen_rack_step(env, n):
    img, rng = new_image(env)
    rack_base(img, rng, obj(env), vtable=VT_REAL_EQ["deck"], active=rng.choice([0, 1]))
    if rng.random() < 0.5:
        img.u32(obj(env) + 0xC, rng.choice([0x38, 0x37, 0x36, 0x1, 600, 0, -0x100 & 0xFFFFFFFF]))
        img.u32(obj(env) + 0x10, rng.choice([0, 0, 0xFFFFFDA8, 0x10]))
    return [], img.pokes()


# ---- the window ----------------------------------------------------------------------------------------------------------------------------------------

W = espec("eqwin")


@W(0x00444980, "EquipWindow_Construct", this=PS_OBJ[0])
def gen_window_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    img.garbage(obj(env), 0x3F4, rng)
    return [], img.pokes()


@W(0x00444FF0, "EquipWindow_ScalarDelete", this=PS_OBJ[0])
def gen_window_delete(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    eq_window_state(env, img, rng, pool, Items(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@W(0x00445010, "EquipWindow_Destroy", this=PS_OBJ[0])
def gen_window_destroy(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    eq_window_state(env, img, rng, pool, Items(env, img, rng), destroy=True)
    return [], img.pokes()


@W(0x004450A0, "EquipWindow_Draw", this=PS_OBJ[0])
def gen_window_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    pool = Pool(env, img, rng)
    eq_window_state(env, img, rng, pool, Items(env, img, rng))
    return [], img.pokes()


@W(0x00445540, "EquipWindow_Slot4")
def gen_window_slot4(env, n):
    img, rng = new_image(env)
    return [], img.pokes()


@W(0x00445550, "EquipWindow_Step", this=PS_OBJ[0])
def gen_window_step(env, n):
    img, rng = new_image(env)
    world = eq_world(env, img, rng)
    pad_edges(img, rng, world["player"], only=rng.choice(["back", None, "back"]))
    pool = Pool(env, img, rng)
    eq_window_state(env, img, rng, pool, Items(env, img, rng))
    img.garbage(a32(env, 0x24800), 8, rng)
    return [a32(env, 0x24800)], img.pokes()


@W(0x004450E0, "EquipWindow_Update", this=PS_OBJ[0])
def gen_window_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    world = eq_world(env, img, rng)
    pad_edges(img, rng, world["player"], only=rng.choice(["left", "right", "up", "down", "toggle", "confirm", "confirm", "alt", "back", None, None]))
    pool = Pool(env, img, rng)
    items = Items(env, img, rng)
    page = rng.choice([0, 0, 1])
    eq_window_state(env, img, rng, pool, items, page=page, view_page=page)
    img.u32(HIGH_RES, rng.choice([0, 1]))
    return [], img.pokes()


# ---- the stat dots, the cash window, the name window ---------------------------------------------------------------------------------------------------------

P = espec("eqpan")


def stat_dots_state(env, img, rng, pool, base=None, *, destroy=False, real=True):
    base = obj(env, 0) if base is None else base
    img.garbage(base, 0x48, rng)
    img.u32(base, VT_REAL_EQ["dots"])
    img.u32(base + 0x14, rng.choice([0, 0, 1]))
    img.u32(base + 0x20, rng.randrange(-20, 300) & 0xFFFFFFFF)
    img.u32(base + 0x24, rng.randrange(-20, 300) & 0xFFFFFFFF)
    for i in range(4):
        if real and not destroy:
            dots = a32(env, DOTS + 0x20 * i)
            dots_object(env, img, rng, dots)
            img.u32(dots + 0xC, 5)
            img.u32(dots + 0x8, rng.randrange(0, 6))
            img.u32(dots + 0x4, rng.randrange(0, 6))
            img.u32(base + 0x28 + 4 * i, dots)
        else:
            img.u32(base + 0x28 + 4 * i, 0 if destroy and rng.random() < 0.15 else pool.take())
    img.u32(base + 0x38, 0 if destroy and rng.random() < 0.15 else pool.take())
    return base


@P(0x004455C0, "StatDots_Construct", this=PS_OBJ[0])
def gen_dots_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0x48, rng)
    for k in range(8):
        img.put(sprite_at(env, k) + 4, bytes(rng.randrange(256) for _ in range(0x20)))
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@P(0x004456A0, "StatDots_ScalarDelete", this=PS_OBJ[0])
def gen_dots_delete(env, n):
    img, rng = new_image(env)
    stat_dots_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@P(0x004456C0, "StatDots_Destroy", this=PS_OBJ[0])
def gen_dots_destroy(env, n):
    img, rng = new_image(env)
    stat_dots_state(env, img, rng, Pool(env, img, rng), destroy=True)
    return [], img.pokes()


@P(0x00445740, "StatDots_Draw", this=PS_OBJ[0])
def gen_dots_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    stat_dots_state(env, img, rng, Pool(env, img, rng))
    return [coords(rng), coords(rng)], img.pokes()


@P(0x00445780, "StatDots_Update", this=PS_OBJ[0])
def gen_dots_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng, owns_something=rng.random() > 0.1)
    stat_dots_state(env, img, rng, Pool(env, img, rng))
    return [], img.pokes()


@P(0x00445920, "CashWindow_Construct", this=PS_OBJ[0])
def gen_cash_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    img.garbage(obj(env), 0x36C, rng)
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@P(0x00445CF0, "CashWindow_ScalarDelete", this=PS_OBJ[0])
def gen_cash_delete(env, n):
    img, rng = new_image(env)
    cash_state(env, img, rng, Pool(env, img, rng), obj(env), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@P(0x00445D10, "CashWindow_Destroy", this=PS_OBJ[0])
def gen_cash_destroy(env, n):
    img, rng = new_image(env)
    cash_state(env, img, rng, Pool(env, img, rng), obj(env), destroy=True)
    return [], img.pokes()


@P(0x00445D80, "CashWindow_Update", this=PS_OBJ[0])
def gen_cash_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    world = eq_world(env, img, rng)
    base = cash_state(env, img, rng, Pool(env, img, rng), obj(env))
    if rng.random() < 0.4:
        # the case where nothing changed
        img.u32(base + 0x344, world["set"])
        img.u32(base + 0x348, world["idx"])
        img.u32(base + 0x34C, env.s32(CAREER + 0x104 * world["character"] + 8) if rng.random() < 0.5 else rng.getrandbits(32))
    return [], img.pokes()


@P(0x00445E70, "CashWindow_SetVisible", this=PS_OBJ[0])
def gen_cash_set_visible(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), 0x36C, rng)
    return [rng.choice([0, 1, 2, 0x100, 0xFF01, rng.getrandbits(32)])], img.pokes()


@P(0x00446490, "NameWindow_Construct", this=PS_OBJ[0])
def gen_name_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    img.garbage(obj(env), 0x21C, rng)
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@P(0x00446650, "NameWindow_ScalarDelete", this=PS_OBJ[0])
def gen_name_delete(env, n):
    img, rng = new_image(env)
    name_state(env, img, rng, Pool(env, img, rng), obj(env), destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@P(0x00446670, "NameWindow_Destroy", this=PS_OBJ[0])
def gen_name_destroy(env, n):
    img, rng = new_image(env)
    name_state(env, img, rng, Pool(env, img, rng), obj(env), destroy=True)
    return [], img.pokes()


@P(0x004466D0, "NameWindow_Update", this=PS_OBJ[0])
def gen_name_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    world = eq_world(env, img, rng)
    base = name_state(env, img, rng, Pool(env, img, rng), obj(env))
    s = rng.choice([world["set"], rng.randrange(20)])
    d = rng.choice([world["idx"] % 8, rng.randrange(8)])
    if rng.random() < 0.4:
        img.u32(base + 0x20C, s)
        img.u32(base + 0x210, d)
    return [s, d], img.pokes()


# ---- the view -------------------------------------------------------------------------------------------------------------------------------------------

V = espec("eqview")


@V(0x00445EA0, "EquipView_Construct", this=PS_OBJ[1])
def gen_view_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    img.garbage(obj(env, 1), 0x50, rng)
    return [coords(rng), coords(rng), rng.choice([-1, coords(rng)]), rng.choice([-1, coords(rng)])], img.pokes()


@V(0x004460A0, "EquipView_ScalarDelete", this=PS_OBJ[1])
def gen_view_delete(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    base, _ = view_state(env, img, rng, pool, Items(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@V(0x004460C0, "EquipView_Destroy", this=PS_OBJ[1])
def gen_view_destroy(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    base, _ = view_state(env, img, rng, pool, Items(env, img, rng))
    return [], img.pokes()


@V(0x00446150, "EquipView_Update", this=PS_OBJ[1])
def gen_view_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    view_state(env, img, rng, pool, Items(env, img, rng))
    return [], img.pokes()


@V(0x00446180, "EquipView_Draw", this=PS_OBJ[1])
def gen_view_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    pool = Pool(env, img, rng)
    view_state(env, img, rng, pool, Items(env, img, rng))
    img.u32(HIGH_RES, rng.choice([0, 1]))
    img.put(a32(env, RECT), bytes(rng.randrange(256) for _ in range(16)))
    return [coords(rng), coords(rng)], img.pokes()


@V(0x00446420, "EquipView_Move", this=PS_OBJ[1])
def gen_view_move(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    view_state(env, img, rng, pool, Items(env, img, rng))
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@V(0x00446440, "EquipView_SetPage", this=PS_OBJ[1])
def gen_view_set_page(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    view_state(env, img, rng, pool, Items(env, img, rng))
    return [rng.choice([0, 1, 1, 0, 2])], img.pokes()


@V(0x00446470, "EquipView_ClearFlag", this=PS_OBJ[1])
def gen_view_clear_flag(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    _, info = view_state(env, img, rng, pool, Items(env, img, rng))
    return [rng.randrange(20), rng.randrange(0, 8)], img.pokes()


# ---- the racks, the deck pictures and the spool queue -------------------------------------------------------------------------------------------------------

R = espec("eqrack")


@R(0x004469F0, "DeckRack_Construct", this=PS_OBJ[0])
def gen_deck_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng, custom=rng.choice([False, False, True]) if rng.random() < 0.2 else None)
    img.garbage(obj(env), 0x6E0, rng)
    return [], img.pokes()


@R(0x00446C20, "DeckRack_ScalarDelete", this=PS_OBJ[0])
def gen_deck_delete(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"], destroy=True)
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@R(0x00446C40, "DeckRack_Destroy", this=PS_OBJ[0])
def gen_deck_destroy(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"], destroy=True)
    return [], img.pokes()


@R(0x00446D80, "DeckRack_BuildSets", this=PS_OBJ[0])
def gen_deck_build_sets(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng, custom=rng.choice([False, False, True]) if rng.random() < 0.25 else None)
    img.garbage(obj(env), 0x6E0, rng)
    img.u32(obj(env) + 0x24, PA(env, PICTURES))
    return [], img.pokes()


@R(0x00446FB0, "DeckRack_Update", this=PS_OBJ[0])
def gen_deck_update(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    pad_edges(img, rng, 0)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"])
    img.u32(LOADING, rng.choice([0, 0, 1]))
    return [], img.pokes()


@R(0x00447340, "DeckRack_Draw", this=PS_OBJ[0])
def gen_deck_draw(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"], state=1)
    if rng.random() < 0.7:
        # a settled rack: the shown angle and height are the targets
        img.u32(obj(env) + 0x34, 0x334)
        img.u32(obj(env) + 0x30, 0x334)
        y = rng.choice([0, 2000])
        img.u32(obj(env) + 0x6D0, y)
        img.u32(obj(env) + 0x6D4, y)
    return [coords(rng), coords(rng)], img.pokes()


@R(0x00447440, "DeckRack_Move", this=PS_OBJ[0])
def gen_deck_move(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"])
    return [rng.choice([0, 1, 2, 3, 4])], img.pokes()


@R(0x004474B0, "DeckRack_Turn", this=PS_OBJ[0])
def gen_deck_turn(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"])
    if rng.random() < 0.1:
        img.u32(obj(env) + 0x28, rng.choice([4, 0, 0xFFFFFFFF, 5, 7]))
    return [rng.choice([0, 1, 2, 3, 0xFFFFFFFF, 4])], img.pokes()


@R(0x00447730, "DeckRack_Scroll", this=PS_OBJ[0])
def gen_deck_scroll(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    pool = Pool(env, img, rng)
    deck_rack_state(env, img, rng, obj(env), pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"])
    return [rng.choice([0, 1, -1 & 0xFFFFFFFF, 0, 1, 2, -2 & 0xFFFFFFFF])], img.pokes()


@R(0x00447A90, "DeckRack_ClearFlag", this=PS_OBJ[0])
def gen_deck_clear_flag(env, n):
    img, rng = new_image(env)
    pool = Pool(env, img, rng)
    base = obj(env)
    deck_rack_state(env, img, rng, base, pool, Items(env, img, rng), vtable=VT_REAL_EQ["deck"])
    return [rng.randrange(20), rng.randrange(0, 8)], img.pokes()


@R(0x00448230, "TruckRack_Construct", this=PS_OBJ[0])
def gen_truck_construct(env, n):
    img, rng = new_image(env)
    common(env, img, rng)
    eq_world(env, img, rng)
    img.garbage(obj(env), 0x2F8, rng)
    return [], img.pokes()


@R(0x00448480, "TruckRack_ScalarDelete", this=PS_OBJ[0])
def gen_truck_delete(env, n):
    img, rng = new_image(env)
    truck_rack_state(env, img, rng, obj(env), vtable=VT_REAL_EQ["truck"])
    return [rng.choice([0, 1, 2, 3])], img.pokes()


@R(0x004484A0, "TruckRack_Destroy", this=PS_OBJ[0])
def gen_truck_destroy(env, n):
    img, rng = new_image(env)
    truck_rack_state(env, img, rng, obj(env), vtable=VT_REAL_EQ["truck"])
    return [], img.pokes()


@R(0x00448540, "TruckRack_Update", this=PS_OBJ[0])
def gen_truck_update(env, n):
    img, rng = new_image(env)
    eq_world(env, img, rng)
    truck_rack_state(env, img, rng, obj(env), vtable=VT_REAL_EQ["truck"])
    return [], img.pokes()


@R(0x00448740, "TruckRack_Draw", this=PS_OBJ[0])
def gen_truck_draw(env, n):
    img, rng = new_image(env)
    eq_world(env, img, rng)
    truck_rack_state(env, img, rng, obj(env), vtable=VT_REAL_EQ["truck"])
    img.u32(obj(env) + 8, rng.choice([1, 1, 0, 2, 3]))
    return [coords(rng), coords(rng)], img.pokes()


@R(0x00448790, "TruckRack_Move", this=PS_OBJ[0])
def gen_truck_move(env, n):
    img, rng = new_image(env)
    truck_rack_state(env, img, rng, obj(env), vtable=VT_REAL_EQ["truck"])
    return [rng.choice([0, 2, 0, 2, 1, 3, 4])], img.pokes()


def one_item(env, img, rng, base=None, **kw):
    items = Items(env, img, rng)
    address = items.item(**kw)
    return address, items


@R(0x00447AE0, "DeckItem_Construct", this=PS_OBJ[0])
def gen_item_construct(env, n):
    img, rng = new_image(env)
    img.garbage(obj(env), ITEM_STRIDE, rng)
    return [string_at(env, rng.randrange(0, 16)), rng.choice([0, PA(env, PICTURES), PA(env, PICTURES) + 0x40]), rng.choice([0, 1, 0x100, 0xFF01, rng.getrandbits(32)])], img.pokes()


def item_at_object(env, img, rng, **kw):
    """A deck item at the object slot (the region the item generators use is the slab's item area: the function is called with `this` = an item)."""
    items = Items(env, img, rng)
    address = items.item(**kw)
    return address


def item_spec(address, name, group="eqrack"):
    def make(fn):
        spec(address, name, group, this=lambda env: env.slab + ITEMS)(fn)
        return fn
    return make


@item_spec(0x00447B70, "DeckItem_FreeThunk")
def gen_item_free_thunk(env, n):
    img, rng = new_image(env)
    item_at_object(env, img, rng)
    return [], img.pokes()


@item_spec(0x00447B80, "DeckItem_SetData")
def gen_item_set_data(env, n):
    img, rng = new_image(env)
    item_at_object(env, img, rng, data=rng.choice([0, 0, PA(env, PS_ARENA + 0x2000)]))
    if rng.random() < 0.7:
        img.u8(env.slab + ITEMS + 0x18, 0)
    return [PA(env, PS_ARENA + 0x10 * rng.randrange(0x100, 0xE00))], img.pokes()


@item_spec(0x00447BE0, "DeckItem_Free")
def gen_item_free(env, n):
    img, rng = new_image(env)
    item_at_object(env, img, rng)
    return [], img.pokes()


@item_spec(0x00447C00, "DeckItem_SetWanted")
def gen_item_set_wanted(env, n):
    img, rng = new_image(env)
    item_at_object(env, img, rng)
    return [rng.choice([0, 1, 0x100, 0xFF01, rng.getrandbits(32)]), rng.choice([-1 & 0xFFFFFFFF, 0, 5, rng.getrandbits(32)])], img.pokes()


@item_spec(0x00447C20, "DeckItem_Update")
def gen_item_update(env, n):
    img, rng = new_image(env)
    item_at_object(env, img, rng)
    return [], img.pokes()


@item_spec(0x00447CF0, "DeckItem_PriorityLessEqual")
def gen_item_priority(env, n):
    img, rng = new_image(env)
    items = Items(env, img, rng)
    items.item()
    other = items.item()
    return [other], img.pokes()


def spool_spec(address, name):
    def make(fn):
        spec(address, name, "eqrack", this=lambda env: env.slab + SPOOL)(fn)
        return fn
    return make


def spool_case(env, rng, *, state=None):
    img, _ = new_image(env)
    items = Items(env, img, rng)
    addresses = [items.item() for _ in range(rng.randrange(1, 9))]
    spool_state(env, img, rng, items, addresses, state=state)
    return img, items, addresses


@spool_spec(0x00447D10, "DeckSpool_Construct")
def gen_spool_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + SPOOL, 0x18, rng)
    return [rng.choice([0x18, 0, 5, rng.getrandbits(32)])], img.pokes()


@spool_spec(0x00447D30, "DeckSpool_Reset")
def gen_spool_reset(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    return [], img.pokes()


@spool_spec(0x00447D70, "DeckSpool_ClearQueue")
def gen_spool_clear(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    return [], img.pokes()


@spool_spec(0x00447E00, "DeckSpool_Insert")
def gen_spool_insert(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    extra = items.item()
    if rng.random() < 0.15 and addresses:
        extra = addresses[0]
    return [extra], img.pokes()


@spool_spec(0x00447E80, "DeckSpool_Begin")
def gen_spool_begin(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    return [], img.pokes()


@spool_spec(0x00447F40, "DeckSpool_StartNext")
def gen_spool_start_next(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng, state=rng.choice([1, 1, 1, 0, 2]))
    base = env.slab + SPOOL
    img.u32(base + 0x10, 0 if rng.random() < 0.9 else PA(env, PS_ARENA + 0x2000))
    return [], img.pokes()


@spool_spec(0x00448110, "DeckSpool_Poll")
def gen_spool_poll(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    img.u32(LOADING, rng.choice([0, 0, 1]))
    base = env.slab + SPOOL
    if rng.random() < 0.9:
        img.u32(base + 0x10, PA(env, PS_ARENA + 0x10 * rng.randrange(0x100, 0xE00)))
    return [], img.pokes()


@spool_spec(0x004481C0, "DeckSpool_Verify")
def gen_spool_verify(env, n):
    rng = env.rng
    img, items, addresses = spool_case(env, rng)
    return [rng.choice([0, PA(env, PS_ARENA + 0x2000), rng.getrandbits(32)])], img.pokes()


# ---- the screen loop -----------------------------------------------------------------------------------------------------------------------------------------

L = espec("eqloop")


@L(0x004445B0, "EquipSelect_Run")
def gen_run(env, n):
    img, rng = new_image(env)
    player = rng.choice([0, 1])
    common(env, img, rng, player=player)
    eq_world(env, img, rng, player=player)
    pad_edges(img, rng, player, only=rng.choice(["back", None, None, "back"]))
    # the window the stubbed constructor hands back is the first block of the arena: its two flags are poked
    window_at = PA(env, PS_ARENA)
    img.u32(window_at + 0xBC, rng.choice([0, 0, 0, 1]))
    img.u8(window_at + 0xC0, rng.choice([0, 0, 0, 1]))
    return [rng.choice([1, 1, 0, 2]), player], img.pokes()


@L(0x00444590, "EquipSelect_ScreenCall")
def gen_screen_call(env, n):
    img, rng = new_image(env)
    player = rng.choice([0, 1])
    common(env, img, rng, player=player)
    eq_world(env, img, rng, player=player)
    pad_edges(img, rng, player, only=rng.choice(["back", None]))
    window_at = PA(env, PS_ARENA)
    img.u32(window_at + 0xBC, 0)
    img.u8(window_at + 0xC0, rng.choice([0, 0, 1]))
    img.u32(obj(env, 5), rng.choice([1, 0, 2]))
    img.u32(obj(env, 5) + 4, player)
    return [obj(env, 5), obj(env, 5) + 8], img.pokes()
