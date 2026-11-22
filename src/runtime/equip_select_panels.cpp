#include "equip_select.hpp"
#include "equip_views.hpp"

namespace opentony::runtime::frontend {

using namespace views;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

// Retail globals
constexpr std::uint32_t kEqCareer = 0x00561c64;
constexpr std::uint32_t kEqCharacter = 0x00561c68;
constexpr std::uint32_t kEqSet = 0x00561cc4;
constexpr std::uint32_t kEqIndex = 0x00561cc8;
constexpr std::uint32_t kViewRect = 0x0052cb54;          // pointer to the four-word rectangle the next view is drawn into
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kHighResolution = 0x029da3a0;
constexpr std::uint32_t kFillHeight = 0x00527d54;
constexpr std::uint32_t kCamera = 0x0055f7c8;
constexpr std::uint32_t kCameraWords = 0x0055f7cc;       // three words: the camera position
constexpr std::uint32_t kCameraMatrix = 0x0055f7fc;

constexpr std::uint32_t kFontS2Font1 = 0x00526acc;
constexpr std::uint32_t kFontS2Font2 = 0x0052544c;
constexpr std::uint32_t kFontS2Menu1 = 0x00527960;

}  // namespace

// ---- the stat dots ---------------------------------------------------------------------------------------------------------------------------------------

// 0x004455c0: four rows of dots (one stat each) drawn from one picture.
std::uint32_t EquipSelect::dots_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    const RuntimeEquipStatDotsView dots(m_, self);
    w_.element_construct(self, x, y, start_x, start_y);
    dots.element().set_vtable(GuestPtr<void>{kVtStatDots});
    std::uint32_t memory = operator_new(0x5c);
    const std::uint32_t sprite = memory != 0 ? sprite_object(memory, 0x0052b730) : 0;
    dots.set_sprite(GuestPtr<void>{sprite});
    for (std::uint32_t i = 0; i < 4; ++i) {
        memory = operator_new(0x18);
        dots.set_rows(i, GuestPtr<void>{memory != 0 ? menu_.dots_construct(memory, dots.sprite().address, i + 4, 5) : 0});
    }
    dots.set_field_3c(-1);
    dots.set_field_44(-1);
    dots.set_field_40(-1);
    return self;
}

std::uint32_t EquipSelect::dots_delete(std::uint32_t self, std::uint32_t flag) {
    dots_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void EquipSelect::dots_destroy(std::uint32_t self) {
    const RuntimeEquipStatDotsView dots(m_, self);
    dots.element().set_vtable(GuestPtr<void>{kVtStatDots});
    for (std::uint32_t i = 0; i < 4; ++i) {
        delete_child(dots.rows(i).address);
    }
    delete_child(dots.sprite().address);
    w_.element_destroy(self);
}

// 0x00445740: the four rows, eight pixels apart.
void EquipSelect::dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const RuntimeEquipStatDotsView dots(m_, self);
    for (std::uint32_t row = 0; row < 4; ++row) {
        menu_.dots_draw(dots.rows(row).address, dots.element().x() + x, dots.element().y() + static_cast<std::int32_t>(8 * row) + y);
    }
}

// 0x00445780: each row shows the stat of the last deck the character owns; when the highlighted deck is the character's own and not owned, the dots between
// the two values show what buying it would add.
void EquipSelect::dots_update(std::uint32_t self) {
    const RuntimeEquipStatDotsView dots(m_, self);
    for (std::uint32_t row = 0; row < 4; ++row) {
        const std::int32_t last = count_owned_decks() - 1;
        const std::int32_t character = s(global_word(m_, kEqCharacter));
        const std::int32_t base = deck_stat(row, character, last);
        std::int32_t value = base;
        if (!owns_deck(s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex))) &&
            global_word(m_, kEqSet) == global_word(m_, kEqCharacter)) {
            value = deck_stat(row, s(global_word(m_, kEqCharacter)), last + 1);
        }
        menu_.dots_set_range(dots.rows(row).address, u(base), value);
    }
    w_.element_update(self);
}

// ---- the cash window -------------------------------------------------------------------------------------------------------------------------------------

