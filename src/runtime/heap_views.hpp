#pragma once

// 11c, Kit heap: the typed views of re/types/heap.yml and the slot helpers heap.cpp shares. Included by heap.cpp only (never by a header: the generated views are large and the Kit's
// header is included by half the runtime).

#include "guest_views_generated.hpp"
#include "heap_kit.hpp"

namespace opentony::runtime::heap::views {

using guest::GuestPtr;
using guest::RuntimeHeapBlockHeader;
using guest::RuntimeHeapBlockHeaderView;
using guest::RuntimeHeapGlobalsView;
using guest::RuntimeHeapZoneBoundsView;
using guest::read_scalar;
using guest::write_scalar;

using Word = std::uint32_t;

// The constants of heap_kit.hpp are the addresses of the globals' fields: the layout in re/types decides, and this is where the two are tied.
inline constexpr Word kGlobals = mem::kWindow;
static_assert(mem::kWindowBytes == guest::kRuntimeHeapGlobalsSize);
static_assert(mem::kFreeHead == kGlobals + guest::kRuntimeHeapGlobals_free_head);
static_assert(mem::kUsed == kGlobals + guest::kRuntimeHeapGlobals_used_bytes);
static_assert(mem::kZones == kGlobals + guest::kRuntimeHeapGlobals_zones);
static_assert(mem::kLowFlag == kGlobals + guest::kRuntimeHeapGlobals_low_memory);
static_assert(mem::kLimit == kGlobals + guest::kRuntimeHeapGlobals_limit);
static_assert(mem::kCounter == kGlobals + guest::kRuntimeHeapGlobals_allocation_counter);
static_assert(guest::kRuntimeHeapBlockHeaderSize == 8);

[[nodiscard]] inline RuntimeHeapGlobalsView globals(RetailMemory& m) { return RuntimeHeapGlobalsView(m, kGlobals); }

// A block by address: the header word (size << 4 | tag) and the link of a free block. No test that the address is a block: retail's functions take whatever they are given.
[[nodiscard]] inline RuntimeHeapBlockHeaderView block_at(RetailMemory& m, Word at) { return RuntimeHeapBlockHeaderView(m, at); }
[[nodiscard]] inline Word hdr(RetailMemory& m, Word at) { return block_at(m, at).size_and_tag(); }
inline void set_hdr(RetailMemory& m, Word at, Word value) { block_at(m, at).set_size_and_tag(value); }
[[nodiscard]] inline Word next_of(RetailMemory& m, Word at) { return block_at(m, at).next_free().address; }
inline void set_next_of(RetailMemory& m, Word at, Word next) { block_at(m, at).set_next_free(GuestPtr<RuntimeHeapBlockHeader>{next}); }

// The per-zone slots, by an unchecked zone: Mem_GetFree takes any signed byte and reads the words around the tables (the `_address` accessors do not test the index; the checked
// array accessors would throw where retail reads).
[[nodiscard]] inline Word zone_head(RetailMemory& m, Word zone) { return read_scalar<Word>(m, globals(m).free_head_address(zone)); }
inline void set_zone_head(RetailMemory& m, Word zone, Word block) { write_scalar<Word>(m, globals(m).free_head_address(zone), block); }
[[nodiscard]] inline Word zone_used(RetailMemory& m, Word zone) { return read_scalar<Word>(m, globals(m).used_bytes_address(zone)); }
inline void set_zone_used(RetailMemory& m, Word zone, Word bytes) { write_scalar<Word>(m, globals(m).used_bytes_address(zone), bytes); }
[[nodiscard]] inline RuntimeHeapZoneBoundsView zone_bounds(RetailMemory& m, Word zone) { return RuntimeHeapZoneBoundsView(m, globals(m).zones_address(zone)); }

}  // namespace opentony::runtime::heap::views
