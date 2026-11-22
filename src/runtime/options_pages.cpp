#include "auto_views.hpp"
#include "options.hpp"
#include "host_options.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// MovieList fields (0x238 bytes, vtable 0x00519424)
constexpr std::uint32_t kMList = 0xb0;
constexpr std::uint32_t kMRows = 0xb4;      // 0x60 slots
constexpr std::uint32_t kMStyle = 0x234;

// SoundLevels fields (0x244 bytes, vtable 0x00519444)
constexpr std::uint32_t kSList = 0xb0;
constexpr std::uint32_t kSRows = 0xb4;
constexpr std::uint32_t kSStyle = 0x234;
constexpr std::uint32_t kSDotsSprite = 0x238;
constexpr std::uint32_t kSDotsMusic = 0x23c;
constexpr std::uint32_t kSDotsEffects = 0x240;

constexpr std::uint32_t kMovieTable = 0x0052d4a8;       // a pointer: 0x24-byte entries (+0 non-zero, +4 the label, +8 the kind), the first word 0 ends the table
constexpr std::uint32_t kSkaterTable = 0x0053a3f8;
constexpr std::uint32_t kSkaterStride = 0x170;
constexpr std::uint32_t kLevelTable = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kMusicVolume = 0x0055fb3c;
constexpr std::uint32_t kEffectsVolume = 0x0055fb40;
constexpr std::uint32_t kVolumeA = 0x00561c84;
constexpr std::uint32_t kVolumeB = 0x00561c94;
constexpr std::uint32_t kSoundScale = 0x0052be44;       // two words the sliders set for the mixer
constexpr std::uint32_t kPlayingMovie = 0x0052cb38;
constexpr std::uint32_t kPadLeft = 0x0056b079;
constexpr std::uint32_t kPadRight = 0x0056b089;

constexpr std::uint32_t kMsgTooManyMovies = 0x00535e24;
constexpr std::uint32_t kMsgNotReferencedSkater = 0x00535df4;
constexpr std::uint32_t kMsgNotReferencedLevel = 0x00535e50;
constexpr std::uint32_t kMsgUnlockType = 0x00535e34;

}  // namespace

// ---- the movie list (class at 0x00519424) --------------------------------------------------------------------------------------------------------------

// 0x00486910: whether movie `index` of kind `kind` is unlocked: 0 always, 1 and 4 when the skater it belongs to has three gold medals (or, when it is flagged, in the
// four enabled characters), 2 when its level is open, 3 never, 5 when the first character's flag is set, 6 when any character has three medals.
bool OptionsScreen::movie_unlocked(std::uint32_t kind, std::int32_t index) {
    if (host_options().unlock_movies && (kind & 0xffff) != 3) {
        return true;   // the host's "all movies" (kind 3 is the one the game never unlocks: no such movie)
    }
    switch (kind & 0xffff) {
    case 0:
        return true;
    case 6:
        for (std::uint32_t character = 0; character < 0x14; ++character) {
            const std::uint32_t config = engine_value(0x00416050, {character});
            if (static_cast<std::int32_t>(engine_value(0x00416530, {config})) >= 3) {
                return true;
            }
        }
        return false;
    case 5: {
        const std::uint32_t config = engine_value(0x00416050, {0});
        return ((byte_at(m_, config + 1) & 0x40) >> 6) != 0;
    }
    case 1:
    case 4: {
        std::int32_t found = -1;
        for (std::int32_t k = 0; k < 20; ++k) {
            if (int_at(m_, 0x0053a520 + kSkaterStride * u(k)) == index) {
                found = k;
            }
        }
        check(found != -1, kMsgNotReferencedSkater);
        if ((byte_at(m_, 0x0053a514 + kSkaterStride * u(found)) & 2) != 0) {
            for (std::uint32_t slot = 0; slot < 4; ++slot) {
                const std::uint32_t character = engine_value(0x004b1980, {slot});
                const std::uint32_t config = engine_value(0x00416050, {character});
                if (static_cast<std::int32_t>(engine_value(0x004164b0, {config})) >= 3) {
                    return true;
                }
            }
            return false;
        }
        const std::uint32_t config = engine_value(0x00416050, {u(found)});
        return static_cast<std::int32_t>(engine_value(0x004164b0, {config})) >= 3;
    }
    case 2: {
        std::int32_t found = -1;
        for (std::int32_t k = 0; k < 13; ++k) {
            if (int_at(m_, 0x0053c290 + kLevelStride * u(k)) == index) {
                found = k;
            }
        }
        check(found != -1, kMsgNotReferencedLevel);
        return engine_value(0x004159d0, {u(found), 3}) != 0;
    }
    case 3:
        return false;
    default:
        check(false, kMsgUnlockType);
        return false;
    }
}

