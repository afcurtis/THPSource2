#pragma once

#include "trg_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opentony::trg {

struct TriggerGapDefinition {
    std::uint16_t flags{};
    std::uint16_t divider{};
    std::int16_t score{};
    std::string name;
    // Record word at +0x02; retail's early-level table fills it, use unknown.
    std::uint16_t unknown_word{};
};

// The PC executable uses 0x2c-byte records. The first four fields are two
// flag/unknown words, a divider ID, and a signed score; the remaining 36 bytes
// are the display name. A divider of 0xffff terminates a table.
class GapTable final {
public:
    static GapTable parse(std::span<const std::byte> records);
    static GapTable from_definitions(std::vector<TriggerGapDefinition> definitions);

    [[nodiscard]] const TriggerGapDefinition* find(std::uint16_t divider) const noexcept;
    [[nodiscard]] const std::vector<TriggerGapDefinition>& definitions() const noexcept {
        return definitions_;
    }

private:
    explicit GapTable(std::vector<TriggerGapDefinition> definitions)
        : definitions_(std::move(definitions)) {}

    std::vector<TriggerGapDefinition> definitions_;
};

// The three compiled retail tables. Front_LaunchGameLevel (0x004544a0) picks
// one for the whole level: the custom-park table (0x005283d0) when the
// custom-park flag is set, else the early-level table (0x00541990) for levels
// below 10 and level 13, else the late-level/Warehouse table (0x00546110).
enum class GapTableVariant { EarlyLevels, LateLevels, CustomPark };

[[nodiscard]] GapTableVariant select_gap_table_variant(int current_level, bool custom_park) noexcept;
[[nodiscard]] const GapTable& retail_gap_table(GapTableVariant variant);

// Late-level table (Warehouse is level 12).
[[nodiscard]] const GapTable& retail_warehouse_gap_table();

} // namespace opentony::trg
