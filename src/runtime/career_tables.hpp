#pragma once

#include "retail_memory.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace opentony::runtime {

// Static data the retail executable ships in .data, at its retail address (generated:
// career_tables.cpp, scripts/generate_career_tables.py):
//   0x00537b84  the alternate Tony Hawk face bitmap pointer
//   0x0053a3a4  the skater table: 20 rows of 0x170 bytes (row 0 at 0x0053a3b8) and the bytes up to `levels`
//   0x0053c1f8  the 13 level records (0x1ac bytes each) and the text they point at
struct StaticRegion final {
    std::uint32_t base;
    std::span<const std::uint8_t> bytes;
};
[[nodiscard]] std::span<const StaticRegion> career_static_regions() noexcept;

#if !OPENTONY_GUEST_MEMORY_FLAT   // fixture loaders: the arena build has no regions (retail_memory.hpp)
// Adds the static regions (initial values) to `memory`.
inline void add_career_static_regions(RetailMemory& memory) {
    for (const auto& region : career_static_regions()) {
        memory.add_region(region.base, std::vector<std::uint8_t>(region.bytes.begin(), region.bytes.end()));
    }
}

// Adds zero-filled regions for the mutable retail memory the career, skater-manager and competition code use (the career image,
// live career globals, skater manager, option and cheat flags). A fixture loader: the live arena holds these addresses itself.
inline void add_career_live_regions(RetailMemory& memory) {
    constexpr struct {
        std::uint32_t base;
        std::size_t size;
    } kRegions[] = {
        {0x0052be44, 8},        // mixer volumes derived from the options
        {0x0052cb3c, 4},
        {0x00525774, 0x10},     // the competition judges' score anchors (written when a competition starts)
        {0x00533f38, 0x70},     // game mode and the career mode switches after it
        {0x0055faf0, 0x4b0},    // live career globals
        {0x0055ffa0, 0xf0},     // competition state
        {0x00561ce4, 4},
        {0x00564378, 8},
        {0x00568608, 0x20},     // cash icons got while a movie plays
        {0x00568a68, 0x1ee0},   // career save image and the flags after it
        {0x0056acf8, 4},
        {0x0056b154, 0x17c},    // controller view pointers
        {0x0056b760, 4},
        {0x0056b7f4, 4},
        {0x0056bb34, 4},
        {0x0056c580, 0x6a0},    // skater manager
        {0x0056db5c, 4},        // the tag game's "it" skater
        {0x0056db8c, 4},
        {0x0056dd58, 4},        // gap table pointer
        {0x0056e210, 8},        // trigger node list pointer and count
    };
    for (const auto& region : kRegions) {
        memory.add_region(region.base, region.size);
    }
}
#endif

}  // namespace opentony::runtime
