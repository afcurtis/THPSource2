#include "auto_views.hpp"
#include "menu_toolkit.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

}  // namespace

std::string engine_name(std::uint32_t address) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "engine_%08x", address);
    return buffer;
}

void MenuToolkit::write_string(std::uint32_t dest, const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    m_.write(dest, bytes);
}

// ---- virtual calls on menu items ---------------------------------------------------------------------------
//
// The vtables of the classes this toolkit implements and which slot is which method (the slot number is the method number; the
// vtable's address, read from the object, names the class):
//   0x00518e10 MenuControl, 0x00518e30 MenuControlB: 0 delete, 1 no-op (two arguments), 2 no-op, 3 highlight, 4 value, 5 log, 6 nothing
//   0x00518e54 MenuText, 0x00518e78 MenuRow, 0x00518ed4 MenuTextScaled: 0 delete, 1 position, 2 draw, 3 highlight, 4 value, 5 log, 6 nothing,
//     7 engine, 8 set text (the row has a 9th: set column)
//   0x00518eb0 the slider's text part: 0 delete (adjusting thunk), 1 position, 2 draw, 3 highlight, 4 value, 5 adjust, 6 nothing, 7 engine thunk
//   0x00518ea8 MenuSlider (the dots): 0 delete, 1 engine;   0x00518ea0 MenuDots: 0 delete, 1 returns zero
void MenuToolkit::vcall_item(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t vt = word_at(m_, object);
    const bool text_like = vt == kMenuVtText || vt == kMenuVtRow || vt == kMenuVtTextScaled || vt == kMenuVtSliderDots;
    const bool control_like = vt == kMenuVtControl || vt == kMenuVtControlB;
    if (text_like || control_like) {
        const std::uint32_t a0 = args.empty() ? 0 : args[0];
        const std::uint32_t a1 = args.size() < 2 ? 0 : args[1];
        switch (slot) {
        case 0:
            if (vt == kMenuVtText) {
                text_delete(object, a0);
            } else if (vt == kMenuVtRow) {
                row_delete(object, a0);
            } else if (vt == kMenuVtTextScaled) {
                text_scaled_delete(object, a0);
            } else if (vt == kMenuVtSliderDots) {
                slider_sub_delete_thunk(object, a0);
            } else if (vt == kMenuVtControl) {
                control_delete(object, a0);
            } else {
                control_b_delete(object, a0);
            }
            return;
        case 1:
            if (control_like) {
                return;   // 0x00470530: returns, popping two arguments
            }
            item_set_position(object, s(a0), s(a1));
            return;
        case 2:
            if (control_like) {
                return;   // 0x00470540
            }
            if (vt == kMenuVtText) {
                text_draw(object);
            } else if (vt == kMenuVtRow) {
                row_draw(object);
            } else if (vt == kMenuVtTextScaled) {
                text_scaled_draw(object);
            } else {
                slider_draw(object);
            }
            return;
        case 3:
            control_set_highlighted(object, a0);
            return;
        case 4:
            control_set_value(object, a0);
            return;
        case 5:
            if (vt == kMenuVtSliderDots) {
                slider_adjust(object, a0);
            } else {
                control_log_navigate(object);
            }
            return;
        case 6:
            return;
        case 8:
            if (!control_like) {
                text_set_text(object, a0);
                return;
            }
            break;
        case 9:
            if (vt == kMenuVtRow) {
                row_set_column(object, a0, s(a1), args.size() > 2 ? args[2] : 0, args.size() > 3 ? args[3] : 0);
                return;
            }
            break;
        default:
            break;
        }
    } else if (vt == kMenuVtSlider && slot == 0) {
        slider_delete(object, args.empty() ? 0 : args[0]);
        return;
    } else if (vt == kMenuVtDots && slot == 0) {
        dots_delete(object, args.empty() ? 0 : args[0]);
        return;
    } else if (vt == kMenuVtProperties && slot == 0) {
        properties_delete(object, args.empty() ? 0 : args[0]);
        return;
    }
    if (extra_ && extra_(object, slot, args)) {
        return;
    }
    vcall(name, slot, object, std::move(args));
}

// ---- virtual calls on lists, grids, windows and wrappers ---------------------------------------------------
//   0x00518ef8 MenuList: 0 delete, 1 arrows, 2 layout, 3 draw, 4 snap, 5 navigate, 6 add, 7 engine
//   0x00518f18 ScrollList: the same, with the scrolling methods;   0x00518f38 MenuGrid: 0 delete, 1 engine
//   0x00518f40 NameEntry: 0 delete, 1 window update, 2 draw;   0x00518f60 MenuWrapper: 0 delete, 1 test complete, 2 poll, 3 id
void MenuToolkit::vcall_list(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    if (vt == kMenuVtList || vt == kMenuVtScrollList) {
        const bool scroll = vt == kMenuVtScrollList;
        switch (slot) {
        case 0:
            scroll ? (void)scroll_delete(object, a0) : (void)list_delete(object, a0);
            return;
        case 1:
            list_set_arrows(object, a0, args.size() > 1 ? args[1] : 0, args.size() > 2 ? args[2] : 0);
            return;
        case 2:
            scroll ? scroll_layout(object) : list_layout(object);
            return;
        case 3:
            scroll ? scroll_draw(object) : list_draw(object);
            return;
        case 4:
            scroll ? scroll_follow(object) : list_snap_all(object);
            return;
        case 5:
            scroll ? scroll_navigate(object, a0) : list_navigate(object, a0);
            return;
        case 6:
            scroll ? scroll_add(object, a0) : list_add(object, a0);
            return;
        default:
            break;
        }
    } else if (vt == kMenuVtGrid && slot == 0) {
        grid_delete(object, a0);
        return;
    } else if (vt == kMenuVtNameEntry) {
        if (slot == 0) {
            name_entry_delete(object, a0);
            return;
        }
        if (slot == 1) {
            w_.window_update(object);
            return;
        }
        if (slot == 2) {
            name_entry_draw(object, a0, args.size() > 1 ? args[1] : 0);
            return;
        }
    } else if (vt == kMenuVtWrapper) {
        if (slot == 0) {
            wrapper_delete(object, a0);
            return;
        }
        if (slot == 1) {
            wrapper_test_complete(object);
            return;
        }
    } else if (vt == kMenuVtWrapperSub && slot == 0) {
        wrapper_sub_delete(object, a0);
        return;
    }
    if (extra_ && extra_(object, slot, args)) {
        return;
    }
    vcall(name, slot, object, std::move(args));
}

}  // namespace opentony::runtime::frontend
