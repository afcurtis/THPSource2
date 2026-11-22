#include "auto_views.hpp"
#include "mainmenu.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kPadBase = 0x0056b029;
// the bytes the input poll rewrites each frame: the confirm and the other-button edges of both pads and the back edge of both
constexpr std::uint32_t kPadEdges[6] = {0x0056b029, 0x0056b0d9, 0x0056aff9, 0x0056b195, 0x0056b245, 0x0056b165};


}  // namespace

void MainMenu::frame_tick() {
    engine(seam::hal::Game_FrameTick);
    set_global_word(m_, 0x0056e31c, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::Game_FrameTick))));
}

void MainMenu::poll_input() {
    engine(0x004f5ff0);
    if (s_.live()) {
        return;   // the call has rewritten the edges itself (milestone 9: the cold boot runs the main menu live); a replay scripts them
    }
    for (const std::uint32_t edge : kPadEdges) {
        set_byte_at(m_, edge, static_cast<std::uint32_t>(s_.value(engine_name(0x004f5ff0))));
    }
}

// Virtual calls on the windows of this screen: the real class runs natively, anything else is reported.
void MainMenu::vcall_window(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    if (vt == kMainVtCard) {
        if (slot == 0) {
            card_delete(object, a0);
            return;
        }
        if (slot == 1) {
            card_update(object);
            return;
        }
        if (slot == 2) {
            card_draw(object, static_cast<std::int32_t>(a0), static_cast<std::int32_t>(args.size() > 1 ? args[1] : 0));
            return;
        }
    } else if (vt == kMainVtWindow) {
        if (slot == 0) {
            window_delete(object, a0);
            return;
        }
        if (slot == 1) {
            window_update(object);
            return;
        }
    }
    std::vector<std::uint32_t> all{object};
    all.insert(all.end(), args.begin(), args.end());
    s_.note(name, std::move(all));
}

bool MainMenu::dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::string service = name;
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    const std::int32_t x = static_cast<std::int32_t>(a0), y = static_cast<std::int32_t>(args.size() > 1 ? args[1] : 0);
    // (the snaps are never made while the title is up; the prompt windows are built by the engine and stay services)
    if (vt == kMainVtWheel) {
        if (service == kVirtualDelete) {
            wheel_delete(object, a0);
        } else if (service == kElementUpdate) {
            wheel_update(object);
        } else if (service == kElementDraw) {
            wheel_draw(object, x, y);
        } else {
            return false;
        }
        return true;
    }
    // the wheel's arrow is a sprite element of the real class
    if (vt == kVtSprite) {
        if (service == kVirtualDelete) {
            w_.sprite_delete(object, a0);
        } else if (service == kElementUpdate) {
            w_.element_update(object);
        } else if (service == kElementDraw) {
            w_.sprite_draw(object, x, y);
        } else {
            return false;
        }
        return true;
    }
    return false;
}

// A prompt window of the title screen: the engine builds it (a service), the window base holds it.
std::uint32_t MainMenu::prompt(std::uint32_t self, std::uint32_t file, std::uint32_t font, std::uint32_t x) {
    const std::uint32_t memory = operator_new(0xb0);
    std::uint32_t window = 0;
    if (memory != 0) {
        s_.note(kSvcPromptConstruct, {memory, file, 0x005252f0, font, x, 0xdc, 0xffffffffu, global_word(m_, kScreenFillHeight) + 0x14}, m_.cstring(file));
        window = static_cast<std::uint32_t>(s_.value(kSvcPromptConstruct));
    }
    (void)self;
    return window;
}

// ---- the title window ----------------------------------------------------------------------------------------------

