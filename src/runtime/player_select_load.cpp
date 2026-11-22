#include "auto_views.hpp"
#include "player_select.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgBadState = 0x005345d8;

// LoadWindow fields (0x248 bytes, vtable 0x00519664)
constexpr std::uint32_t kState = 0xac;          // 0 idle, 1 waiting for the memory card, 2 choosing a skater
constexpr std::uint32_t kImage = 0xb0;          // a copy of the career save image (0x1dc4 bytes)
constexpr std::uint32_t kList = 0xb4;
constexpr std::uint32_t kRows = 0xb8;           // 0x60 row pointers: the title, four skater rows, the back row at +0xcc
constexpr std::uint32_t kStyleA = 0x238;        // the rows' style (s2tricks, grey)
constexpr std::uint32_t kStyleB = 0x23c;        // the title's style
constexpr std::uint32_t kArrowA = 0x240;
constexpr std::uint32_t kArrowB = 0x244;

constexpr std::uint32_t kCareerImage = 0x00568a68;
constexpr std::uint32_t kCustomRecords = 0x0056a488;
constexpr std::uint32_t kLoadPadStride = 0x16c;
constexpr std::uint32_t kPadCurrentGlobal = 0x0056a888;
constexpr std::uint32_t kLoadConfirm = 0x0056b029;
constexpr std::uint32_t kLoadBack = 0x0056aff9;
constexpr std::uint32_t kGlobalSpoolManager = 0x0056db4c;

}  // namespace

// 0x004a6f50: the image copy, the two styles, the arrows and a list: a scaled title, four "TEMP" rows and the back row.
std::uint32_t PlayerSelect::load_construct(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    player_select_load_load_at(m_, self).set_field_0(static_cast<Word>(kVtLoadWindow));
    player_select_load_load_at(m_, self).set_field_ac(0);
    s_.note(kAllocBuffer, {0x1dc4, 0, 1, 0});
    player_select_load_load_at(m_, self).set_field_b0(static_cast<Word>(static_cast<std::uint32_t>(s_.value(kAllocBuffer))));
    load_save_image(self);
    std::uint32_t memory = operator_new(0x17c);
    const std::uint32_t style_a = memory != 0 ? menu_.properties_construct(memory) : 0u;
    player_select_load_load_at(m_, self).set_field_238(static_cast<Word>(style_a));
    copy_font_named(style_a + 4, 0x005254e0);
    set_word_at(m_, style_a + 8, 1);
    f_.set_rgb(style_a + 4, 0x80, 0x80, 0x80);
    set_word_at(m_, style_a + 0x34, 0xc00);
    set_word_at(m_, style_a + 0x150, 0xc00);
    set_word_at(m_, style_a + 0x154, 0xc00);
    memory = operator_new(0x17c);
    const std::uint32_t style_b = memory != 0 ? menu_.properties_construct(memory) : 0u;
    player_select_load_load_at(m_, self).set_field_23c(static_cast<Word>(style_b));
    menu_.copy_font_fields(style_b + 4, style_a + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, style_b + offset, word_at(m_, style_a + offset));
    }
    set_word_at(m_, style_b + 0x158, 0x91);
    set_word_at(m_, style_b + 0x15c, 0x12);
    set_word_at(m_, style_b + 0x160, 0xf);
    auto arrow = [&](std::uint32_t slot, std::uint32_t file) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t sprite = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, sprite);
        s_.note(kSpriteInit, {sprite});
    };
    arrow(kArrowA, 0x00527c20);
    arrow(kArrowB, 0x00527c10);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kRows + 4 * i, 0);
    }
    memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, 0, 0x200, global_word(m_, kScreenFillHeight), 1, 1, 3, 0);
    }
    player_select_load_load_at(m_, self).set_field_b4(static_cast<Word>(list));
    memory = operator_new(0x58);
    std::uint32_t title = 0;
    if (memory != 0) {
        title = menu_.text_scaled_construct(memory, 0xfffffffe, global_word(m_, 0x0052e73c), player_select_load_load_at(m_, self).field_23c(), 1);
    }
    player_select_load_load_at(m_, self).set_field_b8(static_cast<Word>(title));
    vcall_list("list_v6", player_select_load_load_at(m_, self).field_b4(), 6, {title});
    for (std::uint32_t i = 0; i < 4; ++i) {
        memory = operator_new(0x54);
        const std::uint32_t row = memory != 0 ? menu_.text_construct(memory, i, 0x00527c68, player_select_load_load_at(m_, self).field_238()) : 0u;
        set_word_at(m_, self + kRows + 4 + 4 * i, row);
        vcall_list("list_v6", player_select_load_load_at(m_, self).field_b4(), 6, {row});
    }
    memory = operator_new(0x54);
    const std::uint32_t back = memory != 0 ? menu_.text_construct(memory, 0xffffffff, global_word(m_, 0x0052e5f8), player_select_load_load_at(m_, self).field_238()) : 0u;
    player_select_load_load_at(m_, self).set_field_cc(static_cast<Word>(back));
    vcall_list("list_v6", player_select_load_load_at(m_, self).field_b4(), 6, {back});
    vcall_list("list_arrows", player_select_load_load_at(m_, self).field_b4(), 1, {player_select_load_load_at(m_, self).field_240(), player_select_load_load_at(m_, self).field_244(), 5});
    vcall_list("list_v2", player_select_load_load_at(m_, self).field_b4(), 2);
    return self;
}

