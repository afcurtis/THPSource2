#include "auto_views.hpp"
#include "audio_game.hpp"

#include "frame_pacing.hpp"

#include <algorithm>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

std::int32_t mulhi(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 32); }
std::int32_t add32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t mul32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
std::int32_t round_zero(std::int32_t quotient) { return quotient + static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31); }

// value / 255 as the compiler writes it: the high half of value * 0x80808081 (a negative multiplier as a signed word) plus the value, shifted by 7, rounded towards zero
std::int32_t div255(std::int32_t value) { return round_zero(add32(mulhi(value, static_cast<std::int32_t>(0x80808081u)), value) >> 7); }

constexpr std::uint32_t kMsgBadXaVolume = 0x00537790;
constexpr std::uint32_t kMsgBadXaLength = 0x00537804;
constexpr std::uint32_t kMsgBadGroup = 0x005377ec;
constexpr std::uint32_t kMsgBadSector = 0x005377d4;
constexpr std::uint32_t kMsgNoPosition = 0x00537814;
constexpr std::uint32_t kLogXaPlay = 0x005377b8;

}  // namespace

// 0x004ab9c0: the music comes in at full volume and fades
void AudioGame::redbook_reset_volume() {
    set_global_word(m_, audio::kXaVolume, 0xff);
    set_global_word(m_, audio::kXaFading, 1);
}

// 0x004ab9e0: the music volume (the fade times the setting) goes to the movie interface; while fading the fade drops by 3 a frame
void AudioGame::redbook_update_volume() {
    check(global_int(m_, audio::kMusicVolume) <= 0xff, kMsgBadXaVolume);
    std::int32_t fade = global_int(m_, audio::kXaVolume);
    if (global_word(m_, audio::kXaFading) != 0) {
        fade = add32(fade, -framepacing::per_call_amount(3, [&] { return global_word(m_, 0x0056e31c); }));   // F1.5G: 3 a 30 fps frame, whatever the scene rate
        set_global_word(m_, audio::kXaVolume, static_cast<std::uint32_t>(fade < 0 ? 0 : fade));
        fade = fade < 0 ? 0 : fade;
    }
    const std::int32_t volume = std::min(div255(mul32(fade, global_int(m_, audio::kMusicVolume))), 0xff);
    set_xa_volume(volume, volume);
}

// 0x004aba90
void AudioGame::redbook_reset() {
    const std::uint32_t base = global_word(m_, audio::kXaBase);
    set_global_byte(m_, audio::kXaEnabled, 1);
    set_global_word(m_, audio::kXaDelay, 0x1e);
    set_global_word(m_, audio::kXaPaused, 0);
    set_global_word(m_, audio::kXaWaiting, 0);
    set_global_word(m_, audio::kXaPlaying, 0);
    set_global_word(m_, audio::kXaStart, base);
    set_global_word(m_, audio::kXaEnd, base);
    set_global_byte(m_, audio::kXaStopped0, 0);
    set_global_byte(m_, audio::kXaStopped1, 0);
    set_global_word(m_, audio::kXaGroup, 0);
    set_global_word(m_, audio::kXaTrack, 0);
}

// 0x004abb00: play a track (group 0..3, track 0..7 of it): the movie interface starts it, the position is noted and the music fades in unless a cut scene or a muted state is on
bool AudioGame::redbook_play(std::int32_t group, std::int32_t track) {
    xa_play(group, track);
    const std::uint32_t index = static_cast<std::uint32_t>(add32(track, mul32(group, 8)));
    check(group < 4, kMsgBadGroup);
    check(int_at(m_, audio::kTrackLengths + 4 * index) != -1, kMsgBadSector);
    set_global_word(m_, audio::kXaGroup, static_cast<std::uint32_t>(group));
    set_global_word(m_, audio::kXaTrack, static_cast<std::uint32_t>(track));
    if (global_byte(m_, audio::kXaEnabled) == 0) {
        return false;
    }
    set_global_word(m_, audio::kXaPaused, 0);
    log_line(kLogXaPlay);
    set_global_word(m_, audio::kXaFading, 0);
    set_global_word(m_, audio::kXaVolume, 0xff);
    const std::int32_t start = add32(global_int(m_, audio::kXaBase), mul32(int_at(m_, audio::kGroupOffsets + 4 * static_cast<std::uint32_t>(group)), 8));
    set_global_word(m_, audio::kXaStart, static_cast<std::uint32_t>(start));
    redbook_save_position(audio::kSavedPosition);
    set_global_word(m_, audio::kXaEnd, static_cast<std::uint32_t>(add32(add32(global_int(m_, audio::kXaStart), mul32(int_at(m_, audio::kTrackLengths + 4 * index), 8)), -0x83)));
    if (global_word(m_, audio::kMuteA) == 0 && byte_at(m_, audio::kMuteTable + 17 * global_word(m_, audio::kMuteIndex)) == 0 && global_byte(m_, audio::kMuteB) == 0 && global_word(m_, audio::kMuteC) == 0) {
        redbook_update_volume();
        set_global_word(m_, audio::kXaWaiting, 0);
        set_global_word(m_, audio::kXaDelay, 0x1e);
        set_global_word(m_, audio::kXaPlaying, 1);
        return true;
    }
    set_global_word(m_, audio::kXaWaiting, 1);
    return false;
}

