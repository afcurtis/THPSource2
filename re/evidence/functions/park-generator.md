# The park generator of the editor (Phase 4.11c)

Source: `levelgen.cpp` (`0x004397d0..0x0043f720`): 46 functions, two of them (`0x0043f540`, the floor pieces' flag setter, and the inlined `0x0043f710`) not listed by Ghidra. Native:
`src/runtime/level_gen.hpp` (`levelgen::LevelGen`, built on the 4.11b `genobjs::GenObjs`) with `level_gen_maps.cpp`, `level_gen_pieces.cpp`, `level_gen_edit.cpp`, `level_gen_prk.cpp`,
`level_gen_load.cpp`, `level_gen_region.cpp`, `level_gen_rails.cpp`. Probes: `re/gdb/probes/levelgen_specs.py` (the world builder, the engine stubs, group `lgm`), `levelgen_specs_pieces.py`
(`lgf`, `lgb`, `lgl`), `levelgen_specs_edit.py` (`lge`), `levelgen_specs_prk.py` (`lgk`), `levelgen_specs_region.py` (`lgz`, `lgt`, `lgr`), run by `frontend_calls.py`; fixtures
`re/evidence/fixtures/levelgen-calls-*.txt`; test `src/runtime/level_gen_retail_calls_test.cpp` (`tests/test_park_generator.py`). Slices `park-maps`, `park-pieces`, `park-edit`, `park-prk`,
`park-region`, `park-rails`.

What the code is: the park editor keeps the park as a grid of cells holding piece references. `LevelGen` turns it into the game's objects: it loads the theme's library of source models, builds an
*out-model* (4.11b) for every placed piece and for the floor under them, hides the faces that neighbours cover, makes the gap members and start positions, lays the pieces' rails out as polygons,
builds the runtime PSX region the game draws and collides with, and writes the trigger file of nodes (starts, horse points, rails, zones, gaps). The editor's operations (place a piece, add an item,
remove the top piece) and the `.PRK` park file read and write the same cells. Every function is a computation on memory plus a few engine services, so the validation is the forced-call
differential pattern of 4.8: the retail function is called inside the running game on a state the probe pokes, and the native class replays the same state.

## Object layout

