#include "auto_views.hpp"
#include "level_editor.hpp"

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor's interface object: +0 and +4 the position of the panel and the one it moves to, +8 how far it slides, +0xc the height of a text line, +0x10 the layout,
// +0x14 the set's line, +0x3c the set's name, +0x64..+0x70 where they are drawn, +0x74 the piece's line, +0x9c its name, +0x114..+0x128 where they are drawn,
// +0x134 .. +0x160 the panel elements, +0x164 and +0x168 which of the piece library's two names (indices) it shows, +0x16c the category, +0x170 the piece,
// +0x174 the number of the pieces of the library, +0x178 the number of categories, +0x17c the first piece of the category and +0x180 how many it has, +0x184 the
// font (an object with a vtable: +0x188 and +0x18c its shadow), +0x2d0 a second font, +0x41c.. the budget bars and +0x428, +0x42c the limits.

namespace {
constexpr std::uint32_t kFontObject = 0x184;
constexpr std::uint32_t kFmtPiece = 0x0052a6a0;       // "Piece %d of %d"
constexpr std::uint32_t kFmtSet = 0x0052a6d4;         // "Set %d of %d"
constexpr std::uint32_t kFmtName = 0x005256dc;        // "%s"
constexpr std::uint32_t kLogLimits = 0x0052a710;
constexpr std::uint32_t kPieceNameFont = 0x0052a688;  // "s2park1.fnt"
constexpr std::uint32_t kMessages[5] = {0x0052e8f8, 0x0052e8fc, 0x0052e900, 0x0052e904, 0x0052e908};
}  // namespace

// 0x00436520: the interface's destructor: its font is let go, the elements it made are deleted and the two Font objects of its own are taken apart.
void LevelEditor::ui_destruct(std::uint32_t self) {
    engine(0x0044b0a0, {kPieceNameFont}, m_.cstring(kPieceNameFont, 0x20));
    if (level_editor_ui_ui_at(m_, self).field_134() != 0) {
        (void)virtual_call(level_editor_ui_ui_at(m_, self).field_134(), 0, {1});
    }
    if (level_editor_ui_ui_at(m_, self).field_13c() != 0) {
        (void)virtual_call(level_editor_ui_ui_at(m_, self).field_13c(), 0, {1});
    }
    for (std::uint32_t k = 0; k < 7; ++k) {
        if (word_at(m_, self + 0x144 + 4 * k) != 0) {
            (void)virtual_call(word_at(m_, self + 0x144 + 4 * k), 0, {1});
        }
    }
    if (level_editor_ui_ui_at(m_, self).field_160() != 0) {
        (void)virtual_call(level_editor_ui_ui_at(m_, self).field_160(), 0, {1});
    }
    engine(0x0044a060, {self + 0x2d0});
    engine(0x0044a060, {self + kFontObject});
}

