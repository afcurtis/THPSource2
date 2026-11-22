#include "auto_views.hpp"
#include "level_editor.hpp"

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor's modes (+0x194): 0 a piece in hand, 1 an item (a gap), 2 a menu over the pieces, 3 a menu over the items, 4 adjusting a gap, 5 the pad is out.  The state of the
// menus (+0x188): 0 none, 1 the main menu, 2 saving, 3 loading, 4 the saved parks, 5 leaving, 6 the theme, 7 the size, 8 the confirm, 9 the scores, 10 saving a park, 11 loading
// a park, 12 saving before leaving, 13 the gap menu, 14 naming a gap, 15 its points, 16 its type, 17 leaving the gap menu.

namespace {
constexpr std::uint32_t kFontIngame = 0x005287a4;          // "ingame1.fnt"
constexpr std::uint32_t kInputActions = 0x0056aff8;
constexpr std::uint32_t kMenuText = 0x0055fe10;            // an empty text the menus start with
constexpr std::uint32_t kMainMenuTexts = 0x0052e834;       // pointers: Continue, Test Play, (0x52e83c is unused) New Park, Set Theme, two more, then Pre-made parks and Exit
constexpr std::uint32_t kYes = 0x0052e410;
constexpr std::uint32_t kNo = 0x0052e414;
constexpr std::uint32_t kPointValues = 0x005286bc;
constexpr std::uint32_t kGapTypes = 0x005286fc;
constexpr std::uint32_t kFmtDecimal = 0x00525764;          // "%d"
constexpr std::uint32_t kFmtString = 0x005256dc;           // "%s"
constexpr std::uint32_t kScoreTable = 0x0056a1fc;          // eight records of 8 bytes: three initials, a zero byte and a score
constexpr std::uint32_t kReplayFlag = 0x00561ca8;
constexpr std::uint32_t kTwoSkaters = 0x00561c70;
constexpr std::uint32_t kMenuOpen = 0x0056a924;
constexpr std::uint32_t kPadOut = 0x0056b144;
constexpr std::uint32_t kPadGapMenu = 0x0056b0d9;
constexpr std::uint32_t kPadPlace = 0x0056b029;
constexpr std::uint32_t kPadRemove = 0x0056aff9;
constexpr std::uint32_t kMsgNoGap = 0x0052a5fc;
constexpr std::uint32_t kMsgBadState = 0x0052a5b8;
constexpr std::uint32_t kMsgUnknownState = 0x0052a5a8;
constexpr std::uint32_t kConfirmErase = 0x0052e884;
constexpr std::uint32_t kConfirmSave = 0x0052e880;
constexpr std::uint32_t kStrClearScores = 0x0052a5e4;
constexpr std::uint32_t kStrSaveScores = 0x0052a5d0;
constexpr std::uint32_t kStrNoScores = 0x0052a5c8;
constexpr std::uint32_t kFmtGapName = 0x0052a5a0;          // "%s"

// the numbers a menu answers with, written where the replay keeps the array retail builds on its stack
void put_ids(RetailMemory& m, const std::vector<std::uint32_t>& ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        guest::write_scalar<std::uint32_t>(m, kScratchIds + 4 * static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(ids[i]));
    }
}
}  // namespace

// The menu wrapper (0x00472ef0) over the arrays a state builds on its stack: the texts' pointers at kScratchTexts and the numbers at `ids`.  The call reports the texts the
// pointers name (joined by 0x1f), a 0x1e, and the bytes of the numbers.
std::uint32_t LevelEditor::menu_wrapper(const std::vector<std::uint32_t>& texts, std::uint32_t ids, std::uint32_t flag) {
    std::string text;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        set_word_at(m_, kScratchTexts + 4 * static_cast<std::uint32_t>(i), texts[i]);
        if (i != 0) {
            text.push_back('\x1f');
        }
        text += m_.cstring(texts[i], 0x100);
    }
    text.push_back('\x1e');
    text += bytes_text(ids, 4 * static_cast<std::uint32_t>(texts.size()));
    const std::uint32_t block = operator_new(0x290);
    if (block == 0) {
        return 0;
    }
    return engine_value(0x00472ef0, {block, kScratchTexts, ids, static_cast<std::uint32_t>(texts.size()), kFontIngame, flag}, text);
}

