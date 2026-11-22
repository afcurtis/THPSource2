// Unit tests of the front end (Phase 4.10i) on a synthetic memory image. The retail forced calls (front_retail_calls_test.cpp) are the main evidence; these cover
// the paths a forced call cannot reach.
#include "front.hpp"

#include <iostream>

using namespace opentony::runtime;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
        ++failures;
    }
}

struct Fixture {
    RetailMemory mem;
    RetailServices svc;
    frontend::FontTools fonts;
    frontend::WindowBase window;
    frontend::MenuToolkit menu;
    skater::SkaterManager skaters;
    frontend::FrontScreens front;
    Fixture() : fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), front(mem, svc, fonts, window, menu, skaters) {
        mem.add_region(0x00100000, 0x1000);       // the menu
        mem.add_region(0x00561b00, 0x200);
        mem.add_region(0x005616d0, 0x20);
        mem.add_region(0x00560000, 0x100);
        mem.add_region(0x00527000, 0x100);
        mem.add_region(0x0056e300, 0x40);
    }
};

void test_menu_entries_and_selection() {
    Fixture f;
    const std::uint32_t menu = 0x00100000;
    const char* first = "abc";
    const char* second = "def";
    for (int i = 0; i < 4; ++i) {
        f.mem.set8(0x00527010 + i, static_cast<std::uint8_t>(first[i]));
        f.mem.set8(0x00527020 + i, static_cast<std::uint8_t>(second[i]));
    }
    f.front.cmenu_new(menu, 0x100, 0, 0, 0x14f, 0x100, 0x16);
    f.front.cmenu_add_entry(menu, 0x00527010);
    f.front.cmenu_add_entry(menu, 0x00527020);
    expect(f.mem.u8(menu + 9) == 2, "two entries");
    expect(f.front.cmenu_find_entry(menu, 0x00527020) == 1, "find the second entry");
    f.front.cmenu_set_line(menu, 1);
    expect(f.front.cmenu_choice_is(menu, 0x00527020), "the current line is the second entry");
    expect(!f.front.cmenu_choice_is(menu, 0x00527010), "and not the first");
    f.front.cmenu_entry_off(menu, 0x00527010);
    expect(!f.front.cmenu_is_entry_on(menu, 0x00527010), "the first entry is hidden");
}

void test_status_change_is_logged_once() {
    Fixture f;
    f.mem.set32(0x00561bfc, 3);
    f.front.set_status(1, 3);
    expect(f.svc.calls().empty(), "no log for the same status");
    f.front.set_status(1, 4);
    expect(f.svc.calls().size() == 1 && f.svc.calls()[0].name == "printf_log", "a change is logged");
    expect(f.mem.u32(0x00561bfc) == 4, "the status is set");
}

}  // namespace

int main() {
    test_menu_entries_and_selection();
    test_status_change_is_logged_once();
    if (failures == 0) {
        std::cout << "front tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
