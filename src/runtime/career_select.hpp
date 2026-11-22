#pragma once

#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f3: the career select screen (carsel.cpp, 0x004173d0..0x00418150) from the disassembly: frontend screen 10, the first entry of the main menu's wheel. A
// list of three choices (Continue career, New career, Load career), the two memory-card steps a load or a save goes through, and back. The window is built like the
// create-a-skater screens' (the window base, the menu toolkit and the font code run natively on retail memory; the engine's sprite, sound and card calls are seams
// reported as services with the arguments retail passed).
//
// CareerSelect (0x4dc bytes, vtable 0x00518508): a Window. +0x0ac the background sprite   +0x0b0 and +0x1fc two Font objects (mainf, s2font2: the list's style and the
//   prompt windows' captions)   +0x348 the MenuList   +0x34c its MenuProperties   +0x350 the rows (0x60 slots; Continue career id 3, New career id 0, Load career id 1)
//   +0x4d0 and +0x4d4 the arrow sprites   +0x4d8 the state: -1 choosing, 1 loading from the card, 2 saving to it.
// Choosing: confirm on id 0 starts a new career (the save image is reset), id 1 opens the card in load mode, id 2 in save mode (a row the screen does not build), id 3
// continues the current career; back leaves. The poll answers 1 (go on), 2 (back) or 0.
constexpr std::uint32_t kVtCareerSelect = 0x00518508;

// The front end's shared sound state: the voice (0x0052cb38), where its pan is (0x00561c84) and where it is going (0x00561c94), and the two words the position
// update scales it with.
constexpr std::uint32_t kFrontVoice = 0x0052cb38;
constexpr std::uint32_t kFrontPanCurrent = 0x00561c84;
constexpr std::uint32_t kFrontPanTarget = 0x00561c94;
constexpr std::uint32_t kFrontSoundPan = 0x0052be44;
constexpr std::uint32_t kFrontSoundScale = 0x0052be48;
constexpr std::uint32_t kFrontKeyTable = 0x00525500;

class CareerSelect final : public ScreenKit {
public:
    CareerSelect(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen callbacks and the loop ----
    void init_callbacks(std::uint32_t record);                                     // 0x004173d0
    void exit_callback();                                                          // 0x004173f0
    void screen_call(std::uint32_t argument, std::uint32_t result);                // 0x00417400
    std::int32_t run();                                                            // 0x00417f90

    // ---- the window ----
    std::uint32_t construct(std::uint32_t self);                                   // 0x00417410
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flag);           // 0x00417aa0
    void destroy(std::uint32_t self);                                              // 0x00417ac0
    void draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);               // 0x00417bf0
    void update(std::uint32_t self);                                               // 0x00417c90
    std::uint32_t update_save(std::uint32_t self);                                 // 0x00417ce0
    std::uint32_t update_load(std::uint32_t self);                                 // 0x00417d70
    std::uint32_t poll(std::uint32_t self);                                        // 0x00417e30

    // ---- the front end's shared helpers (called by every screen's loop) ----
    [[nodiscard]] std::uint32_t key_table() const;                                 // 0x00418140
    void update_menu_sound();                                                      // 0x00418150
};

}  // namespace opentony::runtime::frontend
