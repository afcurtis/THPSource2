// The career save image (Phase 4.8): the functions that move the live career globals and options
// into the 0x1dc4-byte image the save file carries and back, and the custom-appearance copies.
// Retail addresses throughout; see career.hpp for the memory model and
// re/evidence/functions/career-save-runtime.md for the image layout.
#include "auto_views.hpp"
#include "career.hpp"

#include "trick_database.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <vector>

namespace opentony::runtime::career {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kImage = 0x00568a68;
constexpr std::uint32_t kFormatMagic = 0x101500;

// Image fields (retail addresses inside the image).
constexpr std::uint32_t kImgPlayerSkater = 0x0056a2d8;    // u8 x 2  (image +0x1870)
constexpr std::uint32_t kImgPlayerCostume = 0x0056a2da;   // u8 x 2  (+0x1872)
constexpr std::uint32_t kImgSecondA = 0x0056a2dc;         // u8 x 2  (+0x1874)
constexpr std::uint32_t kImgSecondC = 0x0056a2e0;         // u8 x 2  (+0x1878)
constexpr std::uint32_t kImgMusic = 0x0056a2e8;           // s32 (+0x1880)
constexpr std::uint32_t kImgSfx = 0x0056a2ec;             // s32 (+0x1884)
constexpr std::uint32_t kImgSecondB = 0x0056a688;         // u8 x 2  (+0x1c20)
constexpr std::uint32_t kImgPadA = 0x0056a3b8;            // 2 x 4 dwords: controller button maps (+0x1950)
constexpr std::uint32_t kImgAutoKick = 0x0056a3d8;        // AutoKickSetting, 2 dwords
constexpr std::uint32_t kImgVibration = 0x0056a3e0;       // VibrationSetting, 2 dwords
constexpr std::uint32_t kImgNameA = 0x0056a408;           // text copied from 0x0055fc5c
constexpr std::uint32_t kImgOptionA = 0x0056a46c;         // dword (+0x1a04)
constexpr std::uint32_t kImgOptionB = 0x0056a470;         // dword (+0x1a08)
constexpr std::uint32_t kImgTextLength = 0x0056a474;      // dword (+0x1a0c)
constexpr std::uint32_t kImgText = 0x0056a478;            // 11 bytes (+0x1a10)

// Live copies.
constexpr std::uint32_t kLivePlayerSkater = 0x0055faf8;   // u8 x 2
constexpr std::uint32_t kLiveCharacterCostume = 0x0055fafa;  // u8 x 20
constexpr std::uint32_t kLiveCharacterCustom = 0x0055fb0e;   // u8 x 20
constexpr std::uint32_t kLiveCharacterFlag = 0x0055fb22;     // u8 x 20
constexpr std::uint32_t kLiveSecondA = 0x0055fb36;        // u8 x 2
constexpr std::uint32_t kLiveSecondB = 0x0055fb38;        // u8 x 2
constexpr std::uint32_t kLiveSecondC = 0x0055fb3a;        // u8 x 2
constexpr std::uint32_t kLiveMusic = 0x0055fb3c;
constexpr std::uint32_t kLiveSfx = 0x0055fb40;
constexpr std::uint32_t kLivePadA = 0x0055fc0c;
constexpr std::uint32_t kLiveAutoKick = 0x0055fc2c;
constexpr std::uint32_t kLiveVibration = 0x0055fc34;
constexpr std::uint32_t kLiveNameA = 0x0055fc5c;
constexpr std::uint32_t kLiveOptionA = 0x0055fcc0;
constexpr std::uint32_t kLiveOptionB = 0x0055fcc4;
constexpr std::uint32_t kLiveTextLength = 0x0055fcc8;
constexpr std::uint32_t kLiveText = 0x0055fccc;

constexpr std::uint32_t kVolumeTableMusic = 0x0052be44;  // the mixer values derived from the two volumes
constexpr std::uint32_t kVolumeTableSfx = 0x0052be48;

constexpr std::uint32_t kPadViews = 0x0056b154;   // per player, 0x16c bytes apart: four view pointers
constexpr std::uint32_t kPadViewStride = 0x16c;
constexpr std::uint32_t kPadViewBase = 0x11c;     // the block the views point into lies this far before

constexpr std::uint32_t kCustomStaging = 0x0056a6d0;  // four 0x58-byte slots
constexpr std::uint32_t kCustomStagingStride = 0x58;

constexpr std::uint32_t kMsgOldFormat = 0x00525370;
constexpr std::uint32_t kMsgCorrupt = 0x00525398;
constexpr std::uint32_t kSkaterLoopEnd = 0x0053c1d4;  // 21 rows: the last is the table tail
constexpr std::uint32_t kSkaterLoopStart = skater::kRows + skater::kRowFlags;

// sprintf(destination, text): a plain string copy (the texts carry no '%').
void copy_string(RetailMemory& m, std::uint32_t destination, std::uint32_t source) {
    std::string text = m.cstring(source);
    text.push_back('\0');
    m.write(destination, std::vector<std::uint8_t>(text.begin(), text.end()));
}

}  // namespace

