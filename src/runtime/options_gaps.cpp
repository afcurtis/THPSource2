#include "auto_views.hpp"
#include "options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// VideoWin fields
constexpr std::uint32_t kVLevel = 0xac;
constexpr std::uint32_t kVMinusOne = 0xb0;
constexpr std::uint32_t kVZero = 0xb4;
constexpr std::uint32_t kVWidth = 0xb8;
constexpr std::uint32_t kVTexX = 0xbc;          // two words the engine's texture-page call leaves (this build's call does nothing)
constexpr std::uint32_t kVTexY = 0xc0;
constexpr std::uint32_t kVSizeX = 0xc4;
constexpr std::uint32_t kVSizeY = 0xc8;
constexpr std::uint32_t kVPacket = 0xcc;
constexpr std::uint32_t kVSprites = 0xd0;       // one 64 x 64 picture for each level
constexpr std::uint32_t kVSlotA = 0x140;
constexpr std::uint32_t kVSlotB = 0x144;
constexpr std::uint32_t kVReady = 0x148;

constexpr std::uint32_t kLevelTable = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kTexturePageFlag = 0x0056af5c;
constexpr std::uint32_t kTexturePageFlagB = 0x0056af58;
constexpr std::uint32_t kMsgZeroVideoImage = 0x0052df74;
constexpr std::uint32_t kFormatString = 0x005256dc;         // "%s"
constexpr std::uint32_t kVideoBackgroundFile = 0x0052dfb0;  // "s2win5.bmp"

// GapChecklist fields
constexpr std::uint32_t kGSource = 0xac;        // the font the window was built with (its copy is made at +0xb0)
constexpr std::uint32_t kGFontA = 0xb0;
constexpr std::uint32_t kGFontB = 0x1fc;
constexpr std::uint32_t kGList = 0x348;
constexpr std::uint32_t kGRows = 0x34c;
constexpr std::uint32_t kGStyleGot = 0x4cc;
constexpr std::uint32_t kGStyleHeader = 0x4d0;
constexpr std::uint32_t kGStyleMissing = 0x4d4;
constexpr std::uint32_t kGShown = 0x4d8;        // the level the rows are for (-1: none)
constexpr std::uint32_t kGWanted = 0x4dc;       // the level asked for
constexpr std::uint32_t kGTitle = 0x4e0;
constexpr std::uint32_t kGCount = 0x4e4;
constexpr std::uint32_t kGZero = 0x4e8;
constexpr std::uint32_t kGVideo = 0x4ec;
constexpr std::uint32_t kGPrompts = 0x4f0;
constexpr std::uint32_t kGArrowLeft = 0x4fc;
constexpr std::uint32_t kGArrowRight = 0x500;

constexpr std::uint32_t kGapTable = 0x00541990;     // 0x2c-byte entries: +0 a word (0 ends the table), +4 0xfffe for a header, +6 the id, +8 the name
constexpr std::uint32_t kGapStride = 0x2c;
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kGapTablePointer = 0x0056dd58;
constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;
constexpr std::uint32_t kPadReleased = 0x0056b069;

constexpr std::uint32_t kMsgNoMenuItem = 0x00535d98;
constexpr std::uint32_t kMsgFakeGap = 0x00535d80;
constexpr std::uint32_t kMsgTooManyGaps = 0x00535d64;
constexpr std::uint32_t kFormatNumber = 0x00525764;
constexpr std::uint32_t kTextUnknown = 0x005253ec;
constexpr std::uint32_t kTextGapsOf = 0x0052e7ac;           // pointer variable: "%d of %d gaps"

}  // namespace

// ---- the level information window (videowin.cpp) -------------------------------------------------------------------------------------------------------

