#include "auto_views.hpp"
#include "shell_helpers.hpp"
#include "frame_pacing.hpp"

#include <cstring>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace shellkit;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// the compiler's unsigned divide by 240 of a 32-bit product (the screen height scale)
std::uint32_t div240(std::int32_t value, std::uint32_t height) { return (static_cast<std::uint32_t>(value) * height) / 240u; }

constexpr std::uint32_t kPlayerSpoolTable = 0x0052cf38;     // {id, name} pairs, ended by an empty name
constexpr std::uint32_t kPieWords = 0x00561d58;             // eight triples the slide table keeps

}  // namespace

void ShellHelpers::link_main(std::uint32_t packet) {
    const std::uint32_t head = word_at(m_, global_word(m_, kRenderer) + 0x84);
    set_word_at(m_, packet, word_at(m_, head));
    set_word_at(m_, word_at(m_, global_word(m_, kRenderer) + 0x84), packet);
}

// ---- the pre-load progress screen ------------------------------------------------------------------------------------------------------------------------------------

// 0x00454930
std::uint32_t ShellHelpers::pie_construct(std::uint32_t self) {
    shell_helpers_pie_at(m_, self).set_field_0(static_cast<Word>(kVtPie));
    shell_helpers_pie_at(m_, self).set_field_30(0);
    shell_helpers_pie_at(m_, self).set_field_28(3);
    shell_helpers_pie_at(m_, self).set_field_2c(0);
    return self;
}

