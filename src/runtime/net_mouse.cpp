#include "auto_views.hpp"
#include "net_game.hpp"
#include "seam_addresses_generated.hpp"

#include <cmath>
#include <stdexcept>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

using namespace net;

constexpr std::uint32_t kAllocateObject = 0x00466b70;      // the class allocator (size)
constexpr std::uint32_t kFreeObject = 0x00466bb0;
constexpr std::uint32_t kFileExists = 0x00449460;
constexpr std::uint32_t kSpriteConstruct = 0x00457420;     // thiscall (name, 1, 0, 0)
constexpr std::uint32_t kMenuRowConstruct = 0x0046fc50;    // thiscall, six arguments
constexpr std::uint32_t kMenuRowDestruct = 0x0046fe80;
constexpr std::uint32_t kTextEditVtable = 0x00519994;
constexpr std::uint32_t kHitRect = seam::net::NetMouse_HitRect;             // (x, y, width, height): is the pointer inside the rectangle?
constexpr std::uint32_t kPendingButtons = 0x004e4190;
constexpr std::uint32_t kSetCursorPos = seam::hal::PCInput_SetCursorPos;   // (x, y): the pointer put at a point of the display
constexpr std::uint32_t kFirstKeyDown = 0x004e41d0;        // PCInput_FirstKeyDown(from): the first key at or above it that is down, 0 for none
constexpr std::uint32_t kKeyChar = 0x004e4230;             // PCInput_KeyChar(key): the character a key types
constexpr std::uint32_t kCtypeMultibyte = 0x0055dcd4;      // the C runtime's MB_CUR_MAX
constexpr std::uint32_t kCtypeTable = 0x0055dac8;          // the pointer to its character class table (two bytes a character)
constexpr std::uint32_t kMenuSetLine = 0x0044b770;         // thiscall (row)
constexpr std::uint32_t kFindFiles = seam::hal::WINMAIN_FindFiles;
constexpr std::uint32_t kLoadBuffer = 0x0046f490;          // FileIO_AllocLoadedBuffer: reported as the service "alloc"
constexpr float kFixedOne = 4096.0f;                       // the float at 0x005199bc

