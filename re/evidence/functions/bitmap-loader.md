# The 8-bit BMP loader and the 2D screen primitives: `shellutils.cpp` (Phase 4.10i3)

Source: `shellutils.cpp` (`0x004afd40..0x004b0b20`). Native: `src/runtime/shell_helpers.hpp` + `shell_bitmap.cpp` (`ShellHelpers : ScreenKit`). Probe: `re/gdb/probes/frontend_specs_bmp.py`
(groups `bmp`, `bmpv`, `prim`); fixtures `re/evidence/fixtures/front-calls-bmp-{load,variants,prim}.txt`; test `src/runtime/front_retail_calls_test.cpp`.

## What the range holds

4.12 attributed two Ghidra functions to this milestone, `0x004afd8c` (586 bytes) and `0x004b035e` (541 bytes). They are the *tails* of two functions Ghidra never created (the heads have no
direct caller; the analysis only reached the branch targets). Walking the range in the objdump finds **nine** functions between `0x004afd40` and `0x004b0970`, four of which nothing
calls:

| Address | Name | What it does | Callers |
|---|---|---|---|
| `0x004afd40` | `Load8BitBMP2` | the loader the shell uses (head at `0x004afd40`, body `0x004afd8c`) | `0x004affe0` |
| `0x004affe0` | `Load8BitBMP2_File` | open a file by name, read it into a buffer, run the loader | none |
| `0x004b0060` | `Bmp8_UploadTall` | expand to 16-bit pixels with every row written twice and upload at (0,0) | none |
| `0x004b0320` | `Bmp8_Upload` | expand to 16-bit pixels and upload at (0,0) (body `0x004b035e`) | none |
| `0x004b0580` | `Clut_SetSemiTransparent` | set bit 15 of every non-zero colour of a 256-colour palette row in VRAM | none |
| `0x004b0600` | `Screen_FillRect` | append a flat quad to the draw list | the loading screen (`0x00449be2`) |
| `0x004b0800` | `Screen_SetDrawArea` | append a draw-area packet | the loading screen (`0x00449bbb`) |
| `0x004b0950` | `Screen_ResetPrimitives` | restart the 2D primitive slots | the loading screen (`0x00449ba6`) |
| `0x004b0970` | `Screen_DrawTexturedRect` | draw a textured quad at once | none |

