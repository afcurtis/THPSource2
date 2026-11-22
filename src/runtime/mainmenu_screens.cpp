#include "auto_views.hpp"
#include "mainmenu.hpp"
#include "pc_platform.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kVtRootClass = 0x005184b4;

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kLiveText = 0x7ff0f400;   // (the scratch window: the text a card message is built in, 0x100 bytes; retail's was a local of the frame)

}  // namespace

// ---- the wheel ---------------------------------------------------------------------------------------------------

std::uint32_t MainMenu::wheel_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    mainmenu_screens_wheel_at(m_, self).set_field_0(static_cast<Word>(kMainVtWheel));
    const std::uint32_t sprite_memory = operator_new(0x60);
    std::uint32_t sprite = 0;
    if (sprite_memory != 0) {
        s_.note("sprite_construct_b", {sprite_memory, 0x005345cc, 1, 0, 0}, m_.cstring(0x005345cc));
        sprite = static_cast<std::uint32_t>(s_.value("sprite_construct_b"));
    }
    mainmenu_screens_wheel_at(m_, self).set_field_ac(static_cast<Word>(sprite));
    s_.note(kSpriteInit, {sprite});
    set_byte_at(m_, mainmenu_screens_wheel_at(m_, self).field_ac() + 0x0a, 1);
    set_byte_at(m_, mainmenu_screens_wheel_at(m_, self).field_ac() + 0x0b, 1);
    const std::uint32_t arrow_memory = operator_new(0x2c);
    std::uint32_t arrow = 0;
    if (arrow_memory != 0) {
        arrow = w_.sprite_construct(arrow_memory, 0x005345bc, 1, -81, 0, 0, 1, 1);
    }
    w_.window_add(self, arrow);
    mainmenu_screens_wheel_at(m_, self).set_field_b8(7);
    const std::uint32_t selected = global_byte(m_, kWheelSelectionMemory);
    set_word_at(m_, self + 0xcc, selected);
    const std::uint32_t angle = word_at(m_, kWheelTable + selected * 12);
    mainmenu_screens_wheel_at(m_, self).set_field_c0(0);
    mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(angle));
    mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(angle));
    mainmenu_screens_wheel_at(m_, self).set_field_bc(static_cast<Word>(selected));
    wheel_palette_alloc(self);
    wheel_tint(self);
    return self;
}

void MainMenu::wheel_destroy(std::uint32_t self) {
    mainmenu_screens_wheel_at(m_, self).set_field_0(static_cast<Word>(kMainVtWheel));
    s_.note(kMemDeleteService, {mainmenu_screens_wheel_at(m_, self).field_c4()});
    s_.note(kMemDeleteService, {mainmenu_screens_wheel_at(m_, self).field_c8()});
    set_global_byte(m_, kWheelSelectionMemory, byte_at(m_, self + 0xcc));
    if (mainmenu_screens_wheel_at(m_, self).field_ac() != 0) {
        s_.note(kVirtualDelete, {mainmenu_screens_wheel_at(m_, self).field_ac(), 1});
    }
    w_.window_destroy(self);
}

std::uint32_t MainMenu::wheel_delete(std::uint32_t self, std::uint32_t flag) {
    wheel_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

void MainMenu::wheel_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_draw(self, x, y);
    s_.note(kSpriteDrawService, {mainmenu_screens_wheel_at(m_, self).field_ac(), u(add(mainmenu_screens_wheel_at(m_, self).field_20(), x)), u(add(mainmenu_screens_wheel_at(m_, self).field_24(), y)), mainmenu_screens_wheel_at(m_, self).field_b0()});
}

void MainMenu::wheel_palette_alloc(std::uint32_t self) {
    s_.note(kAllocBuffer, {0x200, 1, 1, 0});
    mainmenu_screens_wheel_at(m_, self).set_field_c4(static_cast<Word>(static_cast<std::uint32_t>(s_.value(kAllocBuffer))));
    s_.note(kAllocBuffer, {0x200, 1, 1, 0});
    mainmenu_screens_wheel_at(m_, self).set_field_c8(static_cast<Word>(static_cast<std::uint32_t>(s_.value(kAllocBuffer))));
    s_.note("sprite_get_palette", {mainmenu_screens_wheel_at(m_, self).field_ac()});
    const std::uint32_t palette = static_cast<std::uint32_t>(s_.value("sprite_get_palette"));
    m_.copy(mainmenu_screens_wheel_at(m_, self).field_c4(), palette, 0x200);
}

