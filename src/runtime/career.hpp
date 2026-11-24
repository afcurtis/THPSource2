#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opentony::runtime {

class TrickDatabase;

// Phase 4.8: the career (career.cpp): per-character progress records, the tapes (goals) and
// medals a level awards, cash icons, gaps, stat points, which levels are open, the cheat list and
// the locked characters, and the live career globals that the save image serialises.
//
// Like the skater manager, every function reads and writes retail memory at its retail address
// (RetailMemory) and reaches the outside world through RetailServices, so a retail call replays
// word for word. The memory model:
//
//   0x00568a6c + character * 0x104   the career record of each of the twenty characters
//   0x0053c1f8 + level * 0x1ac       the static level table (goal rows, rewards, unlock needs)
//   0x0055fd2c ... 0x0055fe10        the live per-level state (goals got, gap counts, cash icons)
//   0x0056a23c ... 0x0056a2d8        the gap bits of each level, three words per level
//   0x0056a374 + cheat * 4           the cheat list: unlocked, character, on/off
//   0x0056a858                       the two player pointers (the skater object holds the score)
namespace career {

// ---- career record (0x104 bytes, TrickConfig_FindByCharacter) --------------------------------
constexpr std::uint32_t kRecords = skater::kCareerRecords;
constexpr std::uint32_t kRecordStride = skater::kCareerRecordStride;
constexpr std::uint32_t kRecTapes = 0x00;       // u8  total tapes (goals) collected over the non-contest levels
constexpr std::uint32_t kRecFlags = 0x01;       // u8  bit 0x40: the game was cleared with this character
constexpr std::uint32_t kRecGolds = 0x03;       // u8  contest levels won gold
constexpr std::uint32_t kRecCashTotal = 0x04;   // s32 all cash earned
constexpr std::uint32_t kRecCash = 0x08;        // s32 cash to spend
constexpr std::uint32_t kRecLevelMasks = 0x0c;  // u16 x 13: goals 0-9, 0x400 gold, 0x800 silver, 0x1000 bronze, 0x8000 cleared
constexpr std::uint32_t kRecLevelBytes = 0x26;  // u8 x 13
constexpr std::uint32_t kRecClassMask = 0x34;   // u32
constexpr std::uint32_t kRecStats = 0x38;       // s8 x 10 stat points
constexpr std::uint32_t kRecMoneyMasks = 0x44;  // u32 x 2 per level: which cash icons were collected
constexpr std::uint32_t kRecUnlocks = 0xac;     // u32 x 4: bitmap of the purchased tricks
constexpr std::uint32_t kRecBindings = 0xcc;    // the 0x35-byte binding block

// ---- level table -----------------------------------------------------------------------------
constexpr std::uint32_t kLevels = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kLevelCount = 13;
constexpr std::uint32_t kLvContest = 0x48;        // u8: nonzero = a competition level
constexpr std::uint32_t kLvGoldCash = 0x5c;       // s32 prize money
constexpr std::uint32_t kLvSilverCash = 0x60;
constexpr std::uint32_t kLvBronzeCash = 0x64;
constexpr std::uint32_t kLvCashNeeded = 0x7c;     // s32 total cash needed to open the level
constexpr std::uint32_t kLvMedalsNeeded = 0x80;   // s32 levels with medals needed (when nonzero)
constexpr std::uint32_t kLvGapLow = 0xb0;         // s32 first gap id of the level (ids are id % 10000)
constexpr std::uint32_t kLvGapHigh = 0xb4;
constexpr std::uint32_t kLvGoals = 0xb8;          // ten rows of 0x18 bytes
constexpr std::uint32_t kGoalStride = 0x18;
constexpr std::uint32_t kGoalType = 0x00;         // 0 score, 1 SKATE, 2 gaps, 3 pickups, 4 count, 5 trick gaps, 6 secret tape, 7 all
constexpr std::uint32_t kGoalThreshold = 0x04;    // score needed (type 0)
constexpr std::uint32_t kGoalNoun = 0x08;         // pointer to the noun ("BARRELS") for progress messages
constexpr std::uint32_t kGoalReward = 0x0c;       // cash
constexpr std::uint32_t kGoalText = 0x10;         // pointer to the goal description

constexpr std::uint32_t kGoalTypeScore = 0;
constexpr std::uint32_t kGoalTypeSkate = 1;
constexpr std::uint32_t kGoalTypeGoalGaps = 2;
constexpr std::uint32_t kGoalTypePickups = 3;
constexpr std::uint32_t kGoalTypeCount = 4;
constexpr std::uint32_t kGoalTypeTrickGaps = 5;
constexpr std::uint32_t kGoalTypeSecret = 6;
constexpr std::uint32_t kGoalTypeAll = 7;

[[nodiscard]] constexpr std::uint32_t level(int index) noexcept {
    return kLevels + static_cast<std::uint32_t>(index) * kLevelStride;
}
[[nodiscard]] constexpr std::uint32_t record(int character) noexcept {
    return kRecords + static_cast<std::uint32_t>(character) * kRecordStride;
}

// ---- globals ---------------------------------------------------------------------------------
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kGameMode = 0x00533f38;          // LevelEventGameModeSelector: 1 is career
constexpr std::uint32_t kPlayers = 0x0056a858;           // two skater object pointers
constexpr std::uint32_t kPlayerScore = 0x16c;            // skater object offsets
constexpr std::uint32_t kPlayerCharacter = 0x2cc0;
constexpr std::uint32_t kPlayerTapes = 0x31e4;
constexpr std::uint32_t kPlayerMedalFlag = 0x31e8;

constexpr std::uint32_t kLevelPickups = 0x0055faf0;      // pickups in this level
constexpr std::uint32_t kLiveCash = 0x0055fd2c;
constexpr std::uint32_t kGoalGapsGot = 0x0055fd30;       // bit per goal gap got this level
constexpr std::uint32_t kPickupText = 0x0055fd34;
constexpr std::uint32_t kTrickGapText = 0x0055fd54;
constexpr std::uint32_t kGameCleared = 0x0055fd74;
constexpr std::uint32_t kHighestOpen = 0x0055fd78;
constexpr std::uint32_t kGoalsGot = 0x0055fd7c;          // bit per goal got this level
constexpr std::uint32_t kPickupsGot = 0x0055fd80;
constexpr std::uint32_t kSkaterUnlockedCopy = 0x0055fd84;  // 21 dwords
constexpr std::uint32_t kTotalGaps = 0x0055fdd4;
constexpr std::uint32_t kGoalGapCount = 0x0055fdd8;
constexpr std::uint32_t kActiveGapCount = 0x0055fddc;
constexpr std::uint32_t kTrickGapsGot = 0x0055fde0;
constexpr std::uint32_t kGapText = 0x0055fde4;
constexpr std::uint32_t kTrickGapCount = 0x0055fe04;
constexpr std::uint32_t kCashIconCount = 0x0055fe08;
constexpr std::uint32_t kNewCheatCharacter = 0x0055fe0c;
constexpr std::uint32_t kSkaterFlagsCopy = 0x0055fcdc;   // 21 dwords ("Globals")

constexpr std::uint32_t kMoneyMaskVideo = 0x00568608;    // cash icons got while a movie plays
constexpr std::uint32_t kGapBits = 0x0056a23c;           // three words per level, levels 0-7 counted
constexpr std::uint32_t kCheats = 0x0056a374;            // 17 entries of 4 bytes: unlocked, character, on/off
constexpr std::uint32_t kUnlockFlags = 0x0056a68c;       // bit 4 cheat character 0x80, 8 / 0x10 open levels 8 / 9
constexpr std::uint32_t kAllLevelsOpen = 0x0056a8e4;     // the "all levels" cheat
constexpr std::uint32_t kNodeCount = 0x0056e214;
constexpr std::uint32_t kNodeList = 0x0056e210;          // pointer to an array of node record pointers
constexpr std::uint32_t kGapTablePointer = 0x0056dd58;   // ActiveGapDefinitionTable
constexpr std::uint32_t kGapRecordSize = 0x2c;
constexpr std::uint32_t kCustomParkFlag = 0x005614c4;    // byte: the level being played is the custom park
constexpr std::uint32_t kMsgGapCompleted = 0x00525190;   // printf_log format of a completed goal gap
constexpr std::uint32_t kPlayerPendingTrickGap = 0x3024; // skater object: the trick gap definition completed, until the player's state consumes it
constexpr std::uint32_t kPlayerPendingGoalGap = 0x3028;  // the same for a goal gap

constexpr std::uint32_t kMessageStyle = 0x00534f68;      // the style block the message service is passed
constexpr std::uint32_t kCashIconsClearedText = 0x0052e2e8;  // pointer variable

// Assertion messages (the addresses of the format strings).
constexpr std::uint32_t kMsgInvalidGoal = 0x00525058;
constexpr std::uint32_t kMsgNoProgress = 0x0052506c;
constexpr std::uint32_t kMsgBadMoneyMask = 0x00525084;
constexpr std::uint32_t kMsgTapeTwice = 0x00525094;
constexpr std::uint32_t kMsgInvalidLevel = 0x005250b4;
constexpr std::uint32_t kMsgTapeNotCareer = 0x005250cc;
constexpr std::uint32_t kMsgIllegalGoalType = 0x005250f4;
constexpr std::uint32_t kMsgMedalNotCareer = 0x00525110;
constexpr std::uint32_t kMsgGapNotFound = 0x00525138;
constexpr std::uint32_t kMsgNotGoalGap = 0x00525150;
constexpr std::uint32_t kMsgNoTrickManager = 0x0055fe10;   // TrickConfig_InitializeCharacter: the manager is null
constexpr std::uint32_t kMsgGapNotInLevel = 0x00525168;
constexpr std::uint32_t kMsgUnhandledTrickGap = 0x005251c0;
constexpr std::uint32_t kMsgGapAwardedWhenGotGoal = 0x005251f0;
constexpr std::uint32_t kMsgGapAwardedTwice = 0x00525218;
constexpr std::uint32_t kMsgNotTrickGap = 0x00525244;
constexpr std::uint32_t kMsgNotPowerup = 0x0052525c;
constexpr std::uint32_t kMsgBadNode = 0x00525284;
constexpr std::uint32_t kMsgTooManyGaps = 0x00525298;
constexpr std::uint32_t kMsgStatRange = 0x005252ac;
constexpr std::uint32_t kMsgBadTrickIndex = 0x0052531c;
constexpr std::uint32_t kMsgBadTrickIndexPlain = 0x00525330;   // "Bad trick index" (no number): TrickConfig_ConsumePointsAndSetBit
constexpr std::uint32_t kMsgCheatRange = 0x005253d4;
constexpr std::uint32_t kMsgUnknownCheat = 0x005253f0;

class Career final {
public:
    Career(RetailMemory& memory, RetailServices& services, skater::SkaterManager& skaters)
        : m_(memory), s_(services), sk_(skaters) {}

