#include "auto_views.hpp"
#include "createobj_kit.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

// the screen object's fields
constexpr Word kPageCopy = 0xb0;             // the page the rows show
constexpr Word kColumn = 0xbc;               // the column the pad selects (0..2)
constexpr Word kBlink = 0xc0;                // a frame counter (modulo 20)
constexpr Word kResetPrompt = 0xc4;
constexpr Word kPie = 0xc8;
constexpr Word kDone = 0xfc;                 // byte: the screen has been left
constexpr Word kFontA = 0x100;
constexpr Word kBackground = 0x250;
constexpr Word kFontB = 0x254;
constexpr Word kList = 0x3a0;
constexpr Word kRows = 0x3a4;                // 15 rows
constexpr Word kStyle = 0x420;
constexpr Word kArrowA = 0x424;
constexpr Word kArrowB = 0x428;
constexpr Word kPictures = 0x42c;            // six corner pictures

// the bindings table: a row is 0x14 bytes from 0x005600a4
constexpr Word kBindingPage = 0x005600a0;    // the page shown (0: the keyboard, others: the pads)
constexpr Word kBindingRows = 0x005600a4;
constexpr Word kBindingRowStride = 0x14;
constexpr Word kBindingTitles = 0x005600a8;  // row 0's second field: the NULL-ended list of page titles
constexpr Word kBindingKeys = 0x005600ac;    // a row's pointer to its two key bytes
constexpr Word kBindingValues = 0x005600b0;  // a row's pointer to its pad button value
constexpr Word kBindingEnd = 0x005601dc;
constexpr Word kKeyRows = 15;
constexpr Word kLabelBuffer = 0x00560088;    // the text of a pad button value

constexpr Word kBlankText = 0x005252f0;      // " "
constexpr Word kNoneText = 0x00525988;       // "-?-"
constexpr Word kNumberFormat = 0x00525764;   // "%d"
constexpr Word kSetKeyFormat = 0x0052598c;   // "SetKey: DI=%d %02Xh, %c %s"
constexpr Word kPromptFileOffset = 0x527d54;
constexpr Word kFonts[2] = {0x00525458, 0x0052544c};   // mainf, s2font2

constexpr Word kGameLoopResult = kScratch + 0x2c0;     // the answer the poll writes (a local of the main loop)

constexpr Word kWindowConstruct = 0x004ac5b0;
constexpr Word kWindowDestroy = 0x004ac610;
constexpr Word kWindowAddElement = 0x004ac670;
constexpr Word kWindowDraw = 0x004ac770;
constexpr Word kPieConstruct = 0x00454930;
constexpr Word kPieDestruct = 0x00454970;
constexpr Word kFontConstruct = 0x00449ca0;
constexpr Word kFontDestruct = 0x0044a060;
constexpr Word kFontLoad = 0x0044ada0;
constexpr Word kFontUnload = 0x0044b0a0;
constexpr Word kFontSetRgb = 0x0044ab70;
constexpr Word kOpenLoad = 0x00448dd0;
constexpr Word kSpriteCreate = seam::hal::D3DSprite_Create;
constexpr Word kSpriteDestroy = seam::hal::D3DSprite_Destroy;
constexpr Word kSpriteDraw = 0x004d7990;
constexpr Word kElementConstruct = 0x004acbe0;
constexpr Word kBmpImageConstruct = 0x00457420;
constexpr Word kPromptConstruct = 0x004a1b40;
constexpr Word kPropertiesConstruct = 0x0046f770;
constexpr Word kListConstruct = 0x00470c60;
constexpr Word kRowConstruct = 0x0046fc50;
constexpr Word kPadPoll = 0x0046f550;
constexpr Word kSfx = 0x004ad620;
constexpr Word kKeyChar = 0x004e4230;
constexpr Word kKeyName = 0x004e4250;
constexpr Word kCrtNew = seam::crt::CRT_operator_new;
constexpr Word kCrtDelete = seam::crt::FUN_005002f3;