void MainMenu::wheel_tint(std::uint32_t self) {
    s_.note("mem_copy", {mainmenu_screens_wheel_at(m_, self).field_c8(), mainmenu_screens_wheel_at(m_, self).field_c4(), 0x200});
    const std::uint32_t buffer = mainmenu_screens_wheel_at(m_, self).field_c8();
    const std::int32_t nearest = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_bc());
    const bool flag = mainmenu_screens_wheel_at(m_, self).field_c0() != 0;
    bool halve = flag;
    if (nearest != 0 || !flag) {
        const std::int32_t row = (sub(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b8()), nearest)) % 7;
        std::uint32_t offset = static_cast<std::uint32_t>((row * 8 + 200) * 2);
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t c = half_at(m_, buffer + offset);
            std::uint32_t g = (c >> 4) & 0x3e;
            if (g > 0x1f) {
                g = 0x1f;
            }
            const std::uint32_t result = (((g | 0x3e0) << 5) | (c & 0x1f) | (c & 0x8000)) & 0xffff;
            set_half_at(m_, buffer + offset, result);
            offset += 2;
        }
        halve = flag;
    }
    if (halve) {
        for (std::uint32_t offset = 0x190; offset < 0x1a0; offset += 2) {
            const std::uint32_t v = half_at(m_, buffer + offset);
            set_half_at(m_, buffer + offset, ((v >> 1) & 0x3def) | (v & 0x8000));
        }
    }
    s_.note("sprite_set_palette", {mainmenu_screens_wheel_at(m_, self).field_ac(), buffer});
}

void MainMenu::wheel_update(std::uint32_t self) {
    s_.note(kElementHook, {self});   // slot 7 of the wheel is the window base's per-frame hook
    if (global_byte(m_, 0x0056b038) != 0) {
        s_.note("pad_present", {0});
        if (static_cast<std::uint32_t>(s_.value("pad_present")) != 0) {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(sub(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b4()), 0x800))));
        }
        s_.note("pad_clear", {0x0056aff8});
        s_.note("pad_clear", {0x0056b164});
    } else {
        set_global_word(m_, 0x0056c574, 0);
        set_global_word(m_, 0x0056c578, 0);
    }
    const std::uint32_t row_at = kWheelTable;
    if (global_byte(m_, 0x0056b079) != 0 || global_byte(m_, 0x0056b1e5) != 0) {
        s_.note(kSvcSfx, {0xe9, 0x2000, 0});
        set_global_byte(m_, 0x0056b079, 0);
        set_global_byte(m_, 0x0056b1e5, 0);
        std::int32_t index = add(int_at(m_, self + 0xcc), 1);
        set_word_at(m_, self + 0xcc, u(index));
        if (!(index < static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b8()))) {
            set_word_at(m_, self + 0xcc, 0);
        }
        const std::int32_t current = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0());
        const std::int32_t target = int_at(m_, row_at + 12 * word_at(m_, self + 0xcc));
        mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(target)));
        if (sub(target, current) > 0) {
            mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(sub(target, 0x1000))));
        }
    }
    if (global_byte(m_, 0x0056b089) != 0 || global_byte(m_, 0x0056b1f5) != 0) {
        s_.note(kSvcSfx, {0xe9, 0x2000, 0});
        set_global_byte(m_, 0x0056b089, 0);
        set_global_byte(m_, 0x0056b1f5, 0);
        std::int32_t index = sub(int_at(m_, self + 0xcc), 1);
        set_word_at(m_, self + 0xcc, u(index));
        if (index < 0) {
            set_word_at(m_, self + 0xcc, u(sub(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b8()), 1)));
        }
        const std::int32_t current = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0());
        const std::int32_t target = int_at(m_, row_at + 12 * word_at(m_, self + 0xcc));
        mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(target)));
        if (sub(target, current) < 0) {
            mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(add(target, 0x1000))));
        }
    }
    {
        const std::int32_t target = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b4());
        const std::int32_t current = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0());
        const std::int32_t gap = sub(target, current);
        if (gap < -0x258) {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(sub(current, 0x80))));
        } else if (gap > 0x258) {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(add(current, 0x80))));
        } else if (gap < -0x40) {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(sub(current, 0x40))));
        } else if (gap > 0x40) {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(add(current, 0x40))));
        } else {
            mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(target)));
        }
    }
    if (static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0()) >= 0x1000) {
        mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(sub(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0()), 0x1000))));
        mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(sub(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b4()), 0x1000))));
    }
    if (static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0()) < 0) {
        mainmenu_screens_wheel_at(m_, self).set_field_b0(static_cast<Word>(u(add(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0()), 0x1000))));
        mainmenu_screens_wheel_at(m_, self).set_field_b4(static_cast<Word>(u(add(static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b4()), 0x1000))));
    }
    // the entry nearest the current angle
    std::int32_t nearest = 0;
    {
        const std::int32_t count = static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b8());
        std::int32_t best = 0x2710;
        for (std::int32_t i = 0; i < count; ++i) {
            std::int32_t distance = sub(int_at(m_, row_at + 12 * u(i)), static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_b0()));
            if (distance < 0) {
                distance = -distance;
            }
            if (distance < best) {
                best = distance;
                nearest = i;
            }
        }
    }
    const std::uint32_t both = (global_word(m_, 0x0056b144) != 0 && global_word(m_, 0x0056b2b0) != 0) ? 0 : 1;
    if (nearest != static_cast<std::int32_t>(mainmenu_screens_wheel_at(m_, self).field_bc()) || mainmenu_screens_wheel_at(m_, self).field_c0() != both) {
        mainmenu_screens_wheel_at(m_, self).set_field_bc(static_cast<Word>(u(nearest)));
        mainmenu_screens_wheel_at(m_, self).set_field_c0(static_cast<std::uint8_t>(both));
        wheel_tint(self);
    }
    w_.window_update(self);
}

