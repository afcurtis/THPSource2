#include "auto_views.hpp"
#include "options.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// HighScores fields
constexpr std::uint32_t kHFontInitials = 0xac;
constexpr std::uint32_t kHFontName = 0x1f8;
constexpr std::uint32_t kHFontScore = 0x344;
constexpr std::uint32_t kHFontTitle = 0x490;
constexpr std::uint32_t kHFontPrompt = 0x5dc;
constexpr std::uint32_t kHInitials = 0x728;     // eight rows of three elements: initials, name, score
constexpr std::uint32_t kHNames = 0x748;
constexpr std::uint32_t kHScores = 0x768;
constexpr std::uint32_t kHShown = 0x788;
constexpr std::uint32_t kHWanted = 0x78c;
constexpr std::uint32_t kHTitle = 0x790;
constexpr std::uint32_t kHZero = 0x794;
constexpr std::uint32_t kHVideo = 0x798;
constexpr std::uint32_t kHPromptSelect = 0x79c;
constexpr std::uint32_t kHPromptBack = 0x7a0;
constexpr std::uint32_t kHArrowLeft = 0x7a4;
constexpr std::uint32_t kHArrowRight = 0x7a8;

constexpr std::uint32_t kLevelTable = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kScoreTable = 0x00569ebc;       // eight bytes an entry, eight entries a level: three letters, the skater, the score
constexpr std::uint32_t kSkaterTable = 0x0053a3f8;      // 0x170 bytes a skater, the name's pointer first
constexpr std::uint32_t kSkaterStride = 0x170;
constexpr std::uint32_t kParkScoresLocked = 0x00561450;
constexpr std::uint32_t kFormatString = 0x005256dc;
constexpr std::uint32_t kFormatNumber = 0x00525764;
constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;

// whether the high scores of a level may be shown: the park's only when it has been opened, and not before the later levels
bool level_shown(RetailMemory& m, std::int32_t level) { return level == 0xd ? guest::read_scalar<std::uint8_t>(m, kParkScoresLocked) == 0 : level < 0xa; }

}  // namespace

