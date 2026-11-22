# The menu widget toolkit (menu.cpp)

Status: native port of every function of `menu.cpp` (0x0046f680..0x004734a0, 96 functions: 70 in Ghidra's list before the sync and 26 vtable-only
methods, thunks and static initialisers it did not list), replayed against retail forced calls in a running game (Phase 4.10c; method in
[frontend-text-layer.md](frontend-text-layer.md#validation-method))
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/menu_toolkit.{hpp,cpp}` (controls, text, rows, dots, sliders), `menu_lists.cpp` (lists, scrolling lists, grids),
`menu_screens.cpp` (the name entry keyboard, the wrapper, the static initialisers) and `menu_dispatch.cpp` (virtual dispatch);
fixtures `re/evidence/fixtures/menu-calls-{items,lists,screens,entry}.txt`
Capture tooling: `re/gdb/probes/frontend_calls.py` with the groups `menu` and `entry_draw` of `frontend_specs_menu.py`,
`frontend_specs_menu_items.py`, `frontend_specs_menu_lists.py` and `frontend_specs_menu_screens.py`; `re/gdb/opentony/fcall.py`
learned to return a stub through a real `ret` when the stubbed function was entered by a tail jump from the forced call

Every pad-driven frontend list (the options pages, the trick list, the card screens, the name entry) is built from these
objects. They are small C++ classes with the vtable pointer at +0, a lot of multiple inheritance (a slider is a dots object with
a text item embedded at +0x18, and its vtable methods are reached through adjusting thunks) and no source: the layouts below
come from the disassembly.

## Objects

**MenuProperties** (0x17c bytes, vtable 0x00518e50) is the style an item draws with: an embedded `Font` object at +4 (the
font code of 4.10a works on it: +0x08 is the font's justification, +0x18..+0x20 its colour, +0x34 its scale), then the normal
(+0x150) and small (+0x154) scale and the colours of a highlighted (+0x158), normal (+0x164) and disabled (+0x170) item.

**MenuControl** (vtable 0x00518e10 / 0x00518e30) is the base of every item: +0x04 word, +0x08 id, +0x0c "can be selected",
+0x0e "highlighted", +0x10 value, +0x14 width, +0x18 height, +0x1c x, +0x20 y, +0x24 pulse timer, +0x28 pulse flag, +0x2c
justification (0 left, 1 centre; anything else asserts). **MenuText** (0x00518e54) adds the text at +0x30 (at most 31
characters: the 32 bytes end at +0x50, which is the style pointer) and the style at +0x50; its width and height come from the
font (`Font_TextWidth`, and the extent of the one-character string at 0x00525768). **MenuRow** (0x00518e78) has one to three text
columns (text at +0x54 + 0x20 per column, x offset at +0xd4, own style at +0xe8, count at +0xe4) and two arrow sprites (+0xf8,
+0xfc); a column index of 4 or more writes over the offsets, as retail does. **MenuDots** (0x00518ea0) is a row of 12-pixel dots
drawn from a sprite; **MenuSlider** (0x00518ea8) is dots at +0 and a MenuText at +0x18 (arrows at +0x6c and +0x70); its text
part is what lists hold (vtable 0x00518eb0: slot 5 adjusts the value). **MenuTextScaled** (0x00518ed4) is an unselectable text
item that draws at its highlight colour, optionally one and a half times taller.

**Lists.** *MenuList* (0x00518ef8) holds up to 0x60 children at +0x14 (count +0x194, highlighted index +0x198, rectangle
+0x19c..+0x1a8, the argument given to every child +0x1ac, centring flags +0x1b0 and +0x1b4, gap +0x1b8; arrow sprites at +0x08
and +0x0c with their margin at +0x10; +0x04 says whether to draw the grey selection box). *ScrollList* (0x00518f18) adds a
visible window (+0x340 top, +0x344 bottom) over children whose offsets are kept at +0x1bc (they are the running sum of height
plus padding, +0x348), shrinks itself to fit its children the first time it lays out (flag +0x34c) and scrolls toward the
highlighted child. *MenuGrid* (0x00518f38) is columns by rows (+4, +8) with the cells in an allocated array (+0x0c) and a
cursor (+0x10, +0x14). A list's virtual methods are 0 delete, 1 arrows, 2 layout, 3 draw, 4 snap, 5 navigate, 6 add and 7 an
engine function; the grid has only a delete and an engine function.

**NameEntry** (0x00518f40, a window) is the on-screen keyboard: a grid of letters (A..Z, optionally digits, five punctuation
characters in the wide layout, SPACE) with DEL and END (ids -2 and -1), a name buffer at +0xb0 (limit + 1 at +0xdc, cursor at
+0xd0), a style copied from a given font (+0x1e4) and optionally three prompt windows (+0x1f0, built by the engine). Its input
function returns 1 for END (with the name's trailing blanks trimmed through a pointer that is, by a slip in the original,
the cursor field itself: the loop only runs for a cursor of 32), 2 for back and 0 otherwise. **MenuWrapper** (0x00518f60) builds a
list from a text array and an id array (negative ids become unselectable scaled texts, id -1 the taller one) and holds the
pad number at +0x0c; it is what the option pages use.

## Virtual dispatch

The original reaches children and containers through their vtables. A native port that only reports "virtual call" cannot run
the toolkit's own methods when a screen builds real objects (the wrapper constructs a list and adds items through the list's own
`add`). `menu_dispatch.cpp` reads the object's vtable pointer: an object of one of the toolkit's classes runs the native method
for that slot, anything else (a fake object of a probe, an engine class such as the text box) is reported as a named service
carrying the object (`item_highlight`, `list_v7`, `grid_v1`, `virtual_delete` ...). The probe uses both: fake vtables (so each call is
a reported service) in most cases and real ones where the cases need the toolkit's own methods to run (the lists' real children,
the wrapper's construction).

## Facts worth keeping

- The pulse scale is `B + (A - B) * step / 4` with A the normal and B the small scale, the product divided by four rounding toward
  zero (`cdq; and edx, 3; add eax, edx; sar eax, 2`); the step is the item's timer, asserted to be 0..4.
- The dots' brightness is `(phase * 64) / 15 + 0x40` with phase folded to 0..15, computed with an `imul` by 0x88888889: the high
  word plus the operand, shifted by three, plus the sign bit.
- `MenuList_Layout` stores its results back into the list (the y centred, the height the children's total, the width the widest
  child) and highlights the child at the cursor after stepping forward, wrapping at most once, to a selectable one.
  `ScrollList_Layout`'s search is a single step (the loop it contains spins on an unchanging byte and exits).
- `ScrollList_Draw` and `MenuDots_Draw` order the engine's rectangle fills by the renderer: the software renderer fills before a
  draw, the hardware renderer after (or the other way round for the dots), which is why both branches are ported.
- The grid's layout does not move the cursor: it highlights the first selectable cell from it.
- `MenuControl_LogNavigate` is the base class's navigate method: it only prints.
- Retail corrupts a text item whose text is 32 characters or longer (the terminator lands on the style pointer), so no case does that;
  a list with no selectable child or a `highlight` request for a missing id (it calls through the field after the array) are not
  generated either.

## Validation

Forced calls on objects built in a scratch slab (`menu` group: items, lists, screens and the wrapper; `entry_draw`: the keyboard's
draw, whose font draws are handed stack strings, so only the font, x, y and the text are recorded). The slab holds the fake
vtables, a glyph table shared by valid fake fonts (built by the retail font code), four styles, strings, sprite objects, the
object under test, up to 64 children, the grid's cell array and an allocation arena; the stubs hand arena addresses to `operator new`
and the file buffer allocator, so constructors build whole sub-objects whose addresses the replay scripts. Replayed: see the slice
`menu-toolkit` for the counts.

Cases retail itself faults on are dropped by the fixture maker (22 of 2,790 captured calls): a `MenuDots_Construct`
without a sprite (18 of 30: the assertion's stub returns and the next store goes through null, which the original never survives either)
and two each of `ScrollList_Add` and `MenuGrid_Layout` on random state the original also faults on. The native port raises the
assertion there instead, which the replay cannot compare.

Not exercised: the `Scale out of range` and `String too long` assertions of items whose strings would corrupt them, the keyboard
with a limit above 31, a wrapper of more than 64 items. The engine functions behind the slot-7 methods (`0x004dc480`, `0x004dc530`,
`0x004dc630`, `0x004dc7b0`) are services, not ported (they belong to the engine's widget base, not `menu.cpp`).
