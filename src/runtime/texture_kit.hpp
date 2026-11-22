#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::texturekit {

// Phase 5.4a, continued (7b): the PC texture layer (pc\d3dtex.cpp) on the retail memory model, for a program with no DirectDraw. Retail creates a 16-bit system-memory
// surface per texture, locks it, writes the pixels the bitmap or the palette gives and blits it into the texture the device draws from. The same steps are written here
// from the disassembly (build/s4/asm_*.txt; the decompilation has the argument lists wrong for these functions: the surface is the first argument of the pixel
// uploads), with the COM calls reported as the named services of src/runtime/surface_store.* (`ddraw_create_surface`, `ddraw_surface_lock` ...). Word 0 of a texture
// record, which holds the surface COM pointer in retail, holds the store's handle.
//
// A failed call of the surface layer stops the program, as it does in retail (Debug_ReportError, D3D_Shutdown, exit), with the retail source line in the message.
//
// The record is the 0x2c-byte one of D3DTex_AllocRecord: +0 surface, +4 material, +8 pixels (or the file name when flag 0x40), +0xc checksum (or the pixels the
// software renderer keeps when flag 0x200), +0x10 flags (1 own pixels, 4 file copy, 8 24-bit bitmap, 0x10 no transparent pixel, 0x20 own palette, 0x40 named file,
// 0x80 keyed, 0x100 first palette entry is 0, 0x200), +0x14/0x16 width/height, +0x18/0x1a realized size, +0x1c/0x1e source size, +0x20 palette cache record,
// +0x24/+0x28 list links.

// ---- retail data the layer reads --------------------------------------------------------------------------------------------
constexpr std::uint32_t kSoftwareRenderer = 0x029da3a0;     // D3DSoftwareRenderer
constexpr std::uint32_t kPixelFormat = 0x006a04d8;          // the device's DDPIXELFORMAT (0x20 bytes) every surface is created with
constexpr std::uint32_t kPaletteHead = 0x006a0524;          // the palette cache record list
constexpr std::uint32_t kPowerOfTwo = 0x006a052c;           // the device wants power-of-two textures
constexpr std::uint32_t kSquare = 0x006a0530;               // ... square ones
constexpr std::uint32_t kMaxWidth = 0x006a0534;
constexpr std::uint32_t kMaxHeight = 0x006a0538;
constexpr std::uint32_t kMinWidth = 0x006a053c;
constexpr std::uint32_t kMinHeight = 0x006a0540;
constexpr std::uint32_t kSaveBitmaps = 0x006a0544;          // debug: write each uploaded surface as a BMP
constexpr std::uint32_t kLevelName = 0x006a0548;            // D3DLevelName
constexpr std::uint32_t kTextureMode = 0x006a6964;          // D3DTextureMode
constexpr std::uint32_t kConfiguredRoot = 0x00549f24;       // D3DConfiguredTextureRootEnabled (a byte)
constexpr std::uint32_t kEmptyName = 0x0055fe10;            // the name D3DLevelName has when no level is chosen
constexpr std::uint32_t kListHead = 0x006a051c;             // the texture records: first (links at +0x24 next, +0x28 previous)
constexpr std::uint32_t kListTail = 0x006a0520;
constexpr std::uint32_t kRecordCount = 0x006a04d4;
constexpr std::uint32_t kNoMaterialLink = 0x029da3a5;       // a byte: when set, DestroyRecord leaves the record's scene material alone
constexpr std::uint32_t kBackgroundSprite = 0x006a04c8;     // the full-screen sprite (the one made with the flag) while it exists
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
// The device's pixel format as bit counts and shifts per channel, in the order of a bitmap palette entry's bytes (B, G, R, 0): the shift 0x006a0510 of the first byte is 0 in
// the retail device's 1555 layout (blue in the low bits) and 0x006a0508 is the 10 of red. (RenderKit's palette conversion has the red and blue names the other way round.)
constexpr std::uint32_t kRedBits = 0x006a04f8;
constexpr std::uint32_t kGreenBits = 0x006a04fc;
constexpr std::uint32_t kBlueBits = 0x006a0500;
constexpr std::uint32_t kAlphaBits = 0x006a0504;
constexpr std::uint32_t kRedShift = 0x006a0508;
constexpr std::uint32_t kGreenShift = 0x006a050c;
constexpr std::uint32_t kBlueShift = 0x006a0510;
constexpr std::uint32_t kAlphaShift = 0x006a0514;

