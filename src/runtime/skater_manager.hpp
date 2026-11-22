#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime {

// Phase 4.8: the skater manager (skatmgr.cpp). A 0x6a0-byte object at 0x0056c580 holds, for each
// of eight player slots, which of the twenty characters is picked and in which costume, plus the
// custom skater a slot has loaded. The character table it indexes is static data of 0x170-byte
// rows from 0x0053a3b8 (names, face bitmap, flags); custom skaters live in 0x80-byte records at
// 0x0056a488. Every function reads and writes that memory at its retail address.
namespace skater {

constexpr std::uint32_t kManager = 0x0056c580;
constexpr std::uint32_t kRows = 0x0053a3b8;            // SkaterModelNameTable: row k at + k * 0x170
constexpr std::uint32_t kRowStride = 0x170;
constexpr std::uint32_t kRowCount = 20;
constexpr std::uint32_t kCustomRecords = 0x0056a488;   // custom skater records, 0x80 bytes each
constexpr std::uint32_t kCustomStride = 0x80;
constexpr std::uint32_t kCareerRecords = 0x00568a6c;   // per-character career record, 0x104 bytes
constexpr std::uint32_t kCareerRecordStride = 0x104;

// Row fields (offsets from the row start).
constexpr std::uint32_t kRowName = 0x20;        // "Tony Hawk" ... inline name
constexpr std::uint32_t kRowStats = 0x144;      // ten base stat bytes
constexpr std::uint32_t kRowSlotFlag = 0x14f;   // byte tested by SkaterManager_SlotIsSelectable
constexpr std::uint32_t kRowFace = 0x150;       // pointer to the face bitmap name
constexpr std::uint32_t kRowUnlocked = 0x158;   // dword: nonzero = available
constexpr std::uint32_t kRowFlags = 0x15c;      // bit 1 custom, 2 ... (see below)
constexpr std::uint32_t kRowVideoId = 0x168;    // dword the ending video uses

// Row flag bits (kRowFlags). Bit 0x02 marks an enabled custom-skater row; 0x08 and 0x10 mark the
// rows whose costume list stops at two / three entries until unlocked; 0x40, 0x80 and 0x100 are
// the secret characters the cheats release; 0x20 is set with 0x08 and 0x10 (Career_UnlockCharacter
// clears both 0x08 and 0x10 when it unlocks a character).
constexpr std::uint32_t kFlagCustom = 0x02;
constexpr std::uint32_t kFlagHidden3 = 0x08;
constexpr std::uint32_t kFlagHidden4 = 0x10;

// Manager fields.
constexpr std::uint32_t kActiveSlot = 0x64;
constexpr std::uint32_t kSlotEntries = 0x04;       // + slot * 0xc: character, custom slot, costume
constexpr std::uint32_t kRememberedCostume = 0x68;  // + character * 4
constexpr std::uint32_t kLoadedFlags = 0xb8;        // + slot * 0xbc: a custom skater is loaded for the slot
constexpr std::uint32_t kLoadedStride = 0xbc;
constexpr std::uint32_t kLoadedBlock = 0xbc;        // + slot * 0xbc: the 0x35-byte binding block copy
constexpr std::uint32_t kLoadedRecord = 0xf4;       // + slot * 0xbc: the 0x80-byte custom record copy

// Custom record fields.
constexpr std::uint32_t kCustomPoints = 0x21;   // spare stat points (signed byte)
constexpr std::uint32_t kCustomFlagA = 0x22;
constexpr std::uint32_t kCustomFlagB = 0x25;
constexpr std::uint32_t kCustomValid = 0x26;    // 0 or 1 in a good save; anything else is corrupt
constexpr std::uint32_t kCustomStats = 0x2c;    // ten dwords

// Assertion messages (the addresses of the format strings).
constexpr std::uint32_t kMsgOutOfRangeSlot = 0x00537b88;
constexpr std::uint32_t kMsgBadCostume = 0x00537bb8;
constexpr std::uint32_t kMsgOutOfRangeSkater = 0x00537bd4;
constexpr std::uint32_t kMsgNoReturnBuffer = 0x00537be8;
constexpr std::uint32_t kMsgUnusedLoaded = 0x00537c04;
constexpr std::uint32_t kMsgNotCustom = 0x00537c30;
constexpr std::uint32_t kMsgBadSkaterId = 0x00537c48;
constexpr std::uint32_t kMsgOutOfRangeCustom = 0x0052659c;
constexpr std::uint32_t kMsgOutOfRangeCustomIndex = 0x00537c5c;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;  // an empty string; retail asserts with it

[[nodiscard]] constexpr std::uint32_t row(int character) noexcept {
    return kRows + static_cast<std::uint32_t>(character) * kRowStride;
}

class SkaterManager final {
public:
    SkaterManager(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- the active slot -------------------------------------------------------------------
    void set_active_slot(int slot);                      // 0x004b0bc0
    [[nodiscard]] std::int32_t active_slot() const;
    [[nodiscard]] std::int32_t current_character() const;   // 0x004b0c30
    [[nodiscard]] std::int32_t current_costume() const;     // 0x004b0c40
    [[nodiscard]] std::int32_t current_custom_slot() const; // 0x004b0c50

    // ---- per slot --------------------------------------------------------------------------
    [[nodiscard]] std::int32_t character_of_slot(int slot);        // 0x004b0c60 Skater_GetResourceSlot
    [[nodiscard]] std::int32_t costume_of_slot(int slot);          // 0x004b0cd0
    [[nodiscard]] std::int32_t slot_loaded_flag(int slot);         // 0x004b1430 Skater_GetSlotRecord
    [[nodiscard]] std::int32_t select_slot(int slot);              // 0x004b18a0 Skater_SelectManagerSlot
    void select_current(int character);                            // 0x004b0d40
    void select_character(int slot, int character);                // 0x004b0e60
    void select_character_checked(int character, int slot);        // 0x004b18d0
    void select_costume(int slot, int costume, bool wrap);         // 0x004b0fe0
    void set_custom_slot(int slot, int custom);                    // 0x004b1070
    [[nodiscard]] std::int32_t remembered_costume(int character);  // 0x004b1130
    void release_all_loaded();                                     // 0x004b1150
    void release_active_loaded();                                  // 0x004b1180
    void load_custom_into_active(int custom);                      // 0x004b11c0
    // 0x004b1240: writes the slot's skater name to `out` (a NUL-terminated string in memory).
    void format_name(int slot, std::uint32_t out);
    [[nodiscard]] std::uint32_t face_bitmap(int slot);             // 0x004b1360
    void swap_custom_record(int slot);                             // 0x004b14a0
    [[nodiscard]] std::uint32_t loaded_block(int slot);            // 0x004b1570
    [[nodiscard]] bool slot_is_selectable(int slot);               // 0x004b1630 (low byte is the result)

    // ---- the character table ---------------------------------------------------------------
    [[nodiscard]] int count_costume_names(int character) const;    // 0x004b0f80 Skater_CountCostumeNames
    [[nodiscard]] std::uint32_t select_model_name(int character, int costume);  // 0x004b1700
    [[nodiscard]] int custom_index(int character);                 // 0x004b1810
    [[nodiscard]] int map_enabled_index(int custom);               // 0x004b1980 Skater_MapEnabledIndex
    [[nodiscard]] bool custom_with_flag(int character);            // 0x004b19f0
    [[nodiscard]] std::uint32_t display_name(int character);       // 0x004b1a30 (address of the name)

    // 0x004b1890: clears the active-player globals.
    void reset_active_player();

    [[nodiscard]] std::uint32_t row_flags(int character) const {
        return guest::read_scalar<std::uint32_t>(m_, row(character) + kRowFlags);
    }

private:
    [[nodiscard]] std::uint32_t slot_entry(int slot) const {
        return kManager + kSlotEntries + static_cast<std::uint32_t>(slot) * 0xc;
    }
    [[nodiscard]] std::uint32_t loaded(int slot) const {
        return kManager + static_cast<std::uint32_t>(slot) * kLoadedStride;
    }
    void check_slot(int slot) { s_.check(slot >= 0 && slot <= 7, kMsgOutOfRangeSlot); }
    void check_costume(int character, int costume) {
        s_.check(costume >= 0 && costume < count_costume_names(character), kMsgBadCostume);
    }

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace skater
}  // namespace opentony::runtime