void Career::rebuild_career_pointers() {
    std::uint32_t views = kPadViews;
    std::uint32_t selector = kImgPadA;
    while (selector < kImgPadA + 0x20) {
        std::uint32_t target = views - kPadViewBase;
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t which = word_at(m_, selector);
            if (which <= 3) {
                set_word_at(m_, views + which * 4, target);
            }
            selector += 4;
            target += 16;
        }
        views += kPadViewStride;
    }
}

void Career::serialize_live_state() {
    set_global_word(m_, kImgMusic, global_word(m_, kLiveMusic));
    set_global_word(m_, kImgSfx, global_word(m_, kLiveSfx));
    set_global_word(m_, kImgOptionB, global_word(m_, kLiveOptionB));
    set_global_word(m_, kImgOptionA, global_word(m_, kLiveOptionA));
    for (std::uint32_t player_index = 0; player_index < 2; ++player_index) {
        set_word_at(m_, kImgAutoKick + player_index * 4, word_at(m_, kLiveAutoKick + player_index * 4));
        set_word_at(m_, kImgVibration + player_index * 4, word_at(m_, kLiveVibration + player_index * 4));
        for (std::uint32_t word = 0; word < 4; ++word) {
            set_word_at(m_, kImgPadA + player_index * 16 + word * 4, word_at(m_, kLivePadA + player_index * 16 + word * 4));
        }
    }
    set_global_word(m_, kVolumeTableMusic, global_word(m_, kImgMusic) * 0x555);
    set_global_word(m_, kVolumeTableSfx, global_word(m_, kImgSfx) * 0x15);
    copy_string(m_, kImgText, kLiveText);
    set_global_word(m_, kImgTextLength, global_word(m_, kLiveTextLength));
    if (global_word(m_, kLiveTextLength) < 0xb) {
        m_.fill(kImgText + global_word(m_, kLiveTextLength), 0, 0xb - global_word(m_, kLiveTextLength));
    }
    copy_string(m_, kImgNameA, kLiveNameA);
    rebuild_career_pointers();
    for (std::uint32_t i = 0; i < 2; ++i) {
        set_byte_at(m_, kImgPlayerSkater + i, byte_at(m_, kLivePlayerSkater + i));
        set_byte_at(m_, kImgSecondA + i, byte_at(m_, kLiveSecondA + i));
        set_byte_at(m_, kImgSecondB + i, byte_at(m_, kLiveSecondB + i));
        set_byte_at(m_, kImgSecondC + i, byte_at(m_, kLiveSecondC + i));
    }
}

