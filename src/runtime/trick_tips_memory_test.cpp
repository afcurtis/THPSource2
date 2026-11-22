// The retail-memory form of TrickTips_Reset 0x004c4570 (CreateObjKit::tips_reset) against the model it wraps, trick_tips_reset (trick_tips.cpp), which the retail trace test replays against
// retail's before and after images of the monitor object (re/evidence/fixtures/tricktips-warehouse.txt). The monitor object at 0x0056db98 is laid out in a RetailMemory filled with a
// pattern, with guard bytes on both sides; the Kit's result must equal the model's on a copy of the same bytes, and the guards must not change. (The 34 stores of the retail function are the
// model's word list exactly: checked against the disassembly when the Kit was written.)
//
//   opentony_trick_tips_memory_test [--mutate]
//
// --mutate changes one byte of the expected image; the check must fail (it has teeth).
//
// Exit codes: 0 match, 1 mismatch.
#include "createobj_kit.hpp"
#include "trick_tips.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace opentony::runtime;

int main(int argc, char** argv) {
    const bool mutate = argc > 1 && std::string(argv[1]) == "--mutate";
    constexpr std::uint32_t kGuard = 0x20;
    std::vector<std::uint8_t> pattern(kGuard + kTipMonitorSize + kGuard);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<std::uint8_t>(0x80 | ((i * 37 + 11) & 0x7f));   // no byte is zero or one: a cleared word shows
    }
    RetailMemory memory;
    memory.add_region(kTipMonitorAddress - kGuard, pattern);
    RetailServices services;
    createobj::CreateObjKit kit(memory, services);
    kit.tips_reset();

    TrickTipState model;
    model.object.bytes().assign(pattern.begin() + kGuard, pattern.begin() + kGuard + kTipMonitorSize);
    trick_tips_reset(model);
    std::vector<std::uint8_t> expected(pattern.begin(), pattern.end());
    std::copy(model.object.bytes().begin(), model.object.bytes().end(), expected.begin() + kGuard);
    if (mutate) {
        expected[kGuard + 0x58] ^= 1;
    }
    const std::vector<std::uint8_t> got = memory.bytes(kTipMonitorAddress - kGuard, pattern.size());
    int differing = 0;
    for (std::size_t i = 0; i < got.size(); ++i) {
        differing += got[i] != expected[i];
    }
    std::cout << "tips_reset: " << differing << " bytes differ from the model, " << (got != pattern ? "the object changed" : "NOTHING CHANGED") << "\n";
    return differing == 0 && got != pattern ? 0 : 1;
}
