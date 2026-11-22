"""Phase 4.10c case generators, part 2: the lists, scrolling lists and grids.  See frontend_specs_menu.py."""

from __future__ import annotations

import struct

from frontend_specs_menu import (ARRAYS, CELLS, CHILD_STRIDE, CHILDREN, OBJ, Image, STRING_POOL, VT_GRID, VT_LIST, VT_REAL, child_at, coords,
                                 item_fields, mspec, poke_props, props_at, sprite_at, string_at, world)


def new_image(env):
    return Image(), env.rng


def flag(rng):
    return rng.choice([0, 1, 2, 3])


def real_text_child(env, img, rng, index, **kw):
    """A genuine MenuText child (the real vtable), so the toolkit's own methods run through the dispatch."""
    kw.setdefault("props", rng.randrange(4))
    item_fields(env, img, child_at(env, index), rng, vtable=VT_REAL["text"], text=rng.choice(STRING_POOL[:15]), **kw)


def make_children(env, img, rng, count, real=False, **kw):
    """`count` children: ids distinct, at least one selectable; fake (reporting) or real MenuText objects."""
    for i in range(4):
        poke_props(env, img, i, rng)
    ids = rng.sample(range(0, 60), count)
    selectable = [rng.choice([0, 1, 1]) for _ in range(count)]
    if not any(selectable):
        selectable[rng.randrange(count)] = 1
    highlighted_at = rng.randrange(count)
    for i in range(count):
        extra = dict(kw)
        extra.setdefault("highlighted", 1 if i == highlighted_at else 0)
        if real:
            real_text_child(env, img, rng, i, id=ids[i], selectable=selectable[i], **extra)
        else:
            item_fields(env, img, child_at(env, i), rng, id=ids[i], selectable=selectable[i], **extra)
    return ids, selectable, highlighted_at


LIST_SIZE = 0x1C0
SCROLL_SIZE = 0x350


def list_object(env, img, rng, kind="list", count=None, real=False, fake_vtable=True, **kw):
    base = env.slab + OBJ
    size = LIST_SIZE if kind == "list" else SCROLL_SIZE
    img.garbage(base, size, rng)
    count = rng.randrange(1, 12) if count is None else count
    ids, selectable, highlighted_at = make_children(env, img, rng, count, real=real, **kw)
    img.u32(base, env.slab + VT_LIST if fake_vtable else VT_REAL[kind])
    img.u8(base + 0x04, rng.choice([0, 1]))
    img.u32(base + 0x08, rng.choice([0, sprite_at(env, 0)]))
    img.u32(base + 0x0C, rng.choice([0, sprite_at(env, 1)]))
    img.u32(base + 0x10, rng.randrange(0, 30))
    for i in range(0x60):
        img.u32(base + 0x14 + 4 * i, child_at(env, i) if i < count else 0)
    img.u32(base + 0x194, count)
    cursor = rng.choice([highlighted_at, rng.randrange(count)])
    img.u32(base + 0x198, cursor)
    img.u32(base + 0x19C, coords(rng))
    img.u32(base + 0x1A0, rng.choice([rng.randrange(-50, 300), 0]))
    img.u32(base + 0x1A4, rng.randrange(1, 400))
    img.u32(base + 0x1A8, rng.randrange(1, 300))
    img.u32(base + 0x1AC, rng.randrange(0, 20))
    img.u32(base + 0x1B0, rng.choice([0, 1, 1]))
    img.u32(base + 0x1B4, rng.choice([0, 1]))
    if kind == "list":
        img.u32(base + 0x1B8, rng.randrange(0, 12))
    else:
        img.u8(base + 0x1B8, rng.choice([0, 1]))
        img.u8(base + 0x1B9, rng.choice([0, 1]))
        pad = rng.randrange(0, 8)
        img.u32(base + 0x348, pad)
        img.u32(base + 0x33C, rng.randrange(0, 20))
        img.u8(base + 0x34C, rng.choice([0, 1]))
        top = rng.randrange(0, 200)
        img.u32(base + 0x340, top)
        img.u32(base + 0x344, top + rng.randrange(20, 300))
    return base, count, ids, selectable, highlighted_at


