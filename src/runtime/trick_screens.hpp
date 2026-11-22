#pragma once

#include "create_screens.hpp"
#include "screen_kit.hpp"
#include "skater_window_helpers.hpp"
#include "trick_database.hpp"
#include "trick_menu.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10e2: the trick-list screen, from the disassembly: the screen window (tricksel.cpp: the preview skater, the name, the loop) around the trick
// menu (tricksel.cpp builds and draws it, trickwin.cpp fills its lists and handles the pad) and the cash / specials panel window (trickwin.cpp).
//
// What is native here and what is a seam: the window base, the menu toolkit, the font code, the skater manager and the confirm and text boxes of 4.10e1 run
// natively on the same memory; the trick-list decisions (which page a trick is on, sorting, binding, unbinding: trick_menu.hpp) are the 4.7b functions over a
// view of the trick manager's records. Everything else (the trick manager's constructor and unlock state, the career's purchase test and spending, the
// engine's model, spool, sprite and render calls) is reported as a service carrying the arguments retail passed.
//
// TrickScreen (0x258 bytes, vtable 0x00519830): a Window. +0x0ac the background sprite   +0x0b0 a Font object (s2font1)   +0x1fc the face sprite element
//   +0x200 the TrickMenu   +0x204 the animation region   +0x208 the model region   +0x20c the skater object (0x168 bytes)   +0x210 a view (width, height, x, y,
//   near, far, field of view: seven 16-bit words)   +0x224 the light rig.
// TrickMenu (0x2158 bytes, vtable 0x00519850): a Window. Six Font objects at +0x0ac (s2menu1), +0x1f8 (s2menu1), +0x344 (s2font3), +0x490 (s2font2), +0x5dc
//   (ltisymbol) and +0x728 (s2font1)   +0x874 the category list and +0x878 its 0x60 rows (the title, the four pages, "Continue")   +0x9f8 the combination list
//   and +0x9fc its 0x60 rows   +0xb7c the trick list and +0xb80 its 0x60 rows   +0xd00..+0xd14 six styles   +0xd18 the buy dialog   +0xd1c the warning text box
//   +0xd20 / +0xd24 the arrows   +0xd28 the panel   +0xd2c / +0xd30 the two header texts   +0xd34..+0xd40 four prompt windows (select, delete, accept, back)
//   +0xd44 the trick manager (0x140c bytes: vtable, 0x80 records of 0x28 bytes from +4, the count at +0x1404, the loaded TRICKS.BIN at +0x1408)
//   +0x2150 the state (0 categories, 1 combinations, 2 tricks, 3 buy dialog, 4 warning)   +0x2154 the character's 0x35-byte binding block.
// TrickPanel (0xdc bytes, vtable 0x00519870): a Window. +0x0ac the cash counter   +0x0b0 its label   +0x0b4 the "n of 5" text   +0x0b8 its label   +0x0bc the
//   glyph text   +0x0c0 its label   +0x0c4 / +0x0c8 / +0x0cc three timers   +0x0d0 / +0x0d4 / +0x0d8 three message counts (message 2 is shown while +0xd8 is set).
constexpr std::uint32_t kVtTrickScreen = 0x00519830;
constexpr std::uint32_t kVtTrickMenu = 0x00519850;
constexpr std::uint32_t kVtTrickPanel = 0x00519870;

class TrickScreens final : public ScreenKit {
public:
    TrickScreens(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters,
                 SkaterWindowHelpers& helpers, CreateScreens& screens)
        : ScreenKit(memory, services, fonts, window, menu, skaters), helpers_(helpers), screens_(screens) {}

    // ---- tricksel.cpp: the screen ----
    void screen_call(std::uint32_t unused, std::uint32_t result);                      // 0x004bf860
    std::uint32_t screen_construct(std::uint32_t self);                                // 0x004bf870
    std::uint32_t screen_delete(std::uint32_t self, std::uint32_t flag);               // 0x004bfa90
    void screen_destroy(std::uint32_t self);                                           // 0x004bfab0
    void screen_load_font(std::uint32_t self);                                         // 0x004bfc30
    void screen_setup_scene(std::uint32_t self);                                       // 0x004bfd30
    void screen_draw(std::uint32_t self, std::int32_t x, std::int32_t y);              // 0x004c0120
    void screen_update_frame(std::uint32_t self);                                      // 0x004c0200
    std::uint32_t screen_step(std::uint32_t self);                                     // 0x004c0230
    std::int32_t run();                                                                // 0x004c0240

