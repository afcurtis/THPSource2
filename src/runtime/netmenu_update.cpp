#include "auto_views.hpp"
#include "netmenu.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace netmenu;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kEmpty = 0x005252f0;
constexpr std::uint32_t kMainFont = 0x00525458;
constexpr std::uint32_t kNetState = 0x0056ad10;
constexpr std::uint32_t kDone = 0x0056ad20;
constexpr std::uint32_t kBack = 0x0056ad19;
constexpr std::uint32_t kResult = 0x0056ad1c;
constexpr std::uint32_t kJoined = 0x0056ad18;
constexpr std::uint32_t kLastPlayers = 0x0056ad14;
constexpr std::uint32_t kFrontFlag = 0x0056114c;
constexpr std::uint32_t kNetActive = 0x006a3b21;
constexpr std::uint32_t kNetHost = 0x006a3b22;
constexpr std::uint32_t kNetLost = 0x006a3b30;
constexpr std::uint32_t kNetPlayers = 0x006a18b0;
constexpr std::uint32_t kMode = 0x00561c74;
constexpr std::uint32_t kGameMode = 0x00533f38;
constexpr std::uint32_t kLevel = 0x00561c90;
constexpr std::uint32_t kRemembered = 0x005358f8;          // the main list's remembered line
constexpr std::uint32_t kRememberedForm = 0x005358fc;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPadBack2 = 0x0056b165;
constexpr std::uint32_t kConfirm0 = 0x0056b029;
constexpr std::uint32_t kConfirm1 = 0x0056b195;
constexpr std::uint32_t kConfirm2 = 0x0056b0d9;
constexpr std::uint32_t kHostName = 0x006a3b71;
constexpr std::uint32_t kJoinName = 0x006a3bb1;
constexpr std::uint32_t kPlayerName = 0x006a18df;
constexpr std::uint32_t kHostPlayer = 0x006a3abf;
constexpr std::uint32_t kLevels = 0x0053c1f8;

}  // namespace

// ---- small methods ----------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0047e210: the items of a list take the texture of the description they were built from (table entries are 24 bytes; a zero first word keeps the item's own)
void NetMenu::set_item_textures(std::uint32_t items, std::uint32_t table, std::int32_t count) {
    for (std::int32_t k = 0; k < count; ++k) {
        const std::uint32_t texture = word_at(m_, table + 24 * u(k));
        if (texture != 0) {
            set_word_at(m_, word_at(m_, items + 4 * u(k)) + 0x1c, texture);
        }
    }
}

// 0x0047e3c0: the two side panels' sliding is switched on
void NetMenu::enable_arrows(std::uint32_t self) {
    if (word_at(m_, self + kWindowLeft) != 0) {
        set_word_at(m_, word_at(m_, self + kWindowLeft) + 0x14, 1);
        set_word_at(m_, word_at(m_, self + kWindowRight) + 0x14, 1);
    }
}

// 0x0047e3e0: a line of chat: the seven lines scroll up, the new one is formatted into the last (a player's line, or the host's, or a bare message)
void NetMenu::add_chat(std::uint32_t self, std::uint32_t kind, std::uint32_t text) {
    s_.note(kSvcPrintfLog, {0x00535a88});
    for (std::uint32_t k = 0; k < 6; ++k) {
        set_word_at(m_, self + kChatKinds + 4 * k, word_at(m_, self + kChatKinds + 4 * (k + 1)));
        const std::string next = m_.cstring(self + kChatLines + 0x38 * (k + 1), 0x38);
        std::vector<std::uint8_t> bytes(next.begin(), next.end());
        bytes.push_back(0);
        m_.write(self + kChatLines + 0x38 * k, bytes);
    }
    if (kind == 0 || kind == 1) {
        const std::uint32_t speaker = kind == 0 ? kHostPlayer : kPlayerName;
        write_string(self + kChatText, retail_format(m_.cstring(0x00525854, 0x20), {m_.cstring(speaker, 0x40), m_.cstring(text, 0x40)}, {}));
    } else {
        write_string(self + kChatText, retail_format(m_.cstring(0x005256dc, 0x20), {m_.cstring(text, 0x40)}, {}));
    }
    set_word_at(m_, self + kChatCurrent, kind);
}

// 0x0047f1e0: a confirm press on any of the pads, with the click (not in the chat states)
bool NetMenu::confirm_pressed(std::uint32_t self) {
    if (global_byte(m_, kConfirm1) == 0 && global_byte(m_, kConfirm0) == 0 && global_byte(m_, kConfirm2) == 0) {
        return false;
    }
    set_global_byte(m_, kConfirm0, 0);
    set_global_byte(m_, kConfirm1, 0);
    set_global_byte(m_, kConfirm2, 0);
    const std::uint32_t current = state(self);
    if (current != 7 && current != 0xc && current != 0xd) {
        play(0x91);
    }
    return true;
}

