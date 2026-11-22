#pragma once

#include "retail_image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.7b: the trick database (trickdb.cpp) and the per-player key table builder.
//
//   TRICKS.BIN  section 5 (GAllTricks)  -- descriptors [n][n words][source][descriptor]...
//     -> Tricks_InitializeManager 0x004bb4f0 / TrickDatabase_ParseRecord 0x004bb7e0 ->
//        TrickDatabase (0x28-byte records, at most 0x80)
//   TRICKS.BIN section 0 (per character) / section 6 (alternate) -- key streams
//     -> TrickDatabase_ApplyUnlockState 0x004bbd70 + TrickDatabase_ApplyCharacterBindings
//        0x004bbf00 -> the character record (0x104 bytes; unlock bitmap, 0x35-byte binding block)
//     -> TrickTable_BuildForPlayer 0x004bcf00 -> the key table the recogniser walks (4.7a)
//
// Retail assertions (M3dUtils_InBetween 0x004011e0) only log and return, so the code here notes
// them in `diagnostics()` and carries on where retail does. A read that would leave the loaded
// image, or a write that would overflow a retail buffer, throws TrickDatabaseFault instead of
// reading wild memory.

class TrickDatabaseFault final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Two 12-byte-record tables in the executable's data (generated: trick_combo_tables.cpp):
// count, three button codes, trick-class mask (+8), slot kind (+10).
using ComboWords = std::array<std::uint16_t, 6>;
[[nodiscard]] std::span<const ComboWords> builtin_combos() noexcept;  // 0x00540cb8
[[nodiscard]] std::span<const ComboWords> special_combos() noexcept;  // 0x00540e30

constexpr std::size_t kTrickRecordSize = 0x28;
constexpr std::size_t kTrickDatabaseCapacity = 0x80;
constexpr std::size_t kTrickNameCapacity = 0x20;  // 31 characters and the NUL
constexpr std::uint16_t kTrickDefaultCost = 0x7b;  // +0x22 when a script has no 'Q' token

// Runtime trick-class flags kept in TrickRecord::flags (+0x24). The low bits are the descriptor
// type's table class; the merge in TrickDatabase_AddOrMerge adds descriptor bits.
constexpr std::uint16_t kTrickClassGrind = 0x100;    // descriptor type 9
constexpr std::uint16_t kTrickClassManual = 0x200;   // descriptor type 10
constexpr std::uint16_t kTrickClassLip = 0x800;      // descriptor type 12
constexpr std::uint16_t kTrickClassAir = 0x2000;     // descriptor types 1 and 2
constexpr std::uint16_t kTrickFlagSpecial = 0x8000;

struct TrickRecord final {
    std::string name;             // +0x00, NUL-terminated, at most 31 characters
    std::uint16_t source{};       // +0x20 offset of the trick's script in TRICKS.BIN
    std::uint16_t cost{};         // +0x22 'Q' token value
    std::uint16_t flags{};        // +0x24 runtime class flags
    std::uint16_t descriptor{};   // +0x26 raw descriptor word
    friend bool operator==(const TrickRecord&, const TrickRecord&) = default;
};

// TrickScript_CommandSize (0x004bf6c0): the offset of the command after the one at `offset`.
// An undefined command logs and advances by one byte, as retail does.
[[nodiscard]] std::size_t trick_command_size(
    std::span<const std::uint8_t> image, std::size_t offset, std::vector<std::string>* diagnostics = nullptr);

// TrickDatabase_CleanName (0x004bb9d0), in place: drops '{' and '}' and, for the lip class, a
// leading "fs " / "bs " (only at offset 0 or 1).
void trick_clean_name(std::string& name, bool strip_direction_prefix);

class TrickDatabase final {
public:
    // Tricks_InitializeManager (0x004bb4f0) over a loaded TRICKS.BIN. Retail lowercases each name
    // string inside the loaded buffer (FUN_004b8340), so `image` is modified in place.
    [[nodiscard]] static TrickDatabase build(std::vector<std::uint8_t>& image);

    [[nodiscard]] std::size_t count() const noexcept { return records_.size(); }                // 0x004bd160
    [[nodiscard]] const TrickRecord& record(int index) const;                                   // 0x004bd170
    [[nodiscard]] const std::vector<TrickRecord>& records() const noexcept { return records_; }
    [[nodiscard]] const TrickRecord* find_by_source(std::uint16_t source) const noexcept;       // 0x004bc300
    // 0x004bc330: the index of the record with this source offset, -1 (and a note) if absent.
    [[nodiscard]] int index_by_source(std::uint16_t source) const;

    // 0x004bc7e0 / 0x004bc900: the index of the table record whose button list equals `buttons`,
    // or -1. Retail asserts (and carries on) when the count is not 1..4.
    [[nodiscard]] int find_button_combination(std::span<const std::uint16_t> buttons) const;
    [[nodiscard]] int find_special_combination(std::span<const std::uint16_t> buttons) const;

