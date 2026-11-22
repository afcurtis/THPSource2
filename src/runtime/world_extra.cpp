#include "world_views.hpp"
#include "frame_pacing.hpp"
#include "host_options.hpp"

// Phase 4.15c: the small methods of the platform (vtable 0x005194f8), the power-up (vtable 0x00519684) and the traffic object (vtable 0x005184e0): their destructors and the few setters between
// the big methods. `self` is the object; the object lists, the sound and the power-up's child object are services.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kObjectFree = 0x0047fd60;
constexpr Word kListRemove = 0x004801f0;
constexpr Word kListPushHead = 0x004801d0;
constexpr Word kStopVoice = 0x004ad9f0;

constexpr Word kPlatformVtable = 0x005194f8;
constexpr Word kPowerupVtable = 0x00519684;
constexpr Word kTrafficVtable = 0x005184e0;
constexpr Word kActiveList = 0x0056af40;
constexpr Word kPowerupList = 0x0056b830;
constexpr Word kBaddyList = 0x0055f6bc;
constexpr Word kTrafficCount = 0x0055faec;
constexpr Word kRegionNames = 0x0056e218;             // dword: the PSX region the level's objects live in

// the platform methods that take the integer square root (0x004f53b0, retail code in their captures) use the native one while they run
struct NativeRoot {
    core::CoreKit& core;
    bool before;
    explicit NativeRoot(core::CoreKit& c) : core(c), before(c.set_native_square_root(true)) {}
    ~NativeRoot() { core.set_native_square_root(before); }
};

std::int32_t sx16i(Word value) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(value)); }

}  // namespace

// ---- the platform ----

// 0x0049f340 (thiscall)
Word WorldKit::platform_scalar_delete(Word self, Word flags) {
    platform_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x0049f360 (thiscall, one argument): a platform put at a position (three dwords) by a caller that has no trigger record
Word WorldKit::platform_construct_at(Word self, Word position) {
    baddy_construct(self);
    platform_at(m_, self).set_field_1fc(0);
    platform_at(m_, self).set_field_1fe(0);
    platform_at(m_, self).set_field_200(0);
    platform_at(m_, self).set_field_20c(0);
    platform_at(m_, self).set_field_210(0);
    platform_at(m_, self).set_field_214(0);
    platform_at(m_, self).set_vtable(static_cast<Word>(kPlatformVtable));
    select_psx_region(self, global_word(m_, kRegionNames));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 8 + 4 * k, word_at(m_, position + 4 * k));
    }
    thiscall(kListPushHead, self, {kActiveList});
    set_half_at(m_, self + object::kFlags, (half_at(m_, self + object::kFlags) & 0xfffdu) | 0x111u);
    platform_at(m_, self).set_field_1f2(0x192);
    platform_at(m_, self).set_field_17c(0);
    platform_at(m_, self).set_field_208(0xffffffff);
    platform_at(m_, self).set_field_1a0(0x20);
    platform_at(m_, self).set_field_16c(0);
    return self;
}

// 0x0049f440
void WorldKit::platform_destruct(Word self) {
    platform_at(m_, self).set_vtable(static_cast<Word>(kPlatformVtable));
    thiscall(kListRemove, self, {kActiveList});
    const Word voice = platform_at(m_, self).field_204();
    if (voice != 0xffffffffu) {
        engine(kStopVoice, {voice});
        platform_at(m_, self).set_field_204(0xffffffff);
    }
    baddy_destruct(self);
}

// 0x004a03e0 (CPlatform__DoPhysics, thiscall): the platform's motion for one frame. Kind 0 (+0x1a4): the velocity (+0x4c) takes the acceleration (+0x58), the three components within +-0x800 are zeroed, the
// position and the angles (+0x14, 12 bits) take the velocity and the spin (+0x70), the spin takes its acceleration (+0x76) and its words within +-1 are zeroed; with a recorded path (+0x1c0 != 0) that step runs
// once per two game ticks elapsed since the path began (+0x1e0), until the path index (+0x1dc) reaches the path length. Kinds 1 and 2: the baddy's step (decay by the shift counts +0x6c and +0x7c, then the
// dead zones); any other kind asserts.
void WorldKit::platform_do_physics(Word self) {
    constexpr Word kClock = 0x0056e320;
    constexpr Word kPlatformMessage = 0x00536fb0;
    platform_at(m_, self).set_field_84(0);
    platform_at(m_, self).set_field_82(0);
    platform_at(m_, self).set_field_80(0);
    const Word kind = platform_at(m_, self).field_1a4();
    if (kind == 0) {
        const auto step = [&] {
            Word velocity[3];
            for (Word k = 0; k < 3; ++k) {
                const Word sum = word_at(m_, self + 0x58 + 4 * k) + word_at(m_, self + 0x4c + 4 * k);
                velocity[k] = (s(sum) <= 0x800 && s(sum) >= -0x800) ? 0 : sum;
            }
            platform_at(m_, self).set_field_4c(static_cast<Word>(velocity[0]));
            platform_at(m_, self).set_field_8(static_cast<Word>(platform_at(m_, self).field_8() + velocity[0]));
            platform_at(m_, self).set_field_c(static_cast<Word>(platform_at(m_, self).field_c() + velocity[1]));
            platform_at(m_, self).set_field_10(static_cast<Word>(platform_at(m_, self).field_10() + velocity[2]));
            platform_at(m_, self).set_field_50(static_cast<Word>(velocity[1]));
            platform_at(m_, self).set_field_54(static_cast<Word>(velocity[2]));
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, self + 0x14 + 2 * k, (half_at(m_, self + 0x14 + 2 * k) + half_at(m_, self + 0x70 + 2 * k)) & 0xfffu);
            }
            for (Word k = 0; k < 3; ++k) {
                const Word spin = (half_at(m_, self + 0x70 + 2 * k) + half_at(m_, self + 0x76 + 2 * k)) & 0xffffu;
                set_half_at(m_, self + 0x70 + 2 * k, (sx16i(spin) >= -1 && sx16i(spin) <= 1) ? 0 : spin);
            }
        };
        if (platform_at(m_, self).field_1c0() == 0) {
            step();
            return;
        }
        const Word start = platform_at(m_, self).field_1e0();
        if (global_word(m_, kClock) <= start + 2 * platform_at(m_, self).field_1dc()) {
            return;
        }
        for (;;) {
            const Word index = platform_at(m_, self).field_1dc() + 1;
            platform_at(m_, self).set_field_1dc(static_cast<Word>(index));
            if (index == platform_at(m_, self).field_1c0()) {
                return;
            }
            step();
            if (global_word(m_, kClock) <= start + 2 * index) {
                return;
            }
        }
    }
    if (kind == 1 || kind == 2) {
        vec3_add(self + 0x4c, self + 0x58);
        vec3_decay_shift(self + 0x4c, self + 0x6c);
        core_.vector_dead_zone(self + 0x4c);
        vec3_add(self + 0x08, self + 0x4c);
        vec3s_add(self + 0x14, self + 0x70);
        vec3s_mask12(self + 0x14);
        vec3s_add(self + 0x70, self + 0x76);
        vec3s_decay_shift(self + 0x70, self + 0x7c);
        vec3s_snap_near_zero(self + 0x70);
        return;
    }
    set_global_word(m_, 0x00568638, 0x38a);
    set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\platform.cpp");
    check(false, kPlatformMessage);
}