// the C runtime's _ftol: the low dword of the truncated value (a value that does not fit gives the integer indefinite, whose low dword is 0)
std::uint32_t ftol(double value) {
    if (!(std::fabs(value) < 9.2233720368547758e18)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
}

float as_float(std::uint32_t bits) {
    float value;
    static_assert(sizeof value == sizeof bits);
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

// 0x004dbd90: load the two cursor sprites (the arrow and the hand) that are not loaded yet
void NetGame::mouse_load_cursors() {
    set_global_byte(m_, kMouseActive, 1);
    set_global_byte(m_, kCursorShown, 0);
    const std::uint32_t names[2] = {0x0054a5f8, 0x0054a5e4};
    for (std::uint32_t i = 0; i < 2; ++i) {
        if (word_at(m_, kCursorSprites + 4 * i) != 0) {
            continue;
        }
        const std::uint32_t name = names[i];
        if (engine_value(kFileExists, {name}) != 0) {
            const std::uint32_t block = engine_value(kAllocateObject, {0x5c});
            std::uint32_t sprite = 0;
            if (block != 0) {
                sprite = engine_value(kSpriteConstruct, {block, name, 1, 0, 0});
            }
            set_word_at(m_, kCursorSprites + 4 * i, sprite);
            set_global_byte(m_, kCursorsLoaded, 1);
        } else {
            log_line(0x0054a5cc);                                   // "Error: %s not found."
        }
    }
    set_global_byte(m_, kMouseFlagA, 0);
    set_global_byte(m_, kMouseFlagB, 0);
}

// 0x004dbe60: delete the cursor sprites
void NetGame::mouse_free_cursors() {
    log_line(0x0054a608);                                           // "MENUPC_Close()"
    for (std::uint32_t slot = kCursorSprites; slot < kCursorSprites + 8; slot += 4) {
        const std::uint32_t sprite = word_at(m_, slot);
        if (sprite != 0) {
            virtual_call(sprite, 0, {1});
        }
        set_word_at(m_, slot, 0);
    }
    set_global_byte(m_, kCursorsLoaded, 0);
}

// 0x004dbea0: draw the arrow sprite at the pointer (when the cursors are loaded and shown)
void NetGame::mouse_draw_cursor() {
    if (global_byte(m_, kCursorsLoaded) == 0) {
        return;
    }
    const std::uint32_t sprite = global_word(m_, kCursorSprites);
    if (sprite == 0 || global_byte(m_, kCursorHidden) != 0) {
        return;
    }
    virtual_call(sprite, 3, {global_word(m_, kMouseX), global_word(m_, kMouseY), 0});
}

// 0x004dbee0: a new pointer position and button state from the input poll, scaled into the 320 x 240 menu space; a button going up sets the release flag
void NetGame::mouse_track(std::uint32_t x, std::uint32_t y, std::uint32_t buttons) {
    const std::uint32_t width = global_word(m_, kDisplayWidth);
    const std::uint32_t height = global_word(m_, kDisplayHeight);
    if (width == 0 || height == 0) {
        throw MemoryFault("the display size is zero (retail divides by it)");
    }
    const std::uint32_t scaled_x = (x << 9) / width;
    const std::uint32_t scaled_y = (y * 240u) / height;
    const std::uint32_t before = global_word(m_, kMouseButtons);
    set_global_byte(m_, kMouseReleased, 0);
    if (global_word(m_, kMouseX) == scaled_x && global_word(m_, kMouseY) == scaled_y && before == buttons) {
        set_global_byte(m_, kMouseChanged, 0);
    } else {
        set_global_byte(m_, kMouseChanged, 1);
        set_global_byte(m_, kMouseOverItem, 0);
        set_global_word(m_, kMouseHot, 0);
        if (before != 0 && buttons == 0) {
            set_global_byte(m_, kMouseReleased, 1);
            log_line(0x0054a618);                                   // "Mouse Pos: %d,%d"
        }
    }
    set_global_word(m_, kMouseY, scaled_y);
    set_global_word(m_, kMouseX, scaled_x);
    set_global_word(m_, kMouseButtons, buttons);
}

// 0x004dc020: switch the mouse on (load the cursors) or off
void NetGame::mouse_enable(std::uint32_t on) {
    if ((on & 0xff) != 0) {
        log_line(0x0054a62c);
        mouse_load_cursors();
        set_global_byte(m_, kCursorsLoaded, 1);
    } else {
        set_global_byte(m_, kCursorsLoaded, 0);
    }
}

// 0x004dc050: the replacement the key-name table gives for a name (the last row that matches, case blind), else the name itself
std::uint32_t NetGame::translate_key_name(std::uint32_t name) {
    std::uint32_t result = name;
    const std::string wanted = m_.cstring(name, 0x100);
    for (std::uint32_t row = kKeyTable + 4;; row += 12) {
        const std::uint32_t pattern = word_at(m_, row);
        if (crt::equal_nocase(wanted, m_.cstring(pattern, 0x100))) {
            result = word_at(m_, row + 4);
        }
        if (word_at(m_, row + 12) == 0) {
            break;
        }
    }
    return result;
}

// 0x004dc090: every '.' of the text becomes a space
void NetGame::dots_to_spaces(std::uint32_t text) {
    for (std::uint32_t at = text; byte_at(m_, at) != 0; ++at) {
        if (byte_at(m_, at) == '.') {
            set_byte_at(m_, at, ' ');
        }
    }
}

// 0x004dc0c0: the font of the lobby banner: left justified, large, a brown tint, and the heading drawn at (256, 222)
void NetGame::banner_style() {
    engine(0x004734b0, {0});
    engine(0x004734f0, {0xc0});
    engine(0x00473580, {0x96, 0x64, 0x19, 0});
    engine(0x004739e0, {0x100, 0xde, global_word(m_, 0x0052eb08), 0});
}

// 0x004dc100: the text entry control: a menu row with an empty buffer, 16 characters, the default colour
std::uint32_t NetGame::edit_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
    engine(kMenuRowConstruct, {self, a0, a1, a2, a3, 0, 0});
    net_mouse_edit_at(m_, self).set_field_0(static_cast<Word>(kTextEditVtable));
    net_mouse_edit_at(m_, self).set_field_100(0);
    net_mouse_edit_at(m_, self).set_field_12c(0x10);
    net_mouse_edit_at(m_, self).set_field_d8(0x64);
    net_mouse_edit_at(m_, self).set_field_130(0);
    net_mouse_edit_at(m_, self).set_field_131(0);
    return self;
}

// 0x004dc160: the deleting destructor
std::uint32_t NetGame::edit_delete(std::uint32_t self, std::uint32_t flags) {
    edit_destruct(self);
    if ((flags & 1) != 0) {
        engine(kFreeObject, {self});
    }
    return self;
}

// 0x004dc180: put the class's table back and run the menu row's destructor (a tail jump)
void NetGame::edit_destruct(std::uint32_t self) {
    net_mouse_edit_at(m_, self).set_field_0(static_cast<Word>(kTextEditVtable));
    engine(kMenuRowDestruct, {self});
}

// 0x004dc370: the buffer the control edits, its capacity and the value the row reports
void NetGame::edit_set_buffer(std::uint32_t self, std::uint32_t buffer, std::uint32_t capacity, std::uint32_t colour) {
    net_mouse_edit_at(m_, self).set_field_100(static_cast<Word>(buffer));
    net_mouse_edit_at(m_, self).set_field_12c(static_cast<Word>(capacity));
    net_mouse_edit_at(m_, self).set_field_d8(static_cast<Word>(colour));
}

// 0x004dc3a0: list the saved parks: the *.prk file names (cut at the first dot, at most 31 characters) and an id per entry, the last entry blanked; with one park or none the
// first entry is the default name and the id 1000
void NetGame::file_list_load(std::uint32_t self, std::uint32_t ids, std::uint32_t count) {
    const std::uint32_t buffer = [&] {
        s_.note("alloc", {count * 260u, 0, 1, 0});
        return static_cast<std::uint32_t>(s_.value("alloc"));
    }();
    set_word_at(m_, self + 0x78, buffer);
    const std::uint32_t names = self + 0x7c;
    const std::uint32_t found = engine_value(kFindFiles, {0x0054a65c, names, count, buffer, 0});
    log_line(0x0054a640);                                           // "GetWinSavedList %d parks"
    std::uint32_t id_cursor = ids;
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t slot = names + 4 * k;
        const std::uint32_t text = word_at(m_, slot);
        if (text != 0) {
            const std::string line = m_.cstring(text, 0x200);
            const auto dot = line.find('.');                        // (0x00501ab0 is strchr: the first dot, so "my.park.PRK" is cut at "my")
            if (dot != std::string::npos) {
                set_byte_at(m_, text + static_cast<std::uint32_t>(dot), 0);
            }
            const std::uint32_t current = word_at(m_, slot);
            if (m_.cstring(current, 0x200).size() >= 0x1f) {
                set_byte_at(m_, current + 0x1f, 0);
            }
        }
        set_word_at(m_, id_cursor, k);
        if (static_cast<std::int32_t>(k) >= static_cast<std::int32_t>(found) - 1) {
            set_word_at(m_, slot, 0x005252f0);
            set_word_at(m_, id_cursor, 0xffffffffu);
        }
        id_cursor += 4;
    }
    if (static_cast<std::int32_t>(found) <= 1) {
        set_word_at(m_, names, global_word(m_, 0x0052eb28));
        set_word_at(m_, ids, 0x3e8);
    }
}

