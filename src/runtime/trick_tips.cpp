#include "trick_tips.hpp"

#include <algorithm>

namespace opentony::runtime {
namespace {

using namespace tip_field;

// Counter words of the monitor object and the skater counter each one follows. The retail code
// keeps, for each watched counter, the highest value seen so far and (for some) a count of how
// often it rose.
constexpr std::size_t kMaxRise3048 = 0xc0;      // highest +0x3048
constexpr std::size_t kMax303c = 0xb4;
constexpr std::size_t kRises303c = 0xe8;
constexpr std::size_t kHoldFrames = 0x10c;      // frames one of the two flags has been held
constexpr std::size_t kMax3054 = 0xcc;
constexpr std::size_t kRises3054 = 0x100;
constexpr std::size_t kMax3050 = 0xc8;
constexpr std::size_t kRises3050 = 0xfc;
constexpr std::size_t kMax3058 = 0xd0;
constexpr std::size_t kRises3058 = 0x104;
constexpr std::size_t kMax3040 = 0xb8;
constexpr std::size_t kRises3040 = 0xec;
constexpr std::size_t kMax302c = 0xa4;
constexpr std::size_t kRises302c = 0xd8;
constexpr std::size_t kMax3030 = 0xa8;
constexpr std::size_t kRises3030 = 0xdc;
constexpr std::size_t kMax3038 = 0xb0;
constexpr std::size_t kRises3038 = 0xe4;

// Words FUN_004c4570 clears (everything except the maxima of +0x3058, the +0x104 rise count and
// the +0x110 flag byte, which it sets to 1).
constexpr std::array<std::size_t, 33> kResetWords{
    0x58, 0x5c, 0x60, 0xd8, 0xdc, 0xe0, 0xe4, 0xe8, 0xec, 0xf0, 0xf4, 0xf8, 0xfc, 0x100, 0x108, 0xa0, 0x90,
    0x94, 0x98, 0x9c, 0xa4, 0xa8, 0xac, 0xb0, 0xb4, 0xb8, 0xbc, 0xc0, 0xc4, 0xc8, 0xcc, 0xd4, 0x10c};

std::int32_t word(const RetailImage& image, std::size_t offset) { return image.s32(offset); }

void rise(TrickTipState& s, std::size_t maximum, std::int32_t counter) {
    if (word(s.object, maximum) < counter) {
        s.object.set32(maximum, static_cast<std::uint32_t>(counter));
    }
}

// The counter rose: bump its rise count, returning the new value.
std::int32_t bump(TrickTipState& s, std::size_t offset) {
    const auto value = static_cast<std::int32_t>(s.object.u32(offset) + 1u);
    s.object.set32(offset, static_cast<std::uint32_t>(value));
    return value;
}

}  // namespace

int trick_tip_bank(int tip) noexcept {
    if (tip < 0x20) {
        return 0;
    }
    if (tip < 0x40) {
        return 1;
    }
    return -1;  // retail asserts "trick tip mask out of bounds" and returns whatever the assert returned
}

void trick_tips_reset(TrickTipState& state) {
    for (std::size_t offset : kResetWords) {
        state.object.set32(offset, 0);
    }
    state.object.set8(0x110, 1);
}

void trick_tips_select(TrickTipState& state, const TrickTipInputs& in, int tip) {
    auto& obj = state.object;
    if (tip < 0) {
        return;
    }
    const std::uint32_t frames = obj.u32(kFrames);
    if (frames != 0 && obj.u32(kLastTipFrame) + 0x258u > frames) {
        return;  // another tip was picked within the last 600 frames
    }
    obj.set32(kLastTipFrame, frames);
    const int bank = trick_tip_bank(tip);
    if (bank < 0 || bank > 1) {
        return;
    }
    if (in.enabled == 0) {
        return;
    }
    const std::uint32_t bit = 1u << (static_cast<unsigned>(tip) - 32u * static_cast<unsigned>(bank));
    if ((in.level_masks[static_cast<std::size_t>(bank)] & bit) != 0) {
        return;  // shown for good on this level
    }
    const std::uint32_t remembered = state.masks[2 + static_cast<std::size_t>(bank)];
    const std::uint32_t seen = state.masks[static_cast<std::size_t>(bank)];
    state.masks[static_cast<std::size_t>(bank)] = seen | bit;
    if ((remembered & bit) != 0) {
        return;  // already shown twice
    }
    if ((seen & bit) != 0) {
        obj.set32(kTitle, trick_tip_remember_pointer());
        state.masks[2 + static_cast<std::size_t>(bank)] = remembered | bit;
    } else {
        obj.set32(kTitle, trick_tip_heading_pointer());
    }
    const auto texts = trick_tip_texts();
    if (static_cast<std::size_t>(tip) >= texts.size()) {
        return;
    }
    const auto& row = texts[static_cast<std::size_t>(tip)];
    obj.set32(kLine1, row.line_pointers[0]);
    obj.set32(kLine2, row.line_pointers[1]);
    obj.set32(kLine3, row.line_pointers[2]);
    obj.set32(kTimer, static_cast<std::uint32_t>(kTipDisplayFrames));
}

void trick_tips_monitor(TrickTipState& state, const TrickTipInputs& in) {
    auto& obj = state.object;
    if (in.gate_a || in.video_playing || in.replay_restart != 0 || in.mode != 1 || in.level_state >= 4) {
        return;
    }
    obj.set32(kPlayer, in.player);
    if (obj.u32(kFrames) == 0) {
        // the first frame: show the first tip that is eligible and not yet seen
        int tip = -1;
        for (std::size_t i = 0; i < kTipCount; ++i) {
            if (in.tip_states[i] != 0) {
                continue;
            }
            const int bank = trick_tip_bank(static_cast<int>(i));
            const std::uint32_t bit = 1u << (static_cast<unsigned>(i) - 32u * static_cast<unsigned>(bank));
            if ((state.masks[static_cast<std::size_t>(bank)] & bit) == 0) {
                tip = static_cast<int>(i);
                break;
            }
        }
        trick_tips_select(state, in, tip);
    }
    obj.set32(kFrames, obj.u32(kFrames) + 1u);
    obj.set32(kSinceReset, obj.u32(kSinceReset) + 1u);

    const auto& c = in.counters;  // indices follow kTipSkaterCounters
    rise(state, kMaxRise3048, c[5]);
    if (word(obj, kMax303c) < c[3]) {
        obj.set32(kMax303c, static_cast<std::uint32_t>(c[3]));
        bump(state, kRises303c);
        obj.set32(kSinceReset, 0);
    } else if (obj.u32(kSinceReset) > 0x258u) {
        obj.set32(kSinceReset, 0);
    }

    if (in.flag_b098 == 0 && in.flag_b204 == 0) {
        obj.set32(kHoldFrames, 0);
    } else {
        const std::int32_t held = bump(state, kHoldFrames);
        if (held > 0x96) {
            trick_tips_select(state, in, 1);
            obj.set32(kHoldFrames, 0);
        }
    }

    if (word(obj, kMax3054) < c[7]) {
        obj.set32(kMax3054, static_cast<std::uint32_t>(c[7]));
        if (bump(state, kRises3054) >= 3) {
            const std::int32_t before = word(obj, kRises3058);
            obj.set32(kRises3054, 0);
            if (before < 3) {
                trick_tips_select(state, in, 3);
            }
        }
    }
    if (word(obj, kMax3050) < c[6]) {
        obj.set32(kMax3050, static_cast<std::uint32_t>(c[6]));
        bump(state, kRises3050);
    }
    if (word(obj, kMax3058) < c[8]) {
        obj.set32(kMax3058, static_cast<std::uint32_t>(c[8]));
        bump(state, kRises3058);
    }
    if (word(obj, kMax3040) < c[4]) {
        obj.set32(kMax3040, static_cast<std::uint32_t>(c[4]));
        if (bump(state, kRises3040) >= 5) {
            trick_tips_select(state, in, 2);
            obj.set32(kRises3040, 0);
        }
    }
    if (word(obj, kMax302c) < c[0]) {
        obj.set32(kMax302c, static_cast<std::uint32_t>(c[0]));
        if (bump(state, kRises302c) >= 3) {
            obj.set32(kRises302c, 0);
        }
    }
    if (word(obj, kMax3030) < c[1]) {
        obj.set32(kMax3030, static_cast<std::uint32_t>(c[1]));
        if (bump(state, kRises3030) >= 3) {
            obj.set32(kRises3030, 0);
        }
    }
    if (word(obj, kMax3038) < c[2]) {
        obj.set32(kMax3038, static_cast<std::uint32_t>(c[2]));
        if (bump(state, kRises3038) >= 3) {
            trick_tips_select(state, in, 5);
            obj.set32(kRises3038, 0);
        }
    }
}

std::vector<TipDrawCall> trick_tips_display(TrickTipState& state, TipTextMetrics& metrics) {
    std::vector<TipDrawCall> calls;
    auto& obj = state.object;
    if (obj.u32(kTitle) == 0) {
        return calls;
    }
    const std::uint32_t timer = obj.u32(kTimer);
    std::uint32_t x = 0x14;
    const std::uint32_t elapsed = 200u - timer;
    if (static_cast<std::uint16_t>(elapsed) < 10) {
        x = elapsed * 40u - 0x17cu;
    }
    if (timer < 10) {
        x = x + timer * 40u - 400u;
    }
    x &= 0xffffu;
    using Kind = TipDrawCall::Kind;
    calls.push_back({Kind::SetTextJustify, {1, 0, 0, 0}});
    calls.push_back({Kind::SetRgb, {0x80, 0x80, 0, 0}});
    calls.push_back({Kind::SetScale, {0xdc, 0, 0, 0}});
    calls.push_back({Kind::DrawPanelText, {x, 100, obj.u32(kTitle), 0}});
    const std::array<std::size_t, 3> lines{kLine1, kLine2, kLine3};
    for (std::uint32_t row = 0; row < 3; ++row) {
        const std::uint32_t text = obj.u32(lines[row]);
        if (text == 0) {
            continue;
        }
        calls.push_back({Kind::GetScale, {text, 0, 0, 0}});
        const std::int32_t width = metrics.text_scale(text);
        const std::int32_t step = static_cast<std::int32_t>(static_cast<std::uint32_t>(width) * 20u) / 0xdc;
        calls.push_back({Kind::DrawPanelText,
                         {x, static_cast<std::uint32_t>(step * static_cast<std::int32_t>(row + 1) + 100), text, 0}});
    }
    const std::uint32_t left = timer - 1u;
    obj.set32(kTimer, left);
    if (left == 0) {
        obj.set32(kLine3, 0);
        obj.set32(kLine2, 0);
        obj.set32(kLine1, 0);
        obj.set32(kTitle, 0);
    }
    return calls;
}

} // namespace opentony::runtime
