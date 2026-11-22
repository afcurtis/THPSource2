#include "auto_views.hpp"
#include "skater_manager.hpp"

#include <cstring>
#include <vector>

namespace opentony::runtime::skater {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kFace17 = 0x00537b84;  // pointer variable: "s2face17.bmp"
}  // namespace

// ---- the active slot --------------------------------------------------------------------------

void SkaterManager::set_active_slot(int slot) {
    check_slot(slot);
    set_global_word(m_, kManager + kActiveSlot, static_cast<std::uint32_t>(slot));
}

std::int32_t SkaterManager::active_slot() const { return global_int(m_, kManager + kActiveSlot); }

std::int32_t SkaterManager::current_character() const { return int_at(m_, slot_entry(active_slot())); }

std::int32_t SkaterManager::current_costume() const { return int_at(m_, slot_entry(active_slot()) + 8); }

std::int32_t SkaterManager::current_custom_slot() const { return int_at(m_, slot_entry(active_slot()) + 4); }

// ---- per slot ---------------------------------------------------------------------------------

std::int32_t SkaterManager::character_of_slot(int slot) {
    check_slot(slot);
    return int_at(m_, slot_entry(slot));
}

std::int32_t SkaterManager::costume_of_slot(int slot) {
    check_slot(slot);
    return int_at(m_, slot_entry(slot) + 8);
}

std::int32_t SkaterManager::slot_loaded_flag(int slot) {
    check_slot(slot);
    return int_at(m_, loaded(slot) + kLoadedFlags);
}

std::int32_t SkaterManager::select_slot(int slot) {
    // Retail: slot -1 means slot 0 ("the first player").
    return character_of_slot(slot == -1 ? 0 : slot);
}

void SkaterManager::select_current(int character) {
    s_.check(character >= 0 && character <= 0x13, kMsgOutOfRangeSkater);
    const int slot = active_slot();
    if ((row_flags(character) & kFlagCustom) != 0) {
        set_word_at(m_, slot_entry(slot) + 4, static_cast<std::uint32_t>(custom_index(character)));
    }
    if (int_at(m_, slot_entry(slot)) == character) {
        return;
    }
    set_word_at(m_, slot_entry(slot), static_cast<std::uint32_t>(character));
    const std::uint32_t costume = word_at(m_, kManager + kRememberedCostume + static_cast<std::uint32_t>(character) * 4);
    set_word_at(m_, slot_entry(active_slot()) + 8, costume);
    check_costume(character, static_cast<std::int32_t>(costume));
}

void SkaterManager::select_character(int slot, int character) {
    check_slot(slot);
    if ((row_flags(character) & kFlagCustom) != 0) {
        set_word_at(m_, slot_entry(slot) + 4, static_cast<std::uint32_t>(custom_index(character)));
    }
    if (int_at(m_, slot_entry(slot)) == character) {
        return;
    }
    set_word_at(m_, slot_entry(slot), static_cast<std::uint32_t>(character));
    const std::uint32_t costume = word_at(m_, kManager + kRememberedCostume + static_cast<std::uint32_t>(character) * 4);
    set_word_at(m_, slot_entry(slot) + 8, costume);
    check_costume(character, static_cast<std::int32_t>(costume));
}

void SkaterManager::select_character_checked(int character, int slot) {
    s_.check(character >= 0 && character <= 0x13, kMsgBadSkaterId);
    select_character(slot, character);
}

void SkaterManager::select_costume(int slot, int costume, bool wrap) {
    check_slot(slot);
    const int count = count_costume_names(int_at(m_, slot_entry(slot)));
    if (wrap && count <= costume) {
        costume = 0;
    }
    set_word_at(m_, slot_entry(slot) + 8, static_cast<std::uint32_t>(costume));
    set_word_at(m_, kManager + kRememberedCostume + word_at(m_, slot_entry(slot)) * 4, static_cast<std::uint32_t>(costume));
}

void SkaterManager::set_custom_slot(int slot, int custom) {
    check_slot(slot);
    s_.check(custom >= 0 && custom <= 3, kMsgOutOfRangeCustom);
    set_word_at(m_, slot_entry(slot) + 4, static_cast<std::uint32_t>(custom));
}

std::int32_t SkaterManager::remembered_costume(int character) {
    return int_at(m_, kManager + kRememberedCostume + static_cast<std::uint32_t>(character) * 4);
}

void SkaterManager::release_all_loaded() {
    for (int slot = 0; slot < 8; ++slot) {
        const std::uint32_t flag = loaded(slot) + kLoadedFlags;
        if (word_at(m_, flag) != 0) {
            select_character_checked(0, slot);
        }
        set_word_at(m_, flag, 0);
    }
}

void SkaterManager::release_active_loaded() {
    const int slot = active_slot();
    if (word_at(m_, loaded(slot) + kLoadedFlags) != 0) {
        select_character_checked(0, slot);
    }
    set_word_at(m_, loaded(active_slot()) + kLoadedFlags, 0);
}

void SkaterManager::load_custom_into_active(int custom) {
    const int slot = active_slot();
    set_word_at(m_, loaded(slot) + kLoadedFlags, 1);
    const int character = map_enabled_index(custom);
    const std::uint32_t record = kCareerRecords + static_cast<std::uint32_t>(character) * kCareerRecordStride;
    m_.copy(kManager + (static_cast<std::uint32_t>(active_slot()) + 1) * kLoadedStride, record + 0xcc, 0x35);
    m_.copy(loaded(active_slot()) + kLoadedRecord, kCustomRecords + static_cast<std::uint32_t>(custom) * kCustomStride,
            kCustomStride);
}

