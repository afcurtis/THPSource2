#pragma once

// 11c, Kit rail (rail_nodes.cpp, rail_create.cpp): the typed views of the CRail runtime record and the lookup table, the GTE register file the normaliser squares through, and the helpers for the
// words that no recovered layout holds (the level's node table, a node's trigger word and link list). Included by those .cpp files only (never by a header: the generated views are large and the
// Kit's header is included by half the runtime).

#include "collision_views.hpp"
#include "guest_views_generated.hpp"
#include "rail_kit.hpp"

namespace opentony::runtime::rail::views {

using namespace collision::views;

using guest::RuntimeRailLookupEntryView;
using guest::RuntimeTriggerRailRecord;
using guest::RuntimeTriggerRailRecordView;

using Word = std::uint32_t;

// The constants of rail_kit.hpp are the offsets of the record's fields: the layout in re/types decides, and this is where the two are tied.
static_assert(record::kBytes == guest::kRuntimeTriggerRailRecordSize);
static_assert(record::kVtable == guest::kRuntimeTriggerRailRecord_vtable);
static_assert(record::kState == guest::kRuntimeTriggerRailRecord_rail_state);
static_assert(record::kKind == guest::kRuntimeTriggerRailRecord_rail_kind);
static_assert(record::kFirstNode == guest::kRuntimeTriggerRailRecord_source_trg_node_index);
static_assert(record::kMinimum == guest::kRuntimeTriggerRailRecord_bounds_minimum);
static_assert(record::kMaximum == guest::kRuntimeTriggerRailRecord_bounds_maximum);
static_assert(record::kPrevious == guest::kRuntimeTriggerRailRecord_previous_rail_record);
static_assert(record::kNext == guest::kRuntimeTriggerRailRecord_next_rail_record);
static_assert(guest::kRuntimeRailLookupEntrySize == 4);

[[nodiscard]] inline RuntimeTriggerRailRecordView record_at(RetailMemory& m, Word at) { return RuntimeTriggerRailRecordView(m, at); }
[[nodiscard]] inline RuntimeGteRegisterFileView gte_file(RetailMemory& m) { return RuntimeGteRegisterFileView(m, kRegisters); }

// An entry of the lookup table by index: the table is a heap block of 2000 entries and the callers test the index (the Kit's own assertions), so the view is built without a bounds test.
[[nodiscard]] inline RuntimeRailLookupEntryView lookup_entry(RetailMemory& m, Word table, Word index) { return RuntimeRailLookupEntryView(m, table + guest::kRuntimeRailLookupEntrySize * index); }

// A single global named by its retail address (no recovered layout contains it): one explicit-width access.
[[nodiscard]] inline Word global_word(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t global_int(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint8_t global_byte(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_global_word(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }

// The words of the level's node data (no fixed layout: a node's body depends on its type, the trigger word follows the decoded position, a link list is a count and the linked nodes).
[[nodiscard]] inline Word word_at(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline Word half_at(const RetailMemory& m, Word at) { return read_scalar<std::uint16_t>(m, at); }
[[nodiscard]] inline Word byte_at(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_half_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint16_t>(m, at, static_cast<std::uint16_t>(value)); }
inline void set_byte_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }

// a half-word widened the way the retail `movsx` reads it
[[nodiscard]] constexpr std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

// The node record of the level's node table: `OffsetList` points at an array of node pointers; the first half-word of a node is its type.
[[nodiscard]] inline Word node_record(const RetailMemory& m, Word node) { return global_word(m, global_word(m, globals::kOffsetList) + 4 * node); }

}  // namespace opentony::runtime::rail::views
