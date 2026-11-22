#include "auto_views.hpp"
#include "player_select.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// Assertion messages (the addresses of the format strings)
constexpr std::uint32_t kMsgNoScoreElement = 0x00537004;
constexpr std::uint32_t kMsgNoMedalIcon = 0x0053707c;
constexpr std::uint32_t kMsgNoSpriteElement = 0x005370d8;
constexpr std::uint32_t kMsgNoTextElement = 0x005370c8;

// ScoreCounter fields
constexpr std::uint32_t kCounterValue = 0xac;
constexpr std::uint32_t kCounterShown = 0xb0;
constexpr std::uint32_t kCounterText = 0xb4;
constexpr std::uint32_t kCounterFont = 0xb8;

// MedalWindow fields: the three medal sprites; the level table's contest flags (one byte per 0x1ac-byte row) say which levels have a medal
constexpr std::uint32_t kMedalGold = 0xac;
constexpr std::uint32_t kMedalSilver = 0xb0;
constexpr std::uint32_t kMedalBronze = 0xb4;
constexpr std::uint32_t kLevelContest = 0x0053c240;   // career::level(0) + 0x48
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kRecordMasks = 0x0c;           // the career record's u16 level masks

constexpr std::uint32_t kPromptText = 0xac;

constexpr std::uint32_t kSelectionGlobal = 0x0056a884;   // the player-select state kept between visits

}  // namespace

// ---- the score counter ---------------------------------------------------------------------------------------------------------------------

// 0x004a1440: a window with one text element ("$     0") that shows `value`, counting up from zero.
std::uint32_t PlayerSelect::counter_construct(std::uint32_t self, std::uint32_t value, std::uint32_t font, std::int32_t x, std::int32_t y) {
    w_.window_construct(self, x, y, -1, -1);
    const std::uint32_t text_at = scratch_line(retail_format("$%6d", {}, {0}));
    player_select_ui_counter_at(m_, self).set_field_0(static_cast<Word>(kVtScoreCounter));
    player_select_ui_counter_at(m_, self).set_field_b0(0);
    player_select_ui_counter_at(m_, self).set_field_ac(static_cast<Word>(value));
    player_select_ui_counter_at(m_, self).set_field_b8(static_cast<Word>(font));
    const std::uint32_t memory = operator_new(0x30);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, text_at, font, 0, 0, -1, -1);
    }
    w_.window_add(self, text);
    player_select_ui_counter_at(m_, self).set_field_b4(static_cast<Word>(text));
    return self;
}

std::uint32_t PlayerSelect::counter_delete(std::uint32_t self, std::uint32_t flag) {
    counter_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::counter_destroy(std::uint32_t self) {
    player_select_ui_counter_at(m_, self).set_field_0(static_cast<Word>(kVtScoreCounter));
    w_.window_destroy(self);
}

// 0x004a1540: the target value, capped at 999999.
void PlayerSelect::counter_set_value(std::uint32_t self, std::int32_t value) {
    if (value > 999999) {
        value = 999999;
    }
    player_select_ui_counter_at(m_, self).set_field_ac(static_cast<Word>(u(value)));
}

// 0x004a1560: drawn with the font's draw argument forced to 1 (and put back).
void PlayerSelect::counter_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t font = player_select_ui_counter_at(m_, self).field_b8();
    const std::uint32_t saved = word_at(m_, font + 0x2c);
    set_word_at(m_, font + 0x2c, 1);
    w_.window_draw(self, x, y);
    set_word_at(m_, player_select_ui_counter_at(m_, self).field_b8() + 0x2c, saved);
}

