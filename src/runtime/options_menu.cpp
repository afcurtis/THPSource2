#include "auto_views.hpp"
#include "host_options.hpp"
#include "options.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// OptionsMenu fields
constexpr std::uint32_t kBackground = 0xac;
constexpr std::uint32_t kVideoBackground = 0xb0;
constexpr std::uint32_t kFontA = 0xb4;          // mainf: the lists' style
constexpr std::uint32_t kFontB = 0x200;         // s2font2: the prompt windows' captions
constexpr std::uint32_t kListMain = 0x34c;
constexpr std::uint32_t kListDisplay = 0x350;
constexpr std::uint32_t kListMovies = 0x354;
constexpr std::uint32_t kListSaved = 0x358;
constexpr std::uint32_t kStyleRows = 0x35c;
constexpr std::uint32_t kStyleTitle = 0x360;
constexpr std::uint32_t kRowsMain = 0x364;      // four arrays of 0x60 slots, one for each list
constexpr std::uint32_t kRowsDisplay = 0x4e4;
constexpr std::uint32_t kRowsMovies = 0x664;
constexpr std::uint32_t kRowsSaved = 0x7e4;
constexpr std::uint32_t kTipsRow = 0x974;       // the trick tips row (also in the display array, +0x4e8)
constexpr std::uint32_t kScoreRow = 0x978;      // the score display row (+0x4ec)
constexpr std::uint32_t kArrowA = 0x964;
constexpr std::uint32_t kArrowB = 0x968;
constexpr std::uint32_t kState = 0x96c;
constexpr std::uint32_t kStateWanted = 0x970;
constexpr std::uint32_t kPageGaps = 0x97c;
constexpr std::uint32_t kPageScores = 0x980;
constexpr std::uint32_t kPageMovies = 0x984;
constexpr std::uint32_t kPageSound = 0x988;
constexpr std::uint32_t kPageCard = 0x98c;
constexpr std::uint32_t kPageReplay = 0x990;
constexpr std::uint32_t kPageControls = 0x994;
constexpr std::uint32_t kModelCard = 0x998;
constexpr std::uint32_t kModelBoombox = 0x99c;
constexpr std::uint32_t kModelControl = 0x9a0;
constexpr std::uint32_t kPageCheats = 0x9a4;
constexpr std::uint32_t kPrompts = 0x9a8;       // four prompt windows
constexpr std::uint32_t kView = 0x9b8;
constexpr std::uint32_t kLight = 0x9cc;

constexpr std::uint32_t kStateMain = 0x14;
constexpr std::uint32_t kStateCheats = 0x15;
constexpr std::uint32_t kStateDisplay = 0x16;
constexpr std::uint32_t kStateMovies = 0x17;
constexpr std::uint32_t kStateGaps = 0x18;
constexpr std::uint32_t kStateScores = 0x19;
constexpr std::uint32_t kStateMovieList = 0x1a;
constexpr std::uint32_t kStateSound = 0x1b;
constexpr std::uint32_t kStateSaved = 0x1c;
constexpr std::uint32_t kStateCard = 0x1d;
constexpr std::uint32_t kStateReplay = 0x1e;
constexpr std::uint32_t kStateControls = 0x1f;

// Retail globals
constexpr std::uint32_t kPad0 = 0x0056aff8;
constexpr std::uint32_t kPad1 = 0x0056b164;
constexpr std::uint32_t kPreManager = 0x0056b834;
constexpr std::uint32_t kReturnFlagPlayer = 0x005601cc;     // set when the controller screens were left for the main list
constexpr std::uint32_t kReturnFlagDisplay = 0x005601cd;
constexpr std::uint32_t kReturnCredits = 0x00561cdc;        // 1 game credits, 2 music credits: shown after the screen
constexpr std::uint32_t kReturnSaved = 0x00561ce0;
constexpr std::uint32_t kLeaveFlag = 0x00561cbd;
constexpr std::uint32_t kTipsOption = 0x0055fcc0;
constexpr std::uint32_t kPanelOption = 0x0055fcc4;
constexpr std::uint32_t kCamera = 0x0055f7c8;
constexpr std::uint32_t kCameraMatrix = 0x0055f7fc;
constexpr std::uint32_t kCameraWords = 0x0055f7cc;

constexpr std::uint32_t kPreName = 0x00535d58;              // "options.pre"
constexpr std::uint32_t kFonts[3] = {0x00525458, 0x0052544c, 0x005254e0};   // mainf, s2font2, s2tricks

constexpr std::uint32_t kMsgNoBackground = 0x00525470;
constexpr std::uint32_t kMsgNoVideoBackground = 0x00535ccc;
constexpr std::uint32_t kMsgNoTipsItem = 0x00535d14;
constexpr std::uint32_t kMsgNoPanelItem = 0x00535cfc;
constexpr std::uint32_t kMsgUnrecognizedState = 0x00527c8c;
constexpr std::uint32_t kMsgUnrecognizedOption = 0x005254c4;
constexpr std::uint32_t kMsgUnrecognizedOptionId = 0x00535d3c;
constexpr std::uint32_t kMsgLeak = 0x00526874;
constexpr std::uint32_t kMsgNothingToDelete = 0x00535d28;

}  // namespace

// ---- the screen call and the loop ---------------------------------------------------------------------------------------------------------------------

// 0x00481200: `*result = run()`.
void OptionsScreen::screen_call(std::uint32_t argument, std::uint32_t result) {
    (void)argument;
    set_word_at(m_, result, u(run()));
}

