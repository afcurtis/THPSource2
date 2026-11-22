#include "auto_views.hpp"
#include "skater_windows.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kHidden = 0x14;
constexpr std::uint32_t kGroup = 0xac;        // the current texture-set id of the skin tone chain
constexpr std::uint32_t kTone = 0xb0;
constexpr std::uint32_t kIds = 0xb4;          // seven ids from +0xb4
constexpr std::uint32_t kList = 0xd0;
constexpr std::uint32_t kItems = 0xd4;
constexpr std::uint32_t kPropsA = 0x254;
constexpr std::uint32_t kPropsB = 0x258;
constexpr std::uint32_t kArrowA = 0x25c;
constexpr std::uint32_t kArrowB = 0x260;
constexpr std::uint32_t kSkater = 0x264;
constexpr std::uint32_t kItemSlots = 0x60;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;

}  // namespace

std::uint32_t SkaterWindows::body_group_apply(std::uint32_t self, std::uint32_t group, std::uint32_t id, std::uint32_t tone) {
    engine(0x004208a0, {word_at(m_, self + kSkater), group, id, tone});
    return 0;
}

// A MenuProperties for one of the two fonts the page uses: the loaded font copied in, then a right-aligned (2) or centred (1) orange style.
std::uint32_t SkaterWindows::new_properties(std::uint32_t font_name, std::uint32_t justification, std::uint32_t vertical, std::uint32_t scale) {
    (void)scale;
    const std::uint32_t memory = operator_new(0x17c);
    std::uint32_t props = 0;
    if (memory != 0) {
        props = menu_.properties_construct(memory);
    }
    s_.note(kSvcFontFind, {font_name}, m_.cstring(font_name, 0x40));
    const std::uint32_t font = static_cast<std::uint32_t>(s_.value(kSvcFontFind));
    menu_.copy_font_fields(props + 4, font);
    set_word_at(m_, props + 8, justification);
    set_word_at(m_, props + 0x0c, vertical);
    f_.set_rgb(props + 4, 0xff, 0x60, 0x20);
    set_byte_at(m_, props + 0x25, 0);
    set_word_at(m_, props + 0x10, 0);
    set_word_at(m_, props + 0x14, 0);
    set_word_at(m_, props + 0x34, 0x1000);
    set_word_at(m_, props + 0x150, 0x1324);
    set_word_at(m_, props + 0x154, 0x1000);
    return props;
}

// 0x00429e00: build the page for the editor object `skater`: the seven texture-set ids of the current custom skater, the skin tone they
// name, the two arrow sprites, then the item list.
std::uint32_t SkaterWindows::appearance_construct(std::uint32_t self, std::uint32_t skater, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    skater_windows_appearance_appearance_at(m_, self).set_field_0(static_cast<Word>(kAppearanceVt));
    for (std::uint32_t i = 0; i < 7; ++i) {
        set_word_at(m_, self + kIds + 4 * i, word_at(m_, 0x0056a4ec + (custom_slot() * 0x20 + i) * 4));
    }
    const std::uint32_t holder = global_word(m_, kTextureManagerPointer);
    const std::uint32_t set = tex_.find_by_id(holder, static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b4()));
    check(set != 0, kAppMsgNoTextureSet);
    check(word_at(m_, set + 0x30) != 0, kAppMsgNoTexture);
    check(word_at(m_, set + 0x30) + 8 != 0, kAppMsgNoFilename);
    check(byte_at(m_, word_at(m_, set + 0x30) + 0x0f) != '0', kAppMsgNoSkintone);
    skater_windows_appearance_appearance_at(m_, self).set_field_b0(static_cast<Word>(u(add(sbyte_at(m_, word_at(m_, set + 0x30) + 0x0f), -0x31))));
    skater_windows_appearance_appearance_at(m_, self).set_field_ac(static_cast<Word>(u(tex_.find_by_group(holder, 0))));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b0()); ++i) {
        skater_windows_appearance_appearance_at(m_, self).set_field_ac(static_cast<Word>(u(tex_.step(holder, -1, static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_ac()), 1))));
    }
    check(skater != 0, kAppMsgNoSkater);
    skater_windows_appearance_appearance_at(m_, self).set_field_264(static_cast<Word>(skater));
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t mode) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, slot, sprite);
        set_byte_at(m_, sprite + 0x0a, mode);
        set_byte_at(m_, sprite + 0x0b, 1);
        s_.note(kSpriteInit, {sprite});
    };
    arrow(self + kArrowA, 0x00525908, 2);
    arrow(self + kArrowB, 0x005258f8, 0);
    appearance_build(self);
    return self;
}

