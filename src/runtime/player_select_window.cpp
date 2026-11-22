#include "auto_views.hpp"
#include "player_select.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Assertion messages
constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kMsgNoBio = 0x00537194;
constexpr std::uint32_t kMsgNoMenu = 0x0053717c;
constexpr std::uint32_t kMsgNoSkaterScreen = 0x005371ac;       // "No skater screen element" (UpdateFrame); Draw's is the lower-case 0x00537160
constexpr std::uint32_t kMsgNoSkaterScreenDraw = 0x00537160;
constexpr std::uint32_t kMsgNoStats = 0x00537148;
constexpr std::uint32_t kMsgNoName = 0x00537138;
constexpr std::uint32_t kMsgBadState = 0x005345d8;             // "Unrecognized state %d" (Draw)
constexpr std::uint32_t kMsgBadStateUpdate = 0x00527c8c;       // the same text, the copy Update uses
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;

// PlayerSelectWindow fields (0x4dc bytes, vtable 0x005195a4)
constexpr std::uint32_t kState = 0xac;
constexpr std::uint32_t kBackground = 0xb0;
constexpr std::uint32_t kFontA = 0xb4;      // s2font2: the prompt windows' and the skater view's font
constexpr std::uint32_t kFontB = 0x200;     // s2font1: the name and the header
constexpr std::uint32_t kFontC = 0x34c;     // s2font3: the medal window's
constexpr std::uint32_t kTwoPlayer = 0x498;   // whether the screen is the multi-player kind (the header, the continue to the next player)
constexpr std::uint32_t kPlayers = 0x49c;     // how many players choose in turn
constexpr std::uint32_t kView = 0x4a0;
constexpr std::uint32_t kMedal = 0x4a4;
constexpr std::uint32_t kBio = 0x4a8;
constexpr std::uint32_t kHeader = 0x4ac;
constexpr std::uint32_t kMenu = 0x4b0;
constexpr std::uint32_t kName = 0x4b4;
constexpr std::uint32_t kLoad = 0x4b8;
constexpr std::uint32_t kPrompts = 0x4bc;   // seven prompt windows
constexpr std::uint32_t kDots = 0x4d8;

// the menu window's last choice and the view's selected entry
constexpr std::uint32_t kMenuChoice = 0x234;
constexpr std::uint32_t kViewSelected = 0x890;
constexpr std::uint32_t kViewMode = 0xb0;
constexpr std::uint32_t kViewCustomFlag = 0x138c;
constexpr std::uint32_t kViewCustoms = 0x1378;
constexpr std::uint32_t kViewSpool = 0xe0;

// Retail globals
constexpr std::uint32_t kGlobalStartInMenu = 0x00561ca4;
constexpr std::uint32_t kGlobalPlayerCount = 0x00561c60;
constexpr std::uint32_t kGlobalReplayState = 0x00561c74;
constexpr std::uint32_t kGlobalExitCode = 0x00561c78;
constexpr std::uint32_t kGlobalPreManager = 0x0056b834;
constexpr std::uint32_t kGlobalCustomName = 0x0052e618;     // pointer to the custom skater's display name
constexpr std::uint32_t kGlobalResourceSlots = 0x00568a2c;  // two words
constexpr std::uint32_t kSelPadStride = 0x16c;
constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;
constexpr std::uint32_t kPadConfirm = 0x0056b029;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kPlayerFile = 0x0052cf48;           // "player.pre"
constexpr std::uint32_t kBackgroundFile = 0x005370ec;       // "s2bgd1.bmp"
constexpr std::uint32_t kFillHeight = 0x00527d54;

constexpr std::uint32_t kSelFontNames[5] = {0x00526acc, 0x0052544c, 0x0052796c, 0x005371c8, 0x005254e0};   // s2font1, s2font2, s2font3, s2bio, s2tricks

}  // namespace

// ---- construction ---------------------------------------------------------------------------------------------------------------------------

// 0x004a2050: the window, three font objects and the initialiser.
std::uint32_t PlayerSelect::select_construct(std::uint32_t self, std::int32_t mode) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    font_construct(self + kFontC);
    player_select_window_select_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerSelect));
    select_init(self, mode);
    return self;
}