// 0x00445920: the "cash" and "cost" captions, the two counters beside them and the frame.
std::uint32_t EquipSelect::cash_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    const RuntimeEquipCashWindowView cash(m_, self);
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(cash.font_a_address());
    font_construct(cash.font_b_address());
    cash.element().set_vtable(GuestPtr<void>{kVtCashWindow});
    cash.set_x(x);
    const RuntimeFontView font_a = cash.font_a();
    font_from(cash.font_a_address(), kFontS2Menu1);
    font_a.set_horizontal_align(0);
    font_a.set_vertical_align(3);
    f_.set_rgb(cash.font_a_address(), 0x80, 0x80, 0x80);
    font_a.set_shadow(0);
    font_a.set_spacing(0);
    font_a.set_field_10(0);
    font_a.set_scale(0x1000);
    const RuntimeFontView font_b = cash.font_b();
    font_from(cash.font_b_address(), kFontS2Font2);
    font_b.set_horizontal_align(0);
    font_b.set_vertical_align(3);
    f_.set_rgb(cash.font_b_address(), 0x80, 0x80, 0x80);
    font_b.set_shadow(0);
    font_b.set_spacing(0);
    auto caption = [&](std::uint32_t slot, std::uint32_t variable, std::int32_t at_y) {
        const std::uint32_t memory = operator_new(0x30);
        cash.set_captions(slot, GuestPtr<void>{memory != 0 ? w_.text_construct(memory, global_word(m_, variable), cash.font_a_address(), 0, at_y, -1, -1) : 0});
    };
    auto counter = [&](std::uint32_t slot, std::int32_t at_y) {
        const std::uint32_t memory = operator_new(0xbc);
        cash.set_counters(slot, GuestPtr<void>{memory != 0 ? engine_value(0x004a1440, {memory, 0, cash.font_b_address(), 0x54, u(at_y)}) : 0});
    };
    caption(0, 0x0052e924, 0xc);
    counter(0, 0xc);
    caption(1, 0x0052e928, 0x18);
    counter(1, 0x18);
    const std::uint32_t memory = operator_new(0x2c);
    cash.set_frame(GuestPtr<void>{memory != 0 ? w_.sprite_construct(memory, 0x0052b780, -0x14, -10, -1, -1, 0, 0) : 0});
    for (std::uint32_t i = 0; i < 2; ++i) {
        w_.window_add(self, cash.captions(i).address);
        w_.window_add(self, cash.counters(i).address);
    }
    w_.window_add(self, cash.frame().address);
    cash.set_index(-1);
    cash.set_set(-1);
    cash.set_amount(-1);
    return self;
}

std::uint32_t EquipSelect::cash_delete(std::uint32_t self, std::uint32_t flag) {
    cash_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void EquipSelect::cash_destroy(std::uint32_t self) {
    const RuntimeEquipCashWindowView cash(m_, self);
    cash.element().set_vtable(GuestPtr<void>{kVtCashWindow});
    font_destroy(cash.font_b_address());
    font_destroy(cash.font_a_address());
    w_.window_destroy(self);
}

// 0x00445d80: when the highlighted deck or the character's cash changed, the counters follow (the cost reads --- for another character's deck).
void EquipSelect::cash_update(std::uint32_t self) {
    const RuntimeEquipCashWindowView cash(m_, self);
    if (u(cash.set()) == global_word(m_, kEqSet) && u(cash.index()) == global_word(m_, kEqIndex)) {
        if (cash.amount() == int_at(m_, career::record(static_cast<int>(global_word(m_, kEqCharacter))) + career::kRecCash)) {
            w_.window_update(self);
            return;
        }
    }
    cash.set_amount(s(word_at(m_, career::record(static_cast<int>(global_word(m_, kEqCharacter))) + career::kRecCash)));
    cash.set_set(s(global_word(m_, kEqSet)));
    cash.set_index(s(global_word(m_, kEqIndex)));
    engine(0x004a1540, {cash.counters(0).address, word_at(m_, career::record(static_cast<int>(global_word(m_, kEqCharacter))) + career::kRecCash)});
    if (is_current_character(s(global_word(m_, kEqSet)))) {
        const std::int32_t price = deck_price(s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)));
        engine(0x004a1540, {cash.counters(1).address, u(price)});
        w_.window_update(self);
        return;
    }
    engine(0x004a1540, {cash.counters(1).address, 0xffffffffu});
    w_.window_update(self);
}

