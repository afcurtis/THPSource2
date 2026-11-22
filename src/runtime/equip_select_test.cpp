// Unit tests of the native equipment-select screen (Phase 4.10f2) on hand-built memory. The retail forced-call replay (equip_select_retail_calls_test.cpp) is the
// evidence the functions match the original; these tests pin the behaviour a reader would expect: the price of a deck, buying it, the colour pairs, the slide of a
// rack, the rack's turn and wrap, the order of the spool queue and the back button ending the screen.
#include "equip_select.hpp"

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
constexpr std::uint32_t kObject = kHeap + 0x10000;
constexpr std::uint32_t kOther = kHeap + 0x20000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    skater::SkaterManager skaters{mem, svc};
    EquipSelect screen{mem, svc, fonts, window, menu, skaters};

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

void test_colour_pairs_mirror_each_other() {
    World w;
    CHECK(w.screen.colours_match(0, 0x12));
    CHECK(w.screen.colours_match(0x12, 0));
    CHECK(w.screen.colours_match(0x12, 0x21));
    CHECK(!w.screen.colours_match(0x12, 0x12));
}

void test_a_deck_costs_five_times_the_owned_decks_times_fifty() {
    World w;
    const std::uint32_t record = career::record(3);
    w.mem.set32(record + career::kRecClassMask, 0x7);         // three decks owned
    CHECK(w.screen.owns_deck(3, 0) && w.screen.owns_deck(3, 2) && !w.screen.owns_deck(3, 3));
    CHECK(w.screen.deck_price(3, 3) == 3 * 5 * 50);
    CHECK(w.screen.deck_price(3, 1) == 0);                    // a deck it owns is free
}

void test_buying_pays_and_owns() {
    World w;
    const std::uint32_t record = career::record(3);
    w.mem.set32(record + career::kRecClassMask, 0x1);
    w.mem.set32(record + career::kRecCash, 1000);
    w.screen.buy_deck(3, 4);
    CHECK(w.screen.owns_deck(3, 4));
    CHECK(w.mem.s32(record + career::kRecCash) == 1000 - 250);
}

void test_the_rack_slides_in_and_lands() {
    World w;
    w.screen.rack_init(kObject);
    CHECK(w.mem.u32(kObject + 8) == 3 && w.mem.u32(kObject + 0xc) == 600);
    w.screen.rack_set_active(kObject, 1);
    CHECK(w.mem.u32(kObject + 8) == 0);                       // a hidden rack that becomes active slides in
    for (int i = 0; i < 20 && w.mem.u32(kObject + 8) != 1; ++i) {
        w.screen.rack_step(kObject);
    }
    CHECK(w.mem.u32(kObject + 8) == 1 && w.mem.s32(kObject + 0xc) == 0);
}

void test_the_spool_queue_keeps_priority_order() {
    World w;
    w.screen.spool_construct(kObject, 2);
    const std::uint32_t items[3] = {kOther, kOther + 0x100, kOther + 0x200};
    const int priority[3] = {5, 1, 3};
    for (int i = 0; i < 3; ++i) {
        w.mem.set8(items[i], static_cast<std::uint8_t>(priority[i]));
        w.mem.set8(items[i] + 1, 0);
        w.svc.script(kAllocBuffer, static_cast<std::int32_t>(kHeap + 0x30000 + 0x10 * i));
        w.screen.spool_insert(kObject, items[i]);
    }
    const std::uint32_t first = w.mem.u32(kObject + 4);
    CHECK(w.mem.u32(first) == items[1]);                      // the nearest first
    CHECK(w.mem.u32(w.mem.u32(first + 4)) == items[2]);
}

void test_back_ends_the_screen_unless_a_dialog_is_open() {
    World w;
    w.mem.set32(0x00561c7c, 0);                                // player 0
    w.mem.set8(0x0056aff9, 1);                                 // its back edge
    CHECK(w.screen.window_step(kObject, kOther));
    CHECK(w.mem.s32(kOther) == -1);
    w.mem.set8(kObject + 0xc0, 0);
    w.mem.set32(kObject + 0xbc, 1);                            // a dialog is open: back is for the dialog
    w.mem.set8(0x0056aff9, 1);
    CHECK(!w.screen.window_step(kObject, kOther));
    CHECK(w.mem.s32(kOther) == 1);
}

}  // namespace

int main() {
    test_colour_pairs_mirror_each_other();
    test_a_deck_costs_five_times_the_owned_decks_times_fifty();
    test_buying_pays_and_owns();
    test_the_rack_slides_in_and_lands();
    test_the_spool_queue_keeps_priority_order();
    test_back_ends_the_screen_unless_a_dialog_is_open();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("equip select tests passed");
    return 0;
}
