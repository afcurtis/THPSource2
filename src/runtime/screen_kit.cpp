#include "auto_views.hpp"
#include "screen_kit.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

}  // namespace

std::string retail_format(const std::string& format, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers) {
    std::string out;
    std::size_t next_string = 0;
    std::size_t next_number = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') {
            out.push_back(format[i]);
            continue;
        }
        ++i;
        bool zero = false;
        if (i < format.size() && format[i] == '0') {
            zero = true;      // "%03d": pad with zeros
            ++i;
        }
        std::size_t width = 0;
        while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + static_cast<std::size_t>(format[i] - '0');
            ++i;
        }
        while (i < format.size() && (format[i] == 'l' || format[i] == 'h')) {
            ++i;      // a length modifier ("%ld") changes nothing here: arguments are 32 bits
        }
        if (i >= format.size()) {
            break;
        }
        std::string piece;
        if (format[i] == 'd') {
            piece = std::to_string(numbers.at(next_number++));
        } else if (format[i] == 's') {
            piece = strings.at(next_string++);
        } else {
            piece = std::string(1, format[i]);
        }
        if (piece.size() < width) {
            piece.insert(0, width - piece.size(), zero ? '0' : ' ');
        }
        out += piece;
    }
    return out;
}

void ScreenKit::write_string(std::uint32_t dest, const std::string& text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(dest, bytes);
}

std::uint32_t ScreenKit::scratch_line(const std::string& text) {
    m_.ensure_region(kScratchLine, 0x100);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kScratchLine, bytes);
    return kScratchLine;
}

std::uint32_t ScreenKit::scratch_text(std::uint32_t offset, const std::string& text) {
    m_.ensure_region(kScratchLine, 0x100);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kScratchLine + offset, bytes);
    return kScratchLine + offset;
}

// A prompt window (a button picture with its caption) is built by the engine and added to the window by the caller.
std::uint32_t ScreenKit::prompt(std::uint32_t file, std::uint32_t caption, std::uint32_t font, std::int32_t x, std::int32_t y) {
    const std::uint32_t memory = operator_new(0xb0);
    std::uint32_t window = 0;
    if (memory != 0) {
        s_.note(kSvcPromptConstruct, {memory, file, caption, font, u(x), u(y), 0xffffffffu, global_word(m_, kScreenFillHeight) + 0x14}, m_.cstring(file));
        window = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
    }
    return window;
}

// The background picture (s2bgd4.bmp): raw `operator new`, the bitmap opened by the engine, then the sprite constructor.
std::uint32_t ScreenKit::background_sprite(std::uint32_t name) {
    const std::uint32_t memory = crt_new(0x1c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSvcFileOpen, {name, 1}, m_.cstring(name));
        const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(kSvcFileOpen));
        sprite = engine_value(seam::hal::D3DSprite_Create, {memory, handle, name, 1});
    }
    return sprite;
}

// The sprite every screen's draw ends with: the background, drawn with the renderer's viewport.
void ScreenKit::draw_background(std::uint32_t sprite) {
    if (sprite != 0) {
        engine(0x004d7990, {sprite, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
    }
}

// A four-word view (width, height, x, y, near, far, ...): the full-screen camera of the editors.
void ScreenKit::init_view(std::uint32_t view) {
    set_half_at(m_, view, global_word(m_, kGlobalScreenWidth) & 0x7fffff);
    set_half_at(m_, view + 2, (global_word(m_, kGlobalScreenHeight) * 0xf0u) / 0xf0u);
    set_half_at(m_, view + 4, 0);
    set_half_at(m_, view + 6, 0);
    set_half_at(m_, view + 8, 10);
    set_half_at(m_, view + 10, 30000);
    set_half_at(m_, view + 12, 0x6aa);
}

// Game_FrameTick: the engine advances the simulation clock; the port reads the new value from the script.
void ScreenKit::game_frame_tick() {
    engine(seam::hal::Game_FrameTick);
    set_global_word(m_, kGlobalSimTick, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::Game_FrameTick))));
}

// The frame loop the screen loops share. `step` polls the screen (1 or 2 ends it), `update` and `draw` are its two virtual methods.
std::int32_t ScreenKit::frame_loop(const std::function<std::int32_t()>& step, const std::function<void()>& update,
                                   const std::function<void()>& draw) {
    std::int32_t result = 0;
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00489a10);
        result = step();
        if (result == 1 || result == 2) {
            break;
        }
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        update();
        draw();
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        if (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2) {
            do {
                game_frame_tick();
            } while (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2);
        }
        set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
        engine(0x00430510);
        engine(0x00466c00);
    }
    return result;
}

}  // namespace opentony::runtime::frontend