std::uint32_t PlayerSelect::select_delete(std::uint32_t self, std::uint32_t flag) {
    select_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004a2100: fonts, the custom skaters' availability, the pages (skater view, loader, seven prompts, name, dots, header or medals, bio, menu) and the
// background. `mode` is the screen's caller (1, 2, 3 and 10 are the single-player kinds; 2 and 3 choose without the menu).
void PlayerSelect::select_init(std::uint32_t self, std::int32_t mode) {
    engine(0x00423bd0);
    auto set_font = [&](std::uint32_t font, std::uint32_t name) {
        copy_font_named(font, name);
        set_word_at(m_, font + 4, 0);
        set_word_at(m_, font + 8, 3);
        f_.set_rgb(font, 0x80, 0x80, 0x80);
    };
    set_font(self + kFontA, 0x0052544c);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0x800);
    set_font(self + kFontB, 0x00526acc);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0xffffffff);
    set_word_at(m_, self + kFontB + 0x10, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    set_font(self + kFontC, 0x0052796c);
    set_word_at(m_, self + kFontC + 0xc, 0);
    set_word_at(m_, self + kFontC + 0x10, 0);
    set_byte_at(m_, self + kFontC + 0x21, 0);
    std::uint32_t pages[0x20] = {};
    set_word_at(m_, self + kFontC + 0x30, 0x800);
    // a custom skater's entry in the character table is available when its record is valid
    for (int character = 0; character < 20; ++character) {
        const std::uint32_t row = skater::row(character);
        if ((word_at(m_, row + skater::kRowFlags) & 2) != 0) {
            set_word_at(m_, row + skater::kRowUnlocked, u(sbyte_at(m_, skater::kCustomRecords + skater::kCustomStride * u(sk_.custom_index(character)) + skater::kCustomValid)));
        }
    }
    const bool single = mode == 1 || mode == 2 || mode == 3 || mode == 10;
    player_select_window_select_at(m_, self).set_field_498(static_cast<Word>(single ? 0 : 1));
    if (mode == 10) {
        player_select_window_select_at(m_, self).set_field_498(1);
    }
    player_select_window_select_at(m_, self).set_field_ac(0);
    restore_selection(self);
    if (global_word(m_, kGlobalStartInMenu) != 0) {
        set_global_word(m_, kGlobalStartInMenu, 0);
        player_select_window_select_at(m_, self).set_field_ac(1);
    }
    // the skater view
    std::uint32_t memory = operator_new(0x13dc);
    std::uint32_t view = 0;
    if (memory != 0) {
        view = engine_value(0x004a3e80, {memory, (mode == 3 || mode == 2) ? 1u : 0u, self + kFontA, 0x154, 0x10, 0x154, u(-200)});
    }
    player_select_window_select_at(m_, self).set_field_4a0(static_cast<Word>(view));
    w_.window_add(self, view);
    if ((engine_value(0x004a9510, {global_word(m_, kGlobalPreManager), kPlayerFile}) & 0xff) == 0) {
        engine(0x004a90f0, {global_word(m_, kGlobalPreManager), kPlayerFile, 1});
    }
    memory = operator_new(0x248);
    std::uint32_t load = 0;
    if (memory != 0) {
        load = engine_value(0x004a6f50, {memory, 0, 0});
    }
    player_select_window_select_at(m_, self).set_field_4b8(static_cast<Word>(load));
    w_.window_add(self, load);
    // the seven prompt windows: (picture, x, y)
    static const std::uint32_t kPromptFiles[7] = {0x005258d8, 0x0053710c, 0x00527840, 0x00527830, 0x00527840, 0x0052540c, 0x00525400};
    static const std::int32_t kPromptX[7] = {0x8c, -0x29, -0x67, 0x134, -0x105, 0x1a4, 0x32};
    static const std::int32_t kPromptY[7] = {0xdc, 999, 999, 0xdc, 999, 0xdc, 0xdc};
    for (int i = 0; i < 7; ++i) {
        set_word_at(m_, self + kPrompts + 4 * static_cast<std::uint32_t>(i), prompt(kPromptFiles[i], 0x005252f0, self + kFontA, kPromptX[i], kPromptY[i]));
    }
    for (std::uint32_t i = 0; i < 7; ++i) {
        w_.window_add(self, word_at(m_, self + kPrompts + 4 * i));
    }
    memory = operator_new(0x30);
    std::uint32_t name = 0;
    if (memory != 0) {
        name = w_.text_construct(memory, 0x005370f8, self + kFontB, 0x42, 0x23, -1, -1);
    }
    player_select_window_select_at(m_, self).set_field_4b4(static_cast<Word>(name));
    pages[7] = name;
    memory = operator_new(0xb0);
    std::uint32_t dots = 0;
    if (memory != 0) {
        dots = engine_value(0x004a5450, {memory, 0x9a, 0x2d, 0xffffffffu, 0xffffffffu});
    }
    player_select_window_select_at(m_, self).set_field_4d8(static_cast<Word>(dots));
    pages[8] = dots;
    player_select_window_select_at(m_, self).set_field_4ac(0);
    player_select_window_select_at(m_, self).set_field_49c(1);
    bool has_slot[2] = {false, false};
    for (int slot = 0; slot < 2; ++slot) {
        has_slot[slot] = sk_.slot_loaded_flag(slot) != 0;
        if (has_slot[slot]) {
            sk_.select_character_checked(0, slot);
        }
    }
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    sk_.release_active_loaded();
    if (player_select_window_select_at(m_, self).field_498() != 0) {
        memory = operator_new(0xb0);
        std::uint32_t header = 0;
        if (memory != 0) {
            header = engine_value(0x004a56b0, {memory, self + kFontB, 0x10e, 0xa3, 0x200, 0xa3});
        }
        player_select_window_select_at(m_, self).set_field_4ac(static_cast<Word>(header));
        pages[9] = header;
        if (mode == 5 || mode == 8 || mode == 9 || mode == 7 || mode == 4) {
            player_select_window_select_at(m_, self).set_field_49c(2);
        } else {
            player_select_window_select_at(m_, self).set_field_49c(static_cast<Word>(global_word(m_, kGlobalPlayerCount)));
        }
    } else if (mode != 3 && mode != 2) {
        memory = operator_new(0xb8);
        std::uint32_t medal = 0;
        if (memory != 0) {
            medal = engine_value(0x004a1670, {memory, self + kFontC, 0xf4, 0x90, 0x200, 0x90});
        }
        player_select_window_select_at(m_, self).set_field_4a4(static_cast<Word>(medal));
        pages[9] = medal;
    }
    memory = operator_new(0x244);
    std::uint32_t bio = 0;
    if (memory != 0) {
        bio = engine_value(0x004a3830, {memory, 0x41, 0x7f, u(-0x200), 0x7f});
    }
    player_select_window_select_at(m_, self).set_field_4a8(static_cast<Word>(bio));
    pages[10] = bio;
    memory = operator_new(0x240);
    std::uint32_t menu = 0;
    if (memory != 0) {
        menu = engine_value(0x004a5890, {memory, player_select_window_select_at(m_, self).field_498(), self + kFontB, 0x19, 0x6c});
    }
    player_select_window_select_at(m_, self).set_field_4b0(static_cast<Word>(menu));
    pages[11] = menu;
    for (std::uint32_t page : pages) {
        if (page != 0) {
            w_.window_add(self, page);
        }
    }
    vcall_item("element_load", view, 4);
    set_word_at(m_, view + kViewSelected, has_slot[0] ? 0x14u : u(sk_.current_character()));
    if (int_at(m_, view + kViewSelected) == 0x10) {
        view_step_selection(view, 1);
    }
    const std::uint32_t sprite_memory = crt_new(0x1c);
    std::uint32_t background = 0;
    if (sprite_memory != 0) {
        s_.note(kSvcFileOpen, {kBackgroundFile, 1}, m_.cstring(kBackgroundFile));
        const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(kSvcFileOpen));
        background = engine_value(seam::hal::D3DSprite_Create, {sprite_memory, handle, kBackgroundFile, 1});
    }
    player_select_window_select_at(m_, self).set_field_b0(static_cast<Word>(background));
    engine(0x004a9330, {global_word(m_, kGlobalPreManager), kPlayerFile});
}

