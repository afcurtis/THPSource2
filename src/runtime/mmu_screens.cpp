#include "mmu_screens.hpp"
#include "mmu_views.hpp"

#include <algorithm>

// Phase 4.14b: the save-card screen (mmu.cpp 0x00476560..0x00479f60): see mmu_screens.hpp for the object. The window base, the menu toolkit, the font code and the confirm / text boxes are native
// (4.10); the card manager inside the screen and the two functions around the labels (`Mmu_TypeLabel`, `Mmu_ValidateSaveActionType`) are reported services.

namespace opentony::runtime::frontend {

using namespace mmu::views;

namespace {

using Word = std::uint32_t;

constexpr Word u(std::int32_t v) { return static_cast<Word>(v); }

// the screen's fields are the layout's (re/types save-runtime.yml): the card manager sub-object is the only offset the header keeps
static_assert(kMmuCard == guest::kRuntimeMmuScreen_card);

constexpr Word kScreenHeight = 0x00527d54;

// the stack locals the stubbed card calls write: what an argument that points into the caller's frame is recorded as (the probe's `stack_to`)
constexpr Word kScratchBase = 0x7ff00800;
constexpr Word kScratchLabel = kScratchBase + 0x900;           // Mmu_TypeLabel's output (36 bytes)
constexpr Word kScratchDescription = kScratchBase + 0x980;     // Mmu_DescribeFile's description (32 bytes)
constexpr Word kScratchBlocks = kScratchBase + 0x9c0;          // and its block count
constexpr Word kScratchText = kScratchBase + 0xa00;            // formatted lines (0x100 bytes each)

// assertion messages
constexpr Word kMsgNameMissing = 0x005354dc;
constexpr Word kMsgActionType = 0x005354c0;
constexpr Word kMsgIdle = 0x005345f0;
constexpr Word kMsgBadAction = 0x005354f0;
constexpr Word kMsgFileType = 0x0053556c;
constexpr Word kMsgDisplayStatus = 0x00535548;
constexpr Word kMsgCardStatus = 0x00535524;

// the card manager's methods (services: the card comes first)
constexpr Word kMapCardResult = 0x00475480;
constexpr Word kGetCardState = 0x00475620;
constexpr Word kCheckCardReady = 0x00475640;
constexpr Word kDeleteSaveFile = 0x00475660;
constexpr Word kScanSaveFiles = 0x00474d70;
constexpr Word kFreeBlocks = 0x00474d50;
constexpr Word kFileCount = 0x004741d0;
constexpr Word kCountOfType = 0x004741f0;
constexpr Word kDisplayName = 0x004742e0;
constexpr Word kFileType = 0x00474a50;
constexpr Word kDescribeFile = 0x00474ae0;
constexpr Word kNameTaken = 0x00474380;
constexpr Word kNameTakenByOther = 0x004744a0;
constexpr Word kFindFile = 0x004746d0;
constexpr Word kSaveFile = 0x004758e0;
constexpr Word kLoadFile = 0x00476200;
constexpr Word kCardConstruct = 0x00473e50;
constexpr Word kValidateType = 0x00473d20;
constexpr Word kTypeLabel = 0x0047b8e0;

}  // namespace

// ---- the prompt group ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00476560: four prompt windows (select, delete, accept, back) and the font they share
MmuScreens::Word MmuScreens::prompts_construct(Word self) {
    w_.window_construct(self, 0, 0, -1, -1);
    const RuntimeMmuPromptGroupView group(m_, self);
    font_construct(group.font_address());
    group.set_vtable(GuestPtr<void>{kVtPromptGroup});
    copy_font_named(group.font_address(), 0x0052544c);
    const RuntimeFontView font = group.font();
    font.set_horizontal_align(0);
    font.set_vertical_align(3);
    f_.set_rgb(group.font_address(), 0x80, 0x80, 0x80);
    font.set_shadow(1);
    font.set_spacing(1);
    font.set_field_10(1);
    font.set_shadow_x(1);
    font.set_shadow_y(1);
    font.set_scale(0x800);
    const struct {
        Word file;
        std::int32_t x;
    } prompts[4] = {{0x0052541c, 300}, {0x00535458, 200}, {0x0052540c, 400}, {0x00525400, 0x32}};
    for (Word i = 0; i < 4; ++i) {
        const Word window = prompt(prompts[i].file, 0x005252f0, group.font_address(), prompts[i].x, 0xdc);
        group.set_prompts(i, GuestPtr<RuntimeFrontElement>{window});
        w_.window_add(self, window);
        if (i == 1) {
            element_at(m_, window).set_pad(0x80);
        }
    }
    return self;
}

// 0x004769a0
MmuScreens::Word MmuScreens::prompts_delete(Word self, Word flag) {
    prompts_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004769d0
void MmuScreens::prompts_destroy(Word self) {
    const RuntimeMmuPromptGroupView group(m_, self);
    group.set_vtable(GuestPtr<void>{kVtPromptGroup});
    font_destroy(group.font_address());
    w_.window_destroy(self);
}

// 0x00476a30: the four prompts are hidden or shown together; the buttons named in `mask` sit at their place on the row
void MmuScreens::prompts_set(Word self, std::uint8_t shown, Word mask) {
    const RuntimeMmuPromptGroupView group(m_, self);
    for (Word i = 0; i < 4; ++i) {
        const RuntimeFrontElementView window = element_at(m_, group.prompts(i).address);
        window.set_hidden(shown == 0 ? 1u : 0u);
        window.set_target_y(static_cast<std::int32_t>(global_word(m_, kScreenHeight) + 0x14));
    }
    for (Word i = 0; i < 4; ++i) {
        if ((mask & (1u << i)) != 0) {
            element_at(m_, group.prompts(i).address).set_target_y(0xdc);
        }
    }
}

// ---- the screen -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x00476b00: the banner and the two block-count texts, the file list's two styles, the confirm box, the name entry and the message box; a save also builds the prompt group, a load the delete hint
MmuScreens::Word MmuScreens::screen_construct(Word self, Word type, Word argument, Word saving) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(screen.font_address());
    engine(kCardConstruct, {card(self)});
    screen.set_vtable(GuestPtr<void>{static_cast<Word>(kVtMmuScreen)});
    screen.set_file_type(type);
    screen.set_saving(saving);
    screen.set_counter(0);
    screen.set_timer(0);
    screen.set_result(0);
    screen.set_busy(1);
    screen.set_button_flags(0);
    copy_font_named(screen.font_address(), 0x005254e0);
    screen.font().set_horizontal_align(1);
    screen.font().set_vertical_align(3);
    f_.set_rgb(screen.font_address(), 0x80, 0x80, 0x80);
    screen.font().set_shadow(0);
    screen.font().set_spacing(2);
    screen.font().set_field_10(2);
    screen.font().set_scale(0xc00);
    auto text_element = [&](Word text, std::int32_t y) {
        const Word memory = operator_new(0x30);
        Word element = 0;
        if (memory != 0) {
            element = w_.text_construct(memory, text, screen.font_address(), 0x100, y, -1, -1);
        }
        w_.window_add(self, element);
        return element;
    };
    screen.set_banner(GuestPtr<RuntimeFrontElement>{static_cast<Word>(text_element(0x00535494, 0x32))});
    screen.set_blocks_needed(GuestPtr<RuntimeFrontElement>{static_cast<Word>(text_element(0x0053547c, 0xbe))});
    screen.set_blocks_free(GuestPtr<RuntimeFrontElement>{static_cast<Word>(text_element(0x00535468, 0xca))});
    screen.set_prompts(GuestPtr<RuntimeMmuPromptGroup>{0});
    screen.set_delete_hint(GuestPtr<RuntimeFrontElement>{0});
    if (argument == 0) {
        screen.set_delete_hint(GuestPtr<RuntimeFrontElement>{static_cast<Word>(text_element(global_word(m_, 0x0052e378), 0xb0))});
    } else {
        const Word memory = operator_new(0x208);
        Word group = 0;
        if (memory != 0) {
            group = prompts_construct(memory);
        }
        screen.set_prompts(GuestPtr<RuntimeMmuPromptGroup>{static_cast<Word>(group)});
        w_.window_add(self, group);
        prompts_set(group, 1, 0);
    }
    screen.set_file_list(GuestPtr<void>{0});
    Word memory = operator_new(0x17c);
    Word style = 0;
    if (memory != 0) {
        style = menu_.properties_construct(memory);
    }
    screen.set_style_matching(GuestPtr<RuntimeFrontMenuProperties>{static_cast<Word>(style)});
    menu_.copy_font_fields(style + 4, find_font(0x005254e0));
    props_at(m_, style).font().set_horizontal_align(1);
    props_at(m_, style).font().set_vertical_align(3);
    props_at(m_, style).font().set_spacing(2);
    props_at(m_, style).font().set_field_10(2);
    props_at(m_, style).font().set_shadow(0);
    f_.set_rgb(style + 4, 0x80, 0x80, 0x80);
    props_at(m_, style).font().set_scale(0xc00);
    props_at(m_, style).set_scale_normal(0xc00);
    props_at(m_, style).set_scale_small(0xc00);
    memory = operator_new(0x17c);
    Word second = 0;
    if (memory != 0) {
        second = menu_.properties_construct(memory);
    }
    screen.set_style_other(GuestPtr<RuntimeFrontMenuProperties>{static_cast<Word>(second)});
    menu_.copy_font_fields(second + 4, style + 4);
    props_at(m_, second).font().set_scale(0xc00);
    props_at(m_, second).set_field_158(0x60);
    props_at(m_, second).set_field_164(0x40);
    props_at(m_, second).set_field_15c(0x40);
    props_at(m_, second).set_field_168(0x40);
    props_at(m_, second).set_field_160(0x10);
    props_at(m_, second).set_field_16c(0x40);
    props_at(m_, second).set_scale_normal(0xc00);
    props_at(m_, second).set_scale_small(0xc00);
    for (Word i = 0; i < 0x10; ++i) {
        screen.set_rows(i, GuestPtr<void>{0});
    }
    memory = operator_new(0x428);
    Word confirm = 0;
    if (memory != 0) {
        confirm = create_.confirm_construct(memory, screen.font_address(), 0x00527c68, 0x100, 0x78);
    }
    screen.set_confirm_box(GuestPtr<void>{static_cast<Word>(confirm)});
    ensure_scratch();
    engine(kTypeLabel, {kScratchLabel, type});
    if (!s_.live()) {   // live: Mmu_TypeLabel has written the label; a replay's stub writes nothing, its recorded writes are the scripted values
        for (Word k = 0; k < 9; ++k) {
            set_word_at(m_, kScratchLabel + 4 * k, static_cast<Word>(s_.value(engine_name(kTypeLabel))));
        }
    }
    const Word length = static_cast<Word>(m_.cstring(kScratchLabel, 0x24).size());
    memory = operator_new(0x374);
    Word entry = 0;
    if (memory != 0) {
        entry = menu_.name_entry_construct(memory, screen.font_address(), 0x20 - static_cast<std::int32_t>(length), 0x100, 0x50, 0x100, 0x96, 1, 1, 1, 0, argument != 0 ? 1u : 0u);
    }
    screen.set_name_entry(GuestPtr<void>{static_cast<Word>(entry)});
    menu_.name_entry_set_prompt(entry, global_word(m_, 0x0052e710), 0x100, 0x3c);
    memory = operator_new(0x418);
    Word box = 0;
    if (memory != 0) {
        box = create_.box_construct(memory, screen.font_address(), 0x00527c68, 0x100, 0x78);
    }
    screen.set_message_box(GuestPtr<void>{static_cast<Word>(box)});
    return self;
}

// 0x00477910
MmuScreens::Word MmuScreens::screen_delete(Word self, Word flag) {
    screen_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00477940: everything the screen built goes, then the card manager, the font and the window
void MmuScreens::screen_destroy(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    screen.set_vtable(GuestPtr<void>{static_cast<Word>(kVtMmuScreen)});
    for (const Word slot : {screen.file_list().address, screen.style_matching().address, screen.style_other().address}) {
        if (slot != 0) {
            delete_object(slot);
        }
    }
    for (Word i = 0; i < 0x10; ++i) {
        if (screen.rows(i).address != 0) {
            delete_object(screen.rows(i).address);
        }
    }
    if (screen.confirm_box().address != 0) {
        create_.confirm_delete(screen.confirm_box().address, 1);
    }
    if (screen.name_entry().address != 0) {
        menu_.name_entry_delete(screen.name_entry().address, 1);
    }
    if (screen.message_box().address != 0) {
        create_.box_delete(screen.message_box().address, 1);
    }
    engine(0x00473f10, {card(self)});
    font_destroy(screen.font_address());
    w_.window_destroy(self);
}


// ---- helpers ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

MmuScreens::Word MmuScreens::table_text(Word address) const { return global_word(m_, address); }
std::string MmuScreens::table_string(Word address) const { return m_.cstring(global_word(m_, address), 0x100); }
MmuScreens::Word MmuScreens::pad_confirm() const { return 0x0056b029 + global_word(m_, kPadCurrent) * 0x16c; }

void MmuScreens::ensure_scratch() {
    m_.ensure_region(kScratchBase, 0x1000, 0x10);
}

MmuScreens::Word MmuScreens::local(Word offset, const std::string& text) {
    ensure_scratch();
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kScratchText + offset, bytes);
    return kScratchText + offset;
}

// Text_Layout on the screen's message box (or its confirm box, which is a text box too): the lines are broken to the box's font
void MmuScreens::layout_text(Word self, Word text, bool confirm_box) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    const Word box = confirm_box ? screen.confirm_box().address : screen.message_box().address;
    const Word font = word_at(m_, box + 0x10);
    spool_.layout(box, text, [&](const std::string& line) { return f_.text_width_of(font, line); });
}

