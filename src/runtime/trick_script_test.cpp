// Hand-built checks of the native trick interpreter and recognition (Phase 4.7a). The retail
// replays live in trick_retail_trace_test.cpp; these pin the semantics that no capture
// exercises and the corrections found against the disassembly.
#include "trick_script.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace opentony::runtime;

namespace {

int failures = 0;

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

constexpr std::uint32_t kBase = 0x0a000000;

struct Recorded {
    TrickService service;
    std::array<std::uint32_t, 6> args;
};

class Recorder final : public TrickServices {
public:
    std::uint32_t call(RetailImage&, const TrickServiceCall& c) override {
        calls.push_back({c.service, c.args});
        return c.service == TrickService::Rnd ? roll : 0;
    }
    std::vector<Recorded> calls;
    std::uint32_t roll{};
};

struct Bench {
    RetailImage skater{kSkaterImageSize};
    RetailImage controller{kControllerImageSize};
    std::vector<std::uint8_t> script;
    Recorder services;

    explicit Bench(std::vector<std::uint8_t> bytes) : script(std::move(bytes)) {
        skater.set32(0x29cc, kBase);
    }
    TrickWorld world() const {
        TrickWorld w;
        w.tricks_bin = script;
        w.tricks_base = kBase;
        w.controller = &controller;
        return w;
    }
    TrickExecuteResult run() { return trick_script_execute(skater, world(), services); }
    std::uint32_t cursor() const { return skater.u32(0x29cc) - kBase; }
};

void yields_advance_or_rewind() {
    // 0x16 always returns with the cursor past the opcode (the evidence table said "rewind").
    Bench a({0x16, 0x07});
    CHECK(a.run().yielded);
    CHECK(a.cursor() == 1);
    // 0x2c waits by rewinding onto itself while queued motion is pending.
    Bench b({0x2c, 0x07});
    b.skater.set32(0x2ca4, 1);
    CHECK(b.run().yielded);
    CHECK(b.cursor() == 0);
    b.skater.set32(0x2ca4, 0);
    CHECK(b.run().finished);
    CHECK(b.skater.u32(0x2c6c) == 1);
}

void bit_branches() {
    // 0x43 branches when bit 0x02 of +0xd8 is SET (the evidence table said "clear");
    // 0x3a branches when (+0xd8 >> 1 & 1) differs from byte +0x163.
    Bench set({0x43, 0x05, 0x00, 0x16, 0x07, 0x07});
    set.skater.set32(0xd8, 2);
    CHECK(set.run().finished);
    CHECK(set.cursor() == 6);  // jumped to offset 5
    Bench clear({0x43, 0x05, 0x00, 0x16, 0x07, 0x07});
    CHECK(clear.run().yielded);
    CHECK(clear.cursor() == 4);  // fell through to the 0x16
    Bench differ({0x3a, 0x05, 0x00, 0x16, 0x07, 0x07});
    differ.skater.set32(0xd8, 2);
    CHECK(differ.run().finished);  // bit 1 (set) != byte 0: branched
    Bench same({0x3a, 0x05, 0x00, 0x16, 0x07, 0x07});
    same.skater.set32(0xd8, 2);
    same.skater.set8(0x163, 1);
    CHECK(same.run().yielded);
}

void frame_operand_resolution() {
    RetailImage s(kSkaterImageSize);
    s.set8(0x106, 10);  // frame count
    s.set8(0x101, 4);   // end frame
    s.set8(0x100, 1);   // forward
    CHECK(trick_resolve_frame_operand(s, 3) == 3);
    CHECK(trick_resolve_frame_operand(s, 50) == 9);
    CHECK(trick_resolve_frame_operand(s, 0x2711) == 4);
    CHECK(trick_resolve_frame_operand(s, -2) == 2);   // end - 2 going forward
    CHECK(trick_resolve_frame_operand(s, -9) == 0);   // clamped at zero
    s.set8(0x100, 0xff);                               // backward (-1)
    CHECK(trick_resolve_frame_operand(s, -2) == 6);   // end + 2, clamped to count - 1
    CHECK(trick_resolve_frame_operand(s, -20) == 9);
}

void gosub_and_random_branch() {
    // 0x3d: Gosub saves the return cursor; 0x3e returns and clears it.
    Bench g({0x3d, 0x06, 0x00, 0x07, 0x00, 0x00, 0x3e});
    CHECK(g.run().finished);
    CHECK(g.skater.u32(0x29d8) == 0);
    CHECK(g.cursor() == 4);  // returned to the 0x07 after the Gosub operand
    // A second Gosub before the first returns reports the failed check (retail only logs).
    Bench nested({0x3d, 0x06, 0x00, 0x07, 0x00, 0x00, 0x3d, 0x03, 0x00});
    CHECK(nested.run().finished);
    CHECK(!nested.services.calls.empty() && nested.services.calls[0].service == TrickService::Assert);
    // 0x39: pick one of two relative targets with the game's Rnd.
    std::vector<std::uint8_t> bytes(0x10, 0x07);
    const std::vector<std::uint8_t> head{0x39, 0x02, 0x00, 0x0a, 0x00, 0x0c, 0x00};
    std::copy(head.begin(), head.end(), bytes.begin());
    Bench r(bytes);
    r.services.roll = 1;
    CHECK(r.run().finished);
    CHECK(r.services.calls.size() == 1 && r.services.calls[0].service == TrickService::Rnd);
    CHECK(r.cursor() == 0x0d);  // second target 0x000c
}

void animation_frame_clamp() {
    // 0x09 steps the frame by direction * operand and clamps between start (+0x114) and end (+0x101).
    Bench b({0x09, 0x05, 0x00, 0x07});
    b.skater.set8(0x100, 1);
    b.skater.set16(0x114, 0);
    b.skater.set8(0x101, 3);
    b.run();
    CHECK(b.skater.s16(0xf4) == 3);
}

void rail_restart_moves_position() {
    Bench b({0x49, 0x0a, 0x00, 0x07});
    b.skater.set32(0x50, 0x1000);
    b.skater.set32(0x2ea8, 100);
    b.skater.set32(0x310c, 2);
    b.run();
    CHECK(b.skater.s32(0x50) == -0x1000);
    CHECK(b.skater.s32(0x30b8) == 8);
    CHECK(b.skater.s32(0x08) == 100 + 0x1e * 2);
    CHECK(b.skater.s32(0x2f58) == 10);
    CHECK(!b.services.calls.empty() && b.services.calls[0].service == TrickService::RailRestart);
}

void undefined_command_clears_active_word() {
    Bench b({0x05, 0x07});
    b.skater.set32(0x2c68, 1);
    b.run();
    CHECK(b.skater.u32(0x2c68) == 0);
    CHECK(!b.services.calls.empty() && b.services.calls[0].service == TrickService::Assert);
}

void recognition_queues_and_starts() {
    // One entry: codes {2, 9} (direction pattern 2 then button 9), stream offset 0x10, no flags.
    const std::vector<std::uint16_t> table{2, 2, 9, 0x10, 0x20, 0};
    RetailImage skater(kSkaterImageSize);
    RetailImage controller(kControllerImageSize);
    std::vector<std::uint8_t> script(0x20, 0x07);
    Recorder services;
    TrickWorld world;
    world.tricks_bin = script;
    world.tricks_base = kBase;
    world.controller = &controller;
    world.key_table = table;
    world.clock = 100;
    skater.set32(0x30b8, 2);  // in the air
    // Pattern index 2 (+0xb0 = the Down byte) maps to pattern 2.
    controller.set8(0xb0, 1);
    trick_input_refresh_history(skater, world, services);
    world.clock = 101;
    controller.set8(0x20, 1);  // button 9 pressed on the next frame
    trick_input_refresh_history(skater, world, services);
    trick_input_select_sequence(skater, world, services);
    CHECK(skater.u32(0x2bd8) == 1);  // queued
    world.clock = 102;
    trick_script_start_pending(skater, world, services);
    CHECK(skater.u32(0x2bd8) == 0);
    CHECK(skater.u32(0x29cc) == kBase + 0x10);
    CHECK(skater.u32(0x2c68) == 1 && skater.u32(0x29c8) == 1);
    CHECK(skater.u32(0x108) == 0x10000);
}

void recognition_three_code_sequence() {
    // Up, Up, Square (codes 1 1 10): the "any other sequence" form reads the history backwards.
    const std::vector<std::uint16_t> table{3, 1, 1, 10, 0x18, 0x14, 0};
    RetailImage skater(kSkaterImageSize);
    RetailImage controller(kControllerImageSize);
    std::vector<std::uint8_t> script(0x30, 0x07);
    Recorder services;
    TrickWorld world;
    world.tricks_bin = script;
    world.tricks_base = kBase;
    world.controller = &controller;
    world.key_table = table;
    skater.set32(0x30b8, 2);
    const auto frame = [&](std::uint32_t clock, std::uint8_t up, std::uint8_t square) {
        world.clock = clock;
        controller.set8(0xa0, up);
        controller.set8(0x10, square);
        trick_input_refresh_history(skater, world, services);
    };
    frame(100, 1, 0);
    frame(101, 0, 0);
    frame(102, 1, 0);
    frame(104, 0, 1);
    trick_input_select_sequence(skater, world, services);
    CHECK(skater.u32(0x2bd8) == 1);
    CHECK(skater.u32(0x2b38) == kBase + 0x18);  // queued script pointer
    // Only one Up press: the sequence is incomplete and nothing is queued.
    RetailImage other(kSkaterImageSize);
    other.set32(0x30b8, 2);
    RetailImage other_controller(kControllerImageSize);
    world.controller = &other_controller;
    const auto other_frame = [&](std::uint32_t clock, std::uint8_t up, std::uint8_t square) {
        world.clock = clock;
        other_controller.set8(0xa0, up);
        other_controller.set8(0x10, square);
        trick_input_refresh_history(other, world, services);
    };
    other_frame(100, 1, 0);
    other_frame(101, 0, 0);
    other_frame(104, 0, 1);
    trick_input_select_sequence(other, world, services);
    CHECK(other.u32(0x2bd8) == 0);
}

}  // namespace

int main() {
    yields_advance_or_rewind();
    bit_branches();
    frame_operand_resolution();
    gosub_and_random_branch();
    animation_frame_clamp();
    rail_restart_moves_position();
    undefined_command_clears_active_word();
    recognition_queues_and_starts();
    recognition_three_code_sequence();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trick_script_test: ok\n";
    return EXIT_SUCCESS;
}
