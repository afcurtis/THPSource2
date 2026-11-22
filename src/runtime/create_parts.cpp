#include "auto_views.hpp"
#include "create_parts.hpp"

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

// The part names the editor draws in fixed slots: string literals in .data.
constexpr std::uint32_t kLiteralPelvis = 0x00525f70;
constexpr std::uint32_t kLiteralStomach = 0x00525fd0;
constexpr std::uint32_t kLiteralChest = 0x00525fc8;
constexpr std::uint32_t kLiteralHead = 0x00525fd8;

}  // namespace

std::int32_t CreateParts::compare(std::uint32_t a, std::uint32_t b) {
    for (;; ++a, ++b) {
        const std::uint8_t x = byte_at(m_, a);
        const std::uint8_t y = byte_at(m_, b);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (x == 0) {
            return 0;
        }
    }
}

std::int32_t CreateParts::group_index(std::uint32_t name) {
    s_.check(name != 0, kMsgNoPartName);
    std::uint32_t at = kPartNameTable;
    for (std::int32_t group = 0; group < 4; ++group) {
        for (;; at += 4) {
            const std::uint32_t entry = word_at(m_, at);
            if (entry == 0) {
                break;
            }
            if (compare(entry, name) == 0) {
                return group;
            }
        }
        at += 4;   // past the list's NULL
    }
    s_.check(false, kMsgPartNotFound);
    return -1;
}

std::int32_t CreateParts::find_body_group(std::uint32_t database, std::uint32_t name, std::int32_t group) {
    s_.check(group >= 0 && group <= 3, kMsgBadBodyGroup);
    s_.check(name != 0, kMsgNoGroupName);
    const std::int32_t count = int_at(m_, database + kGroupCounts + 4u * static_cast<std::uint32_t>(group));
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t entry =
            database + kGroupNames + static_cast<std::uint32_t>(i) * 0x40u + static_cast<std::uint32_t>(group) * 0x10u;
        if (compare(name, entry) == 0) {
            return i;
        }
    }
    return -1;
}

std::int32_t CreateParts::classify(std::uint32_t editor, std::int32_t part, std::uint32_t& counter) {
    const std::uint32_t database = word_at(m_, editor + kEditorParts);
    const std::uint32_t name = database + kPartEntries + static_cast<std::uint32_t>(part) * kPartStride;
    if (compare(name, kLiteralPelvis) == 0) {
        return 0;
    }
    if (compare(name, kLiteralStomach) == 0) {
        return 1;
    }
    if (compare(name, kLiteralChest) == 0) {
        return 2;
    }
    if (compare(name, kLiteralHead) == 0) {
        return 3;
    }
    s_.check(static_cast<std::int32_t>(counter) < 0x13, kMsgTooManyParts);
    return static_cast<std::int32_t>(counter++);
}

std::int32_t CreateParts::classify_part(std::uint32_t editor, std::int32_t part, std::uint32_t counter) {
    s_.check(word_at(m_, editor + kEditorParts) != 0, kMsgNoPartDatabase);
    s_.check(counter != 0, kMsgNoModelCount);
    std::uint32_t value = word_at(m_, counter);
    const std::int32_t slot = classify(editor, part, value);
    set_word_at(m_, counter, value);
    return slot;
}

void CreateParts::build_part_map(std::uint32_t editor, std::uint32_t map) {
    s_.check(map != 0, kMsgNoPartMap);
    s_.check(word_at(m_, editor + kEditorParts) != 0, kMsgNoPartDatabase);
    std::uint32_t counter = 4;   // a local of the retail function, first free slot after the four fixed ones
    const auto parts_of_region = [&] {
        const std::uint32_t database = word_at(m_, editor + kEditorParts);
        return kRegionTable + word_at(m_, database + kPartRegion) * kRegionStride;
    };
    for (std::uint32_t i = 0; i < half_at(m_, parts_of_region() + kRegionParts); ++i) {
        set_word_at(m_, map + 8 * i + 4, 0xffffffffu);
        set_word_at(m_, map + 8 * i, i);
        const std::uint32_t models = word_at(m_, parts_of_region() + kRegionModels);
        s_.check(i < 0x78, kMsgTooManyParts);
        const std::uint32_t model = word_at(m_, models + 4 * i);
        if ((byte_at(m_, model) & 0x20) == 0) {
            const std::uint32_t animation = word_at(m_, word_at(m_, editor + kEditorParts) + 0x18 + i * kPartStride);
            const std::int32_t slot = classify(editor, static_cast<std::int32_t>(i), counter);
            set_word_at(m_, map + 8 * i + 4, static_cast<std::uint32_t>(slot));
            set_word_at(m_, editor + kEditorSlots + 4u * static_cast<std::uint32_t>(slot), animation);
        }
    }
}

std::int32_t CreateParts::checked_region(std::uint32_t editor) {
    s_.check(int_at(m_, editor + kEditorRegion) != -1, kMsgBadRegion);
    return int_at(m_, editor + kEditorRegion);
}

std::uint32_t CreateParts::region_record(std::uint32_t editor) {
    return kRegionTable + word_at(m_, editor + kEditorRegion) * kRegionStride;
}

void CreateParts::free_palettes(std::uint32_t editor) {
    if (word_at(m_, editor + kEditorPalette) != 0) {
        s_.note(kMemDelete, {word_at(m_, editor + kEditorPalette)});
        set_word_at(m_, editor + kEditorPalette, 0);
    }
    m_.fill(editor + kEditorPalette + 4, 0, 36);
}

}  // namespace opentony::runtime::create