std::uint32_t MainMenu::window_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    s_.note(kSvcFontConstruct, {self + 0xac});
    s_.note(kSvcFontConstruct, {self + 0x20c});
    mainmenu_window_at(m_, self).set_field_0(static_cast<Word>(kMainVtWindow));
    s_.note(engine_name(0x004a90f0), {global_word(m_, 0x0056b834), 0x00534580, 0});
    // the background sprite object: raw `operator new` (0x005002fe), the sprite file opened by the engine, then its constructor
    s_.note("crt_new", {0x1c});
    const std::uint32_t sprite_memory = static_cast<std::uint32_t>(s_.value("crt_new"));
    std::uint32_t sprite = 0;
    if (sprite_memory != 0) {
        s_.note(engine_name(0x00448dd0), {0x00534574, 1}, m_.cstring(0x00534574));
        const std::uint32_t handle = static_cast<std::uint32_t>(s_.value(engine_name(0x00448dd0)));
        s_.note(engine_name(seam::hal::D3DSprite_Create), {sprite_memory, handle, 0x00534574, 1});
        sprite = static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::D3DSprite_Create)));
    }
    mainmenu_window_at(m_, self).set_field_204(static_cast<Word>(sprite));
    const std::uint32_t wheel_memory = operator_new(0xd0);
    std::uint32_t wheel = 0;
    if (wheel_memory != 0) {
        wheel = wheel_construct(wheel_memory, 0xff, 0x81, -110, 0x80);
    }
    mainmenu_window_at(m_, self).set_field_208(static_cast<Word>(wheel));
    w_.window_add(self, wheel);
    s_.note("font_load", {0x00525458}, m_.cstring(0x00525458));
    copy_font(self + 0xac, static_cast<std::uint32_t>(s_.value("font_load")));
    mainmenu_window_at(m_, self).set_field_b0(1);
    mainmenu_window_at(m_, self).set_field_b4(1);
    mainmenu_window_at(m_, self).set_field_dc(0x800);
    mainmenu_window_at(m_, self).set_field_c0(0x40);
    mainmenu_window_at(m_, self).set_field_c4(0x40);
    mainmenu_window_at(m_, self).set_field_c8(0xff);
    s_.note("font_load", {0x0052544c}, m_.cstring(0x0052544c));
    copy_font(self + 0x20c, static_cast<std::uint32_t>(s_.value("font_load")));
    mainmenu_window_at(m_, self).set_field_210(0);
    mainmenu_window_at(m_, self).set_field_214(3);
    f_.set_rgb(self + 0x20c, 0x80, 0x80, 0x80);
    mainmenu_window_at(m_, self).set_field_22d(0);
    mainmenu_window_at(m_, self).set_field_218(0);
    mainmenu_window_at(m_, self).set_field_21c(0);
    mainmenu_window_at(m_, self).set_field_23c(0x800);
    const std::uint32_t font = self + 0x20c;
    const std::uint32_t first = prompt(self, 0x00534568, font, 0x30);
    mainmenu_window_at(m_, self).set_field_200(static_cast<Word>(first));
    w_.window_add(self, first);
    const std::uint32_t second = prompt(self, 0x0052b714, font, 0xce);
    mainmenu_window_at(m_, self).set_field_360(static_cast<Word>(second));
    w_.window_add(self, second);
    const std::uint32_t third = prompt(self, 0x00534558, font, 0x184);
    mainmenu_window_at(m_, self).set_field_364(static_cast<Word>(third));
    w_.window_add(self, third);
    s_.note(engine_name(0x004a9330), {global_word(m_, 0x0056b834), 0x00534580});
    mainmenu_window_at(m_, self).set_field_35c(0);
    if (global_byte(m_, kCardScreenFlag) != 0) {
        mainmenu_window_at(m_, self).set_field_1f8(1);
        const std::uint32_t card_memory = operator_new(0xd60);
        std::uint32_t card = 0;
        if (card_memory != 0) {
            card = card_construct(card_memory);
        }
        mainmenu_window_at(m_, self).set_field_35c(static_cast<Word>(card));
    } else {
        mainmenu_window_at(m_, self).set_field_1f8(0);
    }
    return self;
}

void MainMenu::window_destroy(std::uint32_t self) {
    mainmenu_window_at(m_, self).set_field_0(static_cast<Word>(kMainVtWindow));
    const std::uint32_t sprite = mainmenu_window_at(m_, self).field_204();
    if (sprite != 0) {
        s_.note(engine_name(seam::hal::D3DSprite_Destroy), {sprite});
        s_.note("crt_delete", {sprite});
    }
    s_.note("font_unload", {0x00525458}, m_.cstring(0x00525458));
    s_.note("font_unload", {0x0052544c}, m_.cstring(0x0052544c));
    const std::uint32_t card = mainmenu_window_at(m_, self).field_35c();
    if (card != 0) {
        vcall_window(kVirtualDelete, card, 0, {1});
        mainmenu_window_at(m_, self).set_field_35c(0);
    }
    s_.note(kSvcFontDestroy, {self + 0x20c});
    s_.note(kSvcFontDestroy, {self + 0xac});
    w_.window_destroy(self);
}