// 0x0049fef0 (CPlatform__Update, vtable slot 3, thiscall): the scale animation timers (+0x1e4, +0x1e6, +0x1e8 count down, adding +0x1ea.. to the scale words +0x28..), the input flags (+0x68) copied to +0x174 (bit 0
// moved to the counter +0x170), the acceleration cleared; a bouncy platform (+0x178 bit 4) runs BouncyAI and stops. Otherwise the script (mode +0x16c != 0: a recorded path (+0x1c0) running down, or the script
// stream at +0x17c), then the move-to target (+0x178 bit 0, +0x1c4): arrived within +0x1a0 the position becomes the target (kinds 0 and 1 stop), else kind 0 sets the velocity towards it, kind 1 the acceleration
// (and, with bit 5, rolls towards the bank), kind 2 the acceleration only; with no target kind 1 levels the roll. Then gravity (+0x16e), the physics, and the positional sound every +0x208 ticks.
void WorldKit::platform_update(Word self) {
    const NativeRoot native_root(core_);
    constexpr Word kTick = 0x0056a8d4, kGravity = 0x0056b770;
    constexpr Word kDifference = kScratch + 0x210, kAngles = kScratch + 0x220;
    for (Word k = 0; k < 3; ++k) {
        const Word count = self + 0x1e4 + 2 * k;
        if (half_at(m_, count) != 0) {
            set_half_at(m_, self + 0x28 + 2 * k, half_at(m_, self + 0x28 + 2 * k) + half_at(m_, self + 0x1ea + 2 * k));
            set_half_at(m_, count, half_at(m_, count) - 1);
        }
    }
    platform_at(m_, self).set_field_60(0);
    platform_at(m_, self).set_field_5c(0);
    platform_at(m_, self).set_field_58(0);
    const Word input = platform_at(m_, self).field_68();
    platform_at(m_, self).set_field_174(static_cast<std::uint16_t>(input));
    if ((input & 1u) != 0) {
        platform_at(m_, self).set_field_170(static_cast<std::uint8_t>(platform_at(m_, self).field_170() + 1));
        platform_at(m_, self).set_field_68(static_cast<std::uint16_t>(input & 0xfffeu));
    }
    if ((byte_at(m_, self + 0x178) & 0x10u) != 0) {
        thiscall(0x0049f780, self);                                           // CPlatform__BouncyAI
        return;
    }
    if (platform_at(m_, self).field_16c() != 0) {
        const Word ring = platform_at(m_, self).field_1c0();
        if (ring != 0) {
            if (platform_at(m_, self).field_1dc() >= ring) {
                platform_at(m_, self).set_field_1c0(0);
                platform_at(m_, self).set_field_1dc(0);
                platform_at(m_, self).set_field_1e0(0);
            }
        } else {
            thiscall(0x00401520, self, {platform_at(m_, self).field_17c()});               // TRG_RunScriptStream
        }
    }
    const Word kind = platform_at(m_, self).field_1a4();
    const auto set_roll = [&](Word roll) {      // the roll acceleration (+0x7a) towards a bank, or the roll spin cleared when level
        Word at = (roll - platform_at(m_, self).field_18()) & 0xffffu;
        if (sx16i(at) < -0x800) at += 0x1000;
        if (sx16i(at) > 0x800) at -= 0x1000;
        at &= 0xffffu;
        if (at != 0) {
            platform_at(m_, self).set_field_7a(static_cast<std::uint16_t>(u((sx16i(at) * 32) >> 8)));
        } else {
            platform_at(m_, self).set_field_74(0);
        }
    };
    if ((byte_at(m_, self + 0x178) & 1u) != 0) {
        const Word target = self + 0x1c4, position = self + 0x8;
        core_.vector_subtract_out(kDifference, position, target);
        if (s(core_.vector_length(kDifference)) <= static_cast<std::int32_t>(platform_at(m_, self).field_1a0())) {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, position + 4 * k, word_at(m_, target + 4 * k));
            }
            if (kind == 0 || kind == 1) {
                platform_at(m_, self).set_field_54(0);
                platform_at(m_, self).set_field_50(0);
                platform_at(m_, self).set_field_4c(0);
            }
            set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) & 0xfffffffeu);
        } else if (kind == 0) {
            set_global_half(m_, kAngles, 0);
            set_global_half(m_, kAngles + 2, 0);
            set_global_half(m_, kAngles + 4, 0);
            core_.look_angles(kAngles, position, target);
            core_.direction_from_angles(self + 0x4c, platform_at(m_, self).field_1a0(), kAngles);
        } else if (kind == 1 || kind == 2) {
            set_global_half(m_, kAngles, 0);
            set_global_half(m_, kAngles + 2, 0);
            set_global_half(m_, kAngles + 4, 0);
            core_.look_angles(kAngles, position, target);
            core_.vector_subtract_out(kDifference, target, position);
            std::int32_t turn = divide(0x1fe0, s(core_.vector_length(kDifference)));
            if (turn < -0x100) {
                turn = -0x100;
            } else if (turn > 0x100) {
                turn = 0x100;
            } else if (turn > 0) {
                if (turn < 4) turn = 4;
            } else if (turn < 0) {
                if (turn > -4) turn = -4;
            }
            core_.turn_towards(word_at(m_, self + 0x14), platform_at(m_, self).field_18(), self + 0x70, self + 0x76, global_word(m_, kAngles), global_half(m_, kAngles + 4), turn);
            set_half_at(m_, self + 0x14, 0);
            core_.direction_from_angles(self + 0x58, platform_at(m_, self).field_1a0(), kAngles);
            if (kind == 1) {
                std::int32_t bank = 0;
                if ((byte_at(m_, self + 0x178) & 0x20u) != 0) {
                    std::int32_t t = static_cast<std::int32_t>((global_half(m_, kAngles + 2) - half_at(m_, self + 0x16)) & 0xfffu);
                    if (t > 0x800) t -= 0x1000;
                    if (t > 0x400) {
                        t = 0x400;
                    } else if (t < -0x400) {
                        t = -0x400;
                    }
                    bank = (-t) >> 1;
                }
                bank &= 0xfff;
                set_roll(u(bank));
            }
        }
    } else if (kind == 1) {
        set_roll(0);
    }
    if (platform_at(m_, self).field_16e() != 0) {
        vec3_add(self + 0x58, kGravity);
    }
    thiscall(0x004a03e0, self);                                               // CPlatform__DoPhysics (a service here: its own fixture; its form is platform_do_physics)
    const Word period = platform_at(m_, self).field_208();
    if (period != 0xffffffffu) {
        if (global_word(m_, kTick) - u(divide(s(global_word(m_, kTick)), s(period))) * period == 0) {
            engine(0x004ad8b0, {platform_at(m_, self).field_204(), self + 0x8, 0});        // Audio_UpdatePositionalSound
        }
    }
}

// 0x004a0360 (CPlatform__Hit, vtable slot 6, thiscall, three arguments of which only the first, the damage, is read; answers 1): counts the hit in +0x16f; a platform that takes damage (+0x1a2 non-zero) with hit points
// left (+0xb4, a signed word, above 0) loses the damage: still above 0 it flashes (three frames, white), else it dies (slot 2) and shatters.
Word WorldKit::platform_hit(Word self, Word damage) {
    platform_at(m_, self).set_field_16f(static_cast<std::uint8_t>(platform_at(m_, self).field_16f() + 1));
    if (platform_at(m_, self).field_1a2() == 0 || sx16i(platform_at(m_, self).field_b4()) <= 0) {
        return 1;
    }
    const Word points = (platform_at(m_, self).field_b4() - damage) & 0xffffu;
    platform_at(m_, self).set_field_b4(static_cast<std::uint16_t>(points));
    if (sx16i(points) > 0) {
        thiscall(0x0047fe90, self, {3, 0xff, 0xff, 0xff});                    // CItem__Flash
        return 1;
    }
    virtual_call(self, 2);
    engine(0x004af0b0, {self, 0, 1});                                         // Shatter_Item
    return 1;
}

// 0x004c4950 (TrickTips_Show, thiscall on the monitor 0x0056db98, one argument): shows tip n. The tip is asserted below 0x16 (tricktips.cpp line 0x158); a negative one ends there. After the monitor's first
// frame (+0x58 non-zero) a tip is taken only when 600 frames have passed since the last one (+0x5c), which is set to the frame even when the tip then turns out not to show. The mask bank is 0 below 0x20, 1 below 0x40
// and else asserted (line 0xc0) and the tip number itself (the code goes on with it). With the tips option (0x0056a46c) on and the tip not in the level's "never again" mask (0x0053c288, 0x6b words a level), the tip is
// marked seen (0x0056a40c[bank]) before the next test: in the remembered mask (0x0056a414[bank]) nothing is shown, in the seen mask the heading is "REMEMBER:" (0x0052e294) and the tip joins the remembered mask,
// else "TIP:" (0x0052e290); the three text lines come from the 0x14-byte row of the table at 0x00518b98 and the timer is set to 200.
void WorldKit::tips_show(Word self, Word tip_word) {
    constexpr Word kFileName = 0x00541954, kMessageTip = 0x00541974, kMessageBank = 0x00541934, kTable = 0x00518b98, kSeen = 0x0056a40c, kRemembered = 0x0056a414, kOption = 0x0056a46c, kLevel = 0x0056a898;
    constexpr Word kLevelMasks = 0x0053c288, kHeading = 0x0052e290, kRemember = 0x0052e294;
    const std::int32_t tip = s(tip_word);
    set_global_word(m_, 0x00568638, 0x158);
    set_string(0x0056881c, m_.cstring(kFileName, 256));
    check(tip < 0x16, kMessageTip);
    if (tip < 0) {
        return;
    }
    const Word frames = word_at(m_, self + 0x58);
    if (frames != 0 && word_at(m_, self + 0x5c) + 0x258u > frames) {
        return;
    }
    set_word_at(m_, self + 0x5c, frames);
    Word bank = tip_word;
    if (tip < 0x20) {
        bank = 0;
    } else if (tip < 0x40) {
        bank = 1;
    } else {
        set_global_word(m_, 0x00568638, 0xc0);
        set_string(0x0056881c, m_.cstring(kFileName, 256));
        check(false, kMessageBank);
    }
    if (global_word(m_, kOption) == 0) {
        return;
    }
    const Word bit = 1u << ((tip_word - (bank << 5)) & 31u);
    const Word level = global_word(m_, kLevel);
    if ((word_at(m_, kLevelMasks + 4 * (level * 0x6bu + bank)) & bit) != 0) {
        return;
    }
    const Word remembered = word_at(m_, kRemembered + 4 * bank);
    const Word seen = word_at(m_, kSeen + 4 * bank);
    set_word_at(m_, kSeen + 4 * bank, seen | bit);
    if ((remembered & bit) != 0) {
        return;
    }
    if ((seen & bit) != 0) {
        set_word_at(m_, self + 0x90, global_word(m_, kRemember));
        set_word_at(m_, kRemembered + 4 * bank, word_at(m_, kRemembered + 4 * bank) | bit);
    } else {
        set_word_at(m_, self + 0x90, global_word(m_, kHeading));
    }
    const Word row = kTable + 0x14u * tip_word;
    set_word_at(m_, self + 0x94, word_at(m_, row + 8));
    set_word_at(m_, self + 0x98, word_at(m_, row + 12));
    set_word_at(m_, self + 0x9c, word_at(m_, row + 16));
    set_word_at(m_, self + 0xa0, 200);
}

