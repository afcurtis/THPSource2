#include "auto_views.hpp"
#include "frontend_window.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

}  // namespace

// ---- ScreenElement ---------------------------------------------------------------------------------------

std::uint32_t WindowBase::element_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    frontend_window_element_at(m_, self).set_field_0(static_cast<Word>(kVtElement));
    set_word_at(m_, self + kElemTargetX, u(x));
    set_word_at(m_, self + kElemTargetY, u(y));
    set_word_at(m_, self + kElemX, start_x == -1 ? u(x) : u(start_x));
    set_word_at(m_, self + kElemY, start_y == -1 ? u(y) : u(start_y));
    set_word_at(m_, self + kElemArg, 0);
    set_word_at(m_, self + kElemHidden, 0);
    set_word_at(m_, self + kElemDelay, 0);
    set_half_at(m_, self + kElemPad, 0);
    return self;
}

void WindowBase::element_destroy(std::uint32_t self) { set_word_at(m_, self, kVtRoot); }

std::uint32_t WindowBase::element_delete(std::uint32_t self, std::uint32_t flag) {
    element_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void WindowBase::element_update(std::uint32_t self) {
    virtual_call(kElementHook, 7, self);
    const std::int32_t delay = int_at(m_, self + kElemDelay);
    if (delay > 0) {
        set_word_at(m_, self + kElemDelay, u(delay - 1));
        return;
    }
    // slide each axis a quarter of the way (half when that rounds to nothing), snapping within one unit
    auto slide = [&](std::uint32_t target_at, std::uint32_t current_at) {
        const std::int32_t target = int_at(m_, self + target_at);
        const std::int32_t current = int_at(m_, self + current_at);
        const std::int32_t d = target - current;
        if (d < 2 && d > -2) {
            set_word_at(m_, self + current_at, u(target));
        } else {
            std::int32_t step = d >> 2;
            if (step == 0) {
                step = d >> 1;
            }
            set_word_at(m_, self + current_at, u(add(current, step)));
        }
    };
    slide(kElemTargetX, kElemX);
    slide(kElemTargetY, kElemY);
    const bool arrived = int_at(m_, self + kElemY) == int_at(m_, self + kElemTargetY) && int_at(m_, self + kElemX) == int_at(m_, self + kElemTargetX);
    set_byte_at(m_, self + kElemArrived, arrived ? 1 : 0);
}

void WindowBase::element_snap_x(std::uint32_t self) { set_word_at(m_, self + kElemX, word_at(m_, self + kElemTargetX)); }
void WindowBase::element_snap_y(std::uint32_t self) { set_word_at(m_, self + kElemY, word_at(m_, self + kElemTargetY)); }
std::uint32_t WindowBase::element_x(std::uint32_t self) const { return word_at(m_, self + kElemX); }
std::uint32_t WindowBase::element_y(std::uint32_t self) const { return word_at(m_, self + kElemY); }

// ---- Window ----------------------------------------------------------------------------------------------

std::uint32_t WindowBase::window_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    element_construct(self, x, y, start_x, start_y);
    for (std::uint32_t i = 0; i < kWinSlots; ++i) {
        set_word_at(m_, self + kWinChildren + 4 * i, 0);
    }
    frontend_window_window_at(m_, self).set_field_0(static_cast<Word>(kVtWindow));
    set_word_at(m_, self + kWinCount, 0);
    return self;
}

void WindowBase::window_destroy(std::uint32_t self) {
    frontend_window_window_at(m_, self).set_field_0(static_cast<Word>(kVtWindow));
    for (std::uint32_t i = 0; i < kWinSlots; ++i) {
        const std::uint32_t child = word_at(m_, self + kWinChildren + 4 * i);
        if (child != 0) {
            virtual_call(kVirtualDelete, 0, child, {1});
        }
    }
    element_destroy(self);
}

