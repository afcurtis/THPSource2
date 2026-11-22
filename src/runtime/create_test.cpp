// Unit tests for the native create-a-skater code (Phase 4.9) that need no retail capture: they use
// the generated static tables (create_tables.cpp) and, when given, the real CRETEX.BIN.
//
//   opentony_create_test [CRETEX.BIN]
//
// Exit codes: 0 pass, 1 failure, 77 (skip) never: the CRETEX.BIN checks are simply left out without the file.
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_parts.hpp"
#include "create_spool.hpp"
#include "create_tables.hpp"
#include "create_textures.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::create;

namespace {

int failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

struct World {
    RetailMemory mem;
    RetailServices svc;
    World() {
        add_create_static_regions(mem);
        mem.add_region(0x10000, 0x40000);   // scratch for objects
    }
};

void put_text(RetailMemory& m, std::uint32_t at, const std::string& s) {
    std::vector<std::uint8_t> bytes(s.begin(), s.end());
    bytes.push_back(0);
    m.write(at, bytes);
}

void test_colour() {
    World w;
    CreateColor color(w.mem, w.svc);
    // pure red, green, blue: hue 0, 120, 240 degrees, full saturation, full value
    const struct { int r, g, b; std::uint32_t hue; } cases[] = {{255, 0, 0, 0}, {0, 255, 0, 120u << 20}, {0, 0, 255, 240u << 20}};
    for (const auto& c : cases) {
        color.rgb_to_hsv(static_cast<std::uint32_t>(c.r), static_cast<std::uint32_t>(c.g), static_cast<std::uint32_t>(c.b), 0x10000,
                         0x10004, 0x10008);
        expect(w.mem.u32(0x10000) == c.hue, "hue of a primary");
        expect(w.mem.u32(0x10004) == 0x100000, "saturation of a primary");
        expect(w.mem.u32(0x10008) == 255u << 12, "value of a primary");
        color.hsv_to_rgb(static_cast<std::int32_t>(w.mem.u32(0x10000)), 0x100000, static_cast<std::int32_t>(w.mem.u32(0x10008)),
                         0x10010, 0x10011, 0x10012);
        // (the round trip of a saturation of exactly 1.0 is clamped to 0xfffff, so allow one step of error)
        expect(w.mem.u8(0x10010) + 1 >= c.r && w.mem.u8(0x10011) + 1 >= c.g && w.mem.u8(0x10012) + 1 >= c.b, "round trip");
    }
    // grey has no hue or saturation
    color.rgb_to_hsv(80, 80, 80, 0x10000, 0x10004, 0x10008);
    expect(w.mem.u32(0x10000) == 0 && w.mem.u32(0x10004) == 0, "grey");
    // preset 0 is "none"; preset 1 comes from the table
    color.preset_hsv(0, 0x10000, 0x10004, 0x10008);
    expect(w.mem.u32(0x10000) == 0xffffffffu && w.mem.u32(0x10008) == 0xffffffffu, "no preset");
    color.preset_hsv(1, 0x10000, 0x10004, 0x10008);
    expect(w.mem.u32(0x10000) == w.mem.u32(kPalettePresets + 12), "preset row 1");
    expect(w.svc.calls().empty(), "no assertions in the colour tests");
}

void test_parts() {
    World w;
    CreateParts parts(w.mem, w.svc);
    put_text(w.mem, 0x10100, "head");
    expect(parts.group_index(0x10100) == 0, "head is group 0");
    put_text(w.mem, 0x10100, "chest");
    expect(parts.group_index(0x10100) == 1, "chest is group 1");
    put_text(w.mem, 0x10100, "left_shoe");
    expect(parts.group_index(0x10100) == 2, "left_shoe is group 2");
    put_text(w.mem, 0x10100, "back_wheel");
    expect(parts.group_index(0x10100) == 3, "back_wheel is group 3");
    expect(w.svc.calls().empty(), "no assertion for known parts");
    put_text(w.mem, 0x10100, "tail");
    expect(parts.group_index(0x10100) == -1, "unknown part");
    expect(w.svc.calls().size() == 1 && w.svc.calls()[0].name == "assert", "unknown part asserts");
}

void test_palette_table() {
    World w;
    CreateColor color(w.mem, w.svc);
    TextureSets tex(w.mem, w.svc);
    CreatePalette palette(w.mem, w.svc, color, tex);
    put_text(w.mem, 0x10100, "CRPANTS0.BMP");
    expect(palette.bitmap_hue(0x10100, 0x10200, 0x10204, 0x10208), "the table lists crpants0.bmp in any case");
    expect(w.mem.u32(0x10200) == 235, "hue of crpants0.bmp");
    put_text(w.mem, 0x10100, "nothing.bmp");
    expect(!palette.bitmap_hue(0x10100, 0x10200, 0x10204, 0x10208), "an unlisted bitmap");
}

void test_spool_and_text() {
    World w;
    CreateSpool spool(w.mem, w.svc);
    // an empty queue: a nonzero flag marks an entry free (zeroed memory is a queue full of requests)
    for (std::uint32_t i = 0; i < kSpoolCount; ++i) {
        w.mem.set32(0x10000 + kSpoolEntries + i * kSpoolStride + kSpoolFree, 1);
    }
    spool.compact(0x10000);
    expect(spool.find_free(0x10000) == 0, "first free entry of an empty queue");
    put_text(w.mem, 0x12000, "a.bmp");
    spool.add(0x10000, 7, 0x12000, 1, 10, 20, 30);
    expect(spool.find_free(0x10000) == 1, "one entry in use");
    put_text(w.mem, 0x12000, "b.bmp");
    spool.add(0x10000, 7, 0x12000, 1, 99, 99, 99);   // the same request: only the name changes
    expect(spool.find_free(0x10000) == 1, "a repeated request reuses its entry");
    expect(w.mem.cstring(0x10000 + 8 + kSpoolName) == "b.bmp", "the repeated request renames the entry");
    for (std::uint32_t i = 0; i < 64; ++i) {
        spool.add(0x10000, 100 + i, 0x12000, 2, 0, 0, 0);
    }
    expect(w.svc.calls().size() == 1, "the 64th distinct request does not fit");

    // the tokenizer and the layout (every character is 10 pixels wide, so 50 characters a line)
    World t;
    CreateSpool text(t.mem, t.svc);
    put_text(t.mem, 0x12000, "alpha beta&gamma");
    std::uint32_t next = text.next_word(0x12000, 0x12100);
    expect(t.mem.cstring(0x12100) == "alpha" && next == 0x12006, "first word and the space after it");
    next = text.next_word(next, 0x12100);
    expect(t.mem.cstring(0x12100) == "beta" && t.mem.u8(next) == '&', "a & ends a word and is left in place");
    // the layout: a forced break (&), a line that would be too wide, and the word that moves to the next line
    put_text(t.mem, 0x12000, "aa bb cc&dd ee");
    for (const std::int32_t width : {100, 200, 300, 100, 600, 100}) {
        t.svc.script(kTextWidth, width);
    }
    text.layout(0x11000, 0x12000);
    expect(t.mem.cstring(0x11000 + kTextLines) == "aa bb cc", "first line");
    expect(t.mem.cstring(0x11000 + kTextLines + kTextLineStride) == "dd", "line after the forced break");
    expect(t.mem.cstring(0x11000 + kTextLines + 2 * kTextLineStride) == "ee", "the word that did not fit starts a line");
    expect(t.mem.u32(0x11000 + kTextLineCount) == 3, "three lines");
    expect(t.svc.calls().size() == 6, "six width queries");
    expect(t.svc.calls()[4].text == "dd ee", "the too-wide candidate is reported");
}

void test_cretex(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "note: " << path << " not found, skipping the CRETEX.BIN checks\n";
        return;
    }
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    World w;
    w.mem.add_region(0x100000, 0x7000);   // the manager
    w.mem.add_region(0x200000, 0x4000);   // the texture-entry arena
    w.mem.add_region(0x300000, data.size() + 16);
    w.mem.write(0x300000, data);
    put_text(w.mem, 0x10100, "cretex.bin");
    w.svc.script(kFileOpen, 0x300000);
    for (std::uint32_t i = 0; i < 400; ++i) {
        w.svc.script(kAlloc, static_cast<std::int32_t>(0x200000 + i * 0x20));
    }
    TextureSets sets(w.mem, w.svc);
    sets.load_cretex(0x100000, 0x10100);
    expect(w.mem.u32(0x100000 + kSetCountOffset) == 369, "CRETEX.BIN makes 369 texture sets");
    const std::uint32_t first = 0x100000 + kSetsOffset;
    expect(w.mem.cstring(first + kSetName) == "afro" && w.mem.cstring(first + kSetDisplay) == "Afro guy", "the first set");
    expect(w.mem.u8(first + kSetGroup) == 0 && w.mem.s32(first + kSetRoot) == -1, "a group 0 set with no root");
    const std::uint32_t last = 0x100000 + kSetsOffset + 368 * kSetStride;
    expect(w.mem.cstring(last + kSetName) == "afrowhite", "the last set is one the code adds itself");
    expect(w.mem.u32(0x100000 + kDatabaseOffset + kDatabaseCountOffset) > 100, "the database holds the textures");
    for (const auto& call : w.svc.calls()) {
        expect(call.name != "assert", "CRETEX.BIN loads without a failed assertion");
    }
}

}  // namespace

int main(int argc, char** argv) {
    test_colour();
    test_parts();
    test_palette_table();
    test_spool_and_text();
    test_cretex(argc > 1 ? argv[1] : "");
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "create unit tests passed\n";
    return 0;
}