void SkaterManager::format_name(int slot, std::uint32_t out) {
    check_slot(slot);
    s_.check(out != 0, kMsgNoReturnBuffer);
    const int character = character_of_slot(slot);
    std::uint32_t source;
    if (word_at(m_, loaded(slot) + kLoadedFlags) != 0) {
        source = loaded(slot) + kLoadedRecord;
    } else if ((row_flags(character) & kFlagCustom) != 0) {
        source = kCustomRecords + static_cast<std::uint32_t>(custom_index(character)) * kCustomStride;
    } else {
        source = row(character) + kRowName;
    }
    // sprintf(out, "%s", source)
    std::string text = m_.cstring(source);
    text.push_back('\0');
    m_.write(out, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::uint32_t SkaterManager::face_bitmap(int slot) {
    check_slot(slot);
    const int character = character_of_slot(slot);
    if (word_at(m_, loaded(slot) + kLoadedFlags) == 0 && (row_flags(character) & kFlagCustom) == 0) {
        if (character == 0 && costume_of_slot(slot) == 2) {
            return global_word(m_, kFace17);
        }
        return word_at(m_, row(character) + kRowFace);
    }
    return word_at(m_, row(map_enabled_index(0)) + kRowFace);
}

void SkaterManager::swap_custom_record(int slot) {
    check_slot(slot);
    s_.check(word_at(m_, loaded(slot) + kLoadedFlags) != 0, kMsgUnusedLoaded);
    const std::uint32_t a = kCustomRecords + static_cast<std::uint32_t>(slot) * kCustomStride;
    const std::uint32_t b = loaded(slot) + kLoadedRecord;
    const auto first = m_.bytes(a, kCustomStride);
    const auto second = m_.bytes(b, kCustomStride);
    m_.write(a, second);
    m_.write(b, first);
}

std::uint32_t SkaterManager::loaded_block(int slot) {
    check_slot(slot);
    s_.check((word_at(m_, loaded(slot) + kLoadedFlags) & 0xff) != 0, kMsgUnusedLoaded);
    return kManager + (static_cast<std::uint32_t>(slot) + 1) * kLoadedStride;
}

bool SkaterManager::slot_is_selectable(int slot) {
    check_slot(slot);
    const int character = character_of_slot(slot);
    if (word_at(m_, loaded(slot) + kLoadedFlags) != 0) {
        return byte_at(m_, loaded(slot) + 0x119) == 0;
    }
    if ((row_flags(character) & kFlagCustom) != 0) {
        const int custom = custom_index(character);
        return byte_at(m_, kCustomRecords + static_cast<std::uint32_t>(custom) * kCustomStride + kCustomFlagB) == 0;
    }
    return byte_at(m_, row(character) + kRowSlotFlag) == 0;
}

// ---- the character table ----------------------------------------------------------------------

int SkaterManager::count_costume_names(int character) const {
    const std::uint32_t flags = row_flags(character);
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        if (i == 2 && (flags & kFlagHidden3) != 0) {
            return count;
        }
        if (i == 3 && (flags & kFlagHidden4) != 0) {
            return count;
        }
        if (word_at(m_, row(character) + static_cast<std::uint32_t>(i) * 8) != 0) {
            ++count;
        }
    }
    return count;
}

std::uint32_t SkaterManager::select_model_name(int character, int costume) {
    check_costume(character, costume);
    return word_at(m_, row(character) + static_cast<std::uint32_t>(costume) * 8);
}

int SkaterManager::custom_index(int character) {
    s_.check((row_flags(character) & kFlagCustom) != 0, kMsgNotCustom);
    int index = 0;
    for (int i = 0; i < character; ++i) {
        if ((row_flags(i) & kFlagCustom) != 0) {
            ++index;
        }
    }
    return index;
}

int SkaterManager::map_enabled_index(int custom) {
    int character = 0;
    for (std::uint32_t at = row(0) + kRowFlags; at < 0x0053c1d4; at += kRowStride) {
        if ((byte_at(m_, at) & kFlagCustom) != 0) {
            if (custom < 1) {
                return character;
            }
            --custom;
        }
        ++character;
    }
    s_.assert_failed(kMsgOutOfRangeCustomIndex);
    return 0;
}

bool SkaterManager::custom_with_flag(int character) {
    if ((row_flags(character) & kFlagCustom) != 0) {
        const int custom = custom_index(character);
        if (byte_at(m_, kCustomRecords + static_cast<std::uint32_t>(custom) * kCustomStride + kCustomFlagA) != 0) {
            return true;
        }
    }
    return false;
}

std::uint32_t SkaterManager::display_name(int character) {
    s_.check(character >= 0 && character <= 0x13, kMsgEmpty);
    if ((row_flags(character) & kFlagCustom) != 0) {
        return kCustomRecords + static_cast<std::uint32_t>(custom_index(character)) * kCustomStride;
    }
    return row(character) + kRowName;
}

void SkaterManager::reset_active_player() {
    set_global_word(m_, 0x0056a888, 0);  // gActivePlayer
    set_global_word(m_, 0x0056a884, 0);
}

}  // namespace opentony::runtime::skater