// 0x00454950
std::uint32_t ShellHelpers::pie_delete(std::uint32_t self, std::uint32_t flag) {
    pie_destruct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00454970
void ShellHelpers::pie_destruct(std::uint32_t self) { set_word_at(m_, self, kVtBase); }

// 0x00454980: start loading the file of one of the table's entries into the player spool; false when the id has none
bool ShellHelpers::pie_load(std::uint32_t self, std::uint32_t id) {
    if (shell_helpers_pie_at(m_, self).field_30() != 0) {
        return true;
    }
    std::uint32_t entry = kPlayerSpoolTable;
    if (byte_at(m_, global_word(m_, kPlayerSpoolTable + 4)) != 0) {
        while (id != word_at(m_, entry)) {
            const std::uint32_t next = word_at(m_, entry + 0xc);
            entry += 8;
            if (byte_at(m_, next) == 0) {
                break;
            }
        }
    }
    const std::uint32_t name = word_at(m_, entry + 4);
    if (name == 0) {
        return false;
    }
    shell_helpers_pie_at(m_, self).set_field_30(1);
    check(global_byte(m_, 0x005615dc) == 0, 0x0052cf54);
    write_string(self + 4, retail_format(m_.cstring(0x005256dc, 0x20), {m_.cstring(name, 0x40)}, {}));
    engine(0x004b52b0, {global_word(m_, kSpooler)});
    shell_helpers_pie_at(m_, self).set_field_24(static_cast<Word>(engine_value(0x004b5370, {global_word(m_, kSpooler), self + 4, 0, 1, 0xffffffffu})));
    engine(0x004b5580, {global_word(m_, kSpooler)});
    return true;
}

// 0x00454a60: one frame of the screen: while the file loads a dark fade-in quad is drawn (two packets); when the spooler has the data it is handed to the .pre manager and
// the screen is done (true). Before loading starts it only counts its three frames down.
bool ShellHelpers::pie_update(std::uint32_t self) {
    engine(0x004b55c0, {global_word(m_, kSpooler)});
    if (shell_helpers_pie_at(m_, self).field_30() == 0) {
        if (static_cast<std::int32_t>(shell_helpers_pie_at(m_, self).field_28()) > 0) {
            shell_helpers_pie_at(m_, self).set_field_28(static_cast<Word>(shell_helpers_pie_at(m_, self).field_28() - 1));
        }
        return shell_helpers_pie_at(m_, self).field_28() == 0;
    }
    const std::uint32_t packet = global_word(m_, kArenaCursor);
    set_global_word(m_, kArenaCursor, packet + 0x1c);
    set_byte_at(m_, packet + 7, 0x28);
    std::int32_t fade = static_cast<std::int32_t>(shell_helpers_pie_at(m_, self).field_2c()) + 8;
    shell_helpers_pie_at(m_, self).set_field_2c(static_cast<Word>(u(fade)));
    if (fade > 0xff) {
        shell_helpers_pie_at(m_, self).set_field_2c(0xff);
    }
    auto signed_div = [](std::int32_t v, std::int32_t power) { return (v + ((v >> 31) & ((1 << power) - 1))) >> power; };
    set_byte_at(m_, packet + 4, u(signed_div(static_cast<std::int32_t>(shell_helpers_pie_at(m_, self).field_2c()), 3)));
    set_byte_at(m_, packet + 5, u(signed_div(static_cast<std::int32_t>(shell_helpers_pie_at(m_, self).field_2c()), 2)));
    set_byte_at(m_, packet + 6, shell_helpers_pie_at(m_, self).field_2c());
    const std::uint32_t height = global_half(m_, 0x00527d54);
    for (std::uint32_t k : {8u, 10u, 14u, 16u}) {
        set_half_at(m_, packet + k, 0);
    }
    set_half_at(m_, packet + 12, 0x200);
    set_half_at(m_, packet + 0x12, height);
    set_half_at(m_, packet + 0x14, 0x200);
    set_half_at(m_, packet + 0x16, height);
    set_byte_at(m_, packet + 7, byte_at(m_, packet + 7) | 2);
    const std::uint32_t width = global_word(m_, kScreenWidth);
    const std::uint32_t scale = global_word(m_, kScreenHeight);
    set_half_at(m_, packet + 8, 0);
    set_half_at(m_, packet + 10, 0);
    set_half_at(m_, packet + 12, (width << 9) >> 9);
    set_half_at(m_, packet + 14, 0);
    set_half_at(m_, packet + 0x10, 0);
    set_half_at(m_, packet + 0x12, div240(static_cast<std::int16_t>(half_at(m_, packet + 0x12)), scale));
    set_half_at(m_, packet + 0x14, (global_word(m_, kScreenWidth) << 9) >> 9);
    const std::int32_t bottom = static_cast<std::int16_t>(half_at(m_, packet + 0x16));
    set_word_at(m_, packet + 0x18, 0xbf800000);
    set_half_at(m_, packet + 0x16, div240(bottom, scale));
    link_main(packet);
    const std::uint32_t second = global_word(m_, kArenaCursor);
    set_global_word(m_, kArenaCursor, second + 0x10);
    engine(0x004e8940, {second, 0, 1, 0x140, 0});
    set_word_at(m_, second + 0xc, 0xbf800000);
    link_main(second);
    if ((engine_value(0x004b58a0, {global_word(m_, kSpooler), shell_helpers_pie_at(m_, self).field_24()}) & 0xff) == 0) {
        return false;
    }
    const std::uint32_t data = engine_value(0x004b5990, {global_word(m_, kSpooler), shell_helpers_pie_at(m_, self).field_24()});
    check(data != 0, 0x0052cf88);
    engine(0x004a91d0, {global_word(m_, 0x0056b834), data, self + 4});
    return true;
}

// 0x00454c80 / 0x00454c90: the slide table (eight triples) is cleared
void ShellHelpers::clear_slide_table() {
    for (std::uint32_t k = 0; k < 8; ++k) {
        set_word_at(m_, kPieWords - 8 + 12 * k, 0);
        set_word_at(m_, kPieWords - 4 + 12 * k, 0);
        set_word_at(m_, kPieWords + 12 * k, 0);
    }
}

// 0x00454cb0: call `function` (a thiscall without arguments) on `count` consecutive elements `stride` bytes apart
void ShellHelpers::iterate_vector(std::uint32_t base, std::uint32_t stride, std::uint32_t count, std::uint32_t function) {
    for (std::int32_t k = static_cast<std::int32_t>(count) - 1; k >= 0; --k) {
        engine(function, {base});
        base += stride;
    }
}

// 0x00454d10: a game movie: the track is set up, then the player runs (a tail jump)
void ShellHelpers::play_game_fmv(std::uint32_t track) {
    engine(0x00454d30, {track});
    engine(0x004e7090, {track});
}

// ---- the menu pad poll -----------------------------------------------------------------------------------------------------------------------------------------------

// 0x0046f550: the first menu press of the pad (0 up/accept ... 3, by the edge bytes the pad keeps), or the direction held (0..3) with a repeat delay; -1 for none. `pad` -1 asks
// both pads; `flag` makes the pause between repeats a fixed ten frames and keeps an idle pad's counter at ten; `repeat` is the delay before the first repeat.
std::int32_t ShellHelpers::menu_pad_poll(std::int32_t pad, std::uint32_t flag, std::uint32_t repeat) {
    std::int32_t result = -1;
    std::int32_t last = pad;
    if (pad == -1) {
        pad = 0;
        last = 1;
    }
    for (std::int32_t k = pad; k <= last; ++k) {
        const std::uint32_t base = 0x0056b089 + u(k) * 0x16c;
        const std::uint32_t counter = 0x0056a998 + u(k) * 4;
        if (byte_at(m_, base - 0x10) != 0) {
            set_byte_at(m_, base - 0x10, 0);
            result = 0;
        } else if (byte_at(m_, base) != 0) {
            set_byte_at(m_, base, 0);
            result = 1;
        } else if (byte_at(m_, base + 0x10) != 0) {
            set_byte_at(m_, base + 0x10, 0);
            result = 2;
        } else if (byte_at(m_, base + 0x20) != 0) {
            set_byte_at(m_, base + 0x20, 0);
            result = 3;
        }
        std::int32_t direction = -1;
        if (byte_at(m_, base - 0x11) != 0) {
            direction = 0;
        }
        if (byte_at(m_, base - 1) != 0) {
            direction = 1;
        }
        if (byte_at(m_, base + 0xf) != 0) {
            direction = 2;
        }
        bool held = true;
        if (byte_at(m_, base + 0x1f) != 0) {
            direction = 3;
        } else if (direction == -1) {
            held = false;
        }
        if (!held) {
            set_word_at(m_, counter, (flag & 0xff) != 0 ? 10 : 0);
        } else if (word_at(m_, counter) == 0) {
            result = direction;
            if ((flag & 0xff) == 0) {
                set_word_at(m_, counter, repeat);
            }
        } else {
            set_word_at(m_, counter, word_at(m_, counter) - 1);
        }
        if (result != -1) {
            return result;
        }
    }
    return result;
}

// 0x0046f650
std::int32_t ShellHelpers::menu_pad_poll_any() {
    const std::int32_t first = menu_pad_poll(0, 0, 6);
    return first == -1 ? menu_pad_poll(1, 0, 6) : first;
}

// ---- the old menu base class -----------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t ShellHelpers::base_construct_f80(std::uint32_t self) {
    set_word_at(m_, self, 0x00518f80);
    set_word_at(m_, self + 0x1c, 0);
    set_word_at(m_, self + 0x20, 0);
    return self;
}

