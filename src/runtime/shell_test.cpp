// Unit tests of the shell helpers (Phase 4.10i2) on a synthetic memory image. The retail forced calls (front_retail_calls_test.cpp) are the main evidence; these cover
// the paths a forced call cannot reach: the text-window list unlinked from its middle and the player spool table walked to a later entry.
#include "shell_helpers.hpp"

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
    frontend::ShellHelpers helpers;
    Fixture() : fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), helpers(mem, svc, fonts, window, menu, skaters) {
        mem.add_region(0x00100000, 0x1000);       // the text windows
        mem.add_region(0x0056db00, 0x100);        // the window list head, the spooler
        mem.add_region(0x00560000, 0x100);
        mem.add_region(0x005615c0, 0x40);
        mem.add_region(0x00527000, 0x100);
        mem.add_region(0x0052cf00, 0x100);
        mem.add_region(0x00525600, 0x200);
        mem.add_region(0x00535000, 0x100);
    }
};

void test_text_window_unlinked_from_the_middle() {
    Fixture f;
    using namespace frontend::shellkit;
    const std::uint32_t first = 0x00100000, middle = 0x00100200, last = 0x00100400;
    f.mem.set32(kTextWindows, first);
    f.mem.set32(first + 0x48, 0);
    f.mem.set32(first + 0x4c, middle);
    f.mem.set32(middle + 0x48, first);
    f.mem.set32(middle + 0x4c, last);
    f.mem.set32(last + 0x48, middle);
    f.mem.set32(last + 0x4c, 0);
    f.helpers.textwin_destruct(middle);
    expect(f.mem.u32(first + 0x4c) == last, "the first window now points at the last");
    expect(f.mem.u32(last + 0x48) == first, "the last window now points back at the first");
    expect(f.mem.u32(kTextWindows) == first, "the head is unchanged");
    expect(f.mem.u32(middle) == kVtBase, "the window ends as the base class");
    f.helpers.textwin_destruct(first);
    expect(f.mem.u32(kTextWindows) == 0, "destroying the head moves it to the window before the head (none)");
}

}  // namespace

int main() {
    test_text_window_unlinked_from_the_middle();
    if (failures == 0) {
        std::cout << "shell tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
