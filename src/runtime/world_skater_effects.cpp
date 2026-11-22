#include "frame_pacing.hpp"
#include "world_views.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.15c: the skater's ground and light methods (`bruce.cpp`): the blood splat, the water splash, the ground probe under the skater and the shade of the face it stands on. The query of the
// collision zone, the effect objects (the bits of blood and the splash) and the free-memory test are services.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kRecordSplat = 0x004cb430;              // Replay_RecordSplat(skater, kind, count)
constexpr Word kBitAngles = 0x00406070;                // (cdecl) the direction of the next bits: a pointer to three shorts
constexpr Word kBitSpread = 0x00406090;                // (cdecl) their spread: a pointer to three shorts
constexpr Word kBitSpeed = 0x004061c0;                 // (cdecl, three arguments)
constexpr Word kBitLife = 0x004061e0;                  // (cdecl, three arguments)
constexpr Word kBitCount = 0x00406160;                 // (cdecl, one argument)
constexpr Word kBitConstruct = 0x00406200;             // thiscall on the new bit, four arguments: position, a, b, c
constexpr Word kHookPosition = 0x00464e90;             // M3D_GetHookPosition(out, object, hook)
constexpr Word kFreeMemory = 0x0046f2c0;               // Mem_GetFree(heap)
constexpr Word kBitAlloc = 0x00403d30;                 // the display's allocator
constexpr Word kPlaySound = 0x004ad620;
constexpr Word kExpConstruct = 0x00448990;             // Exp_Construct (thiscall, seven arguments)
constexpr Word kRotationVectors = 0x0049c7d0;          // CBruce__FRUVectorsFromRotMatrix (thiscall)

// the skater's frame (0x0046dd60, mech.cpp)
constexpr Word kFrozen = 0x00561c04;                   // dword: the world is frozen (pause, debug camera)
constexpr Word kIsRemotePlayer = 0x004de010;           // NetGame_IsRemotePlayer(skater): a bool in AL
constexpr Word kAnimationRegions = 0x0056d444;         // AnimationTablesByRegion: 0x44 bytes a region, the scale flag at +0x23
constexpr Word kInCompetitionIntro = 0x004113e0;       // CCamera__InCompetitionIntro (thiscall on the camera): a bool in AL
constexpr Word kCompetitionCamera = 0x0055fa38;        // the camera the intro test is asked of
constexpr Word kFrameClock = 0x005685f4;
constexpr Word kCurrentSkater = 0x0056a958;            // the skater whose frame runs (0 outside it)
constexpr Word kPlayerOne = 0x0056a858;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kMechFile = "H:\\TonyHawk\\Pc2\\mech.cpp";   // 0x005347ec
constexpr Word kVideoPlayback = 0x004cd750;            // Video_Playback(skater): the frame of a replay that plays back
constexpr Word kTempTransform = 0x0048ec00;            // CBruce__TempTransform (thiscall)
constexpr Word kWriteFrameChecked = 0x004cc0e0;        // Replay_WriteFrameChecked(skater)
constexpr Word kRestoreSavedPose = 0x0048f590;         // Skater_RestoreSavedPose (thiscall)
constexpr Word kInputProfilePrep = 0x0046d6e0;         // Skater_InputProfilePrep (thiscall)
constexpr Word kRemotePhysics = seam::net::FUN_004de6d0;   // the network player's frame (cdecl, no arguments)
constexpr Word kPhysicsFrame = 0x0049e680;             // Skater_PhysicsFrame (thiscall)
constexpr Word kHandleWater = 0x0046e080;              // CBruce__HandleWater (thiscall)
constexpr Word kLineToSphere = 0x00464200;             // M3dColij_LineToSphere(from, to, vector, list, 0, 0x1000): the object of the list the line meets, or 0
constexpr Word kBaddyList = 0x0055f6bc;
constexpr Word kPowerUpList = 0x0056b830;
constexpr Word kHitBaddy = 0x0046d790;                 // (thiscall on the skater, the object hit)
constexpr Word kNetSendHit = seam::net::FUN_004de790;  // (cdecl, the skater): the hit message of a network game
constexpr Word kCollectPowerUp = 0x004a86f0;           // (thiscall on the power-up, the skater)
constexpr Word kProbeGround = 0x0046d7a0;              // Skater_ProbeGround (thiscall)
constexpr Word kStepOffSound = 0x0049a1b0;             // (cdecl, the skater's sound slot): animation 0x5d past frame 15, once
constexpr Word kLandingSound = 0x0049a1e0;             // Skater_PlayLandingSound (cdecl, the sound slot): animation 0x5e past frame 15, once

