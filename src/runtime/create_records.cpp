#include "auto_views.hpp"
#include "create_records.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

void put_text(RetailMemory& m, std::uint32_t at, const std::string& text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m.write(at, bytes);
}

}  // namespace

int CustomSkaters::rnd(int limit) {
    s_.note(kRnd, {static_cast<std::uint32_t>(limit)});
    return s_.value(kRnd);
}

// The live career arrays name a character as available: the two players' entries that hold it are
// reset, and the character's costume, mapping and enabled flag are set.
void CustomSkaters::enable_live_character(int character) {
    for (std::uint32_t i = 0; i < 2; ++i) {
        if (byte_at(m_, kLiveCharacters + i) == static_cast<std::uint8_t>(character)) {
            set_byte_at(m_, kLivePlayerCostume + i, 0);
            set_byte_at(m_, kLivePlayerCharacter + i, byte_at(m_, kLiveCharacters + i));
            set_byte_at(m_, kLivePlayerEnabled + i, 1);
        }
    }
    const auto c = static_cast<std::uint32_t>(character);
    set_byte_at(m_, kLiveCostume + c, 0);
    set_byte_at(m_, kLiveMapped + c, static_cast<std::uint8_t>(character));
    set_byte_at(m_, kLiveEnabled + c, 1);
}

void CustomSkaters::init_record(int slot, std::uint32_t texture_manager, std::uint32_t trick_manager) {
    s_.check(texture_manager != 0, kMsgNoTextureDb);
    s_.check(trick_manager != 0, kMsgNoTrickDb);
    s_.check(global_word(m_, kTextureManager) == 0, kMsgPossibleLeak);
    set_global_word(m_, kTextureManager, texture_manager);
    s_.check(slot >= 0 && slot < 4, kMsgOutOfRange);
    s_.check(global_word(m_, kTextureManager) != 0, kMsgNoTextureDb);
    s_.check(slot < 4, kMsgTooManyCustom);
    const std::uint32_t rec = record(slot);
    put_text(m_, rec + kRecName, std::string("Skater ") + static_cast<char>('A' + slot));
    put_text(m_, rec + kRecHometown, "Woodland Hills");
    set_byte_at(m_, rec + kRecBuild, 1);
    set_byte_at(m_, rec + kRecSex, 0);
    set_byte_at(m_, rec + kRecHeight, 0x12);
    set_byte_at(m_, rec + kRecSpare, 5);
    m_.fill(rec + kRecStats, 0, 40);
    set_byte_at(m_, rec + kRecScale, 100);
    set_byte_at(m_, rec + kRecFlag, 1);
    set_byte_at(m_, rec + kRecValid, 0);
    m_.fill(rec + kRecColours, 0, 4);
    m_.fill(rec + kRecSets, 0, 28);
    const int character = sk_.map_enabled_index(slot);
    s_.note(kTrickInit, {static_cast<std::uint32_t>(character), trick_manager});
    enable_live_character(character);
    const std::uint32_t career = career_record(character);
    const std::uint32_t offsets = word_at(m_, kBuildRows + 4u * static_cast<std::uint32_t>(sbyte_at(m_, rec + kRecBuild)));
    for (std::uint32_t k = 0; k < 10; ++k) {
        auto stat = static_cast<std::int8_t>(byte_at(m_, career + kStatBytes + k) + byte_at(m_, offsets + k));
        set_byte_at(m_, career + kStatBytes + k, static_cast<std::uint8_t>(stat));
        while (stat > 10) {
            s_.assert_failed(kMsgStatOverflow);
            stat = static_cast<std::int8_t>(stat - 1);
            set_byte_at(m_, career + kStatBytes + k, static_cast<std::uint8_t>(stat));
            set_word_at(m_, rec + kRecStats + 4 * k, word_at(m_, rec + kRecStats + 4 * k) - 1);
            set_byte_at(m_, rec + kRecSpare, static_cast<std::uint8_t>(byte_at(m_, rec + kRecSpare) + 1));
            stat = sbyte_at(m_, career + kStatBytes + k);
        }
    }
    for (std::uint32_t kind = 0; kind < 7; ++kind) {
        s_.check(global_word(m_, kTextureManager) != 0, kMsgNoTextureSetDb);
        const std::uint32_t holder = global_word(m_, kTextureManager);
        set_word_at(m_, rec + kRecSets + 4 * kind, static_cast<std::uint32_t>(tex_.find_by_group(holder, static_cast<std::int32_t>(kind))));
        if (kind == 0) {
            set_word_at(m_, rec + kRecSets, static_cast<std::uint32_t>(
                                         tex_.step(holder, -1, int_at(m_, rec + kRecSets), -1)));
        } else if (kind == 2) {
            set_word_at(m_, rec + kRecSets + 8, word_at(m_, rec + kRecSets));
        }
    }
    set_global_word(m_, kTextureManager, 0);
    randomize(slot, texture_manager, trick_manager);
}

