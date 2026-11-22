#include "auto_views.hpp"
#include "options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

}  // namespace

// The vtable slots of the window-base classes (slot n is method n): 0 deleting destructor, 1 per-frame update, 2 draw (x, y), 5 and 6 the snaps. The per-frame hook (slot 7)
// is an engine function and is never dispatched here; this screen's own classes are replayed on their own (the pages an OptionsMenu holds are fake objects there).
bool OptionsScreen::dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    const std::uint32_t a1 = args.size() < 2 ? 0 : args[1];
    if (slot == 7 || slot == 3) {
        return false;
    }
    if (vt == kVtText) {
        switch (slot) {
        case 0: w_.text_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 2: w_.text_draw(object, s(a0), s(a1)); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtSprite) {
        switch (slot) {
        case 0: w_.sprite_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 2: w_.sprite_draw(object, s(a0), s(a1)); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtWindow) {
        switch (slot) {
        case 0: w_.window_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        case 5: w_.window_snap_x(object); return true;
        case 6: w_.window_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtElement) {
        switch (slot) {
        case 0: w_.element_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    return false;
}

// The window base reports a child's virtual call by service name; map it back to the slot.
bool OptionsScreen::dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) {
    const std::string n = name;
    if (n == kVirtualDelete) {
        return dispatch(object, 0, args);
    }
    if (n == kElementUpdate) {
        return dispatch(object, 1, args);
    }
    if (n == kElementDraw) {
        return dispatch(object, 2, args);
    }
    if (n == kElementSnapX) {
        return dispatch(object, 5, args);
    }
    if (n == kElementSnapY) {
        return dispatch(object, 6, args);
    }
    return false;
}

}  // namespace opentony::runtime::frontend