// 0x004a2bc0: the background, the engine's caches, the three fonts and the window base.
void PlayerSelect::select_destroy(std::uint32_t self) {
    player_select_window_select_at(m_, self).set_field_0(static_cast<Word>(kVtPlayerSelect));
    check(player_select_window_select_at(m_, self).field_b0() != 0, kMsgNoBackground);
    const std::uint32_t sprite = player_select_window_select_at(m_, self).field_b0();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    font_destroy(self + kFontC);
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x004a2cb0: the seven prompts are hidden or shown together.
void PlayerSelect::select_set_prompts_hidden(std::uint32_t self, std::uint32_t hidden) {
    for (std::uint32_t i = 0; i < 7; ++i) {
        set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + kElemHidden, (hidden & 0xff) == 0 ? 1 : 0);
    }
}

// ---- drawing ----------------------------------------------------------------------------------------------------------------------------------

// 0x004a2ce0: hide everything, then show what the state needs, and draw the window and the background.
void PlayerSelect::select_draw(std::uint32_t self) {
    check(player_select_window_select_at(m_, self).field_4a8() != 0, kMsgNoBio);
    set_word_at(m_, player_select_window_select_at(m_, self).field_4a8() + kElemHidden, 1);
    check(player_select_window_select_at(m_, self).field_4b0() != 0, kMsgNoMenu);
    set_word_at(m_, player_select_window_select_at(m_, self).field_4b0() + kElemHidden, 1);
    check(player_select_window_select_at(m_, self).field_4a0() != 0, kMsgNoSkaterScreenDraw);
    view_set_mode(player_select_window_select_at(m_, self).field_4a0(), 1);
    check(player_select_window_select_at(m_, self).field_4d8() != 0, kMsgNoStats);
    set_word_at(m_, player_select_window_select_at(m_, self).field_4d8() + kElemHidden, 1);
    check(player_select_window_select_at(m_, self).field_4b4() != 0, kMsgNoName);
    set_word_at(m_, player_select_window_select_at(m_, self).field_4b4() + kElemHidden, 1);
    select_set_prompts_hidden(self, 0);
    const std::uint32_t fill = global_word(m_, kFillHeight) + 0x14;
    for (std::uint32_t i = 0; i < 5; ++i) {
        set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + kElemTargetY, fill);
    }
    if (player_select_window_select_at(m_, self).field_4ac() != 0) {
        set_word_at(m_, player_select_window_select_at(m_, self).field_4ac() + kElemHidden, 1);
    }
    set_word_at(m_, player_select_window_select_at(m_, self).field_4b8() + kElemHidden, 1);
    auto prompt_y = [&](std::uint32_t index) { set_word_at(m_, word_at(m_, self + kPrompts + 4 * index) + kElemTargetY, 0xdc); };
    switch (player_select_window_select_at(m_, self).field_ac()) {
    case 0: {
        const bool custom = view_is_custom(player_select_window_select_at(m_, self).field_4a0());
        set_word_at(m_, player_select_window_select_at(m_, self).field_4a8() + kElemHidden, custom ? 1 : 0);
        if (player_select_window_select_at(m_, self).field_4ac() != 0) {
            set_word_at(m_, player_select_window_select_at(m_, self).field_4ac() + kElemHidden, 0);
        }
        view_set_mode(player_select_window_select_at(m_, self).field_4a0(), 0);
        select_set_prompts_hidden(self, 1);
        prompt_y(0);
        if (!view_is_custom(player_select_window_select_at(m_, self).field_4a0())) {
            prompt_y(2);
            prompt_y(3);
            prompt_y(4);
        }
        set_word_at(m_, player_select_window_select_at(m_, self).field_4d8() + kElemHidden, 0);
        set_word_at(m_, player_select_window_select_at(m_, self).field_4b4() + kElemHidden, 0);
        break;
    }
    case 1:
        set_word_at(m_, player_select_window_select_at(m_, self).field_4b0() + kElemHidden, 0);
        select_set_prompts_hidden(self, 1);
        prompt_y(1);
        prompt_y(3);
        prompt_y(4);
        set_word_at(m_, player_select_window_select_at(m_, self).field_4d8() + kElemHidden, 0);
        set_word_at(m_, player_select_window_select_at(m_, self).field_4b4() + kElemHidden, 0);
        break;
    case 2:
        set_word_at(m_, player_select_window_select_at(m_, self).field_4b8() + kElemHidden, 0);
        break;
    default:
        s_.note("assert", {kMsgBadState});
        break;
    }
    if (player_select_window_select_at(m_, self).field_ac() == 2 && load_is_loading(player_select_window_select_at(m_, self).field_4b8())) {
        select_set_prompts_hidden(self, 1);
        prompt_y(1);
        prompt_y(5);
        prompt_y(6);
        for (std::uint32_t index : {1u, 5u, 6u}) {
            vcall_item(kElementDraw, word_at(m_, self + kPrompts + 4 * index), 2, {0, 0});
        }
    }
    w_.window_draw(self, 0, 0);
    check(player_select_window_select_at(m_, self).field_b0() != 0, kMsgNoBackground);
    engine(0x004d7990, {player_select_window_select_at(m_, self).field_b0(), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x004a3110: the active player's skater becomes current, its name is shown, the view refreshes.
void PlayerSelect::select_update_frame(std::uint32_t self) {
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    const int character = sk_.current_character();
    if (word_at(m_, skater::row(character) + skater::kRowUnlocked) == 0) {
        sk_.select_current(0);
    }
    check(player_select_window_select_at(m_, self).field_4a0() != 0, kMsgNoSkaterScreen);
    scratch_line({});
    if (view_is_custom(player_select_window_select_at(m_, self).field_4a0())) {
        scratch_line(m_.cstring(global_word(m_, kGlobalCustomName), 0x40));
    } else {
        sk_.format_name(static_cast<int>(global_word(m_, kPadCurrent)), kScratchLine);
        f_.ellipsize(self + kFontB, kScratchLine, 0xe6);
    }
    w_.text_set_string(player_select_window_select_at(m_, self).field_4b4(), kScratchLine);
    view_refresh(player_select_window_select_at(m_, self).field_4a0());
    w_.window_update(self);
}

// ---- the states ---------------------------------------------------------------------------------------------------------------------------------

// 0x004a3230, state 0: left and right choose a skater, confirm accepts it (a locked one is refused), back goes to the previous player or leaves.
std::uint32_t PlayerSelect::select_update_skaters(std::uint32_t self) {
    auto pad = [&](std::uint32_t base) { return base + global_word(m_, kPadCurrent) * kSelPadStride; };
    const std::uint32_t view = player_select_window_select_at(m_, self).field_4a0();
    if (byte_at(m_, pad(kPadLeft)) == 0) {
        if (byte_at(m_, pad(kPadRight)) != 0) {
            sfx(0x90);
            set_byte_at(m_, pad(kPadRight), 0);
            view_step_selection(player_select_window_select_at(m_, self).field_4a0(), 1);
            vcall_item("element_load", player_select_window_select_at(m_, self).field_4a0(), 4);
        }
    } else {
        sfx(0x90);
        set_byte_at(m_, pad(kPadLeft), 0);
        view_step_selection(player_select_window_select_at(m_, self).field_4a0(), 0xffffffffu);
        vcall_item("element_load", player_select_window_select_at(m_, self).field_4a0(), 4);
    }
    (void)view;
    if (byte_at(m_, pad(kPadConfirm)) != 0) {
        set_byte_at(m_, pad(kPadConfirm), 0);
        if (view_is_custom(player_select_window_select_at(m_, self).field_4a0()) || word_at(m_, skater::row(sk_.current_character()) + skater::kRowUnlocked) != 0) {
            sfx(0x91);
            return 1;
        }
        check(false, kMsgEmpty);
        sfx(0x92);
        return 0;
    }
    if (byte_at(m_, pad(kPadBack)) == 0) {
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
        return 0;
    }
    sfx(0x93);
    set_byte_at(m_, pad(kPadBack), 0);
    if (int_at(m_, player_select_window_select_at(m_, self).field_4a0() + kViewSelected) == 0x14) {
        sk_.select_character_checked(0, static_cast<int>(global_word(m_, kPadCurrent)));
    }
    if ((engine_value(0x004de150) & 0xff) == 0) {
        set_global_word(m_, kPadCurrent, global_word(m_, kPadCurrent) - 1);
    }
    if ((engine_value(0x004de150) & 0xff) != 0) {
        return 2;
    }
    if (global_int(m_, kPadCurrent) < 0) {
        set_global_word(m_, kPadCurrent, 0);
        return 2;
    }
    header_slide_in(player_select_window_select_at(m_, self).field_4ac());
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    if (sk_.slot_loaded_flag(static_cast<int>(global_word(m_, kPadCurrent))) != 0) {
        set_word_at(m_, player_select_window_select_at(m_, self).field_4a0() + kViewSelected, 0x14);
    } else {
        set_word_at(m_, player_select_window_select_at(m_, self).field_4a0() + kViewSelected, u(sk_.current_character()));
    }
    sk_.release_active_loaded();
    vcall_item("element_load", player_select_window_select_at(m_, self).field_4a0(), 4);
    return 0;
}

// 0x004a34b0, state 1: the main menu's answer: 1 continue, 2 appearance (the next costume, or the custom skater's editor), 3 buy stats, 4 buy tricks,
// 5 skate shop. With no answer, back leaves (2).
std::uint32_t PlayerSelect::select_update_menu(std::uint32_t self) {
    const std::uint32_t choice = menu_choice(player_select_window_select_at(m_, self).field_4b0());
    if (choice == 0) {
        const std::uint32_t at = kPadBack + global_word(m_, kPadCurrent) * kSelPadStride;
        if (byte_at(m_, at) != 0) {
            set_byte_at(m_, at, 0);
            sfx(0x93);
            return 2;
        }
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
        return 0;
    }
    switch (choice) {
    case 1:
        return select_next_player(self) ? 2 : 1;
    case 2:
        if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) != 0) {
            save_selection(self);
            set_global_word(m_, kGlobalExitCode, 0xc);
            return 1;
        }
        next_costume();
        return 0;
    case 3:
        set_global_word(m_, kGlobalExitCode, 0xf);
        save_selection(self);
        return 1;
    case 4:
        set_global_word(m_, kGlobalExitCode, 0x10);
        save_selection(self);
        return 1;
    case 5:
        set_global_word(m_, kGlobalExitCode, 3);
        save_selection(self);
        return 1;
    default:
        return 1;
    }
}

