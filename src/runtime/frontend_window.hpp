#pragma once

#include "frontend_text.hpp"
#include "guest_view.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10a: the frontend's window base (screentools.cpp, 0x004ac430..0x004acf20), from the disassembly. Every
// frontend screen is built from these objects: a ScreenElement slides toward a target position; a Window holds up
// to 32 child elements and passes update, draw and "snap" down to them; a TextScreenElement draws a string with a
// Font; a SpriteScreenElement draws a sprite.
//
// ScreenElement (0x28 bytes):
//   +0x00 vtable   +0x04 word (0)   +0x08, +0x0c target x, y   +0x10 draw argument (passed to the sprite or font)
//   +0x14 hidden flag   +0x18 delay in frames before sliding starts   +0x1c arrived flag (byte)
//   +0x20, +0x24 current x, y
// Window: +0x28 child pointers (32) and +0xa8 child count.  TextScreenElement: +0x28 string (own copy), +0x2c font.
// SpriteScreenElement: +0x28 sprite object (0x5c bytes; the engine's).
//
// Virtual methods (the vtable slot is the method number times four):
//   0 deleting destructor (flag)  1 update  2 draw (x, y)  3 -1  4 nothing  5 snap x  6 snap y  7 per-frame hook
// A virtual call on an object the port does not own is reported as a service call carrying the object first.
constexpr std::uint32_t kElemPad = 0x04;
constexpr std::uint32_t kElemTargetX = 0x08;
constexpr std::uint32_t kElemTargetY = 0x0c;
constexpr std::uint32_t kElemArg = 0x10;
constexpr std::uint32_t kElemHidden = 0x14;
constexpr std::uint32_t kElemDelay = 0x18;
constexpr std::uint32_t kElemArrived = 0x1c;
constexpr std::uint32_t kElemX = 0x20;
constexpr std::uint32_t kElemY = 0x24;
constexpr std::uint32_t kWinChildren = 0x28;
constexpr std::uint32_t kWinCount = 0xa8;
constexpr std::uint32_t kWinSlots = 0x20;
constexpr std::uint32_t kTextString = 0x28;
constexpr std::uint32_t kTextFont = 0x2c;
constexpr std::uint32_t kSpriteObject = 0x28;
constexpr std::uint32_t kTextWinColours = 0x34;   // the text window's per-line colour bytes (two bits each)
constexpr std::uint32_t kTextWinLines = 0x50;     // and how many lines it has

constexpr std::uint32_t kVtRoot = 0x005184b4;          // the vtable of the root class (every destructor ends here)
constexpr std::uint32_t kVtElement = 0x005196a8;
constexpr std::uint32_t kVtWindow = 0x005196c8;
constexpr std::uint32_t kVtText = 0x005196e8;
constexpr std::uint32_t kVtSprite = 0x00519708;
constexpr std::uint32_t kEmptyString = 0x0055fe10;     // the string a text element is compared with (nothing to draw)

// Virtual method byte offsets
constexpr std::uint32_t kVmDelete = 0x00;
constexpr std::uint32_t kVmUpdate = 0x04;
constexpr std::uint32_t kVmDraw = 0x08;
constexpr std::uint32_t kVmSnapX = 0x14;
constexpr std::uint32_t kVmSnapY = 0x18;
constexpr std::uint32_t kVmHook = 0x1c;
constexpr std::uint32_t kFontVmDraw = 0x04;
constexpr std::uint32_t kSpriteVmDelete = 0x00;
constexpr std::uint32_t kSpriteVmDraw = 0x0c;

// Service names
constexpr const char* kVirtualDelete = "virtual_delete";        // (object, 1)
constexpr const char* kElementUpdate = "element_update";        // (child)
constexpr const char* kElementDraw = "element_draw";            // (child, x, y)
constexpr const char* kElementSnapX = "element_snap_x";         // (child)
constexpr const char* kElementSnapY = "element_snap_y";         // (child)
constexpr const char* kElementHook = "element_hook";            // (element)
constexpr const char* kFontDrawService = "font_draw";           // (font, x, y, string, argument); the string is the call's text
constexpr const char* kSpriteDrawService = "sprite_draw";       // (sprite, x, y, argument)
constexpr const char* kSpriteConstruct = "sprite_construct";    // (memory, file name, 1, 0x40, 0x40) -> sprite (scripted); file name is the text
constexpr const char* kSpriteInit = "sprite_init";              // (sprite)
constexpr const char* kSpriteWidth = "sprite_width";            // (sprite) -> value (scripted)
constexpr const char* kSpriteHeight = "sprite_height";          // (sprite) -> value (scripted)
constexpr const char* kOperatorNew = "operator_new";            // (size) -> address (scripted)
constexpr const char* kOperatorDelete = "operator_delete";      // (object)
constexpr const char* kAllocBuffer = "alloc";                   // FileIO_AllocLoadedBuffer(size, 0, 1, 0) -> address (scripted)
constexpr const char* kMemDeleteService = "mem_delete";         // Mem_Delete(block)

// Assertion messages
constexpr std::uint32_t kMsgNoElementToAdd = 0x005378a4;
constexpr std::uint32_t kMsgTooManyElements = 0x00537880;
constexpr std::uint32_t kMsgNoStringPointer = 0x005370a0;
constexpr std::uint32_t kMsgElementNoFont = 0x0052576c;
constexpr std::uint32_t kMsgNoString = 0x00537094;
constexpr std::uint32_t kMsgNoFilenamePointer = 0x005370b4;
constexpr std::uint32_t kMsgNoFilename = 0x005269a0;
constexpr std::uint32_t kMsgSpriteFailed = 0x005378e0;
constexpr std::uint32_t kMsgNoSprite = 0x00536290;
constexpr std::uint32_t kMsgBadLine = 0x00540bf4;
constexpr std::uint32_t kMsgBadLineColour = 0x00540c20;