std::uint32_t SkaterWindows::appearance_delete(std::uint32_t self, std::uint32_t flag) {
    appearance_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0042a110: the scrolling list and its twelve items: rows for the skin tone, the shirt, the hair, ... and the colour swatches (ColourText).
void SkaterWindows::appearance_build(std::uint32_t self) {
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x2f, 0xef, 0x96, 0, 3, 6, 1);
    }
    skater_windows_appearance_appearance_at(m_, self).set_field_d0(static_cast<Word>(list));
    set_word_at(m_, list + 0x1b4, 1);
    const std::uint32_t props_a = new_properties(0x00527960, 2, 3, 0);
    skater_windows_appearance_appearance_at(m_, self).set_field_254(static_cast<Word>(props_a));
    const std::uint32_t props_b = new_properties(0x00527950, 1, 3, 0);
    skater_windows_appearance_appearance_at(m_, self).set_field_258(static_cast<Word>(props_b));
    for (std::uint32_t i = 0; i < kItemSlots; ++i) {
        set_word_at(m_, self + kItems + 4 * i, 0);
    }
    const std::string heading = "         " + m_.cstring(global_word(m_, 0x0052e968), 0x100);
    const std::uint32_t skater = skater_windows_appearance_appearance_at(m_, self).field_264();
    auto row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text_global) {
        const std::uint32_t block = operator_new(0x100);
        std::uint32_t item = 0;
        if (block != 0) {
            item = menu_.row_construct(block, id, word_at(m_, text_global), skater_windows_appearance_appearance_at(m_, self).field_254(), 2, skater_windows_appearance_appearance_at(m_, self).field_25c(), skater_windows_appearance_appearance_at(m_, self).field_260());
        }
        set_word_at(m_, self + slot, item);
    };
    auto swatch = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text_global, std::uint32_t index_offset) {
        const std::uint32_t block = operator_new(0x7c);
        std::uint32_t item = 0;
        if (block != 0) {
            item = h_.colour_text_construct(block, id, word_at(m_, text_global), skater_windows_appearance_appearance_at(m_, self).field_254(), 0x00525c00, 0x24, skater_windows_appearance_appearance_at(m_, self).field_25c(),
                                            skater_windows_appearance_appearance_at(m_, self).field_260());
        }
        set_word_at(m_, self + slot, item);
        h_.colour_text_set_index(item, byte_at(m_, skater + index_offset));
    };
    row(0xd4, 0x0b, 0x0052e6d4);
    row(0xd8, 0x08, 0x0052e6d8);
    swatch(0xdc, 4, 0x0052e6dc, 0x318);
    row(0xe0, 0x09, 0x0052e6e0);
    row(0xe4, 0x01, 0x0052e6e4);
    swatch(0xe8, 5, 0x0052e6ec, 0x319);
    swatch(0xec, 6, 0x0052e6f0, 0x31a);
    row(0xf0, 0x0a, 0x0052e6f4);
    swatch(0xf4, 7, 0x0052e6f8, 0x31b);
    row(0xf8, 0x02, 0x0052e6fc);
    row(0xfc, 0x03, 0x0052e700);
    memory = operator_new(0x54);
    std::uint32_t item = 0;
    if (memory != 0) {
        item = menu_.text_construct_from(memory, 0xffffffff, heading, skater_windows_appearance_appearance_at(m_, self).field_254());
    }
    skater_windows_appearance_appearance_at(m_, self).set_field_100(static_cast<Word>(item));
    for (std::uint32_t i = 0; i < kItemSlots; ++i) {
        const std::uint32_t child = word_at(m_, self + kItems + 4 * i);
        if (child != 0) {
            vcall_list("list_v6", skater_windows_appearance_appearance_at(m_, self).field_d0(), 6, {child});
        }
    }
    vcall_list("list_v2", skater_windows_appearance_appearance_at(m_, self).field_d0(), 2);
}

