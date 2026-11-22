#pragma once

#include "guest_ptr.hpp"
#include "retail_memory.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace opentony::runtime::guest::detail {

// 11c (docs/PORT64_AUDIT.md D4): what the generated views (guest_views_generated.hpp) read and write. A view is a handle over
// (`RetailMemory`, guest address); every access is one explicit-width load or store through the accessor, so a view works over the
// live arena and the fixture regions alike, and the layout it reads is the one asserted in guest_layouts_generated.hpp.

[[noreturn]] inline void index_fault(const char* field, std::size_t index, std::size_t count) {
    throw MemoryFault(std::string("view index ") + std::to_string(index) + " is outside " + field + "[" + std::to_string(count) + "]");
}

template <typename T>
[[nodiscard]] inline T load(const RetailMemory& memory, std::uint32_t address) {
    if constexpr (std::is_same_v<T, std::uint8_t>) {
        return memory.u8(address);
    } else if constexpr (std::is_same_v<T, std::int8_t>) {
        return memory.s8(address);
    } else if constexpr (std::is_same_v<T, char>) {
        return static_cast<char>(memory.u8(address));
    } else if constexpr (std::is_same_v<T, std::uint16_t>) {
        return memory.u16(address);
    } else if constexpr (std::is_same_v<T, std::int16_t>) {
        return memory.s16(address);
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        return memory.u32(address);
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        return memory.s32(address);
    } else if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(memory.u32(address));
    } else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>) {
        const std::uint64_t bits = memory.u32(address) | (static_cast<std::uint64_t>(memory.u32(address + 4)) << 32);
        return std::bit_cast<T>(bits);
    } else {
        static_assert(sizeof(T) == 0, "no view accessor for this type");
    }
}

template <typename T>
inline void store(RetailMemory& memory, std::uint32_t address, T value) {
    if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::int8_t> || std::is_same_v<T, char>) {
        memory.set8(address, static_cast<std::uint8_t>(value));
    } else if constexpr (std::is_same_v<T, std::uint16_t> || std::is_same_v<T, std::int16_t>) {
        memory.set16(address, static_cast<std::uint16_t>(value));
    } else if constexpr (std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::int32_t>) {
        memory.set32(address, static_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<T, float>) {
        memory.set32(address, std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        memory.set32(address, static_cast<std::uint32_t>(bits));
        memory.set32(address + 4, static_cast<std::uint32_t>(bits >> 32));
    } else {
        static_assert(sizeof(T) == 0, "no view accessor for this type");
    }
}

}  // namespace opentony::runtime::guest::detail

namespace opentony::runtime::guest {

// A single scalar at a guest address that no recovered layout contains: a global the Kit names by its retail address, or the word an out-parameter points at.
// Same explicit-width access as a view's field, so a typed Kit has no `u32(address + 12)` left in it.
template <typename T>
[[nodiscard]] inline T read_scalar(const RetailMemory& memory, std::uint32_t address) {
    return detail::load<T>(memory, address);
}

template <typename T>
inline void write_scalar(RetailMemory& memory, std::uint32_t address, T value) {
    detail::store<T>(memory, address, value);
}

}  // namespace opentony::runtime::guest