void MmuScreens::describe_file(Word self, Word index) {
    ensure_scratch();
    engine(kDescribeFile, {card(self), index, kScratchDescription, kScratchBlocks});
    if (s_.live()) {   // Mmu_DescribeFile has written the description and the block count
        return;
    }
    const std::string name = engine_name(kDescribeFile);
    set_word_at(m_, kScratchBlocks, static_cast<Word>(s_.value(name)));
    for (Word k = 0; k < 8; ++k) {
        set_word_at(m_, kScratchDescription + 4 * k, static_cast<Word>(s_.value(name)));
    }
}

std::int32_t MmuScreens::pad_edge() {
    s_.note(kSvcPadEdges, {global_word(m_, kPadCurrent), 0, 6});
    return static_cast<std::int32_t>(s_.value(kSvcPadEdges));
}

// 0x00477b40: rebuild the list of files: one row per file (its name, and its size in blocks as a second column) and, when saving with room left, an "empty slot" row
void MmuScreens::rebuild_list(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    for (Word i = 0; i < 0x10; ++i) {
        if (screen.rows(i).address != 0) {
            delete_object(screen.rows(i).address);
            screen.set_rows(i, GuestPtr<void>{0});
        }
    }
    if (screen.file_list().address != 0) {
        delete_object(screen.file_list().address);
    }
    std::int32_t height = global_int(m_, kScreenHeight) / 2 - 4;
    if (screen.prompts().address == 0) {
        height = global_int(m_, kScreenHeight) / 2 - 0x12;
    }
    Word memory = operator_new(0x350);
    Word list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x10, 0x39, 0x1f0, u(height), 0, 4, 2, 0);
    }
    screen.set_file_list(GuestPtr<void>{static_cast<Word>(list)});
    const std::int32_t count = static_cast<std::int32_t>(card_value(kFileCount, self));
    std::int32_t rows = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        describe_file(self, u(i));
        std::string shown = m_.cstring(card_value(kDisplayName, self, {u(i)}), 0x80);
        f_.ellipsize_text(screen.style_matching().address + 4, shown, 0x114);
        const Word text = local(0x40, shown);
        const std::int32_t type = static_cast<std::int32_t>(card_value(kFileType, self, {u(i)}));
        memory = operator_new(0x100);
        Word row = 0;
        if (memory != 0) {
            row = menu_.row_construct(memory, u(i), text, (type == static_cast<std::int32_t>(screen.file_type()) ? screen.style_matching().address : screen.style_other().address), 3, 0, 0);
        }
        screen.set_rows(u(i), GuestPtr<void>{static_cast<Word>(row)});
        vcall_item("row_set_column", screen.rows(u(i)).address, 9, {kScratchDescription, 1, 0x11e, 0});
        vcall_list("list_v6", screen.file_list().address, 6, {screen.rows(u(i)).address});
        ++rows;
    }
    if (screen.saving() == 1) {
        const std::int32_t free_blocks = static_cast<std::int32_t>(card_value(kFreeBlocks, self));
        const std::int32_t needed = static_cast<std::int32_t>(engine_value(kValidateType, {screen.file_type(), 1}));
        if (needed <= free_blocks) {
            const Word line = local(0xc0, retail_format(m_.cstring(0x0053549c), {table_string(0x0052e9bc)}, {}));
            memory = operator_new(0x54);
            Word slot = 0;
            if (memory != 0) {
                slot = menu_.text_construct(memory, u(count), line, screen.style_matching().address);
            }
            screen.set_rows(u(count), GuestPtr<void>{static_cast<Word>(slot)});
            vcall_list("list_v6", screen.file_list().address, 6, {screen.rows(u(count)).address});
            ++rows;
        }
    }
    if (rows != 0) {
        vcall_list("list_v2", screen.file_list().address, 2);
    }
}

