#pragma once

#include <cstdint>
#include <type_traits>

namespace opentony::runtime::guest {

// A pointer inside game memory: the 32-bit address the retail code uses (docs/PORT64_AUDIT.md D1). It is the handle on both the 32-bit
// and the 64-bit builds; a host pointer is `arena_base + address`, made only by the memory accessor. `T` is the pointee, for readers.
template <typename T>
struct GuestPtr {
    std::uint32_t address = 0;

    [[nodiscard]] constexpr bool null() const noexcept { return address == 0; }
    friend constexpr bool operator==(GuestPtr, GuestPtr) noexcept = default;
};

static_assert(sizeof(GuestPtr<void>) == 4 && alignof(GuestPtr<void>) == 4);
static_assert(std::is_standard_layout_v<GuestPtr<int>> && std::is_trivially_copyable_v<GuestPtr<int>>);

}  // namespace opentony::runtime::guest