// 0x004860d0: a scroll list with a style copied from the options window's font and one row for each movie the table lists (the ids count down from -1), those the player has
// not unlocked greyed; a Back row ends it.
std::uint32_t OptionsScreen::movie_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_pages_movie_at(m_, self).set_field_0(static_cast<Word>(kVtMovieList));
    std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0, 0x28, 0x200, 0xa0, 1, 0x64, 2, 1);
    }
    options_pages_movie_at(m_, self).set_field_b0(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_pages_movie_at(m_, self).set_field_234(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, font);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kMRows + 4 * i, 0);
    }
    std::uint32_t entry = global_word(m_, kMovieTable);
    std::uint32_t count = 0;
    std::int32_t index = -1;
    auto add_row = [&](std::uint32_t kind, std::uint32_t text_entry) {
        memory = operator_new(0x54);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.text_construct(memory, u(index), word_at(m_, text_entry + 4), options_pages_movie_at(m_, self).field_234());
        }
        set_word_at(m_, self + kMRows + 4 * count, row);
        vcall_list("list_v6", options_pages_movie_at(m_, self).field_b0(), 6, {row});
        if (!movie_unlocked(kind, index)) {
            set_byte_at(m_, row + 0xc, 0);
        }
        ++count;
    };
    if (word_at(m_, entry) != 0) {
        do {
            const std::uint32_t kind = half_at(m_, entry + 8);
            if (kind == 6) {
                check(static_cast<std::int32_t>(count) < 0x60, kMsgTooManyMovies);
                add_row(kind, entry);
            } else if (kind == 5) {
                if ((global_byte(m_, 0x0053a514) & 8) == 0) {
                    check(static_cast<std::int32_t>(count) < 0x60, kMsgTooManyMovies);
                    add_row(kind, entry);
                }
            } else if (kind == 4) {
                std::int32_t found = -1;
                for (std::int32_t k = 0; k < 20; ++k) {
                    if (int_at(m_, 0x0053a520 + kSkaterStride * u(k)) == index) {
                        found = k;
                    }
                }
                check(found != -1, kMsgNotReferencedSkater);
                if (word_at(m_, 0x0053a510 + kSkaterStride * u(found)) != 0) {
                    check(static_cast<std::int32_t>(count) < 0x60, kMsgTooManyMovies);
                    add_row(kind, entry);
                }
            } else if (word_at(m_, entry + 4) != 0) {
                check(static_cast<std::int32_t>(count) < 0x60, kMsgTooManyMovies);
                add_row(kind, entry);
            }
            --index;
            entry += 0x24;
        } while (word_at(m_, entry) != 0);
    }
    check(static_cast<std::int32_t>(count) < 0x60, kMsgTooManyMovies);
    memory = operator_new(0x54);
    std::uint32_t back = 0;
    if (memory != 0) {
        back = menu_.text_construct(memory, 0, global_word(m_, 0x0052e79c), options_pages_movie_at(m_, self).field_234());
    }
    set_word_at(m_, self + kMRows + 4 * count, back);
    vcall_list("list_v6", options_pages_movie_at(m_, self).field_b0(), 6, {back});
    vcall_list("list_v1", options_pages_movie_at(m_, self).field_b0(), 1, {arrow_a, arrow_b, 5});
    vcall_list("list_v2", options_pages_movie_at(m_, self).field_b0(), 2);
    return self;
}