// 0x004a35e0, state 2: a tail jump into the load window's step with the window in ecx.
std::uint32_t PlayerSelect::select_update_load(std::uint32_t self) { return engine_value(0x004a7590, {word_at(m_, self + kLoad)}); }

// 0x004a35f0: on to the next player's choice (when there is one the view shows its skater), else the screen's answer is 2.
bool PlayerSelect::select_next_player(std::uint32_t self) {
    if ((engine_value(0x004de150) & 0xff) == 0) {
        set_global_word(m_, kPadCurrent, global_word(m_, kPadCurrent) + 1);
        if (global_word(m_, kGlobalReplayState) == 7) {
            for (int i = 0; i < 5; ++i) {
                engine(0x00489a10);
                engine(0x00489670, {kPad0});
                engine(0x00489670, {kPad1});
                game_frame_tick();
            }
        }
    }
    if (global_int(m_, kPadCurrent) < static_cast<std::int32_t>(player_select_window_select_at(m_, self).field_49c()) && (engine_value(0x004de150) & 0xff) == 0) {
        sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
        if (sk_.slot_loaded_flag(static_cast<int>(global_word(m_, kPadCurrent))) != 0) {
            set_word_at(m_, player_select_window_select_at(m_, self).field_4a0() + kViewSelected, 0x14);
        } else {
            set_word_at(m_, player_select_window_select_at(m_, self).field_4a0() + kViewSelected, u(sk_.current_character()));
        }
        sk_.release_active_loaded();
        header_slide_in(player_select_window_select_at(m_, self).field_4ac());
        vcall_item("element_load", player_select_window_select_at(m_, self).field_4a0(), 4);
        return true;
    }
    set_global_word(m_, kGlobalExitCode, 2);
    return false;
}

