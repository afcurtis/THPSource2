#include "auto_views.hpp"
#include "frontend_mess.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

// The original's `imul` + `idiv`: a signed product divided by a signed divisor, truncating toward zero.
std::int32_t mul_div(std::int32_t a, std::int32_t b, std::int32_t divisor) {
    const std::int32_t product = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
    return product / divisor;
}

}  // namespace

void Mess::set_sort_mode(std::int32_t mode) {
    set_global_half(m_, kMessSort, static_cast<std::uint32_t>(mode));
    // both branches of the original call the assertion routine: once with false for an illegal mode, once with true
    s_.check(mode == 0 || mode == 1, kMsgSort);
}

void Mess::set_text_justify(std::uint32_t justify) {
    switch (justify & 0xff) {
        case 0:
            set_global_word(m_, kMessFont + kFontHAlign, 1);
            break;
        case 1:
            set_global_word(m_, kMessFont + kFontHAlign, 0);
            break;
        case 2:
            set_global_word(m_, kMessFont + kFontHAlign, 2);
            break;
        default:
            break;
    }
}

void Mess::set_scale(std::int32_t scale) {
    set_global_word(m_, kMessFont + kFontScale, static_cast<std::uint32_t>(scale) << 4);
}

std::int32_t Mess::get_scale() const {
    const std::int32_t scale = global_int(m_, kMessFont + kFontScale);
    return (scale + ((scale >> 31) & 0xf)) >> 4;
}

void Mess::set_rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    set_global_word(m_, kMessFont + kFontRed, r & 0xff);
    set_global_word(m_, kMessFont + kFontGreen, g & 0xff);
    set_global_word(m_, kMessFont + kFontBlue, b & 0xff);
}

std::uint32_t Mess::draw_text(std::int32_t x, std::int32_t y, std::uint32_t text) {
    f_.draw(kMessFont, x, y, text, 0);
    return 0;
}

std::int32_t Mess::text_width(std::uint32_t text) const { return f_.text_width(kMessFont, text); }

void Mess::delete_message(std::uint32_t node) {
    s_.check(node != 0, kMsgNullMessage);
    const std::uint32_t next = word_at(m_, node + kNodeNext);
    const std::uint32_t prev = word_at(m_, node + kNodePrev);
    if (next != 0) {
        set_word_at(m_, next + kNodePrev, prev);
    }
    if (prev != 0) {
        set_word_at(m_, prev + kNodeNext, next);
    }
    if (node == global_word(m_, kMessList)) {
        set_global_word(m_, kMessList, next);
    }
    s_.note(kMemDelete, {node});
}

void Mess::delete_all() {
    std::uint32_t node = global_word(m_, kMessList);
    if (node != 0) {
        std::uint32_t next = word_at(m_, node + kNodeNext);
        for (;;) {
            s_.note(kMemDelete, {node});
            if (next == 0) {
                break;
            }
            node = next;
            next = word_at(m_, next + kNodeNext);
        }
    }
    set_global_word(m_, kMessList, 0);
}

bool Mess::exists(std::uint32_t node) const {
    for (std::uint32_t p = global_word(m_, kMessList); p != 0; p = word_at(m_, p + kNodeNext)) {
        if (p == node) {
            return true;
        }
    }
    return false;
}

void Mess::delete_by_text(std::uint32_t text) {
    for (std::uint32_t p = global_word(m_, kMessList); p != 0; p = word_at(m_, p + kNodeNext)) {
        if (word_at(m_, p + kNodeText) == text) {
            delete_message(p);
            return;
        }
    }
}

std::uint32_t Mess::alloc_node() {
    s_.note(kAllocService, {kNodeSize, 0, 1, 0});
    const std::uint32_t node = static_cast<std::uint32_t>(s_.value(kAllocService));
    const std::uint32_t head = global_word(m_, kMessList);
    set_word_at(m_, node + kNodePrev, 0);
    set_word_at(m_, node + kNodeNext, head);
    set_global_word(m_, kMessList, node);
    if (word_at(m_, node + kNodeNext) != 0) {
        set_word_at(m_, word_at(m_, node + kNodeNext) + kNodePrev, node);
    }
    return node;
}

std::uint32_t Mess::add_message(std::uint32_t text, std::uint32_t program, std::uint32_t stacked, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t node = alloc_node();
    set_word_at(m_, node + kNodeText, text);
    set_half_at(m_, node + kNodeAge, 0);
    set_half_at(m_, node + kNodeTotalAge, 0);
    set_word_at(m_, node + kNodeProgram, program);
    set_byte_at(m_, node + kNodeStacked, stacked);
    set_half_at(m_, node + kNodeX, x);
    set_half_at(m_, node + kNodeY, y);
    return node;
}

