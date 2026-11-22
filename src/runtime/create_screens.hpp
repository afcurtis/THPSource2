#pragma once

#include "create_spool.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "screen_kit.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10e1: the create-a-skater screens, from the disassembly: the help text box and the confirm box (create_ui.cpp), the custom skater
// editor and its personal-information page (creedit.cpp), the save-skater screen (cresave.cpp), the stat editor (statedit.cpp) and the stat
// window's constructor and destructor (statwin.cpp; 4.10d ported the rest of that file).
//
// What is native here and what is a seam: the window base, the menu toolkit, the font code and the skater manager (4.10a, 4.10c, 4.8) run
// natively on the same memory. Everything else a screen reaches (the page classes of other files, the engine's model, spool, sprite and render
// calls, the memory-card screen) is reported as a named service carrying the arguments retail passed, `engine_XXXXXXXX` for the function's
// address, with a thiscall's object first; a value such as an allocation or a font is scripted.
//
// TextBox (0x41c bytes, vtable 0x0051877c): +0x04 draw-the-backdrop flag   +0x08 x   +0x0c y   +0x10 the Font object   +0x14 eight 0x80-byte
//   lines   +0x414 the line count (create_spool.hpp lays the text out).
// ConfirmBox (0x428 bytes, vtable 0x00518784): a TextBox with +0x418 a two-item MenuList, +0x41c the "yes" and +0x420 the "no" MenuText,
//   +0x424 their MenuProperties (the box's font copied in, scaled 1.0).
// CreateEditor (0x3e4 bytes, vtable 0x005187a8): a Window. +0x0ac the background sprite   +0x0b0 a Font object (s2font1)   +0x1fc a Font object
//   (s2font2)   +0x348..+0x35c the six prompt windows   +0x360 the editor skater object (0x31c bytes)   +0x364 the animated skater object (0x168
//   bytes)   +0x368 the animation region   +0x36c the page (0 personal, 1 appearance, 2 stats, 3 tricks)   +0x370 the "already built" flag the caller
//   passes   +0x374 a view (four 16-bit words: width, height, x, y; then near, far, ...)   +0x388 the light rig   +0x3bc / +0x3c0 the texture spool
//   was busy now / last frame   +0x3c4 the stat page   +0x3c8 the name text   +0x3cc the face sprite   +0x3d0 the personal page   +0x3d4 the trick
//   page   +0x3d8 the appearance page   +0x3dc the custom skater's board byte   +0x3e0 the build the stats were set for.
// PersonalPage (0x394 + bytes, vtable 0x005187c8): see skater_windows.hpp; +0x0b0 a Font object, +0x1fc the list, +0x380 / +0x384 the two styles,
//   +0x388 / +0x38c arrows, +0x390 the name entry, +0x398 five icons, +0x200 + 4 * i the rows.
// CreateSave (0x378 bytes, vtable 0x00518808): a Window. +0x0ac the background sprite, +0x0b0 / +0x1fc two Font objects, +0x348 the two-item list,
//   +0x34c the style, +0x350 / +0x354 / +0x358 text, +0x35c / +0x360 two arrows, +0x364 the state (0 list, 1 warning box, 2 card screen), +0x368
//   the warning ConfirmBox, +0x36c three prompt windows.
// CreateSelect (0x7e4 bytes, vtable 0x00518828): a Window. +0x0ac the background sprite   +0x0b0 / +0x1fc / +0x348 three Font objects (mainf, s2font2,
//   s2font1)   +0x494 the slots list and +0x498 its 0x60 rows   +0x618 the skaters list and +0x61c its 0x60 rows   +0x79c / +0x7a0 / +0x7a4 three styles
//   +0x7a8 / +0x7ac the arrows   +0x7b0 the state (0 slots, 1 overwrite question, 2 load from the card, 3 skaters, 4 delete question, 5 message, 6 save
//   to the card)   +0x7b4 the state to enter next frame   +0x7b8 the dialog (a ConfirmBox)   +0x7bc four warning lines   +0x7cc the working copy of the
//   save image   +0x7d0 five prompt windows (select, delete, load, accept, back).
// StatEdit (0x3a0 bytes, vtable 0x0051977c): a Window. +0x0ac the background sprite, +0x0b0 / +0x1fc two Font objects, +0x348 the animation region,
//   +0x34c the model region, +0x350 the animated skater, +0x354 a view, +0x368 the light rig, +0x39c the stat page.
// StatPage (0x630 bytes, vtable 0x0051973c): a Window. +0x0ac / +0x1f8 / +0x344 three Font objects, +0x490 the list, +0x614 the points block,
//   +0x618 the dots sprite, +0x628 / +0x62c the arrows (see skater_windows.hpp).
constexpr std::uint32_t kVtTextBox = 0x0051877c;
constexpr std::uint32_t kVtConfirmBox = 0x00518784;
constexpr std::uint32_t kVtCreateEditor = 0x005187a8;
constexpr std::uint32_t kVtCreateSave = 0x00518808;
constexpr std::uint32_t kVtCreateSelect = 0x00518828;
constexpr std::uint32_t kVtStatEdit = 0x0051977c;
constexpr std::uint32_t kVtStatPage = 0x0051973c;