    // ---- the current character's record --------------------------------------------------
    [[nodiscard]] std::uint32_t current_record() { return record(sk_.current_character()); }  // TrickConfig_FindByCharacter(FUN_004b0c30())

    // ---- goals (tapes) -------------------------------------------------------------------
    [[nodiscard]] bool got(int goal);                                 // 0x00414030 Career_Got
    [[nodiscard]] bool just_got(int goal);                            // 0x004140c0 Career_JustGot
    [[nodiscard]] int level_tapes(std::uint32_t rec, int level);      // 0x00414130 Career_LevelTapes
    [[nodiscard]] int num_level_goals();                              // 0x004141a0 Career_NumLevelGoals
    [[nodiscard]] int count_money();                                  // 0x004141d0 Career_CountMoney
    void check_score();                                               // 0x00414280 Career_CheckScore
    void give_goal(int goal);                                         // 0x004142e0 Career_GiveGoal
    [[nodiscard]] int goal_index(int type);                           // 0x00414540 Career_GoalIndex
    void give_goal_type(int type);                                    // 0x00414570 Career_GiveGoalType
    void check_clear();                                               // 0x004145e0 Career_CheckClear
    [[nodiscard]] bool got_goal_type(int type);                       // 0x00414650 Career_GotGoalType
    void give_medal(int level, int medal_bit);                        // 0x00414670 Career_GiveMedal