constexpr Word kRenderer = 0x00560fd4;
constexpr Word kFrontFlag = 0x0056114c;
constexpr Word kSimTick = 0x0056e31c;
constexpr Word kLastTick = 0x00568624;
constexpr Word kPadBack = 0x0056aff9;
constexpr Word kPadConfirmA = 0x0056b029;
constexpr Word kPadConfirmB = 0x0056b0d9;
constexpr Word kSettingsDirty = 0x006a13fe;
constexpr Word kMouseMode = 0x006a13fc;      // byte: the mouse drives the menus
constexpr Word kMouseColumn = 0x006a1410;    // the column the mouse clicked
constexpr Word kPadCaseA = 0x0056aff8;       // pad edges the step reads: the arrow keys leave the column when one is set
constexpr Word kPadCaseB = 0x0056b028;
constexpr Word kPadLeftA = 0x0056b078;       // rows 11 to 14 leave the column when one of these two is set
constexpr Word kPadLeftB = 0x0056b079;       // (and the second turns the row's value down by one, then is cleared)
constexpr Word kPadRightB = 0x0056b089;      // turns the row's value up by one, then is cleared
constexpr Word kPadMenuA = 0x0056b099;       // either one leaves the column and redoes every row
constexpr Word kPadMenuB = 0x0056b0a9;
constexpr Word kButton = 0xb4;               // the first pad button down, this frame and the frame before
constexpr Word kPreviousButton = 0xb8;
constexpr Word kResetBindings = 0x004e3e80;
constexpr Word kFirstButton = 0x004e4260;
constexpr Word kFirstKey = 0x004e41d0;
constexpr Word kListHighlighted = 0x00470ab0;
constexpr Word kWindowUpdate = 0x004ac730;

constexpr Word kMsgNothing = 0;

}  // namespace

// ---- the font records ---------------------------------------------------------------------------------------------------------------------------------

// the copy of a Font record into a Font object: the words and bytes the fields use and the 0x100 bytes of the glyph table; the two padding bytes at +0x22 are not copied
void CreateObjKit::copy_font_fields(Word dest, Word source) {
    for (Word offset = 4; offset <= 0x1c; offset += 4) {
        set_word_at(m_, dest + offset, word_at(m_, source + offset));
    }
    set_byte_at(m_, dest + 0x20, byte_at(m_, source + 0x20));
    set_byte_at(m_, dest + 0x21, byte_at(m_, source + 0x21));
    for (Word offset = 0x24; offset <= 0x44; offset += 4) {
        set_word_at(m_, dest + offset, word_at(m_, source + offset));
    }
    set_byte_at(m_, dest + 0x48, byte_at(m_, source + 0x48));
    set_byte_at(m_, dest + 0x49, byte_at(m_, source + 0x49));
    set_byte_at(m_, dest + 0x4a, byte_at(m_, source + 0x4a));
    for (Word k = 0; k < 0x100; ++k) {
        set_byte_at(m_, dest + 0x4b + k, byte_at(m_, source + 0x4b + k));
    }
}

// ---- the bindings screen ----------------------------------------------------------------------------------------------------------------------------

