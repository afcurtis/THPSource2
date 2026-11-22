#include "equip_select.hpp"
#include "equip_views.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace views;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

// Retail globals
constexpr std::uint32_t kEqCareer = 0x00561c64;          // byte: career mode (buying costs cash)
constexpr std::uint32_t kEqCharacter = 0x00561c68;
constexpr std::uint32_t kEqPlayer = 0x00561c7c;
constexpr std::uint32_t kEqSet = 0x00561cc4;
constexpr std::uint32_t kEqIndex = 0x00561cc8;
constexpr std::uint32_t kEqTruck = 0x00561ccc;
constexpr std::uint32_t kPlayerSet = 0x0056a688;         // + player: the deck set the player is using (byte)
constexpr std::uint32_t kPlayerIndex = 0x0056a2dc;       // + player: the deck within its set (byte)
constexpr std::uint32_t kPlayerTruck = 0x0056a2e0;       // + player: the truck style (byte)
constexpr std::uint32_t kSavedSet = 0x0055fb38;          // + player, + character: the copies of the choice the game keeps
constexpr std::uint32_t kSavedIndex = 0x0055fb36;
constexpr std::uint32_t kSavedCharacter = 0x0055faf8;
constexpr std::uint32_t kCharacterIndex = 0x0055fafa;
constexpr std::uint32_t kCharacterSet = 0x0055fb0e;
constexpr std::uint32_t kPadStride = 0x16c;
constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;
constexpr std::uint32_t kPadUpEdge = 0x0056b099;
constexpr std::uint32_t kPadDownEdge = 0x0056b0a9;
constexpr std::uint32_t kPadToggle = 0x0056b019;
constexpr std::uint32_t kPadConfirm = 0x0056b029;
constexpr std::uint32_t kPadAlternate = 0x0056b0d9;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kGlobalPreManager = 0x0056b834;
constexpr std::uint32_t kFillHeight = 0x00527d54;
constexpr std::uint32_t kLoading = 0x005615d0;

// Assertion messages
constexpr std::uint32_t kMsgOwnOneDeck = 0x0052b73c;         // "player must own at least one deck"

constexpr std::uint32_t kEquipFonts[3] = {0x0052544c, 0x00526acc, 0x00527960};   // s2font2, s2font1, s2menu1

std::uint32_t pad_at(std::uint32_t base, std::uint32_t player) { return base + player * kPadStride; }

// A screen font in the state s2font2 / s2font1 start from: aligned as the screen wants, with the shadow off and the standard scale.
void set_font(const RuntimeFontView& font, std::uint32_t horizontal, bool grey) {
    font.set_horizontal_align(horizontal);
    font.set_vertical_align(3);
    if (grey) {
        font.set_red(0x80);
        font.set_green(0x80);
        font.set_blue(0x80);
    }
    font.set_shadow(0);
    font.set_spacing(0);
    font.set_field_10(0);
    font.set_scale(0x800);
}

}  // namespace

// ---- the screen callbacks and the run loop ----------------------------------------------------------------------------------------------------------

// 0x00444550: an empty function nothing refers to.
void EquipSelect::no_op() {}

// 0x00444560: the screen table asks each screen for its two callbacks (the same pair PlayerSelect fills).
void EquipSelect::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x00444580);
    set_word_at(m_, record + 0x0c, 0x00444590);
}

void EquipSelect::exit_callback() {}

// 0x00444590: `*result = run(argument[0], argument[1])`.
void EquipSelect::screen_call(std::uint32_t argument, std::uint32_t result) {
    set_word_at(m_, result, u(run(int_at(m_, argument), int_at(m_, argument + 4))));
}