// 0x0045b0a0: a window; with a background an element of s2win5.bmp. The engine's texture page call (a no-op in this build) is made with the window's rectangle, the packet of
// a textured quad is filled from it, and a 64 x 64 picture is made for each of the fourteen levels from the level table's video names.
std::uint32_t OptionsScreen::video_construct(std::uint32_t self, std::uint32_t level, std::uint32_t with_background, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                             std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    set_word_at(m_, self, kVtVideoWindow);
    if (with_background != 0) {
        const std::uint32_t memory = operator_new(0x2c);
        std::uint32_t element = 0;
        if (memory != 0) {
            element = w_.sprite_construct(memory, kVideoBackgroundFile, 0, 0, -1, -1, 0, 0);
        }
        w_.window_add(self, element);
    }
    set_word_at(m_, self + kVSizeX, 0xb0);
    set_word_at(m_, self + kVSizeY, 0x30);
    set_global_word(m_, kTexturePageFlag, 1);
    set_global_word(m_, kTexturePageFlagB, 1);
    engine(0x00489440, {0, word_at(m_, self + kVSizeX), word_at(m_, self + kVSizeY), self + kVTexX, self + kVTexY, 0x10, 0, 0});
    set_global_word(m_, kTexturePageFlag, 0);
    set_global_word(m_, kTexturePageFlagB, 0);
    s_.note(kAllocBuffer, {0x30, 1, 1, 0});
    const std::uint32_t packet = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    set_word_at(m_, self + kVPacket, packet);
    const std::uint8_t tex_x = byte_at(m_, self + kVTexX);
    const std::uint8_t tex_y = byte_at(m_, self + kVTexY);
    set_byte_at(m_, packet + 7, 0x2c);
    set_byte_at(m_, packet + 4, 0x80);
    set_byte_at(m_, packet + 5, 0x80);
    set_byte_at(m_, packet + 6, 0x80);
    set_byte_at(m_, packet + 0xc, tex_x & 0x3f);
    set_byte_at(m_, packet + 0xd, tex_y);
    set_byte_at(m_, packet + 0x14, (tex_x & 0x3f) + byte_at(m_, self + kVSizeX));
    set_byte_at(m_, packet + 0x15, tex_y);
    set_byte_at(m_, packet + 0x1c, tex_x & 0x3f);
    set_byte_at(m_, packet + 0x1d, tex_y + byte_at(m_, self + kVSizeY));
    set_byte_at(m_, packet + 0x24, (tex_x & 0x3f) + byte_at(m_, self + kVSizeX));
    set_byte_at(m_, packet + 0x25, tex_y + byte_at(m_, self + kVSizeY));
    set_byte_at(m_, packet + 7, byte_at(m_, packet + 7) & 0xfd);
    set_byte_at(m_, packet + 7, byte_at(m_, packet + 7) & 0xfe);
    const std::uint32_t page = (static_cast<std::uint32_t>(half_at(m_, self + kVTexY) & 0x200) | 0x40) << 2;
    const std::uint32_t flags = page | (static_cast<std::uint32_t>(int_at(m_, self + kVTexY) >> 4) & 0x10) | (static_cast<std::uint32_t>(int_at(m_, self + kVTexX) >> 6) & 0xf);
    set_half_at(m_, packet + 0x16, flags);
    set_word_at(m_, self + kVZero, 0);
    set_word_at(m_, self + kVLevel, level);
    set_word_at(m_, self + kVMinusOne, 0xffffffffu);
    set_word_at(m_, self + kVWidth, 0x20);
    set_word_at(m_, self + kVSlotA, 10);
    set_word_at(m_, self + kVSlotB, 0x13);
    std::uint32_t slot = self + kVSprites;
    for (std::uint32_t row = kLevelTable; row < kLevelTable + 14 * kLevelStride; row += kLevelStride) {
        const std::string name = retail_format(m_.cstring(kFormatString), {m_.cstring(word_at(m_, row + 0x68), 0x100)}, {});
        check(!name.empty(), kMsgZeroVideoImage);
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            const std::uint32_t buffer = scratch_line(name);
            s_.note(kSpriteConstruct, {memory, buffer, 0, 0x40, 0x40}, name);
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, slot, sprite);
        slot += 4;
    }
    set_byte_at(m_, self + kVReady, 1);
    return self;
}

// ---- the gap checklist (class at 0x005193e4) -------------------------------------------------------------------------------------------------------

// 0x00484ab0: the two levels that must be opened first (8 and 9) are locked until the career opened them with the third medal; every other level is not.
bool OptionsScreen::gaps_locked(std::int32_t level) {
    if (level == 8 || level == 9) {
        return engine_value(0x004159d0, {u(level), 3}) == 0;
    }
    return false;
}