    // ---- gaps ----------------------------------------------------------------------------
    [[nodiscard]] bool gap_active(std::uint32_t gap);                 // 0x00414770 Career_GapActive
    [[nodiscard]] bool gap_is_goal(std::uint32_t gap);                // 0x004147b0
    [[nodiscard]] bool gap_is_trick(std::uint32_t gap);               // 0x004147e0
    [[nodiscard]] int gap_goal_number(std::uint32_t gap);             // 0x00414810
    [[nodiscard]] int gap_trick_number(std::uint32_t gap);            // 0x004148f0
    [[nodiscard]] int gap_number(std::uint32_t gap);                  // 0x004149d0
    [[nodiscard]] bool got_goal_gap(std::uint32_t gap);               // 0x00414a50
    [[nodiscard]] bool got_trick_gap(std::uint32_t gap);              // 0x00414a80
    void give_goal_gap(std::uint32_t gap);                            // 0x00414ab0
    void give_trick_gap(std::uint32_t gap);                           // 0x00414ae0
    [[nodiscard]] std::uint32_t anyone_got_gap(std::uint32_t gap);    // 0x00414b10
    void give_gap(std::uint32_t gap);                                 // 0x00414b50
    [[nodiscard]] static int count_bits(std::uint32_t value);         // 0x00414b90
    [[nodiscard]] int num_goal_gaps_got();                            // 0x00414bb0
    [[nodiscard]] int num_trick_gaps_got();                           // 0x00414bc0
    void award_goal_gap(std::uint32_t gap);                           // 0x00414bd0
    void award_trick_gap(std::uint32_t gap);                          // 0x00414d90
    void complete_gap(std::uint32_t gap);                             // 0x00414c50 Gap_CompleteDefinition: the career records a completed gap definition
    [[nodiscard]] int count_gaps();                                   // 0x00415560 Career_CountGaps
    [[nodiscard]] bool got_all_gaps();                                // 0x00415590

