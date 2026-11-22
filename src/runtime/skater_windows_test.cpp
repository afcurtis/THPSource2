// Unit tests of the native skater-window helpers (Phase 4.10d) on hand-built memory. The retail forced-call replay
// (skater_windows_retail_calls_test.cpp) is the evidence the functions match the original; these tests pin behaviour a reader would expect
// and the small pieces added next to the replay (the string-valued font calls and the toolkit's extra dispatcher).
#include "career.hpp"
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_textures.hpp"
#include "menu_toolkit.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"
#include "skater_windows.hpp"

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
constexpr std::uint32_t kObject = kHeap + 0x10000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    skater::SkaterManager skaters{mem, svc};
    create::CreateColor color{mem, svc};
    create::TextureSets textures{mem, svc};
    create::CreatePalette palette{mem, svc, color, textures};
    career::Career career{mem, svc, skaters};
    SkaterWindowHelpers helpers{mem, svc, fonts, window, menu, color};
    SkaterWindows windows{mem, svc, fonts, window, menu, helpers, skaters, textures, palette, career};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
        // a font of one glyph (every character maps to it) whose advance is six pixels
        mem.set32(kFontAt + 0x30, 0x1000);
        mem.set32(kFontAt + 0x34, kHeap + 0x2000);
        mem.set32(kFontAt + 0x38, 1);
        for (std::uint32_t i = 0; i < 256; ++i) {
            mem.set8(kFontAt + 0x4b + i, 0);
        }
        mem.set8(kHeap + 0x2000 + 4, 6);
    }
};

void test_chunk_walk() {
    World w;
    // two chunks, then the RGBs chunk, then the end tag
    std::uint32_t at = kObject;
    w.mem.set32(at, 0x11111111);
    w.mem.set32(at + 4, 8);
    at += 16;
    w.mem.set32(at, 0x22222222);
    w.mem.set32(at + 4, 0);
    at += 8;
    w.mem.set32(at, 0x73424752);
    w.mem.set32(at + 4, 4);
    w.mem.set32(at + 12, 0xffffffff);
    CHECK(w.helpers.find_rgbs_chunk(kObject) == at + 8);
    w.mem.set32(kObject + 0x40, 0xffffffff);
    w.mem.set32(kObject, 0xffffffff);
    CHECK(w.helpers.find_rgbs_chunk(kObject) == 0);
}

void test_colour_text_step_wraps() {
    World w;
    w.mem.set32(kObject + 0x5c, 4);
    w.mem.set32(kObject + 0x58, 3);
    w.helpers.colour_text_step(kObject, 1);
    CHECK(w.helpers.colour_text_index(kObject) == 0);
    w.helpers.colour_text_step(kObject, 0);
    CHECK(w.helpers.colour_text_index(kObject) == 3);
    w.helpers.colour_text_set_index(kObject, 9);       // out of range: reported, then stored anyway like retail
    CHECK(!w.svc.calls().empty() && w.svc.calls().back().name == "assert");
}

void test_stepper_wraps_within_words() {
    World w;
    w.mem.set32(kObject + 0xf4, 4);
    w.mem.set32(kObject + 0x10, 3);
    w.helpers.stepper_step(kObject, 1);
    CHECK(w.mem.s32(kObject + 0x10) == 1);
    w.helpers.stepper_step(kObject, 0);
    CHECK(w.mem.s32(kObject + 0x10) == 3);
}

void test_ellipsize_text_shortens_to_fit() {
    World w;
    std::string text = "aaaaaaaaaa";   // 60 pixels
    w.fonts.ellipsize_text(kFontAt, text, 100);
    CHECK(text == "aaaaaaaaaa");
    text = "aaaaaaaaaa";
    w.fonts.ellipsize_text(kFontAt, text, 40);
    CHECK(text.size() <= 3 + 3 && text.substr(text.size() - 3) == "...");
}

void test_light_defaults_halves_and_doubles() {
    World w;
    for (std::uint32_t i = 0; i < 13; ++i) {
        w.mem.set32(0x0053dc1c + 4 * i, 0);
    }
    w.mem.set16(0x0053dc1c + 0x16, 100);
    w.mem.set16(0x0053dc1c + 0x1e, 7);
    w.mem.set32(0x0053dc1c + 0x28, 10);
    w.helpers.light_defaults(kObject);
    CHECK(w.mem.s16(kObject + 0x16) == 50);
    CHECK(w.mem.u16(kObject + 0x1e) == 14);
    CHECK(w.mem.s32(kObject + 0x28) == 15);
    CHECK(w.mem.u16(kObject + 0x0c) == 0xf04f);
}

}  // namespace

int main() {
    test_chunk_walk();
    test_colour_text_step_wraps();
    test_stepper_wraps_within_words();
    test_ellipsize_text_shortens_to_fit();
    test_light_defaults_halves_and_doubles();
    if (failures == 0) {
        std::puts("skater windows: all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