// 0x0042a8c0: delete the list, the two styles, every item and the two sprites, and write the seven ids back into the record.
void SkaterWindows::appearance_destroy(std::uint32_t self) {
    skater_windows_appearance_appearance_at(m_, self).set_field_0(static_cast<Word>(kAppearanceVt));
    if (skater_windows_appearance_appearance_at(m_, self).field_d0() != 0) {
        vcall_list("virtual_delete", skater_windows_appearance_appearance_at(m_, self).field_d0(), 0, {1});
    }
    if (skater_windows_appearance_appearance_at(m_, self).field_254() != 0) {
        vcall_item("virtual_delete", skater_windows_appearance_appearance_at(m_, self).field_254(), 0, {1});
    }
    if (skater_windows_appearance_appearance_at(m_, self).field_258() != 0) {
        vcall_item("virtual_delete", skater_windows_appearance_appearance_at(m_, self).field_258(), 0, {1});
    }
    for (std::uint32_t i = 0; i < kItemSlots; ++i) {
        const std::uint32_t item = word_at(m_, self + kItems + 4 * i);
        if (item != 0) {
            vcall_item("virtual_delete", item, 0, {1});
        }
    }
    if (skater_windows_appearance_appearance_at(m_, self).field_25c() != 0) {
        s_.note(kVirtualDelete, {skater_windows_appearance_appearance_at(m_, self).field_25c(), 1});
    }
    if (skater_windows_appearance_appearance_at(m_, self).field_260() != 0) {
        s_.note(kVirtualDelete, {skater_windows_appearance_appearance_at(m_, self).field_260(), 1});
    }
    for (std::uint32_t i = 0; i < 7; ++i) {
        set_word_at(m_, 0x0056a4ec + (custom_slot() * 0x20 + i) * 4, word_at(m_, self + kIds + 4 * i));
    }
    w_.window_destroy(self);
}

// 0x0042a9b0: poll: a sound for up / down, the confirm edge moves the list on (-1 leaves the page), the back edge leaves it.
std::uint32_t SkaterWindows::appearance_poll(std::uint32_t self) {
    auto pad = [&](std::uint32_t base) { return base + global_word(m_, kCurrentPad) * 0x16c; };
    if (byte_at(m_, pad(kPadUpDown)) != 0 || byte_at(m_, pad(kPadUpDown2)) != 0) {
        sfx(0x90);
    }
    if (byte_at(m_, pad(kPadConfirm)) != 0) {
        sfx(0x91);
        set_byte_at(m_, pad(kPadConfirm), 0);
        if (static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_appearance_appearance_at(m_, self).field_d0())) == -1) {
            return 1;
        }
        vcall_list("list_v5", skater_windows_appearance_appearance_at(m_, self).field_d0(), 5, {3});
    }
    if (byte_at(m_, pad(kPadBack)) != 0) {
        sfx(0x93);
        set_byte_at(m_, pad(kPadBack), 0);
        return 2;
    }
    return 0;
}

