#pragma once

#include "screen_kit.hpp"
#include "guest_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10i2: the small classes and helpers the shell and the screens lean on that no screen file owns: the pre-load progress screen's object (frontpre.cpp: "PIE"), the menu
// pad poll, the old menu base class, the panel's line drawing (the balance meters), the text window (textwin.cpp) and a handful of one-liners. Like the other screens they
// are a ScreenKit: the engine calls they make are seams reported by their retail address.
//
// Draw packets: a primitive is written at the draw list's cursor (0x00560fd0, with its end at 0x00568628), its address is pushed on the head of the list the renderer keeps
// (the renderer at 0x00560fd4: +0x84 points at the head of the main list, +0x88 at the viewport whose +8 is the head of the viewport list).
namespace shellkit {

constexpr std::uint32_t kVtPie = 0x00518a04;
constexpr std::uint32_t kVtBase = 0x005184b4;           // the base class every screen object ends its destruction with
constexpr std::uint32_t kVtTextWindow = 0x00519824;
constexpr std::uint32_t kTextWindows = 0x0056db70;      // head of the list of text windows (linked through +0x48 / +0x4c)
constexpr std::uint32_t kSpooler = 0x0056db4c;
constexpr std::uint32_t kArenaCursor = 0x00560fd0;
constexpr std::uint32_t kArenaEnd = 0x00568628;
constexpr std::uint32_t kRenderer = 0x00560fd4;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;

}  // namespace shellkit

class ShellHelpers final : public ScreenKit {
public:
    ShellHelpers(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the pre-load progress screen (frontpre.cpp), a 0x34-byte object: +4 the name of the file, +0x24 the spooler handle, +0x28 frames to wait, +0x2c the fade, +0x30 loading ----
    std::uint32_t pie_construct(std::uint32_t self);                                                       // 0x00454930
    std::uint32_t pie_delete(std::uint32_t self, std::uint32_t flag);                                      // 0x00454950
    void pie_destruct(std::uint32_t self);                                                                 // 0x00454970
    bool pie_load(std::uint32_t self, std::uint32_t id);                                                   // 0x00454980
    bool pie_update(std::uint32_t self);                                                                   // 0x00454a60
    void clear_slide_table();                                                                              // 0x00454c80 (a jump to 0x00454c90) and 0x00454c90
    void iterate_vector(std::uint32_t base, std::uint32_t stride, std::uint32_t count, std::uint32_t function);   // 0x00454cb0
    void play_game_fmv(std::uint32_t track);                                                               // 0x00454d10

    // ---- the menu pad poll (menu.cpp) ----
    std::int32_t menu_pad_poll(std::int32_t pad, std::uint32_t flag, std::uint32_t repeat);                // 0x0046f550
    std::int32_t menu_pad_poll_any();                                                                      // 0x0046f650 (answers the first poll that was not -1)

    // ---- the old menu base class and the class that shares its destructor chain ----
    std::uint32_t base_construct_f80(std::uint32_t self);                                                  // 0x00473c00
    std::uint32_t base_delete(std::uint32_t self, std::uint32_t flag);                                     // 0x00473c20
    void base_destruct(std::uint32_t self);                                                                // 0x00473c40

    // ---- panel.cpp: the balance meters ----
    void panel_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t color);   // 0x0048ab20
    void panel_balance_lr(std::int32_t value, std::int32_t maximum, std::int32_t half_width, std::int32_t center, std::int32_t y, std::uint32_t color, std::uint32_t marker);   // 0x0048abf0
    void panel_balance_ud(std::int32_t value, std::int32_t maximum, std::int32_t half_height, std::int32_t x, std::int32_t center, std::uint32_t color, std::uint32_t marker);   // 0x0048ac60

    // ---- rfront.cpp and screentools.cpp ----
    std::int32_t pad_repeat_direction();                                                                   // 0x004ac370
    void line_unguarded(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t color);   // 0x004acf20

    // ---- one-liners ----
    std::uint32_t credits_alloc_zeroed(std::uint32_t size);                                                // 0x004266a0
    void credits_free(std::uint32_t block);                                                                // 0x004266d0
    std::uint32_t font_string_width(std::uint32_t font, std::uint32_t text);                               // 0x0044aa70
    void set_handicap_names();                                                                             // 0x00452e10 (0x00452e00 jumps to it)
    void shell_utils_tail();                                                                               // 0x004b0b20