// the fake locals of the port
constexpr Word kLocalSphere = kScratch + 0xe0;         // three dwords: the vector the two sphere tests share
constexpr Word kLocalAngles = kScratch + 0x20;         // three shorts
constexpr Word kLocalSpread = kScratch + 0x30;         // three shorts
constexpr Word kLocalHook = kScratch + 0x40;           // four shorts
constexpr Word kLocalOut = kScratch + 0x50;            // three dwords
constexpr Word kLocalPosition = kScratch + 0x60;       // three dwords
constexpr Word kLocalTriples = kScratch + 0x80;        // four (x, z, shade) triples, 12 bytes each
constexpr Word kLocalRatio = kScratch + 0xc0;          // numerator and denominator of the line intersection

std::string bytes_of(const RetailMemory& memory, Word at, Word count) {
    std::string out;
    for (Word k = 0; k < count; ++k) {
        out.push_back(static_cast<char>(byte_at(memory, at + k)));
    }
    return out;
}

}  // namespace

// 0x0046da70: the blood the skater asked for (kind at +0x2c58, count at +0x2c5c) is spawned as bits at the skater's hook, each falling at its own speed towards the ground under it
void WorldKit::blood_splat(Word self) {
    if (skater_view(m_, self).field_2c54() == 0) {
        return;
    }
    skater_view(m_, self).set_field_2c54(0);
    if (global_word(m_, 0x00533fa0) == 0) {
        return;
    }
    engine(kRecordSplat, {self, skater_view(m_, self).field_2c58(), skater_view(m_, self).field_2c5c()});
    if (s(global_word(m_, 0x0055f72c)) >= 0xfa) {
        return;
    }
    set_global_half(m_, kLocalAngles, 0xfc00);
    set_global_half(m_, kLocalAngles + 2, 0);
    set_global_half(m_, kLocalAngles + 4, 0);
    engine(kBitAngles, {kLocalAngles}, bytes_of(m_, kLocalAngles, 6));
    Word hook_index = 0xf;
    const Word kind = skater_view(m_, self).field_2c58();
    if (kind == 1) {
        set_global_half(m_, kLocalSpread, 0x640);
        set_global_half(m_, kLocalSpread + 2, 0x640);
        hook_index = 0;
    } else {
        set_global_half(m_, kLocalSpread, 0x4b0);
        set_global_half(m_, kLocalSpread + 2, 0x320);
    }
    set_global_half(m_, kLocalSpread + 4, 0);
    engine(kBitSpread, {kLocalSpread}, bytes_of(m_, kLocalSpread, 6));
    engine(kBitSpeed, {0x48, 0, 0});
    engine(kBitLife, {0, 0, 0});
    set_global_half(m_, kLocalHook, 0);
    set_global_half(m_, kLocalHook + 2, 0);
    set_global_half(m_, kLocalHook + 4, 0);
    set_global_half(m_, kLocalHook + 6, hook_index);
    engine(kHookPosition, {kLocalOut, self, kLocalHook}, bytes_of(m_, kLocalHook, 8));
    take_filled(kLocalOut, 12, pcplatform::engine_name(kHookPosition));
    const Word pool = global_word(m_, 0x0055f72c);
    const Word position_x = global_word(m_, kLocalOut);
    const Word position_y = global_word(m_, kLocalOut + 4);
    const Word position_z = global_word(m_, kLocalOut + 8);
    set_global_byte(m_, 0x005244e4, 0);
    set_global_word(m_, 0x005244cc, 0);
    std::int32_t count = static_cast<std::int32_t>(0xfa - pool);
    const std::int32_t wanted = s(skater_view(m_, self).field_2c5c());
    if (count > wanted) {
        count = wanted;
    }
    const Word start[3] = {position_x, position_y - 0x28000, position_z};
    const Word end[3] = {position_x, position_y + 0x320000, position_z};
    const Word body = query_line(start, end);
    const Word ground = (body != 0 ? global_word(m_, collision::kInfoScratch + collision::line::kHitY) : position_y) - 0x4000;
    set_global_word(m_, kLocalPosition, position_x);
    set_global_word(m_, kLocalPosition + 4, position_y - 0x1e000);
    set_global_word(m_, kLocalPosition + 8, position_z);
    Word speed = 0x2800;
    for (std::int32_t index = 0; index < count; ++index, speed += 0x100) {
        const Word free_memory = engine_value(kFreeMemory, {1});
        if (s(free_memory) < 0x2710) {
            return;
        }
        engine(kBitCount, {static_cast<Word>(index & 1) + 1});
        const Word block = engine_value(kBitAlloc, {0x50});
        Word bit = 0;
        if (block != 0) {
            bit = engine_value(kBitConstruct, {block, kLocalPosition, static_cast<Word>(index >> 2) + 0xe, speed, static_cast<Word>(index & 7) + 0x32}, bytes_of(m_, kLocalPosition, 12));
        }
        vec3_add(bit + 0x1c, self + 0x4c);
        set_word_at(m_, bit + 0x4c, ground);
    }
    set_global_word(m_, 0x005244cc, 1);
    set_global_byte(m_, 0x005244e4, 1);
}