std::uint32_t ShellHelpers::base_delete(std::uint32_t self, std::uint32_t flag) {
    base_destruct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void ShellHelpers::base_destruct(std::uint32_t self) { set_word_at(m_, self, kVtBase); }

// ---- panel.cpp ------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0048ab20: a line packet (0x14 bytes) if the list has room; the coordinates are the 512x240 panel's, scaled to the screen
void ShellHelpers::panel_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t color) {
    const std::uint32_t packet = global_word(m_, kArenaCursor);
    if (packet + 0x14 > global_word(m_, kArenaEnd)) {
        return;
    }
    line_unguarded(x0, y0, x1, y1, color);
}

// 0x004acf20: the same without the room check
void ShellHelpers::line_unguarded(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t color) {
    const std::uint32_t packet = global_word(m_, kArenaCursor);
    set_global_word(m_, kArenaCursor, packet + 0x14);
    const std::uint32_t width = global_word(m_, kScreenWidth);
    const std::uint32_t height = global_word(m_, kScreenHeight);
    set_word_at(m_, packet + 4, color + 0x40000000);
    set_word_at(m_, packet, 0x3000000);
    const std::int32_t ax = static_cast<std::int16_t>(x0), ay = static_cast<std::int16_t>(y0), bx = static_cast<std::int16_t>(x1), by = static_cast<std::int16_t>(y1);
    set_half_at(m_, packet + 8, u(ax) * width >> 9);
    set_half_at(m_, packet + 10, div240(ay, height));
    set_half_at(m_, packet + 12, u(bx) * width >> 9);
    set_word_at(m_, packet + 0x10, 0xbf800000);
    set_half_at(m_, packet + 14, div240(by, height));
    link_main(packet);
}

// 0x0048abf0: a horizontal meter: a bar of half-width `half_width` around `center` with a tick at each end and a marker at value/maximum of the half-width
void ShellHelpers::panel_balance_lr(std::int32_t value, std::int32_t maximum, std::int32_t half_width, std::int32_t center, std::int32_t y, std::uint32_t color, std::uint32_t marker) {
    const std::int32_t right = half_width + center;
    const std::int32_t left = center - half_width;
    panel_line(left, y, right, y, color);
    panel_line(left, y, left, y - 4, color);
    panel_line(right, y, right, y - 4, color);
    const std::int32_t at = static_cast<std::int32_t>((static_cast<std::int64_t>(value) * half_width) / maximum) + center;
    panel_line(at, y, at, y - 4, marker);
}

