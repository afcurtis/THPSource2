#include "auto_views.hpp"
#include "audio_game.hpp"
#include "seam_addresses_generated.hpp"

#include <bit>
#include <cmath>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

// the services below the sound front, named by their retail address
constexpr std::uint32_t kPlaySound = seam::hal::Audio_PlaySound;           // Audio_PlaySound(sound, left, right, extra) -> voice
constexpr std::uint32_t kSetVoiceVolume = seam::hal::Audio_SetVoiceVolume;
constexpr std::uint32_t kSetVoiceFrequency = seam::hal::Audio_SetVoiceFrequency;
constexpr std::uint32_t kGetVoiceFrequency = seam::hal::Audio_GetVoiceFrequency;
constexpr std::uint32_t kStopSound = seam::hal::Audio_StopSound;
constexpr std::uint32_t kPauseLooping = seam::hal::Audio_PauseLooping;
constexpr std::uint32_t kResumeLooping = seam::hal::Audio_ResumeLooping;
constexpr std::uint32_t kLoadSoundBank = 0x004f2960;
constexpr std::uint32_t kFreeSoundBank = seam::hal::Audio_FreeSoundBank;
constexpr std::uint32_t kLoadOnDemand = 0x004cb3f0;        // (sound, volume, extra)
constexpr std::uint32_t kPan = 0x004ca690;                 // (position, 0x400, 0x2000) -> two 12-bit levels, the left in the low half
constexpr std::uint32_t kReverbCommand = 0x004e8c30;       // (&{command, value}) -> -1 on failure
constexpr std::uint32_t kReverbSwitch = 0x004e8c20;        // (on, mask)
constexpr std::uint32_t kReverbEnable = 0x004e8c10;        // (1) -> non-zero when it could be turned on

constexpr std::uint32_t kLogUnloaded = 0x00537a40;
constexpr std::uint32_t kLogClosed = 0x00537938;
constexpr std::uint32_t kLogFrontendOpened = 0x00537968;
constexpr std::uint32_t kLogFrontendAttempt = 0x0053794c;
constexpr std::uint32_t kLogLevelOpened = 0x005379a0;
constexpr std::uint32_t kLogLevelNone = 0x005379bc;
constexpr std::uint32_t kLogLevelAttempt = 0x00537980;
constexpr std::uint32_t kMsgBadReverbType = 0x00537a98;
constexpr std::uint32_t kMsgReverbTypeFailed = 0x00537a60;
constexpr std::uint32_t kMsgReverbOnFailed = 0x00537a7c;
constexpr std::uint32_t kMsgReverbDepth = 0x00537ac0;

struct VabName {
    std::uint32_t name;
    std::uint32_t id;
};
constexpr VabName kFrontendNames[] = {{0x0052ce64, 0}, {0x0052ca64, 1}, {0x0052ca7c, 2}, {0x0052ca98, 3}};
constexpr VabName kLevelNames[] = {{0x00537a38, 4}, {0x00537a30, 5}, {0x00537a2c, 6}, {0x00537a28, 7}, {0x00537a20, 8}, {0x00537a1c, 9}, {0x00537a14, 10},
                                   {0x00537a08, 11}, {0x00537a04, 12}, {0x005379fc, 13}, {0x005379f4, 14}, {0x005379ec, 15}, {0x005379e4, 16}, {0x005379e0, 17}};

std::int32_t s16(std::int32_t value) { return static_cast<std::int16_t>(value); }
std::int32_t mul32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }

// the C runtime's _ftol (VC6): fistp to a 64-bit integer with truncation, the low dword returned: what does not fit 64 bits ("integer indefinite", 0x8000000000000000) gives 0
std::int32_t ftol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))));
}

std::string dwords(std::uint32_t first, std::uint32_t second, std::uint32_t third) {
    std::string text;
    for (const std::uint32_t word : {first, second, third}) {
        for (int shift = 0; shift < 32; shift += 8) {
            text.push_back(static_cast<char>((word >> shift) & 0xff));
        }
    }
    return text;
}

}  // namespace