// 0x004365e0: draw the interface: the elements, the lines of text, the polygon that darkens the strip behind them, the two bars (the pieces, the nodes and the zone room
// left; the fullest of the three is drawn) and the message with its frame.
void LevelEditor::ui_draw(std::uint32_t self) {
    const std::uint32_t font = self + kFontObject;
    (void)virtual_call(level_editor_ui_ui_at(m_, self).field_134(), 3, {level_editor_ui_ui_at(m_, self).field_138(), level_editor_ui_ui_at(m_, self).field_c() + word_at(m_, self), 0});
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_64(), level_editor_ui_ui_at(m_, self).field_6c() + word_at(m_, self), self + 0x14, 0}, m_.cstring(self + 0x14, 0x100));
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_68(), level_editor_ui_ui_at(m_, self).field_70() + word_at(m_, self), self + 0x3c, 0}, m_.cstring(self + 0x3c, 0x100));
    (void)virtual_call(level_editor_ui_ui_at(m_, self).field_13c(), 3, {level_editor_ui_ui_at(m_, self).field_140(), level_editor_ui_ui_at(m_, self).field_c() + word_at(m_, self), 0});
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_114(), level_editor_ui_ui_at(m_, self).field_124() + word_at(m_, self), self + 0x74, 0}, m_.cstring(self + 0x74, 0x100));
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_118(), level_editor_ui_ui_at(m_, self).field_128() + word_at(m_, self), self + 0x9c, 0}, m_.cstring(self + 0x9c, 0x100));
    std::uint32_t at = global_word(m_, kLineCursor);
    if (at > global_word(m_, kLineEnd)) {
        return;
    }
    set_global_word(m_, kLineCursor, at + 0x1c);
    set_byte_at(m_, at + 7, 0x28);
    set_byte_at(m_, at + 4, 0x3c);
    set_byte_at(m_, at + 5, 0x3c);
    set_byte_at(m_, at + 6, 0x3c);
    set_half_at(m_, at + 8, 0);
    set_half_at(m_, at + 0xa, half_at(m_, self));
    set_half_at(m_, at + 0xc, 0x200);
    set_half_at(m_, at + 0xe, half_at(m_, self));
    set_half_at(m_, at + 0x10, 0);
    set_half_at(m_, at + 0x14, 0x200);
    set_half_at(m_, at + 0x12, half_at(m_, self) + half_at(m_, self + 8));
    set_byte_at(m_, at + 7, 0x2a);
    set_half_at(m_, at + 0x16, half_at(m_, self) + half_at(m_, self + 8));
    at = global_word(m_, kLineCursor);
    if (at > global_word(m_, kLineEnd)) {
        return;
    }
    set_global_word(m_, kLineCursor, at + 0x10);
    engine(0x004e8940, {at, 0, 1, 0x140, 0});
    set_word_at(m_, at + 0xc, 0xbf800000);
    const std::uint32_t head = word_at(m_, global_word(m_, kRenderer) + 0x84);
    set_word_at(m_, at, word_at(m_, head));
    set_word_at(m_, word_at(m_, global_word(m_, kRenderer) + 0x84), at);
    for (std::uint32_t k = 0; k < 6; ++k) {
        (void)virtual_call(word_at(m_, self + 0x144 + 4 * k), 2, {0, 0});
    }
    (void)virtual_call(level_editor_ui_ui_at(m_, self).field_160(), 2, {0, 0});
    const std::int32_t pieces_left = static_cast<std::int32_t>(engine_value(0x00435f60));
    level_editor_ui_ui_at(m_, self).set_field_188(2);
    level_editor_ui_ui_at(m_, self).set_field_18c(2);
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_41c() + 6, 0x36, 0x0052a698, 0}, m_.cstring(0x0052a698, 0x20));
    level_editor_ui_ui_at(m_, self).set_field_18c(0);
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_41c() + 6, 0xd7, 0x0052a694, 0}, m_.cstring(0x0052a694, 0x20));
    level_editor_ui_ui_at(m_, self).set_field_188(0);
    const std::int32_t piece_limit = level_editor_ui_ui_at(m_, self).field_428();
    std::int32_t fill = ((piece_limit - pieces_left) * 0x9f) / piece_limit;
    const std::int32_t nodes_left = static_cast<std::int32_t>(engine_value(0x00440170));
    const std::int32_t node_fill = ((0x8c0 - nodes_left) * 0x9f) / 0x8c0;
    const std::int32_t zone_limit = level_editor_ui_ui_at(m_, self).field_42c();
    const std::int32_t zone_left = static_cast<std::int32_t>(engine_value(0x0043f710));
    const std::int32_t zone_fill = ((zone_limit - zone_left) * 0x9f) / zone_limit;
    if (fill <= node_fill) {
        fill = node_fill;
    }
    if (zone_fill > fill) {
        fill = zone_fill;
    }
    if (fill > 0x9c && fill < 0x9f) {
        fill = 0x9c;
    }
    const std::int32_t top = 0xd6 - fill;
    std::uint32_t red;
    std::uint32_t green;
    if (fill < 0x8f) {
        if (fill < 0x77) {
            red = 0;
            green = 0xff;
        } else {
            red = 0xc0;
            green = 0xc0;
        }
    } else {
        red = 0xff;
        green = 0;
        if (global_byte(m_, kBlink) != 0) {
            if (level_editor_ui_ui_at(m_, self).field_424() == 0) {
                level_editor_ui_ui_at(m_, self).set_field_424(0xc);
            }
            const std::int32_t count = static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_424()) - 1;
            level_editor_ui_ui_at(m_, self).set_field_424(static_cast<Word>(static_cast<std::uint32_t>(count)));
            if (count < 3) {
                red = 0;
            }
        }
    }
    engine(0x00456790, {level_editor_ui_ui_at(m_, self).field_41c(), 0x37, 6, static_cast<std::uint32_t>(0x9f - fill), 0, 0, 0});
    engine(0x00456790, {level_editor_ui_ui_at(m_, self).field_41c(), static_cast<std::uint32_t>(top), 6, static_cast<std::uint32_t>(fill), red, green, 0});
    const std::uint32_t message_x = level_editor_ui_ui_at(m_, self).field_444();
    const std::uint32_t saved_shadow = level_editor_ui_ui_at(m_, self).field_188();
    if (message_x != 0x208) {
        level_editor_ui_ui_at(m_, self).set_field_188(1);
        level_editor_ui_ui_at(m_, self).set_field_444(0x100);
    }
    (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_444(), 0xb2, kMessageBuffer, 0}, m_.cstring(kMessageBuffer, 0x80));
    if (level_editor_ui_ui_at(m_, self).field_430() == 3) {
        const std::uint32_t extent = engine_value(0x0044a540, {font, kMessageBuffer}, m_.cstring(kMessageBuffer, 0x80));
        (void)virtual_call(font, 1, {level_editor_ui_ui_at(m_, self).field_444(), extent + 0xb2, kMessageBuffer2, 0}, m_.cstring(kMessageBuffer2, 0x80));
    }
    level_editor_ui_ui_at(m_, self).set_field_444(static_cast<Word>(message_x));
    engine(0x00456600, {message_x - 2, 0xb0, level_editor_ui_ui_at(m_, self).field_44c() + 6, level_editor_ui_ui_at(m_, self).field_450() + 6, 0x50, 0x50, 0x50});
    level_editor_ui_ui_at(m_, self).set_field_188(static_cast<Word>(saved_shadow));
}