class CreateScreens final : public ScreenKit {
public:
    CreateScreens(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu,
                  skater::SkaterManager& skaters, create::CreateSpool& spool, SkaterWindowHelpers& helpers)
        : ScreenKit(memory, services, fonts, window, menu, skaters), spool_(spool), helpers_(helpers) {}

    // ---- create_ui.cpp ----
    std::uint32_t box_construct(std::uint32_t self, std::uint32_t font, std::uint32_t text, std::int32_t x, std::int32_t y);   // 0x00425a40
    std::uint32_t box_delete(std::uint32_t self, std::uint32_t flag);                  // 0x00425b40
    void box_destroy(std::uint32_t self);                                              // 0x00425b60
    void box_draw(std::uint32_t self);                                                 // 0x00425b70
    std::uint32_t confirm_construct(std::uint32_t self, std::uint32_t font, std::uint32_t text, std::int32_t x, std::int32_t y);   // 0x00425fa0
    std::uint32_t confirm_delete(std::uint32_t self, std::uint32_t flag);              // 0x004261f0
    void confirm_destroy(std::uint32_t self);                                          // 0x00426210
    void confirm_highlight(std::uint32_t self, std::uint32_t id);                      // 0x004262a0
    void confirm_draw(std::uint32_t self);                                             // 0x004262c0
    void confirm_update(std::uint32_t self);                                           // 0x00426300
    std::uint32_t confirm_poll(std::uint32_t self, std::int32_t edge, std::int32_t pad);   // 0x00426310

    // ---- creedit.cpp ----
    void editor_screen_call(std::uint32_t unused, std::uint32_t result);               // 0x004271a0
    std::uint32_t editor_construct(std::uint32_t self, std::uint32_t page, std::uint32_t mode);   // 0x004271c0
    std::uint32_t editor_delete(std::uint32_t self, std::uint32_t flag);               // 0x004277e0
    void editor_destroy(std::uint32_t self);                                           // 0x00427800
    void editor_setup_scene(std::uint32_t self);                                       // 0x00427920
    void editor_load_fonts(std::uint32_t self);                                        // 0x00427be0
    void editor_set_page(std::uint32_t self, std::int32_t page);                       // 0x00427dc0
    void editor_update_frame(std::uint32_t self);                                      // 0x00427e40
    void editor_change_build(std::uint32_t self);                                      // 0x004280d0
    void editor_draw(std::uint32_t self, std::int32_t x, std::int32_t y);              // 0x004281e0
    std::uint32_t editor_update(std::uint32_t self);                                   // 0x004282d0
    void editor_show_buttons(std::uint32_t self, std::uint32_t hidden);                // 0x004283f0
    std::int32_t editor_run(std::uint32_t page, std::uint32_t mode);                  // 0x00428420
    void appearance_highlight_first(std::uint32_t page);                               // 0x0042b4c0 (appwin.cpp)
    std::uint32_t personal_construct(std::uint32_t self, std::int32_t x, std::int32_t y);   // 0x004288a0
    std::uint32_t personal_delete(std::uint32_t self, std::uint32_t flag);             // 0x00428c80
    void personal_build_list(std::uint32_t self);                                      // 0x00428ca0
    void personal_destroy(std::uint32_t self);                                         // 0x004292d0