// 0x0046e080: a skater that dips below the water level plays the splash once and, every few frames, throws a spray of drops from a circle around it
void WorldKit::handle_water(Word self) {
    if (global_word(m_, 0x00533f3c) == 2) {
        return;
    }
    Word level = global_word(m_, 0x00533f90);
    if (self != global_word(m_, 0x0056a960)) {
        level = global_word(m_, 0x00533f94);
    }
    if (s(word_at(m_, self + 0xc) + 0x19000) <= s(level)) {
        set_half_at(m_, self + object::kFlags, half_at(m_, self + object::kFlags) & 0xfff7u);
        return;
    }
    if ((byte_at(m_, self + object::kFlags) & 0x08) == 0) {
        engine(kPlaySound, {0x2b, 0x2000, 0});
    }
    set_byte_at(m_, self + object::kFlags, byte_at(m_, self + object::kFlags) | 0x08);
    if ((global_byte(m_, 0x0056a8d4) & 3) != 0 || !framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); })) {   // F1.5F: the counter repeats on the frames between 30 Hz frames
        return;
    }
    set_global_word(m_, kLocalPosition, word_at(m_, self + 8));
    set_global_word(m_, kLocalPosition + 4, level);
    set_global_word(m_, kLocalPosition + 8, word_at(m_, self + 0x10));
    const Word radius = static_cast<Word>(rnd(0x18)) + 0x10;
    const Word angle = static_cast<Word>(rnd(0x1000));
    set_global_word(m_, kLocalPosition, global_word(m_, kLocalPosition) + u(math_.sin_q12(s(angle))) * radius);
    set_global_word(m_, kLocalPosition + 8, global_word(m_, kLocalPosition + 8) + u(math_.cos_q12(s(angle))) * radius);
    const Word block = engine_value(kBitAlloc, {0x68});
    if (block == 0) {
        return;
    }
    const Word extra = static_cast<Word>(rnd(3)) + 4;
    const Word start = static_cast<Word>(rnd(0x18)) + 0x28;
    engine(kExpConstruct, {block, kLocalPosition, start, 0xdd, 0xee, 0xff, 0xc, extra}, bytes_of(m_, kLocalPosition, 12));
}