    // ---- the trick menu (tricksel.cpp) ----
    std::uint32_t menu_construct(std::uint32_t self, std::int32_t x, std::int32_t y);  // 0x004c0430
    std::uint32_t menu_delete(std::uint32_t self, std::uint32_t flag);                 // 0x004c0e90
    void menu_destroy(std::uint32_t self);                                             // 0x004c0eb0
    void menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                // 0x004c1080
    void menu_update_frame(std::uint32_t self);                                        // 0x004c13c0
    void build_category_list(std::uint32_t self);                                      // 0x004c13d0

    // ---- trickwin.cpp: the lists, the pad and the panel ----
    void rebuild_combo_list(std::uint32_t self);                                       // 0x004c2000
    void build_page_list(std::uint32_t self);                                          // 0x004c21a0
    bool requirements_met(std::int32_t character, std::uint32_t flags);                // 0x004c2700
    std::uint32_t assigned_record(std::uint32_t self);                                 // 0x004c2800
    void sort_by_cost(std::uint32_t self, std::int32_t count);                         // 0x004c2890
    void highlight_first(std::uint32_t self);                                          // 0x004c29d0
    void build_special_combos(std::uint32_t self);                                     // 0x004c2a10
    void build_normal_combos(std::uint32_t self);                                      // 0x004c2c10
    std::uint32_t update_categories(std::uint32_t self);                               // 0x004c2ed0
    std::uint32_t update_combos(std::uint32_t self);                                   // 0x004c3050
    std::uint32_t category_mask(std::int32_t category);                                // 0x004c3350
    std::uint32_t update_tricks(std::uint32_t self);                                   // 0x004c33f0
    void bind_selected(std::uint32_t self, std::int32_t record);                       // 0x004c36d0
    void unbind_selected(std::uint32_t self);                                          // 0x004c38f0
    std::uint32_t update_buy_dialog(std::uint32_t self);                               // 0x004c3a80
    std::uint32_t update_warning(std::uint32_t self);                                  // 0x004c3ca0
    bool specials_full(std::uint32_t self);                                            // 0x004c3cf0
    std::uint32_t menu_update(std::uint32_t self);                                     // 0x004c3d30
    std::uint32_t panel_construct(std::uint32_t self, std::uint32_t font, std::uint32_t font2, std::uint32_t glyph_font, std::int32_t x, std::int32_t y);   // 0x004c3dd0
    std::uint32_t panel_delete(std::uint32_t self, std::uint32_t flag);                // 0x004c40b0
    void panel_destroy(std::uint32_t self);                                            // 0x004c40d0
    void panel_update_frame(std::uint32_t self);                                       // 0x004c40e0
    void panel_show_message(std::uint32_t self, std::int32_t index, std::uint32_t text);   // 0x004c4300
    // 0x004c1310: the button glyphs of a combination (a count and the button codes) written as text at `out`
    void button_glyphs(std::uint32_t combination, std::uint32_t out);

private:
    // The trick manager's records and the character's binding block, as the 4.7b model reads them
    TrickDatabase database_view(std::uint32_t manager);
    std::vector<std::uint8_t> read_block(std::uint32_t block);
    void report_assertions(const TrickDatabase& db, std::size_t from);
    void write_block(std::uint32_t block, const std::vector<std::uint8_t>& bytes);
    // TrickDatabase_GetRecord / _FindBySourceOffset / _FindBySecondaryKey (0x004bd170, 0x004bc300, 0x004bc330), over the manager's memory
    std::uint32_t record_at(std::uint32_t manager, std::int32_t index);
    std::uint32_t find_by_source(std::uint32_t manager, std::uint32_t source);
    std::int32_t index_by_source(std::uint32_t manager, std::uint32_t source);
    // The pad of the active player: an edge byte of its record
    [[nodiscard]] std::uint32_t pad_edge(std::uint32_t first_pad_address) const;
    std::uint32_t pad_edges_value(std::uint32_t third);
    // A text row's second column: a toolkit row sets it itself, any other object is reported
    void set_column(std::uint32_t row, const std::string& text, std::int32_t column, std::uint32_t offset, std::uint32_t props);
    void write_text(const std::string& text);
    // A line of formatted text in the screens' stack buffer
    std::uint32_t scratch_text(const std::string& text);
    std::uint32_t scratch();
    std::string done_text();
    // The character's binding block (the active character's record, or the special costume's)
    std::uint32_t current_block();
    std::uint32_t character_record(std::int32_t character) const;

    SkaterWindowHelpers& helpers_;
    CreateScreens& screens_;
};

}  // namespace opentony::runtime::frontend
