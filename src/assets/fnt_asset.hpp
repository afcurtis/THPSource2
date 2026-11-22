#pragma once

#include "pre_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opentony::assets {

class FntFormatError final : public std::runtime_error {
public:
    explicit FntFormatError(const std::string& message)
        : std::runtime_error(message) {}
};

inline constexpr std::size_t kRuntimeFontAllocationSize = 0x14c;
inline constexpr std::size_t kRuntimeFontGlyphEntrySize = 0x08;
inline constexpr std::size_t kRuntimeFontGlyphImageSize = 0x5c;

struct FntDiskGlyphRecord {
    std::array<std::uint32_t, 4> words{};
};

// One entry of the runtime font's +0x34 table (Font_Parse, 0x00449cd0). The three
// source bytes are record words 3, 1 and 2 truncated to a byte: the glyph's pixel
// width (byte 0), its row count (byte 1) and a third metric (byte 2). The record's
// word 0 is the row stride in 16-bit units; a glyph's pixels are 2 * stride * rows
// bytes, plus 2 when stride * rows is odd, and follow one another in the packed data.
struct RuntimeFontGlyphEntry {
    std::uint8_t source_byte_0{};
    std::uint8_t source_byte_1{};
    std::uint8_t source_byte_2{};
    std::size_t source_record{};
    bool sentinel{};
    // Offset and length of this glyph's pixels inside packed_glyph_data(). The
    // sentinel owns no packed pixels; retail builds it from a 0x12-filled buffer.
    std::size_t packed_offset{};
    std::size_t packed_size{};
};

struct FntTextRuntimeView {
    std::uint32_t glyph_count{};
    std::uint32_t atlas_format_word{};
    std::span<const RuntimeFontGlyphEntry> entries{};
};

// Bounded native representation of the runtime font constructor. It keeps
// source records/palette/glyph bytes value-owned and exposes the proven
// runtime table sizes and copied entry bytes without inventing the graphics
// library's glyph decoding or texture pointers.
class FntRuntimeFont final {
public:
    static FntRuntimeFont load(
        const std::string& path,
        std::uint32_t atlas_format_word = 0);
    static FntRuntimeFont parse(
        std::vector<std::byte> bytes,
        std::string source = {},
        std::uint32_t atlas_format_word = 0);

    [[nodiscard]] std::uint32_t glyph_count() const noexcept {
        return static_cast<std::uint32_t>(records_.size());
    }
    [[nodiscard]] const std::vector<FntDiskGlyphRecord>& records() const noexcept {
        return records_;
    }
    [[nodiscard]] const std::array<std::byte, 0x20>& palette_bytes() const noexcept {
        return palette_bytes_;
    }
    [[nodiscard]] std::span<const std::byte> packed_glyph_data() const noexcept {
        return packed_glyph_data_;
    }
    [[nodiscard]] const std::vector<RuntimeFontGlyphEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::uint32_t atlas_format_word() const noexcept {
        return atlas_format_word_;
    }
    // Byte count of the blank buffer (filled with 0x12) the sentinel glyph is
    // built from: record 0's stride * rows * 2.
    [[nodiscard]] std::size_t sentinel_blank_bytes() const noexcept {
        return sentinel_blank_bytes_;
    }
    // Font_Parse derives the value it stores at +0x3c (and hands every glyph as
    // the CLUT position) from the 16-colour palette allocator, not from the file
    // or the caller: (pal16x >> 4 & 0x3f) | (GetFree16Slot() + pal16y) << 6.
    [[nodiscard]] static constexpr std::uint16_t clut_word(
        std::int32_t pal16x,
        std::int32_t pal16y,
        std::int32_t free_slot) noexcept {
        return static_cast<std::uint16_t>(
            ((pal16x >> 4) & 0x3f)
            | (static_cast<std::uint32_t>(free_slot + pal16y) << 6));
    }
    [[nodiscard]] std::size_t font_allocation_size() const noexcept {
        return kRuntimeFontAllocationSize;
    }
    [[nodiscard]] std::size_t entry_table_allocation_size() const noexcept {
        return entries_.size() * kRuntimeFontGlyphEntrySize;
    }
    [[nodiscard]] std::size_t glyph_image_allocation_size() const noexcept {
        return entries_.size() * kRuntimeFontGlyphImageSize;
    }

private:
    std::string source_;
    std::uint32_t atlas_format_word_{};
    std::vector<FntDiskGlyphRecord> records_;
    std::array<std::byte, 0x20> palette_bytes_{};
    std::vector<std::byte> packed_glyph_data_;
    std::vector<RuntimeFontGlyphEntry> entries_;
    std::size_t sentinel_blank_bytes_{};
};

inline constexpr std::size_t kRuntimeFontSlotCount = 8;
inline constexpr std::size_t kRuntimeFontNameSize = 0x10;

// Native counterpart of the retail eight-slot font table. A font owns its
// parsed records and glyph data; loading an embedded PRE entry copies only the
// payload needed to construct that runtime object, matching the retail
// temporary-buffer lifetime.
//
// Retail (Font_Create 0x0044aea0, Font_Find 0x0044b000, Font_Unload 0x0044b0a0):
// a load takes the first slot with no font and never checks for a duplicate name;
// names compare byte for byte (case-sensitive); the first slot whose stored name
// matches wins, and unloading clears the font pointer but leaves the name behind,
// so a later find of that name returns "no font" even if a newer duplicate lives in
// a later slot.
class FntRuntimeManager final {
public:
    FntRuntimeManager() = default;

    std::size_t load(
        std::string name,
        std::vector<std::byte> bytes,
        std::uint32_t atlas_format_word = 0);
    std::size_t load_file(
        std::string name,
        const std::string& path,
        std::uint32_t atlas_format_word = 0);
    std::size_t load_embedded(
        const PreRuntimeManager& pre,
        std::string name,
        std::string_view resource_name,
        std::uint32_t atlas_format_word = 0);

    [[nodiscard]] const FntRuntimeFont* find(std::string_view name) const noexcept;
    // The entry span borrows the selected font and is invalid after unload of
    // that font or destruction of this manager.
    [[nodiscard]] FntTextRuntimeView text_view(std::string_view name) const;
    [[nodiscard]] const FntRuntimeFont& font(std::size_t slot) const;
    [[nodiscard]] const std::string& font_name(std::size_t slot) const;
    [[nodiscard]] std::size_t loaded_count() const noexcept { return loaded_count_; }

    void unload(std::string_view name);

private:
    struct Slot {
        std::string name;
        FntRuntimeFont font;
    };

    std::array<std::optional<Slot>, kRuntimeFontSlotCount> slots_{};
    std::array<std::string, kRuntimeFontSlotCount> names_{};
    std::size_t loaded_count_{};

    static void validate_font_name(std::string_view name);
};

} // namespace opentony::assets