// 0x004369d0: the interface moves: the panel and the bar slide towards where they are going, the elements step, the message slides in and out, and a new message is
// formatted when it is time.
void LevelEditor::ui_update(std::uint32_t self) {
    const std::int32_t gap = static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_4()) - int_at(m_, self);
    if (gap > -2 && gap < 2) {
        set_word_at(m_, self, level_editor_ui_ui_at(m_, self).field_4());
    }
    std::int32_t step = gap >> 2;
    if (step == 0) {
        step = gap >> 1;
    }
    set_word_at(m_, self, word_at(m_, self) + static_cast<std::uint32_t>(step));
    const std::int32_t bar = static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_420()) - static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_41c());
    if (bar >= -1 && bar <= 1) {
        level_editor_ui_ui_at(m_, self).set_field_41c(static_cast<Word>(level_editor_ui_ui_at(m_, self).field_420()));
    } else {
        level_editor_ui_ui_at(m_, self).set_field_41c(static_cast<Word>(static_cast<std::uint32_t>((bar >> 1) + static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_41c()))));
    }
    for (std::uint32_t k = 0; k < 7; ++k) {
        (void)virtual_call(word_at(m_, self + 0x144 + 4 * k), 1);
    }
    (void)virtual_call(level_editor_ui_ui_at(m_, self).field_160(), 1);
    const std::int32_t target = static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_448());
    const std::int32_t at = static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_444());
    const std::int32_t slide = target - at;
    level_editor_ui_ui_at(m_, self).set_field_444(static_cast<Word>(static_cast<std::uint32_t>((slide >> 1) + at)));
    if (slide >= -1 && slide <= 1) {
        level_editor_ui_ui_at(m_, self).set_field_444(static_cast<Word>(static_cast<std::uint32_t>(target)));
    }
    const std::uint32_t state = level_editor_ui_ui_at(m_, self).field_440();
    if (state == 0) {
        if (level_editor_ui_ui_at(m_, self).field_438() == 0 || level_editor_ui_ui_at(m_, self).field_434() != level_editor_ui_ui_at(m_, self).field_430() || level_editor_ui_ui_at(m_, self).field_43c() != level_editor_ui_ui_at(m_, self).field_454()) {
            level_editor_ui_ui_at(m_, self).set_field_440(1);
            level_editor_ui_ui_at(m_, self).set_field_448(0x208);
        }
        return;
    }
    if (state == 1) {
        if (level_editor_ui_ui_at(m_, self).field_444() == level_editor_ui_ui_at(m_, self).field_448()) {
            level_editor_ui_ui_at(m_, self).set_field_440(2);
        }
        return;
    }
    if (state != 2 || level_editor_ui_ui_at(m_, self).field_438() == 0) {
        return;
    }
    level_editor_ui_ui_at(m_, self).set_field_430(static_cast<Word>(level_editor_ui_ui_at(m_, self).field_434()));
    level_editor_ui_ui_at(m_, self).set_field_454(static_cast<Word>(level_editor_ui_ui_at(m_, self).field_43c()));
    const std::uint32_t kind = level_editor_ui_ui_at(m_, self).field_434();
    if (kind == 3) {
        copy_string(kMessageBuffer, word_at(m_, kMessages[3]));
        copy_string(kMessageBuffer2, global_word(m_, kItems) + level_editor_ui_ui_at(m_, self).field_454() * 0x2c + 0xd);
    } else if (kind <= 4) {
        sprintf_to(kMessageBuffer, word_at(m_, kMessages[kind]), 0);
    }
    const std::uint32_t font = self + kFontObject;
    level_editor_ui_ui_at(m_, self).set_field_44c(static_cast<Word>(engine_value(0x0044a4d0, {font, kMessageBuffer}, m_.cstring(kMessageBuffer, 0x80))));
    level_editor_ui_ui_at(m_, self).set_field_450(static_cast<Word>(engine_value(0x0044a540, {font, kMessageBuffer}, m_.cstring(kMessageBuffer, 0x80))));
    if (level_editor_ui_ui_at(m_, self).field_430() == 3) {
        const std::uint32_t extra = engine_value(0x0044a540, {font, kMessageBuffer2}, m_.cstring(kMessageBuffer2, 0x80));
        level_editor_ui_ui_at(m_, self).set_field_450(static_cast<Word>(level_editor_ui_ui_at(m_, self).field_450() + extra));
        if (static_cast<std::int32_t>(level_editor_ui_ui_at(m_, self).field_44c()) < static_cast<std::int32_t>(engine_value(0x0044a4d0, {font, kMessageBuffer2}, m_.cstring(kMessageBuffer2, 0x80)))) {
            level_editor_ui_ui_at(m_, self).set_field_44c(static_cast<Word>(engine_value(0x0044a4d0, {font, kMessageBuffer2}, m_.cstring(kMessageBuffer2, 0x80))));
        }
    }
    level_editor_ui_ui_at(m_, self).set_field_448(static_cast<Word>(0x100 - (level_editor_ui_ui_at(m_, self).field_44c() >> 1)));
    level_editor_ui_ui_at(m_, self).set_field_440(0);
}