void Career::apply_serialized_state() {
    set_global_word(m_, kLiveOptionA, global_word(m_, kImgOptionA));
    set_global_word(m_, kLiveOptionB, global_word(m_, kImgOptionB));
    set_global_word(m_, kLiveMusic, global_word(m_, kImgMusic));
    set_global_word(m_, kLiveSfx, global_word(m_, kImgSfx));
    set_global_word(m_, kVolumeTableMusic, global_word(m_, kImgMusic) * 0x555);
    set_global_word(m_, kVolumeTableSfx, global_word(m_, kImgSfx) * 0x15);
    for (std::uint32_t player_index = 0; player_index < 2; ++player_index) {
        set_word_at(m_, kLiveAutoKick + player_index * 4, word_at(m_, kImgAutoKick + player_index * 4));
        set_word_at(m_, kLiveVibration + player_index * 4, word_at(m_, kImgVibration + player_index * 4));
        for (std::uint32_t word = 0; word < 4; ++word) {
            set_word_at(m_, kLivePadA + player_index * 16 + word * 4, word_at(m_, kImgPadA + player_index * 16 + word * 4));
        }
    }
    copy_string(m_, kLiveText, kImgText);
    set_global_word(m_, kLiveTextLength, global_word(m_, kImgTextLength));
    if (global_word(m_, kImgTextLength) < 0xb) {
        m_.fill(kLiveText + global_word(m_, kImgTextLength), 0, 0xb - global_word(m_, kImgTextLength));
    }
    copy_string(m_, kLiveNameA, kImgNameA);
    for (std::uint32_t i = 0; i < 0x14; ++i) {
        set_byte_at(m_, kLiveCharacterCostume + i, 0);
        set_byte_at(m_, kLiveCharacterCustom + i, i);
        set_byte_at(m_, kLiveCharacterFlag + i, 1);
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::int8_t skater_index = sbyte_at(m_, kImgPlayerSkater + i);
        const std::uint8_t a = byte_at(m_, kImgSecondA + i);
        set_byte_at(m_, kLivePlayerSkater + i, static_cast<std::uint8_t>(skater_index));
        set_byte_at(m_, kLiveCharacterCostume + static_cast<std::uint32_t>(static_cast<std::int32_t>(skater_index)), a);
        set_byte_at(m_, kLiveSecondA + i, a);
        const std::uint8_t b = byte_at(m_, kImgSecondB + i);
        set_byte_at(m_, kLiveCharacterCustom + static_cast<std::uint32_t>(static_cast<std::int32_t>(skater_index)), b);
        set_byte_at(m_, kLiveSecondB + i, b);
        const std::uint8_t c = byte_at(m_, kImgSecondC + i);
        set_byte_at(m_, kLiveCharacterFlag + static_cast<std::uint32_t>(static_cast<std::int32_t>(skater_index)), c);
        set_byte_at(m_, kLiveSecondC + i, c);
    }
}

void Career::prepare_save() {
    serialize_live_state();
    set_global_byte(m_, kImgPlayerSkater, static_cast<std::uint32_t>(sk_.select_slot(0)));
    set_global_byte(m_, kImgPlayerSkater + 1, static_cast<std::uint32_t>(sk_.select_slot(1)));
    set_global_byte(m_, kImgPlayerCostume, static_cast<std::uint32_t>(sk_.costume_of_slot(0)));
    set_global_byte(m_, kImgPlayerCostume + 1, static_cast<std::uint32_t>(sk_.costume_of_slot(1)));
}

void Career::apply_loaded_save(const ResetSource& reset_source) {
    const std::uint32_t magic = global_word(m_, kImage);
    const std::uint8_t valid = global_byte(m_, skater::kCustomRecords + skater::kCustomValid);
    if (valid == 0 || valid == 1) {
        if (magic == kFormatMagic) {
            for (int slot = 0; slot < 2; ++slot) {
                const std::int8_t selected = sbyte_at(m_, kImgPlayerSkater + static_cast<std::uint32_t>(slot));
                int costume;
                if (word_at(m_, skater::kRows + skater::kRowUnlocked + static_cast<std::uint32_t>(selected) * skater::kRowStride) == 0) {
                    sk_.select_character(slot, 0);
                    costume = 0;
                } else {
                    sk_.select_character(slot, selected);
                    costume = sbyte_at(m_, kImgPlayerCostume + static_cast<std::uint32_t>(slot));
                }
                sk_.select_costume(slot, costume, true);
            }
            apply_serialized_state();
            reset_runtime_state();
            apply_cheats();
            set_global_byte(m_, 0x0052cb3d, 0);
            return;
        }
        s_.assert_failed(kMsgOldFormat);
        reset_save_image(reset_source);
        return;
    }
    s_.assert_failed(kMsgCorrupt);
    s_.note("reload_custom_skaters");  // FUN_004236d0 (create.cpp): rebuilds the four custom skaters
    reset_save_image(reset_source);
}