// ---- the memory-card screen --------------------------------------------------------------------------------------

void MainMenu::card_register(std::uint32_t descriptor) {
    set_word_at(m_, descriptor, 0x0046be80);
    set_word_at(m_, descriptor + 0x0c, 0x0046be90);
}

void MainMenu::card_nothing() {}

void MainMenu::card_run_wrapper(std::uint32_t result_pointer) { set_word_at(m_, result_pointer, card_run()); }

std::uint32_t MainMenu::card_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    s_.note(kSvcFontConstruct, {self + 0xb0});
    s_.note(engine_name(0x00473e50), {self + 0x1fc});
    mainmenu_screens_card_at(m_, self).set_field_0(static_cast<Word>(kMainVtCard));
    mainmenu_screens_card_at(m_, self).set_field_d48(1);
    mainmenu_screens_card_at(m_, self).set_field_d4c(4);
    mainmenu_screens_card_at(m_, self).set_field_d54(0);
    s_.note(kSvcFontFind, {0x0052544c}, m_.cstring(0x0052544c));
    copy_font(self + 0xb0, static_cast<std::uint32_t>(s_.value(kSvcFontFind)));
    mainmenu_screens_card_at(m_, self).set_field_b4(1);
    mainmenu_screens_card_at(m_, self).set_field_b8(3);
    f_.set_rgb(self + 0xb0, 0x80, 0x80, 0x80);
    mainmenu_screens_card_at(m_, self).set_field_d1(1);
    mainmenu_screens_card_at(m_, self).set_field_bc(2);
    mainmenu_screens_card_at(m_, self).set_field_c0(2);
    mainmenu_screens_card_at(m_, self).set_field_d4(2);
    mainmenu_screens_card_at(m_, self).set_field_d8(2);
    mainmenu_screens_card_at(m_, self).set_field_e0(0x800);
    const std::uint32_t memory = operator_new(0x418);
    std::uint32_t box = 0;
    if (memory != 0) {
        s_.note(kSvcTextboxConstruct, {memory, self + 0xb0, 0x00527c68, 0x100, 0xd7});
        box = static_cast<std::uint32_t>(s_.value(kSvcTextboxConstruct));
    }
    mainmenu_screens_card_at(m_, self).set_field_d50(static_cast<Word>(box));
    set_word_at(m_, box + 4, 1);
    mainmenu_screens_card_at(m_, self).set_field_d58(0);
    mainmenu_screens_card_at(m_, self).set_field_d5c(0);
    return self;
}

void MainMenu::card_destroy(std::uint32_t self) {
    mainmenu_screens_card_at(m_, self).set_field_0(static_cast<Word>(kMainVtCard));
    const std::uint32_t box = mainmenu_screens_card_at(m_, self).field_d50();
    if (box != 0) {
        s_.note(kVirtualDelete, {box, 1});
    }
    s_.note(engine_name(0x00473f10), {self + 0x1fc});
    s_.note(kSvcFontDestroy, {self + 0xb0});
    w_.window_destroy(self);
}