// 0x00483ef0: the options screen: the pre file and three fonts, the window (its state set up by 0x00482770), frames until the window's poll answers (1: back,
// 2: leave) or the engine asks to leave (the options are applied then), the options applied, the window and fonts gone. Answers 0, or -1 when the poll answered 2.
std::int32_t OptionsScreen::run() {
    const std::uint32_t saved = global_word(m_, kGlobalFrontFlag);
    set_global_word(m_, kGlobalFrontFlag, 0);
    engine(0x0042fc70);
    engine(0x004a90f0, {global_word(m_, kPreManager), kPreName, 0});
    for (std::uint32_t font : kFonts) {
        font_load(font);
    }
    const std::uint32_t memory = operator_new(kOptionsMenuSize);
    std::uint32_t window = 0;
    if (memory != 0) {
        window = seams_ ? engine_value(0x00481210, {memory}) : menu_construct(memory);
    }
    engine(0x004a9330, {global_word(m_, kPreManager), kPreName});
    if (seams_) {
        engine(0x00482770, {window});
    } else {
        sync(window);
    }
    engine(0x00489670, {kPad0});
    engine(0x00489670, {kPad1});
    engine(0x004c8f30);
    const std::int32_t result = frame_loop(
        [&] { return static_cast<std::int32_t>(seams_ ? engine_value(0x00482e60, {window}) : menu_step(window)); },
        [&] {
            if (seams_) {
                vcall_item("element_update", window, 1);       // (the window's update and draw are virtual calls: a reported service on the replay's fake window)
            } else {
                menu_update(window);
            }
        },
        [&] {
            if (seams_) {
                vcall_item("element_draw", window, 2, {0, 0});
            } else {
                menu_draw(window, 0, 0);
            }
        });
    if (result == 0) {    // (the engine asked to leave: the options are applied once more)
        if (seams_) {
            engine(0x00482870, {window});
        } else {
            apply(window);
        }
    }
    set_global_word(m_, kGlobalFrontFlag, saved);
    if (seams_) {
        engine(0x00482870, {window});
    } else {
        apply(window);
    }
    if (window != 0) {
        if (seams_) {
            delete_object(window);
        } else {
            menu_delete(window, 1);
        }
    }
    for (std::uint32_t font : kFonts) {
        font_unload(font);
    }
    engine(0x0046f480);
    return result != 2 ? 0 : -1;
}

// ---- the window --------------------------------------------------------------------------------------------------------------------------------------

