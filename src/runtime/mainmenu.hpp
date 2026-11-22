#pragma once

#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10c: the title ("main menu") screen and its memory-card scan screen (mainmenu.cpp, 0x0046aee0..0x0046c460), from the
// disassembly.
//
// MainMenuWindow (0x368 bytes, vtable 0x00518d78, a Window): +0xac and +0x20c two embedded Font objects, +0x1f8 state (1 while the
//   memory-card scan screen is up), +0x200 / +0x360 / +0x364 three prompt windows, +0x204 the background sprite object, +0x208 the
//   wheel, +0x35c the memory-card window.
// WheelWindow (0xd0 bytes, vtable 0x00518d98): +0xac the wheel sprite, +0xb0 the current and +0xb4 the target angle (4096 per turn),
//   +0xb8 the number of entries (7), +0xbc the nearest entry, +0xc0 a flag, +0xc4 / +0xc8 two palette buffers of 0x200 bytes,
//   +0xcc the selected entry (the byte at 0x00533e98 remembers it between visits). The entries are rows of three words at 0x00534504:
//   the angle, then what pad 0 and pad 1 choose.
// CardWindow (0xd60 bytes, vtable 0x00518db8, a Window): +0xb0 an embedded Font, +0x1fc a text effect object (the engine's), +0xd48 the
//   state the effect reports, +0xd4c the screen's own state (9 reading, 0xb done), +0xd50 a text box, +0xd54 the "finished" result,
//   +0xd58 a countdown, +0xd5c a timeout.
//
// Virtual calls on the card window are dispatched natively when its vtable is the real one, else reported as services.
constexpr std::uint32_t kMainVtWindow = 0x00518d78;
constexpr std::uint32_t kMainVtWheel = 0x00518d98;
constexpr std::uint32_t kMainVtCard = 0x00518db8;
constexpr std::uint32_t kWheelTable = 0x00534504;       // rows of three words
constexpr std::uint32_t kWheelSelectionMemory = 0x00533e98;
constexpr std::uint32_t kCardScreenFlag = 0x0052cb3c;
constexpr std::uint32_t kMainMsgNoScreen = 0x00534590;
constexpr std::uint32_t kMainMsgCardNotIdle = 0x005345f0;
constexpr std::uint32_t kMainMsgBadCardState = 0x005345d8;

class MainMenu final {
public:
    MainMenu(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu)
        : m_(memory), s_(services), f_(fonts), w_(window), menu_(menu) {}

    // A virtual call the window base makes on a child of this screen: the wheel (a child of the real class) runs its own methods, anything else is for the caller to report
    // (offered to WindowBase::set_dispatch; the per-frame hook is never offered).
    bool dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args);

    // ---- the title window ----
    std::uint32_t window_construct(std::uint32_t self);                             // 0x0046b0a0
    std::uint32_t window_delete(std::uint32_t self, std::uint32_t flag);            // 0x0046b4f0
    void window_destroy(std::uint32_t self);                                        // 0x0046b510
    void window_draw(std::uint32_t self);                                           // 0x0046b5d0
    void window_update(std::uint32_t self);                                         // 0x0046b690
    std::uint32_t window_poll(std::uint32_t self, std::uint32_t result_pointer);    // 0x0046b720
    std::uint32_t poll_to(std::uint32_t self, const std::function<void(std::uint32_t)>& store);
    // 0x0046aee0 / 0x0046aef0: the screen's function and the wrapper that stores its result.
    void run_wrapper(std::uint32_t result_pointer);                                 // 0x0046aee0
    std::uint32_t run();                                                            // 0x0046aef0

    // ---- the wheel ----
    std::uint32_t wheel_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x0046b870
    std::uint32_t wheel_delete(std::uint32_t self, std::uint32_t flag);             // 0x0046b9a0
    void wheel_destroy(std::uint32_t self);                                         // 0x0046b9c0
    void wheel_update(std::uint32_t self);                                          // 0x0046ba50
    void wheel_draw(std::uint32_t self, std::int32_t x, std::int32_t y);            // 0x0046bce0
    void wheel_palette_alloc(std::uint32_t self);                                   // 0x0046bd20
    void wheel_tint(std::uint32_t self);                                            // 0x0046bd70

    // ---- the memory-card screen ----
    void card_register(std::uint32_t descriptor);                                   // 0x0046be60
    void card_nothing();                                                            // 0x0046be80
    void card_run_wrapper(std::uint32_t result_pointer);                            // 0x0046be90
    std::uint32_t card_construct(std::uint32_t self);                               // 0x0046bea0
    std::uint32_t card_delete(std::uint32_t self, std::uint32_t flag);              // 0x0046c080
    void card_destroy(std::uint32_t self);                                          // 0x0046c0a0
    void card_update(std::uint32_t self);                                           // 0x0046c120
    void card_set_text(std::uint32_t box, std::uint32_t text_address, const std::string& text);   // Text_Layout(box, text): the text by address when live
    void card_draw(std::uint32_t self, std::int32_t x, std::int32_t y);             // 0x0046c3d0
    [[nodiscard]] std::uint32_t card_result(std::uint32_t self) const;              // 0x0046c450
    std::uint32_t card_run();                                                       // 0x0046c460

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    // The per-frame engine calls the screen loops make: Game_FrameTick answers the new simulation clock and the input poll the six pad edge bytes (the replay scripts both as
    // writes; the bytes the loops wait on)
    void frame_tick();
    void poll_input();
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}) { s_.note(engine_name(address), std::move(args)); }
    std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {}) {
        engine(address, std::move(args));
        return static_cast<std::uint32_t>(s_.value(engine_name(address)));
    }
    void sfx(std::uint32_t sound) { s_.note(kSvcSfx, {sound, 0x2000, 0}); }
    void vcall_window(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});
    std::uint32_t operator_new(std::uint32_t size) {
        s_.note(kOperatorNew, {size});
        return static_cast<std::uint32_t>(s_.value(kOperatorNew));
    }
    std::uint32_t prompt(std::uint32_t slab_window, std::uint32_t file, std::uint32_t font, std::uint32_t x);
    // The Font object 0x00449ca0 builds, copied field by field from a loaded font (the same copy every menu constructor makes).
    void copy_font(std::uint32_t dest, std::uint32_t source) { menu_.copy_font_fields(dest, source); }

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    MenuToolkit& menu_;
};

}  // namespace opentony::runtime::frontend
