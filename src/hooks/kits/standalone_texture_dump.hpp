#pragma once

#include "../../hal/video.h"

#include <windows.h>

#include <cstdint>

// OT_TEXTURE_DUMP=<dir>: the game's textures, written out as the video layer sees them (standalone_video.cpp calls these from its recording layer; with the option unset every
// call returns at once and nothing is read or written). docs/HOOKS.md, the switches table of "Window, input, sound and clock".
//
//   <dir>\<key>.png   the texels as RGBA (src/hal/texture_key.hpp: the key hashes what HalTextureCache hands the HAL)
//   <dir>\index.tsv   one row a key: key, width, height, format, alpha class (opaque / cutout / smooth), use (world / 2d / both / unknown: from the draws; some vertex with
//                     rhw != 1 is a world draw, a sprite is 2D), wrap (wrap / clamp / both / unknown), mod2x (yes / no / both / unknown: MODULATE_2X is a PlayStation texture),
//                     volatile (1 when a surface took other texels later), tag (the run that first saw it). Read at the start and rewritten whole, so runs share one folder
//                     one after another; never two at once into the same folder
//   <dir>\seen.tsv    appended: tag, key, how (draw: uploaded for a draw; walk: found only by walking the surface store)
//   <dir>\volatile.tsv  appended: tag, handle, size, format, distinct keys, uploads, first key, last key; the last line of a (tag, handle) counts
//
//   OT_TEXTURE_DUMP_TAG=<text>   the run's name in seen.tsv and the tag column (default "run")
//   OT_TEXTURE_DUMP_WALK=<n>     textures the camera has not seen are never uploaded: after the store has held the same number of surfaces for n presents, every surface of
//                                the store with a texture format is dumped as well (default 120; 0 = never)
namespace texdump {

// true when OT_TEXTURE_DUMP is set and the directory can be used. `kits` is the standalone DLL (the store walk's exports).
bool start(HMODULE kits);

void on_texture_create(OtHalHandle handle, const OtHalTextureDesc* desc);
void on_texture_update(OtHalHandle handle, const OtHalRect* area, const void* pixels, std::int32_t pitch);
void on_texture_destroy(OtHalHandle handle);
void on_draw(const OtHalDrawState* state, const OtHalVertex* vertices, std::uint32_t count);
void on_sprite(const OtHalDrawState* state);
void on_present();

}  // namespace texdump