// 0x004dc7b0: a slider row under the pointer: a click on its arrows sends the left / right button (0x8000 / 0x2000), a release over the bar sends its own key
std::uint32_t NetGame::slider_pick(std::uint32_t self) {
    bool inside = false;
    bool arrow = false;
    const std::uint32_t x = word_at(m_, self + 0x34);
    const std::uint32_t y = word_at(m_, self + 0x38);
    const std::uint32_t height = word_at(m_, self + 0x30);
    if ((engine_value(kHitRect, {x, y, 0x200, height}) & 0xff) == 0) {
        return 0;
    }
    const std::uint32_t right = word_at(m_, self + 0x70);
    if (global_int(m_, kMouseX) <= int_at(m_, right + 0x44) + 0x14) {
        inside = true;
    }
    if (global_byte(m_, kMouseReleased) != 0) {
        const std::uint32_t left = word_at(m_, self + 0x6c);
        if (left != 0) {
            if ((engine_value(kHitRect, {word_at(m_, left + 0x44) - 0xa, word_at(m_, self + 0x38), 0x14, word_at(m_, self + 0x30)}) & 0xff) != 0) {
                set_pending_buttons(0x8000);
                arrow = true;
            }
        }
        const std::uint32_t right_now = word_at(m_, self + 0x70);
        if (right_now != 0) {
            if ((engine_value(kHitRect, {word_at(m_, right_now + 0x44) - 0xa, word_at(m_, self + 0x38), 0x14, word_at(m_, self + 0x30)}) & 0xff) != 0) {
                set_pending_buttons(0x2000);
                arrow = true;
            }
        }
        if (inside && half_at(m_, self + 0x1c) != 0 && !arrow) {
            set_pending_buttons(half_at(m_, self + 0x1c));
        }
    }
    set_global_word(m_, kMouseLastId, word_at(m_, self + 0x20));
    return inside ? 1 : 0;
}

// ---- the pick-by-mouse methods (vtable slot 7), from the disassembly. Each answers whether the pointer is over the object (a byte); a release over one with a key sends the key.

// 0x004dbf90: the pointer inside the rectangle, edges included (signed compares)
std::uint32_t NetGame::hit_rect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    const std::int32_t mx = global_int(m_, kMouseX);
    const std::int32_t my = global_int(m_, kMouseY);
    const auto sx = static_cast<std::int32_t>(x);
    const auto sy = static_cast<std::int32_t>(y);
    if (mx - sx > static_cast<std::int32_t>(width) || mx < sx || my < sy || my - sy > static_cast<std::int32_t>(height)) {
        return 0;
    }
    return 1;
}

// 0x004dc190: the text entry control. With the keyboard on (+0xe) the first key down (not the one held last frame) is turned into its character: backspace or the left arrow
// takes the last one off, a letter, a digit, '.' or ' ' is added (not a space when +0x130 is set, only digits and '.' when +0x131 is set), up to the capacity (+0x12c, at most 30).
// The text is copied after the control's own (+0x104), a cursor is added on the first half of every 32 frames (not while up or down is held), and the control's window method in
// slot 9 draws it.
void NetGame::text_edit_update(std::uint32_t self) {
    const std::uint32_t buffer = word_at(m_, self + 0x100);
    if (buffer == 0) {
        return;
    }
    if (int_at(m_, self + 0x12c) > 30) {
        set_word_at(m_, self + 0x12c, 30);
    }
    const std::int32_t capacity = int_at(m_, self + 0x12c);
    std::int32_t at = static_cast<std::int32_t>(m_.cstring(buffer, 0x400).size());
    if (at > capacity) {
        at = capacity;
        set_byte_at(m_, buffer + static_cast<std::uint32_t>(at), 0);
    }
    std::uint32_t key = 0;
    if (byte_at(m_, self + 0xe) != 0) {
        key = engine_value(kFirstKeyDown, {0});
        if (key != 0 && key != global_word(m_, kLastKey)) {
            const std::uint32_t character = engine_value(kKeyChar, {key});
            bool remove = character == 8 || key == 0xcb;
            bool add = false;
            if (!remove && character != 0) {
                const bool alnum = ctype_flags(static_cast<std::int32_t>(character), 0x107, true) != 0;
                if (alnum || character == 0x2e || character == 0x20) {
                    bool accept = true;
                    if (byte_at(m_, self + 0x130) != 0 && ctype_flags(static_cast<std::int32_t>(character), 8, false) != 0) {
                        accept = false;
                    }
                    if (byte_at(m_, self + 0x131) == 0 || ctype_flags(static_cast<std::int32_t>(character), 4, false) != 0 || character == 0x2e) {
                        add = accept;
                    }
                }
            }
            if (remove) {
                if (--at < 0) {
                    at = 0;
                }
                set_byte_at(m_, word_at(m_, self + 0x100) + static_cast<std::uint32_t>(at), 0);
            } else if (add) {
                const std::uint32_t text = word_at(m_, self + 0x100);
                ++at;
                set_byte_at(m_, text + static_cast<std::uint32_t>(at) - 1, character & 0xff);
                if (at > int_at(m_, self + 0x12c)) {
                    at = int_at(m_, self + 0x12c);
                }
                set_byte_at(m_, word_at(m_, self + 0x100) + static_cast<std::uint32_t>(at), 0);
            }
        }
        set_global_word(m_, kLastKey, key);
    }
    set_byte_at(m_, word_at(m_, self + 0x100) + static_cast<std::uint32_t>(int_at(m_, self + 0x12c)), 0);
    const std::string text = m_.cstring(word_at(m_, self + 0x100), 0x400);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(self + 0x104, bytes);
    // the cursor: the first half of every 32 frames, and not while the up or down key is the one just pressed
    std::uint32_t suffix = 0x005252f0;
    const std::uint32_t phase = global_word(m_, kClock) & 0x1f;
    if (!(phase > 0xf || key == 0xc8 || key == 0xd0) && byte_at(m_, self + 0xe) != 0) {
        suffix = 0x0052c740;
    }
    const std::string cursor = m_.cstring(suffix, 0x100);
    std::vector<std::uint8_t> tail(cursor.begin(), cursor.end());
    tail.push_back(0);
    m_.write(self + 0x104 + static_cast<std::uint32_t>(text.size()), tail);
    virtual_call(self, 9, {self + 0x104, 1, word_at(m_, self + 0xd8), word_at(m_, self + 0x50)});
}