// 0x00481210: the window base, two fonts, the backgrounds, four lists with their styles and rows, the arrows, four prompt windows, the cheats page, the camera and light
// rig of the three models (memory card, boombox, controller). The state starts at 0x14, or where the credits or the saved data list were left.
std::uint32_t OptionsScreen::menu_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    font_construct(self + kFontA);
    font_construct(self + kFontB);
    options_menu_menu_at(m_, self).set_field_0(static_cast<Word>(kVtOptionsMenu));
    if (global_word(m_, kReturnCredits) != 0) {
        set_global_word(m_, kReturnCredits, 0);
        options_menu_menu_at(m_, self).set_field_96c(static_cast<Word>(kStateMovies));
    } else if (global_word(m_, kReturnSaved) != 0) {
        set_global_word(m_, kReturnSaved, 0);
        options_menu_menu_at(m_, self).set_field_96c(static_cast<Word>(kStateSaved));
    } else {
        options_menu_menu_at(m_, self).set_field_96c(static_cast<Word>(kStateMain));
    }
    options_menu_menu_at(m_, self).set_field_970(static_cast<Word>(options_menu_menu_at(m_, self).field_96c()));
    options_menu_menu_at(m_, self).set_field_ac(static_cast<Word>(background_sprite()));
    // the video background: a 64 x 64 picture
    std::uint32_t memory = operator_new(0x5c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteConstruct, {memory, 0x00535cc0, 1, 0x40, 0x40}, m_.cstring(0x00535cc0));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    options_menu_menu_at(m_, self).set_field_b0(static_cast<Word>(sprite));
    s_.note(kSpriteInit, {sprite});
    // the first font: mainf, justification 1, grey, scale 0xc00
    copy_font_named(self + kFontA, kFonts[0]);
    set_word_at(m_, self + kFontA + 4, 1);
    set_word_at(m_, self + kFontA + 8, 3);
    set_word_at(m_, self + kFontA + 0x14, 0x80);
    set_word_at(m_, self + kFontA + 0x18, 0x80);
    set_word_at(m_, self + kFontA + 0x1c, 0x80);
    set_byte_at(m_, self + kFontA + 0x21, 0);
    set_word_at(m_, self + kFontA + 0xc, 0);
    set_word_at(m_, self + kFontA + 0x10, 0);
    set_word_at(m_, self + kFontA + 0x30, 0xc00);
    // the second: s2font2
    copy_font_named(self + kFontB, kFonts[1]);
    set_word_at(m_, self + kFontB + 4, 0);
    set_word_at(m_, self + kFontB + 8, 3);
    set_word_at(m_, self + kFontB + 0x14, 0x80);
    set_word_at(m_, self + kFontB + 0x18, 0x80);
    set_word_at(m_, self + kFontB + 0x1c, 0x80);
    set_byte_at(m_, self + kFontB + 0x21, 0);
    set_word_at(m_, self + kFontB + 0xc, 0);
    set_word_at(m_, self + kFontB + 0x30, 0x800);
    // the four lists
    auto list = [&](std::uint32_t slot, std::uint32_t gap, std::uint32_t arg) {
        const std::uint32_t block = operator_new(0x1bc);
        std::uint32_t made = 0;
        if (block != 0) {
            made = menu_.list_construct(block, 0, 4, 0x200, 0xf0, 1, 1, gap, arg);
        }
        set_word_at(m_, self + slot, made);
    };
    list(kListMain, 2, 1);
    list(kListDisplay, 3, 0);
    list(kListMovies, 3, 0);
    list(kListSaved, 3, 0);
    // the rows' style: mainf, grey, scale 0xc00
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_menu_menu_at(m_, self).set_field_35c(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, self + kFontA);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    // the title's style: the same with another highlight colour
    memory = operator_new(0x17c);
    const std::uint32_t title_style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_menu_menu_at(m_, self).set_field_360(static_cast<Word>(title_style));
    menu_.copy_font_fields(title_style + 4, style + 4);
    for (std::uint32_t offset = 0x150; offset <= 0x178; offset += 4) {
        set_word_at(m_, title_style + offset, word_at(m_, style + offset));
    }
    set_word_at(m_, title_style + 0x158, 0x91);
    set_word_at(m_, title_style + 0x15c, 0x12);
    set_word_at(m_, title_style + 0x160, 0xf);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kRowsMain + 4 * i, 0);
        set_word_at(m_, self + kRowsMovies + 4 * i, 0);
        set_word_at(m_, self + kRowsSaved + 4 * i, 0);
    }
    // the rows
    auto text_row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text_variable) {
        const std::uint32_t block = operator_new(0x54);
        std::uint32_t row = 0;
        if (block != 0) {
            row = menu_.text_construct(block, id, word_at(m_, text_variable), options_menu_menu_at(m_, self).field_35c());
        }
        set_word_at(m_, self + slot, row);
        return row;
    };
    auto title_row = [&](std::uint32_t slot, std::uint32_t text_variable) {
        const std::uint32_t block = operator_new(0x58);
        std::uint32_t row = 0;
        if (block != 0) {
            row = menu_.text_scaled_construct(block, 0, word_at(m_, text_variable), options_menu_menu_at(m_, self).field_360(), 1);
        }
        set_word_at(m_, self + slot, row);
    };
    // a stepper row: its words (the label, then on and off) are an array in the caller's frame
    auto stepper_row = [&](std::uint32_t slot, std::uint32_t slot_copy, std::uint32_t id, std::uint32_t label_variable) {
        scratch_line({});
        set_global_word(m_, kScratchLine, word_at(m_, label_variable));
        set_global_word(m_, kScratchLine + 4, global_word(m_, 0x0052e52c));
        set_global_word(m_, kScratchLine + 8, global_word(m_, 0x0052e530));
        const std::uint32_t block = operator_new(0x104);
        std::uint32_t row = 0;
        if (block != 0) {
            row = engine_value(0x004189c0, {block, id, options_menu_menu_at(m_, self).field_35c(), kScratchLine, 3, 1});
        }
        set_word_at(m_, self + slot, row);
        set_word_at(m_, self + slot_copy, row);
    };
    const std::uint32_t player_row = text_row(kRowsMain, 1, 0x0052eaec);
    const std::uint32_t display_row = text_row(kRowsMain + 4, 2, 0x0052eaf0);
    if (host_options().disable_controls) {
        // greyed like the cheats row when there are none to enter
        set_byte_at(m_, player_row + 0xc, 0);
        set_byte_at(m_, display_row + 0xc, 0);
    }
    text_row(kRowsMain + 8, 3, 0x0052e774);
    text_row(kRowsMain + 0xc, 4, 0x0052e778);
    text_row(kRowsMain + 0x10, 5, 0x0052e77c);
    const std::uint32_t cheats_row = text_row(kRowsMain + 0x14, 0x11, 0x0052e318);
    set_byte_at(m_, cheats_row + 0xc, cheats_available() ? 1 : 0);
    text_row(kRowsMain + 0x18, 0xc, 0x0052e780);
    text_row(kRowsMain + 0x1c, 0xd, 0x0052e7a0);
    text_row(kRowsMain + 0x20, 0x12, 0x0052e31c);
    text_row(kRowsMain + 0x28, 0x13, 0x0052e5d8);
    title_row(kRowsDisplay, 0x0052e31c);
    stepper_row(kRowsDisplay + 4, kTipsRow, 6, 0x0052e784);
    stepper_row(kRowsDisplay + 8, kScoreRow, 7, 0x0052e310);
    text_row(kRowsDisplay + 0xc, 0x13, 0x0052e79c);
    title_row(kRowsMovies, 0x0052e778);
    text_row(kRowsMovies + 4, 8, 0x0052e78c);
    text_row(kRowsMovies + 8, 9, 0x0052e790);
    text_row(kRowsMovies + 0xc, 0xa, 0x0052e794);
    text_row(kRowsMovies + 0x10, 0x13, 0x0052e79c);
    title_row(kRowsSaved, 0x0052e77c);
    text_row(kRowsSaved + 4, 0xe, 0x0052e7a4);
    text_row(kRowsSaved + 8, 0xf, 0x0052e314);
    text_row(kRowsSaved + 0xc, 0x10, 0x0052e7a8);
    text_row(kRowsSaved + 0x10, 0x13, 0x0052e79c);
    // each list gets its rows
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::pair<std::uint32_t, std::uint32_t> arrays[] = {{kRowsMain, kListMain}, {kRowsDisplay, kListDisplay}, {kRowsMovies, kListMovies}, {kRowsSaved, kListSaved}};
        for (const auto& [rows, list_slot] : arrays) {
            const std::uint32_t row = word_at(m_, self + rows + 4 * i);
            if (row != 0) {
                vcall_list("list_v6", word_at(m_, self + list_slot), 6, {row});
            }
        }
    }
    // the two arrows
    auto arrow = [&](std::uint32_t slot, std::uint32_t file) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t made = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 1, 0, 0}, m_.cstring(file));
            made = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, made);
        s_.note(kSpriteInit, {made});
    };
    arrow(kArrowA, 0x00527c20);
    arrow(kArrowB, 0x00527c10);
    for (std::uint32_t list_slot : {kListMain, kListDisplay, kListMovies, kListSaved}) {
        vcall_list("list_v1", word_at(m_, self + list_slot), 1, {options_menu_menu_at(m_, self).field_964(), options_menu_menu_at(m_, self).field_968(), 5});
        vcall_list("list_v2", word_at(m_, self + list_slot), 2);
    }
    // the prompt windows: select (the display list), select, accept, back
    const struct {
        std::uint32_t file;
        std::int32_t x;
    } prompts[] = {{0x00535cb0, 0x8c}, {0x00535ca0, 0xf0}, {0x00535c90, 0x190}, {0x00525400, 0x32}};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t window = prompt(prompts[i].file, 0x005252f0, self + kFontB, prompts[i].x, 0xdc);
        set_word_at(m_, self + kPrompts + 4 * i, window);
        w_.window_add(self, window);
    }
    for (std::uint32_t slot : {kPageGaps, kPageScores, kPageSound, kPageCard, kPageReplay}) {
        set_word_at(m_, self + slot, 0);
    }
    // the cheats page
    memory = operator_new(0x288);
    std::uint32_t cheats = 0;
    if (memory != 0) {
        cheats = engine_value(0x004885d0, {memory, self + kFontA, 0, 0, u(-1), u(-1), options_menu_menu_at(m_, self).field_964(), options_menu_menu_at(m_, self).field_968()});
    }
    options_menu_menu_at(m_, self).set_field_9a4(static_cast<Word>(cheats));
    w_.window_add(self, cheats);
    // the models' view (the screen's width and height, a depth of 10 .. 30000), their light rig, the camera and fog
    set_half_at(m_, self + kView + 2, 0xf0);
    set_half_at(m_, self + kView + 6, 0);
    options_menu_menu_at(m_, self).set_field_9b8(0x200);
    set_half_at(m_, self + kView + 4, 0);
    set_half_at(m_, self + kView + 6, 0);
    options_menu_menu_at(m_, self).set_field_9b8(static_cast<std::uint16_t>(global_word(m_, kGlobalScreenWidth) & 0x7fffff));
    set_half_at(m_, self + kView + 8, 10);
    set_half_at(m_, self + kView + 0xa, 30000);
    set_half_at(m_, self + kView + 2, static_cast<std::uint32_t>((0x88888889ull * static_cast<std::uint32_t>(global_word(m_, kGlobalScreenHeight) * 3 * 5 * 16)) >> 39));
    set_half_at(m_, self + kView + 0xc, 0x6aa);
    engine(0x00418200, {self + kLight});
    engine(0x004f5430, {kCameraMatrix});
    set_global_word(m_, kCameraWords, 0);
    set_global_word(m_, kCameraWords + 4, 0);
    set_global_word(m_, kCameraWords + 8, 0xfffffda8u);
    set_half_at(m_, self + kView + 0xc, 0x6aa);
    engine(0x004e8460, {kCameraMatrix, kCameraWords});
    engine(0x00464710, {10, 0x1770, 0x800});
    engine(0x004645f0);
    const struct {
        std::uint32_t slot;
        std::uint32_t name;
    } models[] = {{kModelCard, 0x00535c84}, {kModelBoombox, 0x00535c7c}, {kModelControl, 0x00535c74}};
    for (const auto& model : models) {
        const std::uint32_t block = operator_new(0x40);
        std::uint32_t view = 0;
        if (block != 0) {
            view = engine_value(0x00418fc0, {block, model.name, kCamera, self + kView, self + kLight, 0, 0, 0}, m_.cstring(model.name));
        }
        set_word_at(m_, self + model.slot, view);
        set_word_at(m_, view + 0x28, 0x1e);
        w_.window_add(self, view);
    }
    return self;
}