def scroll_offsets(env, img, rng, base, count, pad):
    """Consistent running offsets for a scroll list's children (offset[i] = offset[i-1] + height[i-1] + pad)."""
    offset = 0
    for i in range(0x60):
        img.u32(base + 0x1BC + 4 * i, 0)
    for i in range(count):
        height = img.b.get(child_at(env, i) + 0x18, 0) | (img.b.get(child_at(env, i) + 0x19, 0) << 8)
        img.u32(base + 0x1BC + 4 * i, offset)
        offset += height + pad


def scroll_object(env, img, rng, **kw):
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng, kind="scroll", **kw)
    pad = img.b[base + 0x348] | (img.b[base + 0x349] << 8)
    scroll_offsets(env, img, rng, base, count, pad)
    return base, count, ids, selectable, highlighted_at


# ---- MenuList ----------------------------------------------------------------------------------------------


@mspec(0x00470A30, "MenuList_DrawBox", this=OBJ)
def gen_list_draw_box(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00470A70, "MenuList_SetArrows", this=OBJ)
def gen_list_set_arrows(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng)
    return [rng.choice([0, sprite_at(env, 2)]), rng.choice([0, sprite_at(env, 3)]), rng.randrange(0, 40)], img.pokes()


@mspec(0x00470AB0, "MenuList_HighlightedId", this=OBJ)
def gen_list_highlighted_id(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng)
    if rng.random() < 0.12:
        for i in range(count):
            img.u8(child_at(env, i) + 0x0E, 0)
    return [], img.pokes()


@mspec(0x00470B20, "MenuList_Highlight", this=OBJ)
def gen_list_highlight(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng, real=rng.random() < 0.4)
    return [rng.choice(ids)], img.pokes()


@mspec(0x00470BD0, "MenuList_HighlightFirst", this=OBJ)
def gen_list_highlight_first(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng, real=rng.random() < 0.4)
    return [], img.pokes()


@mspec(0x00470C60, "MenuList_Construct", this=OBJ)
def gen_list_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, LIST_SIZE, rng)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF, rng.randrange(1, 400), rng.randrange(1, 300), rng.choice([0, 1]), rng.choice([0, 1]),
            rng.randrange(0, 12), rng.randrange(0, 20)], img.pokes()


@mspec(0x00470CF0, "MenuList_Delete", this=OBJ)
def gen_list_delete(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng, fake_vtable=False)
    return [flag(rng)], img.pokes()


@mspec(0x00470D10, "MenuList_Destroy", this=OBJ)
def gen_list_destroy(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00470D20, "MenuList_Layout", this=OBJ)
def gen_list_layout(env, n):
    img, rng = new_image(env)
    real = rng.random() < 0.3
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng, real=real)
    if rng.random() < 0.15:
        # a cursor on a child that cannot be selected, to run the search
        sel = [i for i in range(count) if not selectable[i]]
        if sel:
            img.u32(base + 0x198, sel[0])
    return [], img.pokes()


@mspec(0x00470EC0, "MenuList_Draw", this=OBJ)
def gen_list_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    list_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00470F50, "MenuList_SnapAll", this=OBJ)
def gen_list_snap_all(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00470F80, "MenuList_Navigate", this=OBJ)
def gen_list_navigate(env, n):
    img, rng = new_image(env)
    list_object(env, img, rng, real=rng.random() < 0.3)
    return [rng.choice([0, 1, 2, 3, 2, 3, 4, 7])], img.pokes()