// 0x004445b0: `mode` 1 is the career (buying costs cash), `player` the pad that chooses. The loop ends when the pad's back edge arrives (the answer -1) or the
// engine asks to leave.
std::int32_t EquipSelect::run(std::int32_t mode, std::int32_t player) {
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    set_global_word(m_, kEqCharacter, u(sk_.current_character()));
    set_global_word(m_, kEqPlayer, u(player));
    s_.note(kSvcPrintfLog, {0x0052b6bc});
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_byte(m_, kEqCareer, mode == 1 ? 1 : 0);
    set_global_word(m_, kEqSet, u(static_cast<std::int8_t>(byte_at(m_, kPlayerSet + u(player)))));
    set_global_word(m_, kEqIndex, u(static_cast<std::int8_t>(byte_at(m_, kPlayerIndex + u(player)))));
    set_global_word(m_, kEqTruck, u(static_cast<std::int8_t>(byte_at(m_, kPlayerTruck + u(player)))));
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    engine(0x004e8730, {kScratchLine, 0x80, 0, 0});          // (the first argument is the address of a four-word rectangle on the stack)
    for (std::uint32_t name : kEquipFonts) {
        font_load(name);
    }
    const std::uint32_t memory = operator_new(guest::kRuntimeEquipWindowSize);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = seams_ ? engine_value(0x00444980, {memory}) : window_construct(memory);
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    engine(0x004a9330, {global_word(m_, kGlobalPreManager), 0x0052b6ac});
    engine(0x004a9330, {global_word(m_, kGlobalPreManager), 0x0052b6a0});
    scratch_line({});
    const std::uint32_t out = kScratchLine + 0xf8;      // the answer is a local in retail
    set_word_at(m_, out, 0xffffffffu);
    engine(0x004c8f30);
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        if (window_step(window, out)) {
            break;
        }
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        vcall_item(kElementUpdate, window, 1);
        window_draw(window);
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        if (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2) {
            do {
                game_frame_tick();
            } while (global_word(m_, kGlobalSimTick) < global_word(m_, kGlobalLastTick) + 2);
        }
        set_global_word(m_, kGlobalLastTick, global_word(m_, kGlobalSimTick));
        engine(0x00430510);
        engine(0x00466c00);
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
    }
    engine(0x004304b0);
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        delete_object(window);
    }
    font_unload(kEquipFonts[1]);
    font_unload(kEquipFonts[0]);
    font_unload(kEquipFonts[2]);
    return int_at(m_, out);
}

// ---- decks: ownership, price, stats --------------------------------------------------------------------------------------------------------------------

// 0x00444510: two colour bytes (high nibble, low nibble) that mirror each other (or either is zero).
bool EquipSelect::colours_match(std::int32_t a, std::int32_t b) {
    if (a == 0 || b == 0) {
        return true;
    }
    if ((a >> 4) != (b & 0xf)) {
        return false;
    }
    return (b >> 4) == (a & 0xf);
}

// 0x004448c0: a bit of the character's record says which of the set's decks it owns.
bool EquipSelect::owns_deck(std::int32_t set, std::int32_t index) {
    const std::uint32_t record = career::record(set);
    return (((1u << (u(index) & 31)) & word_at(m_, record + career::kRecClassMask)) != 0);
}

// 0x00444810: five times the number of decks owned times 50 (a skater with the custom flag gets the first eight free and pays 5 x (owned - 9) x 50); a deck the
// character owns costs nothing.
std::int32_t EquipSelect::deck_price(std::int32_t set, std::int32_t index) {
    std::int32_t owned = 0;
    for (std::int32_t i = 0; i < 0x14; ++i) {
        if (owns_deck(set, i)) {
            ++owned;
        }
    }
    std::int32_t price;
    if ((byte_at(m_, skater::row(set) + skater::kRowFlags) & 2) != 0) {
        price = owned < 9 ? 0 : (owned * 5 - 0x2d) * 25 * 2;
    } else {
        price = owned * 5 * 25 * 2;
    }
    return owns_deck(set, index) ? 0 : price;
}

// 0x00444880: pay the price, and own the deck.
void EquipSelect::buy_deck(std::int32_t set, std::int32_t index) {
    const std::uint32_t record = career::record(set);
    const std::int32_t price = deck_price(set, index);
    set_word_at(m_, record + career::kRecCash, u(int_at(m_, record + career::kRecCash) - price));
    const std::uint32_t again = career::record(set);
    set_word_at(m_, again + career::kRecClassMask, word_at(m_, again + career::kRecClassMask) | (1u << (u(index) & 31)));
}