// 0x004abc80
void AudioGame::redbook_stop() {
    xa_stop();
    set_global_byte(m_, audio::kXaStopped0, 0);
    set_global_word(m_, audio::kXaDelay, 0x1e);
    set_global_byte(m_, audio::kXaStopped1, 0);
    redbook_set_paused(1);
}

// 0x004abcb0: the next track. With the CD music setting off the level picks its track (levels 0..9; group = level / 8) and the volume is set from the settings; otherwise `step` moves along the
// tracks (skipping the empty ones and the first two groups).
void AudioGame::redbook_next(std::int32_t step) {
    if (global_word(m_, audio::kMusicVolume) == 0) {
        const std::int32_t level = std::min(global_int(m_, audio::kCurrentLevel), 10);
        const std::int32_t group = level / 8;
        const std::int32_t track = level % 8;
        check(int_at(m_, audio::kTrackLengths + 4 * static_cast<std::uint32_t>(track + group * 8)) != -1, kMsgBadXaLength);
        if (global_int(m_, audio::kCurrentLevel) < 10 && global_word(m_, audio::kPauseFlag) == 0) {
            redbook_play(group, track);
        }
        if (global_word(m_, audio::kMusicVolume) != 0 || global_word(m_, audio::kPauseFlag) != 0) {
            return;
        }
        // the effects volume (0x4000 = full) scaled to 0..255 (divided by 0x3fff), then by the fade
        const std::int32_t sfx = mul32(global_int(m_, audio::kSfxVolume), 255);
        const std::int32_t scaled = round_zero(add32(mulhi(sfx, static_cast<std::int32_t>(0x80020009u)), sfx) >> 13);
        const std::int32_t volume = div255(mul32(scaled, global_int(m_, audio::kXaVolume)));
        set_xa_volume(volume, volume);
        return;
    }
    std::int32_t group = global_int(m_, audio::kXaGroup);
    std::int32_t track = global_int(m_, audio::kXaTrack);
    for (;;) {
        track = add32(track, step);
        if (track >= 8) {
            ++group;
            track = 0;
        } else if (track < 0) {
            --group;
            track = 7;
        }
        if (group >= 4) {
            group = 0;
        } else if (group < 0) {
            group = 3;
        }
        const std::int32_t index = track + group * 8;
        if (int_at(m_, audio::kTrackLengths + 4 * static_cast<std::uint32_t>(index)) == -1 || index < 0x10) {
            continue;
        }
        redbook_play(group, track);
        return;
    }
}

// 0x004abe20: pause or resume the music (only when that changes the state)
void AudioGame::redbook_set_paused(std::uint32_t paused) {
    if ((paused & 0xff) != (global_word(m_, audio::kXaPaused) != 0 ? 1u : 0u)) {
        movie_pause(paused);
        set_global_word(m_, audio::kXaPaused, paused & 0xff);
    }
}

// 0x004abe50: the music's per-frame update: the fade, the delay after a track starts, and the next track when this one has finished
void AudioGame::redbook_update() {
    if (global_word(m_, audio::kXaFading) != 0) {
        redbook_update_volume();
    }
    if (global_byte(m_, audio::kXaEnabled) == 0 || global_word(m_, audio::kXaPaused) != 0) {
        return;
    }
    const std::uint32_t delay = global_word(m_, audio::kXaDelay);
    if (delay != 0) {
        set_global_word(m_, audio::kXaDelay, delay - 1);
        return;
    }
    if (global_word(m_, audio::kPauseFlag) == 0) {
        // (the CD music moves on when the track may continue; the level-picked music when it may not)
        const bool may_continue = redbook_can_continue();
        if (global_word(m_, audio::kMusicVolume) != 0 ? may_continue : !may_continue) {
            redbook_next(1);
            return;
        }
    }
    if (xa_done()) {
        redbook_next(1);
    }
}

// 0x004abed0: may the music move on by itself (not while paused; the level-picked music on the first ten levels stays; otherwise only the first two groups)
bool AudioGame::redbook_can_continue() {
    if (global_word(m_, audio::kPauseFlag) != 0) {
        return false;
    }
    if (global_word(m_, audio::kMusicVolume) == 0 && global_int(m_, audio::kCurrentLevel) >= 10) {
        return true;
    }
    return global_int(m_, audio::kXaTrack) + global_int(m_, audio::kXaGroup) * 8 < 0x10;
}

// 0x004abf10
void AudioGame::redbook_save_position(std::uint32_t out) {
    check(out != 0, kMsgNoPosition);
    set_word_at(m_, out, global_word(m_, audio::kXaGroup));
    set_word_at(m_, out + 4, global_word(m_, audio::kXaTrack));
    set_word_at(m_, out + 8, global_word(m_, audio::kXaStart));
}

// 0x004abf80: back to a saved position: the same track just takes its start back, another one is played
void AudioGame::redbook_restore_position(std::uint32_t saved) {
    check(saved != 0, kMsgNoPosition);
    if (word_at(m_, saved) == global_word(m_, audio::kXaGroup) && word_at(m_, saved + 4) == global_word(m_, audio::kXaTrack)) {
        set_global_word(m_, audio::kXaStart, word_at(m_, saved + 8));
        return;
    }
    redbook_stop();
    redbook_play(int_at(m_, saved), int_at(m_, saved + 4));
}

// 0x004ac010: the track number counted from the first level track
std::int32_t AudioGame::redbook_track_number() {
    return add32(add32(global_int(m_, audio::kXaTrack), mul32(global_int(m_, audio::kXaGroup), 8)), -0xf);
}

}  // namespace opentony::runtime::pcplatform
