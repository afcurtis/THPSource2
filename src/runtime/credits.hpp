#pragma once

#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10c: the credits roll (credits.cpp, 0x00426430..0x004270f0), from the disassembly.
//
// Credits (the manager, 0xff4 bytes; the original keeps it in the stack frame of Credits_Run): +0 the scroll position (fixed point, four
//   bits to a pixel row... the draw divides it by four), +4 up to 1000 record pointers, +0xfa4 how many, +0xfa8 the total height,
//   +0xfac the tick of the last advance, +0xfb0 the line being parsed (0x31 characters), +0xfe4 its record type (0 text, 1 bitmap, 2 music
//   marker), +0xfe8 its font, +0xfec / +0xff0 the two numbers of a music marker.
// Records (vtable at +0, +4 the offset in the roll, +8 the height): text 0x00518790 (+0xc the text, +0x40 the font), bitmap 0x00518798
//   (+0xc the sprite), music marker 0x005187a0 (+0xc / +0x10 the track numbers, +0x14 the state).
// The text file's lines start with tags: "@B" (a bitmap: the rest of the line is its file), "@F" (the second font), "@Mnn" (a music
// marker with two track digits); then the text; '#' ends the file.
constexpr std::uint32_t kCreditsVtText = 0x00518790;
constexpr std::uint32_t kCreditsVtBitmap = 0x00518798;
constexpr std::uint32_t kCreditsVtMusic = 0x005187a0;
constexpr std::uint32_t kCreditsFontA = 0x00560d08;
constexpr std::uint32_t kCreditsFontB = 0x00560e58;
constexpr std::uint32_t kCreditsFlags = 0x00560fa4;      // bit 0, bit 1: the static fonts have been destroyed
constexpr std::uint32_t kCreditsMusicPlaying = 0x00560fa5;
constexpr std::uint32_t kCreditsMsgBadTag = 0x00526b60;
constexpr std::uint32_t kCreditsMsgNoFile = 0x00526b4c;
constexpr std::uint32_t kCreditsMsgNoMemory = 0x00526b30;
constexpr std::uint32_t kCreditsMsgBadParameter = 0x00526a90;

class Credits final {
public:
    Credits(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu)
        : m_(memory), s_(services), f_(fonts), w_(window), menu_(menu) {}

    // ---- the manager ----
    std::uint32_t load_text(std::uint32_t self, std::uint32_t file);                // 0x004266e0
    void unload(std::uint32_t self);                                                // 0x00426b10
    bool advance_timeline(std::uint32_t self);                                      // 0x00426b50
    void update_records(std::uint32_t self);                                        // 0x00426bc0
    std::int32_t parse_line(std::uint32_t self, std::uint32_t text);                // 0x00426c00
    std::uint32_t parse_tag(std::uint32_t self, std::uint32_t text);                // 0x00426c80
    void run(std::uint32_t mode_pointer);                                           // 0x00426430

    // ---- the records ----
    void record_destroy(std::uint32_t self);                                        // 0x00426d90
    std::uint32_t record_set_offset(std::uint32_t self, std::uint32_t offset);      // 0x00426da0
    void record_nothing();                                                          // 0x00426db0
    std::uint32_t text_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font);   // 0x00426e60
    std::uint32_t text_delete(std::uint32_t self, std::uint32_t flag);              // 0x00426ee0
    void text_destroy(std::uint32_t self);                                          // 0x00426f00
    void text_update(std::uint32_t self, std::int32_t position);                    // 0x00426f10
    std::uint32_t bitmap_construct(std::uint32_t self, std::uint32_t file);         // 0x00426f50
    std::uint32_t bitmap_delete(std::uint32_t self, std::uint32_t flag);            // 0x00426fe0
    void bitmap_destroy(std::uint32_t self);                                        // 0x00427000
    void bitmap_update(std::uint32_t self, std::int32_t position);                  // 0x00427050
    std::uint32_t music_construct(std::uint32_t self, std::uint32_t track, std::uint32_t second);   // 0x00427080
    std::uint32_t music_delete(std::uint32_t self, std::uint32_t flag);             // 0x004270b0
    void music_base_destroy(std::uint32_t self);                                    // 0x004270d0
    void music_update(std::uint32_t self, std::int32_t position);                   // 0x004270e0

    // ---- the static fonts ----
    void statics_a_init();                                                          // 0x00426dc0
    void statics_a_construct();                                                     // 0x00426dd0
    void statics_a_atexit();                                                        // 0x00426de0
    void statics_a_destroy();                                                       // 0x00426df0
    void statics_b_init();                                                          // 0x00426e10
    void statics_b_construct();                                                     // 0x00426e20
    void statics_b_atexit();                                                        // 0x00426e30
    void statics_b_destroy();                                                       // 0x00426e40

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {});
    std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {});
    // The per-frame calls of the roll: Game_FrameTick answers the new clock, the input poll the pad's three edge bytes the roll ends on (scripted writes in a replay)
    void frame_tick();
    void poll_input();
    void vcall_record(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});
    std::uint32_t operator_new(std::uint32_t size) {
        s_.note(kOperatorNew, {size});
        return static_cast<std::uint32_t>(s_.value(kOperatorNew));
    }
    void copy_font(std::uint32_t dest, std::uint32_t source) { menu_.copy_font_fields(dest, source); }
    void load_font_into(std::uint32_t dest, std::uint32_t name);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    MenuToolkit& menu_;
};

}  // namespace opentony::runtime::frontend