void Mess::update() {
    std::uint32_t node = global_word(m_, kMessList);
    while (node != 0) {
        const std::uint32_t next = word_at(m_, node + kNodeNext);
        const std::uint32_t program = word_at(m_, node + kNodeProgram);
        if (half_at(m_, node + kNodeAge) < half_at(m_, program)) {
            set_half_at(m_, node + kNodeAge, half_at(m_, node + kNodeAge) + 1u);
            set_half_at(m_, node + kNodeTotalAge, half_at(m_, node + kNodeTotalAge) + 1u);
        } else {
            const std::uint8_t step = byte_at(m_, program + 0x1e);
            if (step == 0) {
                delete_message(node);
            } else if (step != 0x7f) {
                // the program continues `step` records further on (a signed byte, in 0x20-byte steps)
                const std::int32_t offset = static_cast<std::int8_t>(byte_at(m_, program + 0x1e)) * 32;
                set_half_at(m_, node + kNodeAge, 0);
                set_word_at(m_, node + kNodeProgram, program + static_cast<std::uint32_t>(offset));
                set_half_at(m_, node + kNodeTotalAge, half_at(m_, node + kNodeTotalAge) + 1u);
            }
        }
        node = next;
    }
}

void Mess::display() {
    std::int32_t stacked = 0;
    for (std::uint32_t p = global_word(m_, kMessList); p != 0; p = word_at(m_, p + kNodeNext)) {
        if (byte_at(m_, p + kNodeStacked) != 0) {
            ++stacked;
        }
    }
    std::int32_t stack_y = stacked * 16 - 16;
    for (std::uint32_t node = global_word(m_, kMessList); node != 0; node = word_at(m_, node + kNodeNext)) {
        const std::uint32_t step = word_at(m_, node + kNodeProgram);
        const std::uint32_t age = half_at(m_, node + kNodeAge);
        const std::uint32_t flash = byte_at(m_, step + 0x1a);
        if (flash != 0 && ((age / flash) & 1) == 0) {
            continue;
        }
        const std::int32_t duration = half_at(m_, step);
        s_.check(duration != 0, kMsgZeroDuration);
        if (duration == 0) {
            throw MemoryFault("Mess_Display: a message step of zero duration divides by zero in the original");
        }
        set_text_justify(byte_at(m_, step + 3));
        const std::int32_t t = static_cast<std::int32_t>(age);
        const std::int32_t scale0 = half_at(m_, step + 4);
        const std::int32_t scale = mul_div(static_cast<std::int32_t>(half_at(m_, step + 6)) - scale0, t, duration) + scale0;
        set_global_half(m_, kMessScale16, static_cast<std::uint32_t>(scale));
        set_scale(static_cast<std::int32_t>(global_word(m_, kMessScale16) & 0xffff));
        auto channel = [&](std::uint32_t from, std::uint32_t to) {
            return static_cast<std::uint32_t>(mul_div(static_cast<std::int32_t>(byte_at(m_, step + to)) - byte_at(m_, step + from), t, duration)) +
                   byte_at(m_, step + from);
        };
        set_rgb(channel(0x8, 0xb), channel(0x9, 0xc), channel(0xa, 0xd));
        if (half_at(m_, step + 0x1c) != 0) {
            // a flashing message cycles its red channel with the simulation clock
            const std::uint32_t phase = global_word(m_, kSimulationTime) % 20;
            set_rgb(phase * 12, 100, 100);
        }
        const std::int32_t x0 = short_at(m_, step + 0x12);
        const std::int32_t x1 = short_at(m_, step + 0x16);
        const std::int32_t y0 = short_at(m_, step + 0x14);
        const std::int32_t y1 = short_at(m_, step + 0x18);
        std::int32_t x = short_at(m_, node + kNodeX) + (x0 + mul_div(x1 - x0, t, duration));
        std::int32_t y = short_at(m_, node + kNodeY) + (y0 + mul_div(y1 - y0, t, duration));
        if (byte_at(m_, node + kNodeStacked) != 0) {
            y += stack_y;
            stack_y -= 0x20;
        }
        const std::uint32_t text = word_at(m_, node + kNodeText);
        const HostOptions::HudMessage* laid = hud_message(text);
        const std::uint32_t begin = laid != nullptr ? global_word(m_, kMessArenaCursor) : 0;
        draw_text(x, y, text);
        if (laid != nullptr && global_word(m_, kMessArenaCursor) > begin) {
            HostOptions::HudGroup group;
            group.begin = begin;
            group.end = global_word(m_, kMessArenaCursor);
            group.fx = static_cast<float>(laid->x) / 512.0f;
            group.fy = static_cast<float>(laid->y) / 240.0f;
            host_options().hud_groups.push_back(group);
        }
    }
}