std::uint32_t WindowBase::window_delete(std::uint32_t self, std::uint32_t flag) {
    window_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void WindowBase::window_add(std::uint32_t self, std::uint32_t child) {
    check(child != 0, kMsgNoElementToAdd);
    for (std::uint32_t i = 0; i < kWinSlots; ++i) {
        if (word_at(m_, self + kWinChildren + 4 * i) == 0) {
            set_word_at(m_, self + kWinChildren + 4 * i, child);
            set_word_at(m_, self + kWinCount, word_at(m_, self + kWinCount) + 1);
            return;
        }
    }
    check(false, kMsgTooManyElements);
}

void WindowBase::window_update(std::uint32_t self) {
    if (int_at(m_, self + kWinCount) > 0) {
        for (std::int32_t i = 0;;) {
            virtual_call(kElementUpdate, 1, word_at(m_, self + kWinChildren + 4 * static_cast<std::uint32_t>(i)));
            ++i;
            if (!(i < int_at(m_, self + kWinCount))) {
                break;
            }
        }
    }
    element_update(self);
}

void WindowBase::window_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (word_at(m_, self + kElemHidden) != 0 || int_at(m_, self + kWinCount) <= 0) {
        return;
    }
    for (std::int32_t i = 0;;) {
        virtual_call(kElementDraw, 2, word_at(m_, self + kWinChildren + 4 * static_cast<std::uint32_t>(i)),
                     {u(add(x, int_at(m_, self + kElemX))), u(add(y, int_at(m_, self + kElemY)))});
        ++i;
        if (!(i < int_at(m_, self + kWinCount))) {
            break;
        }
    }
}

void WindowBase::window_snap_x(std::uint32_t self) {
    element_snap_x(self);
    for (std::int32_t i = 0; i < int_at(m_, self + kWinCount); ++i) {
        virtual_call(kElementSnapX, 5, word_at(m_, self + kWinChildren + 4 * static_cast<std::uint32_t>(i)));
    }
}

void WindowBase::window_snap_y(std::uint32_t self) {
    element_snap_y(self);
    for (std::int32_t i = 0; i < int_at(m_, self + kWinCount); ++i) {
        virtual_call(kElementSnapY, 6, word_at(m_, self + kWinChildren + 4 * static_cast<std::uint32_t>(i)));
    }
}

// ---- TextScreenElement -----------------------------------------------------------------------------------

std::uint32_t WindowBase::copy_string(std::uint32_t text) {
    const std::string value = m_.cstring(text, 0x1000);
    s_.note(kAllocBuffer, {static_cast<std::uint32_t>(value.size() + 1), 0, 1, 0});
    const std::uint32_t at = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(at, bytes);
    return at;
}

std::uint32_t WindowBase::text_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font, std::int32_t x, std::int32_t y,
                                         std::int32_t start_x, std::int32_t start_y) {
    element_construct(self, x, y, start_x, start_y);
    frontend_window_text_at(m_, self).set_field_0(static_cast<Word>(kVtText));
    check(text != 0, kMsgNoStringPointer);
    check(font != 0, kMsgElementNoFont);
    set_word_at(m_, self + kTextString, copy_string(text));
    set_word_at(m_, self + kTextFont, font);
    return self;
}

void WindowBase::text_destroy(std::uint32_t self) {
    frontend_window_text_at(m_, self).set_field_0(static_cast<Word>(kVtText));
    check(word_at(m_, self + kTextString) != 0, kMsgNoString);
    s_.note(kMemDeleteService, {word_at(m_, self + kTextString)});
    element_destroy(self);
}