std::uint32_t MainMenu::card_delete(std::uint32_t self, std::uint32_t flag) {
    card_destroy(self);
    if ((flag & 1) != 0) {
        s_.note(kOperatorDelete, {self});
    }
    return self;
}

std::uint32_t MainMenu::card_result(std::uint32_t self) const { return word_at(m_, self + 0xd54); }

// Text_Layout(box, text) (0x00425ca0): a replay reports the box and carries the text as the call's text; live the call takes the text's address, so a text that is not in
// memory yet is put in the scratch window first
void MainMenu::card_set_text(std::uint32_t box, std::uint32_t text_address, const std::string& text) {
    if (!s_.live()) {
        s_.note(kSvcTextboxSetText, {box}, text);
        return;
    }
    if (text_address == 0) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        m_.write(kLiveText, bytes);
        text_address = kLiveText;
    }
    s_.note(kSvcTextboxSetText, {box, text_address}, text);
}

void MainMenu::card_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (mainmenu_screens_card_at(m_, self).field_d54() == 0) {
        if (static_cast<std::int32_t>(mainmenu_screens_card_at(m_, self).field_d48()) == 1) {
            const std::uint32_t text = global_word(m_, 0x0052e36c);
            card_set_text(mainmenu_screens_card_at(m_, self).field_d50(), text, m_.cstring(text, 0x100));
            menu_.vcall_item(kSvcTextboxUpdate, mainmenu_screens_card_at(m_, self).field_d50(), 1);   // (the text box's vtable slot 1; live, the call of the address the vtable holds)
        }
        const std::uint32_t state = mainmenu_screens_card_at(m_, self).field_d4c();
        if (state == 9 || state == 0xb) {
            if (mainmenu_screens_card_at(m_, self).field_d58() != 0) {
                mainmenu_screens_card_at(m_, self).set_field_d58(static_cast<Word>(mainmenu_screens_card_at(m_, self).field_d58() - 1));
            }
            menu_.vcall_item(kSvcTextboxUpdate, mainmenu_screens_card_at(m_, self).field_d50(), 1);   // (the text box's vtable slot 1; live, the call of the address the vtable holds)
        }
    }
    w_.window_draw(self, x, y);
}