// 0x00445e70: shown (at its own x) or slid off the left.
void EquipSelect::cash_set_visible(std::uint32_t self, std::uint32_t visible) {
    const RuntimeEquipCashWindowView cash(m_, self);
    if ((visible & 0xff) != 0) {
        cash.set_shown(static_cast<std::uint8_t>(visible));
        cash.element().set_target_x(cash.x());
        return;
    }
    cash.element().set_target_x(s(0xffffff4cu));
    cash.set_shown(0);
}

// ---- the view ------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00445ea0: four arrows, the camera and fog the racks are drawn with, the deck rack and the truck rack; the deck page shows first.
std::uint32_t EquipSelect::view_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    const RuntimeEquipViewView view(m_, self);
    w_.element_construct(self, x, y, start_x, start_y);
    view.element().set_vtable(GuestPtr<void>{kVtEquipView});
    static const std::uint32_t kArrowFiles[4] = {0x0052542c, 0x0052543c, 0x0052b79c, 0x0052b78c};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t memory = operator_new(0x5c);
        view.set_arrows(i, GuestPtr<void>{memory != 0 ? sprite_object(memory, kArrowFiles[i]) : 0});
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t sprite = view.arrows(i).address;
        set_byte_at(m_, sprite + 0xa, 1);
        set_byte_at(m_, sprite + 0xb, 1);
    }
    view.set_blink(0);
    engine(0x004f5430, {kCameraMatrix});
    const RuntimeIntVector3View camera(m_, kCameraWords);
    camera.set_x(0);
    camera.set_y(0);
    camera.set_z(s(0xfffffed4u));
    engine(0x004e8460, {kCameraMatrix, kCameraWords});
    engine(0x00464710, {10, 0x1770, 0x800});
    engine(0x004645f0);
    view.set_width(0xfa);
    view.set_height(0xb4);
    const std::uint32_t deck_memory = alloc_zeroed(guest::kRuntimeEquipDeckRackSize);
    view.set_racks(0, GuestPtr<guest::RuntimeEquipRackBase>{deck_memory != 0 ? engine_value(0x004469f0, {deck_memory}) : 0});
    const std::uint32_t truck_memory = alloc_zeroed(guest::kRuntimeEquipTruckRackSize);
    view.set_racks(1, GuestPtr<guest::RuntimeEquipRackBase>{truck_memory != 0 ? engine_value(0x00448230, {truck_memory}) : 0});
    view_set_page(self, 0);
    return self;
}