// 0x0046d7a0: the ground probe: a line straight down from the skater; if it hits a surface flat enough for the skater's state the hit (position, normal, slope) is kept and, in a one-player game,
// the light fade pair is set from the height of the hit
void WorldKit::probe_ground(Word self) {
    thiscall(kRotationVectors, self);
    const Word start[3] = {word_at(m_, self + 8), word_at(m_, self + 0xc), word_at(m_, self + 0x10)};
    const Word end[3] = {start[0], start[1] + 0xfa0000, start[2]};
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, collision::kInfoScratch + collision::line::kStart + 4 * k, start[k]);
        set_word_at(m_, collision::kInfoScratch + collision::line::kEnd + 4 * k, end[k]);
    }
    engine(collision::kInitCollisionQuery, {collision::kInfoScratch});
    engine(collision::kZoneCollision, {collision::kInfoScratch, 1});
    take_filled(collision::kInfoScratch + 0x40, 0x50, pcplatform::engine_name(collision::kZoneCollision));
    const Word state = skater_view(m_, self).physics_state();
    const std::int32_t slope = static_cast<std::int16_t>(global_half(m_, collision::kInfoScratch + collision::line::kSlope));
    std::int32_t threshold;
    if (state == 0) {
        threshold = -0x3e8;
    } else if (state == 1) {
        const std::int32_t difference = slope - static_cast<std::int16_t>(static_cast<std::uint16_t>(skater_view(m_, self).field_9e()));
        threshold = (difference < 0 ? -difference : difference) <= 0x3e8 ? -0x5dc : -0x1000;
    } else if (state == 2) {
        threshold = -0x1f4;
    } else {
        threshold = -0x3e8;
    }
    bool take = global_word(m_, collision::kInfoScratch + collision::line::kHitBody) != 0 && slope < threshold && state != 4 && state != 8;
    if (take && state == 1 && s(global_word(m_, 0x005685f4) - skater_view(m_, self).field_2edc()) <= 6) {
        take = false;
    }
    if (!take) {
        skater_view(m_, self).set_draw_in_front(0);
        return;
    }
    set_word_at(m_, self + 0x90, global_word(m_, collision::kInfoScratch + 0x6c));
    set_word_at(m_, self + 0x94, global_word(m_, collision::kInfoScratch + 0x70));
    set_word_at(m_, self + 0x98, global_word(m_, collision::kInfoScratch + 0x74));
    skater_view(m_, self).set_field_9c(static_cast<std::int16_t>(global_half(m_, collision::kInfoScratch + 0x78)));
    skater_view(m_, self).set_field_9e(static_cast<std::int16_t>(slope));
    skater_view(m_, self).set_field_a0(static_cast<std::int16_t>(global_half(m_, collision::kInfoScratch + 0x7c)));
    if (global_word(m_, 0x00533f3c) != 1) {
        return;
    }
    const std::int64_t product = static_cast<std::int64_t>(0x10624dd3) * static_cast<std::int32_t>((0x10ccu - global_word(m_, collision::kInfoScratch + 0x40)) << 7);
    std::int32_t value = static_cast<std::int32_t>(product >> 32) >> 8;
    value += static_cast<std::int32_t>(static_cast<Word>(value) >> 31);
    const Word light = skater_view(m_, self).field_e4();
    skater_view(m_, self).set_draw_in_front(1);
    set_byte_at(m_, light + 2, value);
    set_byte_at(m_, light + 1, byte_at(m_, light + 2));
    set_byte_at(m_, light, byte_at(m_, light + 1));
}