(`0x004b0b10` is the compiler's static-initialiser glue for the object at `0x0056c580` and `0x004b0b20`, `ShellUtils_Tail`, was done in 4.10i2.)

## The loader

The input is a BMP file image in memory: width at `+0x12`, height at `+0x16`, depth at `+0x1c` (only the low words are read), the colour count at `+0x2e` (0 means 256), the palette
from `+0x36` as four-byte B, G, R, pad entries, then the rows, bottom-up, each padded to four bytes. A depth other than 8 logs `0x00537b40` and returns 0 with the out pointer cleared.

* **Palette.** Each channel byte is *sign-extended to 16 bits* before it is compared, so "above `0xf7`" means "at or above `0x80`". An entry with blue and red at or above `0x80` and green
  exactly zero is the transparent colour: it is stored as 0 and its index is remembered (the last one wins; 0 when there is none). Every other entry is shifted down three bits (16-bit
  logical shifts, so a channel at `0x80` or above keeps its sign bits and overflows into the next channel when packed) and packed `(blue << 10) + (green << 5) + red` modulo 65536;
  an entry that comes out all zero becomes `0x8000` (black that is not transparent).
* **The palette goes to VRAM** through `GetFree256Slot` (`0x0048a590`): x is the global `0x0056b6fc`, y the slot's answer plus the global `0x0056b2ec`; a 256x1 rectangle is
  uploaded with `0x004e8770` and waited for. The out word is the clut `(y << 6) | ((x >> 4) & 0x3f)`. The palette array is a stack local: entries past the colour count are whatever the
  stack held, so the replay records those bytes of the upload as zero (the native text is zero there too).
* **Pixels.** The buffer (`height * stride`, stride = width rounded up to four; `FileIO_AllocLoadedBuffer(size, 1, 1, 0)`) gets the rows top-down (the file's first row goes last), each
  row's padding filled with the transparent index. The BMP buffer is freed. The out cells receive the buffer, the stride, the height and the clut.
* The file wrapper (`0x004affe0`) asks `FileIO_OpenResource` for the size (a size that is not positive logs `0x0052d9a8`, an allocation that fails logs `0x0052d958`), reads the file
  into a buffer, closes it and calls the loader, returning 1 even when the loader refused the depth.

**The two dead variants** do the same palette and row work but expand the pixels in place through the palette (word `j` = palette entry of byte `j`, last first) and upload the 16-bit
image at (0, 0): `Bmp8_Upload` into a buffer of two bytes a pixel and a rectangle of the picture's own size; `Bmp8_UploadTall` writes **every row twice** (the picture is twice as
tall, `2 * height` rows), into four bytes a pixel, with a wait for the GPU before the upload and a vertical sync after it. The index is a *signed* byte (`movsx`): an index at or above
`0x80` (and so the transparent index that fills the row padding, when it is that high) reads below the palette array, in stack leftovers. The game's own pictures never get there
(nothing calls these functions); the port reads such a pixel as zero, and the probe keeps every index and the transparent index below `0x80` for these two.

## The 2D primitives

All three share the draw list of the renderer (cursor `0x00560fd0`, end `0x00568628`) and the **overlay slot** `0x00537b3c`: `Screen_ResetPrimitives` sets it to `0xfff` (and clears
`0x0056c57c`, which nothing reads); every primitive is linked into the viewport's ordering table at that slot (`table[slot * 8]` is the head; the packet's first word takes the old head)
and moves the slot down one. The packet's depth float is `-1.0` when the renderer's main-list pointer (`+0x84`) and table pointer (`+0x88`) are the same cell; otherwise it is the
viewport's depth word (`u16` at `[0x00563a38] + 8`) divided by `4 * slot` when that is above the threshold `0x00518484` (zero in the data), else the depth word alone. The x87 runs at 24
bits, so the division is a float division.

* `Screen_FillRect(x, y, w, h, r, g, b)`: nothing when the cursor is past the end (it may *equal* the end: the check is `ja`). A 0x1c-byte packet: colour bytes, code `0x28`, four vertices
  scaled from the 512x240 screen (`x * width >> 9`, `y * height / 240`, from the sign-extended low words of the coordinates and of the sums `x + w`, `y + h`), depth float at `+0x18`.
* `Screen_SetDrawArea(x, y, w, h)`: a 0x14-byte packet written by the GPU packet routine `0x004e87f0` from a rectangle of the four low words; depth float at `+0x10`. The cursor moves
  after the call.
* `Screen_DrawTexturedRect(rect, x, y, depth, clut)` draws at once (`0x004e87e0`, then a draw sync) a 0x28-byte quad of code `0x2d`. `rect` is a texture record: `+0` and `+2` the
  texture position and the page bits, `+4` the width, `+6` the height. At depth `0x10` the quad is `width` wide and `height` high with the page word built from `0x4000`; any other depth
  is an 8-bit texture: x extent and u coordinates double, the page word is built from `0x2000` and the low word of `clut` goes in the packet. The packet is built in a local: its tag,
  the clut word of the 16-bit form and the two pad words are leftovers, recorded as zero.

## Retail facts the replay established

* Ghidra names the tails; the real functions start 0x4c / 0x5e bytes earlier. `tony ghidra sync` creates `0x004afd40` and `0x004b0320` from the curated names and the old
  `Load8BitBMP2_Body` name is gone.
* The out cells are 32-bit except the clut (16-bit): the replay's memory diff sees the other two bytes of the clut cell untouched.