// 0x004792f0: the banner and the counters of a browsing screen
void MmuScreens::show_banner(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    vcall_list("list_v3", screen.file_list().address, 3);
    element_at(m_, screen.banner().address).set_hidden(0);
    if (screen.saving() == 1) {
        element_at(m_, screen.blocks_needed().address).set_hidden(0);
        element_at(m_, screen.blocks_free().address).set_hidden(0);
        if (screen.delete_hint().address != 0) {
            element_at(m_, screen.delete_hint().address).set_hidden(0);
        }
        w_.text_set_string(screen.banner().address, table_text(0x0052e9e4));
    } else {
        w_.text_set_string(screen.banner().address, table_text(0x0052e9e8));
    }
}

// 0x00478f00: the file list: navigation, and the button presses that open a file, ask to overwrite one, or ask for a name
void MmuScreens::browse(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    const std::int32_t edge = pad_edge();
    vcall_list("list_v5", screen.file_list().address, 5, {u(edge)});
    vcall_list("list_v4", screen.file_list().address, 4);
    const bool delete_key = engine_value(0x004e41d0, {0}) == 0xd3;
    if (edge == 2 || edge == 3) {
        sfx(0x11f);
    }
    const Word player = global_word(m_, kPadCurrent);
    if (screen.saving() == 0) {
        if (byte_at(m_, pad_confirm()) != 0) {
            sfx(0xb4);
            set_byte_at(m_, pad_confirm(), 0);
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            if (card_value(kFileType, self, {id}) == screen.file_type()) {
                screen.set_display_state(9);
                screen.set_counter(3);
            }
        }
    } else if (screen.saving() == 1) {
        if (byte_at(m_, pad_confirm()) != 0) {
            set_byte_at(m_, pad_confirm(), 0);
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            if (id == card_value(kFileCount, self)) {
                screen.set_display_state(5);
                std::string name;
                Word next = 0;
                do {
                    const Word type = screen.file_type();
                    std::string label;
                    if (type == 1) {
                        label = retail_format(m_.cstring(0x0053551c), {}, {});
                    } else if (type == 2) {
                        label = retail_format(m_.cstring(0x00535514), {}, {});
                    } else if (type == 3) {
                        label = retail_format(m_.cstring(0x0053550c), {}, {});
                    }
                    name = label + static_cast<char>('A' + next);
                    ++next;
                    local(0x100, name);
                } while (card_value(kNameTaken, self, {kScratchText + 0x100}) != 0);
                menu_.name_entry_set_name(screen.name_entry().address, kScratchText + 0x100);
                sfx(0xb4);
            } else if (card_value(kFileType, self, {id}) == screen.file_type()) {
                create_.confirm_highlight(screen.confirm_box().address, 0);
                screen.set_display_state(3);
                const Word highlighted = menu_.list_highlighted_id(screen.file_list().address);
                menu_.name_entry_set_name(screen.name_entry().address, card_value(kDisplayName, self, {highlighted}));
                sfx(0xb4);
            } else {
                sfx(0xbd);
            }
        }
        if (byte_at(m_, 0x0056b009 + player * 0x16c) != 0 || delete_key) {
            set_byte_at(m_, 0x0056b009 + player * 0x16c, 0);
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            if (id == card_value(kFileCount, self)) {
                sfx(0xbd);
            } else {
                sfx(0xb4);
                create_.confirm_highlight(screen.confirm_box().address, 0);
                screen.set_display_state(4);
            }
        }
    } else {
        check(false, kMsgBadAction);
    }
}