// 0x004a36e0: the shoulder buttons spin the skater, then the state's update; the answer 1 or 2 ends the screen.
std::uint32_t PlayerSelect::select_update(std::uint32_t self) {
    auto pad = [&](std::uint32_t base) { return base + global_word(m_, kPadCurrent) * kSelPadStride; };
    if (byte_at(m_, pad(kPadSpinRight)) != 0) {
        view_rotate(player_select_window_select_at(m_, self).field_4a0(), u(-0x1e));
    }
    if (byte_at(m_, pad(kPadSpinLeft)) != 0) {
        view_rotate(player_select_window_select_at(m_, self).field_4a0(), 0x1e);
    }
    switch (player_select_window_select_at(m_, self).field_ac()) {
    case 0: {
        const std::uint32_t answer = select_update_skaters(self);
        if (answer == 1) {
            menu_highlight_first(player_select_window_select_at(m_, self).field_4b0());
            if (view_is_custom(player_select_window_select_at(m_, self).field_4a0())) {
                player_select_window_select_at(m_, self).set_field_ac(2);
                engine(0x004a7480, {player_select_window_select_at(m_, self).field_4b8()});
                return 0;
            }
            player_select_window_select_at(m_, self).set_field_ac(1);
            return 0;
        }
        return answer == 2 ? 2 : 0;
    }
    case 1: {
        const std::uint32_t answer = select_update_menu(self);
        if (answer == 1) {
            return 1;
        }
        if (answer == 2) {
            player_select_window_select_at(m_, self).set_field_ac(0);
        }
        return 0;
    }
    case 2: {
        const std::uint32_t answer = select_update_load(self);
        if (answer == 1) {
            if (select_next_player(self)) {
                player_select_window_select_at(m_, self).set_field_ac(0);
                return 0;
            }
            return 1;
        }
        if (answer == 2) {
            player_select_window_select_at(m_, self).set_field_ac(0);
        }
        return 0;
    }
    default:
        s_.note("assert", {kMsgBadStateUpdate});
        return 0;
    }
}

