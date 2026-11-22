// Unit tests of the native frontend text layer (Phase 4.10a): fonts, messages, the window base and the text
// effects, on hand-built memory. The retail forced-call replay (frontend_retail_calls_test.cpp) is the evidence the
// code matches the original; these tests pin the behaviour a reader would expect and catch regressions without the
// fixtures.
#include "frontend_effects.hpp"
#include "frontend_mess.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"

#include <cstdio>
#include <cstdlib>
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

constexpr std::uint32_t kSlab = 0x10000000;
constexpr std::uint32_t kFont = kSlab;                 // Font object
constexpr std::uint32_t kGlyphs = kSlab + 0x200;       // glyph table
constexpr std::uint32_t kSprites = kSlab + 0x400;      // fake sprites, 0x20 bytes each
constexpr std::uint32_t kText = kSlab + 0x1000;
constexpr std::uint32_t kObjects = kSlab + 0x2000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    Mess mess{mem, svc, fonts};
    WindowBase window{mem, svc, fonts};
    TextEffects effects{mem, svc, fonts};

    World() {
        mem.add_region(kSlab, 0x4000);
        mem.add_region(kMessFont, 0x180);
        mem.add_region(kSimulationTime, 4);
        mem.add_region(kFontTable, 0x20);
        mem.add_region(kFontNames, 0x80);
        mem.add_region(kScreenHeight, 4);
        mem.add_region(kPausedA, 4);
        mem.add_region(kPausedB, 4);
        mem.add_region(kBloodSprite, 4);
        mem.add_region(kGameMode, 8);
        mem.add_region(kEmptyString, 0x10);
        mem.set32(kScreenHeight, 480);
    }

    // A font of `count` glyphs plus the sentinel, every glyph `advance` wide and 10 rows tall starting at row 2.
    void make_font(std::uint32_t font, std::uint32_t glyphs, std::uint32_t sprites, std::uint32_t count, std::uint32_t advance,
                   std::uint32_t mapping) {
        mem.set32(font + kFontScale, 0x1000);
        mem.set32(font + kFontGlyphs, glyphs);
        mem.set32(font + kFontGlyphCount, count);
        mem.set32(font + kFontClut, 0xffffffffu);
        mem.set32(font + kFontSpacing, 1);
        for (std::uint32_t g = 0; g <= count; ++g) {
            mem.set32(glyphs + g * 8, sprites + g * 0x20);
            mem.set8(glyphs + g * 8 + kGlyphAdvance, advance);
            mem.set8(glyphs + g * 8 + kGlyphBottom, 12);
            mem.set8(glyphs + g * 8 + kGlyphTop, 2);
        }
        fonts.set_mapping(font, mapping);
    }

    void put(std::uint32_t at, const std::string& s) {
        std::vector<std::uint8_t> bytes(s.begin(), s.end());
        bytes.push_back(0);
        mem.write(at, bytes);
    }

    std::size_t count_calls(const char* name) const {
        std::size_t n = 0;
        for (const auto& call : svc.calls()) {
            n += call.name == name ? 1 : 0;
        }
        return n;
    }
};