// 0x00482450: whether any cheat is unlocked.
bool OptionsScreen::cheats_available() { return static_cast<std::int32_t>(engine_value(0x00416de0)) > 0; }

std::uint32_t OptionsScreen::menu_delete(std::uint32_t self, std::uint32_t flag) {
    menu_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00482480: the background (its sprite object, then its block), the video background, the lists, the styles, the arrows, the pages and the rows, the two fonts and the
// window base.
void OptionsScreen::menu_destroy(std::uint32_t self) {
    options_menu_menu_at(m_, self).set_field_0(static_cast<Word>(kVtOptionsMenu));
    check(options_menu_menu_at(m_, self).field_ac() != 0, kMsgNoBackground);
    const std::uint32_t sprite = options_menu_menu_at(m_, self).field_ac();
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    check(options_menu_menu_at(m_, self).field_b0() != 0, kMsgNoVideoBackground);
    for (std::uint32_t slot : {kVideoBackground, kListMain, kListDisplay, kListMovies, kListSaved, kStyleRows, kStyleTitle, kArrowA, kArrowB, kPageGaps, kPageScores,
                               kPageMovies, kPageSound, kPageCard, kPageReplay, kPageControls}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        for (std::uint32_t rows : {kRowsMain, kRowsDisplay, kRowsMovies, kRowsSaved}) {
            if (word_at(m_, self + rows + 4 * i) != 0) {
                delete_object(word_at(m_, self + rows + 4 * i));
            }
        }
    }
    font_destroy(self + kFontB);
    font_destroy(self + kFontA);
    w_.window_destroy(self);
}

// 0x004826f0: the four prompt windows show (or hide, when `changed`) and take the caption's height; the ones in `mask` (select, select, accept, back) are lowered to 0xdc.
void OptionsScreen::set_prompts(std::uint32_t self, std::uint32_t changed, std::uint32_t mask) {
    const std::uint32_t hidden = (changed & 0xff) == 0 ? 1u : 0u;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint32_t prompt_window = word_at(m_, self + kPrompts + 4 * i);
        set_word_at(m_, prompt_window + 0x14, hidden);
        set_word_at(m_, prompt_window + 0xc, global_word(m_, kScreenFillHeight) + 0x14);
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        if ((mask & (1u << i)) != 0) {
            set_word_at(m_, word_at(m_, self + kPrompts + 4 * i) + 0xc, 0xdc);
        }
    }
}