@mspec(0x00471050, "MenuList_Add", this=OBJ)
def gen_list_add(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = list_object(env, img, rng, count=rng.randrange(1, 12))
    item = child_at(env, 40)
    img.garbage(item, 0x60, rng)
    img.u16(item + 4, rng.choice([0, 0, 5, 0x40]))
    if rng.random() < 0.06:
        img.u32(base + 0x194, 0x60)
        for i in range(count, 0x60):
            img.u32(base + 0x14 + 4 * i, child_at(env, rng.randrange(count)))
    return [item], img.pokes()


# ---- ScrollList --------------------------------------------------------------------------------------------


@mspec(0x004710D0, "ScrollList_Construct", this=OBJ)
def gen_scroll_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, SCROLL_SIZE, rng)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF, rng.randrange(1, 400), rng.randrange(1, 300), rng.choice([0, 1]),
            rng.randrange(0, 20), rng.randrange(0, 8), rng.randrange(0, 20)], img.pokes()


@mspec(0x00471180, "ScrollList_Delete", this=OBJ)
def gen_scroll_delete(env, n):
    img, rng = new_image(env)
    scroll_object(env, img, rng, fake_vtable=False)
    return [flag(rng)], img.pokes()


@mspec(0x004711A0, "ScrollList_Destroy", this=OBJ)
def gen_scroll_destroy(env, n):
    img, rng = new_image(env)
    scroll_object(env, img, rng)
    return [], img.pokes()


@mspec(0x004711B0, "ScrollList_Layout", this=OBJ)
def gen_scroll_layout(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = scroll_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00471370, "ScrollList_Draw", this=OBJ)
def gen_scroll_draw(env, n):
    img, rng = new_image(env)
    world(env, img, rng)
    base, count, ids, selectable, highlighted_at = scroll_object(env, img, rng, real=rng.random() < 0.3)
    # children inside the visible band, so the draw loop runs
    for i in range(count):
        if rng.random() < 0.7:
            img.u32(child_at(env, i) + 0x20, rng.randrange(0, 300))
    return [], img.pokes()


@mspec(0x00471550, "ScrollList_Follow", this=OBJ)
def gen_scroll_follow(env, n):
    img, rng = new_image(env)
    scroll_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00471650, "ScrollList_Navigate", this=OBJ)
def gen_scroll_navigate(env, n):
    img, rng = new_image(env)
    scroll_object(env, img, rng, real=rng.random() < 0.3)
    return [rng.choice([0, 1, 2, 3, 2, 3, 4, 7])], img.pokes()


@mspec(0x00471750, "ScrollList_Add", this=OBJ)
def gen_scroll_add(env, n):
    img, rng = new_image(env)
    base, count, ids, selectable, highlighted_at = scroll_object(env, img, rng, count=rng.randrange(1, 12))
    item = child_at(env, 40)
    img.garbage(item, 0x60, rng)
    img.u16(item + 4, rng.choice([0, 0, 5, 0x40]))
    if rng.random() < 0.06:
        img.u32(base + 0x194, 0x60)
    return [item], img.pokes()


# ---- MenuGrid ----------------------------------------------------------------------------------------------


GRID_SIZE = 0x40


