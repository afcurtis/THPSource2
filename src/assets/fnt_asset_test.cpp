#include "fnt_asset.hpp"

#include <array>
#include "tests/test_assets.hpp"
#include "tests/test_check.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace {

void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

std::vector<std::byte> make_font() {
    constexpr std::size_t count = 2;
    constexpr std::size_t palette_offset = 4 + count * 0x10;
    constexpr std::size_t glyph_data_offset = palette_offset + 0x20;
    // Glyph 0: stride 1, 2 rows -> 4 bytes. Glyph 1: stride 3, 1 row -> 3 units is
    // odd -> 6 bytes + 2 padding. The pixels total 12 bytes.
    std::vector<std::byte> bytes(glyph_data_offset + 12, std::byte{0});
    put32(bytes, 0, count);
    put32(bytes, 4, 1);
    put32(bytes, 8, 2);
    put32(bytes, 12, 3);
    put32(bytes, 16, 4);
    put32(bytes, 20, 3);
    put32(bytes, 24, 1);
    put32(bytes, 28, 5);
    put32(bytes, 32, 6);
    bytes[palette_offset + 0] = std::byte{0x11};
    for (std::size_t index = 0; index < 12; ++index) {
        bytes[glyph_data_offset + index] = static_cast<std::byte>(0xe0 + index);
    }
    return bytes;
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void align4(std::vector<std::byte>& bytes) {
    while ((bytes.size() & 3U) != 0U) {
        bytes.push_back(std::byte{0});
    }
}

std::vector<std::byte> make_pre_with_font(const std::vector<std::byte>& font) {
    std::vector<std::byte> bytes;
    append_u32(bytes, 1);
    for (const char* cursor = "s2tricks.fnt"; *cursor != '\0'; ++cursor) {
        bytes.push_back(static_cast<std::byte>(*cursor));
    }
    bytes.push_back(std::byte{0});
    align4(bytes);
    append_u32(bytes, static_cast<std::uint32_t>(font.size()));
    bytes.insert(bytes.end(), font.begin(), font.end());
    align4(bytes);
    return bytes;
}

} // namespace