// 0x0046d160 (thiscall; vtable slot 5 of the skater, called at the end of Skater_Shade): the skater's GTE lighting (+0x31ac, 13 dwords: the light matrix, the colour matrix at +0x14, the back colour at
// +0x28) copied from its source (+0x31e0), then the colour matrix's element i scaled by the shade channel i mod 3 (+0x3184) and the back colour by +0x3190, each >> 8
void WorldKit::skater_apply_shade_lighting(Word self) {
    const Word source = skater_view(m_, self).field_31e0();
    for (Word k = 0; k < 0x34; k += 4) {
        set_word_at(m_, self + 0x31ac + k, word_at(m_, source + k));
    }
    for (Word i = 0; i < 9; ++i) {
        const Word at = self + 0x31c0 + 2 * i;
        set_half_at(m_, at, u(s(word_at(m_, self + 0x3184 + 4 * (i % 3)) * static_cast<Word>(static_cast<std::int16_t>(half_at(m_, at)))) >> 8));
    }
    for (Word k = 0; k < 3; ++k) {
        const Word at = self + 0x31d4 + 4 * k;
        set_word_at(m_, at, u(s(word_at(m_, at) * skater_view(m_, self).field_3190()) >> 8));
    }
}

// 0x0046d2e0 (thiscall, four arguments): the light on the skater from the face (A0) it stands on: the average of its colour, or the shade interpolated at the skater's place from the shades of its
// corners; then the smoothed value (+0x317c) follows it by steps of 0x28 and the three colour channels are scaled from it
void WorldKit::skater_shade(Word self, Word face, Word object, Word offset_x, Word offset_z) {
    const Word flags = half_at(m_, face);
    if ((flags & 0x800) == 0) {
        const Word colour = word_at(m_, face + 8) & 0xffffff;
        const Word sum = ((colour >> 16) & 0xff) + ((colour >> 8) & 0xff) + (colour & 0xff);
        const std::int64_t product = static_cast<std::int64_t>(0x55555556) * static_cast<std::int32_t>(sum);
        std::int32_t average = static_cast<std::int32_t>(product >> 32);
        average += static_cast<std::int32_t>(static_cast<Word>(average) >> 31);
        skater_view(m_, self).set_field_3180(static_cast<Word>(u(average)));
    } else {
        const std::int32_t x = s(word_at(m_, object + 8)) >> 12;
        const std::int32_t z = s(word_at(m_, object + 0x10)) >> 12;
        const Word region = byte_at(m_, object + 0x1f);
        const Word model = word_at(m_, word_at(m_, 0x0056d43c + region * 68) + 4 * half_at(m_, object + 0x1a));
        const Word colours = word_at(m_, 0x0056d450 + region * 68);
        const Word vertices = model + 0x1c;
        const Word count = (flags & 0x10) != 0 ? 3u : 4u;
        for (Word i = 0; i < count; ++i) {
            const Word colour = word_at(m_, colours + 4 * byte_at(m_, face + i + 8)) & 0xffffff;
            const Word vertex = vertices + 8 * byte_at(m_, face + i + 4);
            const Word triple = kLocalTriples + 12 * i;
            set_word_at(m_, triple, u(static_cast<std::int16_t>(half_at(m_, vertex)) - s(offset_x) + x));
            set_word_at(m_, triple + 4, u(static_cast<std::int16_t>(half_at(m_, vertex + 4)) + z - s(offset_z)));
            const Word sum = ((colour >> 16) & 0xff) + ((colour >> 8) & 0xff) + (colour & 0xff);
            const std::int64_t product = static_cast<std::int64_t>(0x55555556) * static_cast<std::int32_t>(sum);
            std::int32_t shade = static_cast<std::int32_t>(product >> 32);
            shade += static_cast<std::int32_t>(static_cast<Word>(shade) >> 31);
            set_word_at(m_, triple + 8, u(shade));
        }
        auto triple = [&](Word i, Word k) { return kLocalTriples + 12 * i + 4 * k; };
        if (count == 4) {
            if (s(distance_to_line(0, 0, word_at(m_, triple(1, 0)), word_at(m_, triple(1, 1)), word_at(m_, triple(2, 0)), word_at(m_, triple(2, 1)))) < 0) {
                for (Word k = 0; k < 3; ++k) {
                    const Word first = word_at(m_, triple(1, k));
                    const Word third = word_at(m_, triple(3, k));
                    set_word_at(m_, triple(0, k), first);
                    set_word_at(m_, triple(1, k), third);
                }
            }
        }
        line_intersection(word_at(m_, triple(0, 0)), word_at(m_, triple(0, 1)), 0, 0, word_at(m_, triple(1, 0)), word_at(m_, triple(1, 1)), word_at(m_, triple(2, 0)), word_at(m_, triple(2, 1)), triple(3, 0), triple(3, 1),
                          kLocalRatio, kLocalRatio + 4);
        const std::int32_t numerator = s(global_word(m_, kLocalRatio));
        const std::int32_t denominator = s(global_word(m_, kLocalRatio + 4));
        const std::int32_t t1_shade = s(word_at(m_, triple(1, 2)));
        const std::int32_t t0_shade = s(word_at(m_, triple(0, 2)));
        const std::int32_t at_edge = t1_shade + divide((s(word_at(m_, triple(2, 2))) - t1_shade) * numerator, denominator);
        const std::int32_t t0x = s(word_at(m_, triple(0, 0)));
        const std::int32_t t0z = s(word_at(m_, triple(0, 1)));
        std::int32_t span_x = s(word_at(m_, triple(3, 0))) - t0x;
        std::int32_t span_z = s(word_at(m_, triple(3, 1))) - t0z;
        span_x = span_x < 0 ? -span_x : span_x;
        span_z = span_z < 0 ? -span_z : span_z;
        if (span_x > span_z) {
            if (span_x == 0) {
                span_x = 1;
            }
            const std::int32_t along = t0x < 0 ? -t0x : t0x;
            skater_view(m_, self).set_field_3180(static_cast<Word>(u(divide(along * (at_edge - t0_shade), span_x) + t0_shade)));
        } else {
            if (span_z == 0) {
                span_z = 1;
            }
            const std::int32_t along = t0z < 0 ? -t0z : t0z;
            skater_view(m_, self).set_field_3180(static_cast<Word>(u(divide(along * (at_edge - t0_shade), span_z) + t0_shade)));
        }
    }
    Word target = skater_view(m_, self).field_3180() << 1;
    skater_view(m_, self).set_field_3180(static_cast<Word>(target));
    if (skater_view(m_, self).physics_state() == 2 && s(target) < 0x100) {
        skater_view(m_, self).set_field_3180(0x100);
    }
    const std::int32_t smoothed = s(static_cast<Word>(skater_view(m_, self).depth_scale_source()));
    target = skater_view(m_, self).field_3180();
    if (smoothed < s(target)) {
        const std::int32_t next = smoothed + 0x28;
        skater_view(m_, self).set_depth_scale_source(static_cast<std::int32_t>(u(next)));
        if (next > s(target)) {
            skater_view(m_, self).set_depth_scale_source(static_cast<std::int32_t>(target));
        }
    } else if (smoothed > s(target)) {
        const std::int32_t next = smoothed - 0x28;
        skater_view(m_, self).set_depth_scale_source(static_cast<std::int32_t>(u(next)));
        if (next < s(target)) {
            skater_view(m_, self).set_depth_scale_source(static_cast<std::int32_t>(target));
        }
    }
    auto divide_by_100 = [](std::int32_t value) {
        const std::int64_t product = static_cast<std::int64_t>(0x51eb851f) * value;
        std::int32_t quotient = static_cast<std::int32_t>(product >> 32) >> 5;
        quotient += static_cast<std::int32_t>(static_cast<Word>(quotient) >> 31);
        return quotient;
    };
    Word last;
    const std::int32_t level = s(static_cast<Word>(skater_view(m_, self).depth_scale_source()));
    if (global_byte(m_, 0x005614c4) != 0) {
        const std::int32_t channel = divide_by_100(100 * (level - 0x100)) + 0x100;
        skater_view(m_, self).set_field_3184(static_cast<Word>(u(channel)));
        skater_view(m_, self).set_field_3188(static_cast<Word>(u(channel)));
        skater_view(m_, self).set_field_318c(static_cast<Word>(u(channel)));
        last = u(25 * (level - 0x100));
    } else {
        const Word slot = global_word(m_, 0x0056a898);
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t scale = s(word_at(m_, 0x0053de8c + 4 * (k + slot * 4)));
            set_word_at(m_, self + 0x3184 + 4 * k, u(divide_by_100(scale * (level - 0x100)) + 0x100));
        }
        last = u(s(word_at(m_, 0x0053de98 + (slot << 4))) * (level - 0x100));
    }
    skater_view(m_, self).set_field_3190(static_cast<Word>(u(divide_by_100(s(last)) + 0x100)));
    virtual_call(self, 5);
}

