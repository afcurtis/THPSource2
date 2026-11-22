#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Milestone 11d (docs/PORT64_AUDIT.md D3, section 7): two builds of one accessor class.
//
// OPENTONY_GUEST_MEMORY_FLAT=1 (the production DLLs: opentony_native.dll, opentony_standalone.dll, 32 and 64 bit; set PUBLIC on
// `opentony_kits_runtime` in src/hooks/kits/CMakeLists.txt): the arena backend only. The byte at guest address A is at host address
// `arena_base + A`, with the scratch alias and the optional guard in front. There are no regions, no `add_region`, no `regions()`, no
// comparison, and no `live_` branch per access.
//
// OPENTONY_GUEST_MEMORY_FLAT=0 (the default: ctest, the layer tests, the 32-bit replays of `tony hooks replay32`): the fixture build,
// the sparse region backend plus the same arena backend after `make_live` (retail_live_test.cpp maps a page and goes live).
//
// The two flavours live in different inline namespaces, so the class is spelled `RetailMemory` everywhere but mangles differently: a
// link that mixed objects of both flavours fails instead of breaking the one-definition rule in silence, and the production DLLs'
// symbols show which backend they hold (tests/test_memory_surface.py reads them).
#ifndef OPENTONY_GUEST_MEMORY_FLAT
#define OPENTONY_GUEST_MEMORY_FLAT 0
#endif

#if !OPENTONY_GUEST_MEMORY_FLAT
#include <map>
#endif