// 0x00482770: coming back from the controller screens the list highlights the entry that was left; the two trick tips and score display rows show the options.
void OptionsScreen::sync(std::uint32_t self) {
    if (global_byte(m_, kReturnFlagPlayer) != 0) {
        menu_.list_highlight(word_at(m_, self + kListMain), 1);
    } else if (global_byte(m_, kReturnFlagDisplay) != 0) {
        menu_.list_highlight(word_at(m_, self + kListMain), 2);
    }
    set_global_byte(m_, kReturnFlagPlayer, 0);
    set_global_byte(m_, kReturnFlagDisplay, 0);
    check(word_at(m_, self + kTipsRow) != 0, kMsgNoTipsItem);
    set_word_at(m_, word_at(m_, self + kTipsRow) + 0x10, global_word(m_, kTipsOption) != 0 ? 1u : 2u);
    check(word_at(m_, self + kScoreRow) != 0, kMsgNoPanelItem);
    set_word_at(m_, word_at(m_, self + kScoreRow) + 0x10, global_word(m_, kPanelOption) != 0 ? 1u : 2u);
    engine(0x00489240, {word_at(m_, self + kPageCheats)});
}

// 0x00482870: unless the saved data list was just left, the two rows' values become the options (and the cheats page's), then the live state is written to the career.
void OptionsScreen::apply(std::uint32_t self) {
    if (global_word(m_, kReturnSaved) == 0) {
        check(word_at(m_, self + kTipsRow) != 0, kMsgNoTipsItem);
        set_global_word(m_, kTipsOption, word_at(m_, word_at(m_, self + kTipsRow) + 0x10) == 1 ? 1u : 0u);
        check(word_at(m_, self + kScoreRow) != 0, kMsgNoPanelItem);
        set_global_word(m_, kPanelOption, word_at(m_, word_at(m_, self + kScoreRow) + 0x10) == 1 ? 1u : 0u);
        engine(0x004892a0, {word_at(m_, self + kPageCheats)});
    }
    engine(0x00413c10);
}

// 0x00482950: the window; the video background in the two states that show the gap checklist or the high scores; then the background.
void OptionsScreen::menu_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    w_.window_draw(self, x, y);
    const std::uint32_t state = options_menu_menu_at(m_, self).field_96c();
    if (state == kStateScores || state == kStateGaps) {
        check(options_menu_menu_at(m_, self).field_b0() != 0, kMsgNoBackground);
        vcall_item("sprite_draw", options_menu_menu_at(m_, self).field_b0(), 3, {0x100, 0, 0});
    }
    check(options_menu_menu_at(m_, self).field_ac() != 0, kMsgNoBackground);
    engine(0x004d7990, {options_menu_menu_at(m_, self).field_ac(), word_at(m_, global_word(m_, kGlobalRenderer) + 0x88), 0, 0});
}