// 0x0041c4b0 (fastcall, called "Create_LoadPSH"): the window with its background, six corner pictures, two arrows, the two fonts, four prompts (back, reset, select, accept), the style of
// the rows, the list and its 15 rows
Word CreateObjKit::keyconfig_construct(Word self) {
    method(kWindowConstruct, self, {0, 0, 0xffffffffu, 0xffffffffu});
    method(kPieConstruct, self + kPie);
    method(kFontConstruct, self + kFontA);
    method(kFontConstruct, self + kFontB);
    createobj_screen_keyconfig_at(m_, self).set_field_0(static_cast<Word>(kVtKeyConfig));
    createobj_screen_keyconfig_at(m_, self).set_field_bc(0);
    const Word sprite_memory = engine_value(kCrtNew, {0x1c});
    Word sprite = 0;
    if (sprite_memory != 0) {
        const Word handle = engine_value(kOpenLoad, {0x00525978, 1}, text_at(0x00525978));
        sprite = method(kSpriteCreate, sprite_memory, {handle, 0x00525978, 1});
    }
    createobj_screen_keyconfig_at(m_, self).set_field_250(static_cast<Word>(sprite));
    const struct {
        Word name;
        Word x;
        Word y;
        Word dx;
        Word slot;
    } pictures[6] = {{0x00525968, 0x4b, 0xe, 0xfffffe0c, 0x42c}, {0x00525958, 0x118, 0xe, 0xfffffed9, 0x430}, {0x00525948, 0x1e5, 0xe, 0xffffffa6, 0x434},
                     {0x00525938, 0x4b, 0x8e, 0xfffffe0c, 0x438}, {0x00525928, 0x118, 0x8e, 0xfffffed9, 0x43c}, {0x00525918, 0x1e5, 0x8e, 0xffffffa6, 0x440}};
    for (const auto& picture : pictures) {
        const Word block = operator_new(0x2c);
        const Word element = block != 0 ? method(kElementConstruct, block, {picture.name, picture.x, picture.y, picture.dx, picture.y, 0, 0}, text_at(picture.name)) : 0;
        set_word_at(m_, self + picture.slot, element);
        method(kWindowAddElement, self, {element});
    }
    auto arrow = [&](Word slot, Word name, Word first, Word flag) {
        const Word block = operator_new(0x5c);
        const Word image = block != 0 ? method(kBmpImageConstruct, block, {name, first, 0, 0}, text_at(name)) : 0;
        set_word_at(m_, self + slot, image);
        set_byte_at(m_, image + 0xa, flag);
        set_byte_at(m_, image + 0xb, 1);
    };
    arrow(kArrowA, 0x00525908, 1, 2);
    arrow(kArrowB, 0x005258f8, 1, 0);
    // the first font (mainf): the record's fields, then the screen's own style
    copy_font_fields(self + kFontA, engine_value(kFontLoad, {kFonts[0]}, text_at(kFonts[0])));
    createobj_screen_keyconfig_at(m_, self).set_field_104(1);
    createobj_screen_keyconfig_at(m_, self).set_field_108(1);
    createobj_screen_keyconfig_at(m_, self).set_field_130(0x800);
    createobj_screen_keyconfig_at(m_, self).set_field_114(0x40);
    createobj_screen_keyconfig_at(m_, self).set_field_118(0x40);
    createobj_screen_keyconfig_at(m_, self).set_field_11c(0xff);
    createobj_screen_keyconfig_at(m_, self).set_field_fc(0);
    // the second (s2font2) in grey, the prompts' font
    copy_font_fields(self + kFontB, engine_value(kFontLoad, {kFonts[1]}, text_at(kFonts[1])));
    createobj_screen_keyconfig_at(m_, self).set_field_258(0);
    createobj_screen_keyconfig_at(m_, self).set_field_25c(3);
    method(kFontSetRgb, self + kFontB, {0x80, 0x80, 0x80});
    createobj_screen_keyconfig_at(m_, self).set_field_275(0);
    createobj_screen_keyconfig_at(m_, self).set_field_260(0);
    createobj_screen_keyconfig_at(m_, self).set_field_264(0);
    createobj_screen_keyconfig_at(m_, self).set_field_284(0x1000);
    const struct {
        Word name;
        Word x;
    } prompts[4] = {{0x00525400, 0x32}, {0x005258e8, 0x8c}, {0x005258d8, 0x118}, {0x0052540c, 0x190}};
    for (Word k = 0; k < 4; ++k) {
        const Word block = operator_new(0xb0);
        const Word prompt = block != 0
                                ? method(kPromptConstruct, block, {prompts[k].name, kBlankText, self + kFontB, prompts[k].x, 0xdc, 0xffffffffu, global_word(m_, kPromptFileOffset) + 0x14},
                                         text_at(prompts[k].name))
                                : 0;
        if (k == 1) {
            createobj_screen_keyconfig_at(m_, self).set_field_c4(static_cast<Word>(prompt));
        }
        method(kWindowAddElement, self, {prompt});
    }
    // the style of the rows: mainf's fields moved up four bytes, with the screen's colours
    const Word style_block = operator_new(0x17c);
    const Word style = style_block != 0 ? method(kPropertiesConstruct, style_block) : 0;
    createobj_screen_keyconfig_at(m_, self).set_field_420(static_cast<Word>(style));
    copy_font_fields(style + 4, self + kFontA);
    set_word_at(m_, style + 8, 0);
    set_word_at(m_, style + 0xc, 3);
    set_word_at(m_, style + 0x18, 0xff);
    set_word_at(m_, style + 0x1c, 0x60);
    set_word_at(m_, style + 0x20, 0x20);
    set_byte_at(m_, style + 0x25, 0);
    set_word_at(m_, style + 0x10, 0);
    set_word_at(m_, style + 0x154, 0x640);
    set_word_at(m_, style + 0x150, 0x708);
    const Word list_block = operator_new(0x1bc);
    const Word list = list_block != 0 ? method(kListConstruct, list_block, {0x59, 0x24, 500, 0x78, 0, 0, 3, 0}) : 0;
    createobj_screen_keyconfig_at(m_, self).set_field_3a0(static_cast<Word>(list));
    for (Word i = 0; i < kKeyRows; ++i) {
        const Word block = operator_new(0x100);
        Word row = 0;
        if (block != 0) {
            row = method(kRowConstruct, block, {i, word_at(m_, kBindingRows + i * kBindingRowStride), style, 3, i == 0 ? createobj_screen_keyconfig_at(m_, self).field_424() : 0u, i == 0 ? createobj_screen_keyconfig_at(m_, self).field_428() : 0u});
        }
        set_word_at(m_, self + kRows + 4 * i, row);
        if (row != 0) {
            virtual_call(list, 6, {row});
            set_half_at(m_, row + 4, 0);
        }
        keyconfig_set_row(self, i, 0);
    }
    set_half_at(m_, createobj_screen_keyconfig_at(m_, self).field_3a4() + 4, 0x2000);
    virtual_call(list, 2);
    set_word_at(m_, createobj_screen_keyconfig_at(m_, self).field_3a0() + 0x1a8, 0xaa);
    for (Word y = 0x47, k = 0; y < 0xce; y += 9, ++k) {
        if (y >= 0x50) {
            virtual_call(word_at(m_, self + kRows + 4 * k), 1, {0x59, y});
        }
    }
    return self;
}

