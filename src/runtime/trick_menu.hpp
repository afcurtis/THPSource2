#pragma once

#include "trick_database.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.7b: the decision logic of the trick-list screen (trickwin.cpp / tricksel.cpp). The
// retail window object mixes these rules with the frontend widget toolkit (list boxes, text
// elements, sprites); the functions below are the parts that do not touch a widget, written over
// the character record's binding block (trick_database.hpp) so they can be tested without one.
//
//   0x004c3350  trick_menu_category_mask       category page -> trick-class mask
//   0x004c1310  trick_button_glyphs            a button list -> the font's button glyph characters
//   0x004c2890  trick_menu_sort_by_cost        order the page's rows by the tricks' point cost
//   0x004c2800  trick_menu_assigned_record     the trick bound to a row's button combination
//   0x004c3cf0  trick_menu_specials_full       no free special slot and the row has none
//   0x004c36d0  trick_menu_bind                bind the highlighted trick to a row
//   0x004c38f0  trick_menu_unbind              clear a row's binding
//
// The category numbers are the screen's own page ids (6..10).
constexpr int kMenuCategoryManual = 6;
constexpr int kMenuCategoryGrind = 7;
constexpr int kMenuCategoryLip = 8;
constexpr int kMenuCategoryAir = 9;
constexpr int kMenuCategorySpecial = 10;

// Returns 0 for an unrecognised page (retail asserts "Unrecognized category").
[[nodiscard]] std::uint32_t trick_menu_category_mask(int category) noexcept;

// The font's button glyph characters for a combination `[count, button...]`: 1..12 map to
// 'G' 'H' 'E' 'F' 'J' 'I' 'K' 'L' 'B' 'D' 'C' 'A'; other codes print nothing.
[[nodiscard]] std::string trick_button_glyphs(std::span<const std::uint16_t> combination);

// A stable ascending order of `sources` (each the source offset of a database record) by the
// record's cost. Returns the permutation: result[k] is the index of the row that goes k-th.
[[nodiscard]] std::vector<std::size_t> trick_menu_sort_by_cost(
    const TrickDatabase& db, std::span<const std::uint16_t> sources);

// The record bound to button combination `combo` on the page with class mask `mask`, if any.
[[nodiscard]] const TrickRecord* trick_menu_assigned_record(
    const TrickDatabase& db, std::span<const std::uint8_t> block, std::uint32_t mask, int combo);

// True when a special trick cannot be added: the row has no special trick and all five slots are used.
[[nodiscard]] bool trick_menu_specials_full(std::span<const std::uint8_t> block, bool row_has_trick);

struct TrickMenuBindResult final {
    enum class Outcome : std::uint8_t {
        Bound,
        SpecialSlotsFull,   // retail plays sound 0x92 (volume 0x2000) and shows message 1
        AlreadyOnOtherCombo // sound 0x92, message 2 naming the other combination's glyphs
    };
    Outcome outcome{Outcome::Bound};
    int other_combo{-1};        // the special combination the trick is already bound to
    std::string other_glyphs;   // its button glyphs, for the message
    friend bool operator==(const TrickMenuBindResult&, const TrickMenuBindResult&) = default;
};

// Bind `record` (a database index) to `combo` on the page with class mask `mask`. Ordinary pages
// assign the combination slot; the special page keeps a record on at most one special combination.
TrickMenuBindResult trick_menu_bind(const TrickDatabase& db, std::span<std::uint8_t> block, std::uint32_t mask,
                                    int combo, int record);

// Clear the binding of `combo`. Retail passes the empty marker 0xff (not -1) on the special page,
// which leaves the combination recorded on a slot whose trick is empty.
void trick_menu_unbind(const TrickDatabase& db, std::span<std::uint8_t> block, std::uint32_t mask, int combo);

} // namespace opentony::runtime
