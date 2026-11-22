#pragma once

#include "career_tables.hpp"
#include "retail_memory.hpp"

#include <cstdint>
#include <span>

namespace opentony::runtime {

// Static data the create-a-skater code reads from the retail executable's .data, at its retail address
// (generated: create_tables.cpp, scripts/generate_create_tables.py):
//   0x00525c00  colour preset rows, the part name lists, the bitmap hue table and the strings they name
//   0x00539df8  the stat offsets of the first two builds and the three build row pointers
//   0x0055fe10  the empty string that ends the bitmap hue table
// The third build row (0x0056db50) is a runtime array of zeros in the retail process; a host provides it.
[[nodiscard]] std::span<const StaticRegion> create_static_regions() noexcept;

#if !OPENTONY_GUEST_MEMORY_FLAT   // a fixture loader (retail_memory.hpp)
// Adds the static regions (initial values) to `memory`.
inline void add_create_static_regions(RetailMemory& memory) {
    for (const auto& region : create_static_regions()) {
        memory.add_region(region.base, std::vector<std::uint8_t>(region.bytes.begin(), region.bytes.end()));
    }
}
#endif

}  // namespace opentony::runtime