// 0x0042aab0: one frame: the list follows the pad; on left / right the row the list stands on steps its texture set or colour, the skater's
// textures are re-applied and the swatches refreshed.
void SkaterWindows::appearance_update(std::uint32_t self) {
    if (skater_windows_appearance_appearance_at(m_, self).field_14() != 0) {
        return;
    }
    const std::uint32_t edge = pad_edges(global_word(m_, kCurrentPad), 0, 6);
    const std::uint32_t list = skater_windows_appearance_appearance_at(m_, self).field_d0();
    vcall_list("list_v5", list, 5, {edge});
    vcall_list("list_v4", list, 4);
    if (edge != 0 && edge != 1) {
        return;
    }
    const std::int32_t id = static_cast<std::int32_t>(menu_.list_highlighted_id(skater_windows_appearance_appearance_at(m_, self).field_d0()));
    if (id != -1) {
        sfx(0x90);
    }
    // the position of the highlighted item among the slots (retail reads it, nothing in the cases below uses anything but the id)
    std::uint32_t found = 0;
    for (; found < kItemSlots; ++found) {
        const std::uint32_t item = word_at(m_, self + kItems + 4 * found);
        if (static_cast<std::int32_t>(word_at(m_, item + 8)) == id) {
            break;
        }
    }
    const std::int32_t dir = edge == 1 ? 1 : -1;
    const std::uint32_t holder = global_word(m_, kTextureManagerPointer);
    switch (id) {
    case 11: {
        std::int32_t tone = add(static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b0()), dir);
        skater_windows_appearance_appearance_at(m_, self).set_field_b0(static_cast<Word>(u(tone)));
        if (tone < 0) {
            skater_windows_appearance_appearance_at(m_, self).set_field_b0(2);
        } else if (tone >= 3) {
            skater_windows_appearance_appearance_at(m_, self).set_field_b0(0);
        }
        const std::int32_t root = tex_.find_root(holder, -1, static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b4()));
        const std::int32_t next = tex_.step(holder, -1, root, dir);
        skater_windows_appearance_appearance_at(m_, self).set_field_b4(static_cast<Word>(u(next)));
        skater_windows_appearance_appearance_at(m_, self).set_field_ac(static_cast<Word>(u(next)));
        body_group_apply(self, 0, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 8: {
        const std::int32_t next = tex_.step(holder, static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_ac()), static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b4()), dir);
        skater_windows_appearance_appearance_at(m_, self).set_field_b4(static_cast<Word>(u(next)));
        body_group_apply(self, 0, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 9: {
        const std::int32_t next = tex_.step(holder, -1, int_at(m_, self + kIds + 4), dir);
        set_word_at(m_, self + kIds + 4, u(next));
        set_word_at(m_, self + kIds + 8, u(next));
        body_group_apply(self, 1, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 1: {
        const std::int32_t next = tex_.step(holder, int_at(m_, self + kIds + 4), int_at(m_, self + kIds + 8), dir);
        set_word_at(m_, self + kIds + 8, u(next));
        body_group_apply(self, 1, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 10: {
        const std::int32_t next = tex_.step(holder, -1, int_at(m_, self + kIds + 0x10), dir);
        set_word_at(m_, self + kIds + 0x10, u(next));
        body_group_apply(self, 4, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        body_group_apply(self, 6, word_at(m_, self + kIds + 0x18), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 2: {
        const std::int32_t next = tex_.step(holder, -1, int_at(m_, self + kIds + 0x14), dir);
        set_word_at(m_, self + kIds + 0x14, u(next));
        body_group_apply(self, 5, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 3: {
        const std::int32_t next = tex_.step(holder, -1, int_at(m_, self + kIds + 0x18), dir);
        set_word_at(m_, self + kIds + 0x18, u(next));
        body_group_apply(self, 6, u(next), skater_windows_appearance_appearance_at(m_, self).field_b0());
        break;
    }
    case 5: {   // the second swatch row copies the colour index the first one is on
        for (std::uint32_t i = 0; i < kItemSlots; ++i) {
            const std::uint32_t item = word_at(m_, self + kItems + 4 * i);
            if (item != 0 && int_at(m_, item + 8) == 6) {
                h_.colour_text_set_index(item, static_cast<std::int32_t>(h_.colour_text_index(word_at(m_, self + kItems + 4 * found))));
            }
        }
        break;
    }
    default:
        break;
    }
    engine(0x00422d80, {skater_windows_appearance_appearance_at(m_, self).field_264(), word_at(m_, self + kIds + 8), word_at(m_, self + kIds + 0x10), word_at(m_, self + kIds + 0x14),
                        skater_windows_appearance_appearance_at(m_, self).field_b0()});
    appearance_refresh(self);
    w_.window_update(self);
}

// 0x0042ae30: the camera and viewport of the page's skater view. `camera` gets its rotation zeroed and an offset that depends on the row the
// list stands on; `viewport` the rectangle (words: width, height, ...) scaled to the screen.
void SkaterWindows::appearance_camera(std::uint32_t self, std::uint32_t camera, std::uint32_t viewport) {
    check(camera != 0, kAppMsgNoCamera);
    check(viewport != 0, kAppMsgNoViewport);
    set_half_at(m_, camera + 0x2c, 0);
    set_half_at(m_, camera + 0x2e, 0);
    set_half_at(m_, camera + 0x30, 0);
    set_word_at(m_, camera + 4, 0);
    set_word_at(m_, camera + 8, 0);
    set_word_at(m_, camera + 0x0c, 0);
    set_half_at(m_, viewport + 4, 0x100);
    set_half_at(m_, viewport + 6, 0x28);
    set_half_at(m_, viewport, 0x200);
    set_half_at(m_, viewport + 2, 0xca);
    set_half_at(m_, viewport + 0x0c, 0x6aa);
    auto scale_width = [&] {
        set_half_at(m_, viewport + 4, (static_cast<std::uint32_t>(half_at(m_, viewport + 4)) * global_word(m_, kScreenWidth)) >> 9);
        set_half_at(m_, viewport, (static_cast<std::uint32_t>(half_at(m_, viewport)) * global_word(m_, kScreenWidth)) >> 9);
    };
    auto divide = [&](std::uint32_t at) {
        const std::uint64_t product = static_cast<std::uint64_t>(static_cast<std::uint32_t>(half_at(m_, at)) * global_word(m_, kScreenHeight));
        set_half_at(m_, at, static_cast<std::uint32_t>(static_cast<std::uint32_t>(product) / 0xf0));
    };
    if (skater_windows_appearance_appearance_at(m_, self).field_14() != 0) {
        set_word_at(m_, camera + 8, 0xffffffbf);
        set_word_at(m_, camera + 0x0c, 0xfffffee0);
        set_half_at(m_, viewport + 6, 0);
        set_half_at(m_, viewport + 2, 0xf0);
        scale_width();
        set_half_at(m_, viewport + 6, 0);
        divide(viewport + 2);
        return;
    }
    switch (menu_.list_highlighted_id(skater_windows_appearance_appearance_at(m_, self).field_d0())) {
    case 4:
    case 8:
        set_word_at(m_, camera + 8, 0xffffff88);
        set_word_at(m_, camera + 0x0c, 0xffffff58);
        break;
    case 1:
    case 5:
    case 6:
    case 9:
        set_word_at(m_, camera + 8, 0xffffff8d);
        set_word_at(m_, camera + 0x0c, 0xffffff58);
        break;
    case 7:
    case 10:
        set_word_at(m_, camera + 8, 0xffffffe2);
        set_word_at(m_, camera + 0x0c, 0xffffff40);
        break;
    case 2:
    case 3:
        set_word_at(m_, camera + 8, 0xfffffff1);
        set_word_at(m_, camera + 0x0c, 0xffffff40);
        break;
    default:
        set_word_at(m_, camera + 8, 0xffffffbf);
        set_word_at(m_, camera + 0x0c, 0xfffffee0);
        set_half_at(m_, viewport + 6, 0);
        set_half_at(m_, viewport + 2, 0xf0);
        break;
    }
    scale_width();
    divide(viewport + 6);
    divide(viewport + 2);
}

// 0x0042b070: draw the list rows: the second column of each row shows the name of the texture set it stands for.
void SkaterWindows::appearance_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (skater_windows_appearance_appearance_at(m_, self).field_14() != 0) {
        return;
    }
    const std::uint32_t holder = global_word(m_, kTextureManagerPointer);
    const std::uint32_t props = skater_windows_appearance_appearance_at(m_, self).field_258();
    for (std::uint32_t slot = 0; slot < kItemSlots; ++slot) {
        const std::uint32_t item = word_at(m_, self + kItems + 4 * slot);
        if (item == 0) {
            continue;
        }
        const std::int32_t id = int_at(m_, item + 8);
        if (id == 4 || id == 5 || id == 6 || id == 7 || id == -1) {
            continue;
        }
        auto name_of = [&](std::uint32_t record_pointer) { return m_.cstring(record_pointer + 0x18, 0x40); };
        switch (id) {
        case 9: {
            const std::uint32_t record_pointer = tex_.find_by_id(holder, int_at(m_, self + kIds + 4));
            check(record_pointer != 0, kAppMsgNoTextureSetShort);
            set_column(item, name_of(record_pointer), 0x9b, props);
            for (std::uint32_t other = 0; other < kItemSlots; ++other) {
                const std::uint32_t target = word_at(m_, self + kItems + 4 * other);
                if (target != 0 && int_at(m_, target + 8) == 1) {
                    const std::string name = m_.cstring(record_pointer + 8, 0x40);
                    const bool plain = name == "noshirt" || name == "skinelbw";
                    set_column_at(target, m_.cstring(plain ? global_word(m_, 0x0052e6e8) : global_word(m_, 0x0052e6e4), 0x40), 0, 0, skater_windows_appearance_appearance_at(m_, self).field_254());
                }
            }
            break;
        }
        case 10: {
            const std::uint32_t record_pointer = tex_.find_by_id(holder, int_at(m_, self + kIds + 0x10));
            check(record_pointer != 0, kAppMsgNoTextureSetShort);
            set_column(item, name_of(record_pointer), 0xa0, props);
            break;
        }
        case 8: {
            const std::uint32_t record_pointer = tex_.find_by_id(holder, static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b4()));
            check(record_pointer != 0, kAppMsgNoTextureSetShort);
            set_column(item, name_of(record_pointer), 0xa0, props);
            break;
        }
        case 11:
            set_column(item, m_.cstring(global_word(m_, 0x0052e718), 0x40) + " " + std::to_string(add(static_cast<std::int32_t>(skater_windows_appearance_appearance_at(m_, self).field_b0()), 1)), 0xa0, props);
            break;
        case 2:
            if (byte_at(m_, item + 0x0c) != 0) {
                const std::uint32_t record_pointer = tex_.find_by_id(holder, int_at(m_, self + kIds + 0x14));
                check(record_pointer != 0, kAppMsgNoTextureSetShort);
                set_column(item, name_of(record_pointer), 0xa0, props);
            } else {
                set_column(item, std::string(), 0xa0, props);
            }
            break;
        case 3: {
            const std::uint32_t record_pointer = tex_.find_by_id(holder, int_at(m_, self + kIds + 0x18));
            set_column(item, name_of(record_pointer), 0x96, props);
            break;
        }
        case 1:
            if (word_at(m_, self + kIds + 8) != word_at(m_, self + kIds + 4)) {
                const std::uint32_t record_pointer = tex_.find_by_id(holder, int_at(m_, self + kIds + 8));
                set_column(item, name_of(record_pointer), 0xa0, props);
            } else {
                set_column(item, m_.cstring(global_word(m_, 0x0052e704), 0x40), 0xa0, props);
            }
            break;
        default:
            set_column(item, m_.cstring(global_word(m_, 0x0052e708), 0x40), 0xa0, props);
            break;
        }
        set_word_at(m_, word_at(m_, self + kItems + 4 * slot) + 0x2c, 1);
    }
    vcall_list("list_v3", skater_windows_appearance_appearance_at(m_, self).field_d0(), 3);
    w_.window_draw(self, add(skater_windows_appearance_appearance_at(m_, self).field_20(), x), add(skater_windows_appearance_appearance_at(m_, self).field_24(), y));
}

}  // namespace opentony::runtime::frontend
