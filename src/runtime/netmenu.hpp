#pragma once

#include "screen_kit.hpp"
#include "guest_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10i2: netmenu.cpp, the network lobby. Online and LAN play are not part of the native build (the DirectPlay layer is out of scope), but the screen is: the lobby a
// two-player game and the hot seat go through. The screen is a window of five menu lists (the main choices, the host/join/connect form, the game list, the game settings,
// the status lines), a chat box, a summary window ("Begin Game", the two players, the level) and a state machine of fourteen states. Everything the network layer
// answers (the session, the players, the game list) is a seam with a scripted answer, reported by its retail address.
//
// Window layout (0x9f8 bytes): +0xac the tick the screen was built at plus two, +0xb0 the status line, +0xf0 the state, +0xf4 the state of the last frame, +0xf8 the tick of
// the next game-list refresh, +0xfc the number of games found, +0x100 the pad's last direction, +0x104 the pre-load object, +0x140 the background picture, +0x144 the main list
// (11 items at +0x148), +0x180 the found-games list (4 items at +0x184), +0x194 the form list (9 items at +0x198), +0x1bc the settings list (3 items at +0x1c0), +0x1cc the
// second settings list (3 items at +0x1d0), four fonts at +0x1dc, +0x328, +0x474 and +0x5c0 (0x14c bytes each), three styles at +0x70c, +0x710 and +0x714, seven chat
// lines of 0x38 bytes at +0x734 with their kinds at +0x718, the chat message at +0x884, the typed text at +0x8bc, the back/accept prompts at +0x9e4 and +0x9e8..+0x9f4.
namespace netmenu {

constexpr std::uint32_t kVtNetMenu = 0x00519348;
constexpr std::uint32_t kVtNetSummary = 0x00519368;
constexpr std::uint32_t kSize = 0x9f8;
constexpr std::uint32_t kSummarySize = 0x360;

// the window's fields
constexpr std::uint32_t kTick = 0xac;
constexpr std::uint32_t kStatus = 0xb0;
constexpr std::uint32_t kState = 0xf0;
constexpr std::uint32_t kPrevState = 0xf4;
constexpr std::uint32_t kRefreshAt = 0xf8;
constexpr std::uint32_t kFound = 0xfc;
constexpr std::uint32_t kPad = 0x100;
constexpr std::uint32_t kPie = 0x104;
constexpr std::uint32_t kBackground = 0x140;
constexpr std::uint32_t kMainList = 0x144;
constexpr std::uint32_t kMainItems = 0x148;
constexpr std::uint32_t kFoundList = 0x180;
constexpr std::uint32_t kFoundItems = 0x184;
constexpr std::uint32_t kFormList = 0x194;
constexpr std::uint32_t kFormItems = 0x198;
constexpr std::uint32_t kSettingsList = 0x1bc;
constexpr std::uint32_t kSettingsItems = 0x1c0;
constexpr std::uint32_t kGameList = 0x1cc;
constexpr std::uint32_t kGameItems = 0x1d0;
constexpr std::uint32_t kFontA = 0x1dc;
constexpr std::uint32_t kFontB = 0x328;
constexpr std::uint32_t kFontC = 0x474;
constexpr std::uint32_t kFontD = 0x5c0;
constexpr std::uint32_t kStyleA = 0x70c;
constexpr std::uint32_t kStyleB = 0x710;
constexpr std::uint32_t kStyleC = 0x714;
constexpr std::uint32_t kChatKinds = 0x718;
constexpr std::uint32_t kChatLines = 0x734;
constexpr std::uint32_t kChatCurrent = 0x730;
constexpr std::uint32_t kChatText = 0x884;
constexpr std::uint32_t kTyped = 0x8bc;
constexpr std::uint32_t kMainMenuPrompt = 0x9e4;
constexpr std::uint32_t kArrowA = 0x9e8;
constexpr std::uint32_t kArrowB = 0x9ec;
constexpr std::uint32_t kWindowLeft = 0x9f0;
constexpr std::uint32_t kWindowRight = 0x9f4;

}  // namespace netmenu