// 0x0047cdc0: the game list: the rows are cleared, then filled with the games the network layer found (at most four)
void NetMenu::refresh_games(std::uint32_t self) {
    engine(seam::net::FUN_004ddd60);
    set_word_at(m_, self + kFound, 0);
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t item = word_at(m_, self + kFoundItems + 4 * k);
        if (item != 0) {
            set_byte_at(m_, item + 0xc, 0);
            item_text(item, kEmpty);
        }
    }
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t name = engine_value(0x004dddb0, {k});
        if (name == 0) {
            break;
        }
        const std::uint32_t item = word_at(m_, self + kFoundItems + 4 * k);
        item_text(item, name);
        set_byte_at(m_, word_at(m_, self + kFoundItems + 4 * k) + 0xc, 1);
        set_word_at(m_, self + kFound, word_at(m_, self + kFound) + 1);
    }
    if (menu_.list_highlighted_id(word_at(m_, self + kFoundList)) >= word_at(m_, self + kFound)) {
        menu_.list_highlight(word_at(m_, self + kFoundList), 0);
    }
}

// ---- destruction and the screen loop --------------------------------------------------------------------------------------------------------------------------------

std::uint32_t NetMenu::delete_window(std::uint32_t self, std::uint32_t flag) {
    destruct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0047e260
void NetMenu::destruct(std::uint32_t self) {
    set_word_at(m_, self, kVtNetMenu);
    const std::uint32_t sprite = word_at(m_, self + kBackground);
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});        // (the CRT's delete: the picture was made with the CRT's new)
    }
    font_unload(kMainFont);
    if (word_at(m_, self + kMainList) != 0) {
        delete_object(word_at(m_, self + kMainList));
    }
    if (word_at(m_, self + kStyleA) != 0) {
        delete_object(word_at(m_, self + kStyleA));
    }
    for (std::uint32_t k = 0; k < 11; ++k) {
        check(word_at(m_, self + kMainItems + 4 * k) != 0, 0x00535a58);
        if (word_at(m_, self + kMainItems + 4 * k) != 0) {
            delete_object(word_at(m_, self + kMainItems + 4 * k));
        }
    }
    font_destroy(self + kFontD);
    font_destroy(self + kFontC);
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    engine(0x00454970, {self + kPie});
    w_.window_destroy(self);
}

