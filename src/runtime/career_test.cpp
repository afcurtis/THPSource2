// Hand-built checks of the career and skater-manager ports (Phase 4.8) on a synthetic memory
// image: the static tables from the executable and zeroed mutable regions. The retail replay test
// (career_retail_calls_test.cpp) is the evidence; these pin the rules in readable scenarios.
#include "career.hpp"
#include "career_tables.hpp"
#include "skater_manager.hpp"
#include "trick_database.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::career;

namespace {

int failures = 0;

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

RetailMemory make_memory() {
    RetailMemory memory;
    add_career_static_regions(memory);
    add_career_live_regions(memory);
    memory.add_region(0x00600000, 0x3538);  // a skater object
    return memory;
}

struct Setup {
    RetailMemory memory{make_memory()};
    RetailServices services;
    skater::SkaterManager skaters{memory, services};
    Career career{memory, services, skaters};
    Setup() {
        memory.set32(kPlayers, 0x00600000);
        memory.set32(kGameMode, 1);
    }
};

void test_level_table_and_goal_lookup() {
    Setup w;
    CHECK(w.memory.cstring(level(0)) == "The Hangar");
    w.memory.set32(kCurrentLevel, 0);
    CHECK(w.career.goal_index(kGoalTypeSkate) == 3);
    CHECK(w.career.goal_index(kGoalTypeAll) == 9);
    CHECK(w.career.goal_index(99) == -1);
}

void test_giving_a_tape_awards_cash_and_reports_it() {
    Setup w;
    w.memory.set32(kCurrentLevel, 0);
    w.memory.set32(0x00600000 + kPlayerScore, 0);
    const std::uint32_t rec = w.career.current_record();
    CHECK(!w.career.got(3));
    w.career.give_goal(3);
    CHECK(w.career.got(3));
    CHECK(w.career.just_got(3));
    CHECK(w.memory.u8(rec + kRecTapes) == 1);
    CHECK(w.memory.s32(rec + kRecCash) == 150);      // the S-K-A-T-E tape's reward
    CHECK(w.memory.s32(rec + kRecCashTotal) == 150);
    CHECK(w.memory.u32(0x00600000 + kPlayerTapes) == 1);
    const auto& calls = w.services.calls();
    CHECK(calls.size() == 2);
    CHECK(calls[0].name == "sfx" && calls[0].args[0] == 0x17c);
    CHECK(calls[1].name == "message" && calls[1].text == "Collect S-K-A-T-E");
    // A second award is reported as an assertion and still applies, as retail does.
    w.services.clear();
    w.career.give_goal(3);
    CHECK(!w.services.calls().empty() && w.services.calls()[0].name == "assert" &&
          w.services.calls()[0].args[0] == kMsgTapeTwice);
}

void test_score_goals_are_given_when_the_score_is_reached() {
    Setup w;
    w.memory.set32(kCurrentLevel, 0);
    w.memory.set32(0x00600000 + kPlayerScore, 26000);  // beats 10,000 and 25,000 but not 75,000
    w.career.check_score();
    CHECK(w.career.got(0));
    CHECK(w.career.got(1));
    CHECK(!w.career.got(2));
}

void test_gap_numbering_and_awarding() {
    Setup w;
    // Two gap records in a table: [flags, kind, id, ...] (0x2c bytes); the list ends at id 0xffff.
    w.memory.add_region(0x00700000, 3 * kGapRecordSize);
    const std::uint32_t table = 0x00700000;
    w.memory.set32(kGapTablePointer, table);
    w.memory.set16(table + 2, 1);      // goal gap
    w.memory.set16(table + 4, 2710);   // level 0 range is 2700..2799
    w.memory.set16(table + kGapRecordSize + 2, 2);   // trick gap
    w.memory.set16(table + kGapRecordSize + 4, 2720);
    w.memory.set16(table + 2 * kGapRecordSize + 4, 0xffff);
    w.memory.set32(kCurrentLevel, 0);
    w.memory.set32(kGameMode, 1);
    CHECK(w.career.gap_active(table));
    CHECK(w.career.gap_is_goal(table));
    CHECK(!w.career.gap_is_goal(table + kGapRecordSize));
    CHECK(w.career.gap_is_trick(table + kGapRecordSize));
    CHECK(w.career.gap_number(table + kGapRecordSize) == 1);
    CHECK(w.career.gap_trick_number(table + kGapRecordSize) == 0);
    w.memory.set32(kGoalGapCount, 3);
    w.career.award_goal_gap(table);
    CHECK(w.memory.u32(kGoalGapsGot) == 1);
    CHECK(w.services.calls().back().name == "message");
    CHECK(w.services.calls().back().text.substr(0, 6) == "1 of 3");
}

void test_levels_open_with_cash() {
    Setup w;
    const std::uint32_t rec = record(0);
    CHECK(w.career.level_open_career_mode(0, rec));
    // School II needs 1000 total cash (level table +0x7c).
    CHECK(!w.career.level_open_career_mode(1, rec));
    w.memory.set32(rec + kRecCashTotal, 1000);
    CHECK(w.career.level_open_career_mode(1, rec));
    w.memory.set32(kAllLevelsOpen, 1);
    CHECK(w.career.level_open_career_mode(9, record(5)));
}

void test_clear_game_makes_a_cleared_character() {
    Setup w;
    w.career.clear_game(0);
    CHECK(w.career.game_cleared(0));
    CHECK(w.memory.u8(record(0) + kRecFlags) == 0x40);
    CHECK(w.memory.s32(record(0) + kRecCashTotal) == 900000);
    CHECK(!w.career.game_cleared(1));
}

void test_cheats() {
    Setup w;
    CHECK(!w.career.cheat_unlocked(2));
    w.career.unlock_cheat(2, 7);
    CHECK(w.career.cheat_unlocked(2));
    CHECK(w.career.count_unlocked_cheats() == 1);
    CHECK(w.career.cheat_type(2) == 0 && w.career.cheat_type(8) == 2 && w.career.cheat_type(4) == 1);
    w.services.script("pack_new_free", 0);
    w.career.set_cheat(7, 1, 0);
    CHECK(w.memory.u32(0x0056b7f4) == 10);
    w.career.set_cheat(7, 0, 0);
    CHECK(w.memory.u32(0x0056b7f4) == 0);
    // Kid mode and big head exclude each other.
    w.services.script("pack_new_free", 0);
    w.services.script("pack_new_free", 0);
    w.career.set_cheat(0xb, 1, 0);
    w.career.set_cheat(4, 1, 0);
    CHECK(w.career.cheat_state(0xb) == 0);
    CHECK(w.career.cheat_state(4) == 1);
}

void test_skater_manager_selects_characters_and_costumes() {
    Setup w;
    w.skaters.set_active_slot(0);
    w.skaters.select_current(3);
    CHECK(w.skaters.current_character() == 3);
    CHECK(w.skaters.current_costume() == 0);
    CHECK(w.skaters.count_costume_names(3) >= 2);
    w.skaters.select_costume(0, 1, true);
    CHECK(w.skaters.costume_of_slot(0) == 1);
    CHECK(w.skaters.remembered_costume(3) == 1);
    // Past the last costume the choice wraps to the first.
    w.skaters.select_costume(0, w.skaters.count_costume_names(3), true);
    CHECK(w.skaters.costume_of_slot(0) == 0);
    w.skaters.select_costume(0, 1, true);
    // The costume a character was last worn in is restored when it is picked again.
    w.skaters.select_current(4);
    w.skaters.select_current(3);
    CHECK(w.skaters.current_costume() == 1);
    w.services.clear();
    w.skaters.set_active_slot(9);
    CHECK(w.services.calls().size() == 1 && w.services.calls()[0].name == "assert");
}

void test_save_image_round_trip() {
    Setup w;
    w.career.options_init();
    w.memory.set32(0x0055fb3c, 7);   // music volume
    w.memory.set32(0x0055fb40, 3);
    w.career.serialize_live_state();
    CHECK(w.memory.u32(0x0056a2e8) == 7);
    CHECK(w.memory.u32(0x0052be44) == 7u * 0x555);
    w.memory.set32(0x0055fb3c, 0);
    w.memory.set32(0x0055fb40, 0);
    w.career.apply_serialized_state();
    CHECK(w.memory.u32(0x0055fb3c) == 7);
    CHECK(w.memory.u32(0x0055fb40) == 3);
    CHECK(w.memory.cstring(0x0055fccc) == "HORSE");
    CHECK(w.memory.cstring(0x0056a478) == "HORSE");
}

// TrickDatabase_ApplyUnlockState (0x004bbd70) on retail memory, over a synthetic TRICKS.BIN at the pointer 0x0056a894: two tricks in section 5 (record 0 at
// source 0x80, record 1 at 0xa0), character 0's stream names record 0, section 6 names record 1. The unlock is free and keeps the record's other bits.
void test_trick_unlock_state_on_retail_memory() {
    Setup w;
    constexpr std::uint32_t kImageBase = 0x00700000;
    std::vector<std::uint8_t> image(33168, 0);
    auto put16 = [&](std::size_t at, std::uint16_t value) {
        image[at] = static_cast<std::uint8_t>(value);
        image[at + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    auto put_words = [&](std::size_t at, std::initializer_list<std::uint16_t> words) {
        for (std::uint16_t word : words) {
            put16(at, word);
            at += 2;
        }
    };
    auto put_script = [&](std::size_t at, std::initializer_list<std::uint8_t> bytes) {
        for (std::uint8_t b : bytes) {
            image[at++] = b;
        }
    };
    put16(0, 0x20);    // section 0: the per-character stream offsets
    put16(10, 0x40);   // section 5: GAllTricks
    put16(12, 0x140);  // section 6: the alternate stream
    put16(0x20, 0x100);
    put_words(0x40, {2, 1, 9, 0x80, 0x0000, 2, 1, 1, 0xa0, 0x0000, 0});
    put_script(0x80, {0x0b, '{', 'G', 'r', 'i', 'n', 'd', '}', 0, 0x51, 5, 0, 0x07});
    put_script(0xa0, {0x50, '{', 'O', 'l', 'l', 'i', 'e', '}', 0, 0x07});
    put_words(0x100, {2, 1, 12, 0x80, 0x0000, 0});
    put_words(0x140, {1, 5, 0xa0, 0x0000, 0});
    w.memory.add_region(kImageBase, image);
    w.memory.set32(0x0056a894, kImageBase);

    const std::uint32_t rec = record(0);
    w.memory.set32(rec + kConfigPoints, 7);
    w.memory.set32(rec + kConfigUnlocks + 4, 0x10);
    w.career.apply_trick_unlock_state(0x00600000, 0, true);  // the trick menu's call with cheat 0 unlocked
    CHECK(w.memory.u32(rec + kConfigUnlocks) == 0x2);
    CHECK(w.memory.u32(rec + kConfigUnlocks + 4) == 0x10);
    CHECK(w.memory.s32(rec + kConfigPoints) == 7);
    w.career.apply_trick_unlock_state(0x00600000, 0, false);  // the character's own stream
    CHECK(w.memory.u32(rec + kConfigUnlocks) == 0x3);
    CHECK(w.memory.u32(record(1) + kConfigUnlocks) == 0);
}

// 0x00416140, the trick shop's buy: the Career form (retail memory) against the image form of trick_database.cpp, over random cash, costs and
// indices, plus the plain cases. A cost above the cash writes nothing; otherwise the cash drops and the purchased bit is set.
void test_buying_a_trick_spends_cash_and_marks_it_purchased() {
    Setup w;
    const TrickDatabase db;
    const std::uint32_t rec = record(3);
    w.memory.set32(rec + kRecCash, 500);
    CHECK(!w.career.consume_points_and_set_bit(rec, 40, 501));
    CHECK(w.memory.s32(rec + kRecCash) == 500);
    CHECK(!w.career.is_trick_purchased(rec, 40));
    CHECK(w.career.consume_points_and_set_bit(rec, 40, 500));
    CHECK(w.memory.s32(rec + kRecCash) == 0);
    CHECK(w.career.is_trick_purchased(rec, 40));
    CHECK(w.memory.u32(rec + kRecUnlocks + 4) == (1u << 8));
    CHECK(w.services.calls().empty());

    std::uint32_t seed = 0x2468ace1u;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int i = 0; i < 2000; ++i) {
        const std::int32_t cash = static_cast<std::int32_t>(next() % 4000) - 500;
        const int pick = static_cast<int>(next() % 5);
        const std::int32_t cost = pick == 0 ? 0 : pick == 1 ? cash : pick == 2 ? cash + 1 : static_cast<std::int32_t>(next() % 3000) - 100;
        const int trick = static_cast<int>(next() % 0x80);
        w.memory.set32(rec + kRecCash, static_cast<std::uint32_t>(cash));
        RetailImage image(kTrickConfigSize);
        for (std::uint32_t k = 0; k < 4; ++k) {
            const std::uint32_t bits = next() ^ (next() << 16);
            w.memory.set32(rec + kRecUnlocks + 4 * k, bits);
            image.set32(kConfigUnlocks + 4 * k, bits);
        }
        image.set32(kConfigPoints, static_cast<std::uint32_t>(cash));
        const bool expected = trick_config_consume_points_and_set_bit(image, db, trick, cost);
        CHECK(w.career.consume_points_and_set_bit(rec, trick, cost) == expected);
        CHECK(w.memory.s32(rec + kRecCash) == image.s32(kConfigPoints));
        for (std::uint32_t k = 0; k < 4; ++k) {
            CHECK(w.memory.u32(rec + kRecUnlocks + 4 * k) == image.u32(kConfigUnlocks + 4 * k));
        }
    }
    CHECK(w.services.calls().empty());
    w.career.consume_points_and_set_bit(rec, 0x80, 0);  // retail asserts "Bad trick index" and carries on
    CHECK(!w.services.calls().empty() && w.services.calls().back().args == std::vector<std::uint32_t>{kMsgBadTrickIndexPlain});
}

}  // namespace

int main() {
    test_level_table_and_goal_lookup();
    test_giving_a_tape_awards_cash_and_reports_it();
    test_score_goals_are_given_when_the_score_is_reached();
    test_gap_numbering_and_awarding();
    test_levels_open_with_cash();
    test_clear_game_makes_a_cleared_character();
    test_cheats();
    test_skater_manager_selects_characters_and_costumes();
    test_save_image_round_trip();
    test_trick_unlock_state_on_retail_memory();
    test_buying_a_trick_spends_cash_and_marks_it_purchased();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "career: all checks passed\n";
    return EXIT_SUCCESS;
}