// 0x0048ac60: the vertical meter: a marker line at value/maximum of the half-height, a frame in `color`, then the frame's shadow one pixel on in a dimmer colour
void ShellHelpers::panel_balance_ud(std::int32_t value, std::int32_t maximum, std::int32_t half_height, std::int32_t x, std::int32_t center, std::uint32_t color, std::uint32_t marker) {
    std::int32_t at = static_cast<std::int32_t>((static_cast<std::int64_t>(value) * half_height) / maximum) + center;
    panel_line(x, at, x + 6, at, marker);
    std::int32_t bottom = center + half_height;
    std::int32_t top = center - half_height;
    const std::uint32_t dim = (color >> 3) & 0x1f1f1f;
    panel_line(x, top, x, bottom, color);
    panel_line(x, top, x + 6, top, color);
    panel_line(x, center, x + 6, center, color);
    panel_line(x, bottom, x + 6, bottom, color);
    top += 1;
    bottom += 1;
    const std::int32_t inner = x + 1;
    panel_line(inner, top, inner, bottom, dim);
    const std::int32_t far = x + 7;
    panel_line(inner, top, far, top, dim);
    panel_line(inner, center + 1, far, center + 1, dim);
    panel_line(inner, bottom, far, bottom, dim);
}

// ---- rfront.cpp ------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004ac370: the direction a held pad button repeats in (1 down, -1 up, 0 none): the first press at once, then every fourth frame after sixteen
std::int32_t ShellHelpers::pad_repeat_direction() {
    const std::uint8_t up = global_byte(m_, 0x0056b098), up2 = global_byte(m_, 0x0056b204), down = global_byte(m_, 0x0056b0a8), down2 = global_byte(m_, 0x0056b214);
    if (up == 0 && up2 == 0 && down == 0 && down2 == 0) {
        set_global_word(m_, 0x00561c14, 0);
        return 0;
    }
    std::int32_t result = 0;
    // a count is a call (retail), or a 30 Hz frame of time at 60 and 120 (framepacing::hold_steps: the high score letters at the end of a run are asked once a scene)
    const std::uint32_t steps =
        framepacing::hold_steps(framepacing::pad_repeat_clock(), global_word(m_, 0x00561c14) == 0, [&] { return global_word(m_, 0x0056e31c); });
    bool act = false;
    for (std::uint32_t step = 0; step < steps; ++step) {
        const std::int32_t count = static_cast<std::int32_t>(global_word(m_, 0x00561c14));
        bool now = count == 0;
        if (!now && count > 0xf) {
            std::uint32_t low = global_word(m_, 0x00561c14) & 0x80000003u;
            if (static_cast<std::int32_t>(low) < 0) {
                low = ((low - 1) | 0xfffffffcu) + 1;
            }
            now = low == 0;
        }
        act = act || now;
        set_global_word(m_, 0x00561c14, global_word(m_, 0x00561c14) + 1);
    }
    if (act) {
        if (down != 0 || down2 != 0) {
            result = 1;
        }
        if (up != 0 || up2 != 0) {
            result = -1;
        }
    }
    return result;
}

// ---- one-liners -----------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004266a0
std::uint32_t ShellHelpers::credits_alloc_zeroed(std::uint32_t size) {
    const std::uint32_t block = engine_value(0x0046f490, {size, 0, 1, 0});
    const std::uint32_t words = (size + 3) / 4;
    for (std::uint32_t k = 0; k < words; ++k) {
        set_word_at(m_, block + 4 * k, 0);
    }
    return block;
}

// 0x004266d0
void ShellHelpers::credits_free(std::uint32_t block) { engine(0x0046f4d0, {block}); }

// 0x0044aa70: the width of a string in a font: the sum of its characters' advances
std::uint32_t ShellHelpers::font_string_width(std::uint32_t font, std::uint32_t text) {
    std::uint32_t width = 0;
    for (std::uint32_t at = text; byte_at(m_, at) != 0; ++at) {
        width += engine_value(0x0044aaa0, {font, byte_at(m_, at)});
    }
    return width;
}

// 0x00452e10
void ShellHelpers::set_handicap_names() {
    set_global_word(m_, 0x00561cd0, global_word(m_, 0x0052e91c));
    set_global_word(m_, 0x00561cd4, global_word(m_, 0x0052e918));
    set_global_word(m_, 0x00561cd8, global_word(m_, 0x0052e914));
}