class NetMenu final : public ScreenKit {
public:
    NetMenu(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen ----
    std::int32_t run(std::uint32_t flag);                                                                  // 0x0047cbb0 (NetMenu_Main)
    std::uint32_t construct(std::uint32_t self);                                                           // 0x0047ce60
    std::uint32_t delete_window(std::uint32_t self, std::uint32_t flag);                                   // 0x0047e240
    void destruct(std::uint32_t self);                                                                     // 0x0047e260
    void update(std::uint32_t self);                                                                       // 0x0047e770
    void refresh_games(std::uint32_t self);                                                                // 0x0047cdc0
    bool is_ready(std::uint32_t self) const { return guest::read_scalar<std::uint8_t>(m_, self + 0x138) != 0; }                          // 0x0047cdb0
    void no_op() {}                                                                                        // 0x0047cda0 (ret 4), 0x0047f820
    void set_item_textures(std::uint32_t items, std::uint32_t table, std::int32_t count);                  // 0x0047e210
    void enable_arrows(std::uint32_t self);                                                                // 0x0047e3c0
    void add_chat(std::uint32_t self, std::uint32_t kind, std::uint32_t text);                             // 0x0047e3e0
    void update_lobby(std::uint32_t self);                                                                 // 0x0047e4e0
    bool confirm_pressed(std::uint32_t self);                                                              // 0x0047f1e0
    bool return_false() const { return false; }                                                            // 0x0047f240 (ret 4)
    bool summary_ready(std::uint32_t self) const { return guest::read_scalar<std::uint8_t>(m_, self + 0xac) != 0; }                       // 0x0047f830

    // ---- the summary window (0x360 bytes) and the screen's functions ----
    std::uint32_t summary_construct(std::uint32_t self);                                                   // 0x0047f250
    std::uint32_t summary_delete(std::uint32_t self, std::uint32_t flag);                                  // 0x0047f770
    void summary_destruct(std::uint32_t self);                                                             // 0x0047f790
    std::uint32_t summary_draw_player(std::uint32_t self, std::uint32_t player);                           // 0x0047f840
    void summary_draw(std::uint32_t self);                                                                 // 0x0047f930
    bool return_true() const { return true; }                                                              // 0x0047fac0
    bool back_pressed();                                                                                   // 0x0047fad0
    void init_summary(std::uint32_t self);                                                                 // 0x0047fb20
    void summary_update();                                                                                 // 0x0047fc00
    void close_summary(std::uint32_t flag);                                                                // 0x0047fc20
    void default_host_name();                                                                              // 0x0047fc70

private:
    void new_font(std::uint32_t font_struct);
    void font_from_load(std::uint32_t font_struct);
    void font_from_find(std::uint32_t font_struct);
    std::uint32_t make_style(std::uint32_t source_font);
    std::uint32_t make_item(std::uint32_t id, std::uint32_t text, std::uint32_t style, bool hidden);
    std::uint32_t make_edit(std::uint32_t id, std::uint32_t text, std::uint32_t style);
    std::uint32_t make_list(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, std::uint32_t centre = 1);
    void add_to_list(std::uint32_t list, std::uint32_t item) { vcall_list("list_v6", list, 6, {item}); }
    void play(std::uint32_t sound) { s_.note(kSvcSfx, {sound, 0x2000, 0}); }
    void set_status(std::uint32_t self, std::uint32_t format_cell);
    std::uint32_t state(std::uint32_t self) const { return guest::read_scalar<std::uint32_t>(m_, self + netmenu::kState); }
    void go(std::uint32_t self, std::uint32_t to) { guest::write_scalar<std::uint32_t>(m_, self + netmenu::kState, static_cast<std::uint32_t>(to)); }
    void show_item(std::uint32_t item, bool shown) { guest::write_scalar<std::uint8_t>(m_, item + 0xc, static_cast<std::uint8_t>(shown ? 1 : 0)); }
    void item_text(std::uint32_t item, std::uint32_t text) { vcall_item("item_v8", item, 8, {text}); }
    bool pack_new_free() { return (engine_value(0x004de150) & 0xff) != 0; }
};

}  // namespace opentony::runtime::frontend