// ---- the piece library's selection --------------------------------------------------------------------------------------------

// 0x00436e00: can the piece be chosen (a piece with bit 7 of its flags is not, in the library of kind 4)
bool LevelEditor::piece_selectable(std::uint32_t self, std::uint32_t index) {
    if (word_at(m_, self + 0x168) == 4 && (byte_at(m_, kPieceFlags + index * kPieceBytes) & 0x80) != 0) {
        return false;
    }
    return true;
}

// 0x00436c60: choose a piece (the first of the category if the one asked for cannot be chosen) and format its two lines
void LevelEditor::select_piece(std::uint32_t self, std::uint32_t index) {
    if (!piece_selectable(self, index)) {
        s_.check(piece_selectable(self, word_at(m_, self + 0x17c)), kMsgNoPiece);
        index = word_at(m_, self + 0x17c);
    }
    set_word_at(m_, self + 0x170, index);
    set_byte_at(m_, kCategoryNames + 0x21 + word_at(m_, self + 0x16c) * kCategoryBytes, byte_at(m_, kPieceNames + word_at(m_, self + 0x164) + index * kPieceBytes));
    sprintf_list(self + 0x74, kFmtPiece, {(word_at(m_, self + 0x170) - word_at(m_, self + 0x17c)) + 1, word_at(m_, self + 0x180)});
    sprintf_list(self + 0x9c, kFmtName, {kPieceNames + 4 + word_at(m_, self + 0x170) * kPieceBytes});
    const std::uint32_t font = self + kFontObject;
    const std::uint32_t line = self + 0x74;
    const std::uint32_t width = engine_value(0x0044a4d0, {font, line}, m_.cstring(line, 0x100));
    set_word_at(m_, self + 0x114, (word_at(m_, self + 0x140) - width) - 6);
    const std::uint32_t name_width = engine_value(0x0044a4d0, {font, self + 0x9c}, m_.cstring(self + 0x9c, 0x100));
    const std::uint32_t height_base = word_at(m_, self + 0xc);
    set_word_at(m_, self + 0x118, (word_at(m_, self + 0x140) - name_width) - 6);
    const std::uint32_t extent = engine_value(0x0044a540, {font, line}, m_.cstring(line, 0x100));
    set_word_at(m_, self + 0x128, height_base + 1);
    set_word_at(m_, self + 0x124, (height_base - extent) - 1);
}

