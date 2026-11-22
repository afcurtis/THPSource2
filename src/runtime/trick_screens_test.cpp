// Unit tests of the native trick-list screen (Phase 4.10e2) on hand-built memory. The retail forced-call replay (trick_screens_retail_calls_test.cpp) is the
// evidence the functions match the original; these tests pin behaviour a reader would expect: the button glyphs of a combination, the page ids and their class
// masks, which tricks a character may see, the panel's message timers, and the menu update refusing a state it does not know.
#include "create_color.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "menu_toolkit.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"
#include "trick_screens.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::frontend;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr std::uint32_t kHeap = 0x10000000;
constexpr std::uint32_t kCombination = kHeap + 0x100;
constexpr std::uint32_t kOut = kHeap + 0x200;
constexpr std::uint32_t kPanel = kHeap + 0x1000;
constexpr std::uint32_t kMenu = kHeap + 0x4000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    skater::SkaterManager skaters{mem, svc};
    create::CreateColor color{mem, svc};
    create::CreateSpool spool{mem, svc};
    SkaterWindowHelpers helpers{mem, svc, fonts, window, menu, color};
    CreateScreens create_screens{mem, svc, fonts, window, menu, skaters, spool, helpers};
    TrickScreens screens{mem, svc, fonts, window, menu, skaters, helpers, create_screens};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
    }
    std::vector<RetailServices::Call> calls_named(const std::string& name) const {
        std::vector<RetailServices::Call> out;
        for (const auto& call : svc.calls()) {
            if (call.name == name) {
                out.push_back(call);
            }
        }
        return out;
    }
};

void combination(World& w, std::initializer_list<std::uint16_t> words) {
    std::uint32_t at = kCombination;
    for (std::uint16_t word : words) {
        w.mem.set16(at, word);
        at += 2;
    }
}

void test_button_glyphs_are_the_symbol_fonts_letters() {
    World w;
    combination(w, {3, 1, 2, 12});
    w.screens.button_glyphs(kCombination, kOut);
    CHECK(w.mem.cstring(kOut) == "GHA");
    // the buttons 3..11 map to E F J I K L B D C, a code outside 1..12 prints nothing
    combination(w, {4, 3, 4, 99, 11});
    w.screens.button_glyphs(kCombination, kOut);
    CHECK(w.mem.cstring(kOut) == "EFC");
    combination(w, {0, 5});
    w.screens.button_glyphs(kCombination, kOut);
    CHECK(w.mem.cstring(kOut).empty());
}

void test_category_masks() {
    World w;
    CHECK(w.screens.category_mask(6) == 0x200);
    CHECK(w.screens.category_mask(7) == 0x100);
    CHECK(w.screens.category_mask(8) == 0x800);
    CHECK(w.screens.category_mask(9) == 0x4000);
    CHECK(w.screens.category_mask(10) == 0x8000);
    CHECK(w.calls_named("assert").empty());
    CHECK(w.screens.category_mask(11) == 0);
    CHECK(w.calls_named("assert").size() == 1);
}

void test_requirements_depend_on_the_character_rows() {
    World w;
    // every row available, none marked: any trick may be shown
    for (std::uint32_t c = 0; c < 20; ++c) {
        w.mem.set32(skater::kRows + c * skater::kRowStride + skater::kRowUnlocked, 1);
        w.mem.set32(skater::kRows + c * skater::kRowStride + skater::kRowFlags, 0);
    }
    CHECK(w.screens.requirements_met(3, 0x80));
    // a character marked 0x100 that is not unlocked hides the tricks that need it (flag 0x80)
    w.mem.set32(skater::kRows + 5 * skater::kRowStride + skater::kRowFlags, 0x100);
    w.mem.set32(skater::kRows + 5 * skater::kRowStride + skater::kRowUnlocked, 0);
    CHECK(!w.screens.requirements_met(3, 0x80));
    CHECK(w.screens.requirements_met(3, 0x40));
    w.mem.set32(skater::kRows + 5 * skater::kRowStride + skater::kRowUnlocked, 1);
    CHECK(w.screens.requirements_met(3, 0x80));
    // flag 0x10: not for the first character when it has flag 8
    w.mem.set32(skater::kRows + skater::kRowFlags, 8);
    CHECK(!w.screens.requirements_met(3, 0x10));
}

void test_panel_messages_count_down() {
    World w;
    w.mem.set32(kPanel, kVtTrickPanel);
    w.screens.panel_show_message(kPanel, 1, 0);
    CHECK(w.mem.u32(kPanel + 0xd0 + 4) == 2);
    CHECK(w.mem.u32(kPanel + 0xc4 + 4) == 0x14);
    CHECK(w.mem.u32(kPanel + 0xd8) == 0);
    w.screens.panel_show_message(kPanel, 2, 0);
    CHECK(w.mem.u32(kPanel + 0xd8) == 4);
    CHECK(w.calls_named("assert").empty());
    w.screens.panel_show_message(kPanel, 3, 0);
    CHECK(w.calls_named("assert").size() == 1);
}

void test_menu_update_refuses_an_unknown_state() {
    World w;
    w.mem.set32(kMenu + 0x2150, 9);
    CHECK(w.screens.menu_update(kMenu) == 0);
    CHECK(w.calls_named("assert").size() == 1);
}

}  // namespace

int main() {
    test_button_glyphs_are_the_symbol_fonts_letters();
    test_category_masks();
    test_requirements_depend_on_the_character_rows();
    test_panel_messages_count_down();
    test_menu_update_refuses_an_unknown_state();
    if (failures == 0) {
        std::puts("trick screens: all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