// 0x004c4620 (TrickTips_Update, cdecl, once a frame from Game_GameplayUpdate): the trick tip monitor 0x0056db98 watches the player's trick counters. Nothing happens while the pause check (0x00419420) or the movie
// (Video_Playing) answers, the replay byte (0x00561990) is set, the game mode (0x00533f38) is not 1 or the level state (0x0055fd78) is 4 or more. The player (0x0056a960) is kept in +0x64; on the first frame (+0x58 is 0)
// the first tip whose table state is 0 and whose bit is not yet seen is shown (none: -1, which the show method drops). The frame counters (+0x58, +0x60) count up and nine counters of the player (signed words at +0x302c ..
// +0x3058) are compared with the highest values the monitor has seen of them: a rise stores the new highest value and counts (+0xd8.. +0x104); the third rise of +0x3038 shows tip 5, the fifth of +0x3040 tip 2, the
// third of +0x3054 tip 3 (unless +0x3058 has risen three times), 150 frames of either held flag (0x0056b098, 0x0056b204) tip 1; a rise of +0x303c restarts the counter at +0x60, which also restarts after 600 frames.
void WorldKit::tips_update() {
    constexpr Word kMonitor = 0x0056db98, kShow = 0x004c4950, kTable = 0x00518b98, kSeen = 0x0056a40c;
    if ((engine_value(0x00419420) & 0xffu) != 0 || (engine_value(0x004cde70) & 0xffu) != 0) {
        return;
    }
    if (global_byte(m_, 0x00561990) != 0 || global_word(m_, 0x00533f38) != 1 || s(global_word(m_, 0x0055fd78)) >= 4) {
        return;
    }
    set_global_word(m_, kMonitor + 0x64, global_word(m_, 0x0056a960));
    if (global_word(m_, kMonitor + 0x58) == 0) {
        Word pick = 0xffffffffu;
        for (Word tip = 0; tip < 22; ++tip) {                      // (the rows of the table; the mask bank of a tip below 22 is 0: 0x004c48f0)
            if (word_at(m_, kTable + 0x14u * tip) == 0 && (global_word(m_, kSeen) & (1u << tip)) == 0) {
                pick = tip;
                break;
            }
        }
        thiscall(kShow, kMonitor, {pick});
    }
    // F1.5E: the monitor counts 30 Hz frames (the first call always counts, it is the "first frame" edge)
    const Word frame_step = global_word(m_, kMonitor + 0x58) == 0 ? 1u : framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });
    const Word frames = global_word(m_, kMonitor + 0x58) + frame_step;
    const Word since = global_word(m_, kMonitor + 0x60) + frame_step;
    set_global_word(m_, kMonitor + 0x58, frames);
    set_global_word(m_, kMonitor + 0x60, since);
    const auto player = [&]() { return global_word(m_, kMonitor + 0x64); };
    // a watched counter: true (the highest value stored) when it rose
    const auto rose = [&](Word highest, Word counter) {
        const Word value = word_at(m_, player() + counter);
        if (s(word_at(m_, kMonitor + highest)) >= s(value)) {
            return false;
        }
        set_word_at(m_, kMonitor + highest, value);
        return true;
    };
    const auto count = [&](Word at) {
        const Word now = word_at(m_, kMonitor + at) + 1;
        set_word_at(m_, kMonitor + at, now);
        return s(now);
    };
    rose(0xc0, 0x3048);
    if (rose(0xb4, 0x303c)) {
        count(0xe8);
        set_global_word(m_, kMonitor + 0x60, 0);
    } else if (global_word(m_, kMonitor + 0x60) > 0x258) {
        set_global_word(m_, kMonitor + 0x60, 0);
    }
    if (global_byte(m_, 0x0056b098) == 0 && global_byte(m_, 0x0056b204) == 0) {
        set_global_word(m_, kMonitor + 0x10c, 0);
    } else {
        const Word held = word_at(m_, kMonitor + 0x10c) + frame_step;   // retail's count(): always stored, then tested
        set_word_at(m_, kMonitor + 0x10c, held);
        if (s(held) > 0x96) {
            thiscall(kShow, kMonitor, {1});
            set_global_word(m_, kMonitor + 0x10c, 0);
        }
    }
    if (rose(0xcc, 0x3054) && count(0x100) >= 3) {
        const Word before = global_word(m_, kMonitor + 0x104);
        set_global_word(m_, kMonitor + 0x100, 0);
        if (s(before) < 3) {
            thiscall(kShow, kMonitor, {3});
        }
    }
    if (rose(0xc8, 0x3050)) {
        count(0xfc);
    }
    if (rose(0xd0, 0x3058)) {
        count(0x104);
    }
    if (rose(0xb8, 0x3040) && count(0xec) >= 5) {
        thiscall(kShow, kMonitor, {2});
        set_global_word(m_, kMonitor + 0xec, 0);
    }
    if (rose(0xa4, 0x302c) && count(0xd8) >= 3) {
        set_global_word(m_, kMonitor + 0xd8, 0);
    }
    if (rose(0xa8, 0x3030) && count(0xdc) >= 3) {
        set_global_word(m_, kMonitor + 0xdc, 0);
    }
    if (rose(0xb0, 0x3038) && count(0xe4) >= 3) {
        thiscall(kShow, kMonitor, {5});
        set_global_word(m_, kMonitor + 0xe4, 0);
    }
}

// 0x004c4420 (TrickTips_Display, cdecl, from the HUD each frame): nothing while the heading pointer (0x0056dc28) is 0. The panel's x is 20, slides in over the first ten frames of the 200-frame timer (0x0056dc38: when the
// low word of 200 - timer is below 10, 40 * (200 - timer) - 0x17c) and out over the last ten (timer below 10: plus 40 * timer - 0x190), and is cut to 16 bits. Mess_SetTextJustify(1), Mess_SetRGB(0x80, 0x80, 0, 0),
// Mess_SetScale(0xdc), the heading at (x, 100), then each non-empty line (0x0056dc2c, 30, 34) at (x, 100 + n * (its Mess_GetScale width * 20 / 220)) for n = 1..3 (the arguments of the width call, the line and 0, are left on
// the stack and are the last two of the draw call). The timer counts down and the panel clears itself at 0.
void WorldKit::tips_display() {
    constexpr Word kTitle = 0x0056dc28, kTimer = 0x0056dc38, kJustify = 0x004734b0, kRgb = 0x00473580, kScale = 0x004734f0, kGetScale = 0x00473500, kDraw = 0x004735b0;
    if (global_word(m_, kTitle) == 0) {
        return;
    }
    const Word timer = global_word(m_, kTimer);
    const Word elapsed = 200u - timer;
    Word x = 0x14;
    if ((elapsed & 0xffffu) < 10) {
        x = elapsed * 40u - 0x17cu;
    }
    if (timer < 10) {
        x = x + timer * 40u - 0x190u;
    }
    x &= 0xffffu;
    engine(kJustify, {1});
    engine(kRgb, {0x80, 0x80, 0, 0});
    engine(kScale, {0xdc});
    engine(kDraw, {x, 100, global_word(m_, kTitle), 0});
    for (Word row = 1; row <= 3; ++row) {
        const Word text = word_at(m_, kTitle + 4 * row);
        if (text == 0) {
            continue;
        }
        const std::int32_t step = s(engine_value(kGetScale, {text, 0}) * 20u) / 220;
        engine(kDraw, {x, u(step * s(row) + 100), text, 0});
    }
    const Word left = global_word(m_, kTimer) - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); });   // F1.5E: 30 Hz frames
    set_global_word(m_, kTimer, left);
    if (left == 0) {
        set_global_word(m_, kTitle + 12, 0);
        set_global_word(m_, kTitle + 8, 0);
        set_global_word(m_, kTitle + 4, 0);
        set_global_word(m_, kTitle, 0);
    }
}