std::uint32_t PlayerSelect::load_delete(std::uint32_t self, std::uint32_t flag) {
    load_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004a7480: finish the spool, set up the memory card's load buffer (a card in the first slot), keep the career image and wait for the card.
void PlayerSelect::load_enter(std::uint32_t self) {
    engine(0x004b5300, {global_word(m_, kGlobalSpoolManager)});
    engine(0x0047a6c0, {1, 0, 1});
    engine(0x004b5350, {global_word(m_, kGlobalSpoolManager)});
    load_save_image(self);
    player_select_load_load_at(m_, self).set_field_ac(1);
}

// 0x004a74c0: the career image is put back, then everything the constructor made is deleted.
void PlayerSelect::load_destroy(std::uint32_t self) {
    player_select_load_load_at(m_, self).set_field_0(static_cast<Word>(kVtLoadWindow));
    load_restore_image(self);
    s_.note(kMemDelete, {player_select_load_load_at(m_, self).field_b0()});
    for (std::uint32_t slot : {kArrowA, kArrowB, kStyleA, kStyleB, kList}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRows + 4 * i));
        }
    }
    w_.window_destroy(self);
}

// 0x004a7590: the state machine: 1 the card screen (answers 1 a card is ready, 2 back), 2 the list of skaters on the card (confirm loads one or goes back).
std::uint32_t PlayerSelect::load_step(std::uint32_t self) {
    const std::uint32_t pad = global_word(m_, kPadCurrentGlobal) * kLoadPadStride;
    switch (player_select_load_load_at(m_, self).field_ac()) {
    case 0:
        return 0;
    case 1: {
        const std::uint32_t answer = engine_value(0x0047ac60);
        if (answer == 1) {
            engine(0x0047ae10, {1});
            player_select_load_load_at(m_, self).set_field_ac(2);
            return 0;
        }
        if (answer == 2) {
            engine(0x0047ae10, {2});
            load_restore_image(self);
            player_select_load_load_at(m_, self).set_field_ac(0);
            return 2;
        }
        return 0;
    }
    case 2: {
        const std::uint32_t list = player_select_load_load_at(m_, self).field_b4();
        s_.note(kSvcPadEdges, {global_word(m_, kPadCurrentGlobal), 0, 6});
        const std::uint32_t edges = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        vcall_list("list_v5", list, 5, {edges});
        if (byte_at(m_, pad + kLoadConfirm) != 0) {
            set_byte_at(m_, pad + kLoadConfirm, 0);
            const std::int32_t id = static_cast<std::int32_t>(menu_.list_highlighted_id(player_select_load_load_at(m_, self).field_b4()));
            if (id >= -2 && id < 0) {
                load_restore_image(self);
                return 2;
            }
            const std::uint32_t chosen = menu_.list_highlighted_id(player_select_load_load_at(m_, self).field_b4());
            if (byte_at(m_, kCustomRecords + 0x26 + chosen * 0x80) != 0) {
                sfx(0x91);
                load_load_skater(self, menu_.list_highlighted_id(player_select_load_load_at(m_, self).field_b4()));
                return 1;
            }
            sfx(0x92);
            return 0;
        }
        if (byte_at(m_, pad + kLoadBack) != 0) {
            set_byte_at(m_, pad + kLoadBack, 0);
            load_restore_image(self);
            load_enter(self);
            return 0;
        }
        return 0;
    }
    default:
        s_.note("assert", {kMsgBadState});
        return 0;
    }
}