def grid_object(env, img, rng, real=False, fake_vtable=True, cols=None, rows=None):
    base = env.slab + OBJ
    cols = rng.randrange(1, 10) if cols is None else cols
    rows = rng.randrange(1, 6) if rows is None else rows
    cells = cols * rows
    img.garbage(base, GRID_SIZE, rng)
    for i in range(4):
        poke_props(env, img, i, rng)
    ids = rng.sample(range(0, 60), cells)
    present = [True] * cells
    for i in range(cells):
        if rng.random() < 0.12 and i % cols != 0 and i >= cols:
            present[i] = False
    selectable = [rng.choice([0, 1, 1]) for _ in range(cells)]
    if not any(s and p for s, p in zip(selectable, present)):
        selectable[0] = 1
    cursor_x = rng.randrange(cols)
    cursor_y = rng.randrange(rows)
    if not present[cursor_y * cols + cursor_x]:
        cursor_x = 0
        cursor_y = 0
    highlighted = rng.randrange(cells)
    for i in range(cells):
        extra = {"highlighted": 1 if i == highlighted else 0}
        if not present[i]:
            continue
        if real:
            real_text_child(env, img, rng, i, id=ids[i], selectable=selectable[i], **extra)
        else:
            item_fields(env, img, child_at(env, i), rng, id=ids[i], selectable=selectable[i], **extra)
    for i in range(cells):
        img.u32(env.slab + CELLS + 4 * i, child_at(env, i) if present[i] else 0)
    img.u32(base, env.slab + VT_GRID if fake_vtable else VT_REAL["grid"])
    img.u32(base + 0x04, cols)
    img.u32(base + 0x08, rows)
    img.u32(base + 0x0C, env.slab + CELLS)
    img.u32(base + 0x10, cursor_x)
    img.u32(base + 0x14, cursor_y)
    img.u32(base + 0x18, cells)
    img.u32(base + 0x1C, coords(rng))
    img.u32(base + 0x20, coords(rng))
    img.u32(base + 0x24, rng.randrange(0, 20))
    img.u32(base + 0x28, rng.randrange(0, 20))
    img.u32(base + 0x2C, rng.choice([0, 1, 1]))
    img.u32(base + 0x30, rng.choice([0, 1, 1]))
    img.u32(base + 0x34, rng.randrange(0, 5))
    return base, cols, rows, ids, present, selectable


@mspec(0x00471800, "MenuGrid_Construct", this=OBJ)
def gen_grid_construct(env, n):
    img, rng = new_image(env)
    img.garbage(env.slab + OBJ, GRID_SIZE, rng)
    return [coords(rng) & 0xFFFFFFFF, coords(rng) & 0xFFFFFFFF, rng.randrange(1, 10), rng.randrange(1, 6), rng.randrange(0, 20),
            rng.randrange(0, 20), rng.randrange(0, 5)], img.pokes()


@mspec(0x004718C0, "MenuGrid_Delete", this=OBJ)
def gen_grid_delete(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng, fake_vtable=False)
    return [flag(rng)], img.pokes()


@mspec(0x004718E0, "MenuGrid_Destroy", this=OBJ)
def gen_grid_destroy(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng)
    return [], img.pokes()


@mspec(0x00471930, "MenuGrid_Layout", this=OBJ)
def gen_grid_layout(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00471B40, "MenuGrid_Draw", this=OBJ)
def gen_grid_draw(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00471B80, "MenuGrid_SnapAll", this=OBJ)
def gen_grid_snap_all(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng, real=rng.random() < 0.3)
    return [], img.pokes()


@mspec(0x00471BB0, "MenuGrid_Navigate", this=OBJ)
def gen_grid_navigate(env, n):
    img, rng = new_image(env)
    grid_object(env, img, rng, real=rng.random() < 0.3)
    return [rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4, 9])], img.pokes()


@mspec(0x00471C80, "MenuGrid_Add", this=OBJ)
def gen_grid_add(env, n):
    img, rng = new_image(env)
    base, cols, rows, ids, present, selectable = grid_object(env, img, rng)
    img.u32(base + 0x18, rng.choice([0, 1, cols * rows - 1, cols * rows, rng.randrange(0, cols * rows + 1)]))
    item = child_at(env, 50)
    img.garbage(item, 0x60, rng)
    return [item], img.pokes()


@mspec(0x00471CF0, "MenuGrid_HighlightedId", this=OBJ)
def gen_grid_highlighted_id(env, n):
    img, rng = new_image(env)
    base, cols, rows, ids, present, selectable = grid_object(env, img, rng)
    if rng.random() < 0.12:
        for i in range(cols * rows):
            if present[i]:
                img.u8(child_at(env, i) + 0x0E, 0)
    return [], img.pokes()


@mspec(0x00471D70, "MenuGrid_Highlight", this=OBJ)
def gen_grid_highlight(env, n):
    img, rng = new_image(env)
    base, cols, rows, ids, present, selectable = grid_object(env, img, rng, real=rng.random() < 0.3)
    candidates = [ids[i] for i in range(cols * rows) if present[i]]
    return [rng.choice(candidates)], img.pokes()