// The pitch scale of a bank record times the float constant, truncated, is what Audio_PlaySound reads from its global
void AudioGame::store_pitch(std::uint32_t record) {
    const double product = static_cast<double>(std::bit_cast<float>(word_at(m_, record + 4))) * static_cast<double>(std::bit_cast<float>(global_word(m_, audio::kPitchScale)));
    set_global_word(m_, audio::kPlayPitch, static_cast<std::uint32_t>(ftol(product)));
}

// 0x004ad620: SFX_PlayX(sound, volume, extra): play a sound at a volume on both sides. In a split screen game the player with the other sound set hears the substitutes (some are dropped),
// a sound whose bank is not loaded is loaded first, and the volume is scaled by the effects setting.
std::int32_t AudioGame::sfx_play(std::int32_t sound, std::int32_t volume, std::int32_t extra) {
    if (global_int(m_, audio::kModeB) == 2 && global_word(m_, audio::kSplitPlayer) != 0) {
        const std::uint32_t player = word_at(m_, global_word(m_, audio::kSplitPlayer) + 0x2cc0);
        if ((byte_at(m_, audio::kPlayerFlags + audio::kPlayerFlagsSize * player) & 1) != 0) {
            if (sound == 0x1d || sound == 0x1e) {
                return 0;
            }
            if (sound == 0x1c) {
                sound = 0x189;
            } else if (sound >= 0xb5 && sound <= 0xb8) {
                sound = 0x18a;
            } else if (sound >= 0xb9 && sound <= 0xbb) {
                sound = 0x18b;
            } else if (sound == 0x180) {
                sound = 0x181;
            }
        }
    }
    if (sound != -1) {
        const std::uint32_t record = bank_record(sound);
        if (record != 0 && byte_at(m_, record + 0x2c) == 0) {
            engine(kLoadOnDemand, {static_cast<std::uint32_t>(sound), static_cast<std::uint32_t>(volume), static_cast<std::uint32_t>(extra)});
        }
    }
    const std::int32_t scaled_volume = mul32(s16(volume), global_int(m_, audio::kSfxVolume)) >> 14;
    if (sound == -1) {
        return -1;
    }
    const std::uint32_t record = bank_record(sound);
    if (record == 0) {
        log_line(kLogUnloaded);
        return -1;
    }
    store_pitch(record);
    return static_cast<std::int32_t>(engine_value(kPlaySound, {static_cast<std::uint32_t>(sound), static_cast<std::uint32_t>(s16(scaled_volume)), static_cast<std::uint32_t>(s16(scaled_volume)),
                                                               static_cast<std::uint32_t>(extra)}));
}

// 0x004ad750: SFX_PlayLR(sound, left, right, extra): the same with a volume for each side
std::int32_t AudioGame::sfx_play_lr(std::int32_t sound, std::int32_t left, std::int32_t right, std::int32_t extra) {
    const std::int32_t left_volume = mul32(s16(left), global_int(m_, audio::kSfxVolume)) >> 14;
    const std::int32_t right_volume = mul32(s16(right), global_int(m_, audio::kSfxVolume)) >> 14;
    if (sound == -1) {
        return -1;
    }
    const std::uint32_t record = bank_record(sound);
    if (record == 0) {
        log_line(kLogUnloaded);
        return -1;
    }
    store_pitch(record);
    return static_cast<std::int32_t>(engine_value(kPlaySound, {static_cast<std::uint32_t>(sound), static_cast<std::uint32_t>(s16(left_volume)), static_cast<std::uint32_t>(s16(right_volume)),
                                                               static_cast<std::uint32_t>(extra)}));
}