// 0x00434750: the editor's menus, one state at a time (the states are listed at the top of the file).  A state with no menu on the screen builds its menu; with one, it waits for
// the menu to be done and moves on by what was chosen.  A menu is a wrapper over a list; its virtual methods are +0 delete, +4 step, +8 is-it-done, +0xc what was chosen.
void LevelEditor::run_menus(std::uint32_t self) {
    const std::uint32_t ui = word_at(m_, self + 0x1a8);
    const std::uint32_t item = global_word(m_, kItems) + word_at(m_, self + 0x1a0) * 0x2c;
    auto state = [&](std::uint32_t value) { set_word_at(m_, self + 0x188, value); };
    auto last = [&](std::uint32_t value) { set_word_at(m_, self + 0x18c, value); };
    auto close_menu = [&]() {
        if (word_at(m_, self + 0x30) != 0) {
            (void)virtual_call(word_at(m_, self + 0x30), 0, {1});
        }
        set_word_at(m_, self + 0x30, 0);
    };
    auto done = [&]() { return (virtual_call(word_at(m_, self + 0x30), 2) & 0xff) != 0; };
    auto chosen = [&]() { return virtual_call(word_at(m_, self + 0x30), 3); };
    auto build = [&](const std::vector<std::uint32_t>& texts, const std::vector<std::uint32_t>& ids, std::uint32_t flag) {
        put_ids(m_, ids);
        set_word_at(m_, self + 0x30, menu_wrapper(texts, kScratchIds, flag));
    };
    auto text_at = [&](std::uint32_t address) { return word_at(m_, address); };
    auto clear_scores = [&]() {
        for (std::uint32_t at = 0; at < 8; ++at) {
            const std::uint32_t record = kScoreTable + 8 * at;
            set_half_at(m_, record, 0x4141);
            set_byte_at(m_, record + 2, 0x41);
            set_byte_at(m_, record + 3, 0);
            set_word_at(m_, record + 4, 0);
        }
    };
    auto delete_buffer = [&](std::uint32_t offset) {
        free_block(word_at(m_, self + offset));
        set_word_at(m_, self + offset, 0);
    };
    auto delete_park_menu = [&]() {
        if (word_at(m_, self + 0x190) != 0) {
            (void)virtual_call(word_at(m_, self + 0x190), 0, {1});
        }
        set_word_at(m_, self + 0x190, 0);
    };
    auto park_menu = [&](std::uint32_t mode, std::uint32_t buffer, std::uint32_t size) {
        const std::uint32_t block = operator_new(0x144);
        set_word_at(m_, self + 0x190, block != 0 ? engine_value(0x004329d0, {block, mode, buffer, size, 0}) : 0);
    };
    auto parse = [&]() {
        engine(0x0043c050, {word_at(m_, self + 0x18), self + 0x10});
        if (replay()) {
            set_word_at(m_, self + 0x10, static_cast<std::uint32_t>(s_.value("engine_0043c050")));
            set_word_at(m_, self + 0x14, static_cast<std::uint32_t>(s_.value("engine_0043c050")));
        }
        set_word_at(m_, self + 4, word_at(m_, self + 0x14));
        set_word_at(m_, self + 8, word_at(m_, self + 0x10));
        set_byte_at(m_, self + 0xc, 1);
    };
    // one step of the park menu: the answer is 2 when the pad is out, otherwise the state machine's (0 waiting, 1 done, 2 not a park)
    auto step_park_menu = [&]() -> std::uint32_t {
        if (word_at(m_, self + 0x194) == 5) {
            return 2;
        }
        return engine_value(0x00432c60, {word_at(m_, self + 0x190)});
    };
    switch (word_at(m_, self + 0x188)) {
    case 1:
        if (word_at(m_, self + 0x30) == 0) {
            build({text_at(kMainMenuTexts), text_at(kMainMenuTexts + 4), text_at(kMainMenuTexts + 12), text_at(kMainMenuTexts + 16), text_at(kMainMenuTexts + 20),
                   text_at(kMainMenuTexts + 24), text_at(0x0052e404), text_at(kMainMenuTexts + 28), text_at(kMainMenuTexts + 32)},
                  {2, 4, 7, 6, 1, 3, 9, 8, 0}, 0);
        } else if (done()) {
            last(1);
            switch (chosen()) {
            case 0:
                set_global_byte(m_, kReplayFlag, 0);
                state(0xc);
                break;
            case 1:
                set_byte_at(m_, self + 0x2e, 1);
                state(2);
                break;
            case 3:
                state(3);
                break;
            case 4:
                state(5);
                set_global_byte(m_, kReplayFlag, 1);
                set_global_byte(m_, kTwoSkaters, 0);
                break;
            case 5:
                state(5);
                set_global_byte(m_, kReplayFlag, 1);
                set_global_byte(m_, kTwoSkaters, 1);
                break;
            case 6:
                state(6);
                break;
            case 7:
                state(7);
                break;
            case 8:
                state(0xb);
                break;
            case 9:
                state(4);
                break;
            default:
                state(0);
                break;
            }
            close_menu();
        }
        break;
    case 2: {
        engine(0x00436fd0, {ui, 3});
        if (byte_at(m_, self + 0x2e) != 0) {
            set_byte_at(m_, self + 0x2e, 0);
            clear_scores();
            const std::uint32_t size = engine_out(0x0043c480, {self + 0x1c}, self + 0x1c, true);
            engine(0x0047a480, {word_at(m_, self + 0x1c), size, 1, 0});
        }
        std::uint32_t result;
        if (word_at(m_, self + 0x194) == 5) {
            result = 2;
        } else {
            result = engine_value(0x0047ac60);
            if (result == 0) {
                break;
            }
        }
        if (result == 1) {
            engine_fill(0x0047b850, {kScratchCardName}, {}, kScratchCardName, 0x20);
            engine(0x00432980, {kScratchCardName, 0}, m_.cstring(kScratchCardName, 0x40));
            state(word_at(m_, self + 0x18c) == 0xc ? 5 : 0);
        } else if (result == 2) {
            state(1);
        }
        free_block(word_at(m_, self + 0x1c));
        set_word_at(m_, self + 0x1c, 0);
        engine(0x0047ae10, {result});
        break;
    }
    case 3:
        engine(0x00436fd0, {ui, 3});
        if (word_at(m_, self + 0x190) == 0) {
            set_word_at(m_, self + 0x18, alloc(0x1e00, 0, 1, 0));
            park_menu(1, word_at(m_, self + 0x18), 0);
        } else {
            const std::uint32_t result = step_park_menu();
            if (result == 0) {
                break;
            }
            if (result == 1) {
                parse();
                state(0);
            } else if (result == 2) {
                state(1);
            }
            delete_buffer(0x18);
            delete_park_menu();
        }
        break;
    case 4:
        engine(0x00436fd0, {ui, 3});
        if (word_at(m_, self + 0x190) == 0) {
            set_word_at(m_, self + 0x18, alloc(0x1e00, 0, 1, 0));
            park_menu(1, word_at(m_, self + 0x18), 0);
            engine(0x00433090, {word_at(m_, self + 0x190), 1});
        } else {
            const std::uint32_t result = engine_value(0x00432c60, {word_at(m_, self + 0x190)});
            if (result != 0) {
                if (result == 1) {
                    state(0);
                } else if (result == 2) {
                    state(1);
                }
                delete_park_menu();
            }
        }
        break;
    case 6:
        if (word_at(m_, self + 0x30) == 0) {
            build({text_at(0x0052e86c), text_at(0x0052e870), text_at(0x0052e874), text_at(0x0052e878)}, {0, 1, 2, 3}, 0);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            set_word_at(m_, self + 4, choice);
            if (choice == 0xffffffff) {
                state(1);
            } else {
                state(0);
                set_byte_at(m_, self + 0xe, 1);
            }
            last(6);
            close_menu();
        }
        break;
    case 7:
        if (word_at(m_, self + 0x30) == 0) {
            build({text_at(0x0052e858), text_at(0x0052e85c), text_at(0x0052e860), text_at(0x0052e864), text_at(0x0052e868)}, {0, 1, 2, 3, 4}, 0);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            set_word_at(m_, self + 8, choice);
            last(7);
            state(choice != 0xffffffff ? 8 : 1);
            close_menu();
        }
        break;
    case 8:
        if (word_at(m_, self + 0x30) == 0) {
            std::uint32_t title = kMenuText;
            if (word_at(m_, self + 0x18c) == 7) {
                title = text_at(kConfirmErase);
            } else if (word_at(m_, self + 0x18c) == 10) {
                title = text_at(kConfirmSave);
            } else {
                s_.check(false, kMsgBadState);
            }
            build({title, text_at(kYes), text_at(kNo)}, {0xffffffff, 0, 1}, 0);
            const std::uint32_t inner = word_at(m_, word_at(m_, self + 0x30) + 4);
            engine(0x00470b20, {inner, 1});
            (void)virtual_call(inner, 2);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            close_menu();
            if (choice == 0xffffffff) {
                state(word_at(m_, self + 0x18c));
            } else if (choice == 1) {
                state(0);
            } else if (word_at(m_, self + 0x18c) == 7) {
                set_byte_at(m_, self + 0xd, 1);
                state(0);
            } else if (word_at(m_, self + 0x18c) == 10) {
                state(0);
            } else {
                s_.check(false, kMsgUnknownState);
            }
        }
        break;
    case 9:
        if (word_at(m_, self + 0x30) == 0) {
            build({kStrClearScores, kStrSaveScores, kStrNoScores}, {0, 2, 1}, 0);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            if (choice == 0) {
                clear_scores();
                state(10);
            } else if (choice == 1) {
                state(0xb);
            } else if (choice == 2) {
                state(10);
            } else {
                state(1);
            }
            last(9);
            close_menu();
        }
        break;
    case 10:
        if (word_at(m_, self + 0x190) == 0) {
            const std::uint32_t size = engine_out(0x0043c480, {self + 0x1c}, self + 0x1c, true);
            park_menu(2, word_at(m_, self + 0x1c), size);
        } else {
            const std::uint32_t result = step_park_menu();
            if (result != 2 && result != 1) {
                break;
            }
            delete_buffer(0x1c);
            delete_park_menu();
            last(10);
            state(result != 1 ? 1 : 0);
        }
        break;
    case 11:
        if (word_at(m_, self + 0x190) != 0) {
            const std::uint32_t result = step_park_menu();
            if (result == 1) {
                parse();
            } else if (result != 2) {
                break;
            }
            delete_buffer(0x18);
            delete_park_menu();
            last(0xb);
            state(result != 1 ? 1 : 0);
        } else {
            set_word_at(m_, self + 0x18, alloc(0x2000, 0, 1, 0));
            park_menu(0, word_at(m_, self + 0x18), 0);
        }
        break;
    case 12:
        if (word_at(m_, self + 0x30) == 0) {
            build({text_at(0x0052e87c), text_at(kYes), text_at(kNo)}, {0xffffffff, 0, 1}, 0);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            if (choice == 0) {
                set_byte_at(m_, self + 0x2e, 1);
                state(2);
            } else if (choice == 1) {
                state(5);
            } else {
                state(1);
            }
            last(0xc);
            close_menu();
        }
        break;
    case 13:
        if (word_at(m_, self + 0x30) == 0) {
            sprintf_list(kScratchGapName, kFmtGapName, {item + 0xd});
            build({kScratchGapName, text_at(0x0052e890), text_at(0x0052e894), text_at(0x0052e898), text_at(0x0052e89c), text_at(0x0052e88c)}, {0xffffffff, 0, 1, 3, 2, 4}, 0);
        } else if (done()) {
            switch (chosen()) {
            case 0:
                state(0xe);
                break;
            case 1:
                state(0xf);
                break;
            case 2:
                state(0x11);
                break;
            case 3:
                state(0x10);
                break;
            default:
                state(0);
                break;
            }
            last(0xd);
            close_menu();
        }
        break;
    case 14: {
        engine(0x00436fd0, {ui, 3});
        std::uint32_t entry = word_at(m_, self + 0x34);
        if (entry == 0) {
            const std::uint32_t block = operator_new(0x374);
            entry = block != 0 ? engine_value(0x00471e30, {block, self + 0x38, 0x18, 0x100, 0x46, 0x100, 0x96, 1, 1, 1, 1, 1}) : 0;
            set_word_at(m_, self + 0x34, entry);
            engine(0x00472a70, {entry, global_word(m_, 0x0052e8b0), 0x100, 0x32}, m_.cstring(global_word(m_, 0x0052e8b0), 0x40));
            engine(0x00472930, {word_at(m_, self + 0x34), item + 0xd}, m_.cstring(item + 0xd, 0x40));
        } else {
            (void)virtual_call(entry, 2, {0, 0});
            const std::uint32_t pad = engine_value(0x0046f550, {0, 0, 6});
            if (word_at(m_, self + 0x194) != 5) {
                const std::uint32_t answer = engine_value(0x00472cc0, {word_at(m_, self + 0x34), pad});
                if (answer == 1) {
                    const std::uint32_t name = engine_value(0x00472920, {word_at(m_, self + 0x34)});
                    sprintf_list(item + 0xd, kFmtString, {name});
                } else if (answer != 2) {
                    break;
                }
            }
            state(0xd);
            if (word_at(m_, self + 0x34) != 0) {
                (void)virtual_call(word_at(m_, self + 0x34), 0, {1});
            }
            set_word_at(m_, self + 0x34, 0);
        }
        break;
    }
    case 15:
        if (word_at(m_, self + 0x30) == 0) {
            std::vector<std::uint32_t> texts;
            std::vector<std::uint32_t> ids;
            for (std::uint32_t k = 0; k < 16; ++k) {
                ids.push_back(k);
                sprintf_list(kScratchStrings + 16 * k, kFmtDecimal, {word_at(m_, kPointValues + 4 * k)});
                texts.push_back(kScratchStrings + 16 * k);
            }
            build(texts, ids, 1);
        } else if (done()) {
            const std::uint32_t choice = chosen();
            if (choice != 0xffffffff) {
                set_word_at(m_, item + 0x28, choice);
            }
            state(0xd);
            close_menu();
        }
        break;
    case 16:
        if (word_at(m_, self + 0x30) == 0) {
            set_word_at(m_, self + 0x30, menu_wrapper({text_at(0x0052e8a0), text_at(0x0052e8a4), text_at(0x0052e8a8), text_at(0x0052e8ac)}, kGapTypes, 0));
        } else if (done()) {
            const std::uint32_t choice = chosen();
            if (choice != 0xffffffff) {
                set_byte_at(m_, item + 0xc, static_cast<std::uint8_t>(choice));
            }
            state(0xd);
            close_menu();
        }
        break;
    default:
        break;
    }
    if (word_at(m_, self + 0x30) != 0) {
        (void)virtual_call(word_at(m_, self + 0x30), 1);
    }
    if (word_at(m_, self + 0x194) == 5 && word_at(m_, self + 0x188) != 0) {
        close_menu();
        state(0);
    }
}