namespace opentony::runtime {

// A read or write the memory cannot serve: outside every region (fixture), refused by the guard or outside the scratch alias
// (arena), or retail's undefined behaviour that a Kit refuses itself (Font::draw). Fail-stop: the hook adapter reports it.
class MemoryFault final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

#if OPENTONY_GUEST_MEMORY_FLAT
inline namespace flat_memory {
#else
inline namespace fixture_memory {
#endif

// The retail process's data at the addresses it has in the 32-bit executable. The ported functions read and write the game's
// state at those addresses, exactly as the retail code does.
//
// Arena (live): every address is guest memory, `arena_base + address`: the retail process's own memory in the hooked build and the
// 32-bit standalone (arena_base 0), the program's 4 GiB arena in the 64-bit standalone (docs/PORT64_AUDIT.md D1). A live access is a
// plain load or store, as retail's is; `contains` and, when `check_every_access`, every access ask the guard whether the bytes are
// mapped (a guard that says no makes the access a MemoryFault).
//
// Fixture (sparse, the default before `make_live` in the fixture build): a set of byte regions, each at its retail address; a
// captured memory image replays word for word, and an access outside every region throws MemoryFault (retail would read or write
// wild memory there, the native port refuses instead).
class RetailMemory final {
public:
    // Live: is [address, address + size) readable (and writable when `write`)?
    using LiveGuard = bool (*)(std::uint32_t address, std::size_t size, bool write);

    // Live mode: `contains` asks `probe`; every access asks it too when `check_every_access` (a debugging switch: it costs a query per
    // access). The probe is given guest addresses. 11b (docs/PORT64_AUDIT.md D1): the byte at guest address A is at host address
    // `arena_base + A`; a 32-bit process (the hooked build, the 32-bit standalone) has arena_base 0, the 64-bit standalone its 4 GiB arena
    // (a 64-bit test that maps pages below 4 GiB itself may pass 0). The arena build is live from construction (arena_base 0, no
    // guard); `make_live` gives it the process's base and guard.
    void make_live(LiveGuard probe, bool check_every_access, std::uintptr_t arena_base = 0) {
        static_assert(sizeof(std::uintptr_t) >= sizeof(std::uint32_t));
#if !OPENTONY_GUEST_MEMORY_FLAT
        live_ = true;
#endif
        probe_ = probe;
        check_every_access_ = check_every_access;
        arena_base_ = arena_base;
    }
    [[nodiscard]] std::uintptr_t arena_base() const noexcept { return arena_base_; }
#if OPENTONY_GUEST_MEMORY_FLAT
    [[nodiscard]] static constexpr bool live() noexcept { return true; }
#else
    [[nodiscard]] bool live() const noexcept { return live_; }
#endif

    // Live: a window of retail addresses that are somewhere else in the process. The replays keep the
    // stack locals of the retail functions at fixed scratch addresses (pcplatform::kScratchBase) that
    // a real process may not be able to map (Wine reserves them): accesses to [base, base + size) go
    // to `to`, and the live host passes `live_address` of an argument that points there.
    // 11b: only where a host pointer is a guest address (arena_base 0); the 64-bit arena holds the scratch window itself (D7).
    void alias_live(std::uint32_t base, std::size_t size, std::uint8_t* to) {
        if (arena_base_ != 0 || reinterpret_cast<std::uintptr_t>(to) + size > 0x100000000ull) {
            throw MemoryFault(describe("alias_live (the window's buffer is not a guest address)", base, size));
        }
        alias_base_ = base;
        alias_size_ = size;
        alias_to_ = to;
    }
    [[nodiscard]] std::uint32_t live_address(std::uint32_t address) const noexcept {
        if (address - alias_base_ < alias_size_) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(alias_to_ + (address - alias_base_)));
        }
        return address;
    }

#if !OPENTONY_GUEST_MEMORY_FLAT
    // Fixture build only. Adds a zero-filled region. Regions must not overlap.
    void add_region(std::uint32_t base, std::size_t size) { add_region(base, std::vector<std::uint8_t>(size)); }
    void add_region(std::uint32_t base, std::vector<std::uint8_t> bytes) {
        if (live_) {
            throw MemoryFault(describe("add_region (live memory has no regions)", base, bytes.size()));
        }
        for (const auto& [other, data] : regions_) {
            if (base < other + data.size() && other < base + bytes.size()) {
                throw MemoryFault("overlapping retail memory regions");
            }
        }
        regions_.emplace(base, std::move(bytes));
    }
#endif

    // The one way production code asks for a window of scratch memory (the stack locals of a ported function). A sparse
    // fixture memory maps a zero-filled region of `size` bytes when `probe` bytes at `base` are not mapped yet; a live
    // memory (the guest arena) already holds the address, and an unmapped one is a fault.
    // 11d: nothing outside the fixture loaders and the replays calls `add_region` itself (tests/test_memory_surface.py).
    void ensure_region(std::uint32_t base, std::size_t size, std::size_t probe = 1) {
        if (!contains(base, probe)) {
#if OPENTONY_GUEST_MEMORY_FLAT
            throw MemoryFault(describe("scratch window (not mapped in the arena)", base, size));
#else
            add_region(base, size);
#endif
        }
    }

    [[nodiscard]] bool contains(std::uint32_t address, std::size_t size = 1) const noexcept {
#if !OPENTONY_GUEST_MEMORY_FLAT
        if (!live_) {
            return find_region(address, size) != nullptr;
        }
#endif
        if (address - alias_base_ < alias_size_) {
            return address - alias_base_ + size <= alias_size_;
        }
        return probe_ == nullptr || probe_(address, size, false);
    }

    [[nodiscard]] std::uint8_t u8(std::uint32_t address) const { return get<std::uint8_t>(address); }
    [[nodiscard]] std::int8_t s8(std::uint32_t address) const { return get<std::int8_t>(address); }
    [[nodiscard]] std::uint16_t u16(std::uint32_t address) const { return get<std::uint16_t>(address); }
    [[nodiscard]] std::int16_t s16(std::uint32_t address) const { return get<std::int16_t>(address); }
    [[nodiscard]] std::uint32_t u32(std::uint32_t address) const { return get<std::uint32_t>(address); }
    [[nodiscard]] std::int32_t s32(std::uint32_t address) const { return get<std::int32_t>(address); }

    void set8(std::uint32_t address, std::uint32_t value) { put<std::uint8_t>(address, static_cast<std::uint8_t>(value)); }
    void set16(std::uint32_t address, std::uint32_t value) { put<std::uint16_t>(address, static_cast<std::uint16_t>(value)); }
    void set32(std::uint32_t address, std::uint32_t value) { put<std::uint32_t>(address, value); }

    [[nodiscard]] std::vector<std::uint8_t> bytes(std::uint32_t address, std::size_t size) const {
        const auto* p = find(address, size);
        if (p == nullptr) {
            throw MemoryFault(describe("read", address, size));
        }
        return {p, p + size};
    }
    void write(std::uint32_t address, std::span<const std::uint8_t> data) {
        auto* p = find(address, data.size());
        if (p == nullptr) {
            throw MemoryFault(describe("write", address, data.size()));
        }
        std::memcpy(p, data.data(), data.size());
    }
    // memmove semantics are not needed: retail's Mem_CopyBytes callers never overlap.
    void copy(std::uint32_t to, std::uint32_t from, std::size_t size) { write(to, bytes(from, size)); }
    void fill(std::uint32_t address, std::uint8_t value, std::size_t size) {
        write(address, std::vector<std::uint8_t>(size, value));
    }

    // A NUL-terminated string, at most `limit` bytes (no terminator is a fault).
    [[nodiscard]] std::string cstring(std::uint32_t address, std::size_t limit = 256) const {
        std::string out;
        for (std::size_t i = 0; i < limit; ++i) {
            const char c = static_cast<char>(u8(address + static_cast<std::uint32_t>(i)));
            if (c == 0) {
                return out;
            }
            out.push_back(c);
        }
        throw MemoryFault(describe("string at", address, limit));
    }

#if !OPENTONY_GUEST_MEMORY_FLAT
    // Fixture build only: the replays compare and walk the regions.
    [[nodiscard]] const std::map<std::uint32_t, std::vector<std::uint8_t>>& regions() const noexcept { return regions_; }

    friend bool operator==(const RetailMemory&, const RetailMemory&) = default;
#endif

private:
    [[nodiscard]] const std::uint8_t* find(std::uint32_t address, std::size_t size, bool write = false) const noexcept {
#if !OPENTONY_GUEST_MEMORY_FLAT
        if (!live_) {
            return find_region(address, size);
        }
#endif
        if (address - alias_base_ < alias_size_) {
            return address - alias_base_ + size <= alias_size_ ? alias_to_ + (address - alias_base_) : nullptr;
        }
        if (check_every_access_ && probe_ != nullptr && !probe_(address, size, write)) {
            return nullptr;
        }
        return reinterpret_cast<const std::uint8_t*>(arena_base_ + address);
    }
#if !OPENTONY_GUEST_MEMORY_FLAT
    [[nodiscard]] const std::uint8_t* find_region(std::uint32_t address, std::size_t size) const noexcept {
        auto it = regions_.upper_bound(address);
        if (it == regions_.begin()) {
            return nullptr;
        }
        --it;
        const std::uint64_t offset = address - it->first;
        if (offset + size > it->second.size()) {
            return nullptr;
        }
        return it->second.data() + offset;
    }
#endif
    [[nodiscard]] std::uint8_t* find(std::uint32_t address, std::size_t size) noexcept {
        return const_cast<std::uint8_t*>(static_cast<const RetailMemory*>(this)->find(address, size, true));
    }
    template <typename T>
    [[nodiscard]] T get(std::uint32_t address) const {
        const auto* p = find(address, sizeof(T));
        if (p == nullptr) {
            throw MemoryFault(describe("read", address, sizeof(T)));
        }
        T value{};
        std::memcpy(&value, p, sizeof(T));
        return value;
    }
    template <typename T>
    void put(std::uint32_t address, T value) {
        auto* p = find(address, sizeof(T));
        if (p == nullptr) {
            throw MemoryFault(describe("write", address, sizeof(T)));
        }
        std::memcpy(p, &value, sizeof(T));
    }
    [[nodiscard]] static std::string describe(const char* what, std::uint32_t address, std::size_t size) {
        return std::string("retail memory ") + what + " of " + std::to_string(size) + " bytes at 0x" +
               [&] {
                   char buf[16];
                   std::snprintf(buf, sizeof buf, "%08x", address);
                   return std::string(buf);
               }() + " is outside every region";
    }

#if !OPENTONY_GUEST_MEMORY_FLAT
    std::map<std::uint32_t, std::vector<std::uint8_t>> regions_;
    bool live_ = false;
#endif
    bool check_every_access_ = false;
    LiveGuard probe_ = nullptr;
    std::uintptr_t arena_base_ = 0;
    std::uint32_t alias_base_ = 0;
    std::size_t alias_size_ = 0;
    std::uint8_t* alias_to_ = nullptr;
};

}  // inline namespace

// Does `Memory` carry the sparse region backend? False for the arena-only build; kit_dll.cpp asserts it for the production DLLs.
template <class Memory>
concept SparseRetailMemory = requires(Memory& memory) {
    memory.add_region(0u, std::size_t{1});
    memory.regions();
};
#if !OPENTONY_GUEST_MEMORY_FLAT
static_assert(SparseRetailMemory<RetailMemory>, "the concept must see the fixture build's regions, or the production assert says nothing");
#endif

}  // namespace opentony::runtime
