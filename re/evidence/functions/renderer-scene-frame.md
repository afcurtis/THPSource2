# The frame functions of the scene renderer (m3d.cpp, replay.cpp)

Status: confirmed by retail forced calls (160 cases of the four frame functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0045f3f0`, `0x00460a90`, `0x004667e0`, `0x00466a00` (4 functions) (slice `render-scene-frame`)
Native: `src/runtime/render_kit.hpp`, `render_scene.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs5.py` (group `rscn`); fixtures `re/evidence/fixtures/render-calls-rscn*.txt`

## What is here

`M3d_RenderCleanup` closes a view: it builds a rectangle the size of the view (the viewport edges' corners, or the window for the software renderer, `0x029da3a0` decides) in display pixels, hands
it to the draw-offset service (`0x004e87f0`) and links the primitive behind everything in the current buffer's ordering table: its far word at +0x7ff8 is the head the primitive goes in front of, and
its depth is -1.0 when the buckets are the current buffer's and the viewport's depth word times a float constant otherwise.

`Render_SubmitObjectList(list)` is the per-frame entry for the item renderer. It loads the frame's light matrix (`0x005620e8`) and colour matrix (`0x005620c0`) into the GTE, sets the background
vector from the fog offsets (`0x00563a96`, `0x00563a9e`, halved), culls the list against the view (`Render_FlagObjectsOutsideView`, an earlier slice, run natively), copies the frame's depth and
colour words into the state the item renderer reads, and hands every object that is not culled (bit 15 of its flags) to `RenderSuperItemShadow` (a service here, a slice of its own). The pending
flag (`0x0056437c`) goes back to the value it had at the start of the frame (`0x00564380`) when the list is done.

`M3D_LoadZoneBlockmap(index, record)` reads the zone grid of an environment from its record: the bounds (four words), the cell counts (the low and high halves of a dword, each at most 20, and the
assertion fires above that), the cell size (the extent divided by the width) and then, for every cell, a header of three words whose last is the item count, and the list itself, pointed at inside the
record; the object numbers in the lists become the objects' addresses (`region records + number * 0x4c`). Only index 0 is allowed.

`ReplayLoadingBanner` (`0x00466a00`) sets the display up for text and formats the lines of the loading screen of a replay: the state name, the level name and the skater model name(s), joined with
newlines into a local buffer and given to the text drawer (`0x004e8870`) with the text recorded.

## Method

The probe pokes the buffers, the viewport words and the object list (three objects of 0x80 bytes linked through +0x20) for the first two, a zone record with up to 4 x 3 cells holding random item
numbers for the blockmap, and the table pointers and name strings for the banner. The long-lived pointers into the zone record are re-derived by the replay, so a case compares the whole record.

## Lessons

(1) The culling reads the models of the objects and four float constants (`0x00518910`, `0x00519a74..7c`, `0x00549d98`, `0x00518484`) outside any captured window; the first probe passed only the
cases whose objects were all culled until those constants and a consistent model table were part of the capture. (2) The zone record's list entries are modified in place, so the replay compares the
record bytes, not just a return value.