// 0x004a5f50: the main menu's last choice.
std::uint32_t PlayerSelect::menu_choice(std::uint32_t menu) { return word_at(m_, menu + kMenuChoice); }

// ---- the small methods of the other classes ----------------------------------------------------------------------------------------------------

bool PlayerSelect::view_is_custom(std::uint32_t view) { return word_at(m_, view + kViewSelected) == 0x14; }

void PlayerSelect::view_set_mode(std::uint32_t view, std::uint32_t mode) { set_word_at(m_, view + kViewMode, mode & 0xff); }

// 0x004a44d0: the angle is a 16-bit word added to the skater object's heading.
void PlayerSelect::view_rotate(std::uint32_t view, std::uint32_t angle) {
    const std::uint32_t object = word_at(m_, view + 0xac);
    set_half_at(m_, object + 0x16, half_at(m_, object + 0x16) + (angle & 0xffff));
}

// 0x004a5390: step the selected entry, skipping 0x10 and the custom entry, until it names a skater that is available.
void PlayerSelect::view_step_selection(std::uint32_t view, std::int32_t step) {
    for (;;) {
        for (;;) {
            const std::int32_t value = int_at(m_, view + kViewSelected) + step;
            set_word_at(m_, view + kViewSelected, u(value));
            if (value > 0x14) {
                set_word_at(m_, view + kViewSelected, 0);
            }
            if (int_at(m_, view + kViewSelected) < 0) {
                set_word_at(m_, view + kViewSelected, 0x14);
            }
            const std::int32_t entry = int_at(m_, view + kViewSelected);
            if (entry != 0x10 && entry != 0x14) {
                break;
            }
        }
        sk_.select_current(int_at(m_, view + kViewSelected));
        if (int_at(m_, view + kViewSelected) != 0x14 && word_at(m_, skater::row(sk_.current_character()) + skater::kRowUnlocked) != 0) {
            return;
        }
        const std::uint32_t state = global_word(m_, kGlobalReplayState);
        const bool allowed = !(state == 1 || state == 2 || state == 3);
        if (int_at(m_, view + kViewSelected) == 0x14 && allowed) {
            return;
        }
    }
}