    // ---- cresave.cpp ----
    void save_init_callbacks(std::uint32_t record);                                    // 0x0042be40
    void save_exit_callback();                                                         // 0x0042be60
    void save_screen_call(std::uint32_t unused, std::uint32_t result);                 // 0x0042be70
    std::uint32_t save_construct(std::uint32_t self);                                  // 0x0042be80
    std::uint32_t save_delete(std::uint32_t self, std::uint32_t flag);                 // 0x0042c6f0
    void save_destroy(std::uint32_t self);                                             // 0x0042c710
    void save_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                // 0x0042c860
    void save_update(std::uint32_t self);                                              // 0x0042c910
    std::uint32_t save_step(std::uint32_t self);                                       // 0x0042c940
    void save_draw_list(std::uint32_t self);                                           // 0x0042cb50
    void save_draw_box(std::uint32_t self);                                            // 0x0042cb60
    void save_mmu_thunk();                                                             // 0x0042cb70
    std::int32_t save_run();                                                           // 0x0042cb80

    // ---- cresel.cpp (Phase 4.10e2): the skater select / load / save screen ----
    void select_init_callbacks(std::uint32_t record);                                  // 0x0042cd20
    void select_exit_callback();                                                       // 0x0042cd40
    void select_screen_call(std::uint32_t unused, std::uint32_t result);               // 0x0042cd50
    std::uint32_t select_construct(std::uint32_t self);                                // 0x0042cd60
    std::uint32_t select_delete(std::uint32_t self, std::uint32_t flag);               // 0x0042dda0
    void select_destroy(std::uint32_t self);                                           // 0x0042ddc0
    void select_draw(std::uint32_t self, std::int32_t x, std::int32_t y);              // 0x0042df60
    void select_update_frame(std::uint32_t self);                                      // 0x0042e180
    std::uint32_t select_update_load(std::uint32_t self);                              // 0x0042e7d0
    std::uint32_t select_update_save(std::uint32_t self);                              // 0x0042e860
    std::uint32_t select_update(std::uint32_t self);                                   // 0x0042e9c0
    std::int32_t select_run();                                                         // 0x0042f100

    // ---- statedit.cpp and the stat window's constructor ----
    void stat_screen_call(std::uint32_t unused, std::uint32_t result);                 // 0x004b5ba0
    std::uint32_t stat_page_construct(std::uint32_t self, std::int32_t x, std::int32_t y);   // 0x004b5bb0
    std::uint32_t stat_page_delete(std::uint32_t self, std::uint32_t flag);            // 0x004b6060
    void stat_page_destroy(std::uint32_t self);                                        // 0x004b6080
    std::uint32_t stat_construct(std::uint32_t self);                                  // 0x004b7430
    std::uint32_t stat_delete(std::uint32_t self, std::uint32_t flag);                 // 0x004b77b0
    void stat_destroy(std::uint32_t self);                                             // 0x004b77d0
    void stat_load_fonts(std::uint32_t self);                                          // 0x004b7960
    void stat_setup_scene(std::uint32_t self);                                         // 0x004b7b50
    // The skater the stat editor and the trick screen show (the same code; the arguments are the addresses of the screen's animation region, model region
    // and skater object words and its light rig).
    void preview_scene(std::uint32_t anim_at, std::uint32_t model_at, std::uint32_t object_at, std::uint32_t lights);
    void stat_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                // 0x004b7f40
    void stat_update_skater(std::uint32_t self);                                       // 0x004b8020
    std::uint32_t stat_step(std::uint32_t self);                                       // 0x004b8080
    std::int32_t stat_run();                                                           // 0x004b8090

    // A virtual call on a screen's dialog or help box: a confirm box (vtable 0x00518784: 0 delete, 1 draw, 2 update) or a text box (0x0051877c: 0 delete,
    // 1 draw) runs its own method, anything else is reported as a service.
    void dialog_call(std::uint32_t dialog, std::uint32_t slot, std::vector<std::uint32_t> args = {});

private:
    // The select screen's stack buffer for a formatted line (create_screens_select.cpp)
    void select_write_line(const std::string& text);

    create::CreateSpool& spool_;
    SkaterWindowHelpers& helpers_;
};

}  // namespace opentony::runtime::frontend
