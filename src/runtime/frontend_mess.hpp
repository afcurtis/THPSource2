#pragma once

#include "frontend_text.hpp"
#include "host_options.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>

namespace opentony::runtime::frontend {

// Phase 4.10a: the on-screen message layer (mess.cpp, 0x00473480..0x00473c60), from the disassembly.
//
// One global font object (0x0056a9a0, the Font layout of frontend_text.hpp) is the "mess font": every message and
// all HUD text draws through it. The code around it keeps a doubly linked list of messages, each a 0x1c-byte node
// that follows a small program (a table of 0x20-byte steps) which interpolates the scale, colour and position over
// its duration:
//   node: +0 text pointer  +4 age (u16)  +6 total age (u16)  +8 x offset (s16)  +0xa y offset (s16)
//         +0xc stacked flag (byte)  +0x10 program pointer  +0x14 next  +0x18 previous
//   step: +0 duration (u16)  +3 justify (byte)  +4 / +6 scale at start / end (u16)  +8..+0xa / +0xb..+0xd colour at
//         start / end (bytes)  +0xe / +0x10 (u16, unused by the drawing)  +0x12 +0x14 / +0x16 +0x18 position at
//         start / end (s16)  +0x1a flash period (byte)  +0x1c rainbow flag (u16)  +0x1e next step (signed byte,
//         in steps; 0 deletes the message, 0x7f holds the last step for ever)
constexpr std::uint32_t kMessFont = 0x0056a9a0;
constexpr std::uint32_t kMessFontName = 0x0056aaec;
constexpr std::uint32_t kMessFontLoaded = 0x0056ab0c;
constexpr std::uint32_t kMessList = 0x0056ab10;
constexpr std::uint32_t kMessScale16 = 0x0056ab14;
constexpr std::uint32_t kMessSort = 0x0056ab16;
constexpr std::uint32_t kSimulationTime = 0x0056e320;
constexpr std::uint32_t kMessArenaCursor = 0x00560fd0;   // RenderPolygonArenaCursor: where the next packet is written
constexpr std::uint32_t kMessViewportMode = 0x0056a928;  // 0 full screen, 1 split side by side, 2 one above the other

constexpr std::uint32_t kNodeSize = 0x1c;
constexpr std::uint32_t kNodeText = 0x00;
constexpr std::uint32_t kNodeAge = 0x04;
constexpr std::uint32_t kNodeTotalAge = 0x06;
constexpr std::uint32_t kNodeX = 0x08;
constexpr std::uint32_t kNodeY = 0x0a;
constexpr std::uint32_t kNodeStacked = 0x0c;
constexpr std::uint32_t kNodeProgram = 0x10;
constexpr std::uint32_t kNodeNext = 0x14;
constexpr std::uint32_t kNodePrev = 0x18;

constexpr std::uint32_t kStepSize = 0x20;

constexpr const char* kAllocService = "alloc";            // FileIO_AllocLoadedBuffer(size, 0, 1, 0) -> address (scripted)
constexpr const char* kFontLoad = "font_load";            // Font_Load(name) -> Font object (scripted); the name is the call's text
constexpr const char* kFontUnload = "font_unload";        // Font_Unload(name pointer); the name is the call's text
constexpr const char* kPrintfLog = "printf_log";          // printf_log(format)

constexpr std::uint32_t kMsgSort = 0x00535010;
constexpr std::uint32_t kMsgNullMessage = 0x00535040;
constexpr std::uint32_t kMsgZeroDuration = 0x00535060;
constexpr std::uint32_t kMsgOneFont = 0x00535084;
constexpr std::uint32_t kMsgNoMessFont = 0x005350c0;
constexpr std::uint32_t kMsgDeprecated = 0x005350d4;

class Mess final {
public:
    Mess(RetailMemory& memory, RetailServices& services, FontTools& fonts) : m_(memory), s_(services), f_(fonts) {}

    // 0x00473510: choose the sort mode (0 or 1; stored in the word after the scale).
    void set_sort_mode(std::int32_t mode);
    // 0x004734b0, 0x004734f0, 0x00473500, 0x00473580.
    void set_text_justify(std::uint32_t justify);
    void set_scale(std::int32_t scale);
    [[nodiscard]] std::int32_t get_scale() const;
    void set_rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b);
    // 0x004739e0 (and 0x004735b0, which ignores its fourth argument): draw a string with the mess font.
    std::uint32_t draw_text(std::int32_t x, std::int32_t y, std::uint32_t text);
    // 0x004739d0: the width of a string in the mess font.
    [[nodiscard]] std::int32_t text_width(std::uint32_t text) const;
    // 0x004735d0, 0x00473650, 0x00473690, 0x004736c0: remove a message / all messages, is a node in the list, remove
    // the message that shows a given text.
    void delete_message(std::uint32_t node);
    void delete_all();
    [[nodiscard]] bool exists(std::uint32_t node) const;
    void delete_by_text(std::uint32_t text);
    // 0x00473730: a fresh node at the head of the list; 0x004736f0: start a message.
    std::uint32_t alloc_node();
    std::uint32_t add_message(std::uint32_t text, std::uint32_t program, std::uint32_t stacked, std::uint32_t x, std::uint32_t y);
    // 0x00473760: one tick of every message; 0x004737c0: draw them.
    void update();
    void display();
    // 0x00473a00: load the mess font (copies the Font object's fields into the global one); 0x00473b80: unload it.
    void load_font(std::uint32_t name, std::uint32_t spacing, std::uint32_t shadow_offset);
    void unload_font();
    // 0x00473be0, 0x00473bf0: the retired Mess_SetCurrentFont, which only prints a notice.
    void deprecated_notice();

private:
    [[nodiscard]] const HostOptions::HudMessage* hud_message(std::uint32_t text) const;

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
};

}  // namespace opentony::runtime::frontend
