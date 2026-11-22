// Unit tests of the native options screen (Phase 4.10g) on hand-built memory. The retail forced-call replay (options_retail_calls_test.cpp) is the evidence the functions
// match the original; these tests pin the behaviour a reader would expect: what the main list's entries do, how the sound sliders and the controller buttons move, how the
// checklist's rows are sorted and which movies are unlocked.
#include "options.hpp"

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
constexpr std::uint32_t kWindow = kHeap + 0x10000;
constexpr std::uint32_t kList = kHeap + 0x20000;
constexpr std::uint32_t kItems = kHeap + 0x21000;
constexpr std::uint32_t kPage = kHeap + 0x40000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    skater::SkaterManager skaters{mem, svc};
    OptionsScreen options{mem, svc, fonts, window, menu, skaters};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
    }
    std::size_t count(const std::string& name) const {
        std::size_t n = 0;
        for (const auto& call : svc.calls()) {
            n += call.name == name ? 1 : 0;
        }
        return n;
    }
    // a list (a menu list object's child array) of rows with these ids; the row at `highlighted` is the lit one
    void make_list(std::uint32_t list, const std::vector<std::uint32_t>& ids, std::size_t highlighted, std::uint32_t first_item = kItems) {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint32_t row = first_item + 0x100 * static_cast<std::uint32_t>(i);
            mem.set32(row + 8, ids[i]);
            mem.set8(row + 0xe, i == highlighted ? 1 : 0);
            mem.set32(list + 0x14 + 4 * static_cast<std::uint32_t>(i), row);
        }
    }
};

void test_main_list_entries() {
    {
        World w;
        w.mem.set32(kWindow + 0x96c, 0x14);
        w.mem.set32(kWindow + 0x34c, kList);
        w.make_list(kList, {1, 2, 3}, 0);
        w.mem.set8(kPadConfirmEdge, 1);
        CHECK(w.options.menu_step(kWindow) == 1);                           // Player Controls leaves the screen, remembering the entry
        CHECK(w.mem.u8(0x005601cc) == 1 && w.mem.u8(kPadConfirmEdge) == 0);
    }
    {
        World w;
        w.mem.set32(kWindow + 0x96c, 0x14);
        w.mem.set32(kWindow + 0x34c, kList);
        w.make_list(kList, {1, 2, 3, 4}, 3);
        w.mem.set8(kPadConfirmEdge, 1);
        CHECK(w.options.menu_step(kWindow) == 0);                           // Movies only moves the state
        CHECK(w.mem.u32(kWindow + 0x970) == 0x17);
    }
    {
        World w;
        w.mem.set32(kWindow + 0x96c, 0x14);
        w.mem.set32(kWindow + 0x34c, kList);
        w.make_list(kList, {1, 2, 3}, 0);
        w.mem.set8(kPadBackEdge, 1);
        CHECK(w.options.menu_step(kWindow) == 2);                           // back on the pad leaves
        CHECK(w.mem.u8(kPadBackEdge) == 0 && w.count("sfx") == 1);
    }
    {
        World w;
        w.mem.set32(kWindow + 0x96c, 0x14);
        w.mem.set32(kWindow + 0x34c, kList);
        w.make_list(kList, {3}, 0);
        w.mem.set8(kPadConfirmEdge, 1);
        w.svc.script("operator_new", static_cast<std::int32_t>(kPage));
        w.svc.script("engine_00486b10", static_cast<std::int32_t>(kPage));
        CHECK(w.options.menu_step(kWindow) == 0);                           // Sound levels builds its page
        CHECK(w.mem.u32(kWindow + 0x970) == 0x1b && w.mem.u32(kWindow + 0x988) == kPage);
        CHECK(w.count("engine_00486b10") == 1);
    }
}

