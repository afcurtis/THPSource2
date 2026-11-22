# The generator object layer of the park editor (Phase 4.11b)

Sources: `genobjs.cpp` (`0x0043f730..0x004436d0`, 61 functions, the ring-of-points polygon code in its second half), `faceremoval.cpp` (`0x004436e0..0x00443fc0`, 11 functions) and
`editorpieces2.cpp` (`0x00444010..0x00444450`, 9 functions): 85 functions, 7 of them not listed by Ghidra (`0x0043f8e0`, `0x0043f8f0`, `0x00443290`, `0x004432b0`, `0x004437f0`, `0x00443fc0`, and the
thunk `0x00443200`). Native: `src/runtime/gen_objs.hpp` (`genobjs::GenObjs`) with `gen_objs_pool.cpp`, `gen_objs_out.cpp`, `gen_objs_gap.cpp`, `gen_objs_poly.cpp`, `gen_face_removal.cpp`, `gen_piece_tables.cpp`.
Probes: `re/gdb/probes/gen_specs.py` (groups `gpiece`, `gface`), `gen_specs_poly.py` (`gpoly`), `gen_specs_model.py` (`gpool`, `gring`, `gnode`, `gout`, `ggap`), run by `frontend_calls.py`; fixtures
`re/evidence/fixtures/gen-calls-{gpool,gring,gnode,gout,ggap,gpoly,gface,gpiece}.txt`; test `src/runtime/gen_objs_retail_calls_test.cpp` (`tests/test_generator_objects.py`).

What the code is: the park editor builds each piece it places (a PSX model) into an *out-model*, an object that remembers where the piece sits and which of its faces are hidden by the neighbouring
pieces; the face tests work on rings of points. Every function here is a plain computation on memory plus the allocator, `Mem_Delete`, the logger and one GTE call, so the validation is the
forced-call differential pattern of 4.8: the retail function is called inside the running game on a state the probe pokes, and the native class replays the same state.

## Services

| Address | Name in the replay | What |
|---|---|---|
| `0x004011e0` | `assert` | `M3dUtils_InBetween(cond, message)`: logs a failed condition, carries on (the byte `cond`; the stub reports only failures, with the message address) |
| `0x0046f490` | `alloc` | the loader's allocator `(size, heap, 1, 0)`; the probe hands out a slab arena |
| `0x0046f4d0` | `mem_delete` | `Mem_Delete(block)` |
| `0x004cf850` | `printf_log` | the logger; only the format address is compared |
| `0x0045d760` | `engine_0045d760` | the loading screen's progress call (one byte argument: the deadline has passed) |
| `0x004e3ce0` | `engine_004e3ce0` | the GTE `SQR` on the vector register at `0x006a3eb0` (three halves): the three squares are written to `0x006a3eb8..0x006a3ec3`; the stub writes the squares the retail routine would, the native code reads them back as scripted values |