// 0x00485400: five fonts (the first four copied from the options window's, the fifth s2font2), eight rows of initials, name and score, the arrows, two prompts, the title
// and the level's video window. The level starts at the current one, moved on to one that has scores to show.
std::uint32_t OptionsScreen::scores_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    for (std::uint32_t slot : {kHFontInitials, kHFontName, kHFontScore, kHFontTitle, kHFontPrompt}) {
        font_construct(self + slot);
    }
    options_scores_scores_at(m_, self).set_field_0(static_cast<Word>(kVtHighScores));
    menu_.copy_font_fields(self + kHFontInitials, font);
    set_word_at(m_, self + kHFontInitials + 4, 1);
    set_word_at(m_, self + kHFontInitials + 0x30, 0xc00);
    menu_.copy_font_fields(self + kHFontName, font);
    set_word_at(m_, self + kHFontName + 0x30, 0xc00);
    set_word_at(m_, self + kHFontName + 0x2c, 0);
    set_word_at(m_, self + kHFontName + 4, 0);
    menu_.copy_font_fields(self + kHFontScore, font);
    set_word_at(m_, self + kHFontScore + 0x2c, 1);
    set_word_at(m_, self + kHFontScore + 4, 2);
    set_word_at(m_, self + kHFontScore + 0xc, 5);
    set_word_at(m_, self + kHFontScore + 0x30, 0xc00);
    menu_.copy_font_fields(self + kHFontTitle, font);
    f_.set_rgb(self + kHFontTitle, 0x91, 0x12, 0xf);
    set_word_at(m_, self + kHFontTitle + 4, 1);
    set_word_at(m_, self + kHFontTitle + 0x30, 0xc00);
    options_scores_scores_at(m_, self).set_field_788(0xffffffffu);
    options_scores_scores_at(m_, self).set_field_78c(static_cast<Word>(global_word(m_, kCurrentLevel)));
    while (gaps_locked(static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c())) || !level_shown(m_, static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c()))) {
        const std::int32_t next = static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c()) + 1;
        options_scores_scores_at(m_, self).set_field_78c(static_cast<Word>(u(next)));
        if (next >= 0xe) {
            options_scores_scores_at(m_, self).set_field_78c(0);
        } else if (next < 0) {
            options_scores_scores_at(m_, self).set_field_78c(0xd);
        }
    }
    // eight rows
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::int32_t row_y = static_cast<std::int32_t>(0x58 + 0x10 * i);
        std::uint32_t memory = operator_new(0xb8);
        std::uint32_t element = 0;
        if (memory != 0) {
            element = engine_value(0x00419290, {memory, self + kHFontInitials, 0x80, u(row_y), 0xffffffffu, 0xffffffffu});
        }
        set_word_at(m_, self + kHInitials + 4 * i, element);
        w_.window_add(self, element);
        memory = operator_new(0x30);
        element = 0;
        if (memory != 0) {
            element = w_.text_construct(memory, 0x00535dc0, self + kHFontName, 0x100, row_y, -1, -1);
        }
        set_word_at(m_, self + kHNames + 4 * i, element);
        w_.window_add(self, element);
        memory = operator_new(0x30);
        element = 0;
        if (memory != 0) {
            element = w_.text_construct(memory, 0x00535db4, self + kHFontScore, 0x180, row_y, -1, -1);
        }
        set_word_at(m_, self + kHScores + 4 * i, element);
        w_.window_add(self, element);
    }
    // the arrows
    auto arrow = [&](std::uint32_t slot, std::uint32_t file, std::uint32_t mode) {
        const std::uint32_t memory = operator_new(0x5c);
        std::uint32_t made = 0;
        if (memory != 0) {
            s_.note(kSpriteConstruct, {memory, file, 1, 0, 0}, m_.cstring(file));
            made = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, made);
        set_byte_at(m_, made + 0xa, mode);
        s_.note(kSpriteInit, {made});
    };
    arrow(kHArrowLeft, 0x00527c10, 2);
    arrow(kHArrowRight, 0x00527c20, 0);
    // the prompts' font: s2font2
    copy_font_named(self + kHFontPrompt, 0x0052544c);
    set_word_at(m_, self + kHFontPrompt + 4, 0);
    set_word_at(m_, self + kHFontPrompt + 8, 3);
    set_word_at(m_, self + kHFontPrompt + 0x14, 0x80);
    set_word_at(m_, self + kHFontPrompt + 0x18, 0x80);
    set_word_at(m_, self + kHFontPrompt + 0x1c, 0x80);
    set_byte_at(m_, self + kHFontPrompt + 0x21, 0);
    set_word_at(m_, self + kHFontPrompt + 0xc, 0);
    set_word_at(m_, self + kHFontPrompt + 0x30, 0x800);
    const std::uint32_t select = prompt(0x005258d8, 0x005252f0, self + kHFontPrompt, 0x8f, 0xdc);
    options_scores_scores_at(m_, self).set_field_79c(static_cast<Word>(select));
    w_.window_add(self, select);
    const std::uint32_t back = prompt(0x00525400, 0x005252f0, self + kHFontPrompt, 0x32, 0xdc);
    options_scores_scores_at(m_, self).set_field_7a0(static_cast<Word>(back));
    w_.window_add(self, back);
    // the title: the level's name, and the words HIGH SCORES
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::uint32_t memory = operator_new(0x34);
        std::uint32_t text = 0;
        if (memory != 0) {
            text = engine_value(0x004a1ed0, {memory, i == 0 ? 0x00527c68u : 0x00535da8u, self + kHFontTitle, 1, 0x8c, i == 0 ? 0x38u : 0x46u, 0xffffffffu, 0xffffffffu});
        }
        if (i == 0) {
            options_scores_scores_at(m_, self).set_field_790(static_cast<Word>(text));
        }
        w_.window_add(self, text);
    }
    options_scores_scores_at(m_, self).set_field_794(0);
    // the level's video window
    const std::uint32_t memory = operator_new(0x14c);
    std::uint32_t video = 0;
    if (memory != 0) {
        video = engine_value(0x0045b0a0, {memory, options_scores_scores_at(m_, self).field_78c(), 0, 0x11a, 0xfffffffau, 0xffffffffu, 0xffffffffu});
    }
    options_scores_scores_at(m_, self).set_field_798(static_cast<Word>(video));
    w_.window_add(self, video);
    return self;
}