// 0x00466c10 (LevelEvent_InitializeAndDispatch, cdecl, no arguments; the TRG command 0x9e, the end of a run and the bruce timer reach it): the game-over wait (0x004dee50) and the music reset (0x004ab9c0) are services.
// The first call of a level (+ the byte 0x006a3d49 clear) counts the versus result in the pairs of counters at 0x0056b798.. (mode 8: the two skaters' +0x2cdc compared, the signed pair 0x0056b7b0 / 0x0056b7b4 steps
// away from zero; mode 9 with 0x0056db64 == 2: the byte 0x006a3d48 picks the side), then, while the latch 0x00568658 is clear, arms the event (latch 1, countdown 0x50, step 0x40, the second skater's +0x3144 cleared).
void WorldKit::level_event_dispatch() {
    constexpr Word kMode = 0x00533f38, kSkaters = 0x0056a858, kScoreB = 0x0056b798, kScoreA = 0x0056b79c, kBestB = 0x0056b7a0, kBestA = 0x0056b7a4, kPairB = 0x0056b7b0, kPairA = 0x0056b7b4;
    constexpr Word kCountB = 0x0056b7dc, kCountA = 0x0056b7e0, kLatch = 0x00568658, kCountdown = 0x00568818, kStep = 0x00568610, kInitialised = 0x006a3d49, kSide = 0x006a3d48, kVersus = 0x0056db64;
    engine(0x004dee50, {});
    engine(0x004ab9c0, {});
    const Word mode = global_word(m_, kMode);
    const Word second = global_word(m_, kSkaters + 4);
    const auto bump = [&](Word at) { set_word_at(m_, at, word_at(m_, at) + 1); };
    // the signed pair: the word `from` steps up (1 when it is negative) and the other becomes its negation
    const auto step_pair = [&](Word from, Word other) {
        const Word value = static_cast<std::int32_t>(word_at(m_, from)) < 0 ? 1u : word_at(m_, from) + 1;
        set_word_at(m_, from, value);
        set_word_at(m_, other, 0u - value);
    };
    if (mode == 8) {
        if ((global_byte(m_, kInitialised) & 0xff) == 0) {
            const Word primary = global_word(m_, kSkaters);
            const auto second_value = static_cast<std::int32_t>(word_at(m_, second + 0x2cdc));
            const auto primary_value = static_cast<std::int32_t>(static_cast<Word>(skater_view(m_, primary).field_2cdc()));
            if (second_value != primary_value) {
                if (second_value > primary_value) {
                    bump(kCountA);
                    bump(kScoreA);
                    step_pair(kPairA, kPairB);
                } else {
                    bump(kCountB);
                    bump(kScoreB);
                    step_pair(kPairB, kPairA);
                }
                if (primary_value > static_cast<std::int32_t>(global_word(m_, kBestA))) {
                    set_global_word(m_, kBestA, static_cast<Word>(primary_value));
                }
                if (second_value > static_cast<std::int32_t>(global_word(m_, kBestB))) {
                    set_global_word(m_, kBestB, static_cast<Word>(second_value));
                }
            }
        }
    } else if (mode == 9 && global_word(m_, kVersus) == 2 && (global_byte(m_, kInitialised) & 0xff) == 0) {
        if ((global_byte(m_, kSide) & 0xff) != 0) {
            bump(kCountA);
            bump(kScoreA);
            step_pair(kPairA, kPairB);
        } else {
            bump(kCountB);
            bump(kScoreB);
            step_pair(kPairB, kPairA);
        }
    }
    if (global_word(m_, kLatch) == 0) {
        set_global_word(m_, kLatch, 1);
        set_global_word(m_, kCountdown, 0x50);
        set_global_word(m_, kStep, 0x40);
        if (second != 0) {
            set_word_at(m_, second + 0x3144, 0);
        }
    }
    set_global_byte(m_, kInitialised, 1);
}

// 0x0049f4c0 (CPlatform__Bounce, thiscall, two arguments: the skater and its velocity, skater + 0x4c): the skater hit a bouncy prop (+0x178 bit 4) that is not yet flying (bit 5). The launch speed is the length of the
// skater's velocity without its y (a stack local in retail; kScratch + 0x260 here). The prop starts flying: state +4 and flags take bit 5, the bounce count (+0x1f0) is cleared, and the target angles (+0x1fc: three words)
// are rolled with Rnd(0x1000) (+0x1fe) and, for +0x1fc and +0x200, Rnd(2) * 0x800 + the prop's angle (+0x14, +0x18) - 0x400. Kinds 1 and 3 (+0x1f2): a sound (0x68, 0x3c), the skater's grunt and a rumble (6, 1, 0x78); the
// prop flies off with the skater's x and z and -0x20000 (speed below 0x28) or -0x40000; below speed 0x12 the angles are rolled again about the prop's own angles. Kinds 2, 4 and 5: the sound of 2 (0x6a) or the grunt and
// sound of 4 (0x52), a rumble (2, 1, 0xb4), the velocity one and a half times the skater's x and z with y -0x50000, and the target angles' +-0x800 special cases against 0 (the pitch takes the prop's own angle when the yaw
// is +-0x800 and the roll 0, the yaw when the roll is +-0x800 and the yaw 0). Any other kind asserts (platform.cpp line 0xe4) and flies off like kind 1 at -0x40000.
void WorldKit::platform_bounce(Word self, Word skater, Word velocity) {
    constexpr Word kVector = kScratch + 0x260, kPlaySound = 0x004ad620, kGrunt = 0x0046e1d0, kRumble = 0x0048ffc0, kMessage = 0x00536f70;
    const NativeRoot native_root(core_);
    const auto iabs = [](std::int32_t v) { return v < 0 ? -v : v; };
    const Word flags = word_at(m_, self + 0x178);
    if ((flags & 0x10u) == 0 || (flags & 0x20u) != 0) {
        return;
    }
    set_global_word(m_, kVector, word_at(m_, velocity));
    set_global_word(m_, kVector + 4, 0);
    set_global_word(m_, kVector + 8, word_at(m_, velocity + 8));
    const std::int32_t speed = s(core_.vector_length(kVector));
    set_half_at(m_, self + 4, half_at(m_, self + 4) | 0x20u);
    set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) | 0x20u);
    platform_at(m_, self).set_field_1f0(0);
    platform_at(m_, self).set_field_1fe(static_cast<std::uint16_t>(u(rnd(0x1000))));
    platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>(((u(rnd(2)) << 11) + half_at(m_, self + 0x14) - 0x400) & 0xffffu));
    platform_at(m_, self).set_field_200(static_cast<std::uint16_t>(((u(rnd(2)) << 11) + platform_at(m_, self).field_18() - 0x400) & 0xffffu));
    const Word kind = platform_at(m_, self).field_1f2();
    switch (kind) {
    case 1:
    case 3: {
        engine(kPlaySound, {kind == 1 ? 0x68u : 0x3cu, 0x2000, 0});
        engine(kGrunt, {skater});
        thiscall(kRumble, skater, {6, 1, 0x78});
        const Word launch = speed < 0x28 ? 0xfffe0000u : 0xfffc0000u;
        if (speed < 0x12) {
            platform_at(m_, self).set_field_1fe(static_cast<std::uint16_t>(u(rnd(0x400)) + ((half_at(m_, self + 0x16) - 0x200) & 0xffffu)));
            platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>((half_at(m_, self + 0x14) + 0x200) & 0xc00u));
            platform_at(m_, self).set_field_200(static_cast<std::uint16_t>((platform_at(m_, self).field_18() + 0x200) & 0xc00u));
        }
        const Word x = word_at(m_, velocity), z = word_at(m_, velocity + 8);
        platform_at(m_, self).set_field_50(static_cast<Word>(launch));
        platform_at(m_, self).set_field_4c(static_cast<Word>(x));
        platform_at(m_, self).set_field_54(static_cast<Word>(z));
        return;
    }
    case 2:
    case 4:
    case 5: {
        if (kind == 2) {
            engine(kPlaySound, {0x6a, 0x2000, 0});
        } else if (kind == 4) {
            engine(kGrunt, {skater});
            engine(kPlaySound, {0x52, 0x2000, 0});
        }
        thiscall(kRumble, skater, {2, 1, 0xb4});
        const Word x = word_at(m_, velocity), z = word_at(m_, velocity + 8);
        platform_at(m_, self).set_field_50(0xfffb0000u);
        platform_at(m_, self).set_field_54(static_cast<Word>(u(s(z) >> 1) + z));
        platform_at(m_, self).set_field_4c(static_cast<Word>(u(s(x) >> 1) + x));
        const Word yaw = platform_at(m_, self).field_1fc();
        if (iabs(sx16i(yaw)) == 0x800 && platform_at(m_, self).field_200() == 0) {
            platform_at(m_, self).set_field_200(static_cast<std::uint16_t>(platform_at(m_, self).field_18()));
        }
        if (yaw == 0 && iabs(sx16i(platform_at(m_, self).field_200())) == 0x800) {
            platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>(half_at(m_, self + 0x14)));
        }
        return;
    }
    default: {
        set_global_word(m_, 0x00568638, 0xe4);
        set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\platform.cpp");        // (the string at 0x00536f90)
        check(false, kMessage);
        const Word x = word_at(m_, velocity), z = word_at(m_, velocity + 8);
        platform_at(m_, self).set_field_4c(static_cast<Word>(x));
        platform_at(m_, self).set_field_50(0xfffc0000u);
        platform_at(m_, self).set_field_54(static_cast<Word>(z));
        return;
    }
    }
}