// 0x004b0b20
void ShellHelpers::shell_utils_tail() {
    // `mov ecx, 0x56c580; jmp 0x4b0b50`: RecordBank_Construct on the skater manager's record bank
    engine(0x004b0b50, s_.live() ? std::vector<std::uint32_t>{0x0056c580} : std::vector<std::uint32_t>{});
}

// ---- the effect object ------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004b9320
std::uint32_t ShellHelpers::effect_delete(std::uint32_t self, std::uint32_t flag) {
    effect_destruct(self);
    if ((flag & 1) != 0) {
        credits_free(self);       // (0x0047fd60 and 0x004266d0 are the same function: Mem_Delete)
    }
    return self;
}

// 0x004b9340: the object leaves the power-up list, its child goes, the base class is destroyed
void ShellHelpers::effect_destruct(std::uint32_t self) {
    shell_helpers_effect_at(m_, self).set_field_0(0x0051979c);
    engine(0x004801f0, {self, 0x0056b830});
    effect_free_child(self);
    engine(0x00480160, {self});
}

// 0x004b93a0: marked for removal (flags 0x41), then its own virtual method 1 runs
void ShellHelpers::effect_kill(std::uint32_t self) {
    shell_helpers_effect_at(m_, self).set_field_4(static_cast<std::uint8_t>(shell_helpers_effect_at(m_, self).field_4() | 0x41));
    vcall_item("virtual_slot1", self, 1);
}

void ShellHelpers::effect_set_extra(std::uint32_t self, std::uint32_t value, std::uint32_t flag) {
    shell_helpers_effect_at(m_, self).set_field_5c(static_cast<Word>(value));
    shell_helpers_effect_at(m_, self).set_field_6d(static_cast<std::uint8_t>(flag));
}

void ShellHelpers::effect_free_child(std::uint32_t self) {
    const std::uint32_t child = shell_helpers_effect_at(m_, self).field_c8();
    if (child != 0) {
        delete_object(child);
    }
    shell_helpers_effect_at(m_, self).set_field_c8(0);
}

void ShellHelpers::effect_set_frames(std::uint32_t self, std::uint32_t frames) {
    shell_helpers_effect_at(m_, self).set_field_108(1);
    shell_helpers_effect_at(m_, self).set_field_10e(static_cast<std::uint16_t>(frames));
}

// 0x004b9420: one frame of the wobble: the position moves by the velocity (and is masked to 12 bits), the anchor becomes the base position, and the height gets the sine of the
// angle times the amplitude, rounded to whole units of 4096
void ShellHelpers::effect_move(std::uint32_t self) {
    engine(0x004cae10, {self + 0x14, self + 0x70});
    engine(0x004cad60, {self + 0x14});
    shell_helpers_effect_at(m_, self).set_field_8(static_cast<Word>(shell_helpers_effect_at(m_, self).field_114()));
    shell_helpers_effect_at(m_, self).set_field_c(static_cast<Word>(shell_helpers_effect_at(m_, self).field_118()));
    shell_helpers_effect_at(m_, self).set_field_10(static_cast<Word>(shell_helpers_effect_at(m_, self).field_11c()));
    const std::int32_t sine = static_cast<std::int32_t>(engine_value(0x004f39b0, {static_cast<std::uint32_t>(shell_helpers_effect_at(m_, self).field_120() & 0xfff)}));
    const std::int32_t product = sine * static_cast<std::int32_t>(shell_helpers_effect_at(m_, self).field_124());
    const std::int32_t rounding = (product >> 31) & 0xfff;
    shell_helpers_effect_at(m_, self).set_field_120(static_cast<std::uint16_t>(shell_helpers_effect_at(m_, self).field_120() + shell_helpers_effect_at(m_, self).field_126()));
    shell_helpers_effect_at(m_, self).set_field_c(static_cast<Word>(shell_helpers_effect_at(m_, self).field_c() + u(((product + rounding) >> 12) << 12)));
}

// ---- the object record ------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0047fdb0
std::uint32_t ShellHelpers::object_record_init(std::uint32_t self) {
    set_word_at(m_, self + 8, 0);
    set_word_at(m_, self + 0xc, 0);
    set_word_at(m_, self + 0x10, 0);
    for (std::uint32_t offset : {0x14u, 0x16u, 0x18u}) {
        set_half_at(m_, self + offset, 0);
    }
    set_word_at(m_, self, 0x00519388);
    for (std::uint32_t offset : {0x28u, 0x2au, 0x2cu}) {
        set_half_at(m_, self + offset, 0x1000);
    }
    set_word_at(m_, self + 0x40, 0x00533910);
    return self;
}

