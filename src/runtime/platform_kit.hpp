#pragma once

// The PC platform natives as one Kit object (Phase 5.1t): the pad and DirectInput layer (pad_input.cpp) and the game side of the PC system layer (pc_system.cpp), which share the retail memory and
// services but not an object, so a Kit hook reaches them as `pad.` and `sys.`.
#include "pad_input.hpp"
#include "pc_system.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

namespace opentony::runtime::pcplatform {

struct PlatformKit {
    PlatformKit(RetailMemory& memory, RetailServices& services) : pad(memory, services), sys(memory, services, pad) {}
    PlatformKit(const PlatformKit&) = delete;
    PlatformKit& operator=(const PlatformKit&) = delete;

    PadInput pad;
    PcSystem sys;
};

}  // namespace opentony::runtime::pcplatform