// 0x0049f780 (CPlatform__BouncyAI, thiscall): a thrown platform (+0x178 bit 5) tumbles towards a target orientation (+0x1fc: three angles; the spin +0x70 is a thirty-second of the way per frame, at least 0x28 a frame
// while far off) and flies along its velocity (+0x4c) with a drag: each frame its path (position to position + velocity with the weight of +0x8000 added) is tested against the zone, and when it hits something (or,
// falling, a vertical probe through the end point finds ground) the velocity is mirrored about the hit normal and decays, the end point is moved off the surface by the radius (from the matrix row 1 weights
// +0x1f4..+0x1f8), a bounce sound is played (the first five bounces, by the bounce kind +0x1f2), and the target angles are re-rolled on the first bounce; at most three bounces a frame. After 0x12 bounces it settles on
// the target angles, loses the flying bit and starts a ten frame death timer (+0x1fa); at 0 the timer runs slot 2 (die).
void WorldKit::platform_bouncy_ai(Word self) {
    const NativeRoot native_root(core_);
    constexpr Word kQuery = kScratch + 0x340, kV = kScratch + 0x240, kCounts = kScratch + 0x250, kH = kScratch + 0x260, kN = kScratch + 0x270, kM = kScratch + 0x290, kEnd = kScratch + 0x2b0;
    constexpr Word kInitQuery = 0x004624d0, kZoneQuery = 0x00466090, kPlaySound = 0x004ad620;
    constexpr Word kBody = 0x68, kHit = 0x6c, kNormal = 0x78;
    const auto iabs = [](std::int32_t v) { return v < 0 ? -v : v; };
    Word timer = platform_at(m_, self).field_1fa();
    if (timer != 0) {
        timer = (timer - 1) & 0xffffu;
        platform_at(m_, self).set_field_1fa(static_cast<std::uint16_t>(timer));
        if (timer == 0) {
            virtual_call(self, 2);
            return;
        }
    }
    if ((byte_at(m_, self + 0x178) & 0x20u) == 0) {
        return;
    }
    core_.vector_mask12(self + 0x1fc);
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 0x70 + 2 * k, u((sx16i(half_at(m_, self + 0x1fc + 2 * k)) - sx16i(half_at(m_, self + 0x14 + 2 * k))) >> 5));
    }
    vec3s_snap_near_zero(self + 0x70);
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t spin = sx16i(half_at(m_, self + 0x70 + 2 * k));
        const std::int32_t off = sx16i(half_at(m_, self + 0x1fc + 2 * k)) - sx16i(half_at(m_, self + 0x14 + 2 * k));
        if (iabs(spin) < 0x28 && iabs(off) > 0x28) {
            set_half_at(m_, self + 0x70 + 2 * k, spin >= 0 ? 0x28 : 0xffd8);
        }
    }
    vec3s_add(self + 0x14, self + 0x70);
    vec3s_mask12(self + 0x14);
    math_.rotation_from_angles(self + 0x14, kM);
    const Word weights = static_cast<Word>(iabs(sx16i(global_half(m_, kM + 6)))) * platform_at(m_, self).field_1f4() + static_cast<Word>(iabs(sx16i(global_half(m_, kM + 8)))) * platform_at(m_, self).field_1f6()
        + static_cast<Word>(iabs(sx16i(global_half(m_, kM + 10)))) * platform_at(m_, self).field_1f8();
    const Word radius = u(s(weights) >> 1);
    set_global_word(m_, kV, platform_at(m_, self).field_4c());
    set_global_word(m_, kV + 4, platform_at(m_, self).field_50() + 0x8000);
    set_global_word(m_, kV + 8, platform_at(m_, self).field_54());
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, kCounts + k, 6);
    }
    vec3_decay_shift(kV, kCounts);
    core_.vector_dead_zone(kV);
    core_.vector_add_out(kEnd, self + 0x8, kV);
    Word end[3] = {global_word(m_, kEnd), global_word(m_, kEnd + 4), global_word(m_, kEnd + 8)};
    set_global_word(m_, kH, global_word(m_, kV));
    set_global_word(m_, kH + 4, 0);
    set_global_word(m_, kH + 8, global_word(m_, kV + 8));
    const std::int32_t horizontal = s(core_.vector_length(kH));
    const auto query = [&] {
        engine(kInitQuery, {kQuery});
        std::string line;                                   // (the line the ray runs along, recorded with the call)
        for (Word k = 0; k < 0x18; ++k) {
            line.push_back(static_cast<char>(byte_at(m_, kQuery + k)));
        }
        engine(kZoneQuery, {kQuery, 1}, line);
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kZoneQuery);        // (the host's answers: the body hit, the hit point and the normal)
            set_global_word(m_, kQuery + kBody, static_cast<Word>(s_.value(name)));
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kQuery + kHit + 4 * k, static_cast<Word>(s_.value(name)));
            }
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, kQuery + kNormal + 2 * k, static_cast<Word>(s_.value(name)));
            }
        }
    };
    const auto commit = [&] {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 8 + 4 * k, end[k]);
            set_word_at(m_, self + 0x4c + 4 * k, word_at(m_, kV + 4 * k));
        }
    };
    for (Word pass = 0; pass < 3; ++pass) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kQuery + 4 * k, word_at(m_, self + 8 + 4 * k));
            set_word_at(m_, kQuery + 0xc + 4 * k, end[k]);
        }
        query();
        if (global_word(m_, kQuery + kBody) == 0) {
            if (s(global_word(m_, kV + 4)) <= 0) {
                commit();
                break;
            }
            set_global_word(m_, kQuery, end[0]);
            set_global_word(m_, kQuery + 4, end[1] - radius);
            set_global_word(m_, kQuery + 8, end[2]);
            set_global_word(m_, kQuery + 0xc, end[0]);
            set_global_word(m_, kQuery + 0x10, end[1] + radius);
            set_global_word(m_, kQuery + 0x14, end[2]);
            query();
            if (global_word(m_, kQuery + kBody) == 0) {
                commit();
                break;
            }
        }
        if (platform_at(m_, self).field_1f0() < 5) {
            const Word kind = platform_at(m_, self).field_1f2() - 1;
            Word sound = 0;
            bool play = true;
            switch (kind) {
            case 0: sound = rnd(2) != 0 ? 0x66 : 0x67; break;
            case 1: sound = rnd(2) != 0 ? 0x6a : 0x6b; break;
            case 2: sound = 0x45; break;
            case 3: sound = rnd(2) != 0 ? 0x50 : 0x51; break;
            case 4: sound = u(rnd(2)) + 0x53; break;
            default: play = false; break;
            }
            if (play) {
                engine(kPlaySound, {sound, 0x2000, 0});
            }
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kN + 4 * k, u(sx16i(half_at(m_, kQuery + kNormal + 2 * k))));
        }
        platform_at(m_, self).set_field_1f0(static_cast<std::uint16_t>(platform_at(m_, self).field_1f0() + 1));
        const std::int32_t dot = math_.dot_q12(kV, kN);
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t part = math_.mul_q12(s(word_at(m_, kN + 4 * k)), dot);
            set_word_at(m_, kV + 4 * k, word_at(m_, kV + 4 * k) - (u(part) << 1));
        }
        const Word vy = global_word(m_, kV + 4);
        const Word magnitude = (vy ^ u(s(vy) >> 31)) - u(s(vy) >> 31);
        if (s(magnitude) < 0xc000) {
            set_global_byte(m_, kCounts, 2);
            set_global_byte(m_, kCounts + 1, 0);
            set_global_byte(m_, kCounts + 2, 2);
        } else {
            set_global_byte(m_, kCounts, 3);
            set_global_byte(m_, kCounts + 1, 2);
            set_global_byte(m_, kCounts + 2, 3);
            platform_at(m_, self).set_field_1fe(static_cast<std::uint16_t>(u(rnd(0x1000))));
            if (platform_at(m_, self).field_1f0() == 1) {
                platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>(platform_at(m_, self).field_1fc() + ((u(rnd(2)) << 11) - 0x400)));
                const Word first = platform_at(m_, self).field_1f2(), roll = platform_at(m_, self).field_1fc();
                if (first == 2 || first == 4) {
                    if ((roll == 0x800 && platform_at(m_, self).field_200() == 0) || (roll == 0 && platform_at(m_, self).field_200() == 0x800)) {
                        platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>((u(rnd(2)) << 11) - 0x400));
                    }
                }
                platform_at(m_, self).set_field_200(static_cast<std::uint16_t>(platform_at(m_, self).field_200() + ((u(rnd(3)) << 10) - 0x400)));
                const Word second = platform_at(m_, self).field_1f2(), yaw = platform_at(m_, self).field_1fc(), pitch = platform_at(m_, self).field_200();
                if (second == 2 || second == 4) {
                    if ((iabs(sx16i(yaw)) == 0x800 && pitch == 0) || (yaw == 0 && iabs(sx16i(pitch)) == 0x800)) {
                        platform_at(m_, self).set_field_200(static_cast<std::uint16_t>((u(rnd(2)) << 11) - 0x400));
                    }
                }
                if (horizontal < 0x12) {
                    platform_at(m_, self).set_field_1fe(static_cast<std::uint16_t>(u(rnd(0x400)) + ((half_at(m_, self + 0x16) - 0x200) & 0xffffu)));
                    platform_at(m_, self).set_field_1fc(static_cast<std::uint16_t>((half_at(m_, self + 0x14) + 0x200) & 0xc00u));
                    platform_at(m_, self).set_field_200(static_cast<std::uint16_t>((platform_at(m_, self).field_18() + 0x200) & 0xc00u));
                }
            }
        }
        vec3_decay_shift(kV, kCounts);
        core_.vector_dead_zone(kV);
        if (platform_at(m_, self).field_1f0() > 0x12) {
            set_half_at(m_, self + 0x14, platform_at(m_, self).field_1fc());
            platform_at(m_, self).set_field_18(static_cast<std::uint16_t>(platform_at(m_, self).field_200()));
            math_.rotation_from_angles(self + 0x14, kM);
            platform_at(m_, self).set_field_1fa(0xa);
            set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) & 0xffffffdfu);
            return;
        }
        for (Word k = 0; k < 3; ++k) {
            end[k] = u(math_.mul_q12(sx16i(half_at(m_, kQuery + kNormal + 2 * k)), s(radius))) + word_at(m_, kQuery + kHit + 4 * k);
        }
    }
    if (platform_at(m_, self).field_1f0() > 0x12) {
        set_half_at(m_, self + 0x14, platform_at(m_, self).field_1fc());
        platform_at(m_, self).set_field_18(static_cast<std::uint16_t>(platform_at(m_, self).field_200()));
        platform_at(m_, self).set_field_1fa(0xa);
        set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) & 0xffffffdfu);
    }
}