// 0x004ad7d0: a sound at a position: the pan service turns it into two 12-bit levels (times four) which the effects setting scales
std::int32_t AudioGame::play_positional_sound(std::int32_t sound, std::uint32_t position, std::int32_t extra) {
    if (sound == -1) {
        return 0;
    }
    const std::uint32_t pan = engine_value(kPan, {position, 0x400, 0x2000});
    const std::int32_t left = mul32(static_cast<std::int32_t>((pan & 0xfff) << 2), global_int(m_, audio::kSfxVolume)) >> 14;
    const std::int32_t right = mul32(static_cast<std::int32_t>(((pan >> 16) & 0xfff) << 2), global_int(m_, audio::kSfxVolume)) >> 14;
    const std::uint32_t record = bank_record(sound);
    if (record == 0) {
        log_line(kLogUnloaded);
        return -1;
    }
    store_pitch(record);
    return static_cast<std::int32_t>(engine_value(kPlaySound, {static_cast<std::uint32_t>(sound), static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(extra)}));
}

// 0x004ad870
std::uint32_t AudioGame::voice_frequency(std::int32_t voice) {
    if (voice == -1) {
        return 0xffffffffu;
    }
    return engine_value(kGetVoiceFrequency, {static_cast<std::uint32_t>(voice)});
}

// 0x004ad890
void AudioGame::set_voice_frequency(std::int32_t voice, std::uint32_t frequency) {
    if (voice != -1) {
        engine(kSetVoiceFrequency, {static_cast<std::uint32_t>(voice), frequency & 0xffff});
    }
}

// 0x004ad8b0: move a playing voice to a new position (the same pan service, each side limited to +-0x3fff)
void AudioGame::modify_position(std::int32_t voice, std::uint32_t position) {
    if (voice == -1) {
        return;
    }
    const std::uint32_t pan = engine_value(kPan, {position, 0x400, 0x2000});
    auto limited = [](std::int32_t value) { return value < -0x3fff ? -0x3fff : value > 0x3fff ? 0x3fff : value; };
    const std::int32_t left = mul32(global_int(m_, audio::kSfxVolume), limited(static_cast<std::int32_t>(pan & 0xfff))) >> 14;
    const std::int32_t right = mul32(global_int(m_, audio::kSfxVolume), limited(static_cast<std::int32_t>((pan >> 16) & 0xfff))) >> 14;
    engine(kSetVoiceVolume, {static_cast<std::uint32_t>(voice), static_cast<std::uint32_t>(s16(left)), static_cast<std::uint32_t>(s16(right))});
}

// 0x004ad950: set the two levels of a positional voice (each below 0x4000), scaled by the effects setting
void AudioGame::update_positional_sound(std::int32_t voice, std::int32_t left, std::int32_t right) {
    if (voice == -1) {
        return;
    }
    if (left >= 0x4000) {
        left = 0x3fff;
    }
    if (right >= 0x4000) {
        right = 0x3fff;
    }
    engine(kSetVoiceVolume, {static_cast<std::uint32_t>(voice), static_cast<std::uint32_t>(mul32(global_int(m_, audio::kSfxVolume), left) >> 14),
                             static_cast<std::uint32_t>(mul32(global_int(m_, audio::kSfxVolume), right) >> 14)});
}

// 0x004ad9c0
void AudioGame::sfx_pause() {
    engine(kPauseLooping);
    redbook_set_paused(1);
}

// 0x004ad9d0
void AudioGame::sfx_resume() {
    engine(kResumeLooping);
    redbook_set_paused(0);
}

// 0x004ad9f0
void AudioGame::stop_voice(std::int32_t voice) {
    if (voice != -1) {
        engine(kStopSound, {static_cast<std::uint32_t>(voice)});
    }
}

// 0x004ada10
void AudioGame::stop_all_and_close() {
    stop_all_sounds();
    close_frontend_vab();
}

// 0x004f38d0 (Audio_StopAll): stop every voice slot in use
void AudioGame::stop_all_sounds() {
    if (global_word(m_, audio::kAudioOpen) == 0) {
        return;
    }
    std::uint32_t voice = 0;
    for (std::uint32_t slot = audio::kVoiceSlots; slot < audio::kVoiceSlotsEnd; slot += audio::kVoiceSlotSize, ++voice) {
        if (word_at(m_, slot) != 0) {
            engine(kStopSound, {voice});
        }
    }
}

