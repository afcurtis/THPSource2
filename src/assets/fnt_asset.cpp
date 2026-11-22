#include "fnt_asset.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace opentony::assets {
namespace {

[[nodiscard]] std::uint32_t u32(
    const std::vector<std::byte>& bytes,
    std::size_t offset,
    const std::string& source) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        throw FntFormatError("FNT u32 is truncated: " + source);
    }
    return static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(bytes[offset])
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U));
}

} // namespace

FntRuntimeFont FntRuntimeFont::load(
    const std::string& path,
    std::uint32_t atlas_format_word) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw FntFormatError("cannot open FNT file: " + path);
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        throw FntFormatError("cannot determine FNT file size: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input) {
            throw FntFormatError("cannot read FNT file: " + path);
        }
    }
    return parse(std::move(bytes), path, atlas_format_word);
}

FntRuntimeFont FntRuntimeFont::parse(
    std::vector<std::byte> bytes,
    std::string source,
    std::uint32_t atlas_format_word) {
    if (bytes.size() <= 0x28U) {
        throw FntFormatError("FNT file is too short: " + source);
    }
    const std::uint32_t count = u32(bytes, 0, source);
    if (count > (bytes.size() - 4U - 0x20U) / 0x10U) {
        throw FntFormatError("FNT record count is unreasonably large: " + source);
    }
    const std::size_t records_offset = 4U;
    const std::size_t palette_offset = records_offset
        + static_cast<std::size_t>(count) * 0x10U;
    if (palette_offset > bytes.size() || bytes.size() - palette_offset < 0x20U) {
        throw FntFormatError("FNT palette table is truncated: " + source);
    }

    FntRuntimeFont result{};
    result.source_ = std::move(source);
    result.atlas_format_word_ = atlas_format_word;
    result.records_.reserve(static_cast<std::size_t>(count));
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = records_offset + static_cast<std::size_t>(index) * 0x10U;
        FntDiskGlyphRecord record{};
        for (std::size_t word = 0; word < record.words.size(); ++word) {
            record.words[word] = u32(bytes, offset + word * 4U, result.source_);
        }
        result.records_.push_back(record);
    }
    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(palette_offset),
        result.palette_bytes_.size(),
        result.palette_bytes_.begin());
    const std::size_t glyph_data_offset = palette_offset + result.palette_bytes_.size();
    result.packed_glyph_data_.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(glyph_data_offset),
        bytes.end());

    result.entries_.reserve(static_cast<std::size_t>(count) + 1U);
    std::uint64_t cursor = 0;
    for (std::size_t index = 0; index < result.records_.size(); ++index) {
        const auto& record = result.records_[index].words;
        // 0x00449cd0: rows (record word 1, low byte) times the row stride (word 0),
        // a 32-bit product; the glyph is twice that many bytes, padded by 2 when odd.
        const std::uint32_t units = static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(record[1]) * record[0]);
        const std::uint64_t size = static_cast<std::uint64_t>(units) * 2U
            + ((units & 1U) != 0U ? 2U : 0U);
        if (cursor > result.packed_glyph_data_.size()
            || size > result.packed_glyph_data_.size() - cursor) {
            throw FntFormatError("FNT glyph data is truncated: " + result.source_);
        }
        result.entries_.push_back(RuntimeFontGlyphEntry{
            static_cast<std::uint8_t>(record[3]),
            static_cast<std::uint8_t>(record[1]),
            static_cast<std::uint8_t>(record[2]),
            index,
            false,
            static_cast<std::size_t>(cursor),
            static_cast<std::size_t>(size),
        });
        cursor += size;
    }
    // The extra entry is a blank glyph. Retail copies its three bytes from the file
    // header words at 0x10, 0x08 and 0x0c, which are record 0's words 3, 1 and 2 (the
    // first bytes after the count), and builds its image from a 0x12-filled buffer of
    // record 0's stride * rows * 2 bytes (the words at 0x04 and 0x08).
    result.entries_.push_back(RuntimeFontGlyphEntry{
        std::to_integer<std::uint8_t>(bytes[0x10]),
        std::to_integer<std::uint8_t>(bytes[0x08]),
        std::to_integer<std::uint8_t>(bytes[0x0c]),
        result.records_.size(), true, 0, 0});
    result.sentinel_blank_bytes_ = static_cast<std::size_t>(
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[0x08]) * u32(bytes, 4, result.source_))
        * 2U);
    return result;
}

