#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace opentony::runtime {

// Phase 4.9: two small pieces of create-a-skater that need nothing from the engine but memory.
//
// The bitmap spool queue (create_spool.cpp): the editor asks for texture bitmaps to be loaded a few
// at a time. The queue object keeps 0x40 entries of 0x2c bytes from +8:
//   +0x00 key (the texture checksum)   +0x04 bitmap name (0x14 bytes)   +0x18 editor slot
//   +0x1c free flag (nonzero = the entry is free)   +0x20, +0x24, +0x28 three request words
//
// The help-text layout (create_ui.cpp): a text box breaks a message into lines of at most 500
// pixels. The box object keeps its font at +0x10, eight 0x80-byte lines from +0x14 and the line
// count at +0x414. A `&` in the message forces a line break. Widths come from the font, which is
// the engine's: the layout reports each candidate line and is told its width.
namespace create {

constexpr std::uint32_t kSpoolEntries = 8;
constexpr std::uint32_t kSpoolStride = 0x2c;
constexpr std::uint32_t kSpoolCount = 0x40;
constexpr std::uint32_t kSpoolName = 4;
constexpr std::uint32_t kSpoolSlot = 0x18;
constexpr std::uint32_t kSpoolFree = 0x1c;
constexpr std::uint32_t kSpoolRequestA = 0x20;
constexpr std::uint32_t kSpoolRequestB = 0x24;
constexpr std::uint32_t kSpoolRequestC = 0x28;

constexpr std::uint32_t kTextLines = 0x14;
constexpr std::uint32_t kTextLineStride = 0x80;
constexpr std::uint32_t kTextLineCount = 0x414;
constexpr std::int32_t kTextMaxWidth = 0x1f5;
constexpr std::int32_t kTextMaxLines = 7;

constexpr const char* kTextWidth = "text_width";   // Font width of a line (scripted); the line is the call's text

constexpr std::uint32_t kMsgTooManySpoolerEntries = 0x00526754;
constexpr std::uint32_t kMsgTooManyTextLines = 0x00526a50;
constexpr std::uint32_t kMsgNoSource = 0x00526a70;

class CreateSpool final {
public:
    CreateSpool(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // 0x00424870: pack the entries in use (free flag clear) to the front and mark the rest free.
    void compact(std::uint32_t queue);
    // 0x004248e0: compact, then the index of the first free entry (-1 if none).
    [[nodiscard]] std::int32_t find_free(std::uint32_t queue);
    // 0x00424780: queue a request, or update the name of the matching request already queued.
    void add(std::uint32_t queue, std::uint32_t key, std::uint32_t name, std::int32_t slot, std::uint32_t a, std::uint32_t b,
             std::uint32_t c);

    // 0x00425f20: copy the next word of `src` (up to a space, a `&` or the end) into `dst`;
    // returns where the next word starts.
    [[nodiscard]] std::uint32_t next_word(std::uint32_t src, std::uint32_t dst);
    // 0x00425ca0: break `text` into the box's lines.
    void layout(std::uint32_t box, std::uint32_t text);
    // The same with the width of each candidate line measured by `width` (Phase 4.10e: the screens measure with the font code, which is native too)
    // instead of asked of the "text_width" service.
    void layout(std::uint32_t box, std::uint32_t text, const std::function<std::int32_t(const std::string&)>& width);

private:
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace create
}  // namespace opentony::runtime