// 0x0046dd60 (thiscall, vtable slot 3 of the skater): the skater's frame. Nothing runs while the world is frozen. The model scale follows the region's flag, the competition intro only stamps the
// skater's clock, and the skater is the current one (0x0056a958) while the rest runs: the two trick-repeat stamps expire after ten frames, the replay stream of the skater's slot either plays the
// frame back (mode 2, and that is all) or, when it records (mode 1), the frame is written first; then the input, the physics frame (or the remote player's), the water, the air timer, the
// collisions with the baddies and the power-ups, the ground probe (skipped while a standing skater has not moved more than 0x40 in y) and the two landing sounds of the start animations.
void WorldKit::skater_update(Word self) {
    if (global_word(m_, kFrozen) != 0) {
        return;
    }
    set_global_byte(m_, 0x006a3d19, engine_value(kIsRemotePlayer, {self}));
    const Word scale = byte_at(m_, kAnimationRegions + 0x23 + skater_view(m_, self).psx_region_slot() * 0x44) == 1 ? 0xb54 : 0x1000;
    skater_view(m_, self).set_field_28(static_cast<std::uint16_t>(scale));
    skater_view(m_, self).set_field_2a(static_cast<std::uint16_t>(scale));
    skater_view(m_, self).set_field_2c(static_cast<std::uint16_t>(scale));
    if ((thiscall_value(kInCompetitionIntro, global_word(m_, kCompetitionCamera)) & 0xff) != 0) {
        skater_view(m_, self).set_field_2cc8(static_cast<Word>(global_word(m_, kFrameClock) + 0xe2e));
        return;
    }
    set_global_word(m_, kCurrentSkater, self);
    for (const Word stamp : {0x2e30u, 0x2e34u}) {
        const Word at = word_at(m_, self + stamp);
        if (at != 0 && s(global_word(m_, kFrameClock) - at) > 10) {
            set_word_at(m_, self + stamp, 0);
        }
    }
    const Word stream = self == global_word(m_, kPlayerOne) ? 0x0056e5d8 : 0x0056e450;
    set_global_word(m_, kAssertionLine, 0x778);
    set_string(kAssertionFile, kMechFile);
    check(word_at(m_, stream + 4) != 0, 0x00534868);
    const Word mode = word_at(m_, stream);
    if (mode == 2) {
        engine(kVideoPlayback, {self});
        set_global_word(m_, kCurrentSkater, 0);
        return;
    }
    // F1.5H: a recording takes one frame per 30 Hz frame, as retail's buffer (0x7b800 bytes) holds a whole level at that rate: at 60 fps it would fill in half the time and end the recording
    if (mode == 1 && framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); }) && (thiscall_value(kInCompetitionIntro, global_word(m_, kCompetitionCamera)) & 0xff) == 0) {
        thiscall(kTempTransform, self);
        engine(kWriteFrameChecked, {self});
        thiscall(kRestoreSavedPose, self);
    }
    skater_view(m_, self).set_field_24(0x404040);
    thiscall(kInputProfilePrep, self);
    if ((engine_value(kIsRemotePlayer, {self}) & 0xff) != 0) {
        engine(kRemotePhysics);
    } else {
        thiscall(kPhysicsFrame, self);
    }
    thiscall(kHandleWater, self);
    if ((skater_view(m_, self).field_b2() & 2) == 0) {
        skater_view(m_, self).set_field_31ec(static_cast<std::uint16_t>(skater_view(m_, self).field_31ec() + 1));
    } else {
        skater_view(m_, self).set_field_31ec(0);
    }
    if (skater_view(m_, self).field_305c() != 0) {
        skater_view(m_, self).set_field_305c(static_cast<std::uint8_t>(skater_view(m_, self).field_305c() - framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); })));
    }
    // the two sphere tests along the skater's last move (from +0xbc to the position), with the same local vector (zeroed once; the first test may leave something in it)
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalSphere + 4 * k, 0);
    }
    const Word baddy = engine_value(kLineToSphere, {self + 0xbc, self + object::kPosition, kLocalSphere, global_word(m_, kBaddyList), 0, 0x1000}, bytes_of(m_, kLocalSphere, 12));
    if (baddy != 0) {
        thiscall(kHitBaddy, self, {baddy});
        engine(kNetSendHit, {self});
    }
    const Word powerup = engine_value(kLineToSphere, {self + 0xbc, self + object::kPosition, kLocalSphere, global_word(m_, kPowerUpList), 0, 0x1000}, bytes_of(m_, kLocalSphere, 12));
    if (powerup != 0 && half_at(m_, powerup + 0xf0) == 0xffff) {
        thiscall(kCollectPowerUp, powerup, {self});
        engine(kNetSendHit, {self});
    }
    const Word height = word_at(m_, self + 0xc);
    const Word moved = height - skater_view(m_, self).field_3080();
    const Word sign = static_cast<Word>(s(moved) >> 31);
    const std::int32_t distance = s((moved ^ sign) - sign);       // (retail's branch-free abs: 0x80000000 stays negative)
    if (skater_view(m_, self).physics_state() == 0 && distance <= 0x40) {
        set_word_at(m_, self + 0x90, word_at(m_, self + 0x08));
        set_word_at(m_, self + 0x98, word_at(m_, self + 0x10));
    } else {
        skater_view(m_, self).set_field_3080(static_cast<Word>(height));
        thiscall(kProbeGround, self);
    }
    if (static_cast<std::int16_t>(skater_view(m_, self).field_b4()) < 0) {
        skater_view(m_, self).set_field_b4(0);
    }
    if (skater_view(m_, self).field_320c() == 0 && skater_view(m_, self).field_f6() == 0x5d && static_cast<std::int16_t>(skater_view(m_, self).field_f4()) > 0xf) {
        skater_view(m_, self).set_field_320c(1);
        engine(kStepOffSound, {skater_view(m_, self).field_30b0()});
    }
    if (skater_view(m_, self).field_3208() == 0 && skater_view(m_, self).field_f6() == 0x5e && static_cast<std::int16_t>(skater_view(m_, self).field_f4()) > 0xf) {
        skater_view(m_, self).set_field_3208(1);
        engine(kLandingSound, {skater_view(m_, self).field_30b0()});
    }
    set_global_word(m_, kCurrentSkater, 0);
}

}  // namespace opentony::runtime::worldkit