// 0x0041cdc0 (fastcall): the background, the two fonts' files, the list, the rows and the style are released and the bases destroyed
void CreateObjKit::keyconfig_destruct(Word self) {
    createobj_screen_keyconfig_at(m_, self).set_field_0(static_cast<Word>(kVtKeyConfig));
    const Word background = createobj_screen_keyconfig_at(m_, self).field_250();
    if (background != 0) {
        method(kSpriteDestroy, background);
        engine(kCrtDelete, {background});
    }
    engine(kFontUnload, {kFonts[0]}, text_at(kFonts[0]));
    engine(kFontUnload, {kFonts[1]}, text_at(kFonts[1]));
    if (createobj_screen_keyconfig_at(m_, self).field_3a0() != 0) {
        delete_object(createobj_screen_keyconfig_at(m_, self).field_3a0());
    }
    for (Word i = 0; i < kKeyRows; ++i) {
        if (word_at(m_, self + kRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kRows + 4 * i));
        }
    }
    if (createobj_screen_keyconfig_at(m_, self).field_420() != 0) {
        delete_object(createobj_screen_keyconfig_at(m_, self).field_420());
    }
    method(kFontDestruct, self + kFontB);
    method(kFontDestruct, self + kFontA);
    method(kPieDestruct, self + kPie);
    method(kWindowDestroy, self);
}