int Career::rnd(int limit) {
    s_.note("rnd", {static_cast<std::uint32_t>(limit)});
    return s_.value("rnd");
}

// TrickConfig_InitializeCharacter (0x004170d0) on the character's career record: the ten costs are the character's stats (with the custom skater's added for
// a custom character), then trick_config_initialize_character (trick_database.cpp) over the record
void Career::initialize_trick_config(int character, const TrickDatabase& db, std::span<const std::uint8_t> tricks_image) {
    const bool custom = (byte_at(m_, skater::row(character) + skater::kRowFlags) & skater::kFlagCustom) != 0;
    std::array<std::uint8_t, 10> costs{};
    for (std::uint32_t i = 0; i < 10; ++i) {
        costs[i] = byte_at(m_, skater::row(character) + skater::kRowStats + i);
        if (custom) {
            const int index = sk_.custom_index(character);
            costs[i] = static_cast<std::uint8_t>(
                costs[i] + byte_at(m_, skater::kCustomRecords + static_cast<std::uint32_t>(index) * skater::kCustomStride + skater::kCustomStats + i * 4));
        }
    }
    RetailImage config(kRecordStride);
    config.bytes() = m_.bytes(record(character), kRecordStride);
    trick_config_initialize_character(db, tricks_image, config, character, std::span<const std::uint8_t, 10>(costs), custom);
    m_.write(record(character), config.bytes());
}

// The same as a native of its own (milestone 9: CustomSkater_Init reaches it by address): the manager it is handed was built from TRICKS.BIN as the game has it
// loaded, so the database is built again from that file (TrickDatabase::build lowercases the names of its copy, which the manager's construction already did)
void Career::initialize_trick_config(int character, std::uint32_t manager) {
    s_.check(manager != 0, kMsgNoTrickManager);
    std::vector<std::uint8_t> image = m_.bytes(global_word(m_, 0x0056a894), 33168);
    const TrickDatabase db = TrickDatabase::build(image);
    initialize_trick_config(character, db, image);
}

// TrickDatabase_ApplyUnlockState (0x004bbd70, thiscall on the trick manager, ret 8): every trick a key stream names is unlocked at no cost in the character's
// record (TrickConfig_FindByCharacter). The stream is the character's own (section 0), or section 6's (DAT_0056a850) when `alternate` (a char) is set. The
// trick menu calls it as (0, 1) when cheat 0 is unlocked; TrickConfig_InitializeCharacter's (character, 0) is inside its own port. The manager's database is
// the one TrickDatabase::build makes from the loaded TRICKS.BIN, as initialize_trick_config above has it.
void Career::apply_trick_unlock_state(std::uint32_t manager, int character, bool alternate) {
    (void)manager;
    std::vector<std::uint8_t> image = m_.bytes(global_word(m_, 0x0056a894), 33168);
    const TrickDatabase db = TrickDatabase::build(image);
    const std::size_t stream = alternate ? trick_section_offset(image, kSectionAlternateKeys) : trick_character_key_stream(image, character);
    RetailImage config(kRecordStride);
    config.bytes() = m_.bytes(record(character), kRecordStride);
    trick_apply_unlock_state(db, image, stream, config);
    m_.write(record(character), config.bytes());
}