// 0x00484ac... the checklist: a level (the current one, moved on to one that is not locked and is not the park or a later one), the rows of that level's gaps, the video
// window of the level and the prompts. `font` is the options window's mainf, copied for the rows.
std::uint32_t OptionsScreen::gaps_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(self + kGFontA);
    font_construct(self + kGFontB);
    options_gaps_gaps_at(m_, self).set_field_0(static_cast<Word>(kVtGapChecklist));
    options_gaps_gaps_at(m_, self).set_field_ac(static_cast<Word>(font));
    options_gaps_gaps_at(m_, self).set_field_4d8(0xffffffffu);
    options_gaps_gaps_at(m_, self).set_field_4dc(static_cast<Word>(global_word(m_, kCurrentLevel)));
    for (;;) {
        const std::int32_t level = static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc());
        if (!gaps_locked(level) && level != 0xd && level < 0xa) {
            break;
        }
        const std::int32_t next = static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc()) + 1;
        options_gaps_gaps_at(m_, self).set_field_4dc(static_cast<Word>(u(next)));
        if (next >= 0xe) {
            options_gaps_gaps_at(m_, self).set_field_4dc(0);
        } else if (next < 0) {
            options_gaps_gaps_at(m_, self).set_field_4dc(0xd);
        }
    }
    options_gaps_gaps_at(m_, self).set_field_348(0);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kGRows + 4 * i, 0);
    }
    // the prompts' font: s2font2
    copy_font_named(self + kGFontB, 0x0052544c);
    set_word_at(m_, self + kGFontB + 0x14, 0x80);
    set_word_at(m_, self + kGFontB + 0x18, 0x80);
    set_word_at(m_, self + kGFontB + 0x1c, 0x80);
    set_word_at(m_, self + kGFontB + 4, 0);
    set_word_at(m_, self + kGFontB + 8, 3);
    set_byte_at(m_, self + kGFontB + 0x21, 0);
    set_word_at(m_, self + kGFontB + 0xc, 0);
    set_word_at(m_, self + kGFontB + 0x30, 0x800);
    // the rows' font: a copy of the options window's, coloured
    menu_.copy_font_fields(self + kGFontA, font);
    set_word_at(m_, self + kGFontA + 4, 1);
    set_word_at(m_, self + kGFontA + 0x30, 0xc00);
    f_.set_rgb(self + kGFontA, 0x91, 0x12, 0xf);
    // the prompts: select, select, back
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x0052541c, 0x82}, {0x005258d8, 0xf0}, {0x00525400, 0x32}};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t window = prompt(prompts[i].file, 0x005252f0, self + kGFontB, prompts[i].x, 0xdc);
        set_word_at(m_, self + kGPrompts + 4 * i, window);
        w_.window_add(self, window);
    }
    // the three styles: a gap that was found, the header's, a gap that was not found
    auto style = [&](std::uint32_t slot) {
        const std::uint32_t memory = operator_new(0x17c);
        const std::uint32_t made = memory != 0 ? menu_.properties_construct(memory) : 0u;
        set_word_at(m_, self + slot, made);
        return made;
    };
    const std::uint32_t got = style(kGStyleGot);
    menu_.copy_font_fields(got + 4, font);
    set_word_at(m_, got + 0x18, 0x80);
    set_word_at(m_, got + 0x1c, 0x80);
    set_word_at(m_, got + 0x20, 0x80);
    set_word_at(m_, got + 0x34, 0xc00);
    set_word_at(m_, got + 8, 0);
    set_word_at(m_, got + 0x150, 0xc00);
    set_word_at(m_, got + 0x154, 0x800);
    const std::uint32_t header = style(kGStyleHeader);
    menu_.copy_font_fields(header + 4, got + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, header + offset, word_at(m_, got + offset));
    }
    set_word_at(m_, header + 8, 1);
    set_word_at(m_, header + 0x158, 0xb6);
    set_word_at(m_, header + 0x15c, 0x84);
    set_word_at(m_, header + 0x160, 0x24);
    set_word_at(m_, header + 0x164, 0xb6);
    set_word_at(m_, header + 0x168, 0x84);
    set_word_at(m_, header + 0x16c, 0x24);
    const std::uint32_t missing = style(kGStyleMissing);
    menu_.copy_font_fields(missing + 4, got + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, missing + offset, word_at(m_, got + offset));
    }
    set_word_at(m_, missing + 0x158, 0x60);
    set_word_at(m_, missing + 0x164, 0x40);
    set_word_at(m_, missing + 0x15c, 0x40);
    set_word_at(m_, missing + 0x168, 0x40);
    set_word_at(m_, missing + 0x160, 0x10);
    set_word_at(m_, missing + 0x16c, 0x40);
    set_word_at(m_, missing + 0x150, 0xc00);
    set_word_at(m_, missing + 0x154, 0x800);
    // the level's name and the count of gaps found
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::uint32_t memory = operator_new(0x34);
        std::uint32_t text = 0;
        if (memory != 0) {
            text = engine_value(0x004a1ed0, {memory, 0x00527c68, self + kGFontA, 1, 0x8c, i == 0 ? 0x38u : 0x46u, 0xffffffffu, 0xffffffffu});
        }
        set_word_at(m_, self + (i == 0 ? kGTitle : kGCount), text);
        w_.window_add(self, text);
    }
    // the arrows: left (blinks), right
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t mode) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t made = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            made = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, made);
        set_byte_at(m_, made + 0xa, mode);
        s_.note(kSpriteInit, {made});
    };
    arrow(kGArrowLeft, 0x00527c10, 2);
    arrow(kGArrowRight, 0x00527c20, 0);
    options_gaps_gaps_at(m_, self).set_field_4e8(0);
    // the level's video window
    const std::uint32_t memory = operator_new(0x14c);
    std::uint32_t video = 0;
    if (memory != 0) {
        video = engine_value(0x0045b0a0, {memory, options_gaps_gaps_at(m_, self).field_4dc(), 0, 0x11a, 0xfffffffau, 0xffffffffu, 0xffffffffu});
    }
    options_gaps_gaps_at(m_, self).set_field_4ec(static_cast<Word>(video));
    w_.window_add(self, video);
    return self;
}