// 0x00401520 (TRG_RunScriptStream, thiscall, one argument): runs a script stream from `stream` (kept at +0x17c) until its 0x4100 terminator: each command word is asserted to have one of the two class bits (0x2000
// field/variable commands, 0x4000 object commands), and handed to the object's slot 7 (0x4000: a result of 0 stops the run, the mode byte +0x16c stays) or slot 8 (0x2000); at the terminator the mode byte is cleared
void WorldKit::trg_run_script_stream(Word self, Word stream) {
    set_word_at(m_, self + 0x17c, stream);
    if (half_at(m_, stream) != 0x4100) {
        do {
            const Word at = word_at(m_, self + 0x17c);
            const Word command = half_at(m_, at);
            set_word_at(m_, self + 0x17c, at + 2);
            set_global_word(m_, 0x00568638, 0x232);
            set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\baddy.cpp");
            check((command & 0x6000u) != 0, 0x005241f8);
            if ((command & 0x4000u) != 0) {
                if (thiscall_value(word_at(m_, word_at(m_, self) + 0x1c), self, {command}) == 0) {
                    return;
                }
            } else if ((command & 0x2000u) != 0) {
                thiscall(word_at(m_, word_at(m_, self) + 0x20), self, {command});
            }
        } while (half_at(m_, word_at(m_, self + 0x17c)) != 0x4100);
    }
    set_byte_at(m_, self + 0x16c, 0);
}

// ---- the power-up ----

// 0x004a81d0 (thiscall)
Word WorldKit::powerup_scalar_delete(Word self, Word flags) {
    powerup_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x004a81f0
void WorldKit::powerup_destruct(Word self) {
    powerup_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(kPowerupVtable)});
    thiscall(kListRemove, self, {kPowerupList});
    powerup_release(self);
    object_destruct(self);
}

// 0x004a8250: the power-up is shown (+0x41) and its own kill method (slot 1) runs; the answer is the method's
Word WorldKit::powerup_kill(Word self) {
    set_byte_at(m_, self + object::kFlags, byte_at(m_, self + object::kFlags) | 0x41);
    return virtual_call(self, 1);
}

// 0x004a8260 (thiscall, two arguments)
void WorldKit::powerup_set_fields(Word self, Word value, Word flag) {
    powerup_at(m_, self).set_field_5c(static_cast<Word>(value));
    powerup_at(m_, self).set_field_6d(static_cast<std::uint8_t>(flag));
}

// 0x004a8280
void WorldKit::powerup_allow_sprite() { set_global_word(m_, 0x005244cc, 1); }

// 0x004a8290: the sprite is released and the child object (the glow) deleted
void WorldKit::powerup_release(Word self) {
    release_sprite(self);
    const Word child = powerup_at(m_, self).glow_runtime_object().address;
    if (child != 0) {
        virtual_call(child, 0, {1});
    }
    powerup_at(m_, self).set_glow_runtime_object(GuestPtr<void>{0});
}

// 0x004a82c0 (thiscall, one argument): the node the power-up was made from
void WorldKit::powerup_set_node(Word self, Word node) {
    powerup_at(m_, self).set_field_d0(1);
    powerup_at(m_, self).set_field_d6(static_cast<std::uint16_t>(node));
}

// 0x004a82e0
void WorldKit::powerup_mark_dying(Word self) {
    if (is_dying(self) == 0) {
        set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 0x40);
        powerup_kill(self);
    }
}


// 0x004013b0 (CBaddy__Die, thiscall; the base class's slot 2): an object that is not dying yet sends its death pulse and is marked dead and hidden (+0x6a bit 6, +0x04 bits 0 and 6)
void WorldKit::baddy_die(Word self) {
    if (is_dying(self) != 0) {
        return;
    }
    baddy_send_death_pulse(self);
    baddy_at(m_, self).set_state(static_cast<std::uint8_t>(baddy_at(m_, self).state() | 0x40));
    set_byte_at(m_, self + 0x04, byte_at(m_, self + 0x04) | 0x41);
}

// 0x004013d0 (CBaddy__SendDeathPulse, thiscall): once (+0x171), the link list of the node the object came from (+0xb0, 0xffff for none) is run
void WorldKit::baddy_send_death_pulse(Word self) {
    if (baddy_at(m_, self).field_171() != 0) {
        return;
    }
    const Word node = baddy_at(m_, self).field_b0();
    if (node == 0xffff) {
        return;
    }
    baddy_at(m_, self).set_field_171(1);
    engine(0x004c5b00, {word_at(m_, global_word(m_, 0x0056e210) + 4 * node) + 6});          // TRG_ExecuteCommandList(the record + 6)
}