std::uint32_t MainMenu::window_delete(std::uint32_t self, std::uint32_t flag) {
    window_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void MainMenu::window_draw(std::uint32_t self) {
    const bool scanning = static_cast<std::int32_t>(mainmenu_window_at(m_, self).field_1f8()) == 1;
    for (std::uint32_t i = 0; i < 2; ++i) {
        set_word_at(m_, word_at(m_, self + 0x360 + 4 * i) + 0x14, scanning ? 1 : 0);
    }
    if (scanning) {
        check(mainmenu_window_at(m_, self).field_35c() != 0, kMainMsgNoScreen);
        vcall_window("card_draw", mainmenu_window_at(m_, self).field_35c(), 2, {0, 0});
    }
    w_.window_draw(self, 0, 0);
    s_.note(engine_name(0x004d7990), {mainmenu_window_at(m_, self).field_204(), word_at(m_, global_word(m_, 0x00560fd4) + 0x88), 0, 0});
}

void MainMenu::window_update(std::uint32_t self) {
    if (static_cast<std::int32_t>(mainmenu_window_at(m_, self).field_1f8()) == 1) {
        check(mainmenu_window_at(m_, self).field_35c() != 0, kMainMsgNoScreen);
        vcall_window("card_update", mainmenu_window_at(m_, self).field_35c(), 1);
        s_.note("pad_clear", {0x0056aff8});
        s_.note("pad_clear", {0x0056b164});
    }
    w_.window_update(self);
}

std::uint32_t MainMenu::window_poll(std::uint32_t self, std::uint32_t result_pointer) {
    return poll_to(self, [&](std::uint32_t value) { set_word_at(m_, result_pointer, value); });
}

std::uint32_t MainMenu::poll_to(std::uint32_t self, const std::function<void(std::uint32_t)>& store) {
    const std::uint32_t entry = (global_word(m_, 0x0056b144) != 0 && global_word(m_, 0x0056b2b0) != 0) ? 0x27u : 0xffffffffu;
    set_global_word(m_, 0x00534508, entry);
    set_global_word(m_, 0x0053450c, entry);
    if (int_at(m_, self + 0x1f8) == 1) {
        check(word_at(m_, self + 0x35c) != 0, kMainMsgNoScreen);
        if (card_result(word_at(m_, self + 0x35c)) != 0) {
            const std::uint32_t card = word_at(m_, self + 0x35c);
            if (card != 0) {
                vcall_window(kVirtualDelete, card, 0, {1});
            }
            set_word_at(m_, self + 0x35c, 0);
            set_word_at(m_, self + 0x1f8, 0);
            set_global_byte(m_, kCardScreenFlag, 0);
        }
        return 0;
    }
    for (std::uint32_t pad = 0; pad < 2; ++pad) {
        const std::uint32_t at = kPadBase + 0x16c * pad;     // the "confirm" edge (0x0056b029 for pad 0)
        const std::uint32_t other = at + 0xb0;               // (0x0056b0d9)
        const bool fired = (byte_at(m_, at) != 0 && global_byte(m_, 0x0056b038) == 0) || byte_at(m_, other) != 0;
        if (!fired) {
            continue;
        }
        set_byte_at(m_, at, 0);
        set_byte_at(m_, other, 0);
        const std::uint32_t selected = word_at(m_, word_at(m_, self + 0x208) + 0xcc);
        const std::uint32_t chosen = word_at(m_, kWheelTable + 4 + 4 * (selected * 3 + pad));
        store(chosen);
        if (chosen != 0xffffffffu) {
            sfx(0x91);
            return 1;
        }
        sfx(0x92);
    }
    return 0;
}

void MainMenu::run_wrapper(std::uint32_t result_pointer) { set_word_at(m_, result_pointer, run()); }

std::uint32_t MainMenu::run() {
    const std::uint32_t saved = global_word(m_, 0x0056114c);
    set_global_word(m_, 0x0056114c, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    engine(0x00489490);
    const std::uint32_t memory = operator_new(0x368);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = window_construct(memory);
    }
    s_.note("pad_clear", {0x0056aff8});
    s_.note("pad_clear", {0x0056b164});
    std::uint32_t result = 0xffffffffu;
    set_global_word(m_, 0x00568624, global_word(m_, 0x0056e31c));
    for (;;) {
        frame_tick();
        poll_input();
        engine(0x004dae00);
        const bool quit = (engine_value(0x004e41b0, {0x10}) & 0xff) != 0 || global_byte(m_, 0x006a1415) != 0;
        if (quit) {
            sfx(0x93);
            set_global_byte(m_, 0x006a1415, 0);
            result = 0xff;
            break;
        }
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        // the poll stores through a pointer to a local of the original's frame: the port keeps the local
        if (poll_to(window, [&](std::uint32_t value) { result = value; }) != 0) {
            break;
        }
        vcall_window("title_update", window, 1);
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        window_draw(window);
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        if (!(global_word(m_, 0x0056e31c) >= global_word(m_, 0x00568624) + 2)) {
            do {
                frame_tick();
            } while (global_word(m_, 0x0056e31c) < global_word(m_, 0x00568624) + 2);
        }
        set_global_word(m_, 0x00568624, global_word(m_, 0x0056e31c));
        engine(0x00430510);
        engine(0x00466c00);
    }
    set_global_word(m_, 0x0056114c, saved);
    if (window != 0) {
        vcall_window(kVirtualDelete, window, 0, {1});
    }
    return result;
}

}  // namespace opentony::runtime::frontend