    // ---- cash icons and pickups ----------------------------------------------------------
    [[nodiscard]] int money_number(int node);                         // 0x00414fa0 Career_MoneyNumber
    [[nodiscard]] bool got_money(int node);                           // 0x00415060 Career_GotMoney
    void give_money(int node, int amount);                            // 0x00415180 Career_GiveMoney
    void get_level_pickup();                                          // 0x004152f0 Career_GetLevelPickup
    void count_things();                                              // 0x00415380 Career_CountThings

    // ---- stat points ---------------------------------------------------------------------
    [[nodiscard]] bool buy_point(int stat, int cost);                 // 0x004155a0 Career_BuyPoint
    [[nodiscard]] bool sell_point(int stat);                          // 0x004156f0 Career_SellPoint
    [[nodiscard]] int point_cost();                                   // 0x00415820 Career_GetPointCost

    // ---- levels --------------------------------------------------------------------------
    [[nodiscard]] int num_levels_with_medals(std::uint32_t rec);      // 0x004158d0
    [[nodiscard]] bool level_open_career_mode(int level, std::uint32_t rec);  // 0x00415910
    [[nodiscard]] bool level_open(int level, int mode);               // 0x004159d0 Career_LevelOpen
    [[nodiscard]] int highest_open_level(int mode, bool with_specials);  // 0x00415a90
    [[nodiscard]] int level_needs(int level);                         // 0x00415ae0 Career_LevelNeeds

    // ---- records -------------------------------------------------------------------------
    [[nodiscard]] bool is_trick_purchased(std::uint32_t rec, int trick);   // 0x00416070
    // 0x00416140 TrickConfig_ConsumePointsAndSetBit: false (nothing written) when the record has less than `cost` cash to spend;
    // otherwise the cash drops by `cost` and the trick's purchased bit is set. The memory form of trick_config_consume_points_and_set_bit.
    bool consume_points_and_set_bit(std::uint32_t rec, int trick, int cost);
    [[nodiscard]] int count_gold_medals(std::uint32_t rec);           // 0x004164b0
    [[nodiscard]] int count_medals(std::uint32_t rec);                // 0x00416530
    [[nodiscard]] bool game_cleared(int character);                   // 0x00416740
    void clear_game(int character);                                   // 0x00416790
    void clear_game_with_everyone();                                  // 0x00416890