// 0x0047fe00
std::uint32_t ShellHelpers::object_record_delete(std::uint32_t self, std::uint32_t flag) {
    object_record_destruct(self);
    if ((flag & 1) != 0) {
        credits_free(self);
    }
    return self;
}

// ---- the text window ------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004ba7a0: a text window: its colours, its rectangle (x, y, width, height), a line array of (height - 8) / 8 entries, put on the head of the window list
std::uint32_t ShellHelpers::textwin_construct(std::uint32_t self, std::uint32_t owner, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    shell_helpers_textwin_at(m_, self).set_field_0(static_cast<Word>(kVtTextWindow));
    shell_helpers_textwin_at(m_, self).set_field_b(0x28);
    const std::uint32_t colors[15] = {0x32, 0x32, 0x32, 0x46, 0x46, 0, 0xff, 0xff, 0xff, 0, 0xff, 0, 0, 0, 0};
    textwin_set_colors(self, colors);
    textwin_set_rect(self, x, y, width, height);
    const std::int32_t rows = height - 8;
    shell_helpers_textwin_at(m_, self).set_field_50(static_cast<Word>(u((rows + ((rows >> 31) & 7)) >> 3)));
    shell_helpers_textwin_at(m_, self).set_field_48(static_cast<Word>(global_word(m_, kTextWindows)));
    shell_helpers_textwin_at(m_, self).set_field_4c(0);
    set_global_word(m_, kTextWindows, self);
    if (shell_helpers_textwin_at(m_, self).field_48() != 0) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_48() + 0x4c, self);
    }
    shell_helpers_textwin_at(m_, self).set_field_30(static_cast<Word>(engine_value(0x0046f490, {shell_helpers_textwin_at(m_, self).field_50() << 2, 0, 1, 0})));
    shell_helpers_textwin_at(m_, self).set_field_34(static_cast<Word>(engine_value(0x0046f490, {shell_helpers_textwin_at(m_, self).field_50(), 0, 1, 0})));
    textwin_clear(self);
    shell_helpers_textwin_at(m_, self).set_field_44(static_cast<Word>(owner));
    return self;
}

