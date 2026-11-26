#pragma once

#include "guest_arena.hpp"
#include "guest_heap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace opentony::kitdll {

// Owns the standalone's address space. Guest addresses stay 32-bit offsets
// and the existing guest heap commits pages through this mapping.
class LinuxGuestMemory final {
public:
    LinuxGuestMemory();
    ~LinuxGuestMemory();
    LinuxGuestMemory(const LinuxGuestMemory&) = delete;
    LinuxGuestMemory& operator=(const LinuxGuestMemory&) = delete;

    [[nodiscard]] std::uintptr_t base() const noexcept { return reinterpret_cast<std::uintptr_t>(mapping_); }
    [[nodiscard]] runtime::guest::GuestHeap& heap() noexcept { return heap_; }
    bool commit(std::uint32_t address, std::uint32_t size) const noexcept;

    // Loads a validated OTSD image into the zero-filled guest image region.
    [[nodiscard]] std::size_t load_static_data(std::span<const std::uint8_t> image);

private:
    std::size_t page_size_;
    runtime::guest::GuestHeap heap_;
    void* mapping_;
};

}  // namespace opentony::kitdll