// 0x004a86f0 (CPowerUp__TakeEffect, thiscall, one argument: the skater that touched the power-up): refused while the power-up is hidden or dying for that skater (bit 0 of +0x04, or bit 6 for the
// player the camera follows); the skate letters (types 4, 5, 6, 10, 15: the pickup sound, then the panel's letter and, when it is not a repeat, the message of the letter), the level's goal pickup (16)
// and the money / stat pickups (24, 25, 26 and the level pickup 33: the skater's take-off pose, the sound with its pitch and the award) kill the power-up afterwards (slot 2); the node links run on
// any outcome. Any other type is the assertion at line 0x355.
void WorldKit::powerup_take_effect(Word self, Word skater) {
    constexpr Word kMode = 0x00533f3c, kTarget = 0x0056a960, kPlaySound = 0x004ad620, kMessage = 0x004736f0, kLetter = 0x0048d7c0, kVoice = 0x004ad890, kMoney = 0x00415180;
    const Word flags = byte_at(m_, self + 4);
    if (global_word(m_, kMode) == 1) {
        if ((flags & 1u) != 0) {
            return;
        }
    } else if (skater == global_word(m_, kTarget)) {
        if ((flags & 0x40u) != 0) {
            return;
        }
    } else if ((flags & 1u) != 0) {
        return;
    }
    bool kill = false;
    const Word type = powerup_at(m_, self).item_subtype();
    const auto letter = [&](Word number, Word table) {
        engine(kPlaySound, {0x14, 0x2000, 0});
        if (engine_value(kLetter, {number}) != 0) {
            return;
        }
        const Word text = word_at(m_, table);
        if (text != 0) {
            engine(kMessage, {text, 0x00534ec8, 1, 0, 0});
        }
    };
    switch (type) {
    case 4: case 5: case 6: case 10: case 14: case 15: case 16:
        kill = true;
        engine(0x00473650);                                                 // Mess_DeleteAll
        switch (type) {
        case 4: letter(1, 0x0052ea04); break;
        case 5: letter(0, 0x0052ea00); break;
        case 6: letter(2, 0x0052ea08); break;
        case 10: letter(4, 0x0052ea10); break;
        case 15: letter(3, 0x0052ea0c); break;
        case 16: engine(0x00414570, {6}); break;                            // Career_GiveGoalType
        default: break;
        }
        break;
    case 24: case 25: case 26: case 33:
        if (skater_view(m_, skater).field_2f64() != 0) {
            break;
        }
        kill = true;
        if (skater_view(m_, skater).physics_state() == 0) {
            thiscall(0x0048f5f0, skater);                                   // Trick_TakeOff
        }
        {
            const Word sound = engine_value(kPlaySound, {type == 0x21 ? 0x13cu : 0x12u, 0x2000, 0});
            if (type == 0x21) {
                engine(0x004152f0);                                         // Career_GetLevelPickup
                engine(kVoice, {sound, 0x45a});
            } else {
                const Word row = type - 24;
                const Word texts[3] = {0x0052e424, 0x0052e428, 0x0052e42c};
                const Word pitches[3] = {0x384, 0x45a, 0x5dc};
                const Word amounts[3] = {0xfa, 0x32, 0x64};
                engine(kMessage, {word_at(m_, texts[row]), 0x00534f68, 1, 0, 0});
                engine(kVoice, {sound, pitches[row]});
                engine(kMoney, {powerup_at(m_, self).source_trg_node_index(), amounts[row]});
            }
        }
        if (skater_view(m_, skater).physics_state() == 0) {
            skater_view(m_, skater).set_field_306c(0);
            skater_view(m_, skater).set_field_3068(0);
            thiscall(0x0048f8f0, skater);                                   // Trick_Land
        }
        break;
    default:
        set_global_word(m_, 0x00568638, 0x355);
        set_string(0x0056881c, m_.cstring(0x005373ec, 256));
        check(false, 0x00537408);
        break;
    }
    if (powerup_at(m_, self).field_d0() != 0) {
        engine(0x004c5b00, {engine_value(0x004c8550, {powerup_at(m_, self).field_d6()})});   // TRG_ExecuteCommandList(TRG_GetNodeLinks(node))
    }
    if (kill) {
        virtual_call(self, 2);
    }
}

// 0x004a8620 (CPowerUp__CheckAge, thiscall): counts the power-up's age (+0xfc) and its timer (+0xf0; 0xffff never runs out) down; below 60 frames the pulse amplitude (+0xec) shrinks with the timer, and the
// power-up blinks (the visible bit of +0x04 cleared on a counter bit, the kill method 0x004a8250 otherwise) in the last 60 and the last 30 frames; at 0 its own slot 2 (die) runs
void WorldKit::powerup_check_age(Word self) {
    powerup_at(m_, self).set_field_fc(static_cast<Word>(powerup_at(m_, self).field_fc() + 1));
    Word timer = powerup_at(m_, self).field_f0();
    if (timer == 0xffffu) {
        return;
    }
    if (timer != 0) {
        --timer;
        powerup_at(m_, self).set_field_f0(static_cast<std::uint16_t>(timer));
    }
    const auto show = [&] { set_half_at(m_, self + object::kFlags, half_at(m_, self + object::kFlags) & 0xfffeu); };
    if (timer < 0x3c) {
        powerup_at(m_, self).set_field_ec(static_cast<std::uint16_t>((powerup_at(m_, self).field_ea() * timer) / 0x3c));
        if (timer > 0x1e) {
            if ((global_byte(m_, 0x0056a8d4) & 2u) != 0) {
                show();
            } else {
                powerup_kill(self);
            }
        }
    }
    if (powerup_at(m_, self).field_f0() <= 0x1e) {
        if ((global_byte(m_, 0x0056a8d4) & 1u) != 0) {
            show();
        } else {
            powerup_kill(self);
        }
    }
    if (powerup_at(m_, self).field_f0() == 0) {
        virtual_call(self, 2);
    }
}

// 0x004a8300 (CPowerUp__DoPhysics, thiscall): spins the three packed angles (+0x14, masked to 12 bits), then moves the power-up: hovering (+0xd2 == 0) it bobs on a sine of its phase (+0xe8) about its home (+0xdc);
// waiting for ground (+0xd3 == 0, +0xd8 in -4..-1) it looks for the floor under it on every other frame; otherwise it falls with drag from its velocity (+0x4c) and stops on the floor. A camera-following power-up
// (+0xf6 != 0) is placed in front of the camera through the GTE. The shadow (+0x90 position on the floor, +0xa8 scale, +0xaa height) follows.
void WorldKit::powerup_do_physics(Word self) {
    constexpr Word kGteRotation = 0x006a3e10, kGteV0 = 0x006a3e58, kGteMac = 0x006a3eb8, kCamera = 0x005620e0, kAnimScale = 0x0056865c, kTick = 0x0056a8d4;
    const auto sx16 = [](Word v) { return static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>(v))); };
    const Word scale = global_word(m_, kAnimScale);
    for (Word k = 0; k < 3; ++k) {
        const Word step = static_cast<Word>(s(sx16(half_at(m_, self + 0x70 + 2 * k)) * scale) >> 8);
        set_half_at(m_, self + 0x14 + 2 * k, half_at(m_, self + 0x14 + 2 * k) + step);
    }
    core_.vector_mask12(self + 0x14);
    if (powerup_at(m_, self).motion_state() == 0) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 0xdc + 4 * k));
        }
        const Word product = static_cast<Word>(math_.sin_q12(static_cast<std::int32_t>(powerup_at(m_, self).field_e8() & 0xfffu))) * powerup_at(m_, self).field_ec();
        const Word rounded = product + (static_cast<Word>(s(product) >> 31) & 0xfffu);
        powerup_at(m_, self).set_position_y_fixed(static_cast<std::int32_t>(static_cast<Word>(powerup_at(m_, self).position_y_fixed()) + (static_cast<Word>(s(rounded) >> 12) << 12)));
        powerup_at(m_, self).set_field_e8(static_cast<std::uint16_t>(powerup_at(m_, self).field_e8() + static_cast<Word>(s(powerup_at(m_, self).field_ee() * scale) >> 8)));
    } else if (powerup_at(m_, self).field_d3() == 0 && s(powerup_at(m_, self).field_d8()) < 0 && s(powerup_at(m_, self).field_d8()) > -5) {
        if ((global_byte(m_, kTick) & 1u) != 0) {
            const Word found = engine_value(0x004ca140, {self + 0xdc, 0, 0x1f40});
            if (found == 0xffffffffu) {
                const Word below = powerup_at(m_, self).field_d8() - 1;
                powerup_at(m_, self).set_field_d8(static_cast<Word>(below));
                if (s(below) <= -5) {
                    powerup_at(m_, self).set_field_d3(1);
                }
            } else {
                powerup_at(m_, self).set_field_d8(static_cast<Word>(found));
            }
        }
    } else {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0xdc + 4 * k, word_at(m_, self + 0xdc + 4 * k) + word_at(m_, self + 0x4c + 4 * k));
        }
        Word vy = powerup_at(m_, self).field_50() + powerup_at(m_, self).field_5c();
        vy -= static_cast<Word>(s(vy) >> (powerup_at(m_, self).field_6d() & 31u));
        powerup_at(m_, self).set_field_50(static_cast<Word>(vy));
        if (powerup_at(m_, self).field_d3() == 0) {
            const Word limit = powerup_at(m_, self).field_d8() - (powerup_at(m_, self).field_d4() << 12);
            if (s(powerup_at(m_, self).field_e0()) > s(limit) && s(vy) > 0) {
                powerup_at(m_, self).set_field_e0(static_cast<Word>(limit));
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, self + 0x4c + 4 * k, 0);
                }
                powerup_at(m_, self).set_motion_state(0);
            }
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 0xdc + 4 * k));
        }
    }
    const Word follow = powerup_at(m_, self).field_f6();
    if (follow != 0) {
        const Word camera = global_word(m_, kCamera);
        for (Word k = 0; k < 9; ++k) {
            set_half_at(m_, kGteRotation + 2 * k, half_at(m_, camera + 0x34 + 2 * k));
        }
        set_global_half(m_, kGteV0, powerup_at(m_, self).field_f2());
        set_global_half(m_, kGteV0 + 2, powerup_at(m_, self).field_f4());
        set_global_half(m_, kGteV0 + 4, follow);
        math_.mvmva(0, 0);
        for (Word k = 0; k < 3; ++k) {
            const Word place = (word_at(m_, global_word(m_, kCamera) + 4 + 4 * k) << 12) + word_at(m_, kGteMac + 4 * k);
            set_word_at(m_, self + 8 + 4 * k, place);
            set_word_at(m_, self + 0xdc + 4 * k, place);
        }
    }
    powerup_at(m_, self).set_field_90(static_cast<Word>(powerup_at(m_, self).field_dc()));
    powerup_at(m_, self).set_field_94(static_cast<Word>(powerup_at(m_, self).field_d8()));
    powerup_at(m_, self).set_field_98(static_cast<Word>(powerup_at(m_, self).field_e4()));
    const Word height = static_cast<Word>(s(powerup_at(m_, self).field_d8() - static_cast<Word>(powerup_at(m_, self).position_y_fixed())) >> 12) & 0xffffu;
    powerup_at(m_, self).set_field_aa(static_cast<std::uint16_t>(height));
    if (height == 0) {
        powerup_at(m_, self).set_field_a8(0);
    } else {
        const Word scaled = sx16(height) * 60u;
        const Word rounded = scaled + (static_cast<Word>(s(scaled) >> 31) & 0x7fu);
        powerup_at(m_, self).set_field_a8(static_cast<std::uint16_t>(0x8cu - static_cast<Word>(s(rounded) >> 7)));
    }
}