// 0x004a4890: with the custom flag set the custom skaters are freed (the flag stays set while some remain); otherwise the character's spool refreshes.
void PlayerSelect::view_refresh(std::uint32_t view) {
    if (word_at(m_, view + kViewCustomFlag) != 0) {
        const std::uint32_t result = engine_value(0x00423b50, {4, word_at(m_, view + kViewCustoms) + 4});
        set_word_at(m_, view + kViewCustomFlag, result == 0 ? 1 : 0);
        return;
    }
    engine(0x004a6880, {view + kViewSpool, u(sk_.current_character())});
}

// 0x004a5340: the next costume of the character the player picked (after the last comes the first).
void PlayerSelect::next_costume() {
    std::int32_t costume = sk_.current_costume() + 1;
    if (costume >= sk_.count_costume_names(sk_.current_character())) {
        costume = 0;
    }
    sk_.select_costume(static_cast<int>(global_word(m_, kPadCurrent)), costume, true);
}

void PlayerSelect::header_slide_in(std::uint32_t header) { set_word_at(m_, header + kElemX, 0x200); }

void PlayerSelect::menu_highlight_first(std::uint32_t menu) { menu_.list_highlight(word_at(m_, menu + 0xac), 1); }

bool PlayerSelect::load_is_loading(std::uint32_t load) { return word_at(m_, load + 0xac) == 2; }