std::uint32_t ShellHelpers::textwin_delete(std::uint32_t self, std::uint32_t flag) {
    textwin_destruct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004ba8c0: the window leaves the list, its two arrays go
void ShellHelpers::textwin_destruct(std::uint32_t self) {
    shell_helpers_textwin_at(m_, self).set_field_0(static_cast<Word>(kVtTextWindow));
    const std::uint32_t previous = shell_helpers_textwin_at(m_, self).field_48();
    const std::uint32_t next = shell_helpers_textwin_at(m_, self).field_4c();
    if (previous != 0) {
        set_word_at(m_, previous + 0x4c, next);
    }
    if (next != 0) {
        set_word_at(m_, next + 0x48, previous);
    }
    if (global_word(m_, kTextWindows) == self) {
        set_global_word(m_, kTextWindows, shell_helpers_textwin_at(m_, self).field_48());
    }
    engine(0x0046f4d0, {shell_helpers_textwin_at(m_, self).field_30()});
    engine(0x0046f4d0, {shell_helpers_textwin_at(m_, self).field_34()});
    shell_helpers_textwin_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x004ba940: the rectangle: the vertices in panel units, then scaled to the screen
void ShellHelpers::textwin_set_rect(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    const std::int16_t x16 = static_cast<std::int16_t>(x), y16 = static_cast<std::int16_t>(y);
    const std::int16_t right = static_cast<std::int16_t>(static_cast<std::int16_t>(width) + x16);
    const std::int16_t bottom = static_cast<std::int16_t>(static_cast<std::int16_t>(height) + y16);
    const std::uint32_t screen_w = global_word(m_, kScreenWidth);
    const std::uint32_t screen_h = global_word(m_, kScreenHeight);
    shell_helpers_textwin_at(m_, self).set_field_16(static_cast<std::uint16_t>(u(bottom)));
    shell_helpers_textwin_at(m_, self).set_field_1a(static_cast<std::uint16_t>(u(bottom)));
    shell_helpers_textwin_at(m_, self).set_field_c(static_cast<std::uint16_t>(u(x16)));
    shell_helpers_textwin_at(m_, self).set_field_e(static_cast<std::uint16_t>(u(y16)));
    shell_helpers_textwin_at(m_, self).set_field_10(static_cast<std::uint16_t>(u(right)));
    shell_helpers_textwin_at(m_, self).set_field_12(static_cast<std::uint16_t>(u(y16)));
    shell_helpers_textwin_at(m_, self).set_field_14(static_cast<std::uint16_t>(u(x16)));
    shell_helpers_textwin_at(m_, self).set_field_18(static_cast<std::uint16_t>(u(right)));
    shell_helpers_textwin_at(m_, self).set_field_c(static_cast<std::uint16_t>(u(x16) * screen_w >> 9));
    shell_helpers_textwin_at(m_, self).set_field_e(static_cast<std::uint16_t>(div240(y16, screen_h)));
    shell_helpers_textwin_at(m_, self).set_field_10(static_cast<std::uint16_t>(u(right) * screen_w >> 9));
    shell_helpers_textwin_at(m_, self).set_field_12(static_cast<std::uint16_t>(div240(y16, screen_h)));
    shell_helpers_textwin_at(m_, self).set_field_14(static_cast<std::uint16_t>(u(x16) * screen_w >> 9));
    shell_helpers_textwin_at(m_, self).set_field_16(static_cast<std::uint16_t>(div240(bottom, screen_h)));
    shell_helpers_textwin_at(m_, self).set_field_18(static_cast<std::uint16_t>(u(right) * screen_w >> 9));
    shell_helpers_textwin_at(m_, self).set_field_24(static_cast<Word>(u(y)));
    shell_helpers_textwin_at(m_, self).set_field_1a(static_cast<std::uint16_t>(div240(bottom, screen_h)));
    shell_helpers_textwin_at(m_, self).set_field_20(static_cast<Word>(u(x)));
    shell_helpers_textwin_at(m_, self).set_field_28(static_cast<Word>(u(width)));
    shell_helpers_textwin_at(m_, self).set_field_2c(static_cast<Word>(u(height)));
}

// 0x004baa50: the background colour and the four corner colours (bytes at +8..+10 and +0x38..+0x43)
void ShellHelpers::textwin_set_colors(std::uint32_t self, const std::uint32_t (&c)[15]) {
    shell_helpers_textwin_at(m_, self).set_field_8(static_cast<std::uint8_t>(c[0]));
    shell_helpers_textwin_at(m_, self).set_field_9(static_cast<std::uint8_t>(c[1]));
    shell_helpers_textwin_at(m_, self).set_field_a(static_cast<std::uint8_t>(c[2]));
    shell_helpers_textwin_at(m_, self).set_field_38(static_cast<std::uint8_t>(c[3]));
    shell_helpers_textwin_at(m_, self).set_field_3c(static_cast<std::uint8_t>(c[4]));
    shell_helpers_textwin_at(m_, self).set_field_40(static_cast<std::uint8_t>(c[5]));
    shell_helpers_textwin_at(m_, self).set_field_39(static_cast<std::uint8_t>(c[6]));
    shell_helpers_textwin_at(m_, self).set_field_3d(static_cast<std::uint8_t>(c[7]));
    shell_helpers_textwin_at(m_, self).set_field_41(static_cast<std::uint8_t>(c[8]));
    shell_helpers_textwin_at(m_, self).set_field_3a(static_cast<std::uint8_t>(c[9]));
    shell_helpers_textwin_at(m_, self).set_field_3e(static_cast<std::uint8_t>(c[10]));
    shell_helpers_textwin_at(m_, self).set_field_42(static_cast<std::uint8_t>(c[11]));
    shell_helpers_textwin_at(m_, self).set_field_3b(static_cast<std::uint8_t>(c[12]));
    shell_helpers_textwin_at(m_, self).set_field_3f(static_cast<std::uint8_t>(c[13]));
    shell_helpers_textwin_at(m_, self).set_field_43(static_cast<std::uint8_t>(c[14]));
}

void ShellHelpers::textwin_set_owner(std::uint32_t self, std::uint32_t owner) { set_word_at(m_, self + 0x44, owner); }

// 0x004baad0: no lines
void ShellHelpers::textwin_clear(std::uint32_t self) {
    for (std::int32_t k = 0; k < static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()); ++k) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k), 0);
        set_byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k), 0);
    }
}

void ShellHelpers::textwin_set_line(std::uint32_t self, std::int32_t line, std::uint32_t text) {
    check(line >= 0 && line < static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()), 0x00540bf4);
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(line), text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34() + u(line);
    set_byte_at(m_, flags, byte_at(m_, flags) & 0xfb);
}

