#include "auto_views.hpp"
#include "skater_windows.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

// Virtual dispatch for the pages (the toolkit's extra dispatcher): the slots a page implements itself run natively; anything else is reported by
// the caller. Slot 0 deletes, 1 updates, 2 draws (x, y); the points block's slot 4 refreshes it.
bool SkaterWindows::dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    const std::uint32_t a1 = args.size() < 2 ? 0 : args[1];
    const auto s32 = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };
    if (vt == kPersonalVt) {
        if (slot == 1) {
            personal_update(object);
            return true;
        }
        if (slot == 2) {
            personal_draw(object, s32(a0), s32(a1));
            return true;
        }
    } else if (vt == kAppearanceVt) {
        if (slot == 0) {
            appearance_delete(object, a0);
            return true;
        }
        if (slot == 1) {
            appearance_update(object);
            return true;
        }
        if (slot == 2) {
            appearance_draw(object, s32(a0), s32(a1));
            return true;
        }
    } else if (vt == kStatPointsVt) {
        if (slot == 0) {
            points_delete(object, a0);
            return true;
        }
        if (slot == 4) {
            points_update(object);
            return true;
        }
    } else if (vt == kStatWindowVt) {
        if (slot == 1) {
            stat_update(object);
            return true;
        }
        if (slot == 2) {
            stat_draw(object, s32(a0), s32(a1));
            return true;
        }
    }
    return false;
}

}  // namespace opentony::runtime::frontend
