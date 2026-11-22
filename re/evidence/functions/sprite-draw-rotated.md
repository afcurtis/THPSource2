# The sprite class's rotated draw (slot 3)

Status: modeled (read from the disassembly; checked only at the session level)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00457940` (1 function)
Native: `src/runtime/render_image.cpp` (`RenderKit::sprite_draw_rotated`), row `Sprite_DrawRotated` in `re/config/standalone_natives_manual.yml`

## What is here

Slot 3 of the sprite class (thiscall: x, y, angle). Nothing calls it directly, so no symbol table had it; the cold boot of milestone 9 reached it through the main menu's wheel, which draws its
sprites with it. It queues the sprite as a textured quad (0x30 bytes, code 0x2c) turned by `angle` (Q12, asserted 0..0xfff) about its anchor and scaled by its Q12 scale (+0xc):
- the x axis is the cosine and the negated sine times a mode factor (+0x5c: 0 is 0x1000, 1 is 0xa00; another value reads a stack word retail never wrote, 0 in the native), the y axis the sine and the
  cosine times the factor, each product shifted 12 as retail does it;
- the anchor is the sprite's left / centre / right (+0xa) and top / middle / bottom (+0xb) of its width and height (+0x40, +0x42);
- the corners in Q12 go to whole units, then to the display as every layout primitive does (`screen_x` / `screen_y` of their 16-bit values); u and v run over the whole bitmap;
- the colour is +7..+9, semi-transparency +4 (code bit 1), shading off unless +5 (bit 0), the texture page from +0x58 and +6, the palette +0x5a, the depth -1.0, the image's texture record (+0x10)
  at +0x2c.

## Method and what is not shown

Transcribed from the disassembly. The `boot-menu` scenario, cold-booted in the standalone, equals retail's recording in every block of `.data` (`python -m tony.standalone_boot`), and the main
menu draws the wheel each frame of it, so the primitive cursor and the primitive count agree; the quads themselves are in zone memory, which the session hash does not cover. The mode factor's
third case has no deterministic retail answer. No function-level oracle yet.