// A message of the host's that follows the HUD's layout (HostOptions::hud_messages), or null: none asked for, no layout, or a split screen side by side (whose HUD
// keeps the 4:3 layout, as Panel::display's does)
const HostOptions::HudMessage* Mess::hud_message(std::uint32_t text) const {
    const HostOptions& host = host_options();
    if (host.hud_messages.empty() || !host.hud_layout()) {
        return nullptr;
    }
    const std::uint32_t view = global_word(m_, kMessViewportMode);
    if (view != 0 && view != 2) {
        return nullptr;
    }
    for (const HostOptions::HudMessage& message : host.hud_messages) {
        if (message.text == text) {
            return &message;
        }
    }
    return nullptr;
}

void Mess::load_font(std::uint32_t name, std::uint32_t spacing, std::uint32_t shadow_offset) {
    s_.check(global_byte(m_, kMessFontLoaded) == 0, kMsgOneFont);
    s_.note(kFontLoad, {name}, m_.cstring(name));
    const std::uint32_t font = static_cast<std::uint32_t>(s_.value(kFontLoad));
    // the fields the mess font keeps: alignment, scale and colour, flags, shadow, the glyph tables
    set_global_word(m_, kMessFont + 0x04, word_at(m_, font + 0x04));
    set_global_word(m_, kMessFont + 0x08, word_at(m_, font + 0x08));
    set_global_word(m_, kMessFont + 0x0c, word_at(m_, font + 0x0c));
    set_global_word(m_, kMessFont + 0x10, word_at(m_, font + 0x10));
    set_global_word(m_, kMessFont + 0x14, word_at(m_, font + 0x14));
    set_global_word(m_, kMessFont + 0x18, word_at(m_, font + 0x18));
    set_global_word(m_, kMessFont + 0x1c, word_at(m_, font + 0x1c));
    set_global_byte(m_, kMessFont + 0x20, byte_at(m_, font + 0x20));
    set_global_byte(m_, kMessFont + 0x21, byte_at(m_, font + 0x21));
    set_global_word(m_, kMessFont + 0x24, word_at(m_, font + 0x24));
    set_global_word(m_, kMessFont + 0x28, word_at(m_, font + 0x28));
    set_global_word(m_, kMessFont + 0x2c, word_at(m_, font + 0x2c));
    set_global_word(m_, kMessFont + 0x30, word_at(m_, font + 0x30));
    set_global_word(m_, kMessFont + 0x34, word_at(m_, font + 0x34));
    set_global_word(m_, kMessFont + 0x38, word_at(m_, font + 0x38));
    set_global_word(m_, kMessFont + 0x3c, word_at(m_, font + 0x3c));
    set_global_word(m_, kMessFont + 0x40, word_at(m_, font + 0x40));
    set_global_word(m_, kMessFont + 0x44, word_at(m_, font + 0x44));
    set_global_byte(m_, kMessFont + 0x48, byte_at(m_, font + 0x48));
    set_global_byte(m_, kMessFont + 0x49, byte_at(m_, font + 0x49));
    set_global_byte(m_, kMessFont + 0x4a, byte_at(m_, font + 0x4a));
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        set_byte_at(m_, kMessFont + kFontMap + i, byte_at(m_, font + kFontMap + i));
    }
    // the name is kept for the unload; then the two spacing words and the two shadow offsets are overridden
    const std::string text = m_.cstring(name);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kMessFontName, bytes);
    set_global_word(m_, kMessFont + 0x0c, spacing);
    set_global_word(m_, kMessFont + 0x10, spacing);
    set_global_word(m_, kMessFont + 0x24, shadow_offset);
    set_global_word(m_, kMessFont + 0x28, shadow_offset);
    set_global_word(m_, kMessFont + 0x08, 1);
    set_global_byte(m_, kMessFontLoaded, 1);
}

void Mess::unload_font() {
    s_.check(global_byte(m_, kMessFontLoaded) != 0, kMsgNoMessFont);
    s_.note(kFontUnload, {kMessFontName}, m_.cstring(kMessFontName));
    set_global_byte(m_, kMessFontLoaded, 0);
}

void Mess::deprecated_notice() { s_.note(kPrintfLog, {kMsgDeprecated}); }

}  // namespace opentony::runtime::frontend
