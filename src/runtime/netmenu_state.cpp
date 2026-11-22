#include "auto_views.hpp"
#include "netmenu.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

using namespace netmenu;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

constexpr std::uint32_t kEmpty = 0x005252f0;               // " "
constexpr std::uint32_t kMainFont = 0x00525458;
constexpr std::uint32_t kDone = 0x0056ad20;
constexpr std::uint32_t kBack = 0x0056ad19;
constexpr std::uint32_t kResult = 0x0056ad1c;
constexpr std::uint32_t kJoined = 0x0056ad18;
constexpr std::uint32_t kLastPlayers = 0x0056ad14;
constexpr std::uint32_t kNetActive = 0x006a3b21;
constexpr std::uint32_t kNetHost = 0x006a3b22;
constexpr std::uint32_t kNetJoin = 0x006a3b23;
constexpr std::uint32_t kNetIp = 0x006a3b24;
constexpr std::uint32_t kNetBusy = 0x006a3b20;
constexpr std::uint32_t kNetLost = 0x006a3b30;
constexpr std::uint32_t kNetPlayers = 0x006a18b0;
constexpr std::uint32_t kNetPlayerCount = 0x006a3a90;
constexpr std::uint32_t kNetDirect = 0x006a3de1;
constexpr std::uint32_t kMode = 0x00561c74;
constexpr std::uint32_t kRemembered = 0x005358f8;          // the main list's remembered line
constexpr std::uint32_t kRememberedForm = 0x005358fc;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPadBack2 = 0x0056b165;
constexpr std::uint32_t kJoinName = 0x006a3bb1;
constexpr std::uint32_t kHostName = 0x006a3b71;
constexpr std::uint32_t kJoinHost = 0x006a3b31;
constexpr std::uint32_t kJoinAddress = 0x006a3abf;
constexpr std::uint32_t kPlayerName = 0x006a18df;
constexpr std::uint32_t kIpText = 0x006a3dcd;

}  // namespace

// 0x00544... the status line is formatted from one of the screen's texts (the cells at 0x0052eac4..0x0052eae8 point at them)
void NetMenu::set_status(std::uint32_t self, std::uint32_t format_cell) {
    write_string(self + kStatus, m_.cstring(word_at(m_, format_cell), 0x40));
}

