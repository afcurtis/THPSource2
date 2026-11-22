#pragma once

#include "pc_platform.hpp"
#include "guest_view.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime::pcplatform {

// Phase 4.13b2: the game's side of the two-player network layer (pc\netgame.cpp and the mouse and text-entry code that shares its address range). Like the rest of 4.13 the
// functions work on the game's globals at their retail addresses. The layer is gated by one flag (`kNetFlag`, 0x006a3b21): while it is clear (a single-player game) every
// function that is not a plain query returns at once. DirectPlay (the 0x004daf10..0x004dbd90 wrappers: open, send, receive, close), the message handlers below the update
// function, the sound loops, the pad and every engine routine outside the range are services (`engine_<address>`).
namespace net {

// ---- the mouse (0x006a13e0..0x006a1430) ----
constexpr std::uint32_t kMouseX = 0x006a13f0;              // the pointer in a 320 x 240 space
constexpr std::uint32_t kMouseY = 0x006a13f4;
constexpr std::uint32_t kMouseButtons = 0x006a13f8;        // the button word (non-zero while one is down)
constexpr std::uint32_t kMouseReleased = 0x006a13fc;       // byte: a button went up this poll
constexpr std::uint32_t kMouseChanged = 0x006a13fd;        // byte: the pointer or a button changed this poll
constexpr std::uint32_t kMouseActive = 0x006a13fe;         // byte: the pointer drives the menus (set when the cursors are loaded)
constexpr std::uint32_t kMouseOverItem = 0x006a13ff;       // byte: the pointer is over a menu item
constexpr std::uint32_t kCursorsLoaded = 0x006a1400;       // byte: the two cursor sprites are loaded
constexpr std::uint32_t kMouseHot = 0x006a1404;            // dword
constexpr std::uint32_t kMouseLastId = 0x006a1408;         // dword: the id of the last item the pointer touched
constexpr std::uint32_t kMouseListRow = 0x006a1410;        // dword: the list row under the pointer
constexpr std::uint32_t kCursorHidden = 0x006a1414;        // byte
constexpr std::uint32_t kCursorShown = 0x006a1415;         // byte
constexpr std::uint32_t kMouseFlagA = 0x006a1416;          // bytes cleared when the cursors load
constexpr std::uint32_t kMouseFlagB = 0x006a1417;
constexpr std::uint32_t kCursorSprites = 0x006a1424;       // two pointers: the arrow and the hand
constexpr std::uint32_t kSliderValue = 0x006a142c;         // dword: the slider the pointer drags
constexpr std::uint32_t kSliderTicks = 0x006a1430;         // dword: polls since it was grabbed
constexpr std::uint32_t kPointerCenterX = 0x006a111c;      // the pointer frame's edge-scroll box: its middle (0x140, 0xf0 once the pointer has been put back)
constexpr std::uint32_t kPointerCenterY = 0x006a13e0;
constexpr std::uint32_t kPointerEdgeBits = 0x006a13e4;     // dword the frame clears
constexpr std::uint32_t kLastKey = 0x006a13e8;             // the key the text entry saw last frame
constexpr std::uint32_t kPointerSavedX = 0x006a13ec;       // the pointer when a drag ended
constexpr std::uint32_t kPointerSavedY = 0x006a1118;
constexpr std::uint32_t kPointerX = 0x006a44f0;            // the pointer the input layer reports (display pixels)
constexpr std::uint32_t kPointerY = 0x006a44f4;
constexpr std::uint32_t kDisplayWidth = 0x029da394;        // the display size the pointer is scaled by
constexpr std::uint32_t kDisplayHeight = 0x029da398;
constexpr std::uint32_t kKeyTable = 0x0054a3a8;            // 12-byte rows {u16 key, name, replacement}: the key names the bindings use

// ---- the netgame state ----
constexpr std::uint32_t kNetFlag = 0x006a3b21;             // byte: a network game is running (every function below is a no-op while it is clear)
constexpr std::uint32_t kNetRequested = 0x006a3b20;        // byte: the command line asked for a network game
constexpr std::uint32_t kOptHost = 0x006a3b22;             // bytes the command line options set
constexpr std::uint32_t kOptJoin = 0x006a3b23;
constexpr std::uint32_t kOptMisc = 0x006a3b25;
constexpr std::uint32_t kSessionOpen = 0x006a3b26;         // byte: a DirectPlay session exists
constexpr std::uint32_t kLastPoll = 0x006a3b28;            // dword: the clock of the last heard message
constexpr std::uint32_t kLostLink = 0x006a3b30;            // byte: the other machine went away
constexpr std::uint32_t kSessionName = 0x006a3b31;         // text: the session name
constexpr std::uint32_t kPlayerName = 0x006a3b71;          // text: this player's name
constexpr std::uint32_t kAddressText = 0x006a3bb1;         // text: the address / password typed in the lobby
constexpr std::uint32_t kStatusText = 0x006a3bd1;          // text: the line drawn over the game while waiting
constexpr std::uint32_t kLocalState = 0x006a3a90;          // dword: this machine's step of the handshake (0..0x16)
constexpr std::uint32_t kRemoteState = 0x006a18b0;         // dword: the other machine's step
constexpr std::uint32_t kLocalPrev = 0x006a3a98;
constexpr std::uint32_t kRemotePrev = 0x006a18b8;
constexpr std::uint32_t kLocalFlags = 0x006a3a9c;          // dword: the flag bits this machine reports
constexpr std::uint32_t kRemoteFlags = 0x006a18bc;         // dword: the flag bits the other machine reported
constexpr std::uint32_t kLocalPlayer = 0x006a3a88;         // the skater objects of this machine and of the other one
constexpr std::uint32_t kRemotePlayer = 0x006a18a8;
constexpr std::uint32_t kLocalIndex = 0x006a3d28;
constexpr std::uint32_t kRemoteIndex = 0x006a3d2c;
constexpr std::uint32_t kDirectPlay = 0x006a05c8;          // the DirectPlay object (0x88 bytes)
constexpr std::uint32_t kIsHost = 0x006a05d8;              // dword: this machine hosts the session
constexpr std::uint32_t kClock = 0x0056e31c;               // the frame clock

}  // namespace net