void MainMenu::card_update(std::uint32_t self) {
    const std::uint32_t effect = self + 0x1fc;
    const std::uint32_t before = mainmenu_screens_card_at(m_, self).field_d48();
    // the effect object reports its state through the word it is handed the address of
    engine(0x00475480, {effect, self + 0xd48});
    if (!s_.live()) {   // (live, the call stored the state through the pointer itself)
        mainmenu_screens_card_at(m_, self).set_field_d48(static_cast<Word>(static_cast<std::uint32_t>(s_.value(engine_name(0x00475480)))));
    }
    const std::uint32_t state = mainmenu_screens_card_at(m_, self).field_d48();
    if (state == 1) {
        w_.window_update(self);
        return;
    }
    if (state == 0 && before != 0) {
        if (engine_value(0x00475620, {effect}) == 0) {
            engine(0x00474d70, {effect, 1});
            if (engine_value(0x004741f0, {effect, 1}) == 0) {
                mainmenu_screens_card_at(m_, self).set_field_d54(1);
            } else {
                mainmenu_screens_card_at(m_, self).set_field_d58(3);
                const std::uint32_t a = engine_value(0x00473f30, {effect});
                const std::uint32_t b = engine_value(0x004742e0, {effect, a});
                const std::uint32_t format = global_word(m_, 0x0052e368);
                // the formatted text is written to a buffer of the original's frame: a replay scripts its first eight bytes; live there is no call to make (the retail
                // call's destination is a local of its frame), so the Kit formats it ("Loading [%s]..." with the name)
                std::string text;
                bool done = false;
                if (s_.live()) {
                    text = pcplatform::crt::format(m_.cstring(format, 0x100), {m_.cstring(b, 0x100)});
                    done = true;
                } else {
                    s_.note("sprintf", {format, b}, m_.cstring(format, 0x100));
                }
                for (int i = 0; i < 2 && !done; ++i) {
                    const std::uint32_t word = static_cast<std::uint32_t>(s_.value("sprintf"));
                    for (int k = 0; k < 4 && !done; ++k) {
                        const char c = static_cast<char>((word >> (8 * k)) & 0xff);
                        if (c == 0) {
                            done = true;
                        } else {
                            text.push_back(c);
                        }
                    }
                }
                card_set_text(mainmenu_screens_card_at(m_, self).field_d50(), 0, text);
                mainmenu_screens_card_at(m_, self).set_field_d4c(9);
            }
        }
    }
    check(mainmenu_screens_card_at(m_, self).field_d48() == 0, kMainMsgCardNotIdle);
    std::uint32_t finished = 1;
    if (engine_value(0x00475620, {effect}) != 0 && mainmenu_screens_card_at(m_, self).field_d4c() != 0xb) {
        mainmenu_screens_card_at(m_, self).set_field_d54(static_cast<Word>(finished));
    }
    if (mainmenu_screens_card_at(m_, self).field_d54() != 0) {
        w_.window_update(self);
        return;
    }
    const std::uint32_t screen = mainmenu_screens_card_at(m_, self).field_d4c();
    if (screen == 0xb) {
        const std::int32_t timeout = sub(static_cast<std::int32_t>(mainmenu_screens_card_at(m_, self).field_d5c()), 1);
        mainmenu_screens_card_at(m_, self).set_field_d5c(static_cast<Word>(u(timeout)));
        if (timeout < 0) {
            mainmenu_screens_card_at(m_, self).set_field_d54(static_cast<Word>(finished));
        }
        if (global_byte(m_, kPadPressB) != 0 || global_byte(m_, 0x0056b195) != 0) {
            s_.note(kSvcSfx, {0xb4, 0x2000, 0});
            set_global_byte(m_, kPadPressB, 0);
            set_global_byte(m_, 0x0056b195, 0);
            mainmenu_screens_card_at(m_, self).set_field_d54(static_cast<Word>(finished));
            w_.window_update(self);
            return;
        }
    } else if (screen == 9) {
        if (static_cast<std::int32_t>(mainmenu_screens_card_at(m_, self).field_d58()) <= 0) {
            s_.note(kAllocBuffer, {0x1e00, 0, finished, 0});
            const std::uint32_t buffer = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
            engine(0x00475840, {effect, buffer, 0x1e00});
            const std::uint32_t a = engine_value(0x00473f30, {effect});
            if (engine_value(0x00476200, {effect, a, finished}) == 0) {
                card_set_text(mainmenu_screens_card_at(m_, self).field_d50(), global_word(m_, 0x0052e9d0), m_.cstring(global_word(m_, 0x0052e9d0), 0x100));
                mainmenu_screens_card_at(m_, self).set_field_d4c(0xb);
                mainmenu_screens_card_at(m_, self).set_field_d5c(0x2d);
                engine(0x004138f0);
            } else {
                s_.note("mem_copy", {0x00568a68, buffer, 0x1dc4});
                engine(0x004165d0);
                mainmenu_screens_card_at(m_, self).set_field_d54(static_cast<Word>(finished));
            }
            s_.note(kMemDeleteService, {buffer});
        }
    } else {
        s_.check(false, kMainMsgBadCardState);
        w_.window_update(self);
        return;
    }
    w_.window_update(self);
}

std::uint32_t MainMenu::card_run() {
    const std::uint32_t saved = global_word(m_, 0x0056114c);
    set_global_word(m_, 0x0056114c, 0);
    engine(0x0042fc70);
    s_.note("font_load", {0x005254e0}, m_.cstring(0x005254e0));
    s_.note("font_load", {0x0052544c}, m_.cstring(0x0052544c));
    const std::uint32_t memory = operator_new(0xd60);
    std::uint32_t card = 0;
    if (memory != 0) {
        card = card_construct(memory);
    }
    s_.note("pad_clear", {0x0056aff8});
    s_.note("pad_clear", {0x0056b164});
    engine(0x004c8f30);
    std::uint32_t result = 0;
    for (;;) {
        frame_tick();
        poll_input();
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00489a10);
        result = card_result(card);
        if (result == 1 || result == 2) {
            break;
        }
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
        vcall_window("card_update", card, 1);
        vcall_window("card_draw", card, 2, {0, 0});
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
    if (card != 0) {
        vcall_window(kVirtualDelete, card, 0, {1});
    }
    s_.note("font_unload", {0x005254e0}, m_.cstring(0x005254e0));
    s_.note("font_unload", {0x0052544c}, m_.cstring(0x0052544c));
    s_.note("pad_clear", {0x0056aff8});
    s_.note("pad_clear", {0x0056b164});
    return result == 2 ? 0xffffffffu : 0u;       // (`setne al; dec eax`: -1 only when the scan finished with result 2)
}

}  // namespace opentony::runtime::frontend
