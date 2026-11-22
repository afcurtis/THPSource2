#pragma once

#include "create_color.hpp"
#include "create_services.hpp"
#include "create_textures.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <optional>

namespace opentony::runtime {

// Phase 4.9: how the editor recolours a part's palette (create.cpp). The editor object keeps, for each of
// nine texture kinds, a pointer to the engine's palette (+0x140 + kind * 4), a buffer of the palette in
// hue/saturation/value form (+0x8c + kind * 4, see create_color.hpp) and a colour shift (+0x164 + kind *
// 0x10: hue delta, saturation and value scales). Choosing a colour preset turns it into a shift relative to
// the average colour of the palette; the shift is applied to the saved palette and stored in the engine.
namespace create {

constexpr std::uint32_t kBitmapHues = 0x00525e10;      // { name pointer, hue } pairs ended by a NULL name

constexpr std::uint32_t kObjPalettes = 0x140;          // + kind * 4: the engine palette of a texture kind
constexpr std::uint32_t kObjSaved = 0x8c;              // + kind * 4: the saved HSV palette buffer
constexpr std::uint32_t kObjShift = 0x164;             // + kind * 0x10: hue delta, +4 saturation, +8 value
constexpr std::uint32_t kObjName = 0x288;              // + kind * 0x10: the texture's bitmap name (8th character at +0x28f)
constexpr std::uint32_t kObjPelvisPalette = 0x1f4;
constexpr std::uint32_t kObjPelvisMaterial = 0x1f8;
constexpr std::uint32_t kObjCustomSlot = 0x284;
constexpr std::uint32_t kObjColourPresets = 0x318;     // + kind: four colour preset indices
constexpr std::uint32_t kMaterialPalette = 0x1c;       // a scene material's palette pointer

constexpr std::uint32_t kEditorTextureManager = 0x00561cb4;   // the editor's texture-set manager (kTextureManager is the working one)

constexpr const char* kFindMaterial = "find_material";       // PSX_FindSceneMaterial(checksum) -> material (scripted)

constexpr std::uint32_t kMsgInvalidTexpart = 0x005263ec;
constexpr std::uint32_t kMsgNoPelvisPalette = 0x005263d8;
constexpr std::uint32_t kMsgNoPaletteBuffer = 0x005263c4;
constexpr std::uint32_t kMsgTextureNotFound = 0x00526410;
constexpr std::uint32_t kMsgPaletteNotFound = 0x005264fc;

class CreatePalette final {
public:
    CreatePalette(RetailMemory& memory, RetailServices& services, CreateColor& color, TextureSets& textures)
        : m_(memory), s_(services), color_(color), tex_(textures) {}

    // 0x004226e0: the hue the table names for a bitmap (case-insensitive); true if the bitmap is in the table.
    bool bitmap_hue(std::uint32_t name, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out);
    // 0x00422790: average hue, saturation and value of kind `kind`'s saved palette (the first 0x80 colours
    // of the pelvis palette when `half` is set), overridden by the table for a few bitmaps.
    void average_hsv(std::uint32_t object, std::int32_t kind, std::uint32_t h_out, std::uint32_t s_out, std::uint32_t v_out,
                     std::uint32_t half);
    // 0x00422a80: set kind `kind`'s shift to move the average hue to `target` (-1: no shift).
    void set_shift(std::uint32_t object, std::int32_t kind, std::int32_t target, std::int32_t saturation, std::int32_t value,
                   std::uint32_t half);
    // 0x00422c10: apply the colour preset the editor chose for kind 1, 2, 3 or 6.
    void apply_preset(std::uint32_t object, std::int32_t kind, std::uint32_t half);
    // 0x004214f0: rebuild a kind's engine palette from its saved palette and shift.
    void update_animation(std::uint32_t object, std::int32_t kind, std::uint32_t half);
    // 0x00422460: bind a kind to the palette of the scene material named by `checksum` (0: unbind).
    void set_palette_handle(std::uint32_t object, std::int32_t kind, std::uint32_t checksum);

private:
    struct Average {
        std::int32_t h = 0;
        std::int32_t s = 0;
        std::int32_t v = 0;
    };
    [[nodiscard]] std::optional<std::uint32_t> lookup_bitmap_hue(std::uint32_t name);
    [[nodiscard]] Average average(std::uint32_t object, std::int32_t kind, std::uint32_t half);

    RetailMemory& m_;
    RetailServices& s_;
    CreateColor& color_;
    TextureSets& tex_;
};

}  // namespace create
}  // namespace opentony::runtime