`Mem_Copy` (`0x0046f2e0`, `(dst, src, n)`, both addresses word aligned or the assertion `0x00534bd8` logs) and the integer square root `0x004f53b0` (`fild`, `fsqrt` at the game's 24-bit x87 precision, truncate) are
not services: the port does them, and the probe leaves them running. The assertion prelude (`__FILE__` copied to `0x0056881c`, the line number to `0x00568638`) is not behaviour and is not reproduced.

## Statics

| Address | Meaning |
|---|---|
| `0x00528710`, `0x005611bc[100]` | the model-info pool: the block being filled and the 100 block pointers (a block: a count then 100 elements of 0x2c bytes, 0x1134 bytes in all) |
| `0x00561454`, `0x00561458`, `0x005614e4` | the node ring: table of 0x8c0 sixteen-byte nodes, the cursor and the number in use; `0xffffd8f1` marks a free node, `0xffffdd48` a live one |
| `0x005614c8` | the park theme (0..3: the column of the piece tables) |
| `0x00528398`, `0x0052839c`, `0x005283a0` | the grid width and depth and the size row (a row of light settings is `(5 * theme + row) * 32` into `0x0052a190`) |
| `0x00561488`, `0x0056148c` | the size of one cell (x; the height axis) |
| `0x00561160`, `0x0056119c`, `0x00561194`, `0x00561168`, `0x00561350[12 * n]`, `0x005613f0[12 * n]` | the lights: shade gain, falloff scale, falloff slope, their number, their positions (x, -1000, z) and directions (0, 0x1000, 0) |
| `0x005614dc`, `0x005614e0` | the 0x400-byte falloff table (by squared distance) and the 0x100-byte shade table |
| `0x005611b0`, `0x00561164`, `0x00561190`, `0x0056115c` | the face-removal point pool: block, cursor, number made, overflow flag |
| `0x00528820` | the piece records, 0x25 bytes each: one id per theme (4 bytes), a 0x20-byte name, the kind; the list ends at a record of `0xff` ids (151 pieces) |
| `0x005614a8`, `0x00561490`, `0x00561174` | five ids each: the pieces the editor handles specially (`EditorPieces_Setup` fills them) |
| `0x0052a0c0` | the ids of the listed pieces, ended by `0xfe` (`0xff` is an empty position) |
| `0x00529e18` | five-byte rows: an id and the four ids it has turned by 0..3 quarter turns, ended by `0xff` |

## The model-info pool and the in-model (`slice editor-in-model`)

`EditorInModel_NewModelInfo` (`0x0043f820`) hands out the elements of the pool: block `table[index]` (allocated on first use, count 0) holds up to 100 elements of 0x2c bytes after its count; filling the
100th moves the index on. `EditorInModel_KillModelInfo` (`0x0043f7b0`) frees blocks 0..index inclusive (a null entry is freed too), `ModelInfo_ClearTable` (`0x0043f730`) zeroes the table (asserting it
was empty unless the flag byte allows).

`EditorInModel_SetData(model, type)` (`0x0043f930`) fills the in-model object (+4 model, +8 type, +9 the kind of `EditorPieces_Kind(type)`, +0xa/+0xb/+0xd/+0xc the size in cells from the model's extent
by the cell sizes (rounded up when the remainder passes cell - 4), +0xe..+0x16 the extent turned), then, unless bit 0 of the kind is set, turns every wall face of the model into an element: the four vertices
by the face's signed index bytes; a face whose x (or z) stays within one of its first vertex is a wall; the points are turned into the (x, z)-plane of the wall (z negated for one, x and z swapped for the other),
a repeated third vertex makes a triangle, the quad's last two points swap, the loop is closed with the first point and negated when its signed area is negative. An element (0x2c bytes): +0 flags (bit 0 the turn
kind, bit 1 the orientation), +2 the point count, +4 the next element, +8 the face, +0xc five points of three halves. The fifth point of a triangle's element is a copy of an uninitialised stack local (the replay
does not compare it). The in-model ends with a holder (`0xc` bytes, `+0x20`) for polygons. `EditorInModel_SetTransparency` (`0x0043fda0`) rewrites bits 6-7 of the face flags that have either.

## The out-model's statics and node ring (`slice out-model-statics`)

`EditorOutModel_Init` (`0x0043fe00`) takes the theme's light row (count, three constants, then x and z bytes for each light placed on the grid: `(byte - width/2) * cell`, `(depth/2 - byte) * cell`), builds the
tables (`EditorOutModel_BuildTables`, `0x0043ff30`: falloff[i] = clamp((0x3ff - (0x3ff - i) * slope / 10) * 256 / 1024), shade[i] = ((i - 0x80) * gain + 0x3200) / 100) and allocates the ring of 0x8c0 nodes, all free.
`EditorOutModel_AllocNode` (`0x00440060`) scans from the cursor (wrapping at 0x8c0) for the free marker, claims it (area `0xffffdd48`, alive 1, orientation 0xff, next 0) and moves the cursor on; a full ring logs
and hands out the node under the cursor anyway. `EditorOutModel_SpareNodes` is `0x898 - used` (a small overdraw reads as 0).