// the C runtime's ctype tests (0x00500a21 isalnum with 0x107, 0x005009d1 isspace with 8, 0x0050097c isdigit with 4): the table the locale points at, indexed by the character; the
// wide-locale branch (a character set of more than one byte) calls _isctype and is not reachable in the C locale the game runs in
std::uint32_t NetGame::ctype_flags(std::int32_t character, std::uint32_t mask, bool wide_word) {
    if (global_int(m_, kCtypeMultibyte) > 1) {
        throw ServiceFault("a character set of more than one byte: the C runtime's _isctype is not used in the C locale the game runs in");
    }
    const std::uint32_t at = global_word(m_, kCtypeTable) + 2u * static_cast<std::uint32_t>(character);
    return (wide_word ? half_at(m_, at) : byte_at(m_, at)) & mask;
}

// 0x004dd390: the pointer frame of the screens the pad drives with a pointer. While the pointer is dragged (the cursor hidden, a button held and no release this poll) a pointer outside the
// middle of the box presses the pad direction that way (0x8000 left, 0x2000 right, 0x1000 up, 0x4000 down) and the pointer is put back in the middle; letting go ends the drag. Otherwise the
// two bars at the top (x 0xf and 0x110, 0xe1 wide, 0x1e high) press the pad's two halves when the pointer is over one (the upper half of the display 4 or 8, the lower 1 or 2) and a button
// held over the big box (0x40, 0x40, 0x168 by 0x7c) starts a drag.
std::uint32_t NetGame::pointer_frame() {
    constexpr std::uint32_t kCentreX = 0x140;
    constexpr std::uint32_t kCentreY = 0xf0;
    if (global_byte(m_, kCursorHidden) != 0) {
        if (global_word(m_, kMouseButtons) != 0 && global_byte(m_, kMouseReleased) == 0) {
            const std::int32_t middle_x = global_int(m_, kPointerCenterX);
            const std::int32_t pointer_x = global_int(m_, kPointerX);
            set_global_word(m_, kPointerEdgeBits, 0);
            std::uint32_t bits = 0;
            if (pointer_x < middle_x - 1) {
                bits = 0x8000;
            } else if (pointer_x > middle_x + 1) {
                bits = 0x2000;
            }
            const std::int32_t middle_y = global_int(m_, kPointerCenterY);
            const std::int32_t pointer_y = global_int(m_, kPointerY);
            if (pointer_y < middle_y - 1) {
                bits |= 0x1000;
            } else if (pointer_y > middle_y + 1) {
                bits |= 0x4000;
            }
            if (bits != 0) {
                engine(kSetCursorPos, {kCentreX, kCentreY});
                set_global_word(m_, kPointerCenterX, kCentreX);
                set_global_word(m_, kPointerCenterY, kCentreY);
                engine(kPendingButtons, {bits});
            }
            return 0;
        }
        set_global_word(m_, kPointerSavedX, global_word(m_, kPointerX));
        set_global_word(m_, kPointerSavedY, global_word(m_, kPointerY));
        set_global_byte(m_, kCursorHidden, 0);
        engine(kSetCursorPos, {kCentreX, kCentreY});
        return 0;
    }
    for (const std::uint32_t bar : {0xfu, 0x110u}) {
        if ((engine_value(kHitRect, {bar, 0x10, 0xe1, 0x1e}) & 0xff) != 0) {
            const bool pad = global_byte(m_, kMouseReleased) != 0;
            set_global_byte(m_, kMouseOverItem, 1);
            if (pad) {
                const bool upper = global_int(m_, kMouseY) < 0x1f;
                engine(kPendingButtons, {bar == 0xf ? (upper ? 4u : 1u) : (upper ? 8u : 2u)});
            }
            break;
        }
    }
    if (global_word(m_, kMouseButtons) != 0 && (engine_value(kHitRect, {0x40, 0x40, 0x168, 0x7c}) & 0xff) != 0) {
        set_global_byte(m_, kCursorHidden, 1);
    }
    return 0;
}

// 0x004dc480: a menu list under the pointer: while the pointer moves, each child it is over that is enabled (+0xc) is highlighted, unless an item is hot already, when the
// mouse drives the menus or a button is down
std::uint32_t NetGame::menu_list_pick(std::uint32_t self) {
    const std::uint32_t hit = hit_rect(word_at(m_, self + 0x19c), word_at(m_, self + 0x1a0), word_at(m_, self + 0x1a4), word_at(m_, self + 0x1a8));
    if (hit == 0) {
        return 0;
    }
    for (std::int32_t k = 0; k < int_at(m_, self + 0x194); ++k) {
        if (global_byte(m_, kMouseChanged) == 0) {
            continue;
        }
        const std::uint32_t item = word_at(m_, self + 0x14 + 4 * static_cast<std::uint32_t>(k));
        if ((virtual_call(item, 7) & 0xff) == 0 || byte_at(m_, item + 0xc) == 0 || global_word(m_, kMouseHot) != 0) {
            continue;
        }
        if (global_byte(m_, kMouseActive) != 0 || global_word(m_, kMouseButtons) != 0) {
            engine(0x00470b20, {self, word_at(m_, item + 8)});           // MenuList_Highlight(id)
        }
    }
    return hit;
}