    // TrickDatabase_AddOrMerge (0x004bba50), public for tests: returns the record kept.
    TrickRecord& add_or_merge(const std::string& name, std::uint16_t flags, std::uint16_t cost,
                              std::uint16_t source, std::uint16_t descriptor);

    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept { return diagnostics_; }
    void note(std::string message) const { diagnostics_.push_back(std::move(message)); }
    [[nodiscard]] std::vector<std::string>* diagnostics_sink() const noexcept { return &diagnostics_; }

private:
    std::vector<TrickRecord> records_;
    mutable std::vector<std::string> diagnostics_;
};

// ---- TRICKS.BIN sections (Tricks_LoadBinary, 0x00492a90) -------------------------------------
// The first seven signed words are byte offsets of the sections from the image start.
[[nodiscard]] std::size_t trick_section_offset(std::span<const std::uint8_t> image, int section);
constexpr int kSectionCharacterKeys = 0;  // DAT_00568688: one s16 stream offset per character
constexpr int kSectionAllTricks = 5;      // DAT_0056a844 (GAllTricks)
constexpr int kSectionAlternateKeys = 6;  // DAT_0056a850

// The key stream of one character: the s16 at section 0 + 2 * character is its offset.
[[nodiscard]] std::size_t trick_character_key_stream(std::span<const std::uint8_t> image, int character);

// ---- The character record (skatermanager + character * 0x104) ---------------------------------
constexpr std::size_t kTrickConfigSize = 0x104;
constexpr std::size_t kConfigPoints = 0x08;       // s32 remaining points
constexpr std::size_t kConfigClassMask = 0x34;    // u32, 1 or 0x3ff from the character's flags
constexpr std::size_t kConfigCosts = 0x38;        // ten bytes
constexpr std::size_t kConfigUnlocks = 0xac;      // bitmap of 0x80 tricks
constexpr std::size_t kConfigBindings = 0xcc;     // the 0x35-byte binding block
constexpr std::size_t kBindingBlockSize = 0x35;
constexpr std::size_t kBindingSpecialRecords = 0x2b;  // five record indices
constexpr std::size_t kBindingSpecialCombos = 0x30;   // five special-combo indices
constexpr std::uint8_t kBindingEmpty = 0xff;

// 0x00416140 TrickConfig_ConsumePointsAndSetBit: false when fewer than `cost` points remain.
bool trick_config_consume_points_and_set_bit(
    RetailImage& config, const TrickDatabase& db, int trick_index, int cost);
// 0x00416230 / 0x00416380: bind a trick record to an ordinary combination slot / one of five special slots.
void trick_binding_assign_button(std::span<std::uint8_t> block, const TrickDatabase& db, int combo, int record);
bool trick_binding_assign_special(std::span<std::uint8_t> block, const TrickDatabase& db, int combo, int record);

// 0x00416340 Career_SpecialTrickAssigned: the special combination a record is bound to, if any.
[[nodiscard]] std::optional<std::uint8_t> trick_binding_special_assigned(std::span<const std::uint8_t> block,
                                                                         int record);

// 0x004bbd70: walks a key stream and sets an unlock bit (at zero cost) for every trick it names.
void trick_apply_unlock_state(const TrickDatabase& db, std::span<const std::uint8_t> image,
                              std::size_t key_stream, RetailImage& config);
// 0x004bbf00: rebuilds a binding block from a key stream (ordinary combinations first, special after).
void trick_apply_character_bindings(const TrickDatabase& db, std::span<const std::uint8_t> image,
                                    std::size_t key_stream, std::span<std::uint8_t> block, int character);

// 0x004170d0 TrickConfig_InitializeCharacter: clears the record, stores the ten cost bytes (already
// adjusted by the caller, retail adds the character's stat bonuses) and the class mask, and applies
// unlock state and bindings for the character's own key stream.
void trick_config_initialize_character(const TrickDatabase& db, std::span<const std::uint8_t> image,
                                       RetailImage& config, int character,
                                       std::span<const std::uint8_t, 10> costs, bool full_class_mask);

// ---- The per-player key table ------------------------------------------------------------------
// One entry: count, count button codes, source offset, descriptor. A zero count ends the table.
constexpr std::size_t kPlayerTableMaxBytes = 0x400;

// 0x004bc3a0 TrickTable_BuildPlayerReturnCombos: the key stream as fixed six-word entries
// (count, four button slots, [4] overwritten with the record's flags, [5] source offset).
[[nodiscard]] std::vector<std::array<std::uint16_t, 6>> trick_table_return_combos(
    const TrickDatabase& db, std::span<const std::uint8_t> image, std::size_t key_stream,
    std::size_t limit = 0x100);

// 0x004bcf00 TrickTable_BuildForPlayer: the finished table (with its zero terminator) built from a
// key stream and a binding block. Which stream and block a player gets is the caller's business.
[[nodiscard]] std::vector<std::uint16_t> trick_table_build_for_player(
    const TrickDatabase& db, std::span<const std::uint8_t> image, std::size_t key_stream,
    std::span<const std::uint8_t> block);

} // namespace opentony::runtime
