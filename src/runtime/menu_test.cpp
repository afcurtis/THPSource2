// Unit tests of the native menu toolkit, title screen and credits roll (Phase 4.10c), on hand-built memory. The retail forced-call
// replay (menu_retail_calls_test.cpp) is the evidence the functions match the original; these tests drive what the replay cannot reach (the
// screens' loops, which wait on the player and the clock) and pin behaviour a reader would expect.
#include "credits.hpp"
#include "mainmenu.hpp"
#include "menu_toolkit.hpp"

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
constexpr std::uint32_t kFontAt = kHeap;            // a valid font for Font_Find / Font_Load
constexpr std::uint32_t kStrings = kHeap + 0x1000;
constexpr std::uint32_t kObjects = kHeap + 0x10000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    MainMenu main{mem, svc, fonts, window, menu};
    Credits credits{mem, svc, fonts, window, menu};
    std::uint32_t next = kObjects;

    World() {
        mem.add_region(0x00400000, 0x002c0000);       // the executable's data and globals (zero)
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
        mem.set32(0x00527d54, 0x1e0);
        // a font whose glyph table is empty: every width is zero
        mem.set32(kFontAt + 0x30, 0x1000);
        mem.set32(kFontAt + 0x34, kHeap + 0x2000);
        mem.set32(kFontAt + 0x38, 0);
        for (std::uint32_t i = 0; i < 256; ++i) {
            mem.set8(kFontAt + 0x4b + i, 0xff);
        }
        // strings the screens read
        put(0x00525458, "s2font1.fnt");
        put(0x0052544c, "s2font2.fnt");
        put(0x005287a4, "ingame1.fnt");
        put(0x00526a84, "credits.txt");
        put(0x00526a78, "music.txt");
        put(0x00526b78, "?");
        put(0x00534dd4, "EMPTY");
        put(0x00525768, "X");
        for (int i = 0; i < 64; ++i) {
            svc.script(kOperatorNew, static_cast<std::int32_t>(next));
            svc.script(kAllocBuffer, static_cast<std::int32_t>(next + 0x400));
            svc.script("crt_new", static_cast<std::int32_t>(next + 0x800));
            next += 0x1000;
        }
        for (int i = 0; i < 16; ++i) {
            svc.script("sprite_get_palette", static_cast<std::int32_t>(kHeap + 0x90000));
            svc.script("sprite_construct_b", static_cast<std::int32_t>(kHeap + 0x80000 + 0x100 * i));
            svc.script(kSpriteConstruct, static_cast<std::int32_t>(kHeap + 0x88000 + 0x100 * i));
            svc.script(kSvcFontFind, static_cast<std::int32_t>(kFontAt));
            svc.script("font_load", static_cast<std::int32_t>(kFontAt));
        }
    }
    void put(std::uint32_t at, const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        mem.write(at, bytes);
    }
    [[nodiscard]] bool called(const std::string& name) const {
        for (const auto& c : svc.calls()) {
            if (c.name == name) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::size_t count(const std::string& name) const {
        std::size_t n = 0;
        for (const auto& c : svc.calls()) {
            n += c.name == name ? 1 : 0;
        }
        return n;
    }
};

void test_wrapper_builds_a_list_through_virtual_dispatch() {
    World w;
    const std::uint32_t texts = kStrings;
    const std::uint32_t ids = kStrings + 0x100;
    w.put(kStrings + 0x200, "ONE");
    w.put(kStrings + 0x210, "TWO");
    w.put(kStrings + 0x220, "THREE");
    const std::uint32_t t[3] = {kStrings + 0x200, kStrings + 0x210, kStrings + 0x220};
    const std::int32_t i[3] = {-2, 7, 9};
    for (int k = 0; k < 3; ++k) {
        w.mem.set32(texts + 4 * k, t[k]);
        w.mem.set32(ids + 4 * k, static_cast<std::uint32_t>(i[k]));
    }
    const std::uint32_t self = kHeap + 0x40000;
    w.menu.wrapper_construct(self, texts, ids, 3, 0x005287a4, 0);
    const std::uint32_t list = w.mem.u32(self + 4);
    CHECK(w.mem.u32(list) == kMenuVtList);                    // the list the wrapper built is a real MenuList
    CHECK(w.mem.u32(list + 0x194) == 3);                      // its three items were added through the list's own add method
    // the first selectable item (id 7) is highlighted, and a scaled item (id -2) is not selectable
    CHECK(w.menu.list_highlighted_id(list) == 7);
    CHECK(w.mem.u8(w.mem.u32(self + 0x18c) + 0x0c) == 0);
    CHECK(w.mem.u32(self + 0x160) == 0xdac);                  // the ingame1 font's scale
}

void test_list_navigation_wraps_over_unselectable_items() {
    World w;
    const std::uint32_t list = kHeap + 0x40000;
    w.menu.list_construct(list, 0, 0, 100, 100, 0, 0, 2, 0);
    const std::uint32_t item_at = kHeap + 0x50000;
    for (int k = 0; k < 4; ++k) {
        const std::uint32_t item = item_at + 0x100 * k;
        w.mem.set32(item, kMenuVtControl);
        w.mem.set32(item + 8, 10 + k);
        w.mem.set8(item + 0x0c, k == 1 ? 0 : 1);   // item 1 cannot be selected
        w.menu.list_add(list, item);
    }
    w.mem.set32(list + 0x198, 0);
    w.menu.list_navigate(list, 3);                     // down
    CHECK(w.mem.u32(list + 0x198) == 2);               // skipped the unselectable item
    w.menu.list_navigate(list, 3);
    w.menu.list_navigate(list, 3);
    CHECK(w.mem.u32(list + 0x198) == 0);               // wrapped
    w.menu.list_navigate(list, 2);                     // up wraps to the last
    CHECK(w.mem.u32(list + 0x198) == 3);
}

void test_title_screen_quits_on_the_quit_key() {
    World w;
    w.svc.script(engine_name(0x004e41b0), 1);          // the quit key is down on the first frame
    const std::uint32_t result = w.main.run();
    CHECK(result == 0xff);
    CHECK(w.called("sfx"));
    CHECK(w.mem.u32(0x0056114c) == 0);                 // the saved flag is restored
    CHECK(w.count("engine_004f7ce0") == 1);            // one frame tick, then out
}

void test_title_poll_picks_the_wheels_entry() {
    World w;
    const std::uint32_t title = kHeap + 0x40000;
    const std::uint32_t wheel = kHeap + 0x41000;
    w.mem.set32(title + 0x208, wheel);
    w.mem.set32(wheel + 0xcc, 2);                      // the wheel stands on entry two
    w.mem.set32(0x00534504 + 12 * 2 + 4, 5);           // pad 0 picks 5 there
    w.mem.set8(0x0056b029, 1);                         // pad 0 confirms
    const std::uint32_t out = kHeap + 0x42000;
    CHECK(w.main.window_poll(title, out) == 1);
    CHECK(w.mem.u32(out) == 5);
    CHECK(w.mem.u8(0x0056b029) == 0);                  // the edge is consumed
}

void test_credits_roll_runs_one_frame_and_unloads() {
    World w;
    w.svc.script(engine_name(0x00449030), 0x400);
    w.put(kHeap + 0x400, "");                          // (the file buffer is the first allocation)
    w.mem.set32(kHeap + 0x50000, 0);                   // mode 0: credits.txt
    w.put(kObjects + 0x400, "Producer\r\n@M1,2\r\n#");
    w.svc.script(engine_name(0x004f5ff0), 1);          // the input poll rewrites the six edge bytes: the first (pad 0 confirm) is down, ending the roll at once
    for (int k = 0; k < 5; ++k) {
        w.svc.script(engine_name(0x004f5ff0), 0);
    }
    w.credits.run(kHeap + 0x50000);
    CHECK(w.called("engine_004abc80"));                // the music is stopped on the way out
    CHECK(w.count("font_unload") == 2);
}

void test_credits_parse_tag_recognises_the_tags() {
    World w;
    const std::uint32_t self = kHeap + 0x60000;
    w.put(kStrings, "@B x");
    CHECK(w.credits.parse_tag(self, kStrings) == 3);
    CHECK(w.mem.u32(self + 0xfe4) == 1);
    w.put(kStrings, "@F  y");
    CHECK(w.credits.parse_tag(self, kStrings) == 4);
    CHECK(w.mem.u32(self + 0xfe8) == kCreditsFontB);
    w.put(kStrings, "@M3,4");
    CHECK(w.credits.parse_tag(self, kStrings) == 5);
    CHECK(w.mem.u32(self + 0xfec) == 3 && w.mem.u32(self + 0xff0) == 4);
    w.put(kStrings, "plain");
    CHECK(w.credits.parse_tag(self, kStrings) == 0);
}

void test_card_screen_loop_returns_the_result() {
    World w;
    const std::uint32_t card_memory = kObjects;        // the first operator new
    (void)card_memory;
    // the card window reports "finished with result 1" as soon as it exists: the scripted text box and fonts are enough
    w.svc.script(kSvcTextboxConstruct, static_cast<std::int32_t>(kHeap + 0x70000));
    // run: the window is built by card_construct, its result word (+0xd54) is zero, so the loop would go on: make the services answer "quit"
    w.svc.script(engine_name(0x00452d30), 1);
    const std::uint32_t result = w.main.card_run();
    CHECK(result == 0u);                               // not "2": the player quit instead of finishing (the answer is -1 only for 2)
}

}  // namespace

int main() {
    test_wrapper_builds_a_list_through_virtual_dispatch();
    test_list_navigation_wraps_over_unselectable_items();
    test_title_screen_quits_on_the_quit_key();
    test_title_poll_picks_the_wheels_entry();
    test_credits_roll_runs_one_frame_and_unloads();
    test_credits_parse_tag_recognises_the_tags();
    test_card_screen_loop_returns_the_result();
    if (failures == 0) {
        std::puts("menu tests passed");
    }
    return failures == 0 ? 0 : 1;
}