// 0x004780d0 Front_SaveCardStateMachine: one frame of the screen
void MmuScreens::state_machine(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    const Word state_now = screen.display_state();
    const bool message_state = state_now == 0xd || state_now == 0xb;
    if (screen.result() != 0) {
        clear_triggers();
        return;
    }
    const Word before = screen.busy();
    if (!message_state) {
        card_call(kMapCardResult, self, {screen.busy_address()});
        if (!s_.live()) {   // live: Mmu_MapCardResult wrote through the pointer (or left the word as it was); a replay's stub's write is the scripted value
            const std::string name = engine_name(kMapCardResult);
            screen.set_busy(static_cast<Word>(s_.value(name)));
        }
    }
    if (screen.busy() == 1) {
        clear_triggers();
        w_.window_update(self);
        return;
    }
    if (screen.busy() == 0 && screen.busy() != before && card_value(kGetCardState, self) == 0) {
        if (screen.saving() == 1) {
            card_value(kScanSaveFiles, self, {0});
            rebuild_list(self);
            const std::int32_t free_blocks = static_cast<std::int32_t>(card_value(kFreeBlocks, self));
            const std::int32_t needed = static_cast<std::int32_t>(engine_value(kValidateType, {screen.file_type(), 1}));
            if (free_blocks < needed && static_cast<std::int32_t>(card_value(kFileCount, self)) == 0) {
                screen.set_display_state(1);
            } else {
                const bool park_known = global_byte(m_, 0x0056acfb) != 0 && global_word(m_, 0x0056acfc) == 1;
                if ((screen.file_type() == 1 && global_byte(m_, 0x0056acf9) != 0) || (screen.file_type() == 3 && park_known)) {
                    Word name = 0;
                    Word flag = 0;
                    if (screen.file_type() == 3 && park_known) {
                        name = 0x0056abd8;
                        flag = 0x0056acfb;
                    } else if (screen.file_type() == 1 && global_byte(m_, 0x0056acf9) != 0) {
                        name = 0x0056abf8;
                        flag = 0x0056acf9;
                    }
                    check(byte_at(m_, name) != 0, kMsgNameMissing);
                    const std::int32_t found = static_cast<std::int32_t>(card_value(kFindFile, self, {name}));
                    if (found == -1) {
                        set_byte_at(m_, flag, 0);
                        screen.set_display_state(0);
                    } else {
                        menu_.list_highlight(screen.file_list().address, u(found));
                        Word id = menu_.list_highlighted_id(screen.file_list().address);
                        menu_.name_entry_set_name(screen.name_entry().address, card_value(kDisplayName, self, {id}));
                        id = menu_.list_highlighted_id(screen.file_list().address);
                        const Word shown = card_value(kDisplayName, self, {id});
                        const std::string text = m_.cstring(shown, 0x100);
                        std::vector<std::uint8_t> bytes(text.begin(), text.end());
                        bytes.push_back(0);
                        m_.write(0x0056ac78, bytes);
                        screen.set_display_state(8);
                        screen.set_counter(3);
                    }
                } else {
                    screen.set_display_state(0);
                    if (global_word(m_, 0x006a1420) != 0) {
                        engine(0x004dc090, {0x0053d7b4});
                        menu_.name_entry_set_name(screen.name_entry().address, 0x0053d7b4);
                        screen.set_display_state(5);
                    }
                }
            }
        } else {
            check(screen.saving() == 0, kMsgActionType);
            card_value(kScanSaveFiles, self, {screen.file_type()});
            rebuild_list(self);
            screen.set_display_state(card_value(kCountOfType, self, {screen.file_type()}) == 0 ? 10u : 0u);
        }
    }
    check(screen.busy() == 0, kMsgIdle);
    if (card_value(kGetCardState, self) != 0) {
        clear_triggers();
        w_.window_update(self);
        return;
    }
    element_at(m_, screen.banner().address).set_hidden(0);
    const Word confirm_edge = 0x0056b029 + global_word(m_, kPadCurrent) * 0x16c;
    auto poll_confirm = [&]() {
        element_at(m_, screen.banner().address).set_hidden(1);
        const std::int32_t edge = pad_edge();
        const Word answer = create_.confirm_poll(screen.confirm_box().address, edge, -1);
        create_.confirm_update(screen.confirm_box().address);
        return answer;
    };
    switch (screen.display_state()) {
    case 0:
        browse(self);
        break;
    case 2: {
        const Word answer = poll_confirm();
        if (answer == 1) {
            screen.set_display_state(6);
            screen.set_counter(3);
        } else if (answer == 2) {
            screen.set_result(2);
        }
        break;
    }
    case 3: {
        const Word answer = poll_confirm();
        if (answer == 1) {
            screen.set_display_state(5);
        } else if (answer == 2) {
            screen.set_display_state(0);
        }
        if (global_word(m_, 0x006a1420) != 0) {
            if (answer == 1) {
                screen.set_display_state(8);
            } else if (answer == 2) {
                screen.set_display_state(5);
            }
        }
        break;
    }
    case 4: {
        const Word answer = poll_confirm();
        if (answer == 1) {
            screen.set_display_state(7);
        } else if (answer == 2) {
            screen.set_display_state(0);
        }
        break;
    }
    case 5: {
        const std::int32_t edge = pad_edge();
        const Word answer = menu_.name_entry_handle_input(screen.name_entry().address, u(edge));
        if (answer == 1) {
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            const Word name = menu_.name_entry_name(screen.name_entry().address);
            if (card_value(kNameTakenByOther, self, {name, id}) == 0) {
                screen.set_display_state(8);
                screen.set_counter(3);
                const std::string text = m_.cstring(menu_.name_entry_name(screen.name_entry().address), 0x100);
                std::vector<std::uint8_t> bytes(text.begin(), text.end());
                bytes.push_back(0);
                m_.write(0x0056ac78, bytes);
                element_at(m_, screen.banner().address).set_hidden(0);
            } else {
                screen.set_display_state(0xf);
                if (global_word(m_, 0x006a1420) != 0) {
                    screen.set_display_state(3);
                }
            }
        } else if (answer == 2) {
            screen.set_display_state(0);
            if (global_word(m_, 0x006a1420) != 0) {
                screen.set_display_state(0xd);
                screen.set_result(2);
            }
        }
        break;
    }
    case 6:
        if (screen.counter() < 1) {
            if (card_value(kCheckCardReady, self) == 0) {
                layout_text(self, table_text(0x0052e9d4));
                screen.set_display_state(0xd);
            } else {
                layout_text(self, table_text(0x0052e9d8));
                screen.set_display_state(0xc);
            }
        }
        break;
    case 7:
        if (screen.counter() < 1) {
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            if ((card_value(kDeleteSaveFile, self, {id}) & 0xff) == 0) {
                layout_text(self, table_text(0x0052e9dc));
                screen.set_display_state(0xd);
            } else {
                const Word shown = card_value(kDisplayName, self, {menu_.list_highlighted_id(screen.file_list().address)});
                std::string name = retail_format(m_.cstring(shown, 0x100), {}, {});
                f_.ellipsize_text(screen.font_address(), name, 400);
                const Word line = local(0x40, retail_format(m_.cstring(global_word(m_, 0x0052e374), 0x100), {name}, {}));
                screen.set_display_state(0xe);
                layout_text(self, line);
                screen.set_timer(0x2d);
            }
        }
        break;
    case 8:
        if (screen.counter() < 1) {
            const Word type = screen.file_type();
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            const Word name = menu_.name_entry_name(screen.name_entry().address);
            if ((card_value(kSaveFile, self, {name, id, type}) & 0xff) == 0) {
                layout_text(self, table_text(0x0052e9c8));
                screen.set_display_state(0xd);
            } else {
                layout_text(self, table_text(0x0052e9cc));
                screen.set_display_state(0xb);
                screen.set_timer(0x2d);
            }
        }
        break;
    case 9:
        if (screen.counter() < 1) {
            const Word type = screen.file_type();
            const Word id = menu_.list_highlighted_id(screen.file_list().address);
            if ((card_value(kLoadFile, self, {id, type}) & 0xff) == 0) {
                layout_text(self, table_text(0x0052e9d0));
                screen.set_display_state(0xd);
            } else {
                screen.set_result(1);
            }
        }
        break;
    case 0xb:
        screen.set_timer(static_cast<std::int32_t>(static_cast<Word>(screen.timer()) - 1));
        if (screen.timer() < 0) {
            screen.set_result(1);
        }
        if (byte_at(m_, confirm_edge) != 0) {
            sfx(0xb4);
            set_byte_at(m_, confirm_edge, 0);
            screen.set_result(1);
        }
        break;
    case 0xc:
        if (byte_at(m_, confirm_edge) != 0) {
            sfx(0xb4);
            set_byte_at(m_, confirm_edge, 0);
            screen.set_busy(1);
        }
        break;
    case 0xd:
        if (byte_at(m_, confirm_edge) != 0) {
            sfx(0xb4);
            set_byte_at(m_, confirm_edge, 0);
            screen.set_result(2);
        }
        break;
    case 0xe:
        screen.set_timer(static_cast<std::int32_t>(static_cast<Word>(screen.timer()) - 1));
        if (screen.timer() < 0 || byte_at(m_, confirm_edge) != 0) {
            if (byte_at(m_, confirm_edge) != 0) {
                sfx(0xb4);
            }
            set_byte_at(m_, confirm_edge, 0);
            engine(0x004e8cf0, {0});
            screen.set_busy(1);
        }
        break;
    case 0xf:
        if (byte_at(m_, confirm_edge) != 0) {
            sfx(0xb4);
            set_byte_at(m_, confirm_edge, 0);
            screen.set_display_state(5);
        }
        break;
    default:
        break;
    }
    clear_triggers();
    w_.window_update(self);
}