// 0x0041cda0 (thiscall, one argument)
Word CreateObjKit::keyconfig_scalar_delete(Word self, Word flags) {
    keyconfig_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0041ceb0 (thiscall, two arguments): the text of a row. Row 0 is the page title (the delta turns the page); the others show the keys bound to the row (page 0) or the pad button
// (the other pages); a delta is a click on the arrows and sounds
void CreateObjKit::keyconfig_set_row(Word self, Word row, Word delta) {
    if (delta != 0) {
        engine(kSfx, {0x90, 0x2000, 0});
    }
    createobj_screen_keyconfig_at(m_, self).set_field_b0(static_cast<Word>(global_word(m_, kBindingPage)));
    Word first = kBlankText;
    Word second = kBlankText;
    const Word style = createobj_screen_keyconfig_at(m_, self).field_420();
    if (row == 0) {
        const Word titles = global_word(m_, kBindingTitles);
        Word count = 0;
        while (word_at(m_, titles + 4 * count) != 0) {
            ++count;
        }
        std::int32_t page = s(global_word(m_, kBindingPage) + delta);
        if (page >= s(count)) {
            page = 0;
        } else if (page < 0) {
            page = s(count) - 1;
        }
        const Word text = word_at(m_, titles + 4 * u(page));
        set_global_word(m_, kBindingPage, u(page));
        virtual_call(createobj_screen_keyconfig_at(m_, self).field_3a4(), 9, {text, 1, 0xdc, style}, text_at(text));
        return;
    }
    if (createobj_screen_keyconfig_at(m_, self).field_b0() != 0) {
        const Word pointer = word_at(m_, kBindingValues + row * kBindingRowStride);
        if (pointer != 0) {
            const Word value = word_at(m_, pointer);
            if (value == 0xffffffffu) {
                copy_string(kLabelBuffer, kNoneText);
            } else {
                set_string(kLabelBuffer, std::to_string(s(value)));
            }
            first = kLabelBuffer;
        }
    } else {
        if (delta != 0) {
            std::int32_t column = s(createobj_screen_keyconfig_at(m_, self).field_bc() + delta);
            column = column < 0 ? 0 : (column > 2 ? 2 : column);
            createobj_screen_keyconfig_at(m_, self).set_field_bc(static_cast<Word>(u(column)));
        }
        const Word keys = word_at(m_, kBindingKeys + row * kBindingRowStride);
        first = engine_value(kKeyName, {byte_at(m_, keys)});
        second = engine_value(kKeyName, {byte_at(m_, keys + 1)});
    }
    if (first == 0) {
        first = kBlankText;
    }
    virtual_call(word_at(m_, self + kRows + 4 * row), 9, {first, 1, 0xdc, style}, text_at(first));
    virtual_call(word_at(m_, self + kRows + 4 * row), 9, {second, 2, 0x14a, style}, text_at(second));
}

// 0x0041d060 (fastcall): the frame of the screen: the blink counter, the list steps, and the window and background are drawn
void CreateObjKit::keyconfig_update(Word self) {
    createobj_screen_keyconfig_at(m_, self).set_field_c0(static_cast<Word>(u((s(createobj_screen_keyconfig_at(m_, self).field_c0()) + 1) % 0x14)));
    const Word list = createobj_screen_keyconfig_at(m_, self).field_3a0();
    virtual_call(list, 3);
    virtual_call(list, 7);
    if (createobj_screen_keyconfig_at(m_, self).field_bc() == 0) {
        virtual_call(list, 5, {engine_value(kPadPoll, {0, 0, 6})});
    }
    virtual_call(list, 4);
    method(kWindowDraw, self, {0, 0});
    method(kSpriteDraw, createobj_screen_keyconfig_at(m_, self).field_250(), {word_at(m_, global_word(m_, kRenderer) + 0x88), 0, 0});
}

// 0x0041d0f0 (thiscall, no arguments; slot 1 of the screen's vtable): the screen's step, run once a frame before the update. It reads the mouse (the reset prompt and the row under it),
// the first pad button and the first key down, and applies them to the highlighted row: on a pad page a button (or Backspace for -1) is bound to the row; on the keyboard page the key
// is assigned to the selected column (Escape, up and down leave the column, the arrows leave it only when a pad edge is set, Backspace clears the key, Enter does nothing); the
// pad edges move the page and the column; the selected cell blinks every fifth frame after the tenth. Rows 11 to 14 have one column only. When anything changed every row's text is
// redone and the rows from 11 on are enabled on the keyboard page only.
void CreateObjKit::keyconfig_step(Word self) {
    bool dirty = false;
    if (global_byte(m_, kMouseMode) != 0 && (virtual_call(word_at(m_, self + kResetPrompt), 7) & 0xff) != 0) {
        engine(kResetBindings);
        dirty = true;
        engine(kSfx, {0x93, 0x2000, 0});
    }
    set_global_byte(m_, kSettingsDirty, word_at(m_, self + kColumn) == 0 ? 1 : 0);      // (the byte at 0x006a13fe: the first column is selected)
    set_word_at(m_, self + kPreviousButton, word_at(m_, self + kButton));
    set_word_at(m_, self + kButton, engine_value(kFirstButton));
    const Word key = engine_value(kFirstKey, {0});
    const Word row = method(kListHighlighted, word_at(m_, self + kList));
    bool clicked = false;
    if (global_byte(m_, kMouseMode) != 0) {
        clicked = (virtual_call(word_at(m_, self + kRows + 4 * row), 7) & 0xff) != 0;
        dirty = true;
    }
    if (word_at(m_, self + kPageCopy) != 0) {
        set_word_at(m_, self + kColumn, 0);
        if (key == 0xe) {
            keyconfig_set_button(row, 0xffffffffu);
            keyconfig_set_row(self, row, 0);
            dirty = true;
        }
        const Word button = word_at(m_, self + kButton);
        if (button != word_at(m_, self + kPreviousButton) && button != 0xffffffffu) {
            keyconfig_set_button(row, button);
            keyconfig_set_row(self, row, 0);
            dirty = true;
        }
    } else {
        if (clicked) {
            set_word_at(m_, self + kColumn, global_word(m_, kMouseColumn));
        } else if (global_byte(m_, kMouseMode) != 0) {
            set_word_at(m_, self + kColumn, 0);
        }
        const Word column = word_at(m_, self + kColumn);
        if (s(key) > 0 && s(column) > 0) {
            // the kind of key: 0 leaves the column, 1 clears the key, 2 nothing, 3 leaves the column on a pad edge, 4 (every other key and any code over 0xd0) is assigned
            Word kind = 4;
            switch (key) {
                case 0x01: case 0xc8: case 0xd0: kind = 0; break;
                case 0x0e: kind = 1; break;
                case 0x1c: kind = 2; break;
                case 0xcb: case 0xcd: kind = 3; break;
                default: break;
            }
            if (kind == 0) {
                set_word_at(m_, self + kColumn, 0);
            } else if (kind == 1) {
                keyconfig_assign(row, column, 0);
                keyconfig_set_row(self, row, 0);
            } else if (kind == 3) {
                if (global_byte(m_, kPadCaseA) != 0 || global_byte(m_, kPadCaseB) != 0) {
                    set_word_at(m_, self + kColumn, 0);
                }
            } else if (kind == 4) {
                keyconfig_assign(row, column, key);
                for (Word k = 0; k < kKeyRows; ++k) {
                    keyconfig_set_row(self, k, 0);
                }
            }
        }
    }
    if (word_at(m_, self + kColumn) == 1 && s(row) >= 0xb && s(row) <= 0xe) {
        set_word_at(m_, self + kColumn, 2);
        dirty = true;
        if (global_byte(m_, kPadLeftA) != 0 || global_byte(m_, kPadLeftB) != 0) {
            set_word_at(m_, self + kColumn, 0);
        }
    }
    const std::int32_t blink = s(word_at(m_, self + kBlink));
    if (blink % 5 == 0) {
        if (blink > 10 && key == 0) {
            const Word column = word_at(m_, self + kColumn);
            if (column == 1) {
                virtual_call(word_at(m_, self + kRows + 4 * row), 9, {kBlankText, 1, 0xdc, word_at(m_, self + kStyle)}, text_at(kBlankText));
            } else if (column == 2) {
                virtual_call(word_at(m_, self + kRows + 4 * row), 9, {kBlankText, 2, 0x14a, word_at(m_, self + kStyle)}, text_at(kBlankText));
            }
        } else {
            keyconfig_set_row(self, row, 0);
        }
    }
    if (global_byte(m_, kPadLeftB) != 0) {
        keyconfig_set_row(self, row, 0xffffffffu);
        set_global_byte(m_, kPadLeftB, 0);
        dirty = true;
    }
    if (global_byte(m_, kPadRightB) != 0) {
        keyconfig_set_row(self, row, 1);
        set_global_byte(m_, kPadRightB, 0);
        dirty = true;
    }
    bool all = dirty;
    if (global_byte(m_, kPadMenuA) != 0 || global_byte(m_, kPadMenuB) != 0) {
        engine(kSfx, {0x90, 0x2000, 0});
        set_word_at(m_, self + kColumn, 0);
        all = true;
    }
    if (all) {
        for (Word k = 0; k < kKeyRows; ++k) {
            keyconfig_set_row(self, k, 0);
            if (k >= 0xb) {
                set_byte_at(m_, word_at(m_, self + kRows + 4 * k) + 0xc, word_at(m_, self + kPageCopy) == 0 ? 1 : 0);
            }
        }
    }
    method(kWindowUpdate, self);
}

// 0x0041d4f0 (cdecl, two arguments): row `row` takes the pad button `value`; another row that had it gets the old one, -1 clears the row
void CreateObjKit::keyconfig_set_button(Word row, Word value) {
    if (row == 0) {
        return;
    }
    const Word slot = kBindingValues + row * kBindingRowStride;
    if (word_at(m_, slot) == 0) {
        return;
    }
    if (value == 0xffffffffu) {
        set_word_at(m_, word_at(m_, slot), value);
        engine(kSfx, {0x90, 0x2000, 0});
        return;
    }
    const Word old = word_at(m_, word_at(m_, slot));
    for (Word k = 0; kBindingValues + k * kBindingRowStride < kBindingEnd; ++k) {
        const Word other = word_at(m_, kBindingValues + k * kBindingRowStride);
        if (k != row && other != 0 && word_at(m_, other) == value) {
            set_word_at(m_, other, old);
        }
    }
    set_word_at(m_, word_at(m_, slot), value);
    engine(kSfx, {0x90, 0x2000, 0});
}

// 0x0041d580 (cdecl, three arguments): the key `key` is bound to the row's `column` (1 or 2); the function keys refuse, and the row that had the key gets the old one
void CreateObjKit::keyconfig_assign(Word row, Word column, Word key) {
    if (column == 1 && s(row) >= 0xb && s(row) <= 0xe) {
        return;
    }
    Word character = engine_value(kKeyChar, {key});
    character = character == 0 ? 0x3f : engine_value(kKeyChar, {key});
    const Word name = engine_value(kKeyName, {key});
    (void)character;
    (void)name;
    log_line(kSetKeyFormat);
    if ((s(key) >= 0x3b && s(key) <= 0x44) || (s(key) >= 0x57 && s(key) <= 0x66)) {
        engine(kSfx, {0x92, 0x2000, 0});
        return;
    }
    if (key != 0) {
        Word found = 0;
        Word found_column = 0;
        for (Word k = 0; kBindingKeys + k * kBindingRowStride < kBindingEnd - 4; ++k) {
            const Word keys = word_at(m_, kBindingKeys + k * kBindingRowStride);
            if (keys == 0) {
                continue;
            }
            if (byte_at(m_, keys) == key) {
                found = k;
                found_column = 0;
            }
            if (byte_at(m_, keys + 1) == key) {
                found = k;
                found_column = 1;
            }
        }
        if (s(found) > 0) {
            const Word mine = word_at(m_, kBindingKeys + row * kBindingRowStride);
            const Word theirs = word_at(m_, kBindingKeys + found * kBindingRowStride);
            set_byte_at(m_, theirs + found_column, byte_at(m_, mine + column - 1));
        }
    }
    engine(kSfx, {0x90, 0x2000, 0});
    set_byte_at(m_, word_at(m_, kBindingKeys + row * kBindingRowStride) + column - 1, key);
}

// 0x0041d6f0 (thiscall, one argument): a confirm on either pad or back on the first: on the first column back leaves with -1 and confirm with 1 (the screen is done); on the others
// only the back edge is cleared. Answers 1 when the screen was left.
Word CreateObjKit::keyconfig_poll(Word self, Word result) {
    if (global_byte(m_, kPadConfirmA) == 0 && global_byte(m_, kPadConfirmB) == 0 && global_byte(m_, kPadBack) == 0) {
        return 0;
    }
    const bool back = global_byte(m_, kPadBack) != 0;
    set_global_byte(m_, kPadConfirmA, 0);
    set_global_byte(m_, kPadConfirmB, 0);
    if (createobj_screen_keyconfig_at(m_, self).field_bc() != 0) {
        set_global_byte(m_, kPadBack, 0);
        return 0;
    }
    if (back) {
        set_word_at(m_, result, 0xffffffffu);
        createobj_screen_keyconfig_at(m_, self).set_field_fc(1);
        engine(kSfx, {0x93, 0x2000, 0});
        set_global_byte(m_, kPadBack, 0);
        return 1;
    }
    set_word_at(m_, result, 1);
    createobj_screen_keyconfig_at(m_, self).set_field_fc(1);
    engine(kSfx, {0x91, 0x2000, 0});
    return 1;
}

// 0x0041c4a0 (fastcall)
Word CreateObjKit::keyconfig_done(Word self) { return byte_at(m_, self + kDone) != 0 ? 1u : 0u; }

// 0x0041c2d0 (cdecl, called "Game_MainLoop"): the bindings screen as a game state: the front-end flag is cleared, the screen runs until it says it is done, the bindings are saved (or
// reloaded when the screen was left with back). Answers the screen's answer: -1 for back, 1 otherwise.
Word CreateObjKit::keyconfig_run() {
    const Word saved = global_word(m_, kFrontFlag);
    set_global_word(m_, kFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004b2390);
    engine(0x0048a420);
    const Word block = operator_new(0x444);
    const Word screen = block != 0 ? keyconfig_construct(block) : 0;
    engine(0x00489670, {0x0056aff8});
    engine(0x00489670, {0x0056b164});
    set_global_word(m_, kGameLoopResult, 0xffffffffu);
    bool left = false;
    engine(0x004e4d10);
    engine(0x004e42b0, {0});
    for (;;) {
        engine(seam::hal::Game_FrameTick);
        if (!s_.live()) set_global_word(m_, kSimTick, static_cast<Word>(s_.value("engine_004f7ce0")));
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x00418150);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        if (!s_.live()) set_global_byte(m_, kPadBack, static_cast<Word>(s_.value("engine_00489a10")));              // (the input update's stub sets the pad edges, the way the real update would)
        if (!s_.live()) set_global_byte(m_, kPadConfirmA, static_cast<Word>(s_.value("engine_00489a10")));
        if (left) {
            engine(0x00489670, {0x0056aff8});
            engine(0x00489670, {0x0056b164});
        }
        if ((keyconfig_poll(screen, kGameLoopResult) & 0xff) != 0) {
            left = true;
        }
        virtual_call(screen, 1);
        if (keyconfig_done(screen) != 0) {
            break;
        }
        keyconfig_update(screen);
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        while (global_word(m_, kSimTick) < global_word(m_, kLastTick) + 2) {
            engine(seam::hal::Game_FrameTick);
            if (!s_.live()) set_global_word(m_, kSimTick, static_cast<Word>(s_.value("engine_004f7ce0")));
        }
        set_global_word(m_, kLastTick, global_word(m_, kSimTick));
        engine(0x00430510);
    }
    set_global_word(m_, kFrontFlag, saved);
    if (screen != 0) {
        delete_object(screen);
    }
    engine(0x004e42b0, {1});
    if (global_word(m_, kGameLoopResult) == 0xffffffffu) {
        engine(0x004e4d10);
    } else {
        engine(0x004e5460);
    }
    set_global_byte(m_, kSettingsDirty, 1);
    return global_word(m_, kGameLoopResult);
}