## The out-model (`slice out-model`)

The 0x238-byte object: +0/+1/+2 state bytes (+1 is 0, 1 or 2: 2 means "the piece is known to hide nothing"), +3 the number of neighbours, +4 the number of neighbours before it in the order, +0xb the size
in cells, +0xc the rotation (0..3), +0xd 1 for a specially handled piece, +0xe the size of its data, +0x10/+0x12/+0x14 the model's counts, +0x18 the neighbours (pointers), +0x218 the first node, +0x21c/+0x21e/+0x220
the offset (z, y, x) in 16 bits, +0x224 the in-model, +0x228 the built model, +0x22c the fog header. A node (16 bytes): +0 the area left (or the live marker), +4 alive, +5 the turn of the face, +8 the element, +0xc the
next node.

- `LevelGen_ConstructPieceObject` (`0x00440190`): copies the sizes, clears 0x40 bytes at +0x18, makes a node per element, +0xb from the in-model, +0xd from `EditorPieces_IsSpecial(type)`.
- `EditorOutModel_ReleaseNodes` / `_ShowNodes`: give the nodes back (area free marker, count - 1) / make them all alive and live.
- `EditorOutModel_HideAgainst(other, mode)` (`0x004402f0`): when neither piece is state 2, every pair of live nodes goes through `EditorOutModel_HidePair`; with state 2 the nodes are all made live and every pair is tried.
- `EditorOutModel_HidePair(self, other, a, b, mode)` (`0x004403c0`, cdecl, 5 arguments): the turn of each face is `(rotation + flags') & 3` made odd/even as the flags say (`flags' = flags` for an odd flag, `2 - flags` for an even one);
  only when the second turn equals `mode` and the first is `mode ^ 2` are the faces in one plane facing each other. Then each face becomes a ring of face-removal points (the element's points moved by the piece's offset
  and turned by the turn), reversed when the turn's bit 1 is clear, its area taken when the node is still live; `FaceRemoval_Overlap` gives the area they share, which is taken off both nodes; a node left with 300 or less
  (or a thirty-second of what it had) is hidden (alive 0).
- `EditorOutModel_RemoveAllHiddenFaces` (`0x004407c0`): for each neighbour not special and with a live state, `HideAgainst` with a mode from the kind bit 3 and whether the neighbour is before this piece.
- `EditorOutModel_SetModelDataSize(flag)` / `LevelGen_LoadModelDescriptor(buffer, flag)`: the size of the built model (header, vertices, every face not hidden) and the model itself: header and vertices copied and
  turned by the rotation, each kept face copied with bit 7... of its flags cleared when it was hidden but kept as a wire, the colour index of its kinds 1/4/5/6/8/0xe replaced by the theme's, and a shade for each corner
  from the lights (squared distance by the GTE, falloff table, sum clamped to 255, shade table).
- `EditorOutModel_MarkHiddenFaces` (`0x00440e80`) marks the built faces (bit 7 cleared for hidden ones; for the wire pass set for those whose node faces the given direction); `EditorOutModel_SetFog` writes the
  density and the packed colour `((c & 0x3fffffc) + (c << 10)) * 64 + (c >> 2)` into the header.

## Gap members (`slice gap-members`)

A 0x58-byte object: +4 the record (a piece reference whose bytes 5, 6, 7, 8, 0xa are the cell position, height and sizes), +0x10 the primitive buffer, +0x14 the target item, +0x20..+0x2c the box on the grid (x0, z0,
x1, z1), +0x30/+0x34 its size, +0x38 active, +0x39 visible, +0x3c the buffer's size (0x864), +0x40/+0x44/+0x48/+0x4c the layout (0x1c, 0x3c8, 0x48, 0x31), +0x50 the rotation, +0x54 the height. `ComputeEdges` places the box
for each of four rotations; `BuildPrims` writes the header (`BuildHeader`: the extent, a 7 x 7 lattice of points and pairs of unit directions), then a 6 x 6 grid of two textured quads; `GapPiece_BuildQuad` builds
the marker quad. `SetVisible` and `SetActive` toggle bit 5 of the first flags word.