void test_escape_and_mapping() {
    CHECK(FontTools::escape_class('[') == 0xfd);
    CHECK(FontTools::escape_class(']') == 0xfe);
    CHECK(FontTools::escape_class('}') == 0xfe);
    CHECK(FontTools::escape_class('{') == 0xfc);
    CHECK(FontTools::escape_class('<') == 0xfb);
    CHECK(FontTools::escape_class('|') == 0xf9);
    CHECK(FontTools::escape_class('~') == 0xf8);
    CHECK(FontTools::escape_class('^') == 0xfa);
    CHECK(FontTools::escape_class('A') == 0);

    World w;
    w.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    CHECK(w.fonts.glyph_index(kFont, 'A') == 0);
    CHECK(w.fonts.glyph_index(kFont, 'z') == 25);
    CHECK(w.fonts.glyph_index(kFont, '0') == 26);
    CHECK(w.fonts.glyph_index(kFont, '9') == 35);
    CHECK(w.fonts.glyph_index(kFont, ' ') == 0xff);
    CHECK(w.fonts.glyph_index(kFont, '?') == 0x24);
    CHECK(w.fonts.glyph_index(kFont, '_') == 0x30);
    CHECK(w.fonts.glyph_index(kFont, '[') == 0xff);        // an escape character has no glyph
    CHECK(w.fonts.glyph_index(kFont, 0xe9) == 0x31);       // anything else is the sentinel glyph
    CHECK(w.mem.u8(kFont + kFontMap + 'A') == 0);          // set_mapping filled the table
    CHECK(w.mem.u8(kFont + kFontMap + ' ') == 0xff);
    // a font with fewer glyphs than the punctuation needs maps the missing punctuation to the sentinel glyph
    w.mem.set32(kFont + kFontGlyphCount, 0x28);
    CHECK(w.fonts.glyph_index(kFont, '?') == 0x24);        // present: index 0x24 is below the count
    CHECK(w.fonts.glyph_index(kFont, '/') == 0x28);        // index 0x2e is not: the sentinel (the count)
    w.mem.set32(kFont + kFontGlyphCount, 0x31);
    // the digits mapping
    w.mem.set32(kFont + kFontMapping, 1);
    CHECK(w.fonts.glyph_index(kFont, '7') == 7);
    CHECK(w.fonts.glyph_index(kFont, ':') == 10);
    CHECK(w.fonts.glyph_index(kFont, ' ') == 0xff);
    CHECK(w.svc.calls().empty());
    w.mem.set32(kFont + kFontMapping, 9);
    (void)w.fonts.glyph_index(kFont, 'a');
    CHECK(w.count_calls("assert") == 1);                    // "Unrecognized char mapping"
}

void test_metrics() {
    World w;
    w.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    w.put(kText, "AB CD");
    // four glyphs of 8 + 1 spacing, and a space as wide as glyph 0 without spacing
    CHECK(w.fonts.text_width(kFont, kText) == 4 * 9 + 8);
    w.mem.set32(kFont + kFontScale, 0x800);
    CHECK(w.fonts.text_width(kFont, kText) == (4 * 9 + 8) / 2);
    w.mem.set32(kFont + kFontScale, 0x1000);
    CHECK(w.fonts.text_height(kFont, kText) == 10);
    CHECK(w.fonts.text_top(kFont, kText) == 2);
    CHECK(w.fonts.text_extent(kFont, kText) == 12);
    w.put(kText, "[A]");                                    // escape characters take no room
    CHECK(w.fonts.text_width(kFont, kText) == 9);
    // monospace advance
    CHECK(w.fonts.char_advance(kFont, 'A') == 9);
    CHECK(w.fonts.char_advance(kFont, '[') == 0);
}

void test_ellipsize() {
    World w;
    w.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    w.put(kText, "HELLO WORLD THIS IS LONG");
    const std::int32_t max = 150;
    w.fonts.ellipsize(kFont, kText, max);
    const std::string cut = w.mem.cstring(kText);
    CHECK(cut.size() >= 4 && cut.substr(cut.size() - 3) == "...");
    // the retail loop measures the string with a second "..." appended (see the evidence page): it fits that way
    w.put(kText + 0x100, cut + "...");
    CHECK(w.fonts.text_width(kFont, kText + 0x100) <= max);
    // a string that fits is left alone
    w.put(kText, "HI");
    w.fonts.ellipsize(kFont, kText, 150);
    CHECK(w.mem.cstring(kText) == "HI");
}