class WindowBase final {
public:
    WindowBase(RetailMemory& memory, RetailServices& services, FontTools& fonts) : m_(memory), s_(services), f_(fonts) {}

    // Phase 4.10f: a virtual call on a child is reported as a service by default (the probes' children are fake objects). A dispatcher offered
    // here sees the call first (service name, object, arguments after it) and answers true when it ran the method itself (a child of a real
    // class the port owns); the per-frame hook is never offered.
    using Dispatch = std::function<bool(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args)>;
    void set_dispatch(Dispatch dispatch) { dispatch_ = std::move(dispatch); }

    // ---- ScreenElement ----
    // 0x004ac430: construct at (x, y); the element starts at (start_x, start_y), or at (x, y) for -1.
    std::uint32_t element_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);
    // 0x004ac4b0: the base destructor body (vtable = the root class).  0x004ac490: the deleting destructor (flag).
    void element_destroy(std::uint32_t self);
    std::uint32_t element_delete(std::uint32_t self, std::uint32_t flag);
    // 0x004ac4d0: one frame: the hook, then the slide toward the target (unless delayed).
    void element_update(std::uint32_t self);
    // 0x004ac570 / 0x004ac580 / 0x004ac590 / 0x004ac5a0.
    void element_snap_x(std::uint32_t self);
    void element_snap_y(std::uint32_t self);
    [[nodiscard]] std::uint32_t element_x(std::uint32_t self) const;
    [[nodiscard]] std::uint32_t element_y(std::uint32_t self) const;

    // ---- Window ----
    // 0x004ac5b0, 0x004ac610 (body), 0x004ac5f0 (deleting), 0x004ac670, 0x004ac730, 0x004ac770, 0x004ac7c0, 0x004ac800.
    std::uint32_t window_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);
    void window_destroy(std::uint32_t self);
    std::uint32_t window_delete(std::uint32_t self, std::uint32_t flag);
    void window_add(std::uint32_t self, std::uint32_t child);
    void window_update(std::uint32_t self);
    void window_draw(std::uint32_t self, std::int32_t x, std::int32_t y);
    void window_snap_x(std::uint32_t self);
    void window_snap_y(std::uint32_t self);

    // ---- TextScreenElement ----
    // 0x004ac840, 0x004ac990 (body), 0x004ac970 (deleting), 0x004aca30, 0x004acb50, 0x004acbd0.
    std::uint32_t text_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font, std::int32_t x, std::int32_t y,
                                 std::int32_t start_x, std::int32_t start_y);
    void text_destroy(std::uint32_t self);
    std::uint32_t text_delete(std::uint32_t self, std::uint32_t flag);
    void text_set_string(std::uint32_t self, std::uint32_t text);
    void text_set_string_from(std::uint32_t self, const std::string& text);
    void text_draw(std::uint32_t self, std::int32_t x, std::int32_t y);
    [[nodiscard]] std::int32_t text_width(std::uint32_t self) const;

    // ---- SpriteScreenElement ----
    // 0x004acbe0, 0x004acd80 (body), 0x004acd60 (deleting), 0x004ace20, 0x004ace60 / 0x004acec0 (the sprite's size).
    std::uint32_t sprite_construct(std::uint32_t self, std::uint32_t file, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                   std::int32_t start_y, std::uint32_t b0, std::uint32_t b1);
    void sprite_destroy(std::uint32_t self);
    std::uint32_t sprite_delete(std::uint32_t self, std::uint32_t flag);
    void sprite_draw(std::uint32_t self, std::int32_t x, std::int32_t y);
    std::uint32_t sprite_width(std::uint32_t self);
    std::uint32_t sprite_height(std::uint32_t self);

    // ---- TextWindow (textwin.cpp) ----
    // 0x004bad00: set the colour (0..3) of one line of a text window.
    void textwin_set_line_colour(std::uint32_t self, std::int32_t line, std::int32_t colour);

private:
    // A method of the object's class, by vtable slot. A class the toolkit routes (`dispatch_`) is run natively. Any other class is reported by name (a replay's service) or,
    // with a host attached, as the call of the address the object's vtable holds in the slot (`vcall_<address>`, a thiscall with the object in ECX), which the host
    // resolves to the native or the retail function there.
    void virtual_call(const char* name, std::uint32_t slot, std::uint32_t object, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        if (dispatch_ && dispatch_(name, object, args)) {
            return;
        }
        std::vector<std::uint32_t> all{object};
        all.insert(all.end(), args.begin(), args.end());
        if (s_.live()) {
            char target[24];
            std::snprintf(target, sizeof target, "vcall_%08x", guest::read_scalar<std::uint32_t>(m_, guest::read_scalar<std::uint32_t>(m_, object) + 4 * slot));
            s_.note(target, std::move(all), std::move(text));
            s_.rename_pending(name);
            return;
        }
        s_.note(name, std::move(all), std::move(text));
    }
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    [[nodiscard]] std::uint32_t copy_string(std::uint32_t text);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    Dispatch dispatch_;
};

}  // namespace opentony::runtime::frontend
