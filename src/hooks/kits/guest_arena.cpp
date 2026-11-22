// Milestone 11b: the standalone DLL's side of the guest arena (guest_arena.hpp) and the guest heap over it.
#include "guest_arena.hpp"

#include "guest_heap.hpp"
#include "kit_bridge.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace opentony::kitdll::arena {
namespace {

#if !(UINTPTR_MAX == 0xffffffffu)
std::uintptr_t g_base = 0;
#endif

std::string hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof text, "%08llx", static_cast<unsigned long long>(value));
    return text;
}

std::unique_ptr<runtime::guest::GuestHeap> g_heap;

// 32-bit: the heap's own reservation. The process also holds the image's 157 MB section, the DLLs and the Wine heaps, so the size is the
// largest of these that fits (the scenarios' peak is far below the smallest).
runtime::guest::GuestHeap& make_heap() {
    if (kFlat) {
        for (const std::uint32_t size : {0x20000000u, 0x10000000u, 0x08000000u}) {
            void* reserved = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
            if (reserved == nullptr) {
                continue;
            }
            const auto at = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(reserved));
            g_heap = std::make_unique<runtime::guest::GuestHeap>(at, size, &commit);
            std::printf("kit: guest heap %08x..%08llx (reserved)\n", static_cast<unsigned>(at), static_cast<unsigned long long>(std::uint64_t{at} + size));
            std::fflush(stdout);
            return *g_heap;
        }
        throw runtime::MemoryFault("guest heap: no reservation of 128 MB or more is possible in this process");
    }
    g_heap = std::make_unique<runtime::guest::GuestHeap>(kHeapBase, kHeapSize, &commit);
    return *g_heap;
}

runtime::guest::GuestHeap& heap() { return g_heap ? *g_heap : make_heap(); }

}  // namespace

#if !(UINTPTR_MAX == 0xffffffffu)
std::uintptr_t base() noexcept { return g_base; }
#endif

void attach(std::uintptr_t arena_base) {
#if UINTPTR_MAX == 0xffffffffu
    if (arena_base != 0) {
        throw runtime::MemoryFault("guest arena: a 32-bit program has no arena base (got " + hex(arena_base) + ")");
    }
#else
    if (arena_base == 0) {
        throw runtime::MemoryFault("guest arena: the 64-bit program passed no arena (OtKitHost::arena_base)");
    }
    g_base = arena_base;
#endif
}

std::uint32_t guest(const void* pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t offset = address - base();
    if (pointer == nullptr || address < base() || static_cast<std::uint64_t>(offset) > 0xffffffffull) {
        throw runtime::MemoryFault("guest arena: host pointer " + hex(address) + " is not in guest memory (arena at " + hex(base()) + ")");
    }
    return static_cast<std::uint32_t>(offset);
}

const char* readable_text(std::uint32_t address) {
    const char* start = text(address);
    auto at = reinterpret_cast<std::uintptr_t>(start);
    constexpr std::size_t kLimit = 0x10000;
    for (std::size_t scanned = 0; scanned < kLimit;) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof info) != sizeof info || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return nullptr;
        }
        const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        const std::size_t span = std::min<std::size_t>(end - at, kLimit - scanned);
        if (std::memchr(reinterpret_cast<const void*>(at), 0, span) != nullptr) {
            return start;
        }
        at += span;
        scanned += span;
    }
    return nullptr;
}

bool commit(std::uint32_t address, std::uint32_t size) {
    return VirtualAlloc(host(address), size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

std::uint32_t heap_allocate(std::uint32_t size) { return heap().allocate(size); }

void heap_release(std::uint32_t address) {
    try {
        heap().release(address);
    } catch (const std::invalid_argument& error) {
        throw runtime::MemoryFault(error.what());
    }
}

bool heap_owns(std::uint32_t address) { return heap().owns(address); }

HeapBlock::HeapBlock(std::uint32_t size) : address_(heap_allocate(size == 0 ? 1 : size)) {
    if (address_ == 0) {
        throw runtime::MemoryFault("guest heap: a block of " + hex(size) + " bytes does not fit");
    }
}

HeapBlock::~HeapBlock() {
    try {
        heap_release(address_);
    } catch (...) {   // a destructor must not throw; a block freed twice is a bug the heap already reported once
    }
}

}  // namespace opentony::kitdll::arena

// The program's way to a guest buffer (standalone_spike.cpp: the arguments of its own calls, the hostcheck's files and names): a block of the guest heap, 0 when
// it is full. Exceptions stop at this boundary.
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_guest_alloc(std::uint32_t size) {
    try {
        return opentony::kitdll::arena::heap_allocate(size == 0 ? 1 : size);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "guest heap: %s\n", error.what());
        return 0;
    }
}

extern "C" __declspec(dllexport) void __cdecl ot_standalone_guest_free(std::uint32_t address) {
    try {
        opentony::kitdll::arena::heap_release(address);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "guest heap: %s\n", error.what());
    }
}