std::uint32_t OptionsScreen::movie_delete(std::uint32_t self, std::uint32_t flag) {
    movie_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00486700: the list, the rows, the style, then the window base.
void OptionsScreen::movie_destroy(std::uint32_t self) {
    options_pages_movie_at(m_, self).set_field_0(static_cast<Word>(kVtMovieList));
    if (options_pages_movie_at(m_, self).field_b0() != 0) {
        delete_object(options_pages_movie_at(m_, self).field_b0());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kMRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kMRows + 4 * i));
        }
    }
    if (options_pages_movie_at(m_, self).field_234() != 0) {
        delete_object(options_pages_movie_at(m_, self).field_234());
    }
    w_.window_destroy(self);
}

void OptionsScreen::movie_update(std::uint32_t self) { w_.window_update(self); }

// 0x004867a0: a move on the pad makes a sound; the list follows the pad; then the window.
void OptionsScreen::movie_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    nav_sound();
    const std::uint32_t list = options_pages_movie_at(m_, self).field_b0();
    if (list != 0) {
        vcall_list("list_v3", list, 3);
        s_.note(kSvcPadEdges, {0, 0, 6});
        const std::uint32_t edge = static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
        vcall_list("list_v5", list, 5, {edge});
        vcall_list("list_v4", list, 4);
    }
    w_.window_draw(self, x, y);
}

// 0x00486830: confirm on a movie plays it (the one playing is stopped first), confirm on Back answers 1, back on the pad answers 2.
std::uint32_t OptionsScreen::movie_step(std::uint32_t self) {
    if (global_byte(m_, kPadConfirmEdge) != 0 || global_byte(m_, kPadConfirmEdge + kPadStride) != 0) {
        sfx(0x91);
        set_global_byte(m_, kPadConfirmEdge, 0);
        set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
        if (menu_.list_highlighted_id(options_pages_movie_at(m_, self).field_b0()) == 0) {
            return 1;
        }
        set_global_word(m_, kVolumeA, 0);
        set_global_word(m_, kVolumeB, 0);
        const std::uint32_t playing = global_word(m_, kPlayingMovie);
        if (playing != 0xffffffffu) {
            engine(0x004ad9f0, {playing});
            set_global_word(m_, kPlayingMovie, 0xffffffffu);
        }
        engine(0x00454d10, {menu_.list_highlighted_id(options_pages_movie_at(m_, self).field_b0())});
        set_global_word(m_, kVolumeA, 0);
        set_global_word(m_, kVolumeB, 0x3fff);
        engine(0x00454710);
    }
    if (global_byte(m_, kPadBackEdge) == 0 && global_byte(m_, kPadBackEdge + kPadStride) == 0) {
        return 0;
    }
    sfx(0x93);
    set_global_byte(m_, kPadBackEdge, 0);
    set_global_byte(m_, kPadBackEdge + kPadStride, 0);
    return 2;
}

// ---- the sound levels page (class at 0x00519444) -----------------------------------------------------------------------------------------------------

