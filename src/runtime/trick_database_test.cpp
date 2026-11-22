// Hand-built checks of the native trick database and key-table builder (Phase 4.7b). The retail
// replays live in trick_database_retail_trace_test.cpp; these pin the arithmetic and the quirks
// on a small synthetic TRICKS.BIN whose every expected value is worked out by hand.
#include "trick_database.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

void put16(std::vector<std::uint8_t>& image, std::size_t at, std::uint16_t value) {
    image[at] = static_cast<std::uint8_t>(value);
    image[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_bytes(std::vector<std::uint8_t>& image, std::size_t at, std::initializer_list<int> bytes) {
    for (int b : bytes) {
        image[at++] = static_cast<std::uint8_t>(b);
    }
}

std::size_t put_string(std::vector<std::uint8_t>& image, std::size_t at, const std::string& text) {
    for (char c : text) {
        image[at++] = static_cast<std::uint8_t>(c);
    }
    image[at++] = 0;
    return at;
}

// Section 0 (0x20): character 0's key stream at 0x100. Section 5 (0x40): three descriptors.
//   A: grind (type 9) "{Grind One}", cost 5, source 0x80
//   B: air (type 1) "{Ollie}" through a 'P' token, no cost, source 0xa0
//   C: lip (type 12) 'X' 0x0040 then "{fs Kick}", special (descriptor 0x8000), source 0xc0
std::vector<std::uint8_t> synthetic_image() {
    std::vector<std::uint8_t> image(0x200, 0);
    put16(image, 0, 0x20);
    put16(image, 10, 0x40);
    put16(image, 0x20, 0x100);
    std::size_t d = 0x40;
    for (std::uint16_t w : {2, 1, 9, 0x80, 0x0000, 2, 1, 1, 0xa0, 0x0000, 1, 12, 0xc0, 0x8000, 0}) {
        put16(image, d, w);
        d += 2;
    }
    std::size_t p = 0x80;
    image[p++] = 0x0b;
    p = put_string(image, p, "{Grind One}");
    put_bytes(image, p, {0x51, 5, 0, 0x07});
    p = 0xa0;
    image[p++] = 0x50;
    p = put_string(image, p, "{Ollie}");
    image[p] = 0x07;
    p = 0xc0;
    put_bytes(image, p, {0x58, 0x40, 0});
    p += 3;
    image[p++] = 0x0b;
    p = put_string(image, p, "{fs Kick}");
    image[p] = 0x07;
    d = 0x100;
    for (std::uint16_t w : {2, 1, 12, 0x80, 0x0000,      // ordinary combination {1,12}
                            1, 5, 0xa0, 0x0000,          // generic single button
                            3, 1, 4, 12, 0xc0, 0x8000,   // special combination
                            0}) {
        put16(image, d, w);
        d += 2;
    }
    return image;
}

void clean_name_rules() {
    std::string s = "{Handplant}";
    trick_clean_name(s, false);
    CHECK(s == "Handplant");  // both braces go
    s = "{fs air}";
    trick_clean_name(s, true);
    CHECK(s == "air");
    s = "{fs air}";
    trick_clean_name(s, false);
    CHECK(s == "fs air");
    s = "bs Kick}";
    trick_clean_name(s, true);
    CHECK(s == "Kick");
    s = "a";
    trick_clean_name(s, false);
    CHECK(s == "a");
    s = "";
    trick_clean_name(s, true);
    CHECK(s.empty());
    s = "xfs a}";  // the prefix only counts at offset 0 or 1
    trick_clean_name(s, true);
    CHECK(s == "xa");
}

void command_widths() {
    std::vector<std::uint8_t> image(64, 0);
    put_bytes(image, 0, {0x01, 0, 0});
    CHECK(trick_command_size(image, 0) == 3);
    put_bytes(image, 8, {0x0f, 0, 0, 0, 0, 0, 0});
    CHECK(trick_command_size(image, 8) == 15);
    put_bytes(image, 16, {0x17, 0, 0, 0, 0});
    CHECK(trick_command_size(image, 16) == 21);
    put_bytes(image, 24, {0x0b, 'a', 'b', 0});
    CHECK(trick_command_size(image, 24) == 28);
    put_bytes(image, 32, {0x4f, 0, 0, 0, 0, 0, 0, 0, 0, 'x', 'y', 0});
    CHECK(trick_command_size(image, 32) == 44);
    put_bytes(image, 48, {0x39, 2, 0});
    CHECK(trick_command_size(image, 48) == 48 + 3 + 4);
    put_bytes(image, 56, {0x52, 0});
    CHECK(trick_command_size(image, 56) == 58);
    std::vector<std::string> notes;
    put_bytes(image, 60, {0x5a});
    CHECK(trick_command_size(image, 60, &notes) == 61);
    CHECK(notes.size() == 1);
}

void database_parse() {
    auto image = synthetic_image();
    const auto pristine = image;
    const auto db = TrickDatabase::build(image);
    CHECK(db.count() == 3);
    CHECK(db.record(0) == (TrickRecord{"grind one", 0x80, 5, 0x100, 0x0000}));
    // 'P' names are lowercased in place, so the retained name is lower case too
    CHECK(db.record(1).name == "ollie");
    CHECK(db.record(1).cost == kTrickDefaultCost);
    CHECK(db.record(1).flags == 0x2000);
    // lip class: flags 0x800 | X 0x40 | descriptor 0x8000 merged in; "fs " prefix removed
    CHECK(db.record(2) == (TrickRecord{"kick", 0xc0, kTrickDefaultCost, 0x8840, 0x8000}));
    // the lowercase pass wrote into the image
    CHECK(image != pristine);
    CHECK(image[0x82] == 'g');  // "{Grind One}" became "{grind one}"
    CHECK(db.find_by_source(0xa0) == &db.records()[1]);
    CHECK(db.find_by_source(0x91) == nullptr);
    CHECK(db.index_by_source(0xc0) == 2);
    CHECK(db.index_by_source(0x55) == -1);
    bool threw = false;
    try {
        (void)db.record(3);
    } catch (const TrickDatabaseFault&) {
        threw = true;
    }
    CHECK(threw);
}

void merge_rules() {
    auto image = synthetic_image();
    auto db = TrickDatabase::build(image);
    // a second listing of source 0xa0 as special sets 0x8000 in both words and notes it
    db.add_or_merge("{Ollie}", 0x2000, 0x7b, 0xa0, 0x8000);
    CHECK(db.count() == 3);
    CHECK(db.record(1).flags == 0xa000);
    CHECK(db.record(1).descriptor == 0x8000);
    // descriptor bit 0x4000 clears class 0x800 and sets 0x4000
    db.add_or_merge("x", 0x800, 1, 0xb0, 0x4800);
    CHECK(db.record(3).flags == 0x4000);
    // the capacity is 0x80 records
    for (std::uint16_t s = 0x1000; db.count() < kTrickDatabaseCapacity; ++s) {
        db.add_or_merge("t", 0, 0, s, 0);
    }
    bool threw = false;
    try {
        db.add_or_merge("overflow", 0, 0, 0x2000, 0);
    } catch (const TrickDatabaseFault&) {
        threw = true;
    }
    CHECK(threw);
}

void combination_lookup() {
    auto image = synthetic_image();
    const auto db = TrickDatabase::build(image);
    const std::uint16_t up_triangle[] = {1, 12};
    CHECK(db.find_button_combination(up_triangle) == 0);
    const std::uint16_t right_triangle[] = {4, 12};
    CHECK(db.find_button_combination(right_triangle) == 1);
    const std::uint16_t single[] = {5};
    CHECK(db.find_button_combination(single) == -1);
    const std::uint16_t special[] = {1, 4, 12};
    CHECK(db.find_special_combination(special) == 0);
    CHECK(db.find_button_combination(special) == -1);
    CHECK(builtin_combos().size() == 30);
    CHECK(special_combos().size() == 36);
}

void binding_assignment() {
    auto image = synthetic_image();
    const auto db = TrickDatabase::build(image);
    std::array<std::uint8_t, kBindingBlockSize> block{};
    block.fill(kBindingEmpty);
    trick_binding_assign_button(block, db, 3, 7);
    CHECK(block[3] == 7);
    CHECK(trick_binding_assign_special(block, db, 4, 9));
    CHECK(block[kBindingSpecialRecords] == 9 && block[kBindingSpecialCombos] == 4);
    // the same record cannot take a second special slot
    CHECK(!trick_binding_assign_special(block, db, 5, 9));
    // a new record on the same combination replaces the old assignment
    CHECK(trick_binding_assign_special(block, db, 4, 11));
    CHECK(block[kBindingSpecialRecords] == 11 && block[kBindingSpecialCombos] == 4);
    // five slots, then full
    CHECK(trick_binding_assign_special(block, db, 6, 12));
    CHECK(trick_binding_assign_special(block, db, 7, 13));
    CHECK(trick_binding_assign_special(block, db, 8, 14));
    CHECK(trick_binding_assign_special(block, db, 9, 15));
    CHECK(!trick_binding_assign_special(block, db, 10, 16));
    // record -1 clears a combination
    CHECK(trick_binding_assign_special(block, db, 9, -1));
}

void character_and_table() {
    auto image = synthetic_image();
    const auto db = TrickDatabase::build(image);
    RetailImage config(kTrickConfigSize);
    config.set32(0x00, 0xdeadbeef);  // cleared by initialisation
    const std::array<std::uint8_t, 10> costs{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    trick_config_initialize_character(db, image, config, 0, costs, true);
    CHECK(config.u32(0x00) == 0);
    CHECK(config.u32(kConfigClassMask) == 0x3ff);
    for (std::size_t i = 0; i < 10; ++i) {
        CHECK(config.u8(kConfigCosts + i) == i + 1);
    }
    CHECK(config.u32(kConfigUnlocks) == 0x7);  // all three named tricks unlocked
    CHECK(config.u32(kConfigUnlocks + 4) == 0);
    const auto* block = config.bytes().data() + kConfigBindings;
    CHECK(block[0] == 0);  // combination {1,12} -> record 0
    CHECK(block[1] == kBindingEmpty);
    CHECK(block[kBindingSpecialRecords] == 2 && block[kBindingSpecialCombos] == 0);
    CHECK(block[kBindingSpecialRecords + 1] == kBindingEmpty);

    // the generic single-button trick keeps its own entry; ordinary combinations come from the
    // binding block; the special combination sits between the 0x800 and 0x2000 passes
    const auto stream = trick_character_key_stream(image, 0);
    CHECK(stream == 0x100);
    const auto table = trick_table_build_for_player(
        db, image, stream, std::span(block, kBindingBlockSize));
    const std::vector<std::uint16_t> expected{3, 1, 4, 12, 0xc0, 0x8000,
                                              1, 5, 0xa0, 0x0000,
                                              2, 1, 12, 0x80, 0x0000,
                                              0};
    CHECK(table == expected);

    const auto combos = trick_table_return_combos(db, image, stream);
    CHECK(combos.size() == 3);
    CHECK(combos[0][0] == 2 && combos[0][4] == 0x100 && combos[0][5] == 0x80);
    CHECK(combos[1][0] == 1 && combos[1][1] == 5 && combos[1][4] == 0x2000);
    CHECK(combos[2][4] == 0x8840);

    // an unlock costs points: none left, nothing is set
    RetailImage poor(kTrickConfigSize);
    poor.set32(kConfigPoints, 3);
    CHECK(!trick_config_consume_points_and_set_bit(poor, db, 5, 4));
    CHECK(poor.u32(kConfigUnlocks) == 0);
    CHECK(trick_config_consume_points_and_set_bit(poor, db, 5, 3));
    CHECK(poor.s32(kConfigPoints) == 0);
    CHECK(poor.u32(kConfigUnlocks) == (1u << 5));
    CHECK(trick_config_consume_points_and_set_bit(poor, db, 37, 0));
    CHECK(poor.u32(kConfigUnlocks + 4) == (1u << 5));
}

}  // namespace

int main() {
    clean_name_rules();
    command_widths();
    database_parse();
    merge_rules();
    combination_lookup();
    binding_assignment();
    character_and_table();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trick_database_test: ok\n";
    return EXIT_SUCCESS;
}