// 0x0047e770: one frame of the lobby's state machine. The states: 0 and 1 the first page (play on the network / hot seat), 2 and 3 the host/join form, 4 starting the host,
// 5 the settings page, 6 and 7 waiting for the other player, 8 .. 0xb the game search and the join, 0xc and 0xd the connected lobby (chat), 0xe connected.
void NetMenu::update(std::uint32_t self) {
    engine(0x004e02d0);
    const std::int32_t pad = s(engine_value(0x0046f550, {0, 0, 6}));
    set_word_at(m_, self + kPad, u(pad));
    if (pad > 0) {
        play(0x90);
    }
    if (global_byte(m_, kPadBack) != 0 || global_byte(m_, kPadBack2) != 0) {
        set_global_byte(m_, kPadBack, 0);
        set_global_byte(m_, kPadBack2, 0);
        play(0x93);
        set_byte_at(m_, self + kStatus, 0);
        s_.note(kSvcPrintfLog, {0x00535ac4});
        auto back_to_choice = [&]() {
            engine(0x004ddb70);
            go(self, 1);
        };
        switch (state(self)) {
        case 0:
        case 1:
            go(self, 0);
            set_global_byte(m_, kBack, 1);
            engine(0x004ddb70);
            set_global_word(m_, kResult, 0);
            break;
        case 7:
            go(self, 2);
            engine(0x004ddb70);
            show_item(word_at(m_, self + kSettingsItems + 8), false);
            break;
        case 8:
        case 0xb:
            back_to_choice();
            break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            go(self, 1);
            break;
        case 9:
            go(self, 5);
            break;
        case 10:
        case 0xc:
        case 0xd:
        case 0xe:
            if (global_byte(m_, kNetHost) == 0) {
                engine(0x004ddc00);
                back_to_choice();
            } else {
                engine(0x004ddb70);
                go(self, 2);
            }
            break;
        default:
            break;
        }
    }
    if ((engine_value(0x004e41b0, {0xd3}) & 0xff) != 0) {
        play(0x93);
        set_global_byte(m_, kBack, 1);
        set_global_word(m_, kResult, 0);
    }
    vcall_item("item_v7", word_at(m_, self + kMainMenuPrompt), 7);
    if ((static_cast<std::uint32_t>(s_.value("item_v7")) & 0xff) != 0 && global_byte(m_, 0x006a13fc) != 0) {
        play(0x93);
        set_global_byte(m_, kBack, 1);
        set_global_word(m_, kResult, 0);
    }
    const bool changed = word_at(m_, self + kPrevState) != state(self);
    if (changed) {
        s_.note(kSvcPrintfLog, {0x00535aac});
    }
    const bool players_changed = global_word(m_, kNetPlayers) != global_word(m_, kLastPlayers);
    set_word_at(m_, self + kPrevState, state(self));
    set_global_word(m_, kLastPlayers, global_word(m_, kNetPlayers));
    switch (state(self)) {
    case 1: {
        if (changed) {
            engine(0x004ddb70);
            show_item(word_at(m_, self + 0x1c8), false);
            if (global_byte(m_, kNetDirect) == 0) {
                item_text(word_at(m_, self + 0x158), kEmpty);
                item_text(word_at(m_, self + 0x15c), kEmpty);
                show_item(word_at(m_, self + 0x15c), false);
                item_text(word_at(m_, self + 0x160), kEmpty);
                show_item(word_at(m_, self + 0x160), false);
                set_global_byte(m_, kJoinName, 0);
            }
            std::uint32_t line = global_word(m_, kRemembered);
            if (byte_at(m_, word_at(m_, self + 0x148 + 4 * line) + 0xc) == 0) {
                line = 5;
                set_global_word(m_, kRemembered, line);
            }
            menu_.list_highlight(word_at(m_, self + kMainList), line);
        }
        enable_arrows(self);
        engine(0x00432980, {0, 1});
        if (global_word(m_, kGlobalSimTick) > word_at(m_, self + kTick)) {
            vcall_list("list_v3", word_at(m_, self + kMainList), 3);
            vcall_list("list_v5", word_at(m_, self + kMainList), 5, {word_at(m_, self + kPad)});
        }
        vcall_list("list_v4", word_at(m_, self + kMainList), 4);
        const std::uint32_t id = menu_.list_highlighted_id(word_at(m_, self + kMainList));
        if (confirm_pressed(self)) {
            set_global_byte(m_, kNetHost, 0);
            set_global_byte(m_, kNetIp, 0);
            set_global_byte(m_, kNetJoin, 0);
            set_global_word(m_, kMode, 5);
            switch (id) {
            case 1:
                set_global_byte(m_, kNetHost, 1);
                set_global_byte(m_, kJoinHost, 0);
                set_global_byte(m_, kJoinName, 0);
                if (global_byte(m_, kNetActive) == 0) {
                    engine(0x00432980, {0, 1});
                }
                go(self, 2);
                break;
            case 2:
                set_global_byte(m_, kNetJoin, 1);
                set_global_byte(m_, kJoinHost, 0);
                set_global_byte(m_, kJoinName, 0);
                engine(0x00432980, {0, 1});
                go(self, 4);
                break;
            case 5:
                set_global_byte(m_, kNetHost, 1);
                set_global_byte(m_, kNetIp, 1);
                go(self, 3);
                break;
            case 6:
                set_global_byte(m_, kNetJoin, 1);
                set_global_byte(m_, kNetIp, 1);
                go(self, 5);
                break;
            case 9:
                set_global_word(m_, kMode, 7);
                set_global_word(m_, kResult, 1);
                set_global_byte(m_, kDone, 1);
                break;
            default:
                break;
            }
            set_global_word(m_, kRemembered, id);
        }
        break;
    }
    case 2:
    case 3: {
        if (changed) {
            if (global_byte(m_, kJoined) == 0) {
                set_byte_at(m_, word_at(m_, self + 0x19c) + 0xc, 1);
                if (global_byte(m_, kNetDirect) != 0) {
                    set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 1);
                    if (global_byte(m_, kNetIp) != 0) {
                        write_string(self + kStatus, retail_format("IP: %s", {m_.cstring(kIpText, 0x40)}, {}));
                    } else {
                        set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 0);
                        set_global_byte(m_, kJoinName, 0);
                    }
                } else {
                    set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 0);
                    set_global_byte(m_, kNetIp, 0);
                    set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 0);
                    set_global_byte(m_, kJoinName, 0);
                }
            } else {
                set_byte_at(m_, word_at(m_, self + 0x19c) + 0xc, 0);
                set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 0);
                if (global_byte(m_, kNetIp) == 0) {
                    set_byte_at(m_, word_at(m_, self + 0x1a0) + 0xc, 0);
                    set_global_byte(m_, kJoinName, 0);
                } else {
                    write_string(self + kStatus, retail_format("IP: %s", {m_.cstring(kIpText, 0x40)}, {}));
                }
            }
            std::uint32_t line = global_word(m_, kRememberedForm);
            if (byte_at(m_, word_at(m_, self + 0x198 + 4 * line) + 0xc) == 0) {
                line = 1;
                set_global_word(m_, kRememberedForm, line);
            }
            menu_.list_highlight(word_at(m_, self + kFormList), line);
        }
        vcall_list("list_v3", word_at(m_, self + kFormList), 3);
        vcall_list("list_v5", word_at(m_, self + kFormList), 5, {word_at(m_, self + kPad)});
        vcall_list("list_v4", word_at(m_, self + kFormList), 4);
        const std::uint32_t id = menu_.list_highlighted_id(word_at(m_, self + kFormList));
        if (id != 1) {
            default_host_name();
        }
        if (confirm_pressed(self)) {
            bool leave = true;
            switch (id) {
            case 6:
                set_global_word(m_, kMode, 9);
                break;
            case 7:
                set_global_word(m_, kMode, 5);
                break;
            case 8:
                set_global_word(m_, kMode, 8);
                break;
            case 9:
                set_global_word(m_, kMode, 4);
                break;
            default:
                leave = false;
                break;
            }
            if (leave) {
                set_global_byte(m_, kDone, 1);
            }
            set_global_word(m_, kResult, 2);
            set_global_word(m_, kRememberedForm, id);
        }
        break;
    }
    case 4:
    case 8:
        set_word_at(m_, word_at(m_, self + kWindowLeft) + 0x14, 0);
        set_word_at(m_, word_at(m_, self + kWindowRight) + 0x14, 0);
        set_global_byte(m_, kNetBusy, 1);
        engine(0x004de180, {100, 0x0055fe10});
        engine(seam::net::NetGame_SessionStart);
        set_status(self, 0x0052eacc);
        go(self, 0xb);
        break;
    case 5: {
        vcall_list("list_v3", word_at(m_, self + 0x1cc), 3);
        vcall_list("list_v5", word_at(m_, self + 0x1cc), 5, {word_at(m_, self + kPad)});
        vcall_list("list_v4", word_at(m_, self + 0x1cc), 4);
        (void)menu_.list_highlighted_id(word_at(m_, self + 0x1cc));
        if (confirm_pressed(self)) {
            set_global_byte(m_, kNetBusy, 1);
            engine(seam::net::NetGame_SessionStart);
            go(self, 9);
        }
        break;
    }
    case 6:
        if (changed) {
            default_host_name();
        }
        set_global_byte(m_, kNetBusy, 1);
        if (global_byte(m_, kJoined) == 0 || s(global_word(m_, kNetPlayers)) < 1) {
            if (global_byte(m_, kNetActive) == 0) {
                engine(seam::net::NetGame_SessionStart);
            }
            set_status(self, 0x0052eac4);
            go(self, 7);
        } else {
            go(self, 0xc);
        }
        break;
    case 7:
        update_lobby(self);
        if (s(global_word(m_, kNetPlayerCount)) > 2) {
            go(self, 0xc);
        }
        break;
    case 9:
        set_status(self, 0x0052ead8);
        if (word_at(m_, self + kRefreshAt) < global_word(m_, kGlobalSimTick)) {
            refresh_games(self);
            set_word_at(m_, self + kRefreshAt, global_word(m_, kGlobalSimTick) + 0x3c);
        }
        if (s(word_at(m_, self + kFound)) > 0) {
            if ((engine_value(0x004dde10, {0, kJoinAddress}) & 0xff) == 0) {
                set_word_at(m_, self + kFound, 0);
            } else {
                set_word_at(m_, word_at(m_, self + kWindowLeft) + 0x14, 1);
                set_word_at(m_, word_at(m_, self + kWindowRight) + 0x14, 1);
                go(self, 10);
            }
        }
        break;
    case 10:
        set_status(self, 0x0052eadc);
        if (s(global_word(m_, kNetPlayerCount)) > 2) {
            add_chat(self, 2, scratch_line(retail_format(m_.cstring(global_word(m_, 0x0052eae4), 0x40), {m_.cstring(kHostName, 0x40)}, {})));
            go(self, 0xc);
        }
        break;
    case 0xb: {
        set_word_at(m_, word_at(m_, self + kWindowLeft) + 0x14, 0);
        set_word_at(m_, word_at(m_, self + kWindowRight) + 0x14, 0);
        if (word_at(m_, self + kRefreshAt) < global_word(m_, kGlobalSimTick)) {
            refresh_games(self);
            if (s(word_at(m_, self + kFound)) < 1) {
                set_word_at(m_, self + kRefreshAt, global_word(m_, kGlobalSimTick) + 0x1e);
                set_status(self, 0x0052eacc);
            } else {
                set_word_at(m_, self + kRefreshAt, global_word(m_, kGlobalSimTick) + 0x3c);
            }
        }
        vcall_list("list_v3", word_at(m_, self + kFoundList), 3);
        if (s(word_at(m_, self + kFound)) > 0) {
            vcall_list("list_v5", word_at(m_, self + kFoundList), 5, {word_at(m_, self + kPad)});
            set_status(self, 0x0052ead0);
        }
        vcall_list("list_v4", word_at(m_, self + kFoundList), 4);
        if (confirm_pressed(self) && s(word_at(m_, self + kFound)) > 0) {
            const std::uint32_t id = menu_.list_highlighted_id(word_at(m_, self + kFoundList));
            if ((engine_value(0x004dde10, {id, kJoinAddress}) & 0xff) == 0) {
                set_word_at(m_, self + kFound, 0);
                set_word_at(m_, self + kRefreshAt, global_word(m_, kGlobalSimTick) + 0x1a4);
                item_text(word_at(m_, self + kFoundItems + 4 * id), kEmpty);
                show_item(word_at(m_, self + kFoundItems + 4 * id), false);
                write_string(self + kStatus, m_.cstring(kEmpty, 4));      // (0x005252f0 is a single space, not an empty string)
            } else {
                go(self, 10);
                set_status(self, 0x0052ead4);
                set_word_at(m_, word_at(m_, self + kWindowLeft) + 0x14, 1);
                set_word_at(m_, word_at(m_, self + kWindowRight) + 0x14, 1);
            }
        }
        break;
    }
    case 0xc:
        if (changed) {
            engine(0x004de9c0);
            engine(0x004de570, {5});
            set_status(self, 0x0052eae0);
            set_global_byte(m_, kJoined, 0);
        }
        if (players_changed && global_word(m_, kNetPlayers) == 5) {
            add_chat(self, 2, scratch_line(retail_format(m_.cstring(global_word(m_, 0x0052eac8), 0x40), {m_.cstring(kPlayerName, 0x40)}, {})));
        }
        if (global_byte(m_, kNetLost) != 0) {
            set_status(self, 0x0052eae8);
            add_chat(self, 2, global_word(m_, 0x0052eae8));
            if (global_byte(m_, kNetHost) != 0) {
                engine(0x004ddb70);
            }
            go(self, 1);
        }
        update_lobby(self);
        break;
    case 0xd:
        update_lobby(self);
        break;
    case 0xe:
        set_status(self, 0x0052eae0);
        set_global_byte(m_, kDone, 1);
        break;
    default:
        break;
    }
    set_word_at(m_, self + kFontA + 0x14, 0x60);
    set_word_at(m_, self + kFontA + 0x1c, 0x60);
    set_word_at(m_, self + kFontA + 0x18, 0xff);
    if (byte_at(m_, self + kStatus) != 0) {
        s_.note(kFontDrawService, {self + kFontA, 0x100, 0xbe, self + kStatus, 0}, m_.cstring(self + kStatus, 0x40));
    }
    w_.window_draw(self, 0, 0);
    draw_background(word_at(m_, self + kBackground));
}

// 0x0047fc70: a host without a name is called "THPS2 Host"
void NetMenu::default_host_name() {
    const std::string name = m_.cstring(kHostName, 0x40);
    for (std::size_t k = 0; k < name.size(); ++k) {
        const std::uint32_t space = engine_value(seam::crt::FUN_005009d1, {u(static_cast<std::int8_t>(name[k]))});
        if (space == 0) {
            return;
        }
    }
    const std::string fallback = m_.cstring(0x00535aec, 0x20);
    std::vector<std::uint8_t> bytes(fallback.begin(), fallback.end());
    bytes.push_back(0);
    m_.write(kHostName, bytes);
}

}  // namespace opentony::runtime::frontend
