#include "auto_views.hpp"
#include "audio_game.hpp"

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

// The 32-bit arithmetic the retail code uses for its divisions by constants: the high half of a signed 64-bit product, an arithmetic shift and the correction that rounds towards zero.
std::int32_t mulhi(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 32); }
std::int32_t shl(std::int32_t value, int count) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << count); }
std::int32_t round_zero(std::int32_t quotient) { return quotient + static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31); }
std::int32_t add32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }
std::int32_t sub32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }
std::int32_t mul32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }

constexpr std::int32_t kFull = 0x4000;
constexpr std::int32_t kRoarScore = 0x3a98;      // 15000

}  // namespace

// 0x0042f300: in a level with a crowd and the game type at 1, start the crowd's looping sound (0x2c) and reset its levels and the boo count
void AudioGame::crowd_start() {
    if (word_at(m_, level_record() + 0x3c) == 0 || global_int(m_, audio::kModeA) != 1) {
        return;
    }
    set_global_word(m_, audio::kCrowdCheers, 0);
    const std::int32_t voice = sfx_play(0x2c, 0, 0);
    set_global_word(m_, audio::kCrowdVoice, static_cast<std::uint32_t>(voice));
    (void)voice_frequency(voice);                  // (the log line "Pitch = %d" prints it; the log reports the format only)
    log_line(0x00527cec);
    set_global_word(m_, audio::kCrowdRight, 0);
    set_global_word(m_, audio::kCrowdTargetRight, 0);
    set_global_word(m_, audio::kCrowdTargetLeft, 0);
    set_global_word(m_, audio::kCrowdLeft, 0);
}

// 0x0042f370: stop the crowd's looping sound
void AudioGame::crowd_stop() {
    if (word_at(m_, level_record() + 0x3c) == 0 || global_int(m_, audio::kModeA) != 1) {
        return;
    }
    if (global_int(m_, audio::kCrowdVoice) != -1) {
        stop_voice(global_int(m_, audio::kCrowdVoice));
    }
    set_global_word(m_, audio::kCrowdVoice, 0xffffffffu);
}

// 0x0042f3e0: move *value one step towards `target` (a step of `step`, or all the way when closer than that); false when it is already there
bool AudioGame::crowd_move_towards(std::uint32_t value, std::int32_t target, std::int32_t step) {
    const std::int32_t current = int_at(m_, value);
    if (current == target) {
        return false;
    }
    if (current > sub32(target, step) && current < add32(target, step)) {
        set_word_at(m_, value, static_cast<std::uint32_t>(target));
        return true;
    }
    set_word_at(m_, value, static_cast<std::uint32_t>(current < target ? add32(current, step) : sub32(current, step)));
    return true;
}

// 0x0042f430: the crowd's per-frame update: the two levels move towards their targets (a new target is drawn now and then), the voice follows, and a waiting cheer may turn to a boo
void AudioGame::crowd_update() {
    if (word_at(m_, level_record() + 0x3c) == 0 || global_int(m_, audio::kModeA) != 1) {
        return;
    }
    if (global_int(m_, audio::kCrowdVoice) != -1) {
        bool moved = crowd_move_towards(audio::kCrowdLeft, global_int(m_, audio::kCrowdTargetLeft), 100);
        if (!moved) {
            moved = crowd_move_towards(audio::kCrowdRight, global_int(m_, audio::kCrowdTargetRight), 100);
        }
        if (moved) {
            const std::int32_t right = round_zero(mulhi(0x55555556, add32(global_int(m_, audio::kCrowdRight), global_int(m_, audio::kCrowdRight))));
            const std::int32_t left = round_zero(mulhi(0x55555556, add32(global_int(m_, audio::kCrowdLeft), global_int(m_, audio::kCrowdLeft))));
            update_positional_sound(global_int(m_, audio::kCrowdVoice), left, right);
        } else if (rnd(0x1e) == 0) {
            set_global_word(m_, audio::kCrowdTargetLeft, static_cast<std::uint32_t>(rnd(0x2000)));
            set_global_word(m_, audio::kCrowdTargetRight, static_cast<std::uint32_t>(rnd(0x2000)));
        }
    }
    if (global_word(m_, audio::kCrowdCheers) != 0 && rnd(0x32) == 0) {
        crowd_boo(shl(global_int(m_, audio::kCrowdCheers), 13));
        set_global_word(m_, audio::kCrowdCheers, global_word(m_, audio::kCrowdCheers) - 1);
    }
}