// 0x00435830: one frame of the editor's modes.  The piece and item modes take the pad and watch the budget; the menu modes run the menus and put the park's fog back when they
// close; the gap mode adjusts a gap; the pad-out mode shows a "reinsert the controller" menu until the pad is back.  Whenever the pad is out the editor goes to that mode.
void LevelEditor::update_menu(std::uint32_t self) {
    const std::uint32_t ui = word_at(m_, self + 0x1a8);
    const std::uint32_t park = word_at(m_, self + 0x1ac);
    auto mode = [&]() { return word_at(m_, self + 0x194); };
    auto set_mode = [&](std::uint32_t value) { set_word_at(m_, self + 0x194, value); };
    switch (mode()) {
    case 0:
    case 1: {
        engine(0x00436fd0, {ui, 1});
        handle_input(self);
        if (global_byte(m_, kPadGapMenu) != 0) {
            set_global_byte(m_, kPadGapMenu, 0);
            engine(0x0043f5e0, {park, 1});
            engine(0x00466de0);
            set_word_at(m_, self + 0x18c, 0);
            set_word_at(m_, self + 0x188, 1);
            set_mode(2);
        }
        if (mode() == 0 && engine_value(0x004371d0, {ui}) == global_word(m_, kBaseType)) {
            set_mode(1);
        } else if (mode() == 1) {
            if (engine_value(0x004371d0, {ui}) != global_word(m_, kBaseType)) {
                set_mode(0);
            }
            const std::uint32_t found = engine_out(0x00438360, {word_at(m_, self + 0x1b8), 0}, 0, true);
            if (word_at(m_, self + 0x19c) == 0xffffffff) {
                if (found != 0xffffffff) {
                    set_word_at(m_, ui + 0x43c, found);
                    set_word_at(m_, ui + 0x434, 3);
                }
                if (word_at(m_, self + 0x19c) == 0xffffffff && found == 0xffffffff) {
                    set_word_at(m_, ui + 0x434, global_byte(m_, kBlink) == 0 ? 0 : 4);
                }
            }
        }
        set_byte_at(m_, ui + 0x438, (mode() == 1 || global_byte(m_, kBlink) != 0) ? 1 : 0);
        break;
    }
    case 2:
    case 3:
        engine(0x00436fd0, {ui, 0});
        run_menus(self);
        if (word_at(m_, self + 0x188) == 0) {
            engine(0x0043f5e0, {park, 0});
            set_global_word(m_, kMenuOpen, 0);
            engine(0x00489670, {kInputActions});
            if (mode() == 2) {
                set_mode(0);
            } else if (mode() == 3) {
                set_mode(1);
            }
        }
        if (word_at(m_, self + 0x188) == 0x11) {
            engine(0x0043f5e0, {park, 0});
            set_global_word(m_, kMenuOpen, 0);
            set_word_at(m_, self + 0x188, 0);
            set_mode(4);
        }
        set_byte_at(m_, ui + 0x438, 0);
        break;
    case 4:
        engine(0x00436fd0, {ui, 2});
        s_.check(word_at(m_, self + 0x1a0) != 0xffffffff, kMsgNoGap);
        adjust_gap(self);
        if (global_byte(m_, kPadPlace) != 0 || global_byte(m_, kPadRemove) != 0) {
            engine(0x0043f5e0, {park, 1});
            engine(0x00466de0);
            engine(0x00489670, {kInputActions});
            set_mode(3);
            set_word_at(m_, self + 0x188, 0xd);
        }
        break;
    case 5:
        engine(0x00436fd0, {ui, 3});
        run_menus(self);
        if (word_at(m_, self + 0x30) == 0) {
            put_ids(m_, {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff});
            set_word_at(m_, self + 0x30, menu_wrapper({kMenuText, kMenuText, global_word(m_, 0x0052e8c0), global_word(m_, 0x0052e8c4)}, kScratchIds, 1));
            break;
        }
        if (global_word(m_, kPadOut) != 0) {
            (void)virtual_call(word_at(m_, self + 0x30), 0, {1});
            set_word_at(m_, self + 0x30, 0);
            set_mode(word_at(m_, self + 0x198));
            break;
        }
        break;
    default:
        break;
    }
    if (global_word(m_, kPadOut) == 0 && mode() != 5) {
        set_word_at(m_, self + 0x198, mode());
        set_mode(5);
    }
}

}  // namespace opentony::runtime::leveleditor