    // ---- cheats and locked characters ----------------------------------------------------
    void unlock_cheat(int cheat, int character);                      // 0x004168b0
    [[nodiscard]] bool cheat_unlocked(int cheat);                     // 0x00416940
    [[nodiscard]] std::uint8_t cheat_state(int cheat);                // 0x00416960
    [[nodiscard]] std::uint8_t get_cheat(int cheat);                  // 0x00416980
    void set_cheat(int cheat, int value, std::uint32_t apply);        // 0x00416a00
    void toggle_cheat(int cheat, std::uint32_t apply);                // 0x00416c90
    [[nodiscard]] int new_cheat();                                    // 0x00416cd0
    [[nodiscard]] int count_unlocked_cheats();                        // 0x00416de0
    [[nodiscard]] std::uint32_t cheat_name(int cheat);                // 0x00416e30 (the text pointer)
    [[nodiscard]] static int cheat_type(int cheat) noexcept;          // 0x00416f50
    void unlock_character(std::uint32_t mask);                        // 0x00416f80
    [[nodiscard]] bool character_is_unlocked(std::uint32_t mask);     // 0x00416fb0
    void apply_cheats();                                              // 0x00417060
    void capture_skater_defaults();                                   // 0x00417300
    void reset_runtime_state();                                       // 0x00417330

    // ---- options and the live copy of the saved settings ---------------------------------
    void reset_controller_defaults(int player);                       // 0x00413f30
    void options_init();                                              // 0x00413f70

    // ---- the save image --------------------------------------------------------------------
    // What Career_ResetSaveImage takes from outside: the loaded TRICKS.BIN it builds the trick
    // database from (Tricks_InitializeManager).
    struct ResetSource final {
        std::vector<std::uint8_t> tricks_image;
    };
    void rebuild_career_pointers();                                   // 0x00413ed0
    void serialize_live_state();                                      // 0x00413c10
    void apply_serialized_state();                                    // 0x00413d60
    void prepare_save();                                              // 0x004166f0
    void apply_loaded_save(const ResetSource& reset_source);          // 0x004165d0
    void reset_save_image(const ResetSource& reset_source);           // 0x004138f0
    void initialize_trick_config(int character, std::uint32_t manager);   // 0x004170d0 TrickConfig_InitializeCharacter(character, manager)
    void initialize_trick_config(int character, const TrickDatabase& db, std::span<const std::uint8_t> tricks_image);
    void apply_trick_unlock_state(std::uint32_t manager, int character, bool alternate);   // 0x004bbd70 TrickDatabase_ApplyUnlockState (thiscall on the manager)
    void save_custom_appearance(int slot);                            // 0x004171e0
    void load_custom_appearance(int slot);                            // 0x00417260

private:
    [[nodiscard]] std::int32_t current_level() const { return guest::read_scalar<std::int32_t>(m_, kCurrentLevel); }
    [[nodiscard]] std::uint32_t player() const { return guest::read_scalar<std::uint32_t>(m_, kPlayers); }
    [[nodiscard]] std::uint32_t level_row(int level) const { return career::level(level); }
    [[nodiscard]] std::uint32_t goal_row(int level, int goal) const {
        return career::level(level) + kLvGoals + static_cast<std::uint32_t>(goal) * kGoalStride;
    }
    [[nodiscard]] std::uint32_t mask_address(std::uint32_t rec, int level) const {
        return rec + kRecLevelMasks + static_cast<std::uint32_t>(level) * 2;
    }
    [[nodiscard]] std::uint32_t contest_flag(int level) const { return guest::read_scalar<std::uint8_t>(m_, career::level(level) + kLvContest); }
    void message(std::uint32_t text);
    void format_progress(std::uint32_t destination, std::int32_t a, std::int32_t b, std::uint32_t noun);
    void format_trick_progress(std::uint32_t destination, std::int32_t a, std::int32_t b);
    void check_goal(int goal) { s_.check(goal >= 0 && goal <= 9, kMsgInvalidGoal); }
    [[nodiscard]] int gap_ordinal(std::uint32_t gap, bool (Career::*filter)(std::uint32_t), std::uint32_t not_found);
    [[nodiscard]] int rnd(int limit);  // Rnd(limit): the value is scripted, the call is reported

    RetailMemory& m_;
    RetailServices& s_;
    skater::SkaterManager& sk_;
};

}  // namespace career
}  // namespace opentony::runtime