// 0x00436da0: the next or the previous piece of the category that can be chosen
void LevelEditor::step_piece(std::uint32_t self, std::int32_t delta) {
    std::int32_t index = int_at(m_, self + 0x170);
    for (;;) {
        const std::int32_t first = int_at(m_, self + 0x17c);
        index += delta;
        const std::int32_t offset = index - first;
        if (offset < 0) {
            index = int_at(m_, self + 0x180) + first - 1;
        }
        if (offset >= int_at(m_, self + 0x180)) {
            index = first;
        }
        if (piece_selectable(self, static_cast<std::uint32_t>(index))) {
            break;
        }
        if (delta == 0) {
            delta = -1;
        }
    }
    select_piece(self, static_cast<std::uint32_t>(index));
}

// 0x00436e30: choose a category: the piece last used in it is found among the library's pieces (and where the category begins and how many pieces it has), and the lines are
// formatted
void LevelEditor::select_category(std::uint32_t self, std::uint32_t category) {
    std::int32_t found = -1;
    const std::int32_t count = int_at(m_, self + 0x174) + 1;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t piece_byte = kPieceNames + static_cast<std::uint32_t>(i) * kPieceBytes;
        if (byte_at(m_, word_at(m_, self + 0x164) + piece_byte) == byte_at(m_, kCategoryNames + 0x21 + category * kCategoryBytes)) {
            found = i;
        }
        if (byte_at(m_, piece_byte) == byte_at(m_, kCategoryNames + category * kCategoryBytes)) {
            set_word_at(m_, self + 0x17c, static_cast<std::uint32_t>(i));
        }
        if (byte_at(m_, piece_byte) == byte_at(m_, kCategoryNames + 0x22 + category * kCategoryBytes)) {
            set_word_at(m_, self + 0x180, static_cast<std::uint32_t>(i) - word_at(m_, self + 0x17c));
            break;
        }
    }
    s_.check(found != -1, kMsgLastPiece);
    set_word_at(m_, self + 0x16c, category);
    select_piece(self, static_cast<std::uint32_t>(found));
    sprintf_list(self + 0x14, kFmtSet, {word_at(m_, self + 0x16c) + 1, word_at(m_, self + 0x178)});
    sprintf_list(self + 0x3c, kFmtName, {kCategoryNames + 1 + word_at(m_, self + 0x16c) * kCategoryBytes});
    const std::uint32_t height_base = word_at(m_, self + 0xc);
    const std::uint32_t left = word_at(m_, self + 0x138) + 6;
    set_word_at(m_, self + 0x64, left);
    set_word_at(m_, self + 0x68, left);
    const std::uint32_t extent = engine_value(0x0044a540, {self + kFontObject, self + 0x14}, m_.cstring(self + 0x14, 0x100));
    set_word_at(m_, self + 0x70, height_base + 1);
    set_word_at(m_, self + 0x6c, (height_base - extent) - 1);
}