void ShellHelpers::textwin_set_line_marked(std::uint32_t self, std::int32_t line, std::uint32_t text) {
    check(line >= 0 && line < static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()), 0x00540bf4);
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(line), text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34() + u(line);
    set_byte_at(m_, flags, byte_at(m_, flags) | 4);
}

void ShellHelpers::textwin_push_line(std::uint32_t self, std::uint32_t text) {
    for (std::int32_t k = 1; k < static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()); ++k) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k - 1), word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k)));
        set_byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k - 1), byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k)));
    }
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * shell_helpers_textwin_at(m_, self).field_50() - 4, text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34() + shell_helpers_textwin_at(m_, self).field_50() - 1;
    set_byte_at(m_, flags, byte_at(m_, flags) & 0xfb);
}

void ShellHelpers::textwin_push_line_marked(std::uint32_t self, std::uint32_t text) {
    for (std::int32_t k = 1; k < static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()); ++k) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k - 1), word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k)));
        set_byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k - 1), byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k)));
    }
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * shell_helpers_textwin_at(m_, self).field_50() - 4, text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34() + shell_helpers_textwin_at(m_, self).field_50() - 1;
    set_byte_at(m_, flags, byte_at(m_, flags) | 4);
}

void ShellHelpers::textwin_insert_line(std::uint32_t self, std::uint32_t text) {
    for (std::int32_t k = static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()) - 2; k >= 0; --k) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 + 4 * u(k), word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k)));
        set_byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k) + 1, byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k)));
    }
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30(), text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34();
    set_byte_at(m_, flags, byte_at(m_, flags) & 0xfb);
}

void ShellHelpers::textwin_insert_line_marked(std::uint32_t self, std::uint32_t text) {
    for (std::int32_t k = static_cast<std::int32_t>(shell_helpers_textwin_at(m_, self).field_50()) - 2; k >= 0; --k) {
        set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 + 4 * u(k), word_at(m_, shell_helpers_textwin_at(m_, self).field_30() + 4 * u(k)));
        set_byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k) + 1, byte_at(m_, shell_helpers_textwin_at(m_, self).field_34() + u(k)));
    }
    set_word_at(m_, shell_helpers_textwin_at(m_, self).field_30(), text);
    const std::uint32_t flags = shell_helpers_textwin_at(m_, self).field_34();
    set_byte_at(m_, flags, byte_at(m_, flags) | 4);
}

// 0x004badd0: walks the window list to its end (and returns nothing)
std::uint32_t ShellHelpers::textwin_last() {
    std::uint32_t at = global_word(m_, kTextWindows);
    while (at != 0) {
        at = word_at(m_, at + 0x48);
    }
    return 0;
}

// 0x004badf0: every window's panel goes on the viewport's list, at the depth of the first viewport (or the front when the viewport is the main one)
void ShellHelpers::textwin_draw_all() {
    for (std::uint32_t window = global_word(m_, kTextWindows); window != 0; window = word_at(m_, window + 0x48)) {
        const std::uint32_t renderer = global_word(m_, kRenderer);
        if (word_at(m_, renderer + 0x88) == word_at(m_, renderer + 0x84)) {
            set_word_at(m_, window + 0x1c, 0xbf800000);
        } else {
            const float scale = [&] {
                const std::uint32_t bits = global_word(m_, 0x005189fc);
                float f;
                std::memcpy(&f, &bits, 4);
                return f;
            }();
            const float depth = static_cast<float>(half_at(m_, global_word(m_, 0x00563a38) + 8)) * scale;
            std::uint32_t bits;
            std::memcpy(&bits, &depth, 4);
            set_word_at(m_, window + 0x1c, bits);
        }
        const std::uint32_t packet = window + 4;
        const std::uint32_t viewport = word_at(m_, global_word(m_, kRenderer) + 0x88);
        set_word_at(m_, packet, word_at(m_, viewport + 8));
        set_word_at(m_, word_at(m_, global_word(m_, kRenderer) + 0x88) + 8, packet);
    }
}

// 0x004bae70: the data is xor-ed with the 400-byte key
void ShellHelpers::xor_decrypt(std::uint32_t data, std::uint32_t size) {
    const std::uint32_t key = engine_value(0x00418140);
    for (std::uint32_t k = 0; k < size; ++k) {
        set_byte_at(m_, data + k, byte_at(m_, data + k) ^ byte_at(m_, key + k % 400));
    }
}

}  // namespace opentony::runtime::frontend
