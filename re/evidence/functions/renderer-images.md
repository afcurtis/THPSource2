# The image classes (image.cpp)

Status: confirmed by retail forced calls (1,640 cases of the image functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004564b0`, `0x00456550`, `0x00456570`, `0x00456580`, `0x00456600`, `0x00456790`, `0x004568d0`, `0x00456990`, `0x004569a0`, `0x00456a00`, `0x00456a70`, `0x00456ae0`, `0x00456c60`, `0x00456c90`, `0x00456cb0`, `0x00456cd0`, `0x00456cf0`, `0x00456d10`, `0x00456d90`, `0x00456e10`, `0x00457090`, `0x00457120`, `0x00457130`, `0x00457150`, `0x00457160`, `0x004573e0`, `0x00457400`, `0x00457420`, `0x00457520`, `0x00457540`, `0x00457660`, `0x004576f0`, `0x00457810`, `0x00457820`, `0x00457830`, `0x00457870`, `0x004578a0`, `0x004578c0`, `0x00457e10`, `0x00457f30`, `0x004581a0` (41 functions) (slice `render-images`)
Native: `src/runtime/render_kit.hpp`, `render_image.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs4.py` (group `rim`); fixtures `re/evidence/fixtures/render-calls-rim*.txt`

## What is here

The 2D images of the front end and the loading screens. The base image object (vtable `0x00518a48`) is a screen object with a position, flags and the font lookup of its region; the *sliced image*
(`SlicedImage_Construct`, `SlicedImage_Draw`) is drawn at (x, y) in the 512 x 240 layout units, scaled by the image's Q12 scale (flags `0xa` and `0xb` centre the position on an axis) and has a palette
record (`SlicedImage_PaletteSource`, `SlicedImage_SetPalette`, `Image_BindPalette`, which takes the palette the last BMP load left, or asserts there is none); the *bitmap image* loads a BMP from a file
name or from data already in memory (the file's size and palette, then the format; a flag marks it for VRAM) and the *sprite* is the bitmap image loaded from a file with its own vtable; the *raw image* has
only a vtable and its destructors. The BMP helpers read the colour depth of a header (the short at +0x1c, asserted to be 4 or 8), load the palette of a 4- or 8-bit bitmap and answer its size and a
palette word, and `MakeTransparentClut2` reads a BMP file into a buffer and runs the loader for its depth. The flat rectangle primitives are built in the polygon arena (cursor `0x00560fd0`, limit
`0x00568628`) and linked in front of the first entry of the current buffer's ordering table, or in front of the far entry the sprites sort behind; the screen scale (`0x029da394`, `0x029da398`) turns
layout units into display pixels. Three more functions work on scene materials rather than images: loading a BMP into a material's texture (an 8-bit one may be flagged for the career cheat that shows
the transparent texels), releasing a region's textures (one use less for every material of every model of the region, the palette cleared to black) and filling a material's palette row in VRAM with
one colour (inverted for a 16-colour palette).

Four methods of the classes do nothing (`Image_Nothing456990`, `457120`, `457130`, `457150`), and the "VRAM based sprite" constructor only asserts that the kind is not supported.

## Method

Objects are built in the slab (a vtable of stub addresses at its start) and the probe calls constructors, destructors and methods on them. The BMP loaders read a header the probe pokes as a scripted
0x40-byte read; the file reads, the texture layer, the font lookup and the PSX primitive calls are services. Constructors that build an object in a local leave stack garbage in some bytes; those bytes
are marked unspecified in the replay (the clut word of a bitmap image derives from a stack address, for instance).

## Lessons

(1) The clut word of the bitmap and sprite constructors is derived from the address of a local: the replay cannot match it byte for byte, so the adapter declares the word unspecified and compares the rest.
(2) The BMP generators avoid the error path of a file-size mismatch, which asserts and exits in retail. (3) Thiscall methods reach the object through ecx; the fixtures record it as `this`, and a thiscall
service called from a method (the base constructors) is recorded with the object as its first argument.