// 0x004a15a0: the shown value moves half way to the target (and lands on it when it is one away); a negative target shows "---".
void PlayerSelect::counter_update(std::uint32_t self) {
    const std::int32_t target = static_cast<std::int32_t>(player_select_ui_counter_at(m_, self).field_ac());
    const std::int32_t shown = static_cast<std::int32_t>(player_select_ui_counter_at(m_, self).field_b0());
    const std::int32_t difference = target - shown;
    player_select_ui_counter_at(m_, self).set_field_b0(static_cast<Word>(u((difference >> 1) + shown)));
    if (difference < 2 && difference > -2) {
        player_select_ui_counter_at(m_, self).set_field_b0(static_cast<Word>(u(target)));
    }
    std::string line;
    if (target >= 0) {
        line = retail_format("$%6d", {}, {static_cast<std::int32_t>(player_select_ui_counter_at(m_, self).field_b0())});
    } else {
        line = "$   ---";
    }
    check(player_select_ui_counter_at(m_, self).field_b4() != 0, kMsgNoScoreElement);
    w_.text_set_string(player_select_ui_counter_at(m_, self).field_b4(), scratch_line(line));
    w_.element_update(self);
}

// ---- the medal window ----------------------------------------------------------------------------------------------------------------------

// 0x004a1670: two score counters (the character's two totals), the frame, and the three medal sprites the draw places.
std::uint32_t PlayerSelect::medal_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                            std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    player_select_ui_medal_at(m_, self).set_field_0(static_cast<Word>(kVtMedalWindow));
    check(font != 0, kMsgElementNoFont);
    auto counter = [&](std::int32_t at_y) {
        const std::uint32_t memory = operator_new(0xbc);
        std::uint32_t object = 0;
        if (memory != 0) {
            object = counter_construct(memory, 0, font, 0xa6, at_y);
        }
        w_.window_add(self, object);
    };
    counter(0xe);
    counter(0x1a);
    auto sprite = [&](std::uint32_t file, std::int32_t at_x, std::int32_t at_y) {
        const std::uint32_t memory = operator_new(0x2c);
        return memory != 0 ? w_.sprite_construct(memory, file, at_x, at_y, -1, -1, 0, 0) : 0u;
    };
    w_.window_add(self, sprite(0x00537070, 0, 0));
    player_select_ui_medal_at(m_, self).set_field_ac(static_cast<Word>(sprite(0x00537060, 0x3a, 0x21)));
    player_select_ui_medal_at(m_, self).set_field_b0(static_cast<Word>(sprite(0x00537050, 0x3a, 0x21)));
    player_select_ui_medal_at(m_, self).set_field_b4(static_cast<Word>(sprite(0x00537040, 0x3a, 0x21)));
    return self;
}

