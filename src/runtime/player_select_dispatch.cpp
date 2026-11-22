#include "auto_views.hpp"
#include "player_select.hpp"

#include <cstring>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

}  // namespace

// The vtable slots of the classes below (slot n is method n): 0 deleting destructor, 1 per-frame update, 2 draw (x, y), 4 the skater view's load, 5 and 6 the
// snaps. The per-frame hook (slot 7) is an engine function and is never dispatched here.
bool PlayerSelect::dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
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
    // this screen's own classes (the window base's snaps for the ones that inherit them)
    auto snap = [&]() {
        if (slot == 5) {
            w_.window_snap_x(object);
            return true;
        }
        if (slot == 6) {
            w_.window_snap_y(object);
            return true;
        }
        return false;
    };
    if (vt == kVtScoreCounter) {
        switch (slot) {
        case 0: counter_delete(object, a0); return true;
        case 1: counter_update(object); return true;
        case 2: counter_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtMedalWindow) {
        switch (slot) {
        case 0: medal_delete(object, a0); return true;
        case 1: medal_update(object); return true;
        case 2: medal_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtPromptWindow) {
        switch (slot) {
        case 0: prompt_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtTextFlag) {
        switch (slot) {
        case 0: flag_delete(object, a0); return true;
        case 1: flag_update(object); return true;
        case 2: w_.text_draw(object, s(a0), s(a1)); return true;
        default: return false;
        }
    }
    if (vt == kVtPlayerSelect) {
        switch (slot) {
        case 0: select_delete(object, a0); return true;
        case 1: select_update_frame(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtBioWindow) {
        switch (slot) {
        case 0: bio_delete(object, a0); return true;
        case 1: bio_update(object); return true;
        case 2: bio_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtSkaterView) {
        switch (slot) {
        case 0: view_delete(object, a0); return true;
        case 1: view_update(object); return true;
        case 2: view_draw(object, s(a0), s(a1)); return true;
        case 4: view_load_skater(object); return true;
        default: return snap();
        }
    }
    if (vt == kVtPageDots) {
        switch (slot) {
        case 0: dots_delete(object, a0); return true;
        case 1: dots_update(object); return true;
        case 2: dots_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtPlayerHeader) {
        switch (slot) {
        case 0: header_delete(object, a0); return true;
        case 1: header_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtPlayerMenu) {
        switch (slot) {
        case 0: menu_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: menu_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    if (vt == kVtLoadWindow) {
        switch (slot) {
        case 0: load_delete(object, a0); return true;
        case 1: load_update(object); return true;
        case 2: load_draw(object, s(a0), s(a1)); return true;
        default: return snap();
        }
    }
    return false;
}

// The window base reports a child's virtual call by service name; these are the names of slots 0, 1, 2, 5 and 6.
bool PlayerSelect::dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) {
    std::uint32_t slot = 0xff;
    if (std::strcmp(name, kVirtualDelete) == 0) {
        slot = 0;
    } else if (std::strcmp(name, kElementUpdate) == 0) {
        slot = 1;
    } else if (std::strcmp(name, kElementDraw) == 0) {
        slot = 2;
    } else if (std::strcmp(name, kElementSnapX) == 0) {
        slot = 5;
    } else if (std::strcmp(name, kElementSnapY) == 0) {
        slot = 6;
    } else {
        return false;
    }
    return dispatch(object, slot, args);
}

}  // namespace opentony::runtime::frontend