// 0x00482a40: a new state sends the sub lists back to their first row and clears the pads' triggers; the models show (the memory card, the boombox, the controller, the
// cheats page) and the state's list or page is updated, the prompts follow (what they offer depends on the state).
void OptionsScreen::menu_update(std::uint32_t self) {
    if (options_menu_menu_at(m_, self).field_970() != options_menu_menu_at(m_, self).field_96c()) {
        menu_.list_highlight_first(options_menu_menu_at(m_, self).field_354());
        menu_.list_highlight_first(options_menu_menu_at(m_, self).field_350());
        menu_.list_highlight_first(options_menu_menu_at(m_, self).field_358());
        options_menu_menu_at(m_, self).set_field_96c(static_cast<Word>(options_menu_menu_at(m_, self).field_970()));
        engine(0x00489670, {kPad0});
        engine(0x00489670, {kPad1});
    }
    for (std::uint32_t slot : {kModelCard, kModelBoombox, kModelControl, kPageCheats}) {
        set_word_at(m_, word_at(m_, self + slot) + 0x14, 1);
    }
    bool changed = false;
    std::uint32_t mask = 0;
    const std::uint32_t x = options_menu_menu_at(m_, self).field_20();
    const std::uint32_t y = options_menu_menu_at(m_, self).field_24();
    auto run_page = [&](std::uint32_t slot) {
        vcall_item("element_update", word_at(m_, self + slot), 1);
        vcall_item("element_draw", word_at(m_, self + slot), 2, {x, y});
    };
    // a list that follows the pad: it draws, takes the edge the pad gave and snaps
    auto move_list = [&](std::uint32_t slot) {
        vcall_list("list_v3", word_at(m_, self + slot), 3);
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        vcall_list("list_v5", word_at(m_, self + slot), 5, {edge});
        vcall_list("list_v4", word_at(m_, self + slot), 4);
    };
    switch (options_menu_menu_at(m_, self).field_96c()) {
    case kStateMain: {
        vcall_list("list_v2", options_menu_menu_at(m_, self).field_34c(), 2);
        const std::uint32_t edge = engine_value(0x0046f650);
        vcall_list("list_v5", options_menu_menu_at(m_, self).field_34c(), 5, {edge});
        vcall_list("list_v4", options_menu_menu_at(m_, self).field_34c(), 4);
        vcall_list("list_v3", options_menu_menu_at(m_, self).field_34c(), 3);
        mask = 9;
        changed = true;
        break;
    }
    case kStateCheats:
        set_word_at(m_, options_menu_menu_at(m_, self).field_9a4() + 0x14, 0);
        mask = 9;
        if (engine_value(0x00489350, {options_menu_menu_at(m_, self).field_9a4()}) != 0) {
            mask = 0xb;
        }
        if (engine_value(0x00489390, {options_menu_menu_at(m_, self).field_9a4()}) != 0) {
            mask |= 4;
        }
        changed = mask != 0;
        break;
    case kStateDisplay: {
        const std::uint32_t id = menu_.list_highlighted_id(options_menu_menu_at(m_, self).field_350());
        const bool stepper = id == 6 || id == 7;
        const std::uint32_t edge = engine_value(0x0046f650);
        if ((edge == 0 || edge == 1) && stepper) {
            sfx(0x11f);
        }
        vcall_list("list_v3", options_menu_menu_at(m_, self).field_350(), 3);
        vcall_list("list_v5", options_menu_menu_at(m_, self).field_350(), 5, {edge});
        vcall_list("list_v4", options_menu_menu_at(m_, self).field_350(), 4);
        mask = stepper ? 0xf : 0xd;
        changed = true;
        break;
    }
    case kStateMovies:
        move_list(kListMovies);
        mask = 0xd;
        changed = true;
        break;
    case kStateGaps:
        run_page(kPageGaps);
        break;
    case kStateScores:
        run_page(kPageScores);
        break;
    case kStateMovieList:
        run_page(kPageMovies);
        mask = 0xd;
        changed = true;
        break;
    case kStateSound:
        set_word_at(m_, options_menu_menu_at(m_, self).field_99c() + 0x14, 0);
        run_page(kPageSound);
        mask = 9;
        if (engine_value(0x00487280, {options_menu_menu_at(m_, self).field_988()}) != 0) {
            mask = 0xb;
        }
        if (engine_value(0x004872a0, {options_menu_menu_at(m_, self).field_988()}) != 0) {
            mask |= 4;
        }
        changed = mask != 0;
        break;
    case kStateSaved:
        set_word_at(m_, options_menu_menu_at(m_, self).field_998() + 0x14, 0);
        move_list(kListSaved);
        mask = 0xd;
        changed = true;
        break;
    case kStateCard:
        set_word_at(m_, options_menu_menu_at(m_, self).field_998() + 0x14, 0);
        run_page(kPageCard);
        break;
    case kStateReplay:
        set_word_at(m_, options_menu_menu_at(m_, self).field_998() + 0x14, 0);
        run_page(kPageReplay);
        break;
    case kStateControls:
        set_word_at(m_, options_menu_menu_at(m_, self).field_9a0() + 0x14, 0);
        run_page(kPageControls);
        mask = 9;
        if (engine_value(0x00488590, {options_menu_menu_at(m_, self).field_994()}) != 0) {
            mask = 0xb;
        }
        if (engine_value(0x004885c0, {options_menu_menu_at(m_, self).field_994()}) != 0) {
            mask |= 4;
        }
        changed = mask != 0;
        break;
    default:
        check(false, kMsgUnrecognizedState);
        break;
    }
    set_prompts(self, changed ? 1 : 0, mask);
    w_.window_update(self);
}

// ---- the poll -----------------------------------------------------------------------------------------------------------------------------------------

bool OptionsScreen::nav_edge() {
    return global_byte(m_, kPadUp) != 0 || global_byte(m_, kPadUp + kPadStride) != 0 || global_byte(m_, kPadUp2) != 0 || global_byte(m_, kPadUp2 + kPadStride) != 0;
}

void OptionsScreen::nav_sound() {
    if (nav_edge()) {
        sfx(0x11f);
    }
}

// A page is built when the entry that opens it is chosen: the state moves, the slot must be empty ("Possible memory leak"), and the page's constructor is a seam.
std::uint32_t OptionsScreen::make_page(std::uint32_t self, std::uint32_t slot, std::uint32_t line, std::uint32_t size, std::uint32_t constructor,
                                       std::vector<std::uint32_t> args) {
    (void)line;
    check(word_at(m_, self + slot) == 0, kMsgLeak);
    const std::uint32_t memory = operator_new(size);
    std::uint32_t page = 0;
    if (memory != 0) {
        args.insert(args.begin(), memory);
        page = engine_value(constructor, std::move(args));
    }
    set_word_at(m_, self + slot, page);
    return page;
}

// A page is dropped when it answers: "Nothing to delete" if it is gone, its virtual delete, the slot cleared.
void OptionsScreen::drop_page(std::uint32_t self, std::uint32_t slot, std::uint32_t line) {
    (void)line;
    check(word_at(m_, self + slot) != 0, kMsgNothingToDelete);
    if (word_at(m_, self + slot) != 0) {
        delete_object(word_at(m_, self + slot));
    }
    set_word_at(m_, self + slot, 0);
}

// 0x00482e60: the poll of the state the window is in (the entries of a jump table); a state the table does not hold is reported and answers 0.
std::uint32_t OptionsScreen::menu_step(std::uint32_t self) {
    switch (options_menu_menu_at(m_, self).field_96c()) {
    case kStateMain: return step_main(self);
    case kStateCheats: return step_cheats(self);
    case kStateDisplay: return step_display(self);
    case kStateMovies: return step_movies(self);
    case kStateGaps: return step_gaps(self);
    case kStateScores: return step_scores(self);
    case kStateMovieList: return step_movie_list(self);
    case kStateSound: return step_sound(self);
    case kStateSaved: return step_saved(self);
    case kStateCard: return step_card(self);
    case kStateReplay: return step_replay(self);
    case kStateControls: return step_controls(self);
    default:
        check(false, kMsgUnrecognizedState);
        return 0;
    }
}