std::uint32_t EquipSelect::view_delete(std::uint32_t self, std::uint32_t flag) {
    view_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void EquipSelect::view_destroy(std::uint32_t self) {
    const RuntimeEquipViewView view(m_, self);
    view.element().set_vtable(GuestPtr<void>{kVtEquipView});
    for (std::uint32_t i = 0; i < 2; ++i) {
        delete_child(view.racks(i).address);
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        delete_child(view.arrows(i).address);
    }
    w_.element_destroy(self);
}

// 0x00446150: both racks, then the element's own slide.
void EquipSelect::view_update(std::uint32_t self) {
    const RuntimeEquipViewView view(m_, self);
    for (std::uint32_t i = 0; i < 2; ++i) {
        vcall_item(kElementUpdate, view.racks(i).address, 1);
    }
    w_.element_update(self);
}

// 0x00446180: the arrows blink at the sides (and above and below when the rack has more than one set), the camera view is set to the box the racks sit in, the
// racks draw, and a frame goes round the box when the element is flagged.
void EquipSelect::view_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const RuntimeEquipViewView view(m_, self);
    const RuntimeFrontElementView element = view.element();
    const std::int32_t width = view.width();
    const std::int32_t left = x - (width >> 1) + element.x();
    const std::int32_t height = view.height();
    const std::int32_t right = width + left;
    const std::int32_t top = element.y() - (height >> 1) + y;
    const std::int32_t bottom = height + top;
    const std::int32_t inner_right = right - 0x1e;
    const std::int32_t below = bottom + 8;
    if (view.blink() == 0) {
        view.set_blink(0x28);
    }
    auto arrow = [&](std::uint32_t index, std::int32_t at_x, std::int32_t at_y) {
        s_.note(kSpriteDrawService, {view.arrows(index).address, u(at_x), u(at_y), 0});
    };
    if (view.blink() > 10) {
        const std::int32_t middle = (bottom + top) >> 1;
        arrow(0, left + 0x1e, middle);
        arrow(1, inner_right, middle);
        const RuntimeEquipRackBaseView rack(m_, view.racks(view.page()).address);
        if (rack.single() == 0) {
            const std::int32_t centre = (left + right) >> 1;
            arrow(2, centre, top - 2);
            arrow(3, centre, below);
        }
    }
    view.set_blink(view.blink() - 1);
    // the camera's rectangle: 16-bit words scaled from the 512 x 240 layout to the screen
    const RuntimeViewportRectView rect(m_, global_word(m_, kViewRect));
    rect.set_left(static_cast<std::uint16_t>(left));
    rect.set_top(static_cast<std::uint16_t>(top));
    rect.set_right(static_cast<std::uint16_t>(right));
    rect.set_bottom(static_cast<std::uint16_t>(bottom));
    const std::uint32_t screen_width = global_word(m_, kScreenWidth);
    const std::uint32_t screen_height = global_word(m_, kScreenHeight);
    rect.set_left(static_cast<std::uint16_t>((static_cast<std::uint32_t>(rect.left()) * screen_width) >> 9));
    rect.set_right(static_cast<std::uint16_t>((static_cast<std::uint32_t>(rect.right()) * screen_width) >> 9));
    auto tall = [&](std::uint16_t value) {
        const std::uint32_t product = static_cast<std::uint32_t>(value) * screen_height;
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>((static_cast<std::uint64_t>(0x88888889u) * product) >> 32) >> 7);
    };
    rect.set_top(tall(rect.top()));
    rect.set_bottom(tall(rect.bottom()));
    engine(0x0045e8e0, {kCamera, global_word(m_, kViewRect), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
    const std::uint32_t fill = global_word(m_, kFillHeight);
    auto box = [&]() {
        s_.note(kSvcFillRect, {u(left + 0x1e), u(top - 8), u(inner_right - (left + 0x1e)), u(below - (top - 8)), 1});
    };
    auto full = [&]() { s_.note(kSvcFillRect, {0, 0, 0x200, fill, 0}); };
    if (global_word(m_, kHighResolution) != 0) {
        box();
    } else {
        full();
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        vcall_item(kElementDraw, view.racks(i).address, 2, {u(x + element.x()), u(y + element.y())});
    }
    if (global_word(m_, kHighResolution) != 0) {
        full();
    } else {
        box();
    }
    engine(0x0045f3f0);
    if (element.hidden() != 0) {
        engine(0x004acf20, {u(left), u(top), u(right), u(top), 0xa000});
        engine(0x004acf20, {u(left), u(bottom), u(right), u(bottom), 0xa000});
        engine(0x004acf20, {u(left), u(top), u(left), u(bottom), 0xa000});
        engine(0x004acf20, {u(right), u(top), u(right), u(bottom), 0xa000});
    }
}

// 0x00446420: the page's rack takes the move (0 right, 1 up, 2 left, 3 down).
void EquipSelect::view_move(std::uint32_t self, std::uint32_t direction) {
    const RuntimeEquipViewView view(m_, self);
    vcall_item("rack_v3", view.racks(view.page()).address, 3, {direction});
}

// 0x00446440: the page shown; each rack is told whether it is the active one.
void EquipSelect::view_set_page(std::uint32_t self, std::uint32_t page) {
    const RuntimeEquipViewView view(m_, self);
    view.set_page(page);
    for (std::uint32_t i = 0; i < 2; ++i) {
        rack_set_active(view.racks(i).address, i == view.page() ? 1 : 0);
    }
}

void EquipSelect::view_clear_flag(std::uint32_t self, std::int32_t set, std::int32_t index) {
    const RuntimeEquipViewView view(m_, self);
    deck_rack_clear_flag(view.racks(0).address, set, index);
}

// ---- the name window -------------------------------------------------------------------------------------------------------------------------------------

// 0x00446490: one flagged text element, sliding in from the left.
std::uint32_t EquipSelect::name_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    const RuntimeEquipNameWindowView name(m_, self);
    w_.window_construct(self, x, y, start_x, start_y);
    font_construct(name.font_address());
    name.element().set_vtable(GuestPtr<void>{kVtNameWindow});
    font_from(name.font_address(), kFontS2Font1);
    const RuntimeFontView font = name.font();
    font.set_horizontal_align(0);
    font.set_vertical_align(3);
    font.set_red(0x80);
    font.set_green(0x80);
    font.set_blue(0x80);
    font.set_shadow(0);
    font.set_spacing(0);
    font.set_field_10(0);
    font.set_scale(0x1000);
    const std::uint32_t memory = operator_new(0x34);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = engine_value(0x004a1ed0, {memory, 0x005252f0, name.font_address(), 0, 0xfffffefau, 0, 0xffffffffu, 0xffffffffu});
    }
    name.set_x(name.element().target_x());
    name.set_text(GuestPtr<RuntimeFrontElement>{text});
    w_.window_add(self, text);
    name.set_wanted_set(0xffffffffu);
    name.set_wanted_index(0xffffffffu);
    return self;
}