// 0x0042f540: a score was awarded: a big one makes the crowd roar, any one makes it cheer (louder, with more voices, the bigger it is)
void AudioGame::crowd_react_to_score(std::int32_t score) {
    if (score == 0) {
        return;
    }
    const std::uint32_t record = level_record();
    if (word_at(m_, record + 0x3c) == 0 || global_int(m_, audio::kModeB) >= 2) {
        return;
    }
    if (byte_at(m_, record) != 0 && score > kRoarScore) {
        std::int32_t volume = round_zero(mulhi(0x14f8b589, shl(score, 11)) >> 9);
        if (volume > kFull) {
            volume = kFull;
        }
        sfx_play(0xde, volume, 0);
    }
    if (global_int(m_, audio::kModeA) != 1) {
        return;
    }
    if (global_word(m_, audio::kCrowdCheers) != 0) {
        set_global_word(m_, audio::kCrowdCheers, score >= 100 ? 0 : global_word(m_, audio::kCrowdCheers) - 1);
    }
    std::int32_t volume = round_zero(mulhi(0x68db8bad, shl(score, 14)) >> 12);
    if (volume > kFull) {
        volume = kFull;
    }
    const std::int32_t cheer = (rnd(2) != 0 ? 0xed : 0) + 0x30;
    if (volume > 0x800) {
        sfx_play(cheer, volume, 0);
        sfx_play(0x2e, volume, 0);
        if (score >= 0x1f40) {
            sfx_play(0x31, volume, 0);
        }
        return;
    }
    if (score >= 0x190) {
        sfx_play(0x2e, mul32(volume, 4), 0);
    }
}

// 0x0042f680: the skater bailed: one more cheer is waiting to turn into a groan
void AudioGame::crowd_react_to_bail(std::int32_t score) {
    if (word_at(m_, level_record() + 0x3c) == 0 || global_int(m_, audio::kModeA) != 1) {
        return;
    }
    set_global_word(m_, audio::kCrowdCheers, global_word(m_, audio::kCrowdCheers) + 1);
    std::int32_t volume = round_zero(mulhi(0x66666667, shl(score, 14)) >> 5);
    if (volume > kFull) {
        volume = kFull;
    } else if (volume < 0x800) {
        volume = 0x800;
    }
    sfx_play(0x31, volume, 0);
}

// 0x0042f700: a trick scored: like a score, with its own cheer sounds
void AudioGame::crowd_react_to_trick(std::int32_t score) {
    const std::uint32_t record = level_record();
    if (word_at(m_, record + 0x3c) == 0 || global_int(m_, audio::kModeB) >= 2) {
        return;
    }
    if (byte_at(m_, record) != 0 && score > kRoarScore) {
        std::int32_t volume = round_zero(mulhi(0x14f8b589, shl(score, 11)) >> 9);
        if (volume > kFull) {
            volume = kFull;
        }
        sfx_play(0xdf, volume, 0);
    }
    if (global_int(m_, audio::kModeA) != 1) {
        return;
    }
    if (score < 100) {
        set_global_word(m_, audio::kCrowdCheers, global_word(m_, audio::kCrowdCheers) + 2);
    } else {
        set_global_word(m_, audio::kCrowdCheers, 0);
    }
    std::int32_t volume = round_zero(mulhi(0x68db8bad, shl(score, 14)) >> 11);
    if (volume > kFull) {
        volume = kFull;
    } else if (volume < 0x1000) {
        volume = 0x1000;
        sfx_play(0x2f + (rnd(2) != 0 ? 3 : 0), volume, 0);
        return;
    }
    sfx_play(0x2e, (volume - (volume >> 31)) >> 1, 0);
    sfx_play(0x2f + (rnd(2) != 0 ? 3 : 0), volume, 0);
}

// 0x0042f800: a boo from the crowd, panned to a random side
void AudioGame::crowd_boo(std::int32_t strength) {
    if (word_at(m_, level_record() + 0x3c) == 0 || global_int(m_, audio::kModeA) != 1) {
        return;
    }
    auto level = [&] { return round_zero(mulhi(0x51eb851f, mul32(add32(rnd(0x14), 0x5a), strength)) >> 5); };
    const std::int32_t first = level();
    const std::int32_t second = level();
    sfx_play_lr(0x32, second, first, 0);
}

}  // namespace opentony::runtime::pcplatform
