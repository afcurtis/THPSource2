#pragma once

#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f3: the level select screen (levelsel.cpp, 0x004588c0..0x0045b080) from the disassembly: frontend screen 2 (LEVEL_SELECT). The player turns a globe to a
// level (left and right), confirms it, and in the career gets a menu (Play, Saved Parks, Pre-made parks) before the level starts; the park rows open the custom
// park menu. The window holds a banner for each player, the globe, the level's name, two information windows and the menu. Like the other screens it works on
// retail memory (the window base, the menu toolkit, the font code and the skater manager run natively); the engine's PSX, sprite, sound, card and prompt calls, the
// information windows of 4.10h (0x0045b0a0, 0x0045b950) and the park menu are seams reported as services.
//
// The level table is at 0x0053c1f8, 0x1ac bytes a level: +0 the name (a string), +0x40 a pointer to the location, +0x48 a byte (non-zero: the level has no tapes
// to count), +0x70 and +0x74 the globe's longitude and latitude. The chosen level is the word at 0x00561ce4 (13 is the custom park option); the game mode is
// 0x00561c74 (1 career, 2 free skate, 3 single session ...).
//
// LevelSelect (0x628 bytes, vtable 0x00518a90): a Window. +0x0ac, +0x0b0 the two banners (0 without)   +0x0b4 the banner's x   +0x0b8..+0x0c8 five prompt windows
//   (select, accept, back, goals, blank)   +0x0cc the name window   +0x0d0 the globe   +0x0d4 the info window (0x0045b0a0)   +0x0d8 the goals window (0x0045b950)
//   +0x0dc the mode menu   +0x0e0 the goal percentage prompt (career)   +0x0e4 the background   +0x0e8, +0x234, +0x380, +0x4cc four Fonts (mainf unused, s2font2,
//   s2font1, s2font3)   +0x618 the state (0 choosing the level, 1 the mode menu, 2 the park menu)   +0x61c the park menu   +0x620 done (byte)   +0x624 the answer.
// LevelBanner (0x22c bytes, 0x00518ab0): +0x0ac the face   +0x0b0, +0x0b4 the name's two lines (texts)   +0x0b8 a Font   +0x204 and +0x218 two 0x14-byte buffers.
// LevelGlobe (0xa4, 0x00518ad0): a ScreenElement. +0x2c a PSX object record   +0x78 the PSX region of the model   +0x7c the crosshair sprite   +0x80 and +0x84 the latitude
//   now and wanted   +0x88 and +0x8c the longitude now and wanted   +0x90..+0x9c the view rectangle and planes.
// LevelName (0x210, 0x00518af0): +0x0ac, +0x0b0 two flagged texts (name, location)   +0x0b4 a Font   +0x200 the slide distance   +0x204 the level shown   +0x208 the level
//   asked for   +0x20c the slide state (0 sliding out, 1 shown, 2 waiting).
// ModeMenu (0x240, 0x00518b10): +0x0ac the scroll list   +0x0b0 its rows (0x60 slots)   +0x230 the style   +0x234 the result   +0x238 a byte   +0x23c which pad confirmed.
constexpr std::uint32_t kVtLevelSelect = 0x00518a90;
constexpr std::uint32_t kVtLevelBanner = 0x00518ab0;
constexpr std::uint32_t kVtLevelGlobe = 0x00518ad0;
constexpr std::uint32_t kVtLevelName = 0x00518af0;
constexpr std::uint32_t kVtModeMenu = 0x00518b10;
constexpr std::uint32_t kLevelTable = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kChosenLevel = 0x00561ce4;
constexpr std::uint32_t kLevelGameMode = 0x00561c74;

class LevelSelect final : public ScreenKit {
public:
    LevelSelect(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen callbacks and the loop ----
    void init_callbacks(std::uint32_t record);                                           // 0x004588c0
    void exit_callback();                                                                // 0x004588e0
    void screen_call(std::uint32_t argument, std::uint32_t result);                      // 0x004588f0
    std::int32_t run();                                                                  // 0x00458900

    // ---- the window ----
    std::uint32_t construct(std::uint32_t self);                                         // 0x00458b90
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flag);                 // 0x004594e0
    void destroy(std::uint32_t self);                                                    // 0x00459500
    void draw(std::uint32_t self);                                                       // 0x004595e0
    void update(std::uint32_t self);                                                     // 0x00459650
    void change_level(std::uint32_t self, std::int32_t step);                            // 0x00459be0
    bool step(std::uint32_t self, std::uint32_t out);                                    // 0x00459d90

    // ---- a player's banner ----
    std::uint32_t banner_construct(std::uint32_t self, std::int32_t player, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00459db0
    std::uint32_t banner_delete(std::uint32_t self, std::uint32_t flag);                 // 0x0045a0a0
    void banner_destroy(std::uint32_t self);                                             // 0x0045a0c0
    std::int32_t copy_name_word(std::uint32_t dest, std::uint32_t source, bool ellipsis);   // 0x0045a120

    // ---- the globe ----
    std::uint32_t globe_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x0045a1b0
    std::uint32_t globe_delete(std::uint32_t self, std::uint32_t flag);                  // 0x0045a3e0
    void globe_destroy(std::uint32_t self);                                              // 0x0045a400
    void globe_update(std::uint32_t self);                                               // 0x0045a490
    void globe_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                 // 0x0045a5a0
    void globe_set_level(std::uint32_t self, std::int32_t level);                        // 0x0045a6b0

    // ---- the level name window ----
    std::uint32_t name_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x0045a6e0
    std::uint32_t name_delete(std::uint32_t self, std::uint32_t flag);                   // 0x0045a920
    void name_destroy(std::uint32_t self);                                               // 0x0045a940
    void name_set_level(std::uint32_t self, std::int32_t level);                         // 0x0045a9a0
    void name_update(std::uint32_t self);                                                // 0x0045aa40
    void name_refresh(std::uint32_t self);                                               // 0x0045ab50

    // ---- the career's mode menu ----
    std::uint32_t menu_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y);   // 0x0045abc0
    std::uint32_t menu_delete(std::uint32_t self, std::uint32_t flag);                   // 0x0045aeb0
    void menu_destroy(std::uint32_t self);                                               // 0x0045aed0
    void menu_highlight_play(std::uint32_t self);                                        // 0x0045af60
    void menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                  // 0x0045af70
    std::uint32_t menu_result(std::uint32_t self, std::uint32_t out);                    // 0x0045b080

private:
    [[nodiscard]] static std::uint32_t level_row(std::int32_t level) { return kLevelTable + kLevelStride * static_cast<std::uint32_t>(level); }
    std::uint32_t copy_font(std::uint32_t dest, std::uint32_t name);
};

}  // namespace opentony::runtime::frontend