std::uint32_t OptionsScreen::gaps_delete(std::uint32_t self, std::uint32_t flag) {
    gaps_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00484b10: the list and its rows, the three styles, the spooler (reset), the arrows, the two fonts and the window base.
void OptionsScreen::gaps_destroy(std::uint32_t self) {
    options_gaps_gaps_at(m_, self).set_field_0(static_cast<Word>(kVtGapChecklist));
    if (options_gaps_gaps_at(m_, self).field_348() != 0) {
        delete_object(options_gaps_gaps_at(m_, self).field_348());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kGRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kGRows + 4 * i));
        }
    }
    for (std::uint32_t slot : {kGStyleGot, kGStyleHeader, kGStyleMissing}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    engine(0x004b52b0, {global_word(m_, 0x0056db4c)});
    for (std::uint32_t slot : {kGArrowLeft, kGArrowRight}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    font_destroy(self + kGFontB);
    font_destroy(self + kGFontA);
    w_.window_destroy(self);
}

// 0x004850a0: the square brackets are taken out of a name, in place.
void OptionsScreen::gaps_strip_brackets(std::uint32_t text) {
    std::uint32_t kept = 0;
    for (std::uint32_t i = 0; i < m_.cstring(text, 0x100).size(); ++i) {
        const std::uint8_t c = byte_at(m_, text + i);
        if (c != '[' && c != ']') {
            set_byte_at(m_, text + kept, c);
            ++kept;
        }
    }
    set_byte_at(m_, text + kept, 0);
}

// 0x00485290: the rows from `lo` to `hi` (inclusive) are bubble sorted by their ids until they are in order.
void OptionsScreen::gaps_sort(std::uint32_t self, std::int32_t lo, std::int32_t hi) {
    if (lo >= hi) {
        return;
    }
    const std::int32_t last = hi - 1;
    for (;;) {
        if (lo <= last) {
            std::uint32_t slot = self + kGRows + 4 * u(lo);
            for (std::int32_t k = lo; k <= last; ++k, slot += 4) {
                check(word_at(m_, slot) != 0, kMsgNoMenuItem);
                check(int_at(m_, word_at(m_, slot) + 8) != -2, kMsgFakeGap);
                const std::uint32_t this_row = word_at(m_, slot);
                const std::uint32_t next_row = word_at(m_, slot + 4);
                if (int_at(m_, this_row + 8) > int_at(m_, next_row + 8)) {
                    set_word_at(m_, slot, next_row);
                    set_word_at(m_, slot + 4, this_row);
                }
            }
        }
        if (lo > last) {
            return;
        }
        bool sorted = true;
        std::uint32_t slot = self + kGRows + 4 * u(lo);
        for (std::int32_t k = lo; k <= last; ++k, slot += 4) {
            if (int_at(m_, word_at(m_, slot) + 8) > int_at(m_, word_at(m_, slot + 4) + 8)) {
                sorted = false;
            }
        }
        if (sorted) {
            return;
        }
    }
}

// 0x004853b0: every run of rows between two headers is sorted.
void OptionsScreen::gaps_sort_all(std::uint32_t self) {
    std::int32_t start = 0;
    std::int32_t last = -1;
    for (std::int32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t row = word_at(m_, self + kGRows + 4 * u(i));
        if (row == 0) {
            continue;
        }
        if (int_at(m_, row + 8) == -2) {
            gaps_sort(self, start, last);
            start = i + 1;
        } else {
            last = i;
        }
    }
    gaps_sort(self, start, last);
}

// 0x00484c10: the checklist is rebuilt when the level asked for is not the level shown: a scroll list of the level's gaps (a header for each group that has a gap, a row for each
// gap that is open, the ones found with their number), the name of the level and "n of m gaps", and the level's video window.
void OptionsScreen::gaps_update(std::uint32_t self) {
    if (static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4d8()) != static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc())) {
        if (options_gaps_gaps_at(m_, self).field_348() != 0) {
            delete_object(options_gaps_gaps_at(m_, self).field_348());
            options_gaps_gaps_at(m_, self).set_field_348(0);
        }
        for (std::uint32_t i = 0; i < 0x60; ++i) {
            if (word_at(m_, self + kGRows + 4 * i) != 0) {
                delete_object(word_at(m_, self + kGRows + 4 * i));
                set_word_at(m_, self + kGRows + 4 * i, 0);
            }
        }
        std::uint32_t memory = operator_new(0x350);
        std::uint32_t list = 0;
        if (memory != 0) {
            list = menu_.scroll_construct(memory, 0x3c, 0x4a, 0x200, 0x78, 0, 3, 0xfffffffeu, 1);
        }
        options_gaps_gaps_at(m_, self).set_field_348(static_cast<Word>(list));
        set_byte_at(m_, list + 0x1b9, 0);
        const std::uint32_t saved_level = global_word(m_, kCurrentLevel);
        set_global_word(m_, kCurrentLevel, options_gaps_gaps_at(m_, self).field_4dc());
        set_global_word(m_, kGapTablePointer, kGapTable);
        std::uint32_t cursor = self + kGRows;
        std::uint32_t rows = 0;
        std::uint32_t found = 0;
        std::uint32_t shown = 0;
        if (global_half(m_, kGapTable) != 0) {
            std::uint32_t entry = kGapTable;
            do {
                if (half_at(m_, entry + 4) == 0xfffe) {
                    // a header: shown when the entry after it is open
                    if (engine_value(0x00414770, {entry + kGapStride}) != 0) {
                        memory = operator_new(0x54);
                        std::uint32_t text = 0;
                        if (memory != 0) {
                            text = menu_.text_construct(memory, 0xfffffffeu, entry + 8, options_gaps_gaps_at(m_, self).field_4d0());
                        }
                        set_word_at(m_, cursor, text);
                        cursor += 4;
                        ++rows;
                    }
                } else if (engine_value(0x00414770, {entry}) != 0) {
                    check(rows < 0x60, kMsgTooManyGaps);
                    const bool got = engine_value(0x00414b10, {entry}) != 0;
                    std::string name = m_.cstring(entry + 8, 0x100);
                    std::string number;
                    if (got) {
                        number = retail_format(m_.cstring(kFormatNumber), {}, {short_at(m_, entry + 6)});
                        ++found;
                    } else {
                        number = m_.cstring(kTextUnknown);
                    }
                    ++shown;
                    if (name.size() >= 0x20) {
                        name.resize(0x1f);
                    }
                    const std::uint32_t name_at = scratch_text(0, name);
                    const std::uint32_t number_at = scratch_text(0x80, number);
                    if (got) {
                        memory = operator_new(0x100);
                        std::uint32_t row = 0;
                        if (memory != 0) {
                            row = menu_.row_construct(memory, u(short_at(m_, entry + 6)), name_at, options_gaps_gaps_at(m_, self).field_4cc(), 2, 0, 0);
                        }
                        set_word_at(m_, cursor, row);
                        vcall_item("item_v9", row, 9, {number_at, 1, 0x168, options_gaps_gaps_at(m_, self).field_4cc()});
                    } else {
                        gaps_strip_brackets(name_at);
                        memory = operator_new(0x100);
                        std::uint32_t row = 0;
                        if (memory != 0) {
                            row = menu_.row_construct(memory, u(short_at(m_, entry + 6)), name_at, options_gaps_gaps_at(m_, self).field_4d4(), 2, 0, 0);
                        }
                        set_word_at(m_, cursor, row);
                        vcall_item("item_v9", row, 9, {number_at, 1, 0x168, options_gaps_gaps_at(m_, self).field_4d4()});
                    }
                    cursor += 4;
                    ++rows;
                }
                entry += kGapStride;
            } while (half_at(m_, entry) != 0);
        }
        gaps_sort_all(self);
        for (std::uint32_t i = 0; i < 0x60; ++i) {
            if (word_at(m_, self + kGRows + 4 * i) != 0) {
                vcall_list("list_v6", options_gaps_gaps_at(m_, self).field_348(), 6, {word_at(m_, self + kGRows + 4 * i)});
            }
        }
        set_global_word(m_, kCurrentLevel, saved_level);
        vcall_list("list_v2", options_gaps_gaps_at(m_, self).field_348(), 2);
        const std::uint32_t row = kLevelTable + kLevelStride * options_gaps_gaps_at(m_, self).field_4dc();
        const std::uint32_t title = scratch_line(retail_format(m_.cstring(kFormatString), {m_.cstring(row, 0x40)}, {}));
        f_.ellipsize(self + kGFontA, title, 0x100);
        w_.text_set_string(options_gaps_gaps_at(m_, self).field_4e0(), title);
        const std::uint32_t counts = scratch_line(retail_format(m_.cstring(global_word(m_, kTextGapsOf), 0x40), {}, {static_cast<std::int32_t>(found), static_cast<std::int32_t>(shown)}));
        w_.text_set_string(options_gaps_gaps_at(m_, self).field_4e4(), counts);
        engine(0x0045b890, {options_gaps_gaps_at(m_, self).field_4ec(), options_gaps_gaps_at(m_, self).field_4dc()});
        options_gaps_gaps_at(m_, self).set_field_4d8(static_cast<Word>(options_gaps_gaps_at(m_, self).field_4dc()));
    }
    w_.window_update(self);
}