// 0x00482f50: the main list. Confirm opens the entry: the controller screens (answer 1, with the entry remembered), a page (the state moves; the sound, gap checklist and
// high scores pages are built here), or back (answer 1). Back on the pad answers 2.
std::uint32_t OptionsScreen::step_main(std::uint32_t self) {
    nav_sound();
    if (global_byte(m_, kPadConfirmEdge) == 0 && global_byte(m_, kPadConfirmEdge + kPadStride) == 0) {
        if (global_byte(m_, kPadBackEdge) == 0 && global_byte(m_, kPadBackEdge + kPadStride) == 0) {
            return 0;
        }
        sfx(0x93);
        set_global_byte(m_, kPadBackEdge, 0);
        set_global_byte(m_, kPadBackEdge + kPadStride, 0);
        return 2;
    }
    const std::uint32_t chosen = menu_.list_highlighted_id(options_menu_step_at(m_, self).field_34c());
    if (host_options().disable_controls && (chosen == 1 || chosen == 2)) {
        sfx(0x92);
        set_global_byte(m_, kPadConfirmEdge, 0);
        set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
        return 0;
    }
    if (global_byte(m_, kPadConfirmEdge + kPadStride) != 0 && (chosen == 1 || chosen == 0xb || chosen == 5 || chosen == 4)) {
        // (the second pad cannot open these)
        sfx(0x92);
        set_global_byte(m_, kPadConfirmEdge, 0);
        set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
        return 0;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
    const std::uint32_t id = menu_.list_highlighted_id(options_menu_step_at(m_, self).field_34c());
    const std::uint32_t arrow_a = options_menu_step_at(m_, self).field_964();
    const std::uint32_t arrow_b = options_menu_step_at(m_, self).field_968();
    switch (id) {
    case 1:
        set_global_byte(m_, kReturnFlagPlayer, 1);
        return 1;
    case 2:
        set_global_byte(m_, kReturnFlagDisplay, 1);
        return 1;
    case 3:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateSound));
        make_page(self, kPageSound, 0x3b3, 0x244, 0x00486b10, {self + kFontA, 0, 0, u(-1), u(-1), arrow_a, arrow_b});
        return 0;
    case 4:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMovies));
        return 0;
    case 5:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateSaved));
        return 0;
    case 0xb:
        set_global_byte(m_, kLeaveFlag, 1);
        return 0;
    case 0xc:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateScores));
        make_page(self, kPageScores, 0x3be, 0x7ac, 0x00485400, {self + kFontA, 0, 0, u(-1), u(-1)});
        return 0;
    case 0xd:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateGaps));
        make_page(self, kPageGaps, 0x3c3, 0x504, 0x004840b0, {self + kFontA, 0, 0, u(-1), u(-1)});
        return 0;
    case 0x11:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateCheats));
        return 0;
    case 0x12:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateDisplay));
        return 0;
    case 0x13:
        return 1;
    default:
        check(false, kMsgUnrecognizedOption);
        return 0;
    }
}

// 0x004836f0: the cheats page answers 1 or 2 when it is left; the cheats are stored and the main list returns.
std::uint32_t OptionsScreen::step_cheats(std::uint32_t self) {
    const std::uint32_t answer = engine_value(0x004891a0, {options_menu_step_at(m_, self).field_9a4()});
    if (answer == 1 || answer == 2) {
        engine(0x004892a0, {options_menu_step_at(m_, self).field_9a4()});
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
    }
    return 0;
}

// 0x00483370: the display options list: back (the pad or the Back row) returns to the main list; the two stepper rows and the title do nothing here.
std::uint32_t OptionsScreen::step_display(std::uint32_t self) {
    nav_sound();
    if (global_byte(m_, kPadConfirmEdge) == 0 && global_byte(m_, kPadConfirmEdge + kPadStride) == 0) {
        if (global_byte(m_, kPadBackEdge) == 0 && global_byte(m_, kPadBackEdge + kPadStride) == 0) {
            return 0;
        }
        sfx(0x93);
        set_global_byte(m_, kPadBackEdge, 0);
        set_global_byte(m_, kPadBackEdge + kPadStride, 0);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        return 0;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
    const std::uint32_t id = menu_.list_highlighted_id(options_menu_step_at(m_, self).field_350());
    if (id > 0x13) {
        check(false, kMsgUnrecognizedOption);
        return 0;
    }
    if (id == 0x13) {
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
    } else if (id != 0 && id != 6 && id != 7) {
        check(false, kMsgUnrecognizedOption);
    }
    return 0;
}

// 0x004834b0: the movies list: Cinematics opens the movie list page, the credits answer 1 (with the credits chosen), Back returns to the main list.
std::uint32_t OptionsScreen::step_movies(std::uint32_t self) {
    engine(0x00489670, {kPad1});
    nav_sound();
    if (global_byte(m_, kPadConfirmEdge) == 0 && global_byte(m_, kPadConfirmEdge + kPadStride) == 0) {
        if (global_byte(m_, kPadBackEdge) != 0 || global_byte(m_, kPadBackEdge + kPadStride) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            set_global_byte(m_, kPadBackEdge + kPadStride, 0);
            options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        }
        return 0;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
    const std::uint32_t id = menu_.list_highlighted_id(options_menu_step_at(m_, self).field_354());
    switch (id) {
    case 8:
        make_page(self, kPageMovies, 0x42c, 0x238, 0x004860d0, {self + kFontA, 0, 0, u(-1), u(-1), options_menu_step_at(m_, self).field_964(), options_menu_step_at(m_, self).field_968()});
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMovieList));
        return 0;
    case 9:
        set_global_word(m_, kReturnCredits, 1);
        return 1;
    case 0xa:
        set_global_word(m_, kReturnCredits, 2);
        return 1;
    case 0x13:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        return 0;
    default:
        check(false, kMsgUnrecognizedOption);
        return 0;
    }
}

