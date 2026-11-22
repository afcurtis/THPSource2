// Hand-built checks of the native trick tips (Phase 4.7b). The retail replay lives in
// trick_tips_retail_trace_test.cpp; these pin the rules no capture exercises and the arithmetic of
// the slide-in panel.
#include "trick_tips.hpp"

#include <cstdlib>
#include <iostream>

using namespace opentony::runtime;
using namespace opentony::runtime::tip_field;

namespace {

int failures = 0;

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

TrickTipInputs ready() {
    TrickTipInputs in;
    in.mode = 1;
    in.enabled = 1;
    in.level = 12;
    in.player = 0x04f0a0a0;
    in.tip_states = {0, 2, 2, 2, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    return in;
}

class FixedWidth final : public TipTextMetrics {
public:
    explicit FixedWidth(std::int32_t width) : width_(width) {}
    std::int32_t text_scale(std::uint32_t) override { return width_; }

private:
    std::int32_t width_;
};

void table_and_banks() {
    CHECK(trick_tip_texts().size() == kTipCount);
    CHECK(std::string(trick_tip_texts()[0].lines[0]) == "HOLD JUMP TO");
    CHECK(trick_tip_texts()[4].line_pointers[2] == 0);
    CHECK(trick_tip_bank(0) == 0 && trick_tip_bank(0x1f) == 0);
    CHECK(trick_tip_bank(0x20) == 1 && trick_tip_bank(0x3f) == 1);
    CHECK(trick_tip_bank(0x40) == -1);
}

void reset_clears_the_counters() {
    TrickTipState s;
    for (std::size_t offset = 0; offset < kTipMonitorSize; offset += 4) {
        s.object.set32(offset, 0x11111111);
    }
    trick_tips_reset(s);
    for (std::size_t offset : {0x58u, 0x5cu, 0x60u, 0x90u, 0xa0u, 0xe8u, 0x108u, 0x10cu}) {
        CHECK(s.object.u32(offset) == 0);
    }
    CHECK(s.object.u32(0x64) == 0x11111111);   // the watched skater and the maxima of +0x3058 survive
    CHECK(s.object.u32(0xd0) == 0x11111111);
    CHECK(s.object.u32(0x104) == 0x11111111);
    CHECK(s.object.u8(0x110) == 1);
}

void selection_rules() {
    auto in = ready();
    TrickTipState s;
    // the first frame picks the first eligible tip that has not been seen
    trick_tips_monitor(s, in);
    CHECK(s.object.u32(kTitle) == trick_tip_heading_pointer());
    CHECK(s.object.u32(kLine1) == trick_tip_texts()[0].line_pointers[0]);
    CHECK(s.object.u32(kTimer) == 200);
    CHECK(s.masks[0] == 1 && s.masks[2] == 0);
    CHECK(s.object.u32(kFrames) == 1 && s.object.u32(kPlayer) == in.player);

    // a second tip within 600 frames is refused
    s.object.set32(kTitle, 0);
    trick_tips_select(s, in, 2);
    CHECK(s.object.u32(kTitle) == 0 && s.masks[0] == 1);
    // after 600 frames it shows; a repeat shows REMEMBER and is then remembered; a third time is silent
    s.object.set32(kFrames, 700);
    trick_tips_select(s, in, 2);
    CHECK(s.object.u32(kTitle) == trick_tip_heading_pointer());
    CHECK(s.masks[0] == 0b101 && s.masks[2] == 0);
    s.object.set32(kFrames, 1400);
    trick_tips_select(s, in, 2);
    CHECK(s.object.u32(kTitle) == trick_tip_remember_pointer());
    CHECK(s.masks[2] == 0b100);
    s.object.set32(kTitle, 0);
    s.object.set32(kFrames, 2100);
    trick_tips_select(s, in, 2);
    CHECK(s.object.u32(kTitle) == 0);
    CHECK(s.object.u32(kLastTipFrame) == 2100);  // the attempt still counts

    // a tip blocked for good on this level leaves the session masks alone
    TrickTipState blocked;
    blocked.object.set32(kFrames, 5000);
    in.level_masks[0] = 1u << 7;
    trick_tips_select(blocked, in, 7);
    CHECK(blocked.object.u32(kTitle) == 0 && blocked.masks[0] == 0);
    // the tips option off: nothing is marked seen either
    in.level_masks[0] = 0;
    in.enabled = 0;
    blocked.object.set32(kFrames, 9000);
    trick_tips_select(blocked, in, 7);
    CHECK(blocked.object.u32(kTitle) == 0 && blocked.masks[0] == 0);
    // a negative tip number is ignored outright
    in.enabled = 1;
    trick_tips_select(blocked, in, -1);
    CHECK(blocked.object.u32(kLastTipFrame) == 9000);
}

void gates_skip_the_frame() {
    TrickTipState s;
    for (int gate = 0; gate < 5; ++gate) {
        auto in = ready();
        in.gate_a = gate == 0;
        in.video_playing = gate == 1;
        in.replay_restart = gate == 2 ? 1 : 0;
        in.mode = gate == 3 ? 2 : 1;
        in.level_state = gate == 4 ? 4 : 3;
        trick_tips_monitor(s, in);
        CHECK(s.object.u32(kFrames) == 0 && s.object.u32(kPlayer) == 0);
    }
}

void watched_counters() {
    auto in = ready();
    TrickTipState s;
    s.object.set32(kFrames, 5000);
    // the hold tip: a flag held for 151 frames
    in.flag_b098 = 1;
    for (int frame = 0; frame < 150; ++frame) {
        trick_tips_monitor(s, in);
    }
    CHECK(s.object.u32(kTitle) == 0 && s.object.u32(0x10c) == 150);
    trick_tips_monitor(s, in);
    CHECK(s.object.u32(kTitle) == trick_tip_heading_pointer());
    CHECK(s.object.u32(kLine1) == trick_tip_texts()[1].line_pointers[0]);
    CHECK(s.object.u32(0x10c) == 0);
    in.flag_b098 = 0;
    trick_tips_monitor(s, in);
    CHECK(s.object.u32(0x10c) == 0);

    // tip 3: the +0x3054 counter rising three times, while +0x3058 has risen fewer than three
    TrickTipState t;
    t.object.set32(kFrames, 9000);
    for (std::int32_t value = 1; value <= 3; ++value) {
        in.counters[7] = value;
        trick_tips_monitor(t, in);
    }
    CHECK(t.object.u32(kLine1) == trick_tip_texts()[3].line_pointers[0]);
    CHECK(t.object.u32(0x100) == 0 && t.object.u32(0xcc) == 3);

    // +0x3040 five times: tip 2; +0x3038 three times: tip 5
    TrickTipState u;
    u.object.set32(kFrames, 9000);
    for (std::int32_t value = 1; value <= 5; ++value) {
        in.counters[4] = value;
        trick_tips_monitor(u, in);
    }
    CHECK(u.object.u32(kLine1) == trick_tip_texts()[2].line_pointers[0]);
    CHECK(u.object.u32(0xec) == 0);
    u.object.set32(kFrames, 99000);
    for (std::int32_t value = 1; value <= 3; ++value) {
        in.counters[2] = value;
        trick_tips_monitor(u, in);
    }
    CHECK(u.object.u32(kLine1) == trick_tip_texts()[5].line_pointers[0]);

    // the maxima only ever rise, and a counter that falls is ignored
    TrickTipState v;
    in.counters = {};
    in.counters[5] = 7;
    trick_tips_monitor(v, in);
    in.counters[5] = 3;
    trick_tips_monitor(v, in);
    CHECK(v.object.s32(0xc0) == 7);
    // the 0x3030 and 0x302c counters wrap their rise count at three without a tip
    in.counters = {};
    for (std::int32_t value = 1; value <= 3; ++value) {
        in.counters[0] = value;
        trick_tips_monitor(v, in);
    }
    CHECK(v.object.u32(0xd8) == 0);
}

void since_reset_counter() {
    auto in = ready();
    TrickTipState s;
    s.object.set32(kFrames, 1);
    s.object.set32(kSinceReset, 0x258);
    trick_tips_monitor(s, in);
    CHECK(s.object.u32(kSinceReset) == 0);  // 0x259 > 0x258: reset
    s.object.set32(kSinceReset, 10);
    in.counters[3] = 4;                     // +0x303c rose: counted, counter cleared
    trick_tips_monitor(s, in);
    CHECK(s.object.u32(0xe8) == 1 && s.object.u32(kSinceReset) == 0);
}

void display_panel() {
    auto in = ready();
    TrickTipState s;
    trick_tips_monitor(s, in);
    FixedWidth metrics(220);
    // timer 200: sliding in from off screen
    auto calls = trick_tips_display(s, metrics);
    CHECK(calls.size() == 10);  // justify, rgb, scale, heading, then three (get_scale, draw) pairs
    CHECK(calls[0].kind == TipDrawCall::Kind::SetTextJustify && calls[0].args[0] == 1);
    CHECK(calls[1].kind == TipDrawCall::Kind::SetRgb && calls[1].args[0] == 0x80 && calls[1].args[2] == 0);
    CHECK(calls[2].kind == TipDrawCall::Kind::SetScale && calls[2].args[0] == 0xdc);
    CHECK(calls[3].kind == TipDrawCall::Kind::DrawPanelText && calls[3].args[0] == ((0u - 380u) & 0xffffu) &&
          calls[3].args[1] == 100 && calls[3].args[2] == trick_tip_heading_pointer());
    CHECK(calls[5].kind == TipDrawCall::Kind::DrawPanelText && calls[5].args[1] == 120);
    CHECK(calls[7].args[1] == 140 && calls[9].args[1] == 160);
    CHECK(s.object.u32(kTimer) == 199);

    // mid-flight: resting position
    s.object.set32(kTimer, 100);
    calls = trick_tips_display(s, metrics);
    CHECK(calls[3].args[0] == 20);
    // sliding out: timer 5 -> 20 + 5 * 40 - 400
    s.object.set32(kTimer, 5);
    calls = trick_tips_display(s, metrics);
    CHECK(calls[3].args[0] == ((0u - 180u) & 0xffffu));
    // the last frame clears the panel
    s.object.set32(kTimer, 1);
    calls = trick_tips_display(s, metrics);
    CHECK(calls[3].args[0] == ((20u + 40u - 400u) & 0xffffu));
    CHECK(s.object.u32(kTitle) == 0 && s.object.u32(kLine1) == 0 && s.object.u32(kTimer) == 0);
    CHECK(trick_tips_display(s, metrics).empty());
    // wider text steps the lines further apart: 440 -> 40 per line
    TrickTipState wide;
    wide.object.set32(kTitle, trick_tip_heading_pointer());
    wide.object.set32(kLine1, trick_tip_texts()[0].line_pointers[0]);
    wide.object.set32(kTimer, 100);
    FixedWidth big(440);
    calls = trick_tips_display(wide, big);
    CHECK(calls.size() == 6 && calls[5].args[1] == 140);
}

}  // namespace

int main() {
    table_and_banks();
    reset_clears_the_counters();
    selection_rules();
    gates_skip_the_frame();
    watched_counters();
    since_reset_counter();
    display_panel();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trick_tips_test: ok\n";
    return EXIT_SUCCESS;
}