bool EquipSelect::is_current_character(std::int32_t set) { return global_word(m_, kEqCharacter) == u(set); }

// 0x00444900: the deck is for the character who is buying, and the character has the cash.
bool EquipSelect::can_afford(std::int32_t set, std::int32_t index) {
    if (!is_current_character(set)) {
        return false;
    }
    const std::int32_t price = deck_price(set, index);
    return int_at(m_, career::record(static_cast<int>(global_word(m_, kEqCharacter))) + career::kRecCash) >= price;
}

// 0x00444940: FileIO_AllocLoadedBuffer(size, 0, 1, 0), zero filled.
std::uint32_t EquipSelect::alloc_zeroed(std::uint32_t size) {
    s_.note(kAllocBuffer, {size, 0, 1, 0});
    const std::uint32_t block = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    for (std::uint32_t i = 0; i < ((size + 3) >> 2); ++i) {
        set_word_at(m_, block + 4 * i, 0);
    }
    return block;
}

void EquipSelect::free_block(std::uint32_t block) { s_.note(kMemDeleteService, {block}); }

// 0x00445810: the stat of the deck at the end of the owned decks (kinds 0, 1, 2 and 4 are the entry's words, 3 is the truck's); another kind answers itself.
std::int32_t EquipSelect::deck_stat(std::uint32_t kind, std::int32_t set, std::int32_t index) {
    auto entry = [&]() { return engine_value(0x004547f0, {u(set), u(index)}); };
    switch (kind) {
    case 0: return int_at(m_, entry() + 0xc);
    case 1: return int_at(m_, entry() + 0x10);
    case 2: return int_at(m_, entry() + 0x14);
    case 3: return static_cast<std::int8_t>(byte_at(m_, kPlayerTruck + global_word(m_, kEqPlayer))) * 2 + 1;
    case 4: return int_at(m_, entry() + 0x1c);
    default: return static_cast<std::int32_t>(kind);
    }
}

// 0x004458b0: how many decks the character owns (at least one).
std::int32_t EquipSelect::count_owned_decks() {
    std::int32_t owned = 0;
    for (std::int32_t i = 0; i < 0x14; ++i) {
        if (owns_deck(static_cast<std::int32_t>(global_word(m_, kEqCharacter)), i)) {
            ++owned;
        }
    }
    check_at(owned != 0, kMsgOwnOneDeck);
    return owned;
}

// 0x004468a0: the first word of the character's name (up to ten characters).
void EquipSelect::character_short_name(std::int32_t character, std::uint32_t out) {
    const std::uint32_t name = skater::row(character) + skater::kRowName;
    for (std::uint32_t i = 0; i < 10; ++i) {
        const std::uint8_t c = byte_at(m_, name + i);
        set_byte_at(m_, out + i, c);
        if (c == ' ') {
            set_byte_at(m_, out + i, 0);
            return;
        }
    }
}

// ---- the window --------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t EquipSelect::sprite_object(std::uint32_t memory, std::uint32_t file) {
    s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
    return static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
}