void test_gap_rows_are_sorted() {
    World w;
    const std::vector<std::uint32_t> ids = {9, 4, 7, 4, 1};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const std::uint32_t row = kItems + 0x100 * static_cast<std::uint32_t>(i);
        w.mem.set32(row + 8, ids[i]);
        w.mem.set32(kWindow + 0x34c + 4 * static_cast<std::uint32_t>(i), row);
    }
    w.options.gaps_sort(kWindow, 0, 4);
    std::uint32_t last = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const std::uint32_t id = w.mem.u32(w.mem.u32(kWindow + 0x34c + 4 * static_cast<std::uint32_t>(i)) + 8);
        CHECK(id >= last);
        last = id;
    }
    CHECK(last == 9);
    CHECK(w.count("assert") == 0);
}

void test_strip_brackets() {
    World w;
    const std::string text = "[a] b[c]";
    for (std::size_t i = 0; i <= text.size(); ++i) {
        w.mem.set8(kPage + static_cast<std::uint32_t>(i), i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
    }
    w.options.gaps_strip_brackets(kPage);
    CHECK(w.mem.cstring(kPage) == "a bc");
}

void test_movie_kinds_without_the_career() {
    World w;
    CHECK(w.options.movie_unlocked(0, -1));                                 // kind 0 is always unlocked, kind 3 never
    CHECK(!w.options.movie_unlocked(3, -5));
    CHECK(w.svc.calls().empty());
    CHECK(!w.options.movie_unlocked(9, 0));                                 // an unknown kind is reported
    CHECK(w.count("assert") == 1);
}

void test_controller_buttons_keep_distinct_values() {
    World w;
    const std::uint32_t rows[4] = {kItems, kItems + 0x100, kItems + 0x200, kItems + 0x300};
    const std::uint32_t ids[4] = {0, 2, 1, 3};
    const std::uint32_t values[4] = {1, 1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        w.mem.set32(rows[i] + 8, ids[i]);
        w.mem.set32(rows[i] + 0x10, values[i]);
        w.mem.set32(kWindow + 0xc0 + 4 * i, rows[i]);
    }
    w.mem.set32(kWindow + 0xb0, 0);                                         // the button that was just changed
    w.options.controls_fix(kWindow);
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            CHECK(w.mem.u32(rows[i] + 0x10) != w.mem.u32(rows[j] + 0x10));
        }
    }
    CHECK(w.mem.u32(rows[0] + 0x10) == 1);                                  // (the changed one stays)
    w.mem.set32(rows[0] + 0x10, 5);
    w.options.controls_clamp(rows[0], 0, 3);
    CHECK(w.mem.u32(rows[0] + 0x10) == 0);                                  // past the top wraps to the bottom
    w.mem.set32(rows[0] + 0x10, 0xffffffffu);
    w.options.controls_clamp(rows[0], 0, 3);
    CHECK(w.mem.u32(rows[0] + 0x10) == 3);
}

void test_cheats_are_stored_and_shown() {
    World w;
    w.mem.set32(kPage + 0xac, 0);
    const std::uint32_t rows[3] = {kItems, kItems + 0x100, kItems + 0x200};
    const std::uint32_t ids[3] = {3, 8, 0xffffffffu};
    const std::uint32_t values[3] = {1, 120, 0};
    for (int i = 0; i < 3; ++i) {
        w.mem.set32(rows[i] + 8, ids[i]);
        w.mem.set32(rows[i] + 0x10, values[i]);
        w.mem.set32(kPage + 0xb0 + 4 * i, rows[i]);
    }
    w.options.cheats_store(kPage);                                          // the switch is stored as on, the weight as its byte; Back is left alone
    CHECK(w.count("engine_00416a00") == 2);
    w.svc.script("engine_00416980", 0);
    w.svc.script("engine_00416980", 77);
    w.options.cheats_refresh(kPage);                                        // a switch that is off shows 2; the weight shows its value
    CHECK(w.mem.u32(rows[0] + 0x10) == 2 && w.mem.u32(rows[1] + 0x10) == 77);
}

}  // namespace

int main() {
    test_main_list_entries();
    test_gap_rows_are_sorted();
    test_strip_brackets();
    test_movie_kinds_without_the_career();
    test_controller_buttons_keep_distinct_values();
    test_cheats_are_stored_and_shown();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("options unit tests passed");
    return 0;
}
