// Unit tests of the goals, game-mode and competition screens (Phase 4.10h) on a synthetic memory image. The retail forced calls
// (goals_retail_calls_test.cpp) are the main evidence; these tests cover the paths a forced call cannot reach.
#include "goals.hpp"

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
    frontend::GoalScreens goals;
    Fixture() : fonts(mem, svc), window(mem, svc, fonts), menu(mem, svc, fonts, window), skaters(mem, svc), goals(mem, svc, fonts, window, menu, skaters) {
        mem.add_region(competition::kObject - 0x40, 0x200);
        mem.add_region(0x00560000, 0x100);
    }
};

void test_constructor_and_destructor() {
    Fixture f;
    f.mem.set32(competition::kObject + 0x108, 0x1234);
    const std::uint32_t result = f.goals.comp_construct(competition::kObject);
    expect(result == competition::kObject, "the constructor returns the object");
    expect(f.mem.u32(competition::kObject) == frontend::goals::kVtCompetition, "the constructor ends with the competition vtable");
    expect(f.mem.u32(competition::kObject + 0x108) == 0 && f.mem.u32(competition::kObject + 0x110) == 0, "no medal objects after construction");
    expect(f.svc.calls().size() == 2, "the two arrays are vector-constructed");
    f.goals.comp_destroy(competition::kObject);
    expect(f.mem.u32(competition::kObject) == frontend::kVtRoot, "the destructor ends with the root vtable");
}

void test_judge_deleting_destructor_sets_the_root_vtable() {
    Fixture f;
    f.mem.add_region(0x00561000, 0x20);
    const std::uint32_t judge = 0x00561000;
    expect(f.goals.comp_judge_delete(judge, 0) == judge, "the judge deleting destructor returns the judge");
    expect(f.mem.u32(judge) == frontend::kVtRoot, "and sets the root vtable");
    expect(f.svc.calls().empty(), "a flag of zero deletes nothing");
    f.goals.comp_judge_delete(judge, 1);
    expect(f.svc.calls().size() == 1 && f.svc.calls()[0].name == "operator_delete", "a flag of one deletes the block");
}

}  // namespace

int main() {
    test_constructor_and_destructor();
    test_judge_deleting_destructor_sets_the_root_vtable();
    if (failures == 0) {
        std::cout << "goals tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