// 0x00444980: the window, two fonts, the background, the six prompts and the five pages' windows. Every other class is built by its own constructor (a seam here).
std::uint32_t EquipSelect::window_construct(std::uint32_t self) {
    const RuntimeEquipWindowView window_view(m_, self);
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(window_view.font_a_address());
    font_construct(window_view.font_b_address());
    window_view.element().set_vtable(GuestPtr<void>{kVtEquipWindow});
    window_view.set_background(GuestPtr<void>{background_sprite(0x0052b724)});
    // the first font: s2font2, grey, no outline
    font_from(window_view.font_a_address(), kEquipFonts[0]);
    set_font(window_view.font_a(), 0, true);
    // the second: s2font1
    font_from(window_view.font_b_address(), kEquipFonts[1]);
    set_font(window_view.font_b(), 1, false);
    // the six prompts: the picture, its caption (an empty one), x, y; the first five slide in from the foot of the screen
    struct Prompt {
        std::uint32_t file;
        std::int32_t x;
        bool slides;
    };
    static const Prompt kPromptTable[6] = {{0x0052b714, 0x141, true}, {0x0052b704, 0x6e, true}, {0x0052b6f0, 0xd2, true},
                                           {0x0052540c, 0x19c, true}, {0x00525400, 0x21, true}, {0x0052b6e0, 0xd2, false}};
    for (std::uint32_t i = 0; i < 6; ++i) {
        const Prompt& p = kPromptTable[i];
        std::uint32_t window = 0;
        if (p.slides) {
            window = prompt(p.file, 0x005252f0, window_view.font_a_address(), p.x, 0xdc);
        } else {
            const std::uint32_t memory = operator_new(0xb0);
            if (memory != 0) {
                s_.note(kSvcPromptConstruct, {memory, p.file, 0x005252f0, window_view.font_a_address(), u(p.x), global_word(m_, kFillHeight) + 0x28, 0xffffffffu, 0xffffffffu},
                        m_.cstring(p.file));
                window = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
            }
        }
        window_view.set_prompts(i, GuestPtr<RuntimeFrontElement>{window});
    }
    for (std::uint32_t i = 0; i < 6; ++i) {
        w_.window_add(self, window_view.prompts(i).address);
    }
    std::uint32_t memory = operator_new(guest::kRuntimeEquipStatDotsSize);
    std::uint32_t dots = 0;
    if (memory != 0) {
        dots = engine_value(0x004455c0, {memory, 0x92, 0x2e, 0xffffffffu, 0xffffffffu});
    }
    memory = operator_new(guest::kRuntimeEquipNameWindowSize);
    std::uint32_t name = 0;
    if (memory != 0) {
        name = engine_value(0x00446490, {memory, 0x3d, 0x24, 0xffffffffu, 0xffffffffu});
    }
    window_view.set_name(GuestPtr<guest::RuntimeEquipNameWindow>{name});
    memory = operator_new(guest::kRuntimeEquipCashWindowSize);
    std::uint32_t cash = 0;
    if (memory != 0) {
        cash = engine_value(0x00445920, {memory, 0x4b, 0xa4, 0xffffff4cu, 0xa4});
    }
    window_view.set_cash(GuestPtr<guest::RuntimeEquipCashWindow>{cash});
    cash_set_visible(cash, global_byte(m_, kEqCareer));
    memory = operator_new(guest::kRuntimeEquipViewSize);
    std::uint32_t view = 0;
    if (memory != 0) {
        view = engine_value(0x00445ea0, {memory, 0x172, 0x70, 0xffffffffu, 0xffffffffu});
    }
    window_view.set_view(GuestPtr<RuntimeEquipView>{view});
    // a logo for each character whose row has neither of the flags 0x22 (the picture slides in from the right when the deck is chosen)
    for (std::uint32_t i = 0; i < 20; ++i) {
        const std::uint32_t row = skater::row(static_cast<int>(i));
        if ((word_at(m_, row + 0x15c) & 0x22) == 0) {
            const std::uint32_t stem = word_at(m_, row + 0x44);
            // the two characters at +3 and +4 are copied into a three-byte local (the third is zero): sprintf reads them as a string
            std::string tag;
            if (byte_at(m_, stem + 3) != 0) {
                tag.push_back(static_cast<char>(byte_at(m_, stem + 3)));
                if (byte_at(m_, stem + 4) != 0) {
                    tag.push_back(static_cast<char>(byte_at(m_, stem + 4)));
                }
            }
            const std::string line = retail_format("s2logo%s.bmp", {tag}, {});
            const std::uint32_t file = scratch_line(line);
            memory = operator_new(0x2c);
            std::uint32_t logo = 0;
            if (memory != 0) {
                logo = w_.sprite_construct(memory, file, 0x320, 0x39, -1, -1, 1, 1);
            }
            window_view.set_logos(i, GuestPtr<RuntimeFrontElement>{logo});
            w_.window_add(self, logo);
        } else {
            window_view.set_logos(i, GuestPtr<RuntimeFrontElement>{0});
        }
    }
    w_.window_add(self, window_view.cash().address);
    w_.window_add(self, window_view.view().address);
    w_.window_add(self, dots);
    w_.window_add(self, window_view.name().address);
    window_view.set_dialog_open(0);
    window_view.set_dialog(GuestPtr<void>{0});
    window_view.set_done(0);
    window_view.set_page(0);
    return self;
}

