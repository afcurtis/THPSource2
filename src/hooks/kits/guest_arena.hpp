#pragma once

// Milestone 11b (docs/PORT64_AUDIT.md D1, D2, section 5): the standalone's guest memory. Every pointer inside game memory is the 32-bit
// address the retail code uses; the host reaches the byte at guest address A at `base() + A`.
//
//   32-bit build: base() is 0 (a constant). The program is linked at 0x00400000 and its zero-filled `.retail` section holds the image and
//                 the zones, as since 5.4a; the guest heap is a reservation of its own wherever Windows puts it (below 4 GiB, so its
//                 addresses are guest addresses too).
//   64-bit build: one reservation of 4 GiB (plus a guard) made by the program before it loads the image (`reserve`, below); base() is its
//                 start, which the program hands the DLL in OtKitHost::arena_base. The layout inside it is the map below.
//
// Host code never makes a host pointer from a guest word any other way, and never stores a host pointer into guest memory (`guest` refuses
// one outside the arena). RetailMemory (src/runtime) gets the same base through make_live. The header needs nothing else, so the program
// (standalone_spike.cpp) can share the map.
#include <cstdint>

namespace opentony::kitdll::arena {

// The guest address map (the same on both builds where the 32-bit process allows it)
constexpr std::uint32_t kNullGuardEnd = 0x00010000;     // nothing is ever committed below: a null guest pointer faults
constexpr std::uint32_t kImageBase = 0x00400000;        // the retail image (.rdata and .data from the static data; .text stays zero)
constexpr std::uint32_t kImageEnd = 0x029de000;
constexpr std::uint32_t kZonesEnd = 0x0a100000;         // to here: the retail process's zone block and the session images' heap regions (7a)
constexpr std::uint32_t kHeapBase = 0x10000000;         // 64-bit: the guest heap (the 32-bit heap is wherever its reservation landed)
constexpr std::uint32_t kHeapSize = 0x60000000;
constexpr std::uint32_t kScratchWindow = 0x7ff00000;    // pcplatform::kScratchBase's 64 KB (D7): committed in the arena, no alias on 64-bit
constexpr std::uint32_t kScratchWindowSize = 0x00010000;
constexpr std::uint64_t kArenaBytes = 0x100000000ull + 0x10000;   // 4 GiB, and a guard page run so an access that straddles 0xffffffff faults

#if UINTPTR_MAX == 0xffffffffu
constexpr bool kFlat = true;
[[nodiscard]] constexpr std::uintptr_t base() noexcept { return 0; }
#else
constexpr bool kFlat = false;
[[nodiscard]] std::uintptr_t base() noexcept;
#endif

// The DLL's side: the base the program reserved (OtKitHost::arena_base). 32-bit: must be 0.
void attach(std::uintptr_t arena_base);

template <typename T = std::uint8_t>
[[nodiscard]] inline T* host(std::uint32_t address) noexcept {
    return reinterpret_cast<T*>(base() + address);
}
// a guest word or byte as the game sees it (volatile: the Kits and the seams both write game memory)
template <typename T>
[[nodiscard]] inline volatile T& at(std::uint32_t address) noexcept {
    return *reinterpret_cast<volatile T*>(base() + address);
}
[[nodiscard]] inline const char* text(std::uint32_t address) noexcept { return reinterpret_cast<const char*>(base() + address); }
[[nodiscard]] inline char* chars(std::uint32_t address) noexcept { return reinterpret_cast<char*>(base() + address); }

// The guest address of host memory inside the arena; MemoryFault for any other pointer (a host buffer, a host heap block, a stack local of
// host code): such a pointer must never reach game memory or a native's arguments. On the 32-bit build every pointer is below 4 GiB, so
// this only checks that.
[[nodiscard]] std::uint32_t guest(const void* pointer);

// The C string at a guest address when all of it (to its NUL, within 64 KiB) is committed readable memory, else nullptr. For text whose address is the
// game's garbage (an assertion's `%s` of stack words that were never set): the game would read whatever was there, the host must not fault on it.
[[nodiscard]] const char* readable_text(std::uint32_t address);

// Commit [address, address + size) of the arena, read/write (64-bit; on 32-bit it commits the host pages at those addresses). False on failure.
bool commit(std::uint32_t address, std::uint32_t size);

// The guest heap (src/runtime/guest_heap.hpp): the C runtime's malloc / operator new and the surfaces' pixels. heap_allocate answers 0 when
// it is full; heap_release throws MemoryFault for an address that is not a live block of it.
[[nodiscard]] std::uint32_t heap_allocate(std::uint32_t size);
void heap_release(std::uint32_t address);
[[nodiscard]] bool heap_owns(std::uint32_t address);

// A scoped guest heap block: the one way host code makes a buffer that a native or seam must see (a file name, a read buffer). MemoryFault when
// the heap is full. The block is not cleared (as malloc).
class HeapBlock {
public:
    explicit HeapBlock(std::uint32_t size);
    ~HeapBlock();
    HeapBlock(const HeapBlock&) = delete;
    HeapBlock& operator=(const HeapBlock&) = delete;
    [[nodiscard]] std::uint32_t address() const noexcept { return address_; }
    [[nodiscard]] std::uint8_t* data() const noexcept { return host(address_); }

private:
    std::uint32_t address_ = 0;
};

}  // namespace opentony::kitdll::arena