std::uint32_t OptionsScreen::scores_delete(std::uint32_t self, std::uint32_t flag) {
    scores_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00485cb0: the spooler (reset), the arrows, the five fonts and the window base.
void OptionsScreen::scores_destroy(std::uint32_t self) {
    options_scores_scores_at(m_, self).set_field_0(static_cast<Word>(kVtHighScores));
    engine(0x004b52b0, {global_word(m_, 0x0056db4c)});
    for (std::uint32_t slot : {kHArrowLeft, kHArrowRight}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t slot : {kHFontPrompt, kHFontTitle, kHFontScore, kHFontName, kHFontInitials}) {
        font_destroy(self + slot);
    }
    w_.window_destroy(self);
}

// 0x00485d80: when the level asked for is not the shown one the rows slide in from alternating sides (the odd rows from the left) and the title changes; then every row
// shows its entry of the level's table (the three letters, the skater's name, the score).
void OptionsScreen::scores_update(std::uint32_t self) {
    if (static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_788()) != static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c())) {
        for (std::uint32_t i = 0; i < 8; ++i) {
            const std::uint32_t initials = word_at(m_, self + kHInitials + 4 * i);
            const std::uint32_t name = word_at(m_, self + kHNames + 4 * i);
            const std::uint32_t score = word_at(m_, self + kHScores + 4 * i);
            if ((i & 1) != 0) {
                set_word_at(m_, initials + 8, 0xfffffee0u);
                vcall_item("element_snap_x", initials, 5);
                set_word_at(m_, word_at(m_, self + kHInitials + 4 * i) + 8, 0x6a);
                set_word_at(m_, name + 8, 0xffffff40u);
                vcall_item("element_snap_x", name, 5);
                set_word_at(m_, word_at(m_, self + kHNames + 4 * i) + 8, 0xc0);
                set_word_at(m_, score + 8, 0xffffffc0u);
            } else {
                set_word_at(m_, initials + 8, 0x220);
                vcall_item("element_snap_x", initials, 5);
                set_word_at(m_, word_at(m_, self + kHInitials + 4 * i) + 8, 0x6a);
                set_word_at(m_, name + 8, 0x240);
                vcall_item("element_snap_x", name, 5);
                set_word_at(m_, word_at(m_, self + kHNames + 4 * i) + 8, 0xc0);
                set_word_at(m_, score + 8, 0x340);
            }
            vcall_item("element_snap_x", score, 5);
            set_word_at(m_, word_at(m_, self + kHScores + 4 * i) + 8, 0x1c0);
        }
        const std::uint32_t row = kLevelTable + kLevelStride * options_scores_scores_at(m_, self).field_78c();
        const std::uint32_t title = scratch_line(retail_format(m_.cstring(kFormatString), {m_.cstring(row, 0x40)}, {}));
        f_.ellipsize(self + kHFontTitle, title, 0x100);
        w_.text_set_string(options_scores_scores_at(m_, self).field_790(), title);
        engine(0x0045b890, {options_scores_scores_at(m_, self).field_798(), options_scores_scores_at(m_, self).field_78c()});
        options_scores_scores_at(m_, self).set_field_788(static_cast<Word>(options_scores_scores_at(m_, self).field_78c()));
    }
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::uint32_t entry = kScoreTable + 8 * (i + 8 * options_scores_scores_at(m_, self).field_788());
        engine(0x004193c0, {word_at(m_, self + kHInitials + 4 * i), byte_at(m_, entry), byte_at(m_, entry + 1), byte_at(m_, entry + 2)});
        const std::int32_t skater = sbyte_at(m_, entry + 3);
        const std::uint32_t name_at = scratch_text(0, retail_format(m_.cstring(kFormatString), {m_.cstring(word_at(m_, kSkaterTable + kSkaterStride * u(skater)), 0x40)}, {}));
        w_.text_set_string(word_at(m_, self + kHNames + 4 * i), name_at);
        const std::uint32_t score_at = scratch_text(0x80, retail_format(m_.cstring(kFormatNumber), {}, {int_at(m_, entry + 4)}));
        w_.text_set_string(word_at(m_, self + kHScores + 4 * i), score_at);
    }
    w_.window_update(self);
}

// 0x00485f90: left and right choose the level (skipping the locked ones, the park when it is not open and the later ones); the arrows blink; then the window.
void OptionsScreen::scores_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    std::int32_t direction = 0;
    if (global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0 || global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0) {
        sfx(0x11f);
        if (global_byte(m_, kPadLeft) != 0 || global_byte(m_, kPadLeft + kPadStride) != 0) {
            set_global_byte(m_, kPadLeft, 0);
            set_global_byte(m_, kPadLeft + kPadStride, 0);
            direction = -1;
        } else if (global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0) {
            set_global_byte(m_, kPadRight, 0);
            set_global_byte(m_, kPadRight + kPadStride, 0);
            direction = 1;
        }
    }
    for (;;) {
        const std::int32_t level = static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c()) + direction;
        options_scores_scores_at(m_, self).set_field_78c(static_cast<Word>(u(level)));
        if (level >= 0xe) {
            options_scores_scores_at(m_, self).set_field_78c(0);
        } else if (level < 0) {
            options_scores_scores_at(m_, self).set_field_78c(0xd);
        }
        if (gaps_locked(static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c())) || !level_shown(m_, static_cast<std::int32_t>(options_scores_scores_at(m_, self).field_78c()))) {
            continue;
        }
        break;
    }
    if (global_word(m_, kSimClock) % 0x3c > 0x1e) {
        vcall_item("sprite_draw", options_scores_scores_at(m_, self).field_7a4(), 3, {0x32, 0x78, 0});
        vcall_item("sprite_draw", options_scores_scores_at(m_, self).field_7a8(), 3, {0x1ce, 0x78, 0});
    }
    w_.window_draw(self, x, y);
}

}  // namespace opentony::runtime::frontend