// 0x004acff0
void AudioGame::close_frontend_vab() {
    if (global_int(m_, audio::kFrontendVab) != -1) {
        log_line(kLogClosed);
        engine(kFreeSoundBank, {global_word(m_, audio::kFrontendVab)});
        set_global_word(m_, audio::kFrontendVab, 0xffffffffu);
    }
}

// 0x004ad5b0
void AudioGame::close_level_vab() {
    if (global_int(m_, audio::kLevelVab) != -1) {
        log_line(kLogClosed);
        engine(kFreeSoundBank, {global_word(m_, audio::kLevelVab)});
        set_global_word(m_, audio::kLevelVab, 0xffffffffu);
    }
}

// 0x004ad020: open the bank of a front end sound set by its file name; a name nobody knows keeps the bank already open (and opens that again)
void AudioGame::open_frontend_vab(std::uint32_t name) {
    const std::string wanted = m_.cstring(name, 0x100);
    for (const VabName& known : kFrontendNames) {
        if (wanted == m_.cstring(known.name, 0x100)) {
            set_global_word(m_, audio::kFrontendVab, known.id);
            log_line(kLogFrontendOpened);
            engine(kLoadSoundBank, {global_word(m_, audio::kFrontendVab)});
            return;
        }
    }
    if (global_int(m_, audio::kFrontendVab) == -1) {
        log_line(kLogFrontendAttempt);
        return;
    }
    log_line(kLogFrontendOpened);
    engine(kLoadSoundBank, {global_word(m_, audio::kFrontendVab)});
}

// 0x004ad180: the same for a level's sound set (named by the level's short name)
void AudioGame::open_level_vab(std::uint32_t name) {
    const std::string wanted = m_.cstring(name, 0x100);
    bool matched = false;
    for (const VabName& known : kLevelNames) {
        if (wanted == m_.cstring(known.name, 0x100)) {
            set_global_word(m_, audio::kLevelVab, known.id);
            matched = true;
            break;
        }
    }
    if (!matched) {
        log_line(kLogLevelNone);
        if (global_int(m_, audio::kLevelVab) == -1) {
            log_line(kLogLevelAttempt);
            return;
        }
    }
    log_line(kLogLevelOpened);
    engine(kLoadSoundBank, {global_word(m_, audio::kLevelVab)});
}

// 0x004ada20: choose the reverb type (0 off, below 10); a type the SPU refuses or a reverb that cannot be turned on is reported
void AudioGame::set_reverb(std::uint32_t type) {
    const std::uint32_t value = type & 0xff;
    check(value < 10, kMsgBadReverbType);
    if (static_cast<std::int32_t>(engine_value(kReverbCommand, {kStackLocal}, dwords(1, value, 0))) == -1) {
        check(false, kMsgReverbTypeFailed);
        return;
    }
    if (value == 0) {
        set_global_word(m_, audio::kReverbState, 0);
        engine(kReverbSwitch, {0, 0xffffff});
        return;
    }
    if (engine_value(kReverbEnable, {1}) != 0) {
        set_global_word(m_, audio::kReverbState, 0xf0);
        engine(kReverbSwitch, {1, 0xffffff});
        return;
    }
    check(false, kMsgReverbOnFailed);
    engine(kReverbSwitch, {0, 0xffffff});
}

// 0x004adba0: the reverb depth of the two sides (each below 0x80), sent to the SPU packed as a pair of 16-bit values
std::uint32_t AudioGame::set_reverb_depth(std::uint32_t left, std::uint32_t right) {
    const std::uint32_t l = left & 0xff;
    const std::uint32_t r = right & 0xff;
    check(l < 0x80 && r < 0x80, kMsgReverbDepth);
    return engine_value(kReverbCommand, {kStackLocal}, dwords(6, 0, (l << 8) | (r << 24)));
}

}  // namespace opentony::runtime::pcplatform