// ---- scratch for the locals of the retail functions (pcplatform::kScratchBase + 0x6000, 0x2f8 bytes) ----
// Retail keeps them on its stack. In the Kits' scratch window they need a place no native that is running around them uses: at kScratchBase + 0xc80 the
// surface description lay over the locals of RenderKit's bitmap image constructors (its kScratch + 0x80), which call D3DTex_LoadIndexedBitmap, and the
// call overwrote the bitmap's depth (milestone 9: the cursor sprites of the cold boot came out 16-bit). Above everything the other Kits use.
constexpr std::uint32_t kScratch = 0x7ff00800 + 0x6000;
constexpr std::uint32_t kPath = kScratch + 0x000;           // 0x100 bytes
constexpr std::uint32_t kFormat = kScratch + 0x100;         // a DDPIXELFORMAT
constexpr std::uint32_t kDescStaging = kScratch + 0x120;    // a DDSURFACEDESC2
constexpr std::uint32_t kDescTexture = kScratch + 0x1a0;
constexpr std::uint32_t kOut = kScratch + 0x220;
constexpr std::uint32_t kLock = kScratch + 0x230;
constexpr std::uint32_t kRect = kScratch + 0x2b0;
constexpr std::uint32_t kHeader = kScratch + 0x2c0;         // the bitmap's 14-byte file header
constexpr std::uint32_t kInfo = kScratch + 0x2d0;           // its 40-byte info header

class TextureKit final {
public:
    TextureKit(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- pc\d3dtex.cpp ----
    std::uint32_t open_image(std::uint32_t checksum, std::uint32_t name);                                            // 0x004d8df0: a backend handle, or -1
    void ensure_source_pixels(std::uint32_t record, std::uint32_t keep_surface);                                     // 0x004d8990
    std::uint32_t count_used_palette_entries(std::uint32_t record);                                                  // 0x004da260 D3DTex_CountUsedPaletteEntries
    void upload_rgb_pixels(std::uint32_t surface, std::uint32_t record);                                             // 0x004d9310
    void upload_indexed_pixels(std::uint32_t surface, std::uint32_t record, std::uint32_t palette);                  // 0x004d9630
    void upload_texture(std::uint32_t record, std::uint32_t palette_checksum, std::uint32_t keep_surface, std::uint32_t replace);   // 0x004d8f10
    // The record is returned as EAX would hold it only by accident in retail (the last call is operator delete); no retail caller reads it (they read the list head from
    // memory). It answers the new list head, which is what PcSystem::release_material_texture / destroy_all_textures model it as.
    std::uint32_t destroy_record(std::uint32_t record);                                                              // 0x004d8b50
    void gray_texture(std::uint32_t record);                                                                         // 0x004d9f50
    // The record (a new one when `existing` is 0) of a paletted bitmap held in memory as a BMP file image (`image`), or of the file `name` when one is found; a 24-bit file
    // makes it a bitmap record that keeps the name. `keep_surface` goes to the upload.
    std::uint32_t load_indexed_bitmap(std::uint32_t image, std::uint32_t existing, std::uint32_t keep_surface, std::uint32_t name);   // 0x004d9950
    // pc\d3dsprite.cpp: the tiles a surface larger than the device's texture limit is cut into, and their source rectangles; answers the tile count
    std::uint32_t build_tiles(std::uint32_t record, std::uint32_t out_tiles, std::uint32_t out_rectangles);          // 0x004d83a0
    // The sprite object (thiscall): +0 the bitmap's record, +4 the tile records (0x2c each), +8 their source rectangles (16 bytes each), +0xc the tile count, +0x10 the tiles'
    // scene materials (0x2c each, linked to the records), +0x14 the scale, +0x18 the software renderer's copy of the screen. `flag` makes it the background sprite.
    std::uint32_t sprite_create(std::uint32_t sprite, std::uint32_t image, std::uint32_t name, std::uint32_t flag);  // 0x004d74a0
    void sprite_destroy(std::uint32_t sprite);                                                                       // 0x004d78c0

private:
    // a retail function by address, and its answer
    std::uint32_t call(std::uint32_t address, std::vector<std::uint32_t> args = {});
    // a service of the surface layer: the HRESULT (or, for Release, the references left)
    [[nodiscard]] std::uint32_t surface_call(const char* name, std::vector<std::uint32_t> args);
    // the check retail makes after each DirectDraw call: an error is reported and the program ends
    [[noreturn]] void fail(std::uint32_t result, std::uint32_t line, const char* file) const;
    void check(std::uint32_t result, std::uint32_t line) const {
        if (result != 0) {
            fail(result, line, "pc\\d3dtex.cpp");
        }
    }
    void check_sprite(std::uint32_t result, std::uint32_t line) const {
        if (result != 0) {
            fail(result, line, "pc\\d3dsprite.cpp");
        }
    }
    // IUnknown::Release of a texture surface, and the SetTexture(0, NULL) that follows when one reference is left (the device's); the result retail tests
    [[nodiscard]] std::uint32_t release_surface(std::uint32_t surface);
    void make_surface_desc(std::uint32_t desc, std::uint32_t width, std::uint32_t height, std::uint32_t caps, std::uint32_t caps2);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace opentony::runtime::texturekit