// 0x0047cbb0 (NetMenu_Main): run the lobby until it is left; the result is what the screen asks the shell to do next (-1 or 0 for the main menu)
std::int32_t NetMenu::run(std::uint32_t flag) {
    s_.note(kSvcPrintfLog, {0x00535a00});
    set_global_word(m_, kNetState, flag);
    const std::uint32_t saved = global_word(m_, kFrontFlag);
    set_global_byte(m_, kDone, 0);
    set_global_byte(m_, kBack, 0);
    set_global_word(m_, kFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    if (flag == 0) {
        engine(0x004ddc00);
        set_global_byte(m_, kNetLost, 0);
        set_global_byte(m_, kJoined, 0);
    }
    const std::uint32_t memory = operator_new(kSize);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = seams_ ? engine_value(0x0047ce60, {memory}) : construct(memory);
    }
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    while (global_byte(m_, kDone) == 0 && global_byte(m_, kBack) == 0) {
        game_frame_tick();
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        vcall_item(kElementUpdate, window, 1);
        if (seams_) {
            // (the update leaves the screen's done and back flags and its result in memory: with the window's methods replaced by seams the script supplies them)
            engine(0x0047e770, {window});
            set_global_byte(m_, kDone, u(s_.value(engine_name(0x0047e770))));
            set_global_byte(m_, kBack, u(s_.value(engine_name(0x0047e770))));
            set_global_word(m_, kResult, u(s_.value(engine_name(0x0047e770))));
        } else {
            update(window);
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
    }
    engine(0x00489670, {0x0056aff8});
    set_global_word(m_, kFrontFlag, saved);
    engine(0x00489670, {0x0056b164});
    if (global_word(m_, kNetState) != 0) {
        if ((engine_value(0x004de040) & 0xff) != 0 || global_byte(m_, kNetHost) != 0) {
            engine(0x004ddce0);
        }
    }
    if (global_word(m_, kResult) == 0) {
        engine(0x004ddc00);
        engine(0x004ddb70);
        set_global_word(m_, kMode, 0);
    }
    if (window != 0) {
        if (seams_) {
            delete_object(window);
        } else {
            delete_window(window, 1);
        }
    }
    s_.note(kSvcPrintfLog, {0x005359ec});
    set_global_byte(m_, kNetLost, 0);
    return s(global_word(m_, kResult));
}

// ---- the lobby page (chat) ----------------------------------------------------------------------------------------------------------------------------------------------

// 0x0047e4e0: the chat list is stepped and drawn, the buttons shown for the role, a typed line is sent, and the seven chat lines and the level line are drawn
void NetMenu::update_lobby(std::uint32_t self) {
    const std::uint32_t list = word_at(m_, self + kSettingsList);
    vcall_list("list_v3", list, 3);
    vcall_list("list_v5", word_at(m_, self + kSettingsList), 5, {word_at(m_, self + kPad)});
    vcall_list("list_v4", word_at(m_, self + kSettingsList), 4);
    const std::uint32_t id = menu_.list_highlighted_id(word_at(m_, self + kSettingsList));
    show_item(word_at(m_, self + kSettingsItems + 8), false);
    show_item(word_at(m_, self + kSettingsItems + 4), false);
    const std::int32_t players = s(global_word(m_, kNetPlayers));
    if ((engine_value(0x004de040) & 0xff) != 0) {
        show_item(word_at(m_, self + kSettingsItems + 4), true);
        if (s(state(self)) >= 0xc && players > 0 && players < 0xd) {
            show_item(word_at(m_, self + kSettingsItems + 8), true);
        }
    } else if (players >= 6 && players < 0xd) {
        show_item(word_at(m_, self + kSettingsItems + 8), true);
    }
    if (global_byte(m_, 0x006a3c13) != 0) {
        add_chat(self, 1, 0x006a3c13);
        set_global_byte(m_, 0x006a3c13, 0);
    }
    if (confirm_pressed(self)) {
        if (id == 0) {
            if ((engine_value(0x004e41b0, {0x1c}) & 0xff) != 0 && state(self) == 0xc) {
                engine(0x004df370, {self + kTyped});
                // (the keyboard line editor fills the buffer: the replay hands back its first eight bytes as two scripted dwords)
                for (std::uint32_t k = 0; !s_.live() && k < 2; ++k) {
                    set_word_at(m_, self + kTyped + 4 * k, u(s_.value(engine_name(0x004df370))));
                }
                add_chat(self, 0, self + kTyped);
                set_byte_at(m_, self + kTyped, 0);
            }
        } else if (id == 1) {
            play(0x91);
            set_global_byte(m_, kJoined, 1);
            engine(0x004de180, {100, 0x0055fe10});
            go(self, 2);
        } else if (id == 2) {
            play(0x91);
            set_global_byte(m_, kDone, 1);
            if (global_byte(m_, kNetHost) == 0) {
                const bool host = (engine_value(0x004de040) & 0xff) != 0;
                set_global_word(m_, 0x0056a888, host ? 0u : 1u);
            } else {
                set_global_word(m_, 0x0056a888, 0);
            }
            engine(0x004de570, {6});
            set_global_word(m_, kResult, 1);
        }
    }
    const std::int32_t level = s(global_word(m_, kLevel));
    if (level >= 0 && (global_byte(m_, kNetHost) != 0 || (s(state(self)) >= 0xc && s(global_word(m_, kNetPlayers)) >= 5))) {
        const std::uint32_t mode_name = word_at(m_, 0x005359c0 + 4 * global_word(m_, kMode));
        const std::uint32_t buffer = scratch_line(retail_format("%s  %s  %s", {m_.cstring(kHostName, 0x40), m_.cstring(kLevels + 0x1ac * u(level), 0x40), m_.cstring(mode_name, 0x40)}, {}));
        s_.note(kFontDrawService, {self + kFontB, 0x50, 10, buffer, 0}, m_.cstring(buffer, 0x100));
    }
    std::uint32_t y = 0x32;
    for (std::uint32_t k = 0; k < 7; ++k) {
        const std::uint32_t line = self + kChatLines + 0x38 * k;
        set_byte_at(m_, line + 0x30, 0);
        const std::uint32_t font = word_at(m_, self + kChatKinds + 4 * k) != 0 ? self + kFontD : self + kFontC;
        s_.note(kFontDrawService, {font, 0x50, y, line, 0}, m_.cstring(line, 0x38));
        y += 0xc;
    }
}

}  // namespace opentony::runtime::frontend