class NetGame final : public PcKit {
public:
    NetGame(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    // ---- the mouse and the pointer-driven widgets ----
    void mouse_load_cursors();                                                       // 0x004dbd90
    void mouse_free_cursors();                                                       // 0x004dbe60
    void mouse_draw_cursor();                                                        // 0x004dbea0
    void mouse_track(std::uint32_t x, std::uint32_t y, std::uint32_t buttons);       // 0x004dbee0
    void mouse_enable(std::uint32_t on);                                             // 0x004dc020
    std::uint32_t translate_key_name(std::uint32_t name);                            // 0x004dc050
    void dots_to_spaces(std::uint32_t text);                                         // 0x004dc090
    std::uint32_t blank_text() { return 0x0055fe10; }                                // 0x004dc0b0
    void banner_style();                                                             // 0x004dc0c0
    std::uint32_t edit_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3);   // 0x004dc100
    std::uint32_t edit_delete(std::uint32_t self, std::uint32_t flags);              // 0x004dc160
    void edit_destruct(std::uint32_t self);                                          // 0x004dc180
    void edit_set_buffer(std::uint32_t self, std::uint32_t buffer, std::uint32_t capacity, std::uint32_t colour);   // 0x004dc370
    void text_edit_update(std::uint32_t self);                                       // 0x004dc190 (the text entry control's per-frame method)
    void file_list_load(std::uint32_t self, std::uint32_t ids, std::uint32_t count); // 0x004dc3a0
    std::uint32_t pointer_frame();                                                   // 0x004dd390 (the park editor's pointer: a drag outside the box presses the pad, the two bars' halves too)
    std::uint32_t slider_pick(std::uint32_t self);                                   // 0x004dc7b0
    // the "pick by mouse" method (vtable slot 7) of the window-graph classes: only a vtable reaches them, which is why scope.yml filed them with the netgame (milestone 7c)
    std::uint32_t hit_rect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);   // 0x004dbf90
    std::uint32_t menu_list_pick(std::uint32_t self);                                // 0x004dc480 (MenuList)
    std::uint32_t element_pick(std::uint32_t self);                                  // 0x004dc9f0 (ScreenElement, Window, OptionsMenu ...)
    std::uint32_t text_element_pick(std::uint32_t self);                             // 0x004dca60 (TextScreenElement)
    std::uint32_t sprite_element_pick(std::uint32_t self);                           // 0x004dcb30 (SpriteScreenElement)
    std::uint32_t image_pick(std::uint32_t self);                                    // 0x004dcb70 (SlicedImage2)
    std::uint32_t prompt_window_pick(std::uint32_t self);                            // 0x004dcc20 (PromptWindow)
    // milestone 9: the main menu's two (in no symbol table; the cold boot's main menu is the first to call them) and the wheel's circle test (scope.yml: directplay)
    std::uint32_t hit_circle(std::uint32_t x, std::uint32_t y, std::uint32_t radius);   // 0x004dbfd0
    std::uint32_t main_menu_wheel_pick(std::uint32_t self);                          // 0x004dcf80 (MainMenuWheel)
    std::uint32_t main_menu_window_pick(std::uint32_t self);                         // 0x004dd140 (MainMenuWindow)
    std::uint32_t level_globe_pick(std::uint32_t self);                              // 0x004dcec0 (LevelGlobe, slot 7)
    void widget_set_colour(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c);   // 0x004dcb00
    void key_code_by_name(std::uint32_t self, std::uint32_t name);                   // 0x004dcbc0
    std::uint32_t pick_row(std::uint32_t self);                                      // 0x004dd520
    std::uint32_t grid_pick(std::uint32_t self);                                     // 0x004dc530 (a grid of items)
    std::uint32_t button_pick(std::uint32_t self);                                   // 0x004dc5c0 (a button; 0x004dc630 jumps here)
    std::uint32_t column_list_pick(std::uint32_t self);                              // 0x004dc640 (a list of columns with two arrows, +0xf8 / +0xfc)
    std::uint32_t right_button_pick(std::uint32_t self);                             // 0x004dc8a0 (a button that sends right)
    std::uint32_t arrow_pair_pick(std::uint32_t self);                               // 0x004dc8d0 (a bar with two arrows, +0x70 / +0x74)
    std::uint32_t arrow_children_pick(std::uint32_t self);                           // 0x004dd180 (three children at +0xb0's +0x3c / +0x40 / +0x44)
    void set_tint(std::uint32_t self, std::uint32_t r, std::uint32_t g, std::uint32_t b);   // 0x004dd6d0

    // ---- the network state, its flags and small queries ----
    void net_reset(std::uint32_t argument);                                          // 0x004dd890
    void net_close();                                                                // 0x004ddb70
    void load_settings();                                                            // 0x004ddc00
    void save_settings();                                                            // 0x004ddce0
    std::uint32_t resolve_name(std::uint32_t key);                                   // 0x004dddb0
    bool connect(std::uint32_t target, std::uint32_t buffer);                        // 0x004dde10
    void register_player(std::uint32_t skater);                                      // 0x004ddf30
    void clear_players() { guest::write_scalar<std::uint32_t>(m_, net::kLocalPlayer, 0); guest::write_scalar<std::uint32_t>(m_, net::kRemotePlayer, 0); }   // 0x004de000
    bool is_remote_player(std::uint32_t skater);                                     // 0x004de010
    bool is_host() { return guest::read_scalar<std::uint8_t>(m_, net::kNetFlag) != 0 && guest::read_scalar<std::uint32_t>(m_, net::kIsHost) != 0; }          // 0x004de040
    bool is_guest() { return guest::read_scalar<std::uint8_t>(m_, net::kNetFlag) != 0 && guest::read_scalar<std::uint32_t>(m_, net::kIsHost) == 0; }         // 0x004de060
    std::uint32_t pause_state(std::uint32_t request, std::uint32_t on);              // 0x004de0a0
    bool game_mode_runs_net() { const auto mode = guest::read_scalar<std::uint32_t>(m_, 0x00561c74); return mode == 5 || mode == 8 || mode == 9; }   // 0x004de150
    std::uint32_t abort_requested();                                                 // 0x004de170
    void set_custom_level(std::int32_t kind, std::uint32_t name);                    // 0x004de180
    void restore_level();                                                            // 0x004de270
    std::uint32_t load_custom_level();                                               // 0x004de2b0
    std::uint32_t tint_or(std::uint32_t fallback);                                   // 0x004de300
    void stash_career_state();                                                       // 0x004de320
    void restore_career_state();                                                     // 0x004de420
    void set_local_flag(std::uint32_t mask, std::uint32_t on);                       // 0x004de4d0
    void set_remote_flag(std::uint32_t mask, std::uint32_t on);                      // 0x004de500
    void set_local_state(std::uint32_t state) { guest::write_scalar<std::uint32_t>(m_, net::kLocalState, static_cast<std::uint32_t>(state)); }            // 0x004de570
    void tag_acknowledged(std::uint32_t player);                                     // 0x004de5c0
    void spawn_hit_sparks();                                                         // 0x004de5f0
    void player_tagged(std::uint32_t tagger, std::uint32_t tagged, std::uint32_t had_flag);     // 0x004de7c0
    void input_reset();                                                              // 0x004de9c0
    void status_text_draw();                                                         // 0x004df030
    std::uint32_t ring_pop(std::uint32_t ring, std::uint32_t consume);               // 0x004df1d0
    void ring_remove(std::uint32_t ring, std::uint32_t value);                       // 0x004df220
    void ring_push(std::uint32_t value);                                             // 0x004df250
    void ring_clear(std::uint32_t ring);                                             // 0x004df280
    void table_add(std::uint32_t table, std::uint32_t key, std::uint32_t value);     // 0x004df300
    std::uint32_t table_take(std::uint32_t table, std::uint32_t out, std::uint32_t consume);    // 0x004dfa00
    bool table_remove(std::uint32_t table, std::uint32_t key, std::uint32_t value);  // 0x004dfda0
    void on_point(std::uint32_t a0, std::uint32_t player, std::uint32_t value, std::uint32_t packet);   // 0x004df2c0

    // ---- the handshake and the per-frame update ----
    void begin_match();                                                              // 0x004deb00
    void end_match();                                                                // 0x004deb50
    bool pause_agreement();                                                          // 0x004dece0
    void disconnected();                                                             // 0x004dee50
    void end_run();                                                                  // 0x004defc0
    void send_string(std::uint32_t text);                                            // 0x004df370
    void send_state();                                                               // 0x004df6d0
    void wait_for_options();                                                         // 0x004dfe50
    void wait_for_loading();                                                         // 0x004dffb0
    void sync_if_five(std::uint32_t state) { if (state == 5) send_state(); }         // 0x004e0170
    void update();                                                                   // 0x004e02d0
    void on_system_message(std::uint32_t dp, std::uint32_t message);                 // 0x004e1370
    void on_custom_level(std::uint32_t message);                                     // 0x004e1250
    void on_event_message(std::uint32_t message);                                    // 0x004e1230

private:
    std::uint32_t ctype_flags(std::int32_t character, std::uint32_t mask, bool wide_word);   // the C runtime's isalnum / isspace / isdigit
    // the clock a waiting loop watches: the frame tick moves it; the replay applies the value the stub wrote
    void frame_tick();
    void set_pending_buttons(std::uint32_t buttons) { engine(0x004e4190, {buttons & 0xffff}); }
};

}  // namespace opentony::runtime::pcplatform