// 0x00436fa0: the next or previous category (wrapping)
void LevelEditor::step_category(std::uint32_t self, std::int32_t delta) {
    std::int32_t next = int_at(m_, self + 0x16c) + delta;
    if (next < 0) {
        next = int_at(m_, self + 0x178) - 1;
    }
    if (next >= int_at(m_, self + 0x178)) {
        next = 0;
    }
    select_category(self, static_cast<std::uint32_t>(next));
}

// 0x00436fd0: the layout of the interface: where the panel and its elements sit for each of four layouts
void LevelEditor::ui_layout(std::uint32_t self, std::uint32_t layout) {
    if (layout == level_editor_ui_ui_at(m_, self).field_10()) {
        return;
    }
    level_editor_ui_ui_at(m_, self).set_field_10(static_cast<Word>(layout));
    auto put = [&](std::uint32_t first, std::uint32_t count, std::uint32_t value) {
        for (std::uint32_t k = 0; k < count; ++k) {
            set_word_at(m_, word_at(m_, self + first + 4 * k) + 0xc, value);
        }
    };
    const std::uint32_t left = global_word(m_, kHudLeft) + 0xb;
    switch (layout) {
    case 0:
        level_editor_ui_ui_at(m_, self).set_field_4(static_cast<Word>(static_cast<std::uint32_t>(-int_at(m_, self + 8))));
        put(0x144, 2, left);
        put(0x14c, 2, 0xe0);
        put(0x154, 2, 0xffffffeb);
        put(0x15c, 1, 0xe0);
        put(0x160, 1, 0xffffffeb);
        level_editor_ui_ui_at(m_, self).set_field_420(static_cast<Word>(global_word(m_, kHudTop) + 0x28));
        break;
    case 1:
        level_editor_ui_ui_at(m_, self).set_field_4(3);
        put(0x144, 4, 0xe0);
        put(0x154, 2, 0xffffffeb);
        put(0x15c, 1, left);
        put(0x160, 1, 0xffffffeb);
        level_editor_ui_ui_at(m_, self).set_field_420(0x1e5);
        break;
    case 2:
        level_editor_ui_ui_at(m_, self).set_field_4(static_cast<Word>(static_cast<std::uint32_t>(-int_at(m_, self + 8))));
        put(0x144, 4, 0xe0);
        put(0x154, 2, 0x19);
        put(0x15c, 1, left);
        put(0x160, 1, 0x2a);
        level_editor_ui_ui_at(m_, self).set_field_420(static_cast<Word>(global_word(m_, kHudTop) + 0x28));
        break;
    case 3:
        level_editor_ui_ui_at(m_, self).set_field_4(static_cast<Word>(static_cast<std::uint32_t>(-int_at(m_, self + 8))));
        put(0x144, 4, left);
        put(0x154, 2, 0xffffffeb);
        put(0x15c, 1, left);
        put(0x160, 1, 0xffffffeb);
        level_editor_ui_ui_at(m_, self).set_field_420(static_cast<Word>(global_word(m_, kHudTop) + 0x28));
        break;
    default:
        break;
    }
}

// 0x004371d0: the character that names the piece in hand
std::uint32_t LevelEditor::piece_char(std::uint32_t self) { return byte_at(m_, kPieceNames + word_at(m_, self + 0x164) + word_at(m_, self + 0x170) * kPieceBytes); }

// 0x004371f0: which of the library's names the interface shows
void LevelEditor::set_library(std::uint32_t self, std::uint32_t first, std::uint32_t second) {
    if (first != 0xffffffff) {
        set_word_at(m_, self + 0x164, first);
    }
    if (int_at(m_, self + 0x168) != -1) {
        set_word_at(m_, self + 0x168, second);
    }
}

// 0x00437220: the limits of the park (how many pieces and how much zone room the version and theme allow)
void LevelEditor::set_limits(std::uint32_t self) {
    log(kLogLimits);
    set_word_at(m_, self + 0x428, word_at(m_, kVersionLimits + global_word(m_, kVersion) * 4));
    set_word_at(m_, self + 0x42c, 4000 - word_at(m_, kThemeLimits + (global_word(m_, kVersion) + global_word(m_, kTheme) * 5) * 4));
}

}  // namespace opentony::runtime::leveleditor