// ---- the screen ----------------------------------------------------------------------------------------------------------------------------------

// 0x004a5f60: five fonts, the window, frames until the update answers 1 (go on) or 2 (back), then the fade, the unload and the skater bookkeeping.
std::int32_t PlayerSelect::run(std::uint32_t mode) {
    engine(0x0048a320);
    engine(0x0048a0b0, {0x100, 0x60});
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    for (std::uint32_t name : kSelFontNames) {
        font_load(name);
    }
    const std::uint32_t memory = operator_new(0x4dc);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = seams_ ? engine_value(0x004a2050, {memory, mode}) : select_construct(memory, static_cast<std::int32_t>(mode));
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    engine(0x004c8f30);
    std::uint32_t answer = 0;
    for (;;) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004e02d0);
        engine(0x00418150);
        answer = seams_ ? engine_value(0x004a36e0, {window}) : select_update(window);
        if (answer == 1 || answer == 2) {
            break;
        }
        engine(0x004304b0);
        engine(0x00489a10);
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        vcall_item(kElementUpdate, window, 1);      // (the window's per-frame method, through its vtable)
        if (seams_) {
            engine(0x004a2ce0, {window});
        } else {
            select_draw(window);
        }
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
    }
    engine(0x00423bd0);
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (window != 0) {
        delete_object(window);      // (its deleting destructor, through the vtable)
    }
    if (answer != 2) {
        engine(0x004c8f10, {0xec, 1});
    }
    for (std::uint32_t name : kSelFontNames) {
        font_unload(name);
    }
    engine(0x0046f480);
    for (int slot = 0; slot < 8; ++slot) {
        const std::int32_t character = sk_.character_of_slot(slot);
        if (word_at(m_, skater::row(character) + skater::kRowUnlocked) == 0) {
            sk_.select_character(slot, 0);
        }
    }
    for (int slot = 0; slot < 2; ++slot) {
        set_word_at(m_, kGlobalResourceSlots + 4 * static_cast<std::uint32_t>(slot), u(sk_.character_of_slot(slot)));
    }
    const std::uint32_t exit_code = global_word(m_, kGlobalExitCode);
    if (exit_code == 2) {
        sk_.reset_active_player();
        set_global_word(m_, kPadCurrent, 0);
    } else if (exit_code != 3 && exit_code != 0xc) {
        set_global_word(m_, kPadCurrent, 0);
    }
    sk_.set_active_slot(static_cast<int>(global_word(m_, kPadCurrent)));
    engine(0x0048a320);
    engine(0x0048a0b0, {0x100, 0x50});
    set_global_word(m_, 0x0056a2f0, 0);
    set_global_word(m_, 0x0056a2f4, 0);
    return answer != 2 ? 0 : -1;
}

}  // namespace opentony::runtime::frontend