## Points, polygons and lists (`slice generator-polygons`)

A point is 0x18 bytes (x, height, z as halves; +8 next; +0xc and +0x10 words copies keep; +0x14 a mark byte); a polygon is 0x1c (the box, +0xc the first point, +0x10 the next polygon of a list, +0x14 loop, +0x18 count);
a list holds the first polygon at +0 and a deadline at +8. `GenPoint_Near` is within 6 in all three coordinates. The routines join and cut: `GenPoly_TryMerge` boxes (with 6 to spare) then loops (`MergeLoops`: marks
the points near the other loop's, finds the shared run with `GenPoly_EdgeRuns` and removes it) or open strings (`JoinEnds`: the ends meet, the string is reversed if needed, and the join must be straight enough: the
cosine of the turn in Q12 above 0x800); `GenPoly_Split` splits a loop that touches itself; `GenPoly_Trim` / `CutOut` / `CutBy` lay one string over another. `GenPolyList_Merge` runs the first polygon of the incoming list
against every polygon of this list (`GenPolyList_Pair`: 1 joined, 3 / 2 cut, 0 nothing) and moves what meets nothing to the end of this list, calling the progress service whenever it does.

## Face removal and the piece tables (`slices face-removal`, `editor-piece-tables`)

`FaceRemoval_Overlap(a, b)` (`0x00443820`): both rings must be within one unit in the depth coordinate; for each point of one ring against each edge of the other (and then the other way round) the cross products
decide between the collinear case (side tests choose which of the other's end points to insert), a crossing (the parameter `(t * 64) / (den >> 4)` with the assertions `overflow` and `underflow`) and nothing;
new points are inserted after the current point (flag 2), the first point still flagged 1 starts the result, coincident points of the two rings are linked through their +0x14, and the walk along the result returns its
area. The pool allows 40 points (the 41st sets the overflow flag and the call returns 0).

`EditorPieces_*` (`0x00444010..0x00444450`) read the piece records: find by id for a theme, the id in another theme, the special set, the position in the listed ids, the kind byte, the id turned by quarter turns.

## Validation

Forced calls inside the running game (level 12 loaded, the unit tests' slab from the allocator): the probe pokes a legal state for every case (models of wall faces, in-models, out-models with nodes, rings and
polygons built from rails with the jitters the near test is there for, piece table settings), calls the retail function and records the return value, every written byte and the ordered service calls; the native
class starts from the same bytes and the three must match. All 85 functions are replayed over 8,114 cases with no mismatch (gcov: 98 to 100 % of the lines of each source; the lines not reached are the unreachable `default` arms of the rotation
switches, the second closing branch of `GenPoly_JoinEnds` (its test repeats the one that chose the branch), the block-count remainder of one extent in `EditorInModel_SetData`, a few node-ring
overdraw paths and the longest chains of the polygon routines). The counts per function are in the slice manifests (`re/slices/{editor-in-model,out-model-statics,out-model,gap-members,generator-polygons,face-removal,editor-piece-tables}.yml`).
What the probe avoids, because retail loops or faults there: appending to a loop, a string walked as a loop (`GenPoly_CutOut`'s kill string, `GenPoly_JoinEnds`), a cut with a single marked point on a loop, two rings that run
opposite ways in `FaceRemoval_Overlap`, a pinched loop whose points are closer than the near tolerance (4.11b met each as a hang and found it with `P410_WATCHDOG`), a node pair with a null node, an empty polygon's bounds and
head, a ring with no free node, a null table or list. The forced calls that fault in retail anyway (about 0.2 %) are recorded with an error and left out of the fixtures.
