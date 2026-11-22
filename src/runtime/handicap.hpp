#pragma once

#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f3: the two-player handicap screen (handicap.cpp, 0x00454e80..0x00455f00) from the disassembly: frontend screen 19 (MULTI_HANDICAP). Each of two players
// moves a handicap between -5 and 5 (it is added to or subtracted from the skater's stats) and confirms; the screen ends when both are ready or one backs out. Like
// the other screens it works on retail memory (the window base, the menu toolkit, the font code and the skater manager run natively); the engine's sprite, sound and
// prompt calls are seams reported as services.
//
// HandicapPlayer (0x364 bytes, vtable 0x00518a08): a Window, one player's panel. +0x0ac and +0x1f8 two Font objects (s2font1: the name; s2font2: the lines)
//   +0x344, +0x348 two dots sprites   +0x34c, +0x350 two MenuDots (the handicap as a row of dots, dim and lit)   +0x354 the skater's face (an element)
//   +0x358 the face again (a sprite drawn by hand when the player is ready)   +0x35c ready (byte)   +0x360 the player (0 or 1).
// HandicapWindow (0x350 bytes, vtable 0x00518a28): a Window. +0x0ac the background   +0x0b0 and +0x1fc two Fonts   +0x348 and +0x34c the two panels.
// The handicaps are the two words at 0x0056a2f0 (one per player).
constexpr std::uint32_t kVtHandicapPlayer = 0x00518a08;
constexpr std::uint32_t kVtHandicapWindow = 0x00518a28;
constexpr std::uint32_t kHandicapValues = 0x0056a2f0;
constexpr std::uint32_t kSecondPad = 0x0056b2b0;        // non-zero when a second pad is connected

class HandicapScreen final : public ScreenKit {
public:
    HandicapScreen(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen callbacks and the loop ----
    void init_callbacks(std::uint32_t record);                                                  // 0x00454e80
    void exit_callback();                                                                       // 0x00454ea0
    void screen_call(std::uint32_t argument, std::uint32_t result);                             // 0x00454eb0
    std::int32_t run();                                                                         // 0x00455f00

    // ---- a player's panel ----
    std::uint32_t panel_construct(std::uint32_t self, std::int32_t player, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00454ec0
    std::uint32_t panel_delete(std::uint32_t self, std::uint32_t flag);                         // 0x00455410
    void panel_destroy(std::uint32_t self);                                                     // 0x00455430
    std::uint32_t panel_poll(std::uint32_t self);                                               // 0x004554f0
    void panel_update(std::uint32_t self);                                                      // 0x00455590
    void panel_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                        // 0x00455690

    // ---- the window ----
    std::uint32_t window_construct(std::uint32_t self);                                         // 0x004558c0
    std::uint32_t window_delete(std::uint32_t self, std::uint32_t flag);                        // 0x00455d20
    void window_destroy(std::uint32_t self);                                                    // 0x00455d40
    void window_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);                     // 0x00455e10
    std::uint32_t window_step(std::uint32_t self);                                              // 0x00455e90

    // The bytes the panel's constructor leaves pointing into the caller's frame (the name element holds the address of the local buffer the name was formatted
    // into): the replay does not compare them.
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified() const { return unspecified_; }
    void reset_unspecified() { unspecified_.clear(); }

private:
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
};

}  // namespace opentony::runtime::frontend
