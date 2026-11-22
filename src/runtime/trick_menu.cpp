#include "trick_menu.hpp"

#include <algorithm>

namespace opentony::runtime {
namespace {

bool special_page(std::uint32_t mask) noexcept { return (mask & 0x8000u) != 0; }

}  // namespace

std::uint32_t trick_menu_category_mask(int category) noexcept {
    switch (category) {
    case kMenuCategoryManual: return 0x200;
    case kMenuCategoryGrind: return 0x100;
    case kMenuCategoryLip: return 0x800;
    case kMenuCategoryAir: return 0x4000;
    case kMenuCategorySpecial: return 0x8000;
    default: return 0;
    }
}

std::string trick_button_glyphs(std::span<const std::uint16_t> combination) {
    static constexpr char kGlyph[13] = {0, 'G', 'H', 'E', 'F', 'J', 'I', 'K', 'L', 'B', 'D', 'C', 'A'};
    std::string out;
    if (combination.empty() || combination[0] == 0) {
        return out;
    }
    const std::size_t count = combination[0];
    for (std::size_t i = 0; i < count && 1 + i < combination.size(); ++i) {
        const std::uint16_t button = combination[1 + i];
        if (button >= 1 && button <= 12) {
            out.push_back(kGlyph[button]);
        }
    }
    return out;
}

std::vector<std::size_t> trick_menu_sort_by_cost(const TrickDatabase& db, std::span<const std::uint16_t> sources) {
    std::vector<std::size_t> order;
    std::vector<std::int32_t> costs;
    for (std::size_t row = 0; row < sources.size(); ++row) {
        const TrickRecord* record = db.find_by_source(sources[row]);
        if (record == nullptr) {
            throw TrickDatabaseFault("trick menu row names a record the database does not hold");
        }
        const std::int32_t cost = record->cost;
        std::size_t at = 0;
        while (at < order.size() && !(cost < costs[at])) {
            ++at;
        }
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), row);
        costs.insert(costs.begin() + static_cast<std::ptrdiff_t>(at), cost);
    }
    return order;
}

const TrickRecord* trick_menu_assigned_record(const TrickDatabase& db, std::span<const std::uint8_t> block,
                                              std::uint32_t mask, int combo) {
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    if (!special_page(mask)) {
        if (combo < 0 || static_cast<std::size_t>(combo) >= block.size()) {
            throw TrickDatabaseFault("combination slot is outside the binding block");
        }
        const std::uint8_t record = block[static_cast<std::size_t>(combo)];
        if (record != kBindingEmpty) {
            return &db.record(static_cast<std::int8_t>(record));
        }
        return nullptr;
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (static_cast<std::int8_t>(block[kBindingSpecialCombos + slot]) == static_cast<std::int8_t>(combo) &&
            block[kBindingSpecialRecords + slot] != kBindingEmpty) {
            return &db.record(static_cast<std::int8_t>(block[kBindingSpecialRecords + slot]));
        }
    }
    return nullptr;
}

bool trick_menu_specials_full(std::span<const std::uint8_t> block, bool row_has_trick) {
    if (row_has_trick) {
        return false;
    }
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    int used = 0;
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (block[kBindingSpecialRecords + slot] != kBindingEmpty) {
            ++used;
        }
    }
    return used > 4;
}

TrickMenuBindResult trick_menu_bind(const TrickDatabase& db, std::span<std::uint8_t> block, std::uint32_t mask,
                                    int combo, int record) {
    TrickMenuBindResult result;
    if (combo < 0 || combo > 0x2a) {
        db.note("button index out of range");
    }
    if (!special_page(mask)) {
        trick_binding_assign_button(block, db, combo, record);
        return result;
    }
    const auto existing = trick_binding_special_assigned(block, record);
    if (!existing.has_value() || static_cast<std::int8_t>(*existing) == static_cast<std::int8_t>(combo)) {
        if (!trick_binding_assign_special(block, db, combo, record)) {
            result.outcome = TrickMenuBindResult::Outcome::SpecialSlotsFull;
        }
        return result;
    }
    if (*existing == kBindingEmpty) {
        db.note("special trick bound to an empty combination");
    }
    const auto table = special_combos();
    const std::size_t other = static_cast<std::size_t>(static_cast<std::int8_t>(*existing));
    result.outcome = TrickMenuBindResult::Outcome::AlreadyOnOtherCombo;
    result.other_combo = static_cast<std::int8_t>(*existing);
    if (other < table.size()) {
        result.other_glyphs = trick_button_glyphs(table[other]);
    }
    return result;
}

void trick_menu_unbind(const TrickDatabase& db, std::span<std::uint8_t> block, std::uint32_t mask, int combo) {
    if (combo < 0 || combo > 0x2a) {
        db.note("button index out of range");
    }
    if (!special_page(mask)) {
        trick_binding_assign_button(block, db, combo, 0xff);
        return;
    }
    if (!trick_binding_assign_special(block, db, combo, 0xff)) {
        db.note("Special trick should have been removed");
    }
}

} // namespace opentony::runtime