// 0x004850f0: left and right choose the level (skipping the locked ones and the later ones), up and down move the list; the arrows blink; then the window.
void OptionsScreen::gaps_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    std::int32_t direction = 0;
    const bool any = global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0 || global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0 ||
                     global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp + kPadStride) != 0 || global_byte(m_, kPadUp2) != 0 || global_byte(m_, kPadUp2 + kPadStride) != 0;
    if (any) {
        sfx(0x11f);
        if (global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0) {
            set_global_byte(m_, kPadLeft, 0);
            set_global_byte(m_, kPadLeft + kPadStride, 0);
            direction = -1;
        } else if (global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0) {
            set_global_byte(m_, kPadRight, 0);
            set_global_byte(m_, kPadRight + kPadStride, 0);
            direction = 1;
        }
    }
    for (;;) {
        const std::int32_t level = static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc()) + direction;
        options_gaps_gaps_at(m_, self).set_field_4dc(static_cast<Word>(u(level)));
        if (level >= 0xe) {
            options_gaps_gaps_at(m_, self).set_field_4dc(0);
        } else if (level < 0) {
            options_gaps_gaps_at(m_, self).set_field_4dc(0xd);
        }
        if (gaps_locked(static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc()))) {
            continue;
        }
        const std::int32_t now = static_cast<std::int32_t>(options_gaps_gaps_at(m_, self).field_4dc());
        if (now == 0xd || now >= 0xa) {
            continue;
        }
        break;
    }
    if (global_byte(m_, kPadReleased) != 0) {
        set_global_byte(m_, kPadReleased, 0);
        options_gaps_gaps_at(m_, self).set_field_4d8(0xffffffffu);
    }
    const std::uint32_t list = options_gaps_gaps_at(m_, self).field_348();
    if (list != 0) {
        vcall_list("list_v3", list, 3);
        const std::uint32_t edge = engine_value(0x0046f650);
        vcall_list("list_v5", list, 5, {edge});
        vcall_list("list_v4", list, 4);
    }
    engine(0x004e8740, {0});
    if (global_word(m_, kSimClock) % 0x3c > 0x1e) {
        vcall_item("sprite_draw", options_gaps_gaps_at(m_, self).field_4fc(), 3, {0x1e, 0x78, 0});
        vcall_item("sprite_draw", options_gaps_gaps_at(m_, self).field_500(), 3, {0x1ce, 0x78, 0});
    }
    w_.window_draw(self, x, y);
}

}  // namespace opentony::runtime::frontend