// 0x00486b10: a list of three rows (music, effects, back), the row of dots for each of the two sliders, and the engine told the page is open.
std::uint32_t OptionsScreen::sound_construct(std::uint32_t self, std::uint32_t font, std::uint32_t arrow_a, std::uint32_t arrow_b) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_pages_sound_at(m_, self).set_field_0(static_cast<Word>(kVtSoundLevels));
    std::uint32_t memory = operator_new(0x1bc);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.list_construct(memory, 0, 0x32, 0x200, 0xbe, 1, 1, 0x1e, 0);
    }
    options_pages_sound_at(m_, self).set_field_b0(static_cast<Word>(list));
    memory = operator_new(0x17c);
    const std::uint32_t style = memory != 0 ? menu_.properties_construct(memory) : 0u;
    options_pages_sound_at(m_, self).set_field_234(static_cast<Word>(style));
    menu_.copy_font_fields(style + 4, font);
    set_word_at(m_, style + 0x18, 0x80);
    set_word_at(m_, style + 0x1c, 0x80);
    set_word_at(m_, style + 0x20, 0x80);
    set_word_at(m_, style + 0x34, 0xc00);
    set_word_at(m_, style + 8, 1);
    set_word_at(m_, style + 0x150, 0xc00);
    set_word_at(m_, style + 0x154, 0xc00);
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        set_word_at(m_, self + kSRows + 4 * i, 0);
    }
    auto row = [&](std::uint32_t slot, std::uint32_t id, std::uint32_t text_variable) {
        memory = operator_new(0x54);
        std::uint32_t made = 0;
        if (memory != 0) {
            made = menu_.text_construct(memory, id, word_at(m_, text_variable), options_pages_sound_at(m_, self).field_234());
        }
        set_word_at(m_, self + slot, made);
        vcall_list("list_v6", options_pages_sound_at(m_, self).field_b0(), 6, {made});
    };
    row(kSRows, 0, 0x0052e5cc);
    row(kSRows + 4, 1, 0x0052e5d0);
    row(kSRows + 8, 0xffffffffu, 0x0052e79c);
    vcall_list("list_v1", options_pages_sound_at(m_, self).field_b0(), 1, {arrow_a, arrow_b, 5});
    vcall_list("list_v2", options_pages_sound_at(m_, self).field_b0(), 2);
    memory = operator_new(0x5c);
    std::uint32_t sprite = 0;
    if (memory != 0) {
        s_.note(kSpriteConstruct, {memory, 0x00535e7c, 1, 0, 0}, m_.cstring(0x00535e7c));
        sprite = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
    }
    options_pages_sound_at(m_, self).set_field_238(static_cast<Word>(sprite));
    for (std::uint32_t slot : {kSDotsMusic, kSDotsEffects}) {
        memory = operator_new(0x18);
        set_word_at(m_, self + slot, memory != 0 ? menu_.dots_construct(memory, options_pages_sound_at(m_, self).field_238(), 5, 0xc) : 0u);
    }
    engine(0x004abcb0, {1});
    return self;
}

