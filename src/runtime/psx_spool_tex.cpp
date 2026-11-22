#include "auto_views.hpp"
#include "psx_spool.hpp"

namespace opentony::runtime::psx {

using namespace opentony::runtime::autoviews;

namespace {

constexpr const char* kFindNode = "palette_find_node";       // Palette_FindSourceLookupNode(palette address): the node
constexpr const char* kGetCheat = "career_get_cheat";        // Career_GetCheat(0xd)
constexpr const char* kPrepare = "texture_prepare";          // D3DTex_PrepareTexture(record, palette offset, material, flag)

constexpr std::uint32_t kFlagBlockedChecksumA = 0xa61814c4;   // two materials (the shared board textures) are always tiled
constexpr std::uint32_t kFlagBlockedChecksumB = 0x68b7b136;

}  // namespace

void PsxSpool::load_texture_references(std::uint32_t slot_index, std::uint32_t flags) {
    const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(slot_index));
    s_.check(byte_at(m_, slot + kSlotTexturesInRam) != 0, kMsgTexturesNotInRam);
    // (a non-zero `flags` calls Options_NothingH, an empty function)
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    const std::uint32_t models = word_at(m_, word_at(m_, slot + kSlotModels) - 4);
    const std::uint32_t first = trailing_tables(raw) + models * 4;
    const std::uint32_t materials = first + 4;
    std::uint32_t tail = materials + word_at(m_, first) * 4;
    const std::uint32_t tail_count = word_at(m_, tail);
    const std::uint32_t wide_count = word_at(m_, tail + tail_count * 9 * 4 + 4);
    std::uint32_t records = word_at(m_, tail + tail_count * 9 * 4 + 4 + (wide_count * 0x81 + 1) * 4);
    std::uint32_t list = tail + (tail_count * 9 + wide_count * 0x81 + 3) * 4;
    for (; records != 0; --records) {
        const std::uint32_t record = raw + word_at(m_, list);
        list += 4;
        std::uint32_t record_flags = word_at(m_, record);
        const std::uint32_t colours = word_at(m_, record + 4);
        const std::uint32_t palette_offset = word_at(m_, record + 8);
        const std::uint32_t material_index = word_at(m_, record + 0xc);
        s_.check(colours == 0x10 || colours == 0x100, kMsgBadNumColours);
        const std::uint32_t material = word_at(m_, materials + material_index * 4);
        if (material == 0) {
            continue;
        }
        s_.note(kFindNode, {global_word(m_, kPaletteBase) + palette_offset});
        const std::uint32_t node = static_cast<std::uint32_t>(s_.value(kFindNode));
        if (byte_at(m_, material + 0x12) != 0) {
            s_.check((record_flags & 1) == 0 || byte_at(m_, material + 0x13) != 0, kMsgDuplicateTexture);
            continue;
        }
        if (word_at(m_, material + 0x18) == kFlagBlockedChecksumA) {
            record_flags |= 1;
        }
        if (word_at(m_, material + 0x18) == kFlagBlockedChecksumB) {
            record_flags |= 1;
        }
        const std::uint32_t width = half_at(m_, record + 0x10);
        const std::uint32_t height = half_at(m_, record + 0x12);
        s_.note(kGetCheat, {0xd});
        const bool cheat = (s_.value(kGetCheat) & 0xff) != 0 && global_byte(m_, kPaletteFlag) != 0;
        s_.note(kPrepare, {record, palette_offset, material, cheat ? 1u : 0u});
        if (colours == 0x100 || colours == 0x10) {
            // Options_ReturnZeroG writes nothing through its two out pointers: the rectangle is built from a local nobody set and the argument
            // slot (`flags`); the words that depend on the local are stack leftovers
            set_half_at(m_, material + 0x20, 0);
            set_half_at(m_, material + 0x22, flags);
            unspecified_.emplace_back(material + 0x20, 2);
        }
        set_half_at(m_, node + 4, static_cast<std::uint32_t>(half_at(m_, node + 4) + 1));
        set_word_at(m_, material + 0x1c, node);
        set_half_at(m_, material + 2, half_at(m_, node));
        set_byte_at(m_, material, 0);
        set_byte_at(m_, material + 1, 0);
        const std::uint32_t tpage = (((colours == 0x100 ? 1u : 0u) << 5 | (flags & 0x200u)) << 2) | ((flags >> 4) & 0x10u);
        set_half_at(m_, material + 6, tpage);
        unspecified_.emplace_back(material + 6, 1);       // (its low nibble is a stack leftover)
        const std::uint8_t w1 = static_cast<std::uint8_t>(width - 1);
        const std::uint8_t h1 = static_cast<std::uint8_t>(height - 1);
        set_byte_at(m_, material + 4, w1);
        set_byte_at(m_, material + 5, 0);
        set_byte_at(m_, material + 8, 0);
        set_byte_at(m_, material + 9, h1);
        set_byte_at(m_, material + 10, w1);
        set_byte_at(m_, material + 11, h1);
        const std::uint32_t negative_height = 0u - height;
        const std::uint32_t negative_width = 0u - width;
        set_word_at(m_, material + 0xc, (((negative_height & 0xf8u) | 0xf8800000u) << 2) | ((static_cast<std::uint32_t>(static_cast<std::int32_t>(negative_width) >> 3)) & 0x1fu));
        set_byte_at(m_, material + 0x12, 1);
        set_byte_at(m_, material + 0x13, record_flags & 1);
    }
}

void PsxSpool::clear_texture_references(std::uint32_t slot_index) {
    const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(slot_index));
    s_.check(byte_at(m_, slot + kSlotTexturesShrunk) != 0, kMsgClearTexturesNotLoaded);
    s_.check(byte_at(m_, slot + kSlotTexturesInRam) != 0, kMsgTexturesInRamFlag);
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    const std::uint32_t models = word_at(m_, word_at(m_, slot + kSlotModels) - 4);
    const std::uint32_t first = trailing_tables(raw) + models * 4;
    const std::uint32_t materials = first + 4;
    const std::uint32_t tail = materials + word_at(m_, first) * 4;
    const std::uint32_t tail_count = word_at(m_, tail);
    const std::uint32_t wide_count = word_at(m_, tail + tail_count * 9 * 4 + 4);
    const std::uint32_t count_at = tail + tail_count * 9 * 4 + 4 + (wide_count * 0x81 + 1) * 4;
    std::uint32_t records = word_at(m_, count_at);
    std::uint32_t list = count_at + 4 * records;
    for (; records != 0; --records, list -= 4) {
        const std::uint32_t material = word_at(m_, materials + word_at(m_, raw + word_at(m_, list) + 0xc) * 4);
        if (material != 0) {
            // Options_NothingF(u16 +0x20, u16 +0x22) is an empty function
            set_byte_at(m_, material + 0x12, 0);
        }
    }
}

}  // namespace opentony::runtime::psx