void test_escapes_and_draw() {
    World w;
    w.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    w.mem.set32(kFont + kFontRed, 10);
    w.mem.set32(kFont + kFontGreen, 20);
    w.mem.set32(kFont + kFontBlue, 30);
    w.fonts.apply_escape(kFont, '{');                      // amber, the old colour saved
    CHECK(w.mem.u32(kFont + kFontRed) == 0xff && w.mem.u32(kFont + kFontGreen) == 0x80 && w.mem.u32(kFont + kFontBlue) == 0);
    CHECK(w.mem.u8(kFont + kFontSaved) == 10 && w.mem.u8(kFont + kFontSaved + 2) == 30);
    w.fonts.apply_escape(kFont, '}');
    CHECK(w.mem.u32(kFont + kFontRed) == 10 && w.mem.u32(kFont + kFontBlue) == 30);

    w.put(kText, "AB C");
    w.fonts.draw(kFont, 100, 50, kText, 7);
    CHECK(w.count_calls(kSpriteDraw) == 3);                // three glyphs; the space draws nothing
    const auto& first = w.svc.calls().front();
    CHECK(first.args.size() == 4 && first.args[1] == 100 && first.args[3] == 7);
    // the sprite was set up: scale, mode 1, the font colour
    const std::uint32_t sprite = first.args[0];
    CHECK(w.mem.u32(sprite + kSpriteScale) == 0x1000 && w.mem.u8(sprite + kSpriteMode) == 1);
    CHECK(w.mem.u8(sprite + kSpriteRed) == 10);

    // a shadow doubles the draws
    World s;
    s.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    s.mem.set8(kFont + kFontShadow, 1);
    s.put(kText, "AB");
    s.fonts.draw(kFont, 0, 0, kText, 0);
    CHECK(s.count_calls(kSpriteDraw) == 4);

    // centred text starts half its width to the left
    World c1, c2;
    for (World* w2 : {&c1, &c2}) {
        w2->make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
        w2->put(kText, "AB");
    }
    c2.mem.set32(kFont + kFontHAlign, 1);
    c1.fonts.draw(kFont, 100, 0, kText, 0);
    c2.fonts.draw(kFont, 100, 0, kText, 0);
    CHECK(c1.svc.calls().front().args[1] == 100);
    CHECK(c2.svc.calls().front().args[1] == 100 - 9);
}

void test_window_base() {
    World w;
    const std::uint32_t win = kObjects;
    w.window.window_construct(win, 10, 20, -1, -1);
    CHECK(w.mem.u32(win) == kVtWindow && w.mem.u32(win + kElemX) == 10 && w.mem.u32(win + kElemY) == 20);
    CHECK(w.mem.u32(win + kWinCount) == 0);
    for (std::uint32_t i = 0; i < 32; ++i) {
        w.mem.set32(kObjects + 0x400 + i * 0x40, kSlab);   // child objects (their vtable is the engine's)
        w.window.window_add(win, kObjects + 0x400 + i * 0x40);
    }
    CHECK(w.mem.u32(win + kWinCount) == 32);
    CHECK(w.svc.calls().empty());
    w.window.window_add(win, kObjects + 0x400);             // a 33rd
    CHECK(w.count_calls("assert") == 1 && w.mem.u32(win + kWinCount) == 32);
    w.svc.clear();
    w.window.window_draw(win, 5, 6);
    CHECK(w.count_calls(kElementDraw) == 32);
    CHECK(w.svc.calls().front().args[1] == 15 && w.svc.calls().front().args[2] == 26);   // offset by the window's position
    w.svc.clear();
    w.mem.set32(win + kElemHidden, 1);
    w.window.window_draw(win, 5, 6);
    CHECK(w.svc.calls().empty());

    // an element slides toward its target a quarter of the way, snapping within one unit, then reports arrival
    const std::uint32_t e = kObjects + 0x200;
    w.window.element_construct(e, 100, 0, 0, 0);
    CHECK(w.mem.u32(e + kElemX) == 0 && w.mem.u32(e + kElemTargetX) == 100);
    int frames = 0;
    while (w.mem.u8(e + kElemArrived) == 0 && frames < 100) {
        w.window.element_update(e);
        ++frames;
    }
    CHECK(frames > 5 && frames < 40);
    CHECK(w.mem.s32(e + kElemX) == 100 && w.mem.u8(e + kElemArrived) == 1);
    // a delay holds it still
    w.window.element_construct(e, 100, 0, 0, 0);
    w.mem.set32(e + kElemDelay, 3);
    w.window.element_update(e);
    CHECK(w.mem.u32(e + kElemX) == 0 && w.mem.u32(e + kElemDelay) == 2);
    w.window.element_snap_x(e);
    CHECK(w.mem.u32(e + kElemX) == 100);
}