std::uint32_t EquipSelect::window_delete(std::uint32_t self, std::uint32_t flag) {
    window_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00445010: the background, the two fonts, the window base.
void EquipSelect::window_destroy(std::uint32_t self) {
    const RuntimeEquipWindowView window(m_, self);
    window.element().set_vtable(GuestPtr<void>{kVtEquipWindow});
    const std::uint32_t sprite = window.background().address;
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    font_destroy(window.font_b_address());
    font_destroy(window.font_a_address());
    w_.window_destroy(self);
}

// 0x004450a0: a dialog updates, the window and the background draw.
void EquipSelect::window_draw(std::uint32_t self) {
    const RuntimeEquipWindowView window(m_, self);
    const std::uint32_t dialog = window.dialog().address;
    if (dialog != 0) {
        s_.note("dialog_v1", {dialog});
    }
    w_.window_draw(self, 0, 0);
    engine(0x004d7990, {window.background().address, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x00445550: back (with no dialog open) ends the screen with the answer -1.
bool EquipSelect::window_step(std::uint32_t self, std::uint32_t result) {
    const RuntimeEquipWindowView window(m_, self);
    set_word_at(m_, result, 1);
    const std::uint32_t player = global_word(m_, kEqPlayer);
    if (byte_at(m_, pad_at(kPadBack, player)) != 0 && window.dialog_open() == 0) {
        set_byte_at(m_, pad_at(kPadBack, player), 0);
        sfx(0x93);
        set_word_at(m_, result, 0xffffffffu);
        window.set_done(1);
    }
    return window.done() != 0;
}

void EquipSelect::window_slot4() {}

// 0x004450e0: the dialog's answer, the four directions, the page switch, and a confirm that equips the deck or asks to buy it.
void EquipSelect::window_update(std::uint32_t self) {
    const RuntimeEquipWindowView window(m_, self);
    if (window.dialog_open() == 1) {
        const std::uint32_t box = window.dialog().address;
        s_.note("dialog_v2", {box});
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        const std::uint32_t answer = engine_value(0x00426310, {box, edge, global_word(m_, kEqPlayer)});
        if (answer != 0) {
            if (answer == 1) {
                sfx(0x91);
                view_clear_flag(window.view().address, s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)));
                buy_deck(s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)));
            } else {
                sfx(0x93);
            }
            if (window.dialog().address != 0) {
                delete_object(window.dialog().address);
            }
            window.set_dialog(GuestPtr<void>{0});
            window.set_dialog_open(0);
        }
    }
    auto edge_pressed = [&](std::uint32_t base, std::uint32_t direction) {
        const std::uint32_t at = pad_at(base, global_word(m_, kEqPlayer));
        if (byte_at(m_, at) != 0) {
            set_byte_at(m_, at, 0);
            view_move(window.view().address, direction);
        }
    };
    edge_pressed(kPadLeft, 2);
    edge_pressed(kPadRight, 0);
    edge_pressed(kPadUpEdge, 1);
    edge_pressed(kPadDownEdge, 3);
    const std::uint32_t player = global_word(m_, kEqPlayer);
    if (byte_at(m_, pad_at(kPadToggle, player)) != 0 || (window.page() == 1 && byte_at(m_, pad_at(kPadConfirm, player)) != 0)) {
        set_byte_at(m_, pad_at(kPadToggle, player), 0);
        set_byte_at(m_, pad_at(kPadConfirm, player), 0);
        const std::int32_t page = window.page() + 1;
        window.set_page(page);
        if (page == 2) {
            window.set_page(0);
        }
        const std::uint32_t fill = global_word(m_, kFillHeight);
        if (window.page() == 0) {
            element_at(m_, window.prompts(1).address).set_target_y(0xdc);
            element_at(m_, window.prompts(2).address).set_target_y(0xdc);
            element_at(m_, window.prompts(5).address).set_target_y(s(fill + 0x28));
            if (global_byte(m_, kEqCareer) != 0) {
                cash_set_visible(window.cash().address, 1);
            }
        } else if (window.page() == 1) {
            element_at(m_, window.prompts(1).address).set_target_y(s(fill + 0x28));
            element_at(m_, window.prompts(2).address).set_target_y(s(fill + 0x28));
            element_at(m_, window.prompts(5).address).set_target_y(0xdc);
            if (global_byte(m_, kEqCareer) != 0) {
                cash_set_visible(window.cash().address, 0);
            }
        }
        view_set_page(window.view().address, u(window.page()));
    }
    if (window.dialog_open() == 0 && window.page() == 0) {
        const std::uint32_t at = pad_at(kPadConfirm, global_word(m_, kEqPlayer));
        const std::uint32_t other = pad_at(kPadAlternate, global_word(m_, kEqPlayer));
        if (byte_at(m_, at) != 0 || byte_at(m_, other) != 0) {
            const std::int32_t set = s(global_word(m_, kEqSet));
            const std::int32_t index = s(global_word(m_, kEqIndex));
            set_byte_at(m_, at, 0);
            set_byte_at(m_, other, 0);
            if (owns_deck(set, index)) {
                const std::uint32_t current_player = global_word(m_, kEqPlayer);
                const std::uint32_t character = global_word(m_, kEqCharacter);
                set_byte_at(m_, kSavedCharacter + current_player, character);
                set_byte_at(m_, kSavedIndex + current_player, global_word(m_, kEqIndex));
                set_byte_at(m_, kPlayerIndex + current_player, global_word(m_, kEqIndex));
                set_byte_at(m_, kCharacterIndex + character, global_word(m_, kEqIndex));
                set_byte_at(m_, kSavedSet + current_player, global_word(m_, kEqSet));
                set_byte_at(m_, kPlayerSet + current_player, global_word(m_, kEqSet));
                set_byte_at(m_, kCharacterSet + character, global_word(m_, kEqSet));
                sfx(0x91);
                window.set_done(1);
            } else if (global_byte(m_, kEqCareer) == 0 || !can_afford(s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)))) {
                sfx(0x92);
            } else {
                sfx(0x91);
                const std::int32_t price = deck_price(s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)));
                const std::string line = retail_format(m_.cstring(global_word(m_, 0x0052e920), 0x40), {}, {price});
                const std::uint32_t text = scratch_line(line);
                window.set_dialog_open(1);
                const std::uint32_t memory = operator_new(0x428);
                std::uint32_t box = 0;
                if (memory != 0) {
                    box = engine_value(0x00425fa0, {memory, window.font_b_address(), text, 0x100, 0x78}, m_.cstring(text, 0x100));
                }
                window.set_dialog(GuestPtr<void>{box});
            }
        }
    }
    name_update(window.name().address, s(global_word(m_, kEqSet)), s(global_word(m_, kEqIndex)));
    for (std::uint32_t i = 0; i < 20; ++i) {
        const std::uint32_t logo = window.logos(i).address;
        if (logo != 0) {
            element_at(m_, logo).set_target_x(0x320);
        }
    }
    const std::uint32_t current = logo_at(window, global_word(m_, kEqSet));
    if (current != 0) {
        element_at(m_, current).set_target_x(0x1ce);
    }
    w_.window_update(self);
}

}  // namespace opentony::runtime::frontend
