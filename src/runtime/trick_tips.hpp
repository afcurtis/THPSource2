#pragma once

#include "retail_image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.7b: the in-game trick tips (tricktips.cpp). The retail code keeps a 0x114-byte monitor
// object in the data segment at 0x0056db98 (CTrickTipMonitor) and four tip-mask dwords at
// 0x0056a40c; the functions here run over a byte image of that object so a captured image can be
// replayed word for word.
//
//   FUN_004c4570  trick_tips_reset     level start: clear the counters
//   FUN_004c4620  trick_tips_monitor   once per frame: watch the skater's counters, pick a tip
//   FUN_004c4950  trick_tips_select    show tip n (heading, three lines, 200-frame timer)
//   FUN_004c48f0  trick_tip_bank       which of the two mask words holds tip n
//   0x004c4420    trick_tips_display   the slide-in panel, as Mess_* drawing calls
constexpr std::uint32_t kTipMonitorAddress = 0x0056db98;
constexpr std::size_t kTipMonitorSize = 0x114;
constexpr std::size_t kTipCount = 22;       // the table at 0x00518b98
constexpr std::int32_t kTipDisplayFrames = 200;

// Monitor object offsets (retail address - 0x0056db98).
namespace tip_field {
constexpr std::size_t kFrames = 0x58;          // 0x56dbf0 frames since level start (0 = first frame)
constexpr std::size_t kLastTipFrame = 0x5c;    // 0x56dbf4 the frame the last tip was picked on
constexpr std::size_t kSinceReset = 0x60;      // 0x56dbf8
constexpr std::size_t kPlayer = 0x64;          // 0x56dbfc the skater being watched
constexpr std::size_t kTitle = 0x90;           // 0x56dc28 heading string pointer (0 = nothing shown)
constexpr std::size_t kLine1 = 0x94;           // 0x56dc2c
constexpr std::size_t kLine2 = 0x98;           // 0x56dc30
constexpr std::size_t kLine3 = 0x9c;           // 0x56dc34
constexpr std::size_t kTimer = 0xa0;           // 0x56dc38 display frames left
}  // namespace tip_field

// One row of the tip table: the state/kind words and the three text lines (pointers are the
// executable's own; the text is kept alongside for readers). Generated from the executable.
struct TrickTipText final {
    std::uint32_t state{};
    std::uint32_t kind{};
    std::array<std::uint32_t, 3> line_pointers{};
    std::array<const char*, 3> lines{};
};
[[nodiscard]] std::span<const TrickTipText> trick_tip_texts() noexcept;
[[nodiscard]] std::uint32_t trick_tip_heading_pointer() noexcept;   // the "TIP:" string
[[nodiscard]] std::uint32_t trick_tip_remember_pointer() noexcept;  // the "REMEMBER:" string

// The skater counter offsets the monitor watches (+0x302c ... +0x3058 of the skater image).
constexpr std::array<std::size_t, 9> kTipSkaterCounters{0x302c, 0x3030, 0x3038, 0x303c, 0x3040,
                                                        0x3048, 0x3050, 0x3054, 0x3058};

// Everything the monitor reads from outside its own object.
struct TrickTipInputs final {
    bool gate_a{};              // FUN_00419420 (nonzero: skip this frame)
    bool video_playing{};       // Video_Playing 0x004cde70
    std::uint8_t replay_restart{};   // ReplayVideoRestartActive 0x00561990
    std::int32_t mode{};        // LevelEventGameModeSelector 0x00533f38 (runs in mode 1 only)
    std::int32_t level_state{}; // 0x0055fd78 (runs below 4)
    std::uint8_t flag_b098{};   // 0x0056b098 (held flags that count up to the "hold" tip)
    std::uint8_t flag_b204{};   // 0x0056b204
    std::uint32_t enabled{};    // 0x0056a46c tips option
    std::int32_t level{};       // CurrentLevel 0x0056a898
    std::uint32_t player{};     // 0x0056a960 skater pointer
    std::array<std::uint32_t, 2> level_masks{};  // 0x0053c288 + level * 0x6b * 4: tips shown for good on this level
    std::array<std::uint32_t, kTipCount> tip_states{};  // first dword of each table row
    // The skater counters, in kTipSkaterCounters order.
    std::array<std::int32_t, 9> counters{};
};

struct TrickTipState final {
    RetailImage object{kTipMonitorSize};
    // 0x0056a40c: tips seen this session (words 0, 1) and tips already repeated (words 2, 3).
    std::array<std::uint32_t, 4> masks{};
};

[[nodiscard]] int trick_tip_bank(int tip) noexcept;  // FUN_004c48f0 (-1 past 0x40, after an assert)

void trick_tips_reset(TrickTipState& state);
void trick_tips_select(TrickTipState& state, const TrickTipInputs& inputs, int tip);
void trick_tips_monitor(TrickTipState& state, const TrickTipInputs& inputs);

struct TipDrawCall final {
    enum class Kind : std::uint8_t { SetTextJustify, SetScale, GetScale, SetRgb, DrawPanelText };
    Kind kind{};
    std::array<std::uint32_t, 4> args{};
    friend bool operator==(const TipDrawCall&, const TipDrawCall&) = default;
};

class TipTextMetrics {
public:
    virtual ~TipTextMetrics() = default;
    // Mess_GetScale (0x00473500): the drawn width of a string at the current scale.
    virtual std::int32_t text_scale(std::uint32_t string_pointer) = 0;
};

// TrickTips_Display (0x004c4420): the panel slides in over its first ten frames and out over its
// last ten, draws the heading and up to three lines, and counts the timer down.
std::vector<TipDrawCall> trick_tips_display(TrickTipState& state, TipTextMetrics& metrics);

} // namespace opentony::runtime