void CustomSkaters::randomize(int slot, std::uint32_t texture_manager, std::uint32_t trick_manager) {
    s_.check(texture_manager != 0, kMsgNoTextureDb);
    s_.check(trick_manager != 0, kMsgNoTrickDb);
    s_.check(global_word(m_, kTextureManager) == 0, kMsgPossibleLeak);
    set_global_word(m_, kTextureManager, texture_manager);
    s_.check(slot >= 0 && slot < 4, kMsgOutOfRange);
    const std::uint32_t rec = record(slot);
    if (slot == 0) {
        set_global_byte(m_, kCustomRecords + kRecSex, 0);
    } else {
        set_byte_at(m_, rec + kRecSex, static_cast<std::uint8_t>(rnd(2)));
    }
    set_byte_at(m_, rec + kRecBuild, static_cast<std::uint8_t>(rnd(3)));
    const int character = sk_.map_enabled_index(slot);
    const std::uint32_t career = career_record(character);
    const std::uint32_t offsets = word_at(m_, kBuildRows + 4u * static_cast<std::uint32_t>(sbyte_at(m_, rec + kRecBuild)));
    for (std::uint32_t k = 0; k < 10; ++k) {
        set_byte_at(m_, career + kStatBytes + k, static_cast<std::uint8_t>(byte_at(m_, skater::row(character) + skater::kRowStats + k) + byte_at(m_, offsets + k)));
    }
    const std::uint32_t holder_now = [&] { return global_word(m_, kTextureManager); }();
    for (std::uint32_t kind = 0; kind < 7; ++kind) {
        s_.check(global_word(m_, kTextureManager) != 0, kMsgNoTextureSetDb);
        set_word_at(m_, rec + kRecSets + 4 * kind, static_cast<std::uint32_t>(tex_.find_by_group(holder_now, static_cast<std::int32_t>(kind))));
        if (kind == 2) {
            set_word_at(m_, rec + kRecSets + 8, static_cast<std::uint32_t>(tex_.find_by_group(holder_now, 1)));
        }
    }
    auto step_set = [&](std::uint32_t at, std::int32_t root) {
        set_word_at(m_, at, static_cast<std::uint32_t>(tex_.step(global_word(m_, kTextureManager), root, int_at(m_, at), 1)));
    };
    for (int n = rnd(3); n > 0; --n) {
        step_set(rec + kRecSets, -1);
    }
    const std::int32_t root = int_at(m_, rec + kRecSets);
    for (int n = rnd(10); n > 0; --n) {
        step_set(rec + kRecSets, root);
    }
    for (std::uint32_t kind = 0; kind < 7; ++kind) {
        for (int n = rnd(10); n > 0; --n) {
            if (kind != 3 && kind != 0) {
                step_set(rec + kRecSets + 4 * kind, -1);
            }
        }
    }
    set_word_at(m_, rec + kRecSets + 8, word_at(m_, rec + kRecSets + 4));
    for (int n = rnd(0x14); n > 0; --n) {
        set_word_at(m_, rec + kRecSets + 8, static_cast<std::uint32_t>(tex_.step(global_word(m_, kTextureManager), int_at(m_, rec + kRecSets + 4),
                                                                           int_at(m_, rec + kRecSets + 8), 1)));
    }
    set_byte_at(m_, rec + kRecColours, static_cast<std::uint8_t>(rnd(0x24)));
    const auto second = static_cast<std::uint8_t>(rnd(0x24));
    set_byte_at(m_, rec + kRecColours + 1, second);
    set_byte_at(m_, rec + kRecColours + 2, second);
    set_byte_at(m_, rec + kRecColours + 3, static_cast<std::uint8_t>(rnd(0x24)));
    set_global_word(m_, kTextureManager, 0);
}

void CustomSkaters::save_colours(std::uint32_t editor, int slot) {
    const std::uint32_t rec = record(slot);
    for (std::uint32_t i = 0; i < 4; ++i) {
        set_byte_at(m_, rec + kRecColours + i, byte_at(m_, editor + kEditorColours + i));
    }
    set_word_at(m_, rec + kRecHue, word_at(m_, editor + kEditorHue));
}

void CustomSkaters::load_colours(std::uint32_t editor, int slot) {
    const std::uint32_t rec = record(slot);
    for (std::uint32_t i = 0; i < 4; ++i) {
        set_byte_at(m_, editor + kEditorColours + i, byte_at(m_, rec + kRecColours + i));
    }
    set_word_at(m_, editor + kEditorHue, word_at(m_, rec + kRecHue));
}

void CustomSkaters::copy_slot(std::uint32_t menu, int from, int to) {
    s_.check(from >= 0 && from <= 3, kMsgBadCopyIndex);
    s_.check(to >= 0 && to <= 3, kMsgBadCopyIndex);
    const std::uint32_t image = word_at(m_, menu + kMenuImage);
    m_.copy(image + 0x1a20 + static_cast<std::uint32_t>(to) * kCustomStride, record(from), kCustomStride);
    m_.copy(image + 0x1c64 + static_cast<std::uint32_t>(to) * 0x58, kStagedAppearance + static_cast<std::uint32_t>(from) * 0x58, 0x58);
    const int source = sk_.map_enabled_index(from);
    const std::uint32_t source_record = career_record(source);
    const int target = sk_.map_enabled_index(to);
    m_.copy(image + 4 + static_cast<std::uint32_t>(target) * skater::kCareerRecordStride, source_record,
            skater::kCareerRecordStride);
    m_.copy(kSaveImage, image, kSaveImageSize);
    enable_live_character(sk_.map_enabled_index(to));
}

void CustomSkaters::commit_image(std::uint32_t menu) { m_.copy(kSaveImage, word_at(m_, menu + kMenuImage), kSaveImageSize); }

}  // namespace opentony::runtime::create
