#pragma once

// The retail executable's C++ dynamic initializers that the standalone runs itself (milestone 9): standalone_static_init_generated.cpp holds the
// transcribed `jmp` stub bodies, standalone_boot.cpp the rest of the table (natives, and the constructor stubs written by hand).

#include <cstddef>
#include <cstdint>

namespace opentony::kitdll {

struct StaticInitializer {
    std::uint32_t slot;       // its entry in the initializer table 0x00524000..0x00524118
    std::uint32_t function;   // the function the slot holds (a `jmp` stub)
    void (*body)();           // what the stub's target does, transcribed
};

extern const StaticInitializer kTranscribedInitializers[];
extern const std::size_t kTranscribedInitializerCount;

}  // namespace opentony::kitdll
