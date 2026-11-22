#pragma once

// 11c, Kit equip_select (equip_select_racks.cpp): the typed views of the decks-and-trucks racks (the slide base, the deck rack and its sets and items, the spool queue, the truck rack and the
// 0x4c object records they place) and the helpers for the words no recovered layout holds (single globals, the slot table, the character file stems). Included by those .cpp files only (never by a
// header: the generated views are large and the Kit's header is included by half the runtime).

#include "equip_select.hpp"
#include "guest_views_generated.hpp"

namespace opentony::runtime::frontend::views {

using guest::GuestPtr;
using guest::RuntimeEquipDeckItem;
using guest::RuntimeEquipDeckItemView;
using guest::RuntimeEquipDeckRackView;
using guest::RuntimeEquipDeckSet;
using guest::RuntimeEquipDeckSetView;
using guest::RuntimeEquipRackBaseView;
using guest::RuntimeEquipSpoolNode;
using guest::RuntimeEquipSpoolNodeView;
using guest::RuntimeEquipSpoolQueue;
using guest::RuntimeEquipSpoolQueueView;
using guest::RuntimeEquipTruckRackView;
using guest::RuntimeEquipCashWindowView;
using guest::RuntimeEquipNameWindowView;
using guest::RuntimeEquipStatDotsView;
using guest::RuntimeEquipView;
using guest::RuntimeEquipViewView;
using guest::RuntimeEquipWindowView;
using guest::RuntimeFontView;
using guest::RuntimeFrontElement;
using guest::RuntimeFrontElementView;
using guest::RuntimeIntVector3View;
using guest::RuntimeViewportRectView;
using guest::RuntimePsxObjectRecord;
using guest::RuntimePsxObjectRecordView;
using guest::read_scalar;
using guest::write_scalar;

using Word = std::uint32_t;

// Each object record the racks place is 0x4c bytes (the vector constructor 0x0047fdb0 builds five).
static_assert(guest::kRuntimePsxObjectRecordSize == 0x4c);
static_assert(guest::kRuntimeEquipDeckRackSize == 0x6e0);
static_assert(guest::kRuntimeEquipTruckRackSize == 0x2f8);
static_assert(guest::kRuntimeEquipDeckSetSize == 0x5c);
static_assert(guest::kRuntimeEquipDeckRack_sets + 14 * guest::kRuntimeEquipDeckSetSize == guest::kRuntimeEquipDeckRack_set_count);
static_assert(guest::kRuntimeEquipDeckRack_records + 5 * guest::kRuntimePsxObjectRecordSize == guest::kRuntimeEquipDeckRack_sets);
static_assert(guest::kRuntimeEquipTruckRack_font + guest::kRuntimeFontSize == guest::kRuntimeEquipTruckRack_style);

// The window, the dots, the cash window, the view and the name window are allocated at these sizes (operator new in window_construct, dots_construct and the others).
static_assert(guest::kRuntimeEquipWindowSize == 0x3f4);
static_assert(guest::kRuntimeEquipStatDotsSize == 0x48);
static_assert(guest::kRuntimeEquipCashWindowSize == 0x36c);
static_assert(guest::kRuntimeEquipViewSize == 0x50);
static_assert(guest::kRuntimeEquipNameWindowSize == 0x21c);
static_assert(guest::kRuntimeEquipWindow_font_a + guest::kRuntimeFontSize == guest::kRuntimeEquipWindow_font_b);
static_assert(guest::kRuntimeEquipWindow_font_b + guest::kRuntimeFontSize == guest::kRuntimeEquipWindow_background);
static_assert(guest::kRuntimeEquipCashWindow_font_a + guest::kRuntimeFontSize == guest::kRuntimeEquipCashWindow_font_b);
static_assert(guest::kRuntimeEquipCashWindow_font_b + guest::kRuntimeFontSize == guest::kRuntimeEquipCashWindow_set);
static_assert(guest::kRuntimeEquipNameWindow_font + guest::kRuntimeFontSize == guest::kRuntimeEquipNameWindow_x);

[[nodiscard]] inline RuntimeFrontElementView element_at(RetailMemory& m, Word at) { return RuntimeFrontElementView(m, at); }

// The logo of a character by index, unchecked: the choice word is a character index the screen trusts.
[[nodiscard]] inline Word logo_at(const RuntimeEquipWindowView& window, Word index) { return read_scalar<Word>(window.memory(), window.logos_address(index)); }

[[nodiscard]] inline RuntimeEquipDeckSetView deck_set_at(RetailMemory& m, const RuntimeEquipDeckRackView& rack, std::int32_t index) {
    // unchecked: the address arithmetic wraps at 32 bits like the retail code's (a set index is a count the code loops to, never a table bound it checks)
    return RuntimeEquipDeckSetView(m, rack.sets_address(static_cast<Word>(index)));
}
[[nodiscard]] inline RuntimeEquipDeckItemView item_at(RetailMemory& m, Word at) { return RuntimeEquipDeckItemView(m, at); }
[[nodiscard]] inline RuntimeEquipSpoolQueueView spool_at(RetailMemory& m, Word at) { return RuntimeEquipSpoolQueueView(m, at); }
[[nodiscard]] inline RuntimeEquipSpoolNodeView node_at(RetailMemory& m, Word at) { return RuntimeEquipSpoolNodeView(m, at); }

[[nodiscard]] inline GuestPtr<RuntimeEquipSpoolNode> node_ptr(Word at) { return GuestPtr<RuntimeEquipSpoolNode>{at}; }
[[nodiscard]] inline GuestPtr<RuntimeEquipDeckItem> item_ptr(Word at) { return GuestPtr<RuntimeEquipDeckItem>{at}; }

// A set's deck items by index, unchecked: the retail code asserts the deck count (the log line is all that happens) and goes on.
[[nodiscard]] inline Word set_item(const RuntimeEquipDeckSetView& set, Word index) { return read_scalar<Word>(set.memory(), set.items_address(index)); }
inline void set_set_item(const RuntimeEquipDeckSetView& set, Word index, Word item) { write_scalar<Word>(set.memory(), set.items_address(index), item); }

// A single global named by its retail address (no recovered layout contains it): one explicit-width access.
[[nodiscard]] inline Word global_word(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t global_int(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint8_t global_byte(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_global_word(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }
inline void set_global_byte(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }

// The words of untyped tables and records (the slot table, the character file stems, a text entry's first word, a sprite object's two bytes).
[[nodiscard]] inline Word word_at(const RetailMemory& m, Word at) { return read_scalar<Word>(m, at); }
[[nodiscard]] inline std::int32_t int_at(const RetailMemory& m, Word at) { return read_scalar<std::int32_t>(m, at); }
[[nodiscard]] inline std::uint8_t byte_at(const RetailMemory& m, Word at) { return read_scalar<std::uint8_t>(m, at); }
inline void set_byte_at(RetailMemory& m, Word at, Word value) { write_scalar<std::uint8_t>(m, at, static_cast<std::uint8_t>(value)); }
inline void set_word_at(RetailMemory& m, Word at, Word value) { write_scalar<Word>(m, at, value); }

}  // namespace opentony::runtime::frontend::views