std::uint32_t OptionsScreen::sound_delete(std::uint32_t self, std::uint32_t flag) {
    sound_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00486ea0: the list, the rows, the style, the sprite and the two rows of dots; the mixer is told the page is closed and the volumes return.
void OptionsScreen::sound_destroy(std::uint32_t self) {
    options_pages_sound_at(m_, self).set_field_0(static_cast<Word>(kVtSoundLevels));
    if (options_pages_sound_at(m_, self).field_b0() != 0) {
        delete_object(options_pages_sound_at(m_, self).field_b0());
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        if (word_at(m_, self + kSRows + 4 * i) != 0) {
            delete_object(word_at(m_, self + kSRows + 4 * i));
        }
    }
    for (std::uint32_t slot : {kSStyle, kSDotsSprite, kSDotsMusic, kSDotsEffects}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    engine(0x004abc80);
    set_global_word(m_, kVolumeB, 0x3fff);
    set_global_word(m_, kVolumeA, 0x3fff);
    if (global_word(m_, kPlayingMovie) == 0xffffffffu) {
        engine(0x00454710);
    }
    w_.window_destroy(self);
}

// 0x00486f80: left and right move the highlighted slider between 0 and 12 (the music's makes the click, 0x91); the dots and the mixer's two scale words follow the values
// (value * 0x3fff / 12 and value * 255 / 12), and the effects row mutes the music while its sample plays.
void OptionsScreen::sound_update(std::uint32_t self) {
    const std::uint32_t selected = menu_.list_highlighted_id(options_pages_sound_at(m_, self).field_b0());
    std::uint32_t value = 0;
    bool adjust = true;
    if (selected == 1) {
        set_global_word(m_, kVolumeB, 0);
        set_global_word(m_, kVolumeA, 0);
        engine(0x004abe20, {0});
        value = kEffectsVolume;
    } else {
        engine(0x004abe20, {1});
        if (selected != 0) {
            adjust = false;
        } else {
            value = kMusicVolume;
        }
    }
    if (adjust) {
        const std::uint8_t left_a = global_byte(m_, kPadLeft);
        const std::uint8_t left_b = global_byte(m_, kPadLeft + kPadStride);
        const bool left = left_a != 0 || left_b != 0;
        const bool right = global_byte(m_, kPadRight) != 0 || global_byte(m_, kPadRight + kPadStride) != 0;
        if (left) {
            set_word_at(m_, value, word_at(m_, value) - 1);
        } else if (right) {
            set_word_at(m_, value, word_at(m_, value) + 1);
        }
        if (int_at(m_, value) < 0) {
            set_word_at(m_, value, 0);
        }
        if (int_at(m_, value) >= 0xc) {
            set_word_at(m_, value, 0xc);
        }
        if (left || right) {
            set_global_byte(m_, kPadLeft, 0);
            set_global_byte(m_, kPadLeft + kPadStride, 0);
            set_global_byte(m_, kPadRight, 0);
            set_global_byte(m_, kPadRight + kPadStride, 0);
            if (selected == 0) {
                set_global_word(m_, kSoundScale, global_word(m_, kMusicVolume) * 0x111 * 5);
                sfx(0x91);
            }
        }
    }
    menu_.dots_set_range(options_pages_sound_at(m_, self).field_23c(), global_word(m_, kMusicVolume), global_int(m_, kMusicVolume));
    menu_.dots_set_range(options_pages_sound_at(m_, self).field_240(), global_word(m_, kEffectsVolume), global_int(m_, kEffectsVolume));
    auto twelfth = [](std::int32_t v) {
        const std::int64_t q = (static_cast<std::int64_t>(v) * 0x2aaaaaabLL) >> 32;
        std::int32_t d = static_cast<std::int32_t>(q) >> 1;
        d += static_cast<std::int32_t>(static_cast<std::uint32_t>(d) >> 31);
        return d;
    };
    set_global_word(m_, kSoundScale, u(twelfth(global_int(m_, kMusicVolume) * 0x3fff)));
    set_global_word(m_, kSoundScale + 4, u(twelfth(global_int(m_, kEffectsVolume) * 0xff)));
    if (selected != 1) {
        set_global_word(m_, kVolumeA, 0x3fff);
        set_global_word(m_, kVolumeB, 0x3fff);
    }
    if (selected != 1 || global_int(m_, kVolumeB) > 0) {
        if (global_word(m_, kPlayingMovie) == 0xffffffffu) {
            engine(0x00454710);
        }
    }
    engine(0x004ab9e0);
    engine(0x004abe50);
    w_.window_update(self);
}

// 0x00487130: a move on the pad makes a sound; the list follows the pad; the two rows of dots and the window draw.
void OptionsScreen::sound_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    nav_sound();
    const std::uint32_t list = options_pages_sound_at(m_, self).field_b0();
    if (list != 0) {
        vcall_list("list_v3", list, 3);
        const std::uint32_t edge = engine_value(0x0046f650);
        vcall_list("list_v5", list, 5, {edge});
        vcall_list("list_v4", list, 4);
    }
    menu_.dots_draw(options_pages_sound_at(m_, self).field_23c(), 0xbe, 0x62);
    menu_.dots_draw(options_pages_sound_at(m_, self).field_240(), 0xbe, 0x8c);
    w_.window_draw(self, x, y);
}

// 0x004871e0: confirm on Back answers 1, back on the pad answers 2.
std::uint32_t OptionsScreen::sound_step(std::uint32_t self) {
    if (global_byte(m_, kPadConfirmEdge) != 0 || global_byte(m_, kPadConfirmEdge + kPadStride) != 0) {
        sfx(0x91);
        set_global_byte(m_, kPadConfirmEdge, 0);
        set_global_byte(m_, kPadConfirmEdge + kPadStride, 0);
        if (menu_.list_highlighted_id(options_pages_sound_at(m_, self).field_b0()) == 0xffffffffu) {
            sfx(0x91);
            return 1;
        }
    }
    if (global_byte(m_, kPadBackEdge) == 0 && global_byte(m_, kPadBackEdge + kPadStride) == 0) {
        return 0;
    }
    sfx(0x93);
    set_global_byte(m_, kPadBackEdge, 0);
    set_global_byte(m_, kPadBackEdge + kPadStride, 0);
    return 2;
}

// 0x00487280: whether a row is highlighted.
bool OptionsScreen::sound_has_selection(std::uint32_t self) { return menu_.list_highlighted_id(word_at(m_, self + kSList)) != 0xffffffffu; }

// 0x004872a0: whether nothing is.
bool OptionsScreen::sound_no_selection(std::uint32_t self) { return !sound_has_selection(self); }

// ---- the memory card and replay pages (classes at 0x00519464 and 0x00519484) ----------------------------------------------------------------------------

// 0x004872b0: the card page: the engine's save or load buffer is set up (load when `load`); +0xac remembers which.
std::uint32_t OptionsScreen::card_construct(std::uint32_t self, std::uint32_t load) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_pages_card_at(m_, self).set_field_0(static_cast<Word>(kVtCardPage));
    if (load != 0) {
        engine(0x0047a6c0, {1, 1, 0});
    } else {
        engine(0x0047a0b0, {0x12, 0x12, 1});
    }
    options_pages_card_at(m_, self).set_field_ac(static_cast<std::uint8_t>(load != 0 ? 1 : 0));
    return self;
}

