// Unit tests of the native player-select screen (Phase 4.10f) on hand-built memory. The retail forced-call replay (player_select_retail_calls_test.cpp) is the
// evidence the functions match the original; these tests pin behaviour a reader would expect: the score counter counting, the flagged text element's slide,
// the skater view skipping the entries that cannot be chosen, the packet rewrite, the main menu's answers and the scroll limits of the bio window.
#include "player_select.hpp"

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
    PlayerSelect screen{mem, svc, fonts, window, menu, skaters};

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

void test_counter_counts_towards_its_target() {
    World w;
    w.mem.set32(kObject + 0xac, 100);   // target
    w.mem.set32(kObject + 0xb0, 0);     // shown
    w.mem.set32(kObject + 0xb4, kOther);
    w.mem.set32(kOther + 0x28, kHeap + 0x400);
    w.mem.set32(kObject + 0x14, 0);
    // the text element's string buffer is the slab's: SetString copies into a fresh block
    w.svc.script("alloc", static_cast<std::int32_t>(kHeap + 0x30000));
    w.screen.counter_update(kObject);
    CHECK(w.mem.u32(kObject + 0xb0) == 50);
    w.mem.set32(kObject + 0xb0, 99);
    w.svc.script("alloc", static_cast<std::int32_t>(kHeap + 0x30100));
    w.screen.counter_update(kObject);
    CHECK(w.mem.u32(kObject + 0xb0) == 100);   // within one of the target: lands on it
    w.screen.counter_set_value(kObject, 5000000);
    CHECK(w.mem.u32(kObject + 0xac) == 999999);
}

void test_flag_element_slides_with_its_shift() {
    World w;
    w.mem.set32(kObject + 0x18, 0);          // no delay
    w.mem.set32(kObject + 0x08, 100);        // target x
    w.mem.set32(kObject + 0x0c, 100);        // target y
    w.mem.set32(kObject + 0x20, 0);          // x
    w.mem.set32(kObject + 0x24, 100);        // y (already there)
    w.mem.set32(kObject + 0x30, 1);          // fast
    w.screen.flag_update(kObject);
    CHECK(w.mem.u32(kObject + 0x20) == 50);
    w.mem.set32(kObject + 0x30, 2);
    w.screen.flag_update(kObject);
    CHECK(w.mem.u32(kObject + 0x20) == 50 + (50 >> 2));
    // a delay holds the element still and counts down
    w.mem.set32(kObject + 0x18, 2);
    const std::uint32_t before = w.mem.u32(kObject + 0x20);
    w.screen.flag_update(kObject);
    CHECK(w.mem.u32(kObject + 0x20) == before);
    CHECK(w.mem.u32(kObject + 0x18) == 1);
}

void test_view_skips_entries_that_cannot_be_chosen() {
    World w;
    // every character but 3 and 7 is locked; the skater manager's slot 0 is character 5
    for (int c = 0; c < 20; ++c) {
        w.mem.set32(skater::row(c) + skater::kRowUnlocked, (c == 3 || c == 7) ? 1 : 0);
    }
    w.mem.set32(kObject + 0x890, 3);
    w.screen.view_step_selection(kObject, 1);
    CHECK(w.mem.s32(kObject + 0x890) == 7);
    w.screen.view_step_selection(kObject, 1);
    CHECK(w.mem.s32(kObject + 0x890) == 3);   // wraps past 0x14 to 0 and finds 3 again
    w.mem.set32(kObject + 0x890, 3);
    w.screen.view_step_selection(kObject, -1);
    CHECK(w.mem.s32(kObject + 0x890) == 7);   // (0x14 and 0x10 are skipped on the way round)
}

void test_menu_answers_set_the_exit_code() {
    World w;
    const std::uint32_t menu = kOther;
    w.mem.set32(kObject + 0x4b0, menu);
    w.mem.set32(0x0056a888, 0);
    const std::uint32_t expected[] = {0, 0, 0xc, 0xf, 0x10, 3};   // choices 1..5: 3 buys stats (0xf), 4 tricks (0x10), 5 the shop (3)
    (void)expected;
    w.mem.set32(menu + 0x234, 3);
    CHECK(w.screen.select_update_menu(kObject) == 1);
    CHECK(w.mem.u32(0x00561c78) == 0xf);
    w.mem.set32(menu + 0x234, 4);
    CHECK(w.screen.select_update_menu(kObject) == 1);
    CHECK(w.mem.u32(0x00561c78) == 0x10);
    w.mem.set32(menu + 0x234, 5);
    CHECK(w.screen.select_update_menu(kObject) == 1);
    CHECK(w.mem.u32(0x00561c78) == 3);
    // no choice and back pressed: the answer is 2
    w.mem.set32(menu + 0x234, 0);
    w.mem.set8(0x0056aff9, 1);
    CHECK(w.screen.select_update_menu(kObject) == 2);
    CHECK(w.mem.u8(0x0056aff9) == 0);
}

void test_bio_scrolls_within_limits() {
    World w;
    const std::uint32_t lines[2] = {kHeap + 0x100, kHeap + 0x200};
    w.mem.set32(kObject + 0xf0, 2);
    w.mem.set32(kObject + 0xb0, lines[0]);
    w.mem.set32(kObject + 0xb4, lines[1]);
    w.mem.set32(lines[0] + 0xc, 0x14);
    w.mem.set32(lines[1] + 0xc, 0x49);   // the last line is above 0x48: scrolling up moves everything
    w.screen.bio_scroll_up(kObject);
    CHECK(w.mem.s32(lines[0] + 0xc) == 0x13);
    CHECK(w.mem.s32(lines[1] + 0xc) == 0x48);
    w.screen.bio_scroll_up(kObject);       // now at the limit: no further
    CHECK(w.mem.s32(lines[0] + 0xc) == 0x13);
    w.mem.set32(lines[0] + 0xc, 0x14);
    w.screen.bio_scroll_down(kObject);     // the first line is already at the top: down does nothing
    CHECK(w.mem.s32(lines[0] + 0xc) == 0x14);
}

void test_prompt_width_adds_the_caption_and_four() {
    World w;
    const std::uint32_t sprite = kHeap + 0x300;
    const std::uint32_t text = kHeap + 0x400;
    w.mem.set32(kObject + 0x28, sprite);
    w.mem.set32(kObject + 0x2c, text);
    w.svc.script("element_width", 30);
    w.svc.script("element_width", 12);
    CHECK(w.screen.prompt_width(kObject) == 46);   // children of an unknown class are asked through the service
}

}  // namespace

int main() {
    test_counter_counts_towards_its_target();
    test_flag_element_slides_with_its_shift();
    test_view_skips_entries_that_cannot_be_chosen();
    test_menu_answers_set_the_exit_code();
    test_bio_scrolls_within_limits();
    test_prompt_width_adds_the_caption_and_four();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("player select tests passed");
    return 0;
}
