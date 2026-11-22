#pragma once

#include "create_screens.hpp"
#include "screen_kit.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime::frontend {

// Phase 4.14b: the save-card screen (mmu.cpp 0x00476560..0x00479f60): the window the career, replay and park saves and loads run in. The global `MmuManager` (0x0056ad00) is one of these objects; its card manager
// (src/runtime/mmu_kit.hpp) is a sub-object at +0x1f8, so the card calls below are reported services (`engine_<address>`, the card manager first) and the screen is native over them.
//
// MmuScreen (0xdd0 bytes, vtable 0x00519308: 0 deleting destructor 0x00477910, 1 the state machine 0x004780d0, 2 draw 0x00479f60, then the window base's):
//   +0x0ac a Font object (0x14c bytes, a copy of s2tricks.fnt)   +0x1f8 the card manager (0xb4c bytes: records, names, buffers; see mmu_kit.hpp)
//   +0xd44 the banner text   +0xd48 / +0xd4c the "blocks needed" / "blocks free" texts   +0xd50 the delete hint (loads) or 0   +0xd54 the file list (a ScrollList)
//   +0xd58 / +0xd5c two styles   +0xd60 + 4 * i the 16 rows of the list   +0xda0 busy flag (1: scanning)   +0xda4 the display state (0 browse, 1 not enough room, 2 card inserted
//   warning, 3 overwrite question, 4 delete question, 5 name entry, 6 formatting, 7 deleting, 8 saving, 9 loading, 10 no files, 11..14 messages, 15 name exists)
//   +0xda8 the file type (1 career, 2 replay, 3 park)   +0xdac the confirm box   +0xdb0 the message text box   +0xdb4 the counter a message waits out
//   +0xdb8 the name entry   +0xdbc 1 when saving, 0 when loading   +0xdc0 the message timer   +0xdc4 the result (1 done, 2 back)   +0xdc8 the prompt group (saves)
//   +0xdcc the button flags (bit 0 select, 1 delete, 2 accept, 3 back)
// PromptGroup (0x208 bytes, vtable 0x005192e8): a Window. +0x0ac select, +0x0b0 delete, +0x0b4 accept, +0x0b8 back (prompt windows), +0x0bc a Font object (s2font2).
constexpr std::uint32_t kVtMmuScreen = 0x00519308;
constexpr std::uint32_t kVtPromptGroup = 0x005192e8;
constexpr std::uint32_t kMmuCard = 0x1f8;               // the card manager inside the screen

class MmuScreens final : public ScreenKit {
public:
    MmuScreens(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters, CreateScreens& create,
               create::CreateSpool& spool)
        : ScreenKit(memory, services, fonts, window, menu, skaters), create_(create), spool_(spool) {}

    std::uint32_t prompts_construct(std::uint32_t self);                                  // 0x00476560
    std::uint32_t prompts_delete(std::uint32_t self, std::uint32_t flag);                 // 0x004769a0
    void prompts_destroy(std::uint32_t self);                                             // 0x004769d0
    void prompts_set(std::uint32_t self, std::uint8_t shown, std::uint32_t mask);         // 0x00476a30
    std::uint32_t screen_construct(std::uint32_t self, std::uint32_t type, std::uint32_t argument, std::uint32_t saving);   // 0x00476b00
    std::uint32_t screen_delete(std::uint32_t self, std::uint32_t flag);                  // 0x00477910
    void screen_destroy(std::uint32_t self);                                              // 0x00477940
    void rebuild_list(std::uint32_t self);                                                // 0x00477b40
    void state_machine(std::uint32_t self);                                               // 0x004780d0 Front_SaveCardStateMachine
    void browse(std::uint32_t self);                                                      // 0x00478f00
    void show_banner(std::uint32_t self);                                                 // 0x004792f0
    void display(std::uint32_t self);                                                     // 0x004793a0
    void draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);                      // 0x00479f60

private:
    using Word = std::uint32_t;
    // the card manager's calls, reported with the card first
    Word card(Word self) const { return self + kMmuCard; }
    Word card_value(Word address, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), card(self));
        return engine_value(address, std::move(args));
    }
    void card_call(Word address, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), card(self));
        engine(address, std::move(args));
    }
    // a formatted line in a scratch buffer (the screens' stack locals)
    Word scratch(Word offset, const std::string& text) { return scratch_text(offset, text); }
    void clear_triggers() {
        engine(0x00489670, {0x0056aff8});
        engine(0x00489670, {0x0056b164});
    }

    // a local buffer of the retail function's frame, in the scratch region the probe maps stack pointers to
    Word local(Word offset, const std::string& text);
    // Text_Layout on the screen's message box (or its confirm box, which is a text box too)
    void layout_text(Word self, Word text, bool confirm_box = false);
    void ensure_scratch();
    // the message a table entry points at (a pointer variable at 0x0052e9xx)
    Word table_text(Word address) const;
    std::string table_string(Word address) const;
    // the card manager's Mmu_DescribeFile: the description and the block count it writes to the caller's locals (scripted words)
    void describe_file(Word self, Word index);
    // the pad poll (Menu_PadPoll(player, 0, 6)): the edge the pad shows
    std::int32_t pad_edge();
    Word pad_confirm() const;

    CreateScreens& create_;
    create::CreateSpool& spool_;
};

}  // namespace opentony::runtime::frontend