std::uint32_t WindowBase::text_delete(std::uint32_t self, std::uint32_t flag) {
    text_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void WindowBase::text_set_string(std::uint32_t self, std::uint32_t text) {
    check(word_at(m_, self + kTextString) != 0, kMsgNoString);
    s_.note(kMemDeleteService, {word_at(m_, self + kTextString)});
    check(text != 0, kMsgNoStringPointer);
    check(byte_at(m_, text) != 0, kMsgNoString);
    set_word_at(m_, self + kTextString, copy_string(text));
}

// The same with the string given as a value (a window passes the address of a one-letter local buffer).
void WindowBase::text_set_string_from(std::uint32_t self, const std::string& value) {
    check(word_at(m_, self + kTextString) != 0, kMsgNoString);
    s_.note(kMemDeleteService, {word_at(m_, self + kTextString)});
    check(!value.empty(), kMsgNoString);
    s_.note(kAllocBuffer, {static_cast<std::uint32_t>(value.size() + 1), 0, 1, 0});
    const std::uint32_t at = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(at, bytes);
    set_word_at(m_, self + kTextString, at);
}

void WindowBase::text_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (int_at(m_, self + kElemDelay) > 0 || word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    const std::uint32_t text = word_at(m_, self + kTextString);
    // an empty string is not drawn: the original compares the element's string with a global empty string
    if (m_.cstring(text, 0x1000) == m_.cstring(kEmptyString)) {
        return;
    }
    s_.note(kFontDrawService,
            {word_at(m_, self + kTextFont), u(add(x, int_at(m_, self + kElemX))), u(add(y, int_at(m_, self + kElemY))), text, word_at(m_, self + kElemArg)},
            m_.cstring(text, 0x1000));
}

std::int32_t WindowBase::text_width(std::uint32_t self) const {
    return f_.text_width(word_at(m_, self + kTextFont), word_at(m_, self + kTextString));
}

// ---- SpriteScreenElement ---------------------------------------------------------------------------------

std::uint32_t WindowBase::sprite_construct(std::uint32_t self, std::uint32_t file, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                           std::int32_t start_y, std::uint32_t b0, std::uint32_t b1) {
    element_construct(self, x, y, start_x, start_y);
    frontend_window_sprite_at(m_, self).set_field_0(static_cast<Word>(kVtSprite));
    check(file != 0, kMsgNoFilenamePointer);
    check(byte_at(m_, file) != 0, kMsgNoFilename);
    s_.note(kOperatorNew, {0x5c});
    const std::uint32_t memory = static_cast<std::uint32_t>(s_.value(kOperatorNew));
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteConstruct, {memory, file, 1, 0x40, 0x40}, m_.cstring(file));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    set_word_at(m_, self + kSpriteObject, sprite);
    check(sprite != 0, kMsgSpriteFailed);
    s_.note(kSpriteInit, {sprite});
    set_byte_at(m_, sprite + 0x0b, b1);
    set_byte_at(m_, sprite + 0x0a, b0);
    return self;
}

void WindowBase::sprite_destroy(std::uint32_t self) {
    frontend_window_sprite_at(m_, self).set_field_0(static_cast<Word>(kVtSprite));
    check(word_at(m_, self + kSpriteObject) != 0, kMsgNoSprite);
    if (word_at(m_, self + kSpriteObject) != 0) {
        virtual_call(kVirtualDelete, 0, word_at(m_, self + kSpriteObject), {1});
    }
    element_destroy(self);
}

std::uint32_t WindowBase::sprite_delete(std::uint32_t self, std::uint32_t flag) {
    sprite_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void WindowBase::sprite_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (int_at(m_, self + kElemDelay) > 0 || word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    s_.note(kSpriteDrawService,
            {word_at(m_, self + kSpriteObject), u(add(x, int_at(m_, self + kElemX))), u(add(y, int_at(m_, self + kElemY))), word_at(m_, self + kElemArg)});
}

std::uint32_t WindowBase::sprite_width(std::uint32_t self) {
    check(word_at(m_, self + kSpriteObject) != 0, kMsgNoSprite);
    s_.note(kSpriteWidth, {word_at(m_, self + kSpriteObject)});
    return static_cast<std::uint32_t>(s_.value(kSpriteWidth));
}

std::uint32_t WindowBase::sprite_height(std::uint32_t self) {
    check(word_at(m_, self + kSpriteObject) != 0, kMsgNoSprite);
    s_.note(kSpriteHeight, {word_at(m_, self + kSpriteObject)});
    return static_cast<std::uint32_t>(s_.value(kSpriteHeight));
}

// ---- TextWindow ------------------------------------------------------------------------------------------

void WindowBase::textwin_set_line_colour(std::uint32_t self, std::int32_t line, std::int32_t colour) {
    check(!(line < 0 || int_at(m_, self + kTextWinLines) <= line), kMsgBadLine);
    check(!(colour < 0 || colour > 3), kMsgBadLineColour);
    const std::uint32_t at = word_at(m_, self + kTextWinColours) + static_cast<std::uint32_t>(line);
    set_byte_at(m_, at, (byte_at(m_, at) & 0xfc) | (static_cast<std::uint32_t>(colour) & 0xff));
}

}  // namespace opentony::runtime::frontend
