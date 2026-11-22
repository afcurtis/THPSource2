// Unit tests of the native create-a-skater screens (Phase 4.10e1) on hand-built memory. The retail forced-call replay
// (create_screens_retail_calls_test.cpp) is the evidence the functions match the original; these tests pin behaviour a reader would expect: the text
// box laying a message out and drawing it centred, the confirm box's answers, the editor moving between its pages, and a screen loop ending.
#include "create_color.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "menu_toolkit.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"

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
constexpr std::uint32_t kFontAt = kHeap;
constexpr std::uint32_t kText = kHeap + 0x4000;
constexpr std::uint32_t kObject = kHeap + 0x10000;
constexpr std::uint32_t kEditor = kHeap + 0x20000;

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
    CreateScreens screens{mem, svc, fonts, window, menu, skaters, spool, helpers};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
        // a font of one glyph (every character maps to it) whose advance is six pixels, 10 pixels tall
        mem.set32(kFontAt + 0x30, 0x1000);
        mem.set32(kFontAt + 0x34, kHeap + 0x2000);
        mem.set32(kFontAt + 0x38, 1);
        for (std::uint32_t i = 0; i < 256; ++i) {
            mem.set8(kFontAt + 0x4b + i, 0);
        }
        mem.set8(kHeap + 0x2000 + 4, 6);
        mem.set8(kHeap + 0x2000 + 5, 10);
        mem.set8(kHeap + 0x2000 + 6, 0);
    }
    void text(std::uint32_t at, const std::string& value) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            mem.set8(at + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(value[i]));
        }
        mem.set8(at + static_cast<std::uint32_t>(value.size()), 0);
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

void test_text_box_lays_out_and_draws_centred() {
    World w;
    w.text(kText, "one two three");   // 13 characters: 78 pixels, it fits on one line
    w.screens.box_construct(kObject, kFontAt, kText, 100, 50);
    CHECK(w.mem.u32(kObject) == kVtTextBox);
    CHECK(w.mem.u32(kObject + 0x414) == 1);
    CHECK(w.mem.cstring(kObject + 0x14) == "one two three");
    CHECK(w.mem.u32(kObject + 4) == 0);
    w.svc.clear();
    w.screens.box_draw(kObject);
    const auto draws = w.calls_named(kFontDrawService);
    CHECK(draws.size() == 1);
    CHECK(draws.size() == 1 && draws[0].args[1] == 100 && draws[0].text == "one two three");
    // with the backdrop flag a dark rectangle is drawn around the widest line
    w.mem.set32(kObject + 4, 1);
    w.svc.clear();
    w.screens.box_draw(kObject);
    const auto boxes = w.calls_named(kSvcSelectionBox);
    CHECK(boxes.size() == 1 && boxes[0].args[0] == static_cast<std::uint32_t>(100 - 78 / 2 - 4) && boxes[0].args[2] == 78 + 8);
}

void test_text_box_wraps_long_messages_and_breaks_on_ampersand() {
    World w;
    std::string long_text;
    for (int i = 0; i < 40; ++i) {
        long_text += "word ";    // 200 characters: 1200 pixels, three lines of 500
    }
    long_text.pop_back();
    w.text(kText, long_text);
    w.screens.box_construct(kObject, kFontAt, kText, 0, 0);
    CHECK(w.mem.u32(kObject + 0x414) == 3);
    w.text(kText, "first&second");
    w.screens.box_construct(kObject, kFontAt, kText, 0, 0);
    CHECK(w.mem.u32(kObject + 0x414) == 2);
    CHECK(w.mem.cstring(kObject + 0x14) == "first" && w.mem.cstring(kObject + 0x14 + 0x80) == "second");
}

void test_confirm_poll_answers() {
    World w;
    constexpr std::uint32_t kList = kHeap + 0x30000;
    w.mem.set32(kObject + 0x418, kList);
    w.mem.set32(kList, 0x12345678);        // a list of an unknown class: its methods are reported
    w.mem.set32(kList + 0x194, 2);
    w.mem.set32(kList + 0x198, 1);
    constexpr std::uint32_t kItems[2] = {kHeap + 0x30100, kHeap + 0x30200};
    for (std::uint32_t i = 0; i < 2; ++i) {
        w.mem.set32(kList + 0x14 + 4 * i, kItems[i]);
        w.mem.set32(kItems[i] + 8, i);
    }
    // no edge: nothing
    CHECK(w.screens.confirm_poll(kObject, 0, -1) == 0);
    // the confirm edge on the highlighted second item (id 1): answers 1 and clears the edge
    w.mem.set8(kPadConfirmEdge, 1);
    w.mem.set8(kItems[1] + 0xe, 1);
    CHECK(w.screens.confirm_poll(kObject, 0, -1) != 0);
    CHECK(w.mem.u8(kPadConfirmEdge) == 0);
    // the back edge answers 2
    w.mem.set8(kPadBackEdge, 1);
    CHECK(w.screens.confirm_poll(kObject, 0, 0) == 2);
    CHECK(w.mem.u8(kPadBackEdge) == 0);
    // a down edge plays the move sound first
    w.svc.clear();
    (void)w.screens.confirm_poll(kObject, 2, 0);
    CHECK(!w.svc.calls().empty() && w.svc.calls().front().name == kSvcSfx && w.svc.calls().front().args[0] == 0x11f);
}