void Career::reset_save_image(const ResetSource& reset_source) {
    constexpr std::uint32_t kNamePointers = 0x00524ed8;   // pointers to 3-letter initials, null terminated
    constexpr std::uint32_t kHighScores = 0x00569ebc;     // image +0x1454: 14 tables of 8 entries of 8 bytes
    constexpr std::uint32_t kCustomBlockA = 0x0056a488;   // image blocks preserved across the reset
    constexpr std::uint32_t kCustomBlockB = 0x0056a6cc;
    constexpr std::uint32_t kMsgProblem = 0x00525030;
    constexpr std::uint32_t kMsgCopyingStats = 0x00525018;
    constexpr std::uint32_t kAlternateBindings = 0x0056a690;

    set_global_word(m_, 0x00561ce4, 0);
    set_global_word(m_, kCurrentLevel, 0);
    const auto block_a = m_.bytes(kCustomBlockA, 0x200);
    const auto block_b = m_.bytes(kCustomBlockB, 0x160);
    // Retail keeps the two blocks in Mem_New(size, 0, 1, 0) blocks at the top of the heap (Mem_CopyBytes in and out) and frees each after its copy back. A replay's
    // fixture has no such calls; live (milestone 9's cold boot) they are made, because each one moves the heap's allocation counter 0x0056a994.
    constexpr std::uint32_t kMemNew = 0x0046f490, kMemDelete = 0x0046f4d0;
    auto heap_call = [&](std::uint32_t address, std::vector<std::uint32_t> args) {
        char name[24];
        std::snprintf(name, sizeof name, "engine_%08x", address);
        s_.note(name, std::move(args));
        return static_cast<std::uint32_t>(s_.value(name));
    };
    std::uint32_t saved_a = 0, saved_b = 0;
    if (s_.live()) {
        saved_a = heap_call(kMemNew, {0x200, 0, 1, 0});
        m_.write(saved_a, block_a);
        saved_b = heap_call(kMemNew, {0x160, 0, 1, 0});
        m_.write(saved_b, block_b);
    }
    m_.fill(kImage, 0, 0x771 * 4);
    set_global_word(m_, kImage, kFormatMagic);
    m_.write(kCustomBlockA, block_a);
    if (s_.live()) {
        heap_call(kMemDelete, {saved_a});
    }
    m_.write(kCustomBlockB, block_b);
    if (s_.live()) {
        heap_call(kMemDelete, {saved_b});
    }

    int names = 0;
    while (word_at(m_, kNamePointers + static_cast<std::uint32_t>(names) * 4) != 0) {
        ++names;
    }
    std::int32_t base_score = 8;
    std::uint32_t table = 0;
    for (;;) {
        for (std::uint32_t entry = 0; entry < 8; ++entry) {
            const int pick = rnd(names);
            s_.check(pick < names, kMsgProblem);
            const std::uint32_t at = kHighScores + (table + entry) * 8;
            const std::uint32_t initials = word_at(m_, kNamePointers + static_cast<std::uint32_t>(pick) * 4);
            set_byte_at(m_, at, byte_at(m_, initials));
            set_byte_at(m_, at + 1, byte_at(m_, initials + 1));
            set_byte_at(m_, at + 2, byte_at(m_, initials + 2));
            set_word_at(m_, at + 4, static_cast<std::uint32_t>((base_score - static_cast<std::int32_t>(entry)) * 5000));
            int character;
            do {
                do {
                    character = rnd(0x14);
                } while (word_at(m_, skater::row(character) + skater::kRowUnlocked) == 0);
            } while ((byte_at(m_, skater::row(character) + skater::kRowFlags) & 0x22) != 0);
            set_byte_at(m_, at + 3, static_cast<std::uint32_t>(character));
        }
        base_score += 4;
        table += 8;
        if (base_score > 0x3f) {
            break;
        }
    }

    set_global_word(m_, 0x0056bb34, 0);
    s_.note("printf_log", {kMsgCopyingStats});
    // Retail builds the database in a trick manager it makes here (CClass_OpNew(0x140c), Tricks_InitializeManager), whose constructor loads TRICKS.BIN when it is
    // not loaded and whose deleting destructor unloads it again before the manager is freed. A replay has the image from its fixture; live (milestone 9's cold
    // boot, where the file is not loaded yet), the manager is made as retail makes it, the computation below reads the file it loaded, and the manager goes after
    // the bindings.
    constexpr std::uint32_t kOpNew = 0x00466b70, kInitializeManager = 0x004bb4f0, kDeleteManager = 0x004bb770, kTricksImage = 0x0056a894;
    constexpr std::uint32_t kTricksImageSize = 33168;
    auto engine = [&](std::uint32_t address, std::vector<std::uint32_t> args) {
        char name[24];
        std::snprintf(name, sizeof name, "engine_%08x", address);
        s_.note(name, std::move(args));
        return static_cast<std::uint32_t>(s_.value(name));
    };
    std::uint32_t manager = 0;
    std::vector<std::uint8_t> tricks_image = reset_source.tricks_image;
    if (s_.live()) {
        manager = engine(kOpNew, {0x140c});
        if (manager != 0) {
            manager = engine(kInitializeManager, {manager});
        }
        tricks_image = m_.bytes(global_word(m_, kTricksImage), kTricksImageSize);
    }
    const TrickDatabase db = TrickDatabase::build(tricks_image);
    std::uint32_t flags_at = kSkaterLoopStart;
    for (int character = 0; flags_at < kSkaterLoopEnd; ++character, flags_at += skater::kRowStride) {
        const bool custom = (byte_at(m_, flags_at) & skater::kFlagCustom) != 0;
        initialize_trick_config(character, db, tricks_image);
        if (custom) {
            load_custom_appearance(sk_.custom_index(character));
        }
    }
    {
        std::vector<std::uint8_t> block = m_.bytes(kAlternateBindings, kBindingBlockSize);
        trick_apply_character_bindings(db, tricks_image, trick_section_offset(tricks_image, kSectionAlternateKeys), block, 0);
        m_.write(kAlternateBindings, block);
    }
    if (s_.live() && manager != 0) {
        engine(kDeleteManager, {manager, 1});   // the scalar deleting destructor: UnloadGTricks when it loaded the file, then the block
    }
    for (std::uint32_t i = 0; i < 0x14; ++i) {
        const std::uint8_t costume = byte_at(m_, kLiveCharacterCostume + i);
        const std::uint32_t rec = record(byte_at(m_, kLiveCharacterCustom + i));
        if (((1u << (costume & 31)) & word_at(m_, rec + kRecClassMask)) == 0) {
            set_byte_at(m_, kLiveCharacterCostume + i, 0);
            set_byte_at(m_, kLiveCharacterCustom + i, i);
        }
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::uint8_t skater_index = byte_at(m_, kLivePlayerSkater + i);
        set_byte_at(m_, kLiveSecondA + i, byte_at(m_, kLiveCharacterCostume + skater_index));
        set_byte_at(m_, kLiveSecondB + i, byte_at(m_, kLiveCharacterCustom + skater_index));
        set_byte_at(m_, kLiveSecondC + i, byte_at(m_, kLiveCharacterFlag + skater_index));
    }
    serialize_live_state();
    set_global_byte(m_, 0x0056a396, 100);
    set_global_word(m_, 0x0056a2f0, 0);
    set_global_word(m_, 0x0056a2f4, 0);
    reset_runtime_state();
    set_global_byte(m_, 0x0056acf9, 0);
}

void Career::save_custom_appearance(int slot) {
    s_.check(slot >= 0 && slot <= 3, skater::kMsgEmpty);
    const std::uint32_t rec = record(sk_.map_enabled_index(slot));
    m_.copy(kCustomStaging + static_cast<std::uint32_t>(slot) * kCustomStagingStride, rec + kRecBindings, 0x35);
}

void Career::load_custom_appearance(int slot) {
    s_.check(slot >= 0 && slot <= 3, skater::kMsgEmpty);
    if (byte_at(m_, skater::kCustomRecords + static_cast<std::uint32_t>(slot) * skater::kCustomStride + skater::kCustomValid) != 0) {
        const std::uint32_t rec = record(sk_.map_enabled_index(slot));
        m_.copy(rec + kRecBindings, kCustomStaging + static_cast<std::uint32_t>(slot) * kCustomStagingStride, 0x35);
    }
}

}  // namespace opentony::runtime::career