// 0x004dc9f0: an element's box is (x, y) with its width (slot 3) and a height of 12 (0x004dc9e0 answers -1, which means the default); keys 0x5000 and 0xa000 are not sent
std::uint32_t NetGame::element_pick(std::uint32_t self) {
    const std::uint32_t x = word_at(m_, self + 0x20);
    const std::uint32_t y = word_at(m_, self + 0x24);
    const std::uint32_t width = virtual_call(self, 3);
    if (static_cast<std::int32_t>(width) <= 0) {
        return 0;
    }
    const std::uint32_t hit = hit_rect(x, y, width, 0xc);
    const std::uint32_t key = half_at(m_, self + 4);
    if (hit != 0 && key != 0 && global_byte(m_, kMouseReleased) != 0 && key != 0x5000 && key != 0xa000) {
        set_pending_buttons(key);
    }
    return hit;
}

// 0x004dca60: a text's box (height 14) placed by its font's justification (+4: 1 centred, 2 right; +8: 1 middle, 2 and 3 bottom)
std::uint32_t NetGame::text_element_pick(std::uint32_t self) {
    std::uint32_t x = word_at(m_, self + 0x20);
    std::uint32_t y = word_at(m_, self + 0x24);
    const std::uint32_t width = virtual_call(self, 3);
    if (static_cast<std::int32_t>(width) <= 0) {
        return 0;
    }
    constexpr std::uint32_t height = 0xe;
    const std::uint32_t font = word_at(m_, self + 0x2c);
    const std::uint32_t across = word_at(m_, font + 4);
    if (across == 1) {
        x -= static_cast<std::uint32_t>(static_cast<std::int32_t>(width) >> 1);
    } else if (across == 2) {
        x -= width;
    }
    const std::int32_t down = int_at(m_, font + 8);
    if (down == 1) {
        y -= height >> 1;
    } else if (down == 2 || down == 3) {
        y -= height;
    }
    const std::uint32_t hit = hit_rect(x, y, width, height);
    const std::uint32_t key = half_at(m_, self + 4);
    if (hit != 0 && key != 0 && global_byte(m_, kMouseReleased) != 0) {
        set_pending_buttons(key);
    }
    return hit;
}

// 0x004dcb30: a sprite element asks its image (+0x28, slot 4)
std::uint32_t NetGame::sprite_element_pick(std::uint32_t self) {
    const std::uint32_t hit = virtual_call(word_at(m_, self + 0x28), 4) & 0xff;
    const std::uint32_t key = half_at(m_, self + 4);
    if (hit != 0 && key != 0 && global_byte(m_, kMouseReleased) != 0) {
        set_pending_buttons(key);
    }
    return hit;
}

// 0x004dcb70: an image's box is its position (+0x44, +0x48) and its screen size; its key is at +0x4c
std::uint32_t NetGame::image_pick(std::uint32_t self) {
    const std::uint32_t x = word_at(m_, self + 0x44);
    const std::uint32_t y = word_at(m_, self + 0x48);
    const std::uint32_t width = engine_value(0x00457820, {self});    // SlicedImage2::screenWidth
    const std::uint32_t height = engine_value(0x00457830, {self});   // SlicedImage2::screenHeight
    const std::uint32_t hit = hit_rect(x, y, width, height);
    const std::uint32_t key = half_at(m_, self + 0x4c);
    if (hit != 0 && key != 0 && global_byte(m_, kMouseReleased) != 0) {
        set_pending_buttons(key);
    }
    return hit;
}

// 0x004dcc20: a prompt window: the window's own test first (its answer unused), then its two children (+0xac, +0x28). Over neither, the image goes grey (0x9a). Over one, it is lit
// (0xff) and marked hot, and the window's key decides: 0xa0 and 0xa000 are a pair of arrow boxes (50 x 40 each, from 20 above the window's point), 0x5000 two pairs of up/down
// boxes (the second pair can miss, which greys the image again); a release sends the key, and a hit with a release holds the pending buttons for two polls. A held button over
// the window tints the image 0xc0.
std::uint32_t NetGame::prompt_window_pick(std::uint32_t self) {
    constexpr std::uint32_t kSetColour = 0x004dcb00;
    auto colour = [&](std::uint32_t level) { engine(kSetColour, {word_at(m_, self + 0x28), level, level, level}); };
    (void)element_pick(self);
    if ((virtual_call(word_at(m_, self + 0xac), 7) & 0xff) == 0 && (virtual_call(word_at(m_, self + 0x28), 7) & 0xff) == 0) {
        colour(0x9a);
        return 0;
    }
    bool hit = true;
    set_global_byte(m_, kMouseOverItem, 1);
    set_global_word(m_, kMouseHot, 1);
    colour(0xff);
    const std::uint32_t key = half_at(m_, self + 4);
    const std::uint32_t x = word_at(m_, self + 8);
    const std::uint32_t y = word_at(m_, self + 0xc);
    const bool released = global_byte(m_, kMouseReleased) != 0;
    auto arrows = [&](std::uint32_t left, std::uint32_t right) {
        if (hit_rect(x, y - 0x14, 0x32, 0x28) != 0) {
            set_pending_buttons(left);
        } else if (hit_rect(x + 0x32, y - 0x14, 0x32, 0x28) != 0) {
            set_pending_buttons(right);
        }
    };
    bool tail = true;                                               // 0x004dce07: a release with a hit holds the buttons
    if (key == 0xa0) {
        if (global_word(m_, kMouseButtons) != 0) {
            arrows(0x80, 0x20);
        }
    } else if (key == 0x5000) {
        std::int32_t button = -1;
        if (hit_rect(x + 0x28, y - 0xf, 0x18, 0xf) != 0) {
            button = 0x1000;
        } else if (hit_rect(x + 0x28, y, 0x18, 0xf) != 0) {
            button = 0x4000;
        } else if (hit_rect(x + 0xf, y - 8, 0x48, 8) != 0) {
            button = 0x1000;
        } else if (hit_rect(x + 0xf, y, 0x48, 8) != 0) {
            button = 0x4000;
        } else {
            set_global_byte(m_, kMouseOverItem, 0);
            set_global_word(m_, kMouseHot, 0);
            colour(0x9a);
            hit = false;
        }
        if (!released) {
            tail = false;
        } else if (button >= 0) {
            set_pending_buttons(static_cast<std::uint32_t>(button));
        }
    } else if (!released) {
        tail = false;
    } else if (key == 0xa000) {
        arrows(0x8000, 0x2000);
    } else {
        set_pending_buttons(key);
    }
    if (tail && global_byte(m_, kMouseReleased) != 0 && hit) {
        set_global_word(m_, 0x006a6970, 2);                                    // PCInput's pending frames
    }
    if (global_word(m_, kMouseButtons) != 0 && hit) {
        colour(0xc0);
    }
    return hit ? 1 : 0;
}