// 0x004793a0: show what the state asks for: the list or a message, the name entry, the confirm box; the prompts follow the button flags
void MmuScreens::display(Word self) {
    const RuntimeMmuScreenView screen = screen_at(m_, self);
    element_at(m_, screen.banner().address).set_hidden(1);
    element_at(m_, screen.blocks_needed().address).set_hidden(1);
    element_at(m_, screen.blocks_free().address).set_hidden(1);
    if (screen.delete_hint().address != 0) {
        element_at(m_, screen.delete_hint().address).set_hidden(1);
    }
    screen.set_button_flags(0);
    auto flag = [&](Word bits) { screen.set_button_flags(screen.button_flags() | bits); };
    auto box_draw = [&]() { create_.box_draw(screen.message_box().address); };
    auto confirm_draw = [&]() { create_.confirm_draw(screen.confirm_box().address); };
    auto name_text = [&]() {
        return m_.cstring(card_value(kDisplayName, self, {menu_.list_highlighted_id(screen.file_list().address)}), 0x100);
    };
    if (screen.busy() == 0) {
        const Word card_state = card_value(kGetCardState, self);
        switch (card_state) {
        case 0:
            switch (screen.display_state()) {
            case 0:
                if (global_word(m_, 0x006a1420) == 0) {
                    screen.set_button_flags(0xd);
                    if (screen.saving() == 1) {
                        flag(2);
                        if (screen.delete_hint().address != 0) {
                            element_at(m_, screen.delete_hint().address).set_hidden(0);
                        }
                    }
                    show_banner(self);
                } else {
                    screen.set_display_state(5);
                }
                break;
            case 1: {
                const std::int32_t needed = static_cast<std::int32_t>(engine_value(kValidateType, {screen.file_type(), 1}));
                const std::int32_t free_blocks = static_cast<std::int32_t>(card_value(kFreeBlocks, self));
                const std::int32_t missing = needed - free_blocks;
                const Word plural_missing = missing == 1 ? 0x0055fe10 : 0x005354bc;
                const Word plural_needed = needed == 1 ? 0x0055fe10 : 0x005354bc;
                const std::string line = retail_format(m_.cstring(global_word(m_, 0x0052e9a4), 0x100), {m_.cstring(plural_needed, 4), m_.cstring(plural_missing, 4)}, {needed, missing});
                layout_text(self, local(0x100, line));
                box_draw();
                flag(8);
                break;
            }
            case 2:
                layout_text(self, table_text(0x0052e9ac), true);
                confirm_draw();
                flag(5);
                break;
            case 3:
            case 4: {
                std::string shown;
                if (global_word(m_, 0x006a1420) == 0) {
                    shown = retail_format(name_text(), {}, {});
                } else {
                    shown = retail_format(m_.cstring(menu_.name_entry_name(screen.name_entry().address), 0x100), {}, {});
                }
                f_.ellipsize_text(screen.font_address(), shown, 400);
                const Word format = word_at(m_, screen.display_state() == 3 ? 0x0052e9b0 : 0x0052e9b4);
                layout_text(self, local(0x180, retail_format(m_.cstring(format, 0x100), {shown}, {})), true);
                confirm_draw();
                flag(5);
                break;
            }
            case 5:
                menu_.name_entry_draw(screen.name_entry().address, 0, 0);
                if (global_word(m_, 0x00561bfc) == 0x12) {
                    flag(0xc);
                }
                break;
            case 6:
                layout_text(self, table_text(0x0052e9c4));
                box_draw();
                if (screen.counter() > 0) {
                    screen.set_counter(static_cast<std::int32_t>(static_cast<Word>(screen.counter()) - 1));
                }
                break;
            case 7: {
                std::string shown = retail_format(name_text(), {}, {});
                f_.ellipsize_text(screen.font_address(), shown, 400);
                layout_text(self, local(0x100, retail_format(m_.cstring(global_word(m_, 0x0052e370), 0x100), {shown}, {})));
                break;
            }
            case 8:
                layout_text(self, local(0x180, retail_format(m_.cstring(global_word(m_, 0x0052e9c0), 0x100), {m_.cstring(0x0056ac78, 0x100)}, {})));
                box_draw();
                if (screen.counter() > 0) {
                    screen.set_counter(static_cast<std::int32_t>(static_cast<Word>(screen.counter()) - 1));
                }
                break;
            case 9: {
                const Word id = menu_.list_highlighted_id(screen.file_list().address);
                const std::string shown = m_.cstring(card_value(kDisplayName, self, {id}), 0x100);
                layout_text(self, local(0x180, retail_format(m_.cstring(global_word(m_, 0x0052e9e0), 0x100), {shown}, {})));
                box_draw();
                if (screen.counter() > 0) {
                    screen.set_counter(static_cast<std::int32_t>(static_cast<Word>(screen.counter()) - 1));
                }
                break;
            }
            case 10: {
                const Word type = screen.file_type();
                Word label = 0;
                if (type == 1) {
                    label = 0x00534178;
                } else if (type == 2) {
                    label = 0x00535584;
                } else if (type == 3) {
                    label = 0x0053558c;
                } else {
                    check(false, kMsgFileType);
                }
                layout_text(self, local(0x100, retail_format(m_.cstring(global_word(m_, 0x0052e9a8), 0x100), {label != 0 ? m_.cstring(label, 0x20) : std::string()}, {})));
                box_draw();
                flag(8);
                break;
            }
            case 0xb:
            case 0xc:
            case 0xd:
            case 0xe:
                box_draw();
                flag(4);
                break;
            case 0xf: {
                const std::string name = m_.cstring(menu_.name_entry_name(screen.name_entry().address), 0x100);
                layout_text(self, local(0x100, retail_format(m_.cstring(global_word(m_, 0x0052e364), 0x100), {name}, {})));
                box_draw();
                flag(4);
                break;
            }
            default:
                check(false, kMsgDisplayStatus);
            }
            break;
        case 1:
            layout_text(self, table_text(0x0052e9f0));
            box_draw();
            flag(8);
            break;
        case 2:
            if (screen.saving() == 1) {
                screen.set_display_state(2);
            } else {
                screen.set_display_state(0xd);
                layout_text(self, table_text(0x0052e298));
                box_draw();
                flag(8);
            }
            break;
        case 3:
            layout_text(self, table_text(0x0052e9f4));
            box_draw();
            flag(8);
            break;
        default:
            check(false, kMsgCardStatus);
        }
    } else if (screen.busy() == 1) {
        layout_text(self, table_text(0x0052e9a0));
        box_draw();
        flag(8);
    }
    if (screen.prompts().address != 0) {
        prompts_set(screen.prompts().address, screen.button_flags() != 0 ? 1 : 0, screen.button_flags());
    }
    if (screen.saving() == 1) {
        const Word slots = engine_value(kValidateType, {screen.file_type(), 1});
        w_.text_set_string_from(screen.blocks_needed().address, retail_format(m_.cstring(0x0052a56c, 0x20), {table_string(0x0052e9f8)}, {static_cast<std::int32_t>(slots)}));
        const Word room = card_value(kFreeBlocks, self);
        w_.text_set_string_from(screen.blocks_free().address, retail_format(m_.cstring(0x0052a56c, 0x20), {table_string(0x0052e9fc)}, {static_cast<std::int32_t>(room)}));
    }
}

// 0x00479f60: the screen draws what its state shows, then the window
void MmuScreens::draw(Word self, Word x, Word y) {
    display(self);
    w_.window_draw(self, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
}

}  // namespace opentony::runtime::frontend
