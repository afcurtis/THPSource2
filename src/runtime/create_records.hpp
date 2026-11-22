#pragma once

#include "create_services.hpp"
#include "create_textures.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"

#include <cstdint>

namespace opentony::runtime {

// Phase 4.9: the four custom skaters' records (create.cpp, creedit.cpp, cresave.cpp). A custom
// skater is a 0x80-byte record at 0x0056a488 + slot * 0x80 (skater_manager.hpp has the fields the
// manager reads) plus a career record (TrickConfig_FindByCharacter of the character the slot maps
// to) that carries its stat bytes. Creating one fills the record with defaults, builds its
// appearance out of the texture-set manager (one set id per clothing kind, in seven dwords at
// +0x64) and, for the "random skater" button, steps those ids by random amounts.
namespace create {

constexpr std::uint32_t kCustomRecords = skater::kCustomRecords;    // 0x0056a488
constexpr std::uint32_t kCustomStride = skater::kCustomStride;      // 0x80
constexpr std::uint32_t kTextureManager = 0x0056a87c;               // the manager the create code is working with
constexpr std::uint32_t kBuildRows = 0x00539e10;                    // three pointers to ten stat offsets each
constexpr std::uint32_t kStagedAppearance = 0x0056a6cc;             // per slot, 0x58 bytes (the binding staging table)
constexpr std::uint32_t kLiveCharacters = 0x0055faf8;               // the two players' characters
constexpr std::uint32_t kLiveCostume = 0x0055fafa;                  // + character
constexpr std::uint32_t kLiveMapped = 0x0055fb0e;                   // + character
constexpr std::uint32_t kLiveEnabled = 0x0055fb22;                  // + character
constexpr std::uint32_t kLivePlayerCostume = 0x0055fb36;            // + player
constexpr std::uint32_t kLivePlayerCharacter = 0x0055fb38;          // + player
constexpr std::uint32_t kLivePlayerEnabled = 0x0055fb3a;            // + player
constexpr std::uint32_t kSaveImage = 0x00568a68;                    // CareerSaveImage
constexpr std::uint32_t kSaveImageSize = 0x1dc4;

// Custom record fields (offsets from the record).
constexpr std::uint32_t kRecName = 0x00;          // "Skater A" ... (16 bytes)
constexpr std::uint32_t kRecHometown = 0x10;      // "Woodland Hills" (16 bytes)
constexpr std::uint32_t kRecHeight = 0x20;        // 0x12 in a new record
constexpr std::uint32_t kRecSpare = 0x21;         // spare stat points (5 in a new record)
constexpr std::uint32_t kRecFlag = 0x22;          // 1 in a new record
constexpr std::uint32_t kRecScale = 0x23;         // 100 in a new record
constexpr std::uint32_t kRecBuild = 0x24;         // 0..2: which row of kBuildRows offsets the stats
constexpr std::uint32_t kRecSex = 0x25;           // 0 or 1 (slot 0 is always 0)
constexpr std::uint32_t kRecValid = 0x26;
constexpr std::uint32_t kRecColours = 0x27;       // four colour-preset indices (0..0x23)
constexpr std::uint32_t kRecStats = 0x2c;         // ten dwords of allocated stat points
constexpr std::uint32_t kRecHue = 0x54;           // the editor's hue dword (saved with the colours)
constexpr std::uint32_t kRecSets = 0x64;          // seven texture-set ids, one per kind

// Career record: the ten stat bytes.
constexpr std::uint32_t kStatBytes = 0x38;

// Editor object fields the colour save/restore touch.
constexpr std::uint32_t kEditorHue = 0x0c;
constexpr std::uint32_t kEditorColours = 0x318;
// Menu object: the working save image pointer.
constexpr std::uint32_t kMenuImage = 0x7cc;

// Assertion messages.
constexpr std::uint32_t kMsgNoTextureDb = 0x005265e0;
constexpr std::uint32_t kMsgNoTrickDb = 0x005265cc;
constexpr std::uint32_t kMsgPossibleLeak = 0x005265b8;
constexpr std::uint32_t kMsgOutOfRange = 0x0052659c;
constexpr std::uint32_t kMsgTooManyCustom = 0x00526584;
constexpr std::uint32_t kMsgStatOverflow = 0x00526544;
constexpr std::uint32_t kMsgNoTextureSetDb = 0x0052652c;
constexpr std::uint32_t kMsgBadCopyIndex = 0x00527cdc;

class CustomSkaters final {
public:
    CustomSkaters(RetailMemory& memory, RetailServices& services, skater::SkaterManager& skaters, TextureSets& textures)
        : m_(memory), s_(services), sk_(skaters), tex_(textures) {}

    // 0x00422f20: a new custom skater in `slot` with default values, stats balanced to at most 10 and the
    // first texture sets of each kind, then randomised (below).
    void init_record(int slot, std::uint32_t texture_manager, std::uint32_t trick_manager);
    // 0x00423370: the "random skater" button: build, sex, stats and texture sets chosen at random.
    void randomize(int slot, std::uint32_t texture_manager, std::uint32_t trick_manager);
    // 0x00422640 / 0x00422690: copy the editor's four colour bytes and hue dword into / out of a record.
    void save_colours(std::uint32_t editor, int slot);
    void load_colours(std::uint32_t editor, int slot);
    // 0x0042ef50: copy custom skater `from` (record, staged appearance, career record) into slot `to` of the
    // menu's working save image, make that image the live one and mark the character enabled.
    void copy_slot(std::uint32_t menu, int from, int to);
    // 0x0042ef30: make the menu's working save image the live one.
    void commit_image(std::uint32_t menu);

private:
    [[nodiscard]] static std::uint32_t record(int slot) noexcept {
        return kCustomRecords + static_cast<std::uint32_t>(slot) * kCustomStride;
    }
    [[nodiscard]] static std::uint32_t career_record(int character) noexcept {
        return skater::kCareerRecords + static_cast<std::uint32_t>(character) * skater::kCareerRecordStride;
    }
    [[nodiscard]] int rnd(int limit);
    void enable_live_character(int character);

    RetailMemory& m_;
    RetailServices& s_;
    skater::SkaterManager& sk_;
    TextureSets& tex_;
};

}  // namespace create
}  // namespace opentony::runtime
