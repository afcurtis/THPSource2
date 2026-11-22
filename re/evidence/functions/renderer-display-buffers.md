# The display buffers (db.cpp)

Status: confirmed by retail forced calls (552 cases of the thirteen buffer functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0042fc60`, `0x0042fc70`, `0x0042fcd0`, `0x0042fd20`, `0x0042fed0`, `0x0042ffc0`, `0x004301a0`, `0x004301b0`, `0x004301f0`, `0x004302c0`, `0x004304b0`, `0x00430510`, `0x004305b0` (13 functions) (slice `render-display-buffers`)
Native: `src/runtime/render_kit.hpp`, `render_display.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs.py` (group `rdb`); fixtures `re/evidence/fixtures/render-calls-rdb*.txt`

## What is here

The renderer double-buffers two 0xb4-byte records (`0x00560fe0`, `0x00561094`). The first 0x5c bytes of a record are the draw environment, the next 0x5c the display environment, and the ordering-table
pointers sit at +0x84 and +0xb0. `Db_Init` sizes the ordering table from the player count word `0x00533f3c` (4096 entries per player plus 2, and 200 more when the flag in the low byte of its third argument
is set) and the primitive area from a per-level table (indexed by level * 107 + the player count, plus 0x145000 bytes), then allocates the area and both tables; `Db_Reset` gives them back. `Db_Render` closes the frame's primitives, links the sky and queues the buffer; the vertical-blank
callback (`Db_CallBack`) marks a queued buffer displayed when the GPU is idle and `Db_Vblank` applies the display environment of the finished buffer and starts the GPU on the one that is ready.
`Db_WaitRender` waits for the drawn buffer, clears its table and swaps the primitive pointer. `Db_Flip` and `Db_FlipClear` swap the buffers (the second also clears the new current table),
`Db_DisplayDraw` shows the drawn buffer at once for the loading screens, and `HiRes_ClearOTag` sets up the hi-res mode: both buffers' environments, the sky and the ordering tables, and (when the flag is
set and the screen is not already showing) the clear of video memory.

Everything the PSX library does for these functions is a service of the replay: `PutDrawEnv`, `PutDispEnv`, `ClearOTag`, `DrawSync`, `VSync`, the GPU start and the video-memory clear are recorded by
address with their arguments, and the replay compares the order and the values.

## Method

The probe pokes both buffer records, the table sizes, the sky colour and the primitive pointers into legal combinations (a flag of 0 or 1, one or two players, a buffer already displayed or not) and calls
each function. Three things make the cases repeatable: the clock callback (`0x004dace0`) is stubbed so that the game clock does not tick between the poke and the call, `Db_Init` and the hi-res set-up
run against a fake allocator that returns slab addresses (the real one would exhaust the level's arena after a few hundred cases), and `DrawSync` is always replayed through its scripted values.

## Lessons

(1) `HiRes_ClearOTag` calls `Db_Init` with its *third* argument as the flag: an unpopped push of an earlier call shifts the offsets, so the flag is `arg3`'s low byte, not the fourth argument.
(2) The game clock moved between the poke and the call until the timer callback was stubbed; `Db_Vblank` and `Db_CallBack` read it. (3) The assertion globals (`0x00568638`, `0x0056881c`) change in every
function that asserts; they are never inside the captured regions, so the replay does not compare them.