std::uint32_t EquipSelect::name_delete(std::uint32_t self, std::uint32_t flag) {
    name_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void EquipSelect::name_destroy(std::uint32_t self) {
    const RuntimeEquipNameWindowView name(m_, self);
    name.element().set_vtable(GuestPtr<void>{kVtNameWindow});
    font_destroy(name.font_address());
    w_.window_destroy(self);
}

// 0x004466d0: a new deck slides the old name out (as far as the wider of the two names), then the new name slides in: its own name, "unlock with <character>"
// for another character's deck the player does not have, or "unlock in career mode" (outside the career an unowned deck of the character's own).
void EquipSelect::name_update(std::uint32_t self, std::int32_t set, std::int32_t index) {
    const RuntimeEquipNameWindowView name(m_, self);
    const RuntimeFrontElementView element = name.element();
    if (u(set) != name.wanted_set() || u(index) != name.wanted_index()) {
        name.set_wanted_set(u(set));
        name.set_wanted_index(u(index));
        const std::uint32_t old_entry = engine_value(0x004547f0, {name.shown_set(), name.shown_index()});
        const std::int32_t old_width = f_.text_width(name.font_address(), word_at(m_, old_entry + 4));
        const std::uint32_t new_entry = engine_value(0x004547f0, {name.wanted_set(), name.wanted_index()});
        const std::int32_t new_width = f_.text_width(name.font_address(), word_at(m_, new_entry + 4));
        const std::int32_t wider = old_width > new_width ? old_width : new_width;
        element_at(m_, name.text().address).set_target_x(-wider - element.x() - 10);
        name.set_state(3);
    }
    if (name.state() == 0) {
        name.set_shown_set(name.wanted_set());
        name.set_shown_index(name.wanted_index());
        element_at(m_, name.text().address).set_target_x(0);
        const std::int32_t shown = s(name.shown_set());
        const std::int32_t shown_index = s(name.shown_index());
        std::uint32_t text;
        if (name.shown_set() == global_word(m_, kEqCharacter) || owns_deck(shown, shown_index)) {
            if (global_byte(m_, kEqCareer) != 0 || owns_deck(shown, shown_index)) {
                text = word_at(m_, engine_value(0x004547f0, {u(shown), u(shown_index)}) + 4);
            } else {
                text = global_word(m_, 0x0052e90c);
            }
        } else {
            // the first word of the other character's name, in the screens' scratch line
            scratch_line({});
            character_short_name(shown, kScratchLine + 0xc0);
            const std::string line = retail_format(m_.cstring(global_word(m_, 0x0052e910), 0x40), {m_.cstring(kScratchLine + 0xc0, 0x10)}, {});
            text = scratch_line(line);
        }
        w_.text_set_string(name.text().address, text);
        name.set_state(1);
        element.set_target_x(name.x());
        element.set_x(name.x());
    } else if (name.state() == 3 && element_at(m_, name.text().address).arrived() != 0) {
        name.set_state(0);
    }
    w_.window_update(self);
}

}  // namespace opentony::runtime::frontend