std::uint32_t PlayerSelect::medal_delete(std::uint32_t self, std::uint32_t flag) {
    medal_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004a18a0: the three medal sprites (they are not children) are deleted here.
void PlayerSelect::medal_destroy(std::uint32_t self) {
    player_select_ui_medal_at(m_, self).set_field_0(static_cast<Word>(kVtMedalWindow));
    check(player_select_ui_medal_at(m_, self).field_ac() != 0, kMsgNoMedalIcon);
    check(player_select_ui_medal_at(m_, self).field_b0() != 0, kMsgNoMedalIcon);
    check(player_select_ui_medal_at(m_, self).field_b4() != 0, kMsgNoMedalIcon);
    for (std::uint32_t slot : {kMedalGold, kMedalSilver, kMedalBronze}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    w_.window_destroy(self);
}

// 0x004a19f0: one medal for each contest level the character has won (gold, else silver, else bronze), 0x44 pixels apart (a level with no medal leaves
// a gap), then the children.
void PlayerSelect::medal_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::int32_t at_x = int_at(m_, self + kElemX);
    const std::int32_t at_y = int_at(m_, self + kElemY);
    std::int32_t offset = 0;
    std::uint32_t mask_at = kRecordMasks;
    for (std::uint32_t flag = kLevelContest; mask_at < 0x26; mask_at += 2, flag += kLevelStride) {
        if (byte_at(m_, flag) != 0) {
            const std::uint32_t record = career::record(sk_.current_character());
            const std::uint32_t mask = half_at(m_, record + mask_at);
            std::uint32_t medal = 0;
            if ((mask & 0x400) != 0) {
                medal = player_select_ui_medal_at(m_, self).field_ac();
            } else if ((mask & 0x800) != 0) {
                medal = player_select_ui_medal_at(m_, self).field_b0();
            } else if ((mask & 0x1000) != 0) {
                medal = player_select_ui_medal_at(m_, self).field_b4();
            }
            if (medal != 0) {
                child_draw(medal, at_x + offset + x, at_y + y);
            }
            offset += 0x44;
        }
    }
    for (std::int32_t i = 0; i < int_at(m_, self + kWinCount); ++i) {
        child_draw(word_at(m_, self + kWinChildren + 4 * static_cast<std::uint32_t>(i)), x + int_at(m_, self + kElemX), int_at(m_, self + kElemY) + y);
    }
}

// 0x004a1b00: the two counters show the character's two totals (the career record's words at +4 and +8).
void PlayerSelect::medal_update(std::uint32_t self) {
    const std::uint32_t record = career::record(sk_.current_character());
    counter_set_value(word_at(m_, self + kWinChildren), int_at(m_, record + 4));
    counter_set_value(word_at(m_, self + kWinChildren + 4), int_at(m_, record + 8));
    w_.window_update(self);
}

// ---- the prompt window ---------------------------------------------------------------------------------------------------------------------

// 0x004a1b40: a button picture and its caption: the engine maps the file and the caption to the platform's button art, the caption sits to the right of
// the picture, centred on the font's tallest glyph.
std::uint32_t PlayerSelect::prompt_construct(std::uint32_t self, std::uint32_t file, std::uint32_t text, std::uint32_t font, std::int32_t x, std::int32_t y,
                                             std::int32_t start_x, std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    player_select_ui_prompt_at(m_, self).set_field_0(static_cast<Word>(kVtPromptWindow));
    check(file != 0, kMsgNoFilenamePointer);
    check(byte_at(m_, file) != 0, kMsgNoFilename);
    check(text != 0, kMsgNoStringPointer);
    check(byte_at(m_, text) != 0, kMsgNoString);
    check(font != 0, kMsgElementNoFont);
    const std::uint32_t picture = engine_value(0x004dc050, {file});
    const std::uint32_t caption = engine_value(0x004dc0b0, {text});
    std::uint32_t memory = operator_new(0x2c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        sprite = w_.sprite_construct(memory, picture, 0, 0, -1, -1, 0, 1);
    }
    w_.window_add(self, sprite);
    memory = operator_new(0x30);
    std::uint32_t label = 0;
    if (memory != 0) {
        label = w_.text_construct(memory, caption, font, 0, 0, -1, -1);
    }
    player_select_ui_prompt_at(m_, self).set_field_ac(static_cast<Word>(label));
    w_.window_add(self, label);
    const std::uint32_t width = w_.sprite_width(sprite);
    set_word_at(m_, player_select_ui_prompt_at(m_, self).field_ac() + kElemTargetX, width + word_at(m_, sprite + kElemTargetX) + 4);
    const std::uint32_t at = player_select_ui_prompt_at(m_, self).field_ac();
    set_word_at(m_, at + kElemTargetY, u(int_at(m_, at + kElemTargetY) + (f_.text_top(font, caption) >> 1)));
    w_.element_snap_x(player_select_ui_prompt_at(m_, self).field_ac());
    engine(0x004dcbc0, {self, picture});
    return self;
}

std::uint32_t PlayerSelect::prompt_delete(std::uint32_t self, std::uint32_t flag) {
    prompt_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::prompt_destroy(std::uint32_t self) {
    player_select_ui_prompt_at(m_, self).set_field_0(static_cast<Word>(kVtPromptWindow));
    w_.window_destroy(self);
}

// 0x004a1e00: the picture's width, the caption's width and four.
std::uint32_t PlayerSelect::prompt_width(std::uint32_t self) {
    check(word_at(m_, self + kWinChildren) != 0, kMsgNoSpriteElement);
    check(word_at(m_, self + kWinChildren + 4) != 0, kMsgNoTextElement);
    const std::uint32_t picture = child_width(word_at(m_, self + kWinChildren));
    const std::uint32_t caption = child_width(word_at(m_, self + kWinChildren + 4));
    return picture + caption + 4;
}

void PlayerSelect::prompt_set_string(std::uint32_t self, std::uint32_t text) { w_.text_set_string(word_at(m_, self + kPromptText), text); }

// ---- the flagged text element --------------------------------------------------------------------------------------------------------------

// 0x004a1ed0: a text element whose slide speed is a shift: 1 when `flag` (a byte) is set, else 2.
std::uint32_t PlayerSelect::flag_construct(std::uint32_t self, std::uint32_t text, std::uint32_t font, std::uint32_t flag, std::int32_t x, std::int32_t y,
                                           std::int32_t start_x, std::int32_t start_y) {
    w_.text_construct(self, text, font, x, y, start_x, start_y);
    player_select_ui_flag_at(m_, self).set_field_0(static_cast<Word>(kVtTextFlag));
    player_select_ui_flag_at(m_, self).set_field_30(static_cast<Word>((flag & 0xff) != 0 ? 1 : 2));
    return self;
}

std::uint32_t PlayerSelect::flag_delete(std::uint32_t self, std::uint32_t flag) {
    flag_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void PlayerSelect::flag_destroy(std::uint32_t self) {
    player_select_ui_flag_at(m_, self).set_field_0(static_cast<Word>(kVtTextFlag));
    w_.text_destroy(self);
}

// 0x004a1f50: the element's slide with the shift from +0x30 (the base class uses a fixed one): wait out the delay, move each axis by the distance
// shifted right (half of it when that is zero), and note whether it has arrived. (The y step falls back to half of the x distance: retail does.)
void PlayerSelect::flag_update(std::uint32_t self) {
    if (int_at(m_, self + kElemDelay) > 0) {
        set_word_at(m_, self + kElemDelay, u(int_at(m_, self + kElemDelay) - 1));
        return;
    }
    const std::int32_t target_x = int_at(m_, self + kElemTargetX);
    std::int32_t x = int_at(m_, self + kElemX);
    const std::int32_t dx = target_x - x;
    const std::uint32_t shift = player_select_ui_flag_at(m_, self).field_30() & 31;
    if (dx <= 1 && dx >= -1) {
        set_word_at(m_, self + kElemX, u(target_x));
    } else {
        std::int32_t step = dx >> shift;
        if (step == 0) {
            step = dx >> 1;
        }
        x += step;
        set_word_at(m_, self + kElemX, u(x));
    }
    const std::int32_t target_y = int_at(m_, self + kElemTargetY);
    std::int32_t y = int_at(m_, self + kElemY);
    std::int32_t dy = target_y - y;
    if (dy < 1 && dy > -1) {
        set_word_at(m_, self + kElemY, u(target_y));
    } else {
        std::int32_t step = dy >> (player_select_ui_flag_at(m_, self).field_30() & 31);
        if (step == 0) {
            step = dx >> 1;
        }
        y += step;
        set_word_at(m_, self + kElemY, u(y));
    }
    set_byte_at(m_, self + kElemArrived, (int_at(m_, self + kElemY) == target_y && int_at(m_, self + kElemX) == target_x) ? 1 : 0);
}

// ---- the screen callbacks and the selection globals ----------------------------------------------------------------------------------------

// 0x004a1fe0: the screen table asks each screen for its two callbacks.
void PlayerSelect::init_callbacks(std::uint32_t record) {
    set_word_at(m_, record, 0x004a2000);
    set_word_at(m_, record + 0x0c, 0x004a2010);
}

void PlayerSelect::exit_callback() {}

// 0x004a2010: `*result = run(*argument)` (the argument is the screen's mode).
void PlayerSelect::screen_call(std::uint32_t argument, std::uint32_t result) { set_word_at(m_, result, u(run(word_at(m_, argument)))); }

void PlayerSelect::save_selection(std::uint32_t self) { set_global_word(m_, kSelectionGlobal, word_at(m_, self + 0xac)); }

void PlayerSelect::restore_selection(std::uint32_t self) { set_word_at(m_, self + 0xac, global_word(m_, kSelectionGlobal)); }

std::uint32_t PlayerSelect::child_width(std::uint32_t child) {
    const std::uint32_t vtable = word_at(m_, child);
    if (vtable == kVtSprite) {
        return w_.sprite_width(child);
    }
    if (vtable == kVtText) {
        return u(w_.text_width(child));
    }
    s_.note("element_width", {child});
    return static_cast<std::uint32_t>(s_.value("element_width"));
}

}  // namespace opentony::runtime::frontend