// ---- the career loading screen -----------------------------------------------------------------------------------------------------------------------

// 0x00415b30 (cdecl, one argument): the loading screen of a level: the goal font, the two backgrounds, the buttons of the mode (1: back, 2: play, else both) and two frames of the
// level's name, a check mark for each goal reached and the high scores
void CreateObjKit::career_loading_screen(Word mode) {
    constexpr Word kFont = kScratch + 0x800;                 // the screen's own Font object, a local
    constexpr Word kTitle = kScratch + 0x600;                // the "<level> Checklist" line and the amount of a goal, built in locals
    constexpr Word kAmount = kTitle;                          // (one buffer: each line is drawn before the next is built)
    constexpr Word kGoalFont = 0x00525310;
    constexpr Word kLevelNames = 0x0053c1f8;
    constexpr Word kLevelScores = 0x0053c2bc;
    constexpr Word kLevelStride = 0x1ac;
    constexpr Word kCurrentLevel = 0x0056a898;
    constexpr Word kBackgroundSprite = 0x00562074;
    constexpr Word kScribbleSprite = 0x00562070;
    constexpr Word kPlayPrompt = 0x0056206c;
    constexpr Word kBackPrompt = 0x00562058;
    constexpr Word kFontMapped = 0x00562079;
    engine(kFontLoad, {kGoalFont}, text_at(kGoalFont));
    method(kFontConstruct, kFont);
    copy_font_fields(kFont, engine_value(0x0044b000, {kGoalFont}, text_at(kGoalFont)));   // Font_Find: the record the local font is filled from
    set_global_word(m_, kFont + 4, 1);
    set_global_word(m_, kFont + 8, 3);
    set_global_word(m_, kFont + 0x14, 0x80);
    set_global_word(m_, kFont + 0x18, 0x80);
    set_global_word(m_, kFont + 0x1c, 0x80);
    set_global_byte(m_, kFont + 0x21, 0);
    set_global_word(m_, kFont + 0xc, 0);
    set_global_word(m_, kFont + 0x10, 0);
    set_global_word(m_, kFont + 0x30, 0x800);
    // Font_SetMapping on the font itself (`lea ecx, [esp+0x1c]` after the `push 2`); the Phase 4 probe recorded its `this` four bytes on (0x7ff02004), so the replay is given that
    method(0x0044abb0, s_.live() ? kFont : kFont + 4, {2});
    set_global_byte(m_, kFontMapped, 1);
    auto sprite = [&](Word name, Word flag) {
        const Word memory = engine_value(kCrtNew, {0x1c});
        Word made = 0;
        if (memory != 0) {
            const Word handle = engine_value(kOpenLoad, {name, 1}, text_at(name));
            made = method(kSpriteCreate, memory, {handle, name, flag});
        }
        return made;
    };
    set_global_word(m_, kBackgroundSprite, sprite(0x00525304, 1));
    set_global_word(m_, kScribbleSprite, sprite(0x005252f4, 0));
    auto prompt = [&](Word name, Word x) {
        const Word block = operator_new(0xb0);
        return block != 0 ? method(kPromptConstruct, block, {name, kBlankText, kFont, x, 0xdc, 0xffffffffu, global_word(m_, kPromptFileOffset) + 0x14}, text_at(name)) : 0u;
    };
    if (mode == 1) {
        set_global_word(m_, kBackPrompt, prompt(0x005252d8, 0xea));
    } else if (mode == 2) {
        set_global_word(m_, kPlayPrompt, prompt(0x005252e4, 0xea));
    } else {
        set_global_word(m_, kPlayPrompt, prompt(0x005252e4, 0xa4));
        set_global_word(m_, kBackPrompt, prompt(0x005252d8, 0x132));
    }
    for (Word frame = 2; frame != 0; --frame) {
        engine(seam::hal::Game_FrameTick);
        if (!s_.live()) set_global_word(m_, kSimTick, static_cast<Word>(s_.value("engine_004f7ce0")));
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x00466be0);
        const Word level = global_word(m_, kCurrentLevel);
        set_string(kTitle, m_.cstring(kLevelNames + kLevelStride * level, 0x40) + " Checklist");
        set_global_word(m_, kFont + 4, 1);                                                // the alignment word: centred, left for a goal, right for its amount
        method(0x0044a070, kFont, {0x100, 0x2d, kTitle, 0}, text_at(kTitle));
        Word y = 0x3c;
        for (Word i = 0; y < 0xdc; ++i, y += 0x10) {
            if (engine_value(0x00414030, {i}) != 0) {
                const float above = static_cast<float>(s(y - 10));
                method(kSpriteDraw, global_word(m_, kScribbleSprite), {word_at(m_, global_word(m_, kRenderer) + 0x84), 0x42e60000u, std::bit_cast<Word>(above)});
            }
            const Word row = kLevelStride * level + 0x18 * i;
            set_global_word(m_, kFont + 4, 0);
            Word text = word_at(m_, row + 0x53c2c4);
            if (text == 0) {
                text = word_at(m_, row + 0x53c2c0);
            }
            method(0x0044a070, kFont, {0x82, y, text, 0}, text_at(text));
            set_string(kAmount, "$" + std::to_string(s(word_at(m_, kLevelScores + row))));
            set_global_word(m_, kFont + 4, 2);
            method(0x0044a070, kFont, {0x1a6, y, kAmount, 0}, text_at(kAmount));
        }
        set_global_word(m_, kFont + 4, 1);
        method(kSpriteDraw, global_word(m_, kBackgroundSprite), {word_at(m_, global_word(m_, kRenderer) + 0x88), 0, 0});
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        engine(0x00430510);
    }
    engine(kFontUnload, {kGoalFont}, text_at(kGoalFont));
    method(kFontDestruct, kFont);
}

}  // namespace opentony::runtime::createobj
