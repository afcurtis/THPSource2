// Replays retail forced calls (Phase 4.8) through the native career / skater-manager code:
//
//   opentony_career_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/career_calls.py) holds the
// retail memory regions the ported code touches and, for every case, the state, the arguments and
// what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "career.hpp"
#include "competition.hpp"
#include "retail_calls_replay.hpp"
#include "skater_manager.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    skater::SkaterManager sk;
    career::Career career;
    competition::Competition comp;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), sk(mem, svc), career(mem, svc, sk), comp(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using CareerAdapter = Adapter<Ctx>;

// TRICKS.BIN as the retail process has it loaded (the pointer at 0x0056a894, 33168 bytes).
career::Career::ResetSource reset_source(Ctx& c) {
    career::Career::ResetSource source;
    source.tricks_image = c.mem.bytes(c.mem.u32(0x0056a894), 33168);
    return source;
}

#define A(expr) [](Ctx& c, const Args& a, std::uint32_t) -> std::uint32_t { (void)a; (void)c; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t) -> std::uint32_t { (void)a; (void)c; expr; return 0; }

const std::map<std::uint32_t, CareerAdapter>& adapters() {
    static const std::map<std::uint32_t, CareerAdapter> table = {
        // ---- goals ----
        {0x00414030, {"Career_Got", 0xff, false, A(c.career.got(S(a[0])))}},
        {0x004140c0, {"Career_JustGot", 0xff, false, A(c.career.just_got(S(a[0])))}},
        {0x00414130, {"Career_LevelTapes", 0xffffffff, false, A(c.career.level_tapes(a[0], S(a[1])))}},
        {0x004141a0, {"Career_NumLevelGoals", 0xffffffff, false, A(c.career.num_level_goals())}},
        {0x004141d0, {"Career_CountMoney", 0xffffffff, false, A(c.career.count_money())}},
        {0x00414280, {"Career_CheckScore", 0, false, V(c.career.check_score())}},
        {0x004142e0, {"Career_GiveGoal", 0, false, V(c.career.give_goal(S(a[0])))}},
        {0x00414540, {"Career_GoalIndex", 0xffffffff, false, A(c.career.goal_index(S(a[0])))}},
        {0x00414570, {"Career_GiveGoalType", 0, false, V(c.career.give_goal_type(S(a[0])))}},
        {0x004145e0, {"Career_CheckClear", 0, false, V(c.career.check_clear())}},
        {0x00414650, {"Career_GotGoalType", 0xff, false, A(c.career.got_goal_type(S(a[0])))}},
        {0x00414670, {"Career_GiveMedal", 0, false, V(c.career.give_medal(S(a[0]), static_cast<int>(a[1] & 0xff)))}},
        // ---- gaps ----
        {0x00414770, {"Career_GapActive", 0xff, false, A(c.career.gap_active(a[0]))}},
        {0x004147b0, {"Career_GapIsGoal", 0xff, false, A(c.career.gap_is_goal(a[0]))}},
        {0x004147e0, {"Career_GapIsTrick", 0xff, false, A(c.career.gap_is_trick(a[0]))}},
        {0x00414810, {"Career_GapGoalNumber", 0xffffffff, true, A(c.career.gap_goal_number(a[0]))}},
        {0x004148f0, {"Career_GapTrickNumber", 0xffffffff, true, A(c.career.gap_trick_number(a[0]))}},
        {0x004149d0, {"Career_GapNumber", 0xffffffff, true, A(c.career.gap_number(a[0]))}},
        {0x00414a50, {"Career_GotGoalGap", 0xff, true, A(c.career.got_goal_gap(a[0]))}},
        {0x00414a80, {"Career_GotTrickGap", 0xff, true, A(c.career.got_trick_gap(a[0]))}},
        {0x00414ab0, {"Career_GiveGoalGap", 0, false, V(c.career.give_goal_gap(a[0]))}},
        {0x00414ae0, {"Career_GiveTrickGap", 0, false, V(c.career.give_trick_gap(a[0]))}},
        {0x00414b10, {"Career_AnyoneGotGap", 0xffffffff, true, A(c.career.anyone_got_gap(a[0]))}},
        {0x00414b50, {"Career_GiveGap", 0, false, V(c.career.give_gap(a[0]))}},
        {0x00414b90, {"Career_CountBits", 0xffffffff, false, A(career::Career::count_bits(a[0]))}},
        {0x00414bb0, {"Career_NumGoalGapsGot", 0xffffffff, false, A(c.career.num_goal_gaps_got())}},
        {0x00414bc0, {"Career_NumTrickGapsGot", 0xffffffff, false, A(c.career.num_trick_gaps_got())}},
        {0x00414bd0, {"Career_AwardGoalGap", 0, false, V(c.career.award_goal_gap(a[0]))}},
        {0x00414c50, {"Gap_CompleteDefinition", 0, false, V(c.career.complete_gap(a[0]))}},
        {0x00414d90, {"Career_AwardTrickGap", 0, false, V(c.career.award_trick_gap(a[0]))}},
        {0x00415560, {"Career_CountGaps", 0xffffffff, false, A(c.career.count_gaps())}},
        {0x00415590, {"Career_GotAllGaps", 0xff, false, A(c.career.got_all_gaps())}},
        // ---- cash icons ----
        {0x00414fa0, {"Career_MoneyNumber", 0xffffffff, false, A(c.career.money_number(S(a[0])))}},
        {0x00415060, {"Career_GotMoney", 0xff, false, A(c.career.got_money(S(a[0])))}},
        {0x00415180, {"Career_GiveMoney", 0, false, V(c.career.give_money(S(a[0]), S(a[1])))}},
        {0x004152f0, {"Career_GetLevelPickup", 0, false, V(c.career.get_level_pickup())}},
        {0x00415380, {"Career_CountThings", 0, false, V(c.career.count_things())}},
        // ---- stat points ----
        {0x004155a0, {"Career_BuyPoint", 0xff, false, A(c.career.buy_point(S(a[0]), S(a[1])))}},
        {0x004156f0, {"Career_SellPoint", 0xff, false, A(c.career.sell_point(S(a[0])))}},
        {0x00415820, {"Career_GetPointCost", 0xffffffff, false, A(c.career.point_cost())}},
        // ---- levels ----
        {0x004158d0, {"Career_NumLevelsWithMedals", 0xffffffff, false, A(c.career.num_levels_with_medals(a[0]))}},
        {0x00415910, {"Career_LevelOpenCareerMode", 0xffffffff, false, A(c.career.level_open_career_mode(S(a[0]), a[1]))}},
        {0x004159d0, {"Career_LevelOpen", 0xffffffff, false, A(c.career.level_open(S(a[0]), S(a[1])))}},
        {0x00415a90, {"Career_HighestOpenLevel", 0xffffffff, false, A(c.career.highest_open_level(S(a[0]), (a[1] & 0xff) != 0))}},
        {0x00415ae0, {"Career_LevelNeeds", 0xffffffff, false, A(c.career.level_needs(S(a[0])))}},
        // ---- records ----
        {0x00416070, {"Career_IsTrickPurchased", 0xff, false, A(c.career.is_trick_purchased(a[0], S(a[1])))}},
        {0x00416140, {"TrickConfig_ConsumePointsAndSetBit", 0xff, false, A(c.career.consume_points_and_set_bit(a[0], S(a[1]), S(a[2])))}},
        {0x004164b0, {"Career_CountGoldMedals", 0xffffffff, false, A(c.career.count_gold_medals(a[0]))}},
        {0x00416530, {"Career_CountMedals", 0xffffffff, false, A(c.career.count_medals(a[0]))}},
        {0x00416740, {"Career_GameCleared", 0xff, false, A(c.career.game_cleared(S(a[0])))}},
        {0x00416790, {"Career_ClearGame", 0, false, V(c.career.clear_game(S(a[0])))}},
        {0x00416890, {"Career_ClearGameWithEveryone", 0, false, V(c.career.clear_game_with_everyone())}},
        // ---- cheats, locked characters, options ----
        {0x004168b0, {"Career_UnlockCheat", 0, false, V(c.career.unlock_cheat(S(a[0]), S(a[1])))}},
        {0x00416940, {"Career_CheatUnlocked", 0xff, false, A(c.career.cheat_unlocked(S(a[0])))}},
        {0x00416960, {"Career_CheatState", 0xff, false, A(static_cast<std::uint32_t>(c.career.cheat_state(S(a[0]))))}},
        {0x00416980, {"Career_GetCheat", 0xff, false, A(static_cast<std::uint32_t>(c.career.get_cheat(S(a[0]))))}},
        {0x00416a00, {"Career_SetCheat", 0, false, V(c.career.set_cheat(S(a[0]), static_cast<std::int8_t>(a[1] & 0xff), a[2]))}},
        {0x00416c90, {"Career_ToggleCheat", 0, false, V(c.career.toggle_cheat(S(a[0]), a[1]))}},
        {0x00416cd0, {"Career_NewCheat", 0xffffffff, false, A(c.career.new_cheat())}},
        {0x00416de0, {"Career_CountUnlockedCheats", 0xffffffff, false, A(c.career.count_unlocked_cheats())}},
        {0x00416e30, {"Career_CheatName", 0xffffffff, false, A(c.career.cheat_name(S(a[0])))}},
        {0x00416f50, {"Career_CheatType", 0xffffffff, false, A(career::Career::cheat_type(S(a[0])))}},
        {0x00416f80, {"Career_UnlockCharacter", 0, false, V(c.career.unlock_character(a[0]))}},
        {0x00416fb0, {"Career_CharacterIsUnlocked", 0xffffffff, true, A(c.career.character_is_unlocked(a[0]))}},
        {0x00417060, {"Career_ApplyCheats", 0, false, V(c.career.apply_cheats())}},
        {0x00417300, {"Career_CaptureSkaterDefaults", 0, false, V(c.career.capture_skater_defaults())}},
        {0x00417330, {"Career_ResetRuntimeState", 0, false, V(c.career.reset_runtime_state())}},
        {0x00413f30, {"Options_ResetControllerDefaults", 0, false, V(c.career.reset_controller_defaults(S(a[0])))}},
        {0x00413f70, {"Options_Init", 0, false, V(c.career.options_init())}},
        // ---- the save image ----
        {0x00413ed0, {"Career_RebuildCareerPointers", 0, false, V(c.career.rebuild_career_pointers())}},
        {0x00413c10, {"Career_SerializeLiveState", 0, false, V(c.career.serialize_live_state())}},
        {0x00413d60, {"Career_ApplySerializedState", 0, false, V(c.career.apply_serialized_state())}},
        {0x004166f0, {"Career_PrepareSave", 0, false, V(c.career.prepare_save())}},
        {0x004165d0, {"Career_ApplyLoadedSave", 0, false, V(c.career.apply_loaded_save(reset_source(c)))}},
        {0x004171e0, {"Career_SaveCustomAppearance", 0, false, V(c.career.save_custom_appearance(S(a[0])))}},
        {0x00417260, {"Career_LoadCustomAppearance", 0, false, V(c.career.load_custom_appearance(S(a[0])))}},
        {0x004138f0, {"Career_ResetSaveImage", 0, false, V(c.career.reset_save_image(reset_source(c)))}},
        // ---- the skater manager ----
        {0x004b0bc0, {"SkaterManager_SetActiveSlot", 0, false, V(c.sk.set_active_slot(S(a[0])))}},
        {0x004b0c30, {"SkaterManager_CurrentCharacter", 0xffffffff, false, A(c.sk.current_character())}},
        {0x004b0c40, {"SkaterManager_CurrentCostume", 0xffffffff, false, A(c.sk.current_costume())}},
        {0x004b0c50, {"SkaterManager_CurrentCustomSlot", 0xffffffff, false, A(c.sk.current_custom_slot())}},
        {0x004b0c60, {"Skater_GetResourceSlot", 0xffffffff, false, A(c.sk.character_of_slot(S(a[0])))}},
        {0x004b0cd0, {"SkaterManager_GetCostume", 0xffffffff, false, A(c.sk.costume_of_slot(S(a[0])))}},
        {0x004b0d40, {"SkaterManager_SelectCurrent", 0, false, V(c.sk.select_current(S(a[0])))}},
        {0x004b0e60, {"SkaterManager_SelectCharacter", 0, false, V(c.sk.select_character(S(a[0]), S(a[1])))}},
        {0x004b0f80, {"Skater_CountCostumeNames", 0xffffffff, false, A(c.sk.count_costume_names(S(a[0])))}},
        {0x004b0fe0, {"SkaterManager_SelectCostume", 0, false, V(c.sk.select_costume(S(a[0]), S(a[1]), (a[2] & 0xff) != 0))}},
        {0x004b1070, {"SkaterManager_SetCustomSlot", 0, false, V(c.sk.set_custom_slot(S(a[0]), S(a[1])))}},
        {0x004b1130, {"SkaterManager_GetRememberedCostume", 0xffffffff, false, A(c.sk.remembered_costume(S(a[0])))}},
        {0x004b1140, {"SkaterManager_Nop", 0, false, V((void)0)}},
        {0x004b1150, {"SkaterManager_ReleaseAllLoaded", 0, false, V(c.sk.release_all_loaded())}},
        {0x004b1180, {"SkaterManager_ReleaseActiveLoaded", 0, false, V(c.sk.release_active_loaded())}},
        {0x004b11c0, {"SkaterManager_LoadCustomIntoActive", 0, false, V(c.sk.load_custom_into_active(S(a[0])))}},
        {0x004b1240, {"SkaterManager_FormatName", 0, false, V(c.sk.format_name(S(a[0]), a[1]))}},
        {0x004b1360, {"SkaterManager_FaceBitmap", 0xffffffff, false, A(c.sk.face_bitmap(S(a[0])))}},
        {0x004b1430, {"Skater_GetSlotRecord", 0xffffffff, false, A(c.sk.slot_loaded_flag(S(a[0])))}},
        {0x004b14a0, {"SkaterManager_SwapCustomRecord", 0, false, V(c.sk.swap_custom_record(S(a[0])))}},
        {0x004b1570, {"SkaterManager_LoadedBlock", 0xffffffff, false, A(c.sk.loaded_block(S(a[0])))}},
        {0x004b1630, {"SkaterManager_SlotIsSelectable", 0xff, false, A(c.sk.slot_is_selectable(S(a[0])))}},
        {0x004b1700, {"Skater_SelectModelName", 0xffffffff, false, A(c.sk.select_model_name(S(a[0]), S(a[1])))}},
        {0x004b1810, {"Skater_CustomSlotIndex", 0xffffffff, false, A(c.sk.custom_index(S(a[0])))}},
        {0x004b1890, {"Skater_ResetActivePlayer", 0, false, V(c.sk.reset_active_player())}},
        {0x004b18a0, {"Skater_SelectManagerSlot", 0xff, false, A(c.sk.select_slot(S(a[0])))}},
        {0x004b18d0, {"SkaterManager_SelectCharacterChecked", 0, false, V(c.sk.select_character_checked(S(a[0]), S(a[1])))}},
        {0x004b1980, {"Skater_MapEnabledIndex", 0xffffffff, false, A(c.sk.map_enabled_index(S(a[0])))}},
        {0x004b19f0, {"Skater_CustomWithFlag", 0xffffffff, false, A(c.sk.custom_with_flag(S(a[0])))}},
        {0x004b1a30, {"Skater_DisplayName", 0xffffffff, false, A(c.sk.display_name(S(a[0])))}},
        // ---- the competition's judging and ranking ----
        {0x00419460, {"Competition_AllMedalsWon", 0xff, false, A(c.comp.all_medals_won())}},
        {0x00419500, {"Competition_AllGoldsWon", 0xff, false, A(c.comp.all_golds_won())}},
        {0x00419880, {"Competition_AssignHeatOrder", 0, false, V(c.comp.assign_heat_order())}},
        {0x00419910, {"Competition_JudgeScore", 0, false, V(c.comp.judge_score(S(a[0])))}},
        {0x00419cc0, {"Competition_AiScore", 0, false, V(c.comp.ai_score(S(a[0])))}},
        {0x00419f90, {"Competition_MedalDisplayDone", 0xff, false, A(c.comp.medal_display_done())}},
        {0x00419fb0, {"Competition_NextHuman", 0xff, false, A(c.comp.next_human())}},
        {0x0041aee0, {"Competition_SortRanking", 0, false, V(c.comp.sort_ranking())}},
        {0x004b8410, {"Tag_StatModifier", 0xffffffff, false, A(c.comp.tag_stat_modifier(a[0], S(a[1]), S(a[2])))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    // 0x00525138: "Gap %s not found", after which the ordinal the caller uses is whatever the assert routine left in eax.
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); }, 0x00525138);
    return replay.run_files(argc, argv, "career retail calls");
}