    // ---- shellutils.cpp: the 8-bit BMP loader and the 2D screen primitives (Phase 4.10i3) ----
    // The loader reads a BMP held in memory (width at +0x12, height at +0x16, depth at +0x1c, colour count at +0x2e, the palette from +0x36 as four-byte BGR entries, the rows after it,
    // bottom-up and padded to four bytes). Its palette locals live on the stack in retail: the entries past the colour count are whatever the stack held, so the text of the palette
    // upload blanks them (`palette_gap`).
    std::uint32_t load_8bit_bmp(std::uint32_t bmp, std::uint32_t out_buffer, std::uint32_t out_stride, std::uint32_t out_height, std::uint32_t out_clut);   // 0x004afd40
    std::uint32_t load_8bit_bmp_file(std::uint32_t name, std::uint32_t out_buffer, std::uint32_t out_stride, std::uint32_t out_height, std::uint32_t out_clut);   // 0x004affe0
    void bmp8_upload_tall(std::uint32_t bmp);                                                              // 0x004b0060
    void bmp8_upload(std::uint32_t bmp);                                                                   // 0x004b0320
    void clut_set_semi_transparent(std::uint32_t x, std::uint32_t y);                                      // 0x004b0580
    void screen_fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t r, std::uint32_t g, std::uint32_t b);   // 0x004b0600
    void screen_set_draw_area(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);        // 0x004b0800
    void screen_reset_primitives();                                                                        // 0x004b0950
    void screen_draw_textured_rect(std::uint32_t rect, std::uint32_t x, std::uint32_t y, std::uint32_t depth, std::uint32_t clut);   // 0x004b0970

    // ---- the effect object of tapes.cpp (vtable 0x0051979c; +0xc8 a child object, +0x70 a velocity, +0x14 a position, +0x114 the anchor, +0x120 the wobble angle) ----
    std::uint32_t effect_delete(std::uint32_t self, std::uint32_t flag);                                   // 0x004b9320
    void effect_destruct(std::uint32_t self);                                                              // 0x004b9340
    void effect_kill(std::uint32_t self);                                                                  // 0x004b93a0
    void effect_set_extra(std::uint32_t self, std::uint32_t value, std::uint32_t flag);                    // 0x004b93b0
    void set_effects_busy() { guest::write_scalar<std::uint32_t>(m_, 0x005244cc, 1); }                                                   // 0x004b93d0
    void effect_free_child(std::uint32_t self);                                                            // 0x004b93e0
    void effect_set_frames(std::uint32_t self, std::uint32_t frames);                                      // 0x004b9400
    void effect_move(std::uint32_t self);                                                                  // 0x004b9420

    // ---- the object record of the model tables (vtable 0x00519388) ----
    std::uint32_t object_record_init(std::uint32_t self);                                                  // 0x0047fdb0 (answers this)
    std::uint32_t object_record_delete(std::uint32_t self, std::uint32_t flag);                            // 0x0047fe00
    void object_record_destruct(std::uint32_t self) { guest::write_scalar<std::uint32_t>(m_, self, 0x00519388); }                        // 0x0047fe20

    // ---- the text window (textwin.cpp), a 0x54-byte object ----
    std::uint32_t textwin_construct(std::uint32_t self, std::uint32_t owner, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);   // 0x004ba7a0
    std::uint32_t textwin_delete(std::uint32_t self, std::uint32_t flag);                                  // 0x004ba8a0
    void textwin_destruct(std::uint32_t self);                                                             // 0x004ba8c0
    void textwin_set_rect(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);   // 0x004ba940
    void textwin_set_colors(std::uint32_t self, const std::uint32_t (&c)[15]);                             // 0x004baa50
    void textwin_set_owner(std::uint32_t self, std::uint32_t owner);                                       // 0x004baac0
    void textwin_clear(std::uint32_t self);                                                                // 0x004baad0
    void textwin_set_line(std::uint32_t self, std::int32_t line, std::uint32_t text);                      // 0x004bab00
    void textwin_push_line(std::uint32_t self, std::uint32_t text);                                        // 0x004bab70
    void textwin_insert_line(std::uint32_t self, std::uint32_t text);                                      // 0x004babc0
    void textwin_set_line_marked(std::uint32_t self, std::int32_t line, std::uint32_t text);               // 0x004bac00
    void textwin_push_line_marked(std::uint32_t self, std::uint32_t text);                                 // 0x004bac70
    void textwin_insert_line_marked(std::uint32_t self, std::uint32_t text);                               // 0x004bacc0
    std::uint32_t textwin_last();                                                                          // 0x004badd0
    void textwin_draw_all();                                                                               // 0x004badf0
    void xor_decrypt(std::uint32_t data, std::uint32_t size);                                              // 0x004bae70

private:
    // a draw packet at the cursor, linked on the head of the main list (the line packets use this)
    void link_main(std::uint32_t packet);
    // a 2D primitive linked into the next ordering-table slot (0x00537b3c counts down from 0xfff); its depth float goes at `depth_offset`
    void link_overlay(std::uint32_t packet, std::uint32_t depth_offset);
    struct BmpPalette {
        std::uint16_t colour[256] = {};
        std::uint32_t count = 0;
        std::uint32_t transparent = 0;      // the index of the last pure-magenta entry (0 when there is none)
    };
    BmpPalette bmp_palette(std::uint32_t bmp);
    // the BMP rows, bottom-up, into the buffer from its row `first` upwards, `copies` times each (padding bytes get the transparent index)
    void bmp_rows(std::uint32_t source, std::uint32_t first, std::uint32_t width, std::uint32_t pad, std::uint32_t height, std::uint32_t fill, std::uint32_t copies);
    void bmp_expand(std::uint32_t buffer, std::uint32_t count, const BmpPalette& palette);
    static std::string rect_text(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
    static std::uint32_t scale_x(std::int32_t v, std::uint32_t width) { return static_cast<std::uint32_t>((static_cast<std::int32_t>(v) * width) >> 9) & 0xffff; }
};

}  // namespace opentony::runtime::frontend
