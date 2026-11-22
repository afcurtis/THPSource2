// Hand-built checks of the trick-list screen's decision logic (Phase 4.7b): the rules in
// trickwin.cpp that do not touch a widget, derived from the disassembly of 0x004c3350, 0x004c1310,
// 0x004c2890, 0x004c2800, 0x004c3cf0, 0x004c36d0 and 0x004c38f0. The retail window needs the
// frontend widget toolkit to run, so these are not trace-validated (see the slice notes).
#include "trick_menu.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

using namespace opentony::runtime;

namespace {

int failures = 0;

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

using Block = std::array<std::uint8_t, kBindingBlockSize>;

Block empty_block() {
    Block block{};
    block.fill(kBindingEmpty);
    return block;
}

TrickDatabase sample_database() {
    TrickDatabase db;
    const std::uint16_t costs[] = {500, 100, 500, 50, 100, 0, 75, 75};
    for (std::uint16_t i = 0; i < 8; ++i) {
        db.add_or_merge("t" + std::to_string(i), 0x4000, costs[i], static_cast<std::uint16_t>(0x100 + i), 0);
    }
    return db;
}

void categories_and_glyphs() {
    CHECK(trick_menu_category_mask(6) == 0x200);
    CHECK(trick_menu_category_mask(7) == 0x100);
    CHECK(trick_menu_category_mask(8) == 0x800);
    CHECK(trick_menu_category_mask(9) == 0x4000);
    CHECK(trick_menu_category_mask(10) == 0x8000);
    CHECK(trick_menu_category_mask(5) == 0 && trick_menu_category_mask(11) == 0);

    const std::uint16_t up_triangle[] = {2, 1, 12};
    CHECK(trick_button_glyphs(up_triangle) == "GA");
    const std::uint16_t special[] = {3, 1, 4, 12};
    CHECK(trick_button_glyphs(special) == "GFA");
    const std::uint16_t all[] = {4, 9, 10, 11, 12};
    CHECK(trick_button_glyphs(all) == "BDCA");
    const std::uint16_t odd[] = {4, 0, 5, 13, 8};  // unknown codes print nothing but still count
    CHECK(trick_button_glyphs(odd) == "JL");
    const std::uint16_t none[] = {0};
    CHECK(trick_button_glyphs(none).empty());
    CHECK(trick_button_glyphs({}).empty());
    // every button code in order
    const std::uint16_t every[] = {12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(trick_button_glyphs(every) == "GHEFJIKLBDCA");
}

void sorting() {
    const auto db = sample_database();
    const std::uint16_t rows[] = {0x100, 0x101, 0x102, 0x103, 0x104};
    const auto order = trick_menu_sort_by_cost(db, rows);
    const std::vector<std::size_t> expected{3, 1, 4, 0, 2};  // 50, 100, 100, 500, 500 (stable)
    CHECK(order == expected);
    CHECK(trick_menu_sort_by_cost(db, {}).empty());
    const std::uint16_t missing[] = {0x100, 0x999};
    bool threw = false;
    try {
        (void)trick_menu_sort_by_cost(db, missing);
    } catch (const TrickDatabaseFault&) {
        threw = true;
    }
    CHECK(threw);
}

void ordinary_pages() {
    const auto db = sample_database();
    auto block = empty_block();
    CHECK(trick_menu_assigned_record(db, block, 0x4000, 3) == nullptr);
    CHECK(trick_menu_bind(db, block, 0x4000, 3, 5).outcome == TrickMenuBindResult::Outcome::Bound);
    CHECK(block[3] == 5);
    const auto* record = trick_menu_assigned_record(db, block, 0x4000, 3);
    CHECK(record != nullptr && record->source == 0x105);
    // another trick on the same combination simply replaces it
    trick_menu_bind(db, block, 0x4000, 3, 6);
    CHECK(block[3] == 6);
    trick_menu_unbind(db, block, 0x4000, 3);
    CHECK(block[3] == kBindingEmpty);
    CHECK(trick_menu_assigned_record(db, block, 0x4000, 3) == nullptr);
    // the same page logic serves the grind, manual and lip pages
    trick_menu_bind(db, block, 0x100, 10, 1);
    CHECK(block[10] == 1);
}

void special_page() {
    const auto db = sample_database();
    auto block = empty_block();
    const std::uint32_t mask = 0x8000;
    CHECK(trick_menu_bind(db, block, mask, 2, 7).outcome == TrickMenuBindResult::Outcome::Bound);
    CHECK(block[kBindingSpecialRecords] == 7 && block[kBindingSpecialCombos] == 2);
    CHECK(trick_menu_assigned_record(db, block, mask, 2) != nullptr);
    CHECK(trick_menu_assigned_record(db, block, mask, 3) == nullptr);

    // the same record on another combination is refused, naming the combination it already has
    const auto refused = trick_menu_bind(db, block, mask, 0, 7);
    CHECK(refused.outcome == TrickMenuBindResult::Outcome::AlreadyOnOtherCombo);
    CHECK(refused.other_combo == 2);
    CHECK(refused.other_glyphs == trick_button_glyphs(special_combos()[2]));
    CHECK(refused.other_glyphs == "GEA");  // {1, 3, 12}
    // binding it again on the same combination is fine
    CHECK(trick_menu_bind(db, block, mask, 2, 7).outcome == TrickMenuBindResult::Outcome::Bound);

    // fill the five slots; a sixth is refused with the "full" outcome
    for (int n = 0; n < 4; ++n) {
        CHECK(trick_menu_bind(db, block, mask, 3 + n, n).outcome == TrickMenuBindResult::Outcome::Bound);
    }
    CHECK(trick_menu_specials_full(block, false));
    CHECK(!trick_menu_specials_full(block, true));
    CHECK(trick_menu_bind(db, block, mask, 10, 4).outcome == TrickMenuBindResult::Outcome::SpecialSlotsFull);

    // retail unbinds with the marker 0xff: the slot's trick is emptied but the combination stays
    trick_menu_unbind(db, block, mask, 2);
    CHECK(block[kBindingSpecialRecords] == kBindingEmpty && block[kBindingSpecialCombos] == 2);
    CHECK(trick_menu_assigned_record(db, block, mask, 2) == nullptr);
    CHECK(!trick_menu_specials_full(block, false));
}

}  // namespace

int main() {
    categories_and_glyphs();
    sorting();
    ordinary_pages();
    special_page();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trick_menu_test: ok\n";
    return EXIT_SUCCESS;
}