// 0x004dbfd0 (from the disassembly): is the pointer inside the circle? The distance is the x87 square root of the squared integer offsets, and the pointer is
// inside when it is below the radius (equal is outside)
std::uint32_t NetGame::hit_circle(std::uint32_t x, std::uint32_t y, std::uint32_t radius) {
    const std::int32_t dx = std::abs(global_int(m_, kMouseX) - static_cast<std::int32_t>(x));
    const std::int32_t dy = std::abs(global_int(m_, kMouseY) - static_cast<std::int32_t>(y));
    const std::int32_t squared = static_cast<std::int32_t>(static_cast<std::uint32_t>(dy * dy) + static_cast<std::uint32_t>(dx * dx));
    return std::sqrt(static_cast<double>(squared)) < static_cast<double>(static_cast<std::int32_t>(radius)) ? 1 : 0;
}

// 0x004dcf80 (from the disassembly): the main menu's wheel. With no turn pending (kSliderValue 0): over the wheel (a circle of 150 around (255, 128)), each of its
// seven hot spots (a 50 x 50 box around each point of the table below) marks the pointer over an item and, on a release, asks for that turn (the first spot, the
// one in front, also presses the confirm button 0x40); the poll is counted. With a turn pending and more than four polls since the last step, one step is
// taken (the left or right button: 0x2000 or 0x8000) and the count starts again. The answer is whether the pointer is over the wheel.
std::uint32_t NetGame::main_menu_wheel_pick(std::uint32_t) {
    struct Spot {
        std::int32_t x, y, turn;
    };
    static constexpr Spot kSpots[7] = {{0xff, 0x44, 0}, {0x144, 0x5e, -1}, {0x15b, 0x92, -2}, {0x120, 0xbb, -3}, {0xc5, 0xb3, 3}, {0x9b, 0x87, 2}, {0xb7, 0x5c, 1}};
    const std::int32_t pending = global_int(m_, kSliderValue);
    if (pending != 0) {
        if (global_int(m_, kSliderTicks) <= 4) {
            set_global_word(m_, kSliderTicks, global_word(m_, kSliderTicks) + 1);
            return 0;
        }
        set_pending_buttons(pending < 0 ? 0x2000 : 0x8000);
        set_global_word(m_, kSliderValue, static_cast<std::uint32_t>(pending < 0 ? pending + 1 : pending - 1));
        set_global_word(m_, kSliderTicks, 1);
        return 0;
    }
    const std::uint32_t over = hit_circle(0xff, 0x80, 0x96) & 0xff;
    if (over != 0) {
        for (std::uint32_t k = 0; k < 7; ++k) {
            const Spot& spot = kSpots[k];
            if ((hit_rect(static_cast<std::uint32_t>(spot.x - 0x19), static_cast<std::uint32_t>(spot.y - 0x19), 0x32, 0x32) & 0xff) == 0) {
                continue;
            }
            set_global_byte(m_, kMouseOverItem, 1);
            if (global_byte(m_, kMouseReleased) != 0) {
                set_global_word(m_, kSliderValue, static_cast<std::uint32_t>(spot.turn));
                if (k == 0) {
                    set_pending_buttons(0x40);
                }
            }
        }
    }
    set_global_word(m_, kSliderTicks, global_word(m_, kSliderTicks) + 1);
    return over;
}

// 0x004dcec0 (from the disassembly; slot 7 of the level select's globe, in no symbol table): over the globe (a circle of 50 around (361, 70)) a release presses
// the buttons the element holds (the word at +0x28, when it has one); the answer is whether the pointer is over the globe
std::uint32_t NetGame::level_globe_pick(std::uint32_t self) {
    const std::uint32_t over = hit_circle(0x169, 0x46, 0x32) & 0xff;
    if (over != 0) {
        const std::uint32_t buttons = half_at(m_, self + 0x28);
        if (buttons != 0 && global_byte(m_, kMouseReleased) != 0) {
            set_pending_buttons(buttons);
        }
    }
    return over;
}

// 0x004dd140 (from the disassembly): the main menu's window. On a release over its wheel (+0x200, the wheel's own test, slot 7) the cursor is shown; the answer is
// always 0
std::uint32_t NetGame::main_menu_window_pick(std::uint32_t self) {
    if (global_byte(m_, kMouseReleased) != 0 && (virtual_call(word_at(m_, self + 0x200), 7) & 0xff) != 0) {
        set_global_byte(m_, kCursorShown, 1);
    }
    return 0;
}

// 0x004dcb00: the row's sprite takes a colour (three bytes) and is flagged as changed
void NetGame::widget_set_colour(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const std::uint32_t sprite = word_at(m_, self + 0x28);
    set_byte_at(m_, sprite + 5, 1);
    set_byte_at(m_, word_at(m_, self + 0x28) + 7, a);
    set_byte_at(m_, word_at(m_, self + 0x28) + 8, b);
    set_byte_at(m_, word_at(m_, self + 0x28) + 9, c);
}