// An editor on page `page` whose poll answers `answer`: where does it go?
std::pair<std::uint32_t, std::uint32_t> editor_goes(std::uint32_t page, std::int32_t answer, std::uint32_t mode) {
    World w;
    w.mem.set32(kEditor + 0x36c, page);
    w.mem.set32(kEditor + 0x370, mode);
    constexpr std::uint32_t kFace = kHeap + 0x30000;
    w.mem.set32(kEditor + 0x3cc, kFace);
    std::uint32_t index = 0;
    for (std::uint32_t slot : {0x3c4u, 0x3d0u, 0x3d4u, 0x3d8u}) {
        w.mem.set32(kEditor + slot, kHeap + 0x40000 + 0x1000 * index++);
    }
    // the appearance page's list: one selectable item (a list of an unknown class and an item of an unknown class: their methods are reported)
    w.mem.set32(w.mem.u32(kEditor + 0x3d8) + 0xd0, kHeap + 0x50000);
    w.mem.set32(kHeap + 0x50000, 0x12345678);
    w.mem.set32(kHeap + 0x50000 + 0x194, 1);
    w.mem.set32(kHeap + 0x50000 + 0x14, kHeap + 0x50100);
    w.mem.set32(kHeap + 0x50100, 0x12345678);
    w.mem.set8(kHeap + 0x50100 + 0xc, 1);
    const std::uint32_t poll_address = page == 0 ? 0x004293b0 : page == 1 ? 0x0042a9b0 : page == 2 ? 0x004b6180 : 0x004c3d30;
    w.svc.script(engine_name(poll_address), answer);
    const std::uint32_t result = w.screens.editor_update(kEditor);
    return {result, w.mem.u32(kEditor + 0x36c)};
}

void test_editor_moves_between_pages() {
    // 1 goes on to the next page, 2 goes back, the last page finishes with 1 and the first leaves with 2
    CHECK(editor_goes(0, 1, 0) == std::make_pair(0u, 1u));
    CHECK(editor_goes(1, 1, 0) == std::make_pair(0u, 2u));
    CHECK(editor_goes(2, 2, 0) == std::make_pair(0u, 1u));
    CHECK(editor_goes(3, 1, 0) == std::make_pair(1u, 3u));
    CHECK(editor_goes(0, 2, 0) == std::make_pair(2u, 0u));
    // with the mode flag the poll's answer is returned as it is and the page stays
    CHECK(editor_goes(2, 1, 1) == std::make_pair(1u, 2u));
    CHECK(editor_goes(1, 0, 0) == std::make_pair(0u, 1u));
}

void test_stat_run_ends_when_the_step_answers() {
    World w;
    w.mem.set32(kPadCurrent, 0);
    w.screens.set_loop_seams(true);
    // the first frame: the clock moves on (a script value), the step answers 2 (back); every other service answers with the zero the script
    // runs out to (no window is built, the allocator answers nothing)
    w.svc.script(engine_name(0x004f7ce0), 5);
    w.svc.script(engine_name(0x004b8080), 2);
    const std::int32_t result = w.screens.stat_run();
    CHECK(result == -1);
    CHECK(w.mem.u32(kGlobalSimTick) == 5);
    CHECK(w.svc.calls().front().name == engine_name(0x00423bd0));
    CHECK(w.calls_named(kSvcFontLoad).size() == 5 && w.calls_named(kSvcFontUnload).size() == 5);
    // a second world: the step answers 0 once and then 1 (accept): the screen returns 0 after a second frame
    World v;
    v.screens.set_loop_seams(true);
    v.mem.set32(kGlobalLastTick, 0);
    v.svc.script(engine_name(0x004f7ce0), 9);
    v.svc.script(engine_name(0x004f7ce0), 9);
    v.svc.script(engine_name(0x004b8080), 0);
    v.svc.script(engine_name(0x004b8080), 1);
    CHECK(v.screens.stat_run() == 0);
    CHECK(v.calls_named(engine_name(0x004b8080)).size() == 2);
}

}  // namespace

int main() {
    test_text_box_lays_out_and_draws_centred();
    test_text_box_wraps_long_messages_and_breaks_on_ampersand();
    test_confirm_poll_answers();
    test_editor_moves_between_pages();
    test_stat_run_ends_when_the_step_answers();
    if (failures == 0) {
        std::puts("create screens: all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