void test_messages() {
    World w;
    w.make_font(kMessFont, kGlyphs, kSprites, 0x31, 8, 0);
    w.mem.set32(kMessFont + kFontScale, 0x1000);
    const std::uint32_t program = kObjects;                 // one step of three frames that then ends the message
    w.mem.set16(program, 3);
    w.mem.set8(program + 0x1e, 0);
    w.mem.set16(program + 4, 0x100);
    w.mem.set16(program + 6, 0x100);
    w.put(kText, "HELLO");
    w.svc.script(kAllocService, static_cast<std::int32_t>(kObjects + 0x100));
    const std::uint32_t node = w.mess.add_message(kText, program, 0, 40, 50);
    CHECK(node == kObjects + 0x100 && w.mem.u32(kMessList) == node && w.mess.exists(node));
    w.svc.clear();
    for (int i = 0; i < 3; ++i) {
        w.mess.update();
        CHECK(w.mess.exists(node));
    }
    CHECK(w.mem.u16(node + kNodeAge) == 3);
    w.mess.update();                                        // the step is over and the next is 0: deleted
    CHECK(!w.mess.exists(node) && w.mem.u32(kMessList) == 0);
    CHECK(w.count_calls(kMemDelete) == 1);
}

void test_text_effects() {
    World w;
    w.make_font(kFont, kGlyphs, kSprites, 0x31, 8, 0);
    const std::uint32_t fall = kObjects;
    w.effects.falling_construct(fall);
    CHECK(w.mem.u32(fall) == kEffVtFalling && w.mem.u32(fall + kEffEnabled) == 0);
    for (int i = 0; i < 32 * 3 + 1; ++i) {
        w.svc.script(kRndService, i % 7);
    }
    w.effects.falling_init(fall);
    CHECK(w.count_calls(kRndService) == 97);                // 32 glyph records of three draws, and the blood flag
    CHECK(w.mem.u32(fall + kEffEnabled) == 1 && w.mem.u32(fall + kFallFloor) == 480 + 0x1e);
    const std::uint16_t before = w.mem.u16(fall + kFallSlots);
    w.effects.falling_update(fall);
    CHECK(w.mem.u16(fall + kFallSlots) == static_cast<std::uint16_t>(before - 1) && w.mem.u32(fall + kFallFrame) == 1);
    // the colour callback paints a blood red, only while enabled
    const std::uint32_t colours = kObjects + 0x100;
    w.effects.falling_colour(fall, colours, colours + 1, colours + 2);
    CHECK(w.mem.u8(colours) == 0xff && w.mem.u8(colours + 1) == 0x32 && w.mem.u8(colours + 2) == 0x32);
    w.effects.effect_disable(fall);
    w.mem.set8(colours, 1);
    w.effects.falling_colour(fall, colours, colours + 1, colours + 2);
    CHECK(w.mem.u8(colours) == 1);

    // binding stores the effect in the font, a second bind asserts, unbinding clears it
    const std::uint32_t zoom = kObjects + 0x200;
    w.effects.zoom_construct(zoom);
    w.effects.zoom_init(zoom);
    w.svc.clear();
    w.effects.effect_bind(zoom, kFont);
    CHECK(w.fonts.effect(kFont) == zoom && w.svc.calls().empty());
    w.effects.effect_bind(zoom, kFont);
    CHECK(w.count_calls("assert") == 1);
    w.effects.effect_unbind(zoom, kFont);
    CHECK(w.fonts.effect(kFont) == 0);

    // the zoom runs states 1, 2, 3, 4 and 7, a frame at a time, and ends with the font scaled to nothing
    w.mem.set32(kGameMode + 4, 1);                          // level kind 1: the target scale is 0x1000
    w.mem.set32(kFont + kFontEffect, 0);
    int guard = 0;
    std::vector<std::uint32_t> states;
    while (w.mem.u32(zoom + kZoomState) != 7 && guard++ < 200) {
        const std::uint32_t state = w.mem.u32(zoom + kZoomState);
        if (states.empty() || states.back() != state) {
            states.push_back(state);
        }
        w.effects.zoom_update(zoom);
        w.mem.set32(kFont + kFontEffect, 0);
        w.effects.zoom_bind(zoom, kFont);
    }
    CHECK((states == std::vector<std::uint32_t>{1, 2, 3, 4}));
    w.effects.zoom_update(zoom);
    w.mem.set32(kFont + kFontEffect, 0);
    w.effects.zoom_bind(zoom, kFont);
    CHECK(w.mem.u32(kFont + kFontScale) == 0 && w.effects.zoom_finished(zoom) == 0);
}

}  // namespace

int main() {
    test_escape_and_mapping();
    test_metrics();
    test_ellipsize();
    test_escapes_and_draw();
    test_window_base();
    test_messages();
    test_text_effects();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("frontend text layer: all checks passed\n");
    return 0;
}