// 0x004a8ac0 (Powerup_Update, vtable slot 3): the age check, then, by the distance to the skater, either the glow tail (0x004a8b4c: within the update range) or the power-up's kill method (slot 1)
// when it is out of range; a power-up already visible and moving (flags 0x41) is left alone in a game of two. The range is the level's fog far distance (0x00563a3c), stretched by
// the player's fog distance (item_reach) as the far plane is, so cash, letters and goal items are drawn as far out as the level is.
void WorldKit::powerup_update(Word self) {
    thiscall(0x004a8620, self);                            // CPowerUp__CheckAge (a service here: the wpg fixture records it; its own form is powerup_check_age)
    const auto range = [this] { return item_reach(global_word(m_, 0x00563a3c)); };   // read where the game reads it
    Word distance;
    if (global_word(m_, 0x00533f3c) == 1) {
        if ((byte_at(m_, self + object::kFlags) & 1u) != 0) {
            return;
        }
        distance = powerup_at(m_, self).field_b8();
    } else {
        if ((byte_at(m_, self + object::kFlags) & 0x41u) == 0x41u) {
            return;
        }
        distance = powerup_at(m_, self).field_b8();
        if (distance > range()) {
            const Word second = word_at(m_, global_word(m_, 0x0056a960) + object::kNext);
            distance = core_.crap_distance_of(second + 8, self + 8);
        }
    }
    if (distance <= range()) {
        powerup_attach_glow(self);
        return;
    }
    virtual_call(self, 1);
}

// 0x004a8b4c (the tail block of Powerup_Update, which jumps into it with the power-up in ebp): the glow object of a power-up: made on first use (a sprite of 0x5c bytes) and given the colours of the power-up's type, then placed at the power-up and
// pulsed with the sine of the frame counter
void WorldKit::powerup_attach_glow(Word self) {
    check(((byte_at(m_, self + object::kState) >> 2) & 1u) == 0, 0x005348f4);
    thiscall(0x004a8300, self);                           // CPowerUp__DoPhysics
    if (powerup_at(m_, self).visual_active() == 0) {
        powerup_allow_sprite();
    }
    if (powerup_at(m_, self).glow_runtime_object().address == 0) {
        const Word block = engine_value(0x00403d30, {0x5c});
        Word made = 0;
        if (block != 0) {
            made = thiscall_value(0x004056c0, block, {4, 2});
        }
        powerup_at(m_, self).set_glow_runtime_object(GuestPtr<void>{static_cast<Word>(made)});
        set_byte_at(m_, made + 0x52, 1);
        const Word child = powerup_at(m_, self).glow_runtime_object().address;
        thiscall(0x00405b20, child, {0, 0, 0});
        thiscall(0x00405b50, child, {0, 0, 0});
        thiscall(0x00405b90, child, {1, 0, 0, 0});
        thiscall(0x00405a70, child, {0});
        set_byte_at(m_, powerup_at(m_, self).glow_runtime_object().address + 0x3a, 1);
        const Word type = powerup_at(m_, self).item_subtype() - 4u;
        static constexpr std::uint8_t kHandler[15] = {0, 0, 0, 3, 3, 3, 0, 3, 3, 3, 3, 0, 1, 3, 2};
        if (type <= 0xe && kHandler[type] != 3) {
            const Word glow = powerup_at(m_, self).glow_runtime_object().address;
            const Word count = word_at(m_, glow + 0x44);
            const Word handler = kHandler[type];
            for (Word part = 0; part < count; ++part) {
                const bool odd = (part & 1u) != 0;
                if (handler == 0) {
                    thiscall(0x00405c40, powerup_at(m_, self).glow_runtime_object().address, {part, 0, 0, odd ? 0x60u : 0u});
                } else if (odd) {
                    thiscall(0x00405c40, powerup_at(m_, self).glow_runtime_object().address, {part, 0x60, 0x60, 0});
                } else {
                    thiscall(0x00405c40, powerup_at(m_, self).glow_runtime_object().address, {part, 0x40, 0x40, 0x40});
                }
            }
            if (handler == 2) {
                thiscall(0x00405b50, powerup_at(m_, self).glow_runtime_object().address, {0x20, 0, 0});
                thiscall(0x00405b90, powerup_at(m_, self).glow_runtime_object().address, {1, 0, 0, 0});
                powerup_at(m_, self).set_field_cc(0xa);
                powerup_at(m_, self).set_field_ce(3);
            } else {
                thiscall(0x00405b50, powerup_at(m_, self).glow_runtime_object().address, {0, 0, 0});
                thiscall(0x00405b90, powerup_at(m_, self).glow_runtime_object().address, {1, 0, 0, 0});
                powerup_at(m_, self).set_field_cc(0x3c);
                powerup_at(m_, self).set_field_ce(0xa);
            }
        }
    }
    check(powerup_at(m_, self).glow_runtime_object().address != 0, 0x00537420);
    thiscall(0x00403ea0, powerup_at(m_, self).glow_runtime_object().address, {self + 8});
    const Word tick = global_word(m_, 0x0056a8d4);
    const Word sine = u(math_.sin_q12(s(tick * 150u)));
    const Word product = sine * powerup_at(m_, self).field_ce();
    const Word rounded = product + (static_cast<Word>(s(product) >> 31) & 0xfffu);
    const Word pulse = static_cast<Word>(s(rounded) >> 12) + powerup_at(m_, self).field_cc();
    thiscall(0x00405aa0, powerup_at(m_, self).glow_runtime_object().address, {0, pulse});
    thiscall(0x00405aa0, powerup_at(m_, self).glow_runtime_object().address, {1, pulse});
}

// ---- the traffic object ----

// 0x004128a0 (thiscall)
Word WorldKit::traffic_scalar_delete(Word self, Word flags) {
    traffic_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x004128c0
void WorldKit::traffic_destruct(Word self) {
    traffic_at(m_, self).set_vtable_or_class_dispatch(GuestPtr<void>{static_cast<Word>(kTrafficVtable)});
    set_global_word(m_, kTrafficCount, global_word(m_, kTrafficCount) - 1);
    thiscall(kListRemove, self, {kBaddyList});
    baddy_destruct(self);
}

}  // namespace opentony::runtime::worldkit