// 0x004dcbc0: the key code (word at +4) of the key a name in the table (the second text of each row) stands for, if the table has one
void NetGame::key_code_by_name(std::uint32_t self, std::uint32_t name) {
    const std::string wanted = m_.cstring(name, 0x100);
    std::uint32_t row = 0;
    if (!crt::equal_nocase(wanted, m_.cstring(global_word(m_, kKeyTable + 8), 0x100))) {
        for (;;) {
            const std::uint32_t next = word_at(m_, kKeyTable + 12 * row + 16);
            ++row;
            if (next == 0) {
                return;
            }
            if (crt::equal_nocase(wanted, m_.cstring(word_at(m_, kKeyTable + 12 * row + 8), 0x100))) {
                break;
            }
        }
    }
    set_half_at(m_, self + 4, half_at(m_, kKeyTable + 12 * row));
}

// 0x004dd520: the pointer over a row of a vertical menu (rows of 0x18 bytes from +0x24: spacing, visible, enabled); touching an enabled row selects it and a click on one is a key press
std::uint32_t NetGame::pick_row(std::uint32_t self) {
    std::uint32_t found = 0;
    const std::int32_t height = int_at(m_, self + 0x14);
    std::uint32_t top = static_cast<std::uint32_t>(int_at(m_, self + 0x10) - height / 2);
    const std::uint32_t count = byte_at(m_, self + 9);
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t row = self + 0x24 + 0x18 * k;
        if (byte_at(m_, row + 1) == 0) {
            continue;
        }
        if (byte_at(m_, row + 2) != 0) {
            if ((engine_value(kHitRect, {0xc8, top, 0xc8, static_cast<std::uint32_t>(height)}) & 0xff) != 0) {
                found = 1;
                if (global_byte(m_, kMouseChanged) != 0 || global_byte(m_, kMouseReleased) != 0 || global_word(m_, kMouseButtons) != 0) {
                    engine(kMenuSetLine, {self, k});
                    if (global_byte(m_, kMouseReleased) != 0) {
                        set_pending_buttons(0x40);
                    }
                }
            }
        }
        top += byte_at(m_, row) + word_at(m_, self + 0x14);
    }
    return found;
}

// 0x004dd6d0: the tint of the row's sprite (three 16-bit components, the floats scaled by 4096); the first sprite wins when the row has two
void NetGame::set_tint(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    const std::uint32_t sprite = word_at(m_, self + 0x30) != 0 ? word_at(m_, self + 0x30) : word_at(m_, self + 0x2c);
    set_byte_at(m_, sprite + 5, byte_at(m_, sprite + 5) | 2);
    const double scale = static_cast<double>(kFixedOne);
    set_half_at(m_, sprite + 0x28, ftol(static_cast<double>(as_float(r)) * scale));
    set_half_at(m_, sprite + 0x2a, ftol(static_cast<double>(as_float(g)) * scale));
    set_half_at(m_, sprite + 0x2c, ftol(static_cast<double>(as_float(b)) * scale));
}

// ---- the pick-by-mouse methods that were still missing (12 s7: the career fuzz stopped on 0x004dc530 and 0x004dd180), from the disassembly

// 0x004dc530: a grid of [self+0x18] items (the table at +0xc, [self+4] items to a row). While the pointer changes, an item that answers slot 7 (the pointer is over it), is enabled (+0xc) and
// is being driven by the mouse becomes the selection: column to +0x10, row to +0x14, and its key (+4) is set to 0x40. The answer is always 0.
std::uint32_t NetGame::grid_pick(std::uint32_t self) {
    if (global_byte(m_, kMouseChanged) == 0) {
        return 0;
    }
    for (std::int32_t k = 0; k < int_at(m_, self + 0x18); ++k) {
        const std::int32_t columns = int_at(m_, self + 4);
        if (columns == 0) {
            throw std::runtime_error("division fault in the item grid");
        }
        const std::uint32_t slot = word_at(m_, self + 0xc) + 4 * static_cast<std::uint32_t>(k);
        const std::uint32_t item = word_at(m_, slot);
        if (item == 0 || (virtual_call(item, 7) & 0xff) == 0) {
            continue;
        }
        const std::uint32_t again = word_at(m_, word_at(m_, self + 0xc) + 4 * static_cast<std::uint32_t>(k));
        if (byte_at(m_, again + 0xc) == 0 || global_byte(m_, kMouseChanged) == 0) {
            continue;
        }
        if (global_byte(m_, kMouseActive) == 0 && global_word(m_, kMouseButtons) == 0) {
            continue;
        }
        set_word_at(m_, self + 0x14, static_cast<std::uint32_t>(k / columns));
        set_word_at(m_, self + 0x10, static_cast<std::uint32_t>(k % columns));
        set_half_at(m_, word_at(m_, slot) + 4, 0x40);
    }
    return 0;
}

// 0x004dc5c0 (and the jump at 0x004dc630): a button's rectangle (x +0x1c, y +0x20, width +0x14, height +0x18). Over it, the id (+8) is remembered and, on a release, its key (+4) is sent
// when the button is enabled (+0xc). The answer is the rectangle test's.
std::uint32_t NetGame::button_pick(std::uint32_t self) {
    const std::uint32_t over = engine_value(kHitRect, {word_at(m_, self + 0x1c), word_at(m_, self + 0x20), word_at(m_, self + 0x14), word_at(m_, self + 0x18)}) & 0xff;
    if (over != 0) {
        set_global_word(m_, kMouseLastId, word_at(m_, self + 8));
        const std::uint32_t key = half_at(m_, self + 4);
        if (key != 0 && global_byte(m_, kMouseReleased) != 0 && byte_at(m_, self + 0xc) != 0) {
            set_pending_buttons(key);
        }
    }
    return over;
}

// 0x004dc8a0: a button that also sends right (0x2000) on a release over it
std::uint32_t NetGame::right_button_pick(std::uint32_t self) {
    const std::uint32_t over = button_pick(self);
    if ((over & 0xff) != 0 && global_byte(m_, kMouseReleased) != 0) {
        set_pending_buttons(0x2000);
    }
    return over & 0xff;
}