std::uint32_t OptionsScreen::card_delete(std::uint32_t self, std::uint32_t flag) {
    card_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void OptionsScreen::card_destroy(std::uint32_t self) {
    options_pages_card_at(m_, self).set_field_0(static_cast<Word>(kVtCardPage));
    w_.window_destroy(self);
}

void OptionsScreen::card_update(std::uint32_t self) { w_.window_update(self); }

// 0x00487370 (and 0x00487450 of the replay page): the engine draws the card screen, then the window.
void OptionsScreen::card_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    engine(0x0047ace0);
    w_.window_draw(self, x, y);
}

// 0x00487390 (and 0x00487470): the engine's card screen answers; an answer of 1 or 2 closes it.
std::uint32_t OptionsScreen::card_step() {
    const std::uint32_t answer = engine_value(0x0047ac60);
    if (answer == 1 || answer == 2) {
        engine(0x0047ae10, {answer});
    }
    return answer;
}

// 0x004873b0: the replay page: the engine's replay load buffer is set up.
std::uint32_t OptionsScreen::replay_construct(std::uint32_t self) {
    w_.window_construct(self, 0, 0, -1, -1);
    options_pages_replay_at(m_, self).set_field_0(static_cast<Word>(kVtReplayPage));
    engine(0x0047a8a0, {1});
    return self;
}

std::uint32_t OptionsScreen::replay_delete(std::uint32_t self, std::uint32_t flag) {
    replay_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

void OptionsScreen::replay_destroy(std::uint32_t self) {
    options_pages_replay_at(m_, self).set_field_0(static_cast<Word>(kVtReplayPage));
    w_.window_destroy(self);
}

// ---- the video menu (vidmenu.cpp) ---------------------------------------------------------------------------------------------------------------------

// 0x004cea70: the video menu window's destructor: its background picture (the sprite object, then its block), the two fonts it loaded, two of its objects, four items
// ("no item to delete" for an empty one), its two Font objects, the engine object at +0xb0 and the window base.
void OptionsScreen::video_menu_destroy(std::uint32_t self) {
    set_word_at(m_, self, 0x00519914);
    const std::uint32_t sprite = word_at(m_, self + 0x238);
    if (sprite != 0) {
        engine(seam::hal::D3DSprite_Destroy, {sprite});
        s_.note("crt_delete", {sprite});
    }
    font_unload(0x00525458);
    font_unload(0x0052544c);
    for (std::uint32_t slot : {0x388u, 0x3b0u}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        check(word_at(m_, self + 0x38c + 4 * i) != 0, 0x00535a58);
        if (word_at(m_, self + 0x38c + 4 * i) != 0) {
            delete_object(word_at(m_, self + 0x38c + 4 * i));
        }
    }
    font_destroy(self + 0x23c);
    font_destroy(self + 0xe8);
    engine(0x00454970, {self + 0xb0});
    w_.window_destroy(self);
}

}  // namespace opentony::runtime::frontend
