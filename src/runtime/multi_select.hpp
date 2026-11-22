#pragma once

#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f3: the multi-player select screen (multisel.cpp, 0x0047ba30..0x0047c560) from the disassembly: frontend screen 9 (MULTI_SELECT), the list of two-player
// games (Trick attack, Graffiti, Horse, Tag, Free Skate). Like the other screens it works on retail memory; the engine's sprite, sound and prompt calls are seams
// reported as services. (The retail netgame flag at 0x006a3b21 stays clear on this path: a retail capture walked the screen with it watched.)
//
// MultiSelect (0x238 bytes, vtable 0x00519328): a Window. +0x0ac the background   +0x0b0 a Font (s2font2)   +0x1fc the MenuList   +0x200 and +0x204 two
//   MenuProperties (the rows' style from mainf, and the title's, with its own colour)   +0x208 ten row slots (the scaled title "2 player games", then Trick attack
//   id 5, Graffiti 8, Horse 7, Tag 9, Free Skate 4)   +0x230, +0x234 the arrow sprites.
// The screen's answer goes to the global 0x00561c6c: the highlighted row's id, or 2 for back.
constexpr std::uint32_t kVtMultiSelect = 0x00519328;
constexpr std::uint32_t kMultiAnswer = 0x00561c6c;

class MultiSelect final : public ScreenKit {
public:
    MultiSelect(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen callbacks and the loop ----
    void init_callbacks(std::uint32_t record);                                     // 0x0047ba30
    void exit_callback();                                                          // 0x0047ba50
    void screen_call(std::uint32_t argument, std::uint32_t result);                // 0x0047ba60
    std::int32_t run();                                                            // 0x0047c560

    // ---- the window ----
    std::uint32_t construct(std::uint32_t self);                                   // 0x0047ba70
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flag);           // 0x0047c290
    void destroy(std::uint32_t self);                                              // 0x0047c2b0
    void draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);               // 0x0047c3e0
    void update(std::uint32_t self);                                               // 0x0047c460
    std::uint32_t poll();                                                          // 0x0047c4a0
    std::uint32_t selected_id(std::uint32_t self);                                 // 0x0047c550
};

}  // namespace opentony::runtime::frontend