void FntRuntimeManager::validate_font_name(std::string_view name) {
    if (name.empty()) {
        throw FntFormatError("FNT manager name is empty");
    }
    if (name.size() >= kRuntimeFontNameSize) {
        throw FntFormatError("FNT manager name exceeds the 16-byte slot");
    }
}

std::size_t FntRuntimeManager::load(
    std::string name,
    std::vector<std::byte> bytes,
    std::uint32_t atlas_format_word) {
    validate_font_name(name);
    std::size_t slot = slots_.size();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].has_value()) {
            slot = index;
            break;
        }
    }
    if (slot == slots_.size()) {
        throw FntFormatError("FNT manager has no free slots");
    }

    FntRuntimeFont font = FntRuntimeFont::parse(
        std::move(bytes), name, atlas_format_word);
    names_[slot] = name;
    slots_[slot].emplace(Slot{std::move(name), std::move(font)});
    ++loaded_count_;
    return slot;
}

std::size_t FntRuntimeManager::load_file(
    std::string name,
    const std::string& path,
    std::uint32_t atlas_format_word) {
    validate_font_name(name);
    std::size_t slot = slots_.size();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].has_value()) {
            slot = index;
            break;
        }
    }
    if (slot == slots_.size()) {
        throw FntFormatError("FNT manager has no free slots");
    }

    FntRuntimeFont font = FntRuntimeFont::load(path, atlas_format_word);
    names_[slot] = name;
    slots_[slot].emplace(Slot{std::move(name), std::move(font)});
    ++loaded_count_;
    return slot;
}

std::size_t FntRuntimeManager::load_embedded(
    const PreRuntimeManager& pre,
    std::string name,
    std::string_view resource_name,
    std::uint32_t atlas_format_word) {
    const std::optional<PreEmbeddedResourceView> resource =
        pre.find_embedded(resource_name);
    if (!resource.has_value()) {
        throw FntFormatError(
            "embedded FNT resource was not found: " + std::string(resource_name));
    }
    return load(
        std::move(name),
        std::vector<std::byte>(resource->payload.begin(), resource->payload.end()),
        atlas_format_word);
}

const FntRuntimeFont* FntRuntimeManager::find(std::string_view name) const noexcept {
    // The first stored name that matches wins, even when its font was unloaded.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (names_[index] == name) {
            return slots_[index].has_value() ? &slots_[index]->font : nullptr;
        }
    }
    return nullptr;
}

FntTextRuntimeView FntRuntimeManager::text_view(std::string_view name) const {
    const FntRuntimeFont* loaded = find(name);
    if (loaded == nullptr) {
        throw FntFormatError("FNT font is not loaded: " + std::string(name));
    }
    return FntTextRuntimeView{
        loaded->glyph_count(),
        loaded->atlas_format_word(),
        loaded->entries(),
    };
}

const FntRuntimeFont& FntRuntimeManager::font(std::size_t slot) const {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        throw FntFormatError("FNT manager slot is not loaded");
    }
    return slots_[slot]->font;
}

const std::string& FntRuntimeManager::font_name(std::size_t slot) const {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        throw FntFormatError("FNT manager slot is not loaded");
    }
    return slots_[slot]->name;
}

void FntRuntimeManager::unload(std::string_view name) {
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (names_[index] != name) {
            continue;
        }
        // A repeated unload finds the stale name and clears nothing further.
        if (slots_[index].has_value()) {
            slots_[index].reset();
            --loaded_count_;
        }
        return;
    }
    throw FntFormatError("FNT font is not loaded: " + std::string(name));
}

} // namespace opentony::assets