// 0x004dc640: a row of columns (widths at +0xd4, [self+0xe4] of them) with a left and a right arrow (+0xf8, +0xfc). Over the row (width 0x200), a pointer left of the widest column's end
// (+0x14) is over the list and the list row it is over is remembered (the last one, or the first column whose end it is not past); a release over an arrow (20 wide, centred 10 left of the
// arrow's x at +0x44) sends left (0x8000) or right (0x2000); a release elsewhere over the list sends its key (+4) unless an arrow took it or a key is pending (0x006a696c). The answer is "over the list".
std::uint32_t NetGame::column_list_pick(std::uint32_t self) {
    if ((engine_value(kHitRect, {word_at(m_, self + 0x1c), word_at(m_, self + 0x20), 0x200, word_at(m_, self + 0x18)}) & 0xff) == 0) {
        return 0;
    }
    bool over = false;
    bool arrow = false;
    const std::int32_t count = int_at(m_, self + 0xe4);
    std::int32_t widest = 0;
    for (std::int32_t k = 0; k < count; ++k) {
        const std::int32_t width = int_at(m_, self + 0xd4 + 4 * static_cast<std::uint32_t>(k));
        if (width > widest) {
            widest = width;
        }
    }
    const std::int32_t left_edge = int_at(m_, self + 0x1c);
    if (global_int(m_, kMouseX) <= static_cast<std::int32_t>(static_cast<std::uint32_t>(left_edge) + static_cast<std::uint32_t>(widest) + 0x14)) {
        over = true;
        set_global_word(m_, kMouseListRow, static_cast<std::uint32_t>(count - 1));
    }
    const std::int32_t columns = int_at(m_, self + 0xe4);
    for (std::int32_t k = 0; k < columns - 1; ++k) {
        if (global_int(m_, kMouseX) <= static_cast<std::int32_t>(static_cast<std::uint32_t>(left_edge) + word_at(m_, self + 0xd8 + 4 * static_cast<std::uint32_t>(k)))) {
            set_global_word(m_, kMouseListRow, static_cast<std::uint32_t>(k));
            break;
        }
    }
    if (byte_at(m_, self + 0xe) != 0) {
        if (global_byte(m_, kMouseReleased) != 0) {
            const std::uint32_t arrows[2][2] = {{word_at(m_, self + 0xf8), 0x8000}, {word_at(m_, self + 0xfc), 0x2000}};
            for (const auto& entry : arrows) {
                if (entry[0] != 0 &&
                    (engine_value(kHitRect, {word_at(m_, entry[0] + 0x44) - 0xa, word_at(m_, self + 0x20), 0x14, word_at(m_, self + 0x18)}) & 0xff) != 0) {
                    set_pending_buttons(entry[1]);
                    arrow = true;
                }
            }
        }
        const std::uint32_t key = half_at(m_, self + 4);
        if (over && global_byte(m_, kMouseReleased) != 0 && key != 0 && !arrow && half_at(m_, 0x006a696c) == 0) {
            set_pending_buttons(key);
        }
    }
    set_global_word(m_, kMouseLastId, word_at(m_, self + 8));
    return over ? 1 : 0;
}

// 0x004dc8d0: a bar (x +0x1c, y +0x20, height +0x18, width 0x200) with a left arrow (+0x70) and a right arrow (+0x74): the pointer left of the right arrow's end (+0x44, +0x14) is over the bar;
// a release over an arrow sends left (0x8000) or right (0x2000), a release elsewhere over the bar sends right. The answer is "over the bar".
std::uint32_t NetGame::arrow_pair_pick(std::uint32_t self) {
    if ((engine_value(kHitRect, {word_at(m_, self + 0x1c), word_at(m_, self + 0x20), 0x200, word_at(m_, self + 0x18)}) & 0xff) == 0) {
        return 0;
    }
    bool arrow = false;
    const bool over = global_int(m_, kMouseX) <= int_at(m_, word_at(m_, self + 0x74) + 0x44) + 0x14;
    if (byte_at(m_, self + 0xe) != 0) {
        if (global_byte(m_, kMouseReleased) != 0) {
            const std::uint32_t arrows[2][2] = {{word_at(m_, self + 0x70), 0x8000}, {word_at(m_, self + 0x74), 0x2000}};
            for (const auto& entry : arrows) {
                if (entry[0] != 0 &&
                    (engine_value(kHitRect, {word_at(m_, entry[0] + 0x44) - 0xa, word_at(m_, self + 0x20), 0x14, word_at(m_, self + 0x18)}) & 0xff) != 0) {
                    set_pending_buttons(entry[1]);
                    arrow = true;
                }
            }
        }
        if (over && global_byte(m_, kMouseReleased) != 0 && !arrow) {
            set_pending_buttons(0x2000);
        }
    }
    set_global_word(m_, kMouseLastId, word_at(m_, self + 8));
    return over ? 1 : 0;
}

// 0x004dd180: on a release, the three children of the object at +0xb0 (+0x3c, +0x40, +0x44) answer slot 4 (a press?) and each that does sends left (0x8000), right (0x2000), up (0x1000). The answer is 0.
std::uint32_t NetGame::arrow_children_pick(std::uint32_t self) {
    if (global_byte(m_, kMouseReleased) == 0) {
        return 0;
    }
    static constexpr std::uint32_t kChild[3][2] = {{0x3c, 0x8000}, {0x40, 0x2000}, {0x44, 0x1000}};
    for (const auto& entry : kChild) {
        if ((virtual_call(word_at(m_, word_at(m_, self + 0xb0) + entry[0]), 4) & 0xff) != 0) {
            set_pending_buttons(entry[1]);
        }
    }
    return 0;
}

}  // namespace opentony::runtime::pcplatform