// 0x00483730: back leaves the gap checklist.
std::uint32_t OptionsScreen::step_gaps(std::uint32_t self) {
    if (global_byte(m_, kPadBackEdge) != 0 || global_byte(m_, kPadBackEdge + kPadStride) != 0) {
        sfx(0x93);
        set_global_byte(m_, kPadBackEdge, 0);
        set_global_byte(m_, kPadBackEdge + kPadStride, 0);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        drop_page(self, kPageGaps, 0x46a);
    }
    return 0;
}

// 0x004837e0: back leaves the high scores.
std::uint32_t OptionsScreen::step_scores(std::uint32_t self) {
    if (global_byte(m_, kPadBackEdge) != 0 || global_byte(m_, kPadBackEdge + kPadStride) != 0) {
        sfx(0x93);
        set_global_byte(m_, kPadBackEdge, 0);
        set_global_byte(m_, kPadBackEdge + kPadStride, 0);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        drop_page(self, kPageScores, 0x47f);
    }
    return 0;
}

// 0x00483890: the movie list page answers 1 or 2 when it is left; the movies list returns.
std::uint32_t OptionsScreen::step_movie_list(std::uint32_t self) {
    engine(0x00489670, {kPad1});
    const std::uint32_t answer = engine_value(0x00486830, {options_menu_step_at(m_, self).field_984()});
    if (answer == 1 || answer == 2) {
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMovies));
        drop_page(self, kPageMovies, 0x496);
    }
    return 0;
}

// 0x00483930: the sound levels page answers 1 or 2 when it is left; the main list returns.
std::uint32_t OptionsScreen::step_sound(std::uint32_t self) {
    const std::uint32_t answer = engine_value(0x004871e0, {options_menu_step_at(m_, self).field_988()});
    if (answer == 1 || answer == 2) {
        drop_page(self, kPageSound, 0x4a9);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
    }
    return 0;
}

// 0x004839c0: the saved data list: Load data and Save data open the memory card page (saving first applies the options), Load replay the replay page, Back returns to
// the main list.
std::uint32_t OptionsScreen::step_saved(std::uint32_t self) {
    engine(0x00489670, {kPad1});
    nav_sound();
    if (global_byte(m_, kPadConfirmEdge) == 0 && global_byte(m_, kPadConfirmEdge + kPadStride) == 0) {
        if (global_byte(m_, kPadBackEdge) != 0 || global_byte(m_, kPadBackEdge + kPadStride) != 0) {
            sfx(0x93);
            set_global_byte(m_, kPadBackEdge, 0);
            set_global_byte(m_, kPadBackEdge + kPadStride, 0);
            options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        }
        return 0;
    }
    sfx(0x91);
    set_global_byte(m_, kPadConfirmEdge, 0);
    set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
    const std::uint32_t id = menu_.list_highlighted_id(options_menu_step_at(m_, self).field_358());
    switch (id) {
    case 0xe:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateCard));
        make_page(self, kPageCard, 0x4d4, 0xb0, 0x004872b0, {1, self + kFontA, 0, 0, u(-1), u(-1), 0, 0});
        return 0;
    case 0xf:
        apply(self);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateCard));
        make_page(self, kPageCard, 0x4cf, 0xb0, 0x004872b0, {0, self + kFontA, 0, 0, u(-1), u(-1), 0, 0});
        return 0;
    case 0x10:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateReplay));
        make_page(self, kPageReplay, 0x4d9, 0xac, 0x004873b0, {self + kFontA, 0, 0, u(-1), u(-1), 0, 0});
        return 0;
    case 0x13:
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
        return 0;
    default:
        check(false, kMsgUnrecognizedOptionId);
        return 0;
    }
}

// 0x00483da0: the memory card page answers 1 or 2 when it is left; the saved data list returns, or (answer 1 on a page that flagged it) the screen leaves for the loaded
// game.
std::uint32_t OptionsScreen::step_card(std::uint32_t self) {
    const std::uint32_t answer = engine_value(0x00487390, {options_menu_step_at(m_, self).field_98c()});
    if (answer == 1 || answer == 2) {
        check(options_menu_step_at(m_, self).field_98c() != 0, kMsgNothingToDelete);
        const std::uint32_t page = options_menu_step_at(m_, self).field_98c();
        const std::uint8_t loaded = page != 0 ? byte_at(m_, page + 0xac) : 0;
        if (page != 0) {
            delete_object(page);
        }
        options_menu_step_at(m_, self).set_field_98c(0);
        if (answer == 1 && loaded != 0) {
            set_global_word(m_, kReturnSaved, 1);
            return 1;
        }
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateSaved));
    }
    return 0;
}

// 0x00483d00: the replay page answers 1 or 2 when it is left; the saved data list returns. Answers 1 when the replay was chosen.
std::uint32_t OptionsScreen::step_replay(std::uint32_t self) {
    const std::uint32_t answer = engine_value(0x00487470, {options_menu_step_at(m_, self).field_990()});
    if (answer == 1 || answer == 2) {
        drop_page(self, kPageReplay, 0x4fa);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateSaved));
    }
    return answer == 1 ? 1u : 0u;
}

// 0x00483e60: the controller page answers 1 or 2 when it is left; the main list returns.
std::uint32_t OptionsScreen::step_controls(std::uint32_t self) {
    const std::uint32_t answer = engine_value(0x00488160, {options_menu_step_at(m_, self).field_994()});
    if (answer == 1 || answer == 2) {
        drop_page(self, kPageControls, 0x533);
        options_menu_step_at(m_, self).set_field_970(static_cast<Word>(kStateMain));
    }
    return 0;
}

}  // namespace opentony::runtime::frontend