The `LevelGen` object is 0x3748 bytes (the layout is the comment at the top of `level_gen.hpp`). The cells are 16 bytes (five 16-bit piece references at +0, five turns at +0xa, the layers used at +0xf)
in the rows the editor keeps at `0x005614d4`; the object keeps `FloorTab` (+0, the cells with the floor and wall fill applied) and two maps of out-model pointers, five per cell (`OutModelMap` +0x2348 for the park's pieces,
`FloorModelMap` +0x2350 for the floor's), each with its list of pieces (+0x2344, +0x234c). The source models are 250 in-models of 0x24 bytes (+0x18, how many +0x2340). A piece (an out-model, 0x238 bytes) records
its cell and size (+5..+0xa), turn (+0xc), offsets (+0x21c..+0x220), in-model (+0x224), built model (+0x228), fog header (+0x22c), next piece (+0x230) and gap member (+0x234).

## Services

The allocator (`0x0046f490`), `Mem_Delete` (`0x0046f4d0`), the logger (`0x004cf850`) and the assertion (`0x004011e0`) are the services of 4.11b. The rest are the engine: the PSX region spooler (`0x004b32f0` queue a region,
`0x004667e0`, `0x00464710`, `0x0042fc70` clear and build regions; replayed as `engine_<address>` with scripted results), the loader's progress call (`0x0045d760`, one byte argument), the trigger-file
reader (`0x004c8650`: three scripted dwords for the position, then the record) and the constructors and virtual methods of the six node classes (vtables `0x005198a0/ac/b8/c4/d0/dc`: a constructor is a
service whose stub writes the vtable; a virtual call is a service named by the slot's address). The class allocator `0x00466b70` is modelled natively (allocate, zero, count at `0x0056a920`), and the CRT vector
constructor and destructor (`0x00500400`, `0x0050030c`) as loops.

## Slice `park-maps`

`LevelGen_Construct` (`0x004397d0`, takes the in-game flag stored at +0x3744, returns the object); `LevelGen_Destruct`, `LevelGen_UnloadModels`, `LevelGen_ClearSource`,
`LevelGen_AllocMaps` / `LevelGen_FreeMaps` (FloorTab and the two maps as rows over one block each; an assertion fires if they are already allocated or freed), `LevelGen_Resize` (a size row of the table at
`0x005283a4`: free, allocate again, empty), `LevelGen_InitStatics` (point the globals at the editor's rows and items; with the flag empty the park: no piece, one layer, items named "Gap n", the eight trailing
records), and the four "give back" functions (`ReleasePieces`, `FreeFloorPieces`, `FreeGaps`, `FreeStarts`), each of which frees the nodes of a piece before the piece.

## Slice `park-pieces`

`LevelGen_BuildFloorTable` (`0x0043a3e0`) fills FloorTab: a fill piece under every stack (the three special ids of the theme are the 1x1, 2x1 and 2x2 fills), the footprints of placed pieces cleared, neighbouring fill
pieces merged. `BuildPieceObjects` makes an out-model for every layer of every cell that holds a piece, places it and fills the cells it covers in `OutModelMap`, recording start and horse pieces;
`BuildGridPieceObjects` does the same for the floor (a floor piece fills only its own layer; the park's builder fills all layers: the two are one routine, `build_pieces(park)`). `PrunePieces`,
`ComputeNeighbours` (the pieces on the far sides that reach above a piece's base), `BuildItemGapObjects` (gap members for the items), `BuildStarts`, `MarkEdges` (the neighbours' faces on the four sides),
`SetCellFlags`, `SetFloorFlags`, `FlagFloorPieces` and `MarkGaps` (show the gap members of one piece, hide the others).

## Slice `park-edit`

The editor's operations: `PlacePiece` (take the old start piece away, record start and horse pieces, make the out-model, fill its cells), `AddItem` (take a free item record or update one; its ends and, for an
update, its turns from the direction between them), `RefreshItemGaps` (lay the gap members of one end of an item out again from the item's record: `ComputeEdges`, then `BuildHeader`), `RemoveItemsAt`, `SetFlash`
(the flash level steps up and down between 0 and 0x7f), `RemoveTop` (refuse a solid block unless forced, or a piece with something on it; empty its cells, tell its neighbours, clear its records; returns a byte).

## Slice `park-prk`

`CustomPark_ParseBuffer` reads both format versions of the park file (`0x4e24`, `0x4e25`), `CustomPark_SerializeBuffer` writes it and returns its size, and `LevelGen_TranslateReferences` turns the cells' piece
references between list positions and library slots of the theme. The cases include the 50 `PARK*.PRK` files of the game (unpacked by the 4.5 tools) as well as generated buffers.

## Slice `park-region`

`LevelGen_FinalizeRuntimeRegion` (`0x0043d400`) is the park builder proper: it queues and builds the regions (objects, models, colour table in the tables at `0x0056d438`, 0x44 bytes per slot), runs the
builders above in order, rasterises the objects into the 20 x 20 zone grid (`Rasterise`: the cells a box touches; `CreateZonePacket`: counts on the first call, then the object indices, 0x1ce bytes a cell), and
finishes with the rails and the nodes. `PSX_DestroyObjectRecordArray` and `UnloadOutPsx` give the regions back.

## Slice `park-rails`

`LoadModels` loads a park library by name (queue the model, world and source regions, give the source models their positions, read them into in-models, then load the rails), `SelectVariant` picks the theme and
size, `LoadRails` reads the trigger file's rail nodes (every rail that links to a model node becomes a polygon of points in that model's in-model), `CreateRails` lays the pieces' rail polygons, moved by each
piece's offset and turn, together into one set of outlines (the 4.11b polygon list code), `BuildNodes` builds the trigger file (starts, horse points, rail points, zone nodes, gaps) and lays it out in one block,
`SetFog` / `DimFog` set the fog range for the theme and size (95 per cent when little memory is left) and `ZoneRoom` is how many zone items there is room for (4000 less the live count).

## Validation

Forced calls inside the running game (level 12 loaded): the probe pokes a legal world into a scratch slab (a park with models, pieces, items, gaps, rails, regions), calls the retail function and records the
return value, every written byte and the ordered service calls; the native class starts from the same bytes and all three must match. The 46 functions are replayed over 8,719 cases in full captures
(`build/p411c`, 300 MB, kept out of git) with no mismatch; the committed fixtures hold the first 30 cases of each capture for every function (1,500 cases, 58 MB), which replay with no mismatch too.
gcov over the full captures: 99.5 % or more of the lines of every source (224 / 225 lines of `level_gen_maps.cpp`, 365 / 370 `pieces`, 208 / 208 `edit`, 125 / 125 `prk`, 156 / 157 `load`, 415 / 415
`region`, 182 / 183 `rails`); over the committed fixtures alone `edit` reaches 203 / 208 (the others are as above). The lines never reached are the `default` arms of the rotation switches, two loops that skip when
the grid has no width or a footprint no columns, one scan that ends on the last model and one line of `level_gen_rails.cpp`.

Notes for later sessions:
- Values that follow the live clock are not compared: the loader's progress call (`0x0045d760`) takes a flag "the deadline has passed", and the forced call under GDB runs slowly enough for retail to see the
  deadline pass where the port, with a frozen clock, does not. `Adapter::time_dependent` (in `retail_calls_replay.hpp`) compares such services by name only; `CreateRails` and `FinalizeRuntimeRegion`
  use it. The deadline word of the rails' polygon list (list B, `+8`) is the clock and is declared unspecified.
- The stack leftover of the fifth point of a three-point element (4.11b) shows up again in `LoadModels` and `SelectVariant`; it is declared unspecified there.
- Retail faults, not mistakes in the port: the probe records a forced call that faults and leaves it out of the fixtures. The rates in the full captures are 17 % of `BuildGridPieceObjects` (34 of 200), 31 % of `FreeMaps` (37 of 120), 5 % of `InitStatics` (11 of 200)
  and 17 % of `LoadRails` (7 of 40); the causes were not investigated (a segfault on the made-up state), and the cases that replay are the clean ones. `RefreshItemGaps` was the instructive one: every case in which a gap matched the item faulted, because the generator's gap members carried random layout words
  (`+0x3c..+0x4c`), and `BuildHeader` writes through them; with the constructor's layout (`0x864, 0x1c, 0x3c8, 0x48, 0x31`) set, the branch replays (the first coverage run had missed it entirely).
- `lgl`'s floor-piece cases once recorded nothing (the pokes ran past the window of the pieces), and `lgz`, `lgm`, `lge`, `lgl` and `lgt` hit the forced-call time limit before their last functions; the
  functions that were starved were captured again by themselves (`P410_ONLY`).