int main() {
    std::vector<std::byte> bytes = make_font();

    const opentony::assets::FntRuntimeFont font =
        opentony::assets::FntRuntimeFont::parse(
            std::move(bytes), "font.fnt", 0x1234);
    CHECK(font.glyph_count() == 2);
    CHECK(font.records()[0].words[0] == 1);
    CHECK(font.records()[0].words[3] == 4);
    CHECK(font.palette_bytes()[0] == std::byte{0x11});
    CHECK(font.packed_glyph_data().size() == 12);
    CHECK(font.packed_glyph_data()[4] == std::byte{0xe4});
    CHECK(font.entries().size() == 3);
    CHECK(font.entries()[0].source_byte_0 == 4);
    CHECK(font.entries()[0].source_byte_1 == 2);
    CHECK(font.entries()[0].source_byte_2 == 3);
    CHECK(!font.entries()[0].sentinel);
    // Glyph cursor arithmetic of Font_Parse: 2 * stride * rows, plus 2 when odd.
    CHECK(font.entries()[0].packed_offset == 0);
    CHECK(font.entries()[0].packed_size == 4);
    CHECK(font.entries()[1].packed_offset == 4);
    CHECK(font.entries()[1].packed_size == 8);
    // The sentinel copies record 0's bytes and is built from a blank buffer.
    CHECK(font.entries()[2].sentinel);
    CHECK(font.entries()[2].source_byte_0 == 4);
    CHECK(font.entries()[2].source_byte_1 == 2);
    CHECK(font.entries()[2].source_byte_2 == 3);
    CHECK(font.sentinel_blank_bytes() == 4);
    CHECK(opentony::assets::FntRuntimeFont::clut_word(0, 0, 91) == 0x16c0);
    CHECK(opentony::assets::FntRuntimeFont::clut_word(0x40, 3, 1) == (4 | (4 << 6)));
    CHECK(font.atlas_format_word() == 0x1234);
    CHECK(font.font_allocation_size() == 0x14c);
    CHECK(font.entry_table_allocation_size() == 3 * 8);
    CHECK(font.glyph_image_allocation_size() == 3 * 0x5c);

    opentony::assets::PreRuntimeManager pre;
    pre.load("PANEL.PRE", make_pre_with_font(make_font()));
    opentony::assets::FntRuntimeManager fonts;
    const std::size_t slot = fonts.load_embedded(
        pre, "s2tricks.fnt", "S2TRICKS.FNT", 0x1234);
    CHECK(slot == 0);
    CHECK(fonts.loaded_count() == 1);
    const auto* found = fonts.find("s2tricks.fnt");
    CHECK(found != nullptr);
    CHECK(found->glyph_count() == 2);
    const auto text = fonts.text_view("s2tricks.fnt");
    CHECK(text.glyph_count == 2);
    CHECK(text.atlas_format_word == 0x1234);
    CHECK(text.entries.size() == 3);
    CHECK(text.entries[0].source_byte_0 == 4);
    CHECK(text.entries[2].sentinel);
    CHECK(&fonts.font(slot) == found);
    CHECK(fonts.font_name(slot) == "s2tricks.fnt");
    pre.unload("PANEL.PRE");
    CHECK(fonts.find("s2tricks.fnt") == found);
    CHECK(fonts.text_view("s2tricks.fnt").entries.back().sentinel);
    fonts.unload("s2tricks.fnt");
    CHECK(fonts.loaded_count() == 0);

    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "opentony_fnt_runtime_test.fnt";
    const std::vector<std::byte> disk_bytes = make_font();
    {
        std::ofstream output(temp_path, std::ios::binary);
        CHECK(output);
        output.write(
            reinterpret_cast<const char*>(disk_bytes.data()),
            static_cast<std::streamsize>(disk_bytes.size()));
    }
    const std::size_t direct_slot = fonts.load_file(
        "direct.fnt", temp_path.string(), 0x4321);
    CHECK(fonts.font(direct_slot).atlas_format_word() == 0x4321);
    fonts.unload("direct.fnt");
    std::filesystem::remove(temp_path);

    bool rejected = false;
    opentony::assets::FntRuntimeManager capacity;
    for (unsigned index = 0; index < opentony::assets::kRuntimeFontSlotCount; ++index) {
        capacity.load("F" + std::to_string(index), make_font());
    }
    CHECK(capacity.loaded_count() == opentony::assets::kRuntimeFontSlotCount);
    rejected = false;
    try {
        capacity.load("overflow", make_font());
    } catch (const opentony::assets::FntFormatError&) {
        rejected = true;
    }
    CHECK(rejected);

    // Font_Find / Font_Unload compare names byte for byte (0x0044b000, 0x0044b0a0).
    {
        opentony::assets::FntRuntimeManager exact;
        exact.load("Case.fnt", make_font());
        CHECK(exact.find("case.fnt") == nullptr);
        CHECK(exact.find("Case.fnt") != nullptr);
        rejected = false;
        try {
            exact.unload("case.fnt");
        } catch (const opentony::assets::FntFormatError&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    // No duplicate check: a second load takes the next free slot, the first name
    // match wins, and an unloaded slot keeps its name so it shadows the duplicate.
    {
        opentony::assets::FntRuntimeManager dup;
        CHECK(dup.load("a.fnt", make_font()) == 0);
        CHECK(dup.load("a.fnt", make_font()) == 1);
        CHECK(dup.find("a.fnt") == &dup.font(0));
        dup.unload("a.fnt");
        CHECK(dup.loaded_count() == 1);
        CHECK(dup.find("a.fnt") == nullptr);
        CHECK(dup.load("b.fnt", make_font()) == 0);
        CHECK(dup.find("a.fnt") == &dup.font(1));
        dup.unload("a.fnt");
        CHECK(dup.loaded_count() == 1);
        CHECK(dup.find("a.fnt") == nullptr);
    }
    // Glyph pixels that would run past the file are rejected here (retail reads on).
    rejected = false;
    try {
        std::vector<std::byte> truncated = make_font();
        truncated.resize(truncated.size() - 1);
        (void)opentony::assets::FntRuntimeFont::parse(std::move(truncated), "truncated.fnt");
    } catch (const opentony::assets::FntFormatError&) {
        rejected = true;
    }
    CHECK(rejected);

    rejected = false;
    try {
        std::vector<std::byte> malformed = make_font();
        put32(malformed, 0, 0xffffffffU);
        (void)opentony::assets::FntRuntimeFont::parse(
            std::move(malformed), "malformed.fnt");
    } catch (const opentony::assets::FntFormatError&) {
        rejected = true;
    }
    CHECK(rejected);

    const std::filesystem::path retail_font =
        opentony::test::data_dir() / "S2TRICKS.FNT";
    if (opentony::test::have_file(retail_font)) {
        const auto retail = opentony::assets::FntRuntimeFont::load(retail_font.string());
        CHECK(retail.glyph_count() == 59);
        const std::array<std::uint32_t, 4> first_record{5, 14, 14, 19};
        CHECK(retail.records()[0].words == first_record);
        CHECK(retail.packed_glyph_data().size() == 9104 - 0x3d4);
        CHECK(retail.entries().size() == 60);
    }
    return opentony::test::exit_code();
}
