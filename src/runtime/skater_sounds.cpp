#include "skater_views.hpp"

// Phase 4.15a: the continuous sounds of a skater (physics.cpp, 0x0049c970): the rail grind, the board's roll on the surface and the wheels' whine. Every sound is a voice the audio layer hands out
// (-1: none) that is kept in the skater (+0x308c the grinding start, +0x3090 the rail grind, +0x3094 the surface roll, +0x3098 the wheels), started when the state calls for it, kept in tune with the speed and
// stopped when the state ends. The speed is the integer root of the velocity's squared length (Q12) times 64, scaled by the skater's speed stat (`Skater_ProfileValue(3)`).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kNone = 0xffffffffu;
constexpr Word kStopVoice = 0x004ad9f0;           // SFX_StopVoice(voice)
constexpr Word kSetFrequency = 0x004ad890;        // SFX_SetVoiceFrequency(voice, frequency)
constexpr Word kUpdateSound = 0x004ad950;         // Audio_UpdatePositionalSound(voice, volume, volume)
constexpr Word kMsgRailType = 0x00536d00;         // "Unknown rail type"

}  // namespace

// 0x0049c970 (fastcall)
void SkaterKit::handle_continuous_sounds(Word self) {
    const auto play = [this](Word sound) { return engine_value(kSoundPlay, {sound, 0x2000, 0}); };
    const auto stop = [this, self](Word field) {
        engine(kStopVoice, {word_at(m_, self + field)});
        set_word_at(m_, self + field, kNone);
    };
    const Word state = skater_at(m_, self).physics_state();

    // the start of a grind
    if (state == 5) {
        if (skater_at(m_, self).voice_308c() == kNone) {
            skater_at(m_, self).set_voice_308c(static_cast<Word>(play(0xf)));
        }
    } else if (skater_at(m_, self).voice_308c() != kNone) {
        stop(0x308c);
    }

    // the rail grind: a sound by the rail's type and the direction
    if (state == 4) {
        if (skater_at(m_, self).voice_3090() == kNone) {
            const bool reverse = skater_at(m_, self).field_2c0c() != 0;
            switch (skater_at(m_, self).field_2e8c()) {
            case 0:
                skater_at(m_, self).set_voice_3090(static_cast<Word>(play(reverse ? 0xce : 0xb)));
                break;
            case 1:
                skater_at(m_, self).set_voice_3090(static_cast<Word>(play(reverse ? 0xc : 0xd1)));
                break;
            case 2:
                skater_at(m_, self).set_voice_3090(static_cast<Word>(play(reverse ? 0xcf : 0xd3)));
                break;
            case 3:
                skater_at(m_, self).set_voice_3090(static_cast<Word>(play(reverse ? 0xd0 : 0xd4)));
                break;
            default:
                check(false, kMsgRailType);
                break;
            }
        }
    } else if (skater_at(m_, self).voice_3090() != kNone) {
        stop(0x3090);
    }

    // the roll on the surface: on the ground or in the air-less states 0 and 7 only; a change of surface (or a stopped skater) ends the voice
    if (state == 0 || state == 7) {
        if (skater_at(m_, self).voice_3094() != kNone && (skater_at(m_, self).field_30b4() != skater_at(m_, self).field_30b0() || skater_at(m_, self).field_2f64() != 0)) {
            stop(0x3094);
        }
        if (skater_at(m_, self).voice_3094() == kNone && skater_at(m_, self).field_2f64() == 0) {
            skater_at(m_, self).set_field_30b4(static_cast<Word>(skater_at(m_, self).field_30b0()));
            skater_at(m_, self).set_field_3084(100);
            skater_at(m_, self).set_field_3088(100);
            Word sound = 0;
            switch (skater_at(m_, self).field_30b0()) {
            case 1: sound = 0x49; break;
            case 2: sound = 1; break;
            case 3: sound = 0x21; break;
            case 4: sound = 0xa9; break;
            case 5:
                sound = 0x3d;
                skater_at(m_, self).set_field_3084(0x28);
                skater_at(m_, self).set_field_3088(0x96);
                break;
            case 6: sound = 0x48; break;
            case 8: sound = 0x39; break;
            case 0xc: sound = 0x37; break;
            case 0xd: sound = 0x38; break;
            case 0xe: sound = 0x69; break;
            case 0xf: sound = 0x77; break;
            default: sound = 0; break;
            }
            skater_at(m_, self).set_voice_3094(static_cast<Word>(play(sound)));
        }
    } else if (skater_at(m_, self).voice_3094() != kNone) {
        stop(0x3094);
    }

    // the wheels: started at a pitch worked out from the speed in the states 1, 2 and 8, then faded away
    const Word velocity = self + 0x4c;
    // ((speed stat + 500) * 0x2d000) / 0x118
    const auto scale = [this, self] { return s(u(profile_value(self, 3) + 500) * 0x2d000u) / 0x118; };
    if (state == 1 || state == 2 || state == 8) {
        if (skater_at(m_, self).field_3098() == kNone) {
            std::int32_t speed = s(sqrt_truncated(u(math_.dot_q12(velocity, velocity))) << 6);
            if (skater_at(m_, self).physics_state() == 8) {
                speed = 300000;
            }
            const std::int32_t first = scale();
            const std::int32_t sixteenth = (first + ((first >> 31) & 0xf)) >> 4;
            const Word volume_base = static_cast<Word>(s(u(speed) * 200u) / sixteenth) & 0xffff;
            skater_at(m_, self).set_field_3098(static_cast<Word>(engine_value(kSoundPlay, {7, u((2 * s(volume_base)) / 3), 0})));
            const std::int32_t second = scale();
            Word frequency = u(s(u(speed) << 11) / second);
            if ((frequency & 0xffff) <= 0x190) {
                frequency = 0x190;
            } else if ((frequency & 0xffff) >= 0x400) {
                frequency = 0x3ff;
            }
            skater_at(m_, self).set_field_309c(static_cast<Word>(frequency & 0xffff));
            skater_at(m_, self).set_field_30a0(static_cast<Word>(volume_base));
            engine(kSetFrequency, {skater_at(m_, self).field_3098(), frequency});
        } else {
            const Word volume = skater_at(m_, self).field_30a0();
            if (volume != 0) {
                const Word faded = volume - (volume >> 5);
                skater_at(m_, self).set_field_30a0(static_cast<Word>(faded));
                engine(kUpdateSound, {skater_at(m_, self).field_3098(), faded, faded});
            }
        }
    } else if (skater_at(m_, self).field_3098() != kNone) {
        stop(0x3098);
    }

    // the surface roll follows the speed
    if (skater_at(m_, self).voice_3094() != kNone) {
        const std::int32_t root = s(sqrt_truncated(u(math_.dot_q12(velocity, velocity))));
        const std::int32_t pitch = s(u(root) << 17) / scale();
        const std::int32_t whole = scale();           // (the stat is read twice: for the pitch, then for the volume)
        const std::int32_t sixteenth = (whole + ((whole >> 31) & 0xf)) >> 4;
        const std::int32_t level = s(u(root) * 0x1fa00u) / sixteenth;
        const std::int32_t volume = s(u(s(skater_at(m_, self).field_3084())) * u(level)) / 100;
        engine(kUpdateSound, {skater_at(m_, self).voice_3094(), u(volume), u(volume)});
        std::int32_t clamped = pitch;
        if (pitch <= 0x2bc) {
            clamped = 0x2bc;
        } else if (pitch >= 0x400) {
            clamped = 0x3ff;
        }
        const std::int32_t frequency = s(u(s(skater_at(m_, self).field_3088())) * u(clamped)) / 100;
        engine(kSetFrequency, {skater_at(m_, self).voice_3094(), u(frequency)});
        skater_at(m_, self).set_field_309c(static_cast<Word>(u(frequency)));
        skater_at(m_, self).set_field_30a0(static_cast<Word>(u(volume)));
    }
}

}  // namespace opentony::runtime::skaterkit
