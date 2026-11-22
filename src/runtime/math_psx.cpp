#include "math_views.hpp"
#include "seam_addresses_generated.hpp"

#include <cstring>
#include <string>

// Phase 4.14a: the PSX library stubs of the PC build (0x004e86f0..0x004e8d3f: the calls the PS1 code makes into libgpu / libetc / libcard, which the PC build leaves empty or answers with a constant), the frame
// clock that follows the C runtime's `clock()`, and the test the park editor uses to see whether a park file exists.

namespace opentony::runtime::mathkit {

using namespace views;

namespace {

constexpr std::uint32_t kCardFlag = 0x006a74bc;      // byte
constexpr std::uint32_t kCardState = 0x006a74c0;     // dword
constexpr std::uint32_t kFrameClock = 0x0056e31c;    // the frame counter
constexpr std::uint32_t kGameClock = 0x0056e320;     // the counter that only runs while the game is not paused
constexpr std::uint32_t kPauseFlag = 0x00561c04;
constexpr std::uint32_t kPauseFlag2 = 0x0056a8e0;
constexpr std::uint32_t kLastClock = 0x029d836c;     // the C clock at the last update
constexpr std::uint32_t kTimerActive = 0x004dab30;   // D3DTIMER_IsActive
constexpr std::uint32_t kClock = seam::crt::FUN_00502ccb;         // the C runtime's clock()
constexpr std::uint32_t kParkExtension = 0x0055bd08; // ".PRK"
constexpr std::uint32_t kReadMode = 0x00549d3c;      // "r"

}  // namespace

// 0x004e87f0: a draw-area primitive: code 0xe3 at +7, the rectangle at +8 with a width and height of at least 1
void MathKit::psx_init_rect_primitive(std::uint32_t primitive_address, std::uint32_t rect_address) {
    const RuntimePsxDrawAreaPrimitiveView primitive(m_, primitive_address);
    const RuntimePsxRectView source(m_, rect_address);
    const RuntimePsxRectView area = primitive.rect();
    primitive.set_code_word(0xe3000000u);
    area.set_x(source.x());
    area.set_y(source.y());
    const std::uint16_t width = source.width();
    area.set_width(width);
    area.set_height(source.height());
    if (width == 0) {
        area.set_width(1);
    }
    if (area.height() == 0) {
        area.set_height(1);
    }
}

// 0x004e88a0 / 0x004e88f0: LoadClut: the rectangle {x, y, width, 1} goes to the (empty) LoadImage stub; the answer is the CLUT id
std::uint32_t MathKit::psx_load_clut(std::uint32_t, std::uint32_t x, std::uint32_t y, std::uint32_t) {
    return (((static_cast<std::int32_t>(x) >> 4) & 0x3f) | (y << 6));
}

// (four dwords cleared and the command byte at +7 set: the code word ends up 0xe0000000)
void MathKit::psx_init_terminator(std::uint32_t primitive_address) {
    const RuntimePsxDrawAreaPrimitiveView primitive(m_, primitive_address);
    const RuntimePsxRectView area = primitive.rect();
    primitive.set_tag(0);
    primitive.set_code_word(0xe0000000u);
    area.set_x(0);
    area.set_y(0);
    area.set_width(0);
    area.set_height(0);
}

std::uint32_t MathKit::card_set_busy() {
    write_scalar<std::uint8_t>(m_, kCardFlag, 0);
    write_scalar<std::uint32_t>(m_, kCardState, 1);
    return 1;
}

std::uint32_t MathKit::card_set_done() {
    write_scalar<std::uint8_t>(m_, kCardFlag, 0);
    write_scalar<std::uint32_t>(m_, kCardState, 2);
    return 0;
}

std::uint32_t MathKit::card_get_state(std::uint32_t, std::uint32_t state_out, std::uint32_t flag_out) {
    write_scalar<std::uint32_t>(m_, state_out, read_scalar<std::uint32_t>(m_, kCardState));
    write_scalar<std::uint32_t>(m_, flag_out, read_scalar<std::uint8_t>(m_, kCardFlag));
    return 1;
}

// 0x004f5ff0: the game clocks advance by 60 ticks per second of C `clock()` (milliseconds), unless the multimedia timer drives them
void MathKit::update_frame_clock() {
    if ((engine_value(kTimerActive) & 0xff) != 0) {
        return;
    }
    const std::uint32_t now = engine_value(kClock);
    std::uint32_t last = read_scalar<std::uint32_t>(m_, kLastClock);
    if (last == 0) {
        last = now;
    }
    const std::int32_t delta = static_cast<std::int32_t>((now - last) * 60u) / 1000;
    if (delta > 0) {
        write_scalar<std::uint32_t>(m_, kFrameClock, read_scalar<std::uint32_t>(m_, kFrameClock) + static_cast<std::uint32_t>(delta));
        if (read_scalar<std::uint32_t>(m_, kPauseFlag) == 0 && read_scalar<std::uint32_t>(m_, kPauseFlag2) == 0) {
            write_scalar<std::uint32_t>(m_, kGameClock, read_scalar<std::uint32_t>(m_, kGameClock) + static_cast<std::uint32_t>(delta));
        }
    }
    write_scalar<std::uint32_t>(m_, kLastClock, now);
}

// 0x004dace0 (from the disassembly; the typed model is src/camera/camera_timing.hpp): one tick of the multimedia timer. The step is the interval (ms, +4 of the
// timer state) times the two doubles at 0x00519980 and 0x00519978; the state's +0xc counts the milliseconds; the frame accumulator (0x006a0590) always moves, the
// game accumulator (0x006a0598) unless a pause flag is set, and the two clocks are their truncations. Double arithmetic, as on the winmm thread (53 bits).
void MathKit::timer_callback(std::uint32_t state_address) {
    constexpr std::uint32_t kStep = 0x006a05b8;
    constexpr std::uint32_t kFrameAccumulator = 0x006a0590;
    constexpr std::uint32_t kGameAccumulator = 0x006a0598;
    const RetailTimerStateView state(m_, state_address);
    auto f64 = [&](std::uint32_t address) { return read_scalar<double>(m_, address); };
    auto set_f64 = [&](std::uint32_t address, double value) { write_scalar<double>(m_, address, value); };
    using x87::Precision;
    using x87::round_to;
    const std::uint32_t interval = state.interval_ms();
    const long double step = round_to(Precision::k53, round_to(Precision::k53, static_cast<long double>(interval) * f64(0x00519980)) * f64(0x00519978));
    set_f64(kStep, static_cast<double>(step));
    state.set_accumulated_ms(state.accumulated_ms() + interval);
    set_f64(kFrameAccumulator, static_cast<double>(round_to(Precision::k53, static_cast<long double>(f64(kFrameAccumulator)) + f64(kStep))));
    if (read_scalar<std::uint32_t>(m_, kPauseFlag) == 0 && read_scalar<std::uint32_t>(m_, kPauseFlag2) == 0) {
        set_f64(kGameAccumulator, static_cast<double>(round_to(Precision::k53, static_cast<long double>(f64(kGameAccumulator)) + f64(kStep))));
    }
    write_scalar<std::uint32_t>(m_, kFrameClock, static_cast<std::uint32_t>(x87::ftol(f64(kFrameAccumulator))));
    write_scalar<std::uint32_t>(m_, kGameClock, static_cast<std::uint32_t>(x87::ftol(f64(kGameAccumulator))));
}

// 0x004f6080: cut the path at its first dot and append the extension
std::uint32_t MathKit::change_extension(std::uint32_t path, std::uint32_t extension) {
    std::string text = m_.cstring(path, 0x1000);
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        write_scalar<std::uint8_t>(m_, path + static_cast<std::uint32_t>(dot), 0);
        text.resize(dot);
    }
    const std::string suffix = m_.cstring(extension, 0x1000);
    std::vector<std::uint8_t> bytes(suffix.begin(), suffix.end());
    bytes.push_back(0);
    m_.write(path + static_cast<std::uint32_t>(text.size()), bytes);
    return path;
}

// 0x004f60d0: does `name` + ".PRK" (the name cut at its first dot) open for reading?
bool MathKit::park_file_exists(std::uint32_t name) {
    std::string text = m_.cstring(name, 0x1000);
    const auto dot = text.find('.');
    if (dot != std::string::npos) {
        text.resize(dot);
    }
    text += m_.cstring(kParkExtension, 0x100);
    const std::uint32_t file = crt_fopen(text, pcplatform::kStackLocal, kReadMode);
    if (file == 0) {
        return false;
    }
    crt_fclose(file);
    return true;
}

}  // namespace opentony::runtime::mathkit