// 0x004a7730 (virtual slot 2): the card screen's draw in state 1, the list's in state 2, then the window and the grey selection box.
void PlayerSelect::load_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    if (word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    switch (player_select_load_load_at(m_, self).field_ac()) {
    case 0:
        break;
    case 1:
        engine(0x0047ace0);
        break;
    case 2:
        vcall_list("list_v3", player_select_load_load_at(m_, self).field_b4(), 3);
        break;
    default:
        s_.note("assert", {kMsgBadState});
        break;
    }
    w_.window_draw(self, x, y);
    s_.note(kSvcSelectionBox, {0, 0, 0x200, 0xf0, 0x60, 0x60, 0x60});
}

// 0x004a77e0 (virtual slot 1): in state 2 each row shows its custom skater's name (or "<--EMPTY-->"), then the list lays out and snaps.
void PlayerSelect::load_update(std::uint32_t self) {
    if (word_at(m_, self + kElemHidden) != 0) {
        return;
    }
    if (player_select_load_load_at(m_, self).field_ac() == 2) {
        for (std::uint32_t i = 0; i < 0x60; ++i) {
            const std::uint32_t row = word_at(m_, self + kRows + 4 * i);
            if (row == 0) {
                continue;
            }
            const std::int32_t id = int_at(m_, row + 8);
            if (id == -2 || id == -1) {
                continue;
            }
            std::string line;
            if (byte_at(m_, kCustomRecords + 0x26 + u(id) * 0x80) != 0) {
                line = retail_format("%s", {m_.cstring(kCustomRecords + u(id) * 0x80, 0x80)}, {});
            } else {
                line = retail_format("<%s>", {m_.cstring(global_word(m_, 0x0052e744), 0x40)}, {});
            }
            vcall_item("item_set_text", row, 8, {scratch_line(line)});
        }
        vcall_list("list_v2", player_select_load_load_at(m_, self).field_b4(), 2);
        vcall_list("list_v4", player_select_load_load_at(m_, self).field_b4(), 4);
    }
    w_.window_update(self);
}

// 0x004a7890 / 0x004a78b0: the career image to the window's copy and back.
void PlayerSelect::load_save_image(std::uint32_t self) { m_.copy(word_at(m_, self + kImage), kCareerImage, 0x1dc4); }

void PlayerSelect::load_restore_image(std::uint32_t self) { m_.copy(kCareerImage, word_at(m_, self + kImage), 0x1dc4); }

// 0x004a78d0: the chosen custom skater goes into the active slot, then the career image is put back.
void PlayerSelect::load_load_skater(std::uint32_t self, std::uint32_t custom) {
    sk_.load_custom_into_active(static_cast<int>(custom));
    load_restore_image(self);
}

}  // namespace opentony::runtime::frontend
