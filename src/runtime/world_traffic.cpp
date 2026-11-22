#include "frame_pacing.hpp"
#include "world_views.hpp"

// Phase 4.15c: the traffic objects' collision with the skaters (`traffic.cpp`, 0x00412ef0 and 0x00413000): each car sweeps the skaters' last step through its own frame (the GTE rotation of the car's
// transposed matrix) and, when the swept box meets the car's bounding box, the skater is thrown (a stomp from above, a hit from the front or from behind) with sound, vibration and blood.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kVideoPlaying = 0x004cde70;             // Video_Playing() -> al
constexpr Word kTransposeMatrix = 0x004f53e0;          // Render_TransposeMatrixQ12(source, destination)
constexpr Word kVibrate = 0x0048ffc0;                  // CBruce__Vibrate (thiscall, three arguments)
constexpr Word kPlaySound = 0x004ad620;
constexpr Word kApplyGap = 0x0048f690;                 // Gap_ApplyDefinitionState (thiscall, four arguments)
constexpr Word kUpdateAngles = 0x0049c8d0;             // Skater_UpdateOrientationAngles (thiscall)
constexpr Word kBitAngles = 0x00406070;
constexpr Word kBitSpread = 0x00406090;
constexpr Word kBitAlloc = 0x00403d30;
constexpr Word kGlassMove = 0x0040b060;                // CGlassBit__Move (thiscall, five arguments)
constexpr Word kBail = 0x004904d0;                     // CBruce__Bail (thiscall, two arguments)

constexpr Word kRegionTable = 0x0056d43c;
constexpr Word kRegionBoxes = 0x0056d460;
constexpr Word kGteMatrix = 0x006a3e10;                // the nine words of the rotation matrix of the GTE
constexpr Word kGteVector = 0x006a3eb0;                // the three words of IR
constexpr Word kGteResult = 0x006a3eb8;                // MAC1..3
constexpr Word kPlayers = 0x0056a858;
constexpr Word kGteFlag = 0x006a3b21;
constexpr Word kGteSecond = 0x006a3a88;

// 12 s5: the car's per-frame update (0x00412960)
constexpr Word kModifyPos = 0x004ad8b0;                // SFX_ModifyPos(voice, position, 0)
constexpr Word kBuildTransform = 0x00461bf0;           // M3d_BuildTransform(object)
constexpr Word kLoadCarPosition = 0x004cdef0;          // Replay_LoadCarPosition(players, number, position, angles)
constexpr Word kStoreCarPosition = 0x004cde80;         // Replay_StoreCarPosition(players, number, position (three words), angles (a dword and a word))
constexpr Word kGetNodeLinks = 0x004c8550;             // TRG_GetNodeLinks(node)
constexpr Word kDecodeNodePosition = 0x004c8650;       // TRG_DecodeNodePosition(out, node)
constexpr Word kVectorDot = 0x004f5f90;                // Fixed_DotProductQ12(a, b)
constexpr Word kMulQ12 = 0x004f5fc0;                   // Fixed_MulQ12(a, b)
constexpr Word kCrossSign = 0x004f5f50;                // Fixed_CrossSignQ12(a, b, c, d)
constexpr Word kRatan2 = 0x004e8560;                   // orig_ratan2(y, x)
constexpr Word kCrapDistance = 0x004c9620;             // Utils_CrapDist(a, b)
constexpr Word kRandom = 0x004c9340;                   // Rnd(limit)
constexpr Word kTrafficCheck = 0x00412ef0;             // the car's collision with the skaters (above)
constexpr Word kSoundScale = 0x0052be44;               // the voice time scale: a car with a voice scales it for the call that moves the voice
constexpr Word kTrafficTimeScale = 0x0055fae8;         // 0x12c, set by every car's constructor
constexpr Word kAnimationClock = 0x005685f4;
constexpr Word kAnimationTimeScale = 0x0056865c;
constexpr Word kPlayerCount = 0x00533f3c;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kCarFile = "H:\\TonyHawk\\Pc2\\car.cpp";   // 0x00524e20
constexpr Word kLinkMessage = 0x00524eb8;              // "Current road incorrectly linked"
constexpr Word kBackwardsMessage = 0x00524e9c;         // "Oops, car going backwards!"
constexpr Word kOneLinkMessage = 0x00524e7c;           // "Road must have exactly 1 link"
constexpr Word kRoadDrop = 0x6e000;                    // the road surface lies this far below its node
constexpr Word kFrom = pcplatform::kScratchBase + 0xe280;   // the retail stack's three vectors of the update: the segment's start, its end, and its direction (12 bytes each)
constexpr Word kTo = pcplatform::kScratchBase + 0xe290;
constexpr Word kDirection = pcplatform::kScratchBase + 0xe2a0;

// 0x00465f60 Math_NormalizeFixed12: a Q12 unit vector in place (see RailKit::normalize_fixed12); a zero length gives the x axis
void normalize_q12(std::int32_t (&v)[3], std::int32_t (*root)(std::int32_t)) {
    const auto outside = [&v] {
        for (const std::int32_t w : v) {
            if (w >= 0x6883 || w <= -0x6883) {
                return true;
            }
        }
        return false;
    };
    while (outside()) {
        for (std::int32_t& w : v) {
            w >>= 1;
        }
    }
    std::uint32_t sum = 0;
    for (const std::int32_t w : v) {
        const auto narrow = static_cast<std::int32_t>(static_cast<std::int16_t>(w));
        sum += static_cast<std::uint32_t>(narrow * narrow);
    }
    const std::int32_t length = root(static_cast<std::int32_t>(sum));
    if (length == 0) {
        v[0] = 0x1000;
        v[1] = 0;
        v[2] = 0;
        return;
    }
    for (std::int32_t& w : v) {
        w = static_cast<std::int32_t>(static_cast<std::uint32_t>(w) << 12) / length;
    }
}

constexpr Word kLocalMatrix = kScratch + 0x300;        // the transposed matrix (nine words)
constexpr Word kLocalAngles = kScratch + 0x320;
constexpr Word kLocalSpread = kScratch + 0x330;

}  // namespace

// 0x00412ef0 (cdecl, one argument): finds the car's largest model box and runs the interaction with the skaters
void WorldKit::traffic_check(Word car) {
    if ((engine_value(kVideoPlaying) & 0xff) != 0) {
        return;
    }
    const Word region = byte_at(m_, car + 0x1f) * 68u;
    const std::int32_t count = half_at(m_, kRegionBoxes + region);
    std::int32_t best = 0;
    Word best_box = 0;
    const Word models = word_at(m_, kRegionTable + region);
    for (std::int32_t k = 0; k < count; ++k) {
        const Word box = word_at(m_, models + 4 * static_cast<Word>(k)) + 0xc;
        std::int32_t width = static_cast<std::int16_t>(half_at(m_, box)) - static_cast<std::int16_t>(half_at(m_, box + 2));
        std::int32_t depth = static_cast<std::int16_t>(half_at(m_, box + 8)) - static_cast<std::int16_t>(half_at(m_, box + 10));
        width = width < 0 ? -width : width;
        depth = depth < 0 ? -depth : depth;
        const std::int32_t area = width * depth;
        if (area > best) {
            best = area;
            best_box = box;
        }
    }
    engine(kTransposeMatrix, {car + 0x118, kLocalMatrix});
    take_filled(kLocalMatrix, 18, pcplatform::engine_name(kTransposeMatrix));
    if (global_byte(m_, kGteFlag) != 0) {
        traffic_interaction(car, global_word(m_, kGteSecond), kLocalMatrix, best_box);
        return;
    }
    traffic_interaction(car, global_word(m_, kPlayers), kLocalMatrix, best_box);
    const Word second = global_word(m_, kPlayers + 4);
    if (second != 0) {
        traffic_interaction(car, second, kLocalMatrix, best_box);
    }
}

// 0x00413000 (cdecl, four arguments): a skater (`who`) against a car: the skater's step this frame, in the car's frame, against the car's box
void WorldKit::traffic_interaction(Word car, Word who, Word matrix, Word box) {
    if (car == word_at(m_, who + 0x29b8)) {
        return;
    }
    if (word_at(m_, who + 0x2f64) != 0 && word_at(m_, who + 0x30b8) != 0) {
        return;
    }
    if (core_.crap_distance_of(who + 8, car + 8) >= 0x3e8) {
        return;
    }
    const std::int32_t now_x = s(word_at(m_, who + 8) - static_cast<Word>(traffic_at(m_, car).position_x_fixed()));
    const std::int32_t now_y = s(word_at(m_, who + 0xc) - static_cast<Word>(traffic_at(m_, car).position_y_fixed_after_constructor_offset()));
    const std::int32_t now_z = s(word_at(m_, who + 0x10) - static_cast<Word>(traffic_at(m_, car).position_z_fixed()));
    const std::int32_t then_x = s(word_at(m_, who + 0xbc) - static_cast<Word>(traffic_at(m_, car).position_x_fixed()));
    const std::int32_t then_y = s(word_at(m_, who + 0xc0) - static_cast<Word>(traffic_at(m_, car).position_y_fixed_after_constructor_offset()));
    const std::int32_t then_z = s(word_at(m_, who + 0xc4) - static_cast<Word>(traffic_at(m_, car).position_z_fixed()));
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, kGteMatrix + 2 * k, half_at(m_, matrix + 2 * k));
    }
    set_global_half(m_, kGteVector, u(then_x >> 12));
    set_global_half(m_, kGteVector + 2, u(then_y >> 12));
    set_global_half(m_, kGteVector + 4, u(then_z >> 12));
    math_.mvmva(0, 3);
    const std::int32_t then_rx = s(global_word(m_, kGteResult));
    const std::int32_t then_ry = s(global_word(m_, kGteResult + 4));
    const std::int32_t then_rz = s(global_word(m_, kGteResult + 8));
    set_global_half(m_, kGteVector, u(now_x >> 12));
    set_global_half(m_, kGteVector + 2, u(now_y >> 12));
    set_global_half(m_, kGteVector + 4, u(now_z >> 12));
    math_.mvmva(0, 3);
    const std::int32_t now_rx = s(global_word(m_, kGteResult));
    const std::int32_t now_ry = s(global_word(m_, kGteResult + 4));
    const std::int32_t now_rz = s(global_word(m_, kGteResult + 8));
    constexpr std::int32_t kMargin = 0x14000;
    const std::int32_t min_x = (then_rx < now_rx ? then_rx : now_rx) - kMargin;
    const std::int32_t max_x = (then_rx > now_rx ? then_rx : now_rx) + kMargin;
    const std::int32_t min_y = (then_ry < now_ry ? then_ry : now_ry) - kMargin;
    const std::int32_t max_y = (then_ry > now_ry ? then_ry : now_ry) + kMargin;
    const std::int32_t min_z = (then_rz < now_rz ? then_rz : now_rz) - kMargin;
    const std::int32_t max_z = (then_rz > now_rz ? then_rz : now_rz) + kMargin;
    auto edge = [&](Word offset) { return static_cast<std::int32_t>(static_cast<std::int16_t>(half_at(m_, box + offset))) * 4096; };
    if (max_x < edge(2) || min_x > edge(0) || max_z < edge(10) || min_z > edge(8) || max_y < edge(6) || min_y > edge(4)) {
        return;
    }
    const Word type = traffic_at(m_, car).object_subtype();
    const std::int32_t stomp = (type == 0xd8 || type == 0xd7) ? 0x64000 : 0;
    if (word_at(m_, who + 0x2f64) == 0 && word_at(m_, who + 0x30b8) == 1 && s(static_cast<Word>(traffic_at(m_, car).position_y_fixed_after_constructor_offset()) - word_at(m_, who + 0xc)) > stomp) {
        set_word_at(m_, who + 0x54, u(s(word_at(m_, who + 0x54)) >> 1));
        set_word_at(m_, who + 0x4c, u(s(word_at(m_, who + 0x4c)) >> 1));
        set_word_at(m_, who + 0x50, u(s(word_at(m_, who + 0x50)) >> 1) - 0x1e000);
        thiscall(kVibrate, who, {8, 1, 0x96});
        skater_grunt();
        if (type == 0xdb) {
            engine(kPlaySound, {0x14c, 0x2000, 0});
            engine(kPlaySound, {0x30, 0x2000, 0});
            thiscall(kApplyGap, who, {global_word(m_, 0x0052e5a4), 0xfffff830, 0, 0xffffffff});
        } else {
            engine(kPlaySound, {0x28, 0x2000, 0});
            Word first;
            if (type == 0xd7) {
                first = global_word(m_, 0x0052e2e0);
            } else if (type == 0xd8) {
                first = global_word(m_, 0x0052e2e4);
            } else {
                first = global_word(m_, 0x0052e5a0);
            }
            thiscall(kApplyGap, who, {first, 0xfffffc18, 0, 0xffffffff});
        }
        set_word_at(m_, who + 0x29b8, car);
        if (s(global_word(m_, 0x0055f72c)) >= 0xfa) {
            return;
        }
        thiscall(kUpdateAngles, who);
        set_global_half(m_, kLocalAngles, 0x1c2);
        set_global_half(m_, kLocalAngles + 2, 0x384);
        set_global_half(m_, kLocalAngles + 4, 0);
        const auto angles = m_.bytes(who + 0x14, 6);
        engine(kBitAngles, {who + 0x14}, std::string(angles.begin(), angles.end()));
        set_global_half(m_, kLocalSpread, 0x1c2);
        set_global_half(m_, kLocalSpread + 2, 0x384);
        set_global_half(m_, kLocalSpread + 4, 0);
        engine(kBitSpread, {kLocalSpread}, std::string("\xc2\x01\x84\x03\x00\x00", 6));
        set_global_word(m_, 0x005244cc, 0);
        for (Word k = 0; k < 0x14; ++k) {
            const Word block = engine_value(kBitAlloc, {0x6c});
            if (block != 0) {
                thiscall(kGlassMove, block, {who, 0x0056af28, 0xffffffd8, 0x1000, 5});
            }
        }
        set_global_word(m_, 0x005244cc, 1);
        return;
    }
    if (type == 0xd7) {
        set_word_at(m_, who + 0x50, 0xfffec000);
        {
            std::int32_t part = static_cast<std::int32_t>((static_cast<std::int64_t>(0x55555556) * s(static_cast<Word>(traffic_at(m_, car).motion_response_x_fixed()) << 2)) >> 32);
            part += static_cast<std::int32_t>(static_cast<Word>(part) >> 31);
            set_word_at(m_, who + 0x4c, u(part));
        }
        {
            std::int32_t part = static_cast<std::int32_t>((static_cast<std::int64_t>(0x55555556) * s(static_cast<Word>(traffic_at(m_, car).motion_response_z_fixed()) << 2)) >> 32);
            part += static_cast<std::int32_t>(static_cast<Word>(part) >> 31);
            set_word_at(m_, who + 0x54, u(part));
        }
        const std::int32_t zv = s(static_cast<Word>(traffic_at(m_, car).motion_response_z_fixed()));
        const std::int32_t xv = s(static_cast<Word>(traffic_at(m_, car).motion_response_x_fixed()));
        const std::int32_t zsign = zv >> 31;
        const std::int32_t xsign = xv >> 31;
        std::int32_t add_z;
        if (rnd(2) != 0) {
            set_word_at(m_, who + 0x4c, u(s(word_at(m_, who + 0x4c)) + ((zv - zsign) >> 1)));
            add_z = -((xv - xsign) >> 1);
        } else {
            set_word_at(m_, who + 0x4c, u(s(word_at(m_, who + 0x4c)) + (-((zv - zsign) >> 1))));
            add_z = (xv - xsign) >> 1;
        }
        set_word_at(m_, who + 0x54, u(s(word_at(m_, who + 0x54)) + add_z));
        thiscall(kBail, who, {1, 1});
        engine(kPlaySound, {0x72, 0x2000, 0});
    } else if (type == 0xdb) {
        set_word_at(m_, who + 0x4c, u(-s(static_cast<Word>(traffic_at(m_, car).motion_response_x_fixed())) >> 2));
        const std::int32_t lift = s(word_at(m_, who + 0x50));
        set_word_at(m_, who + 0x50, u((lift < 0 ? -lift : lift) - 0x64000));
        set_word_at(m_, who + 0x54, u(-s(static_cast<Word>(traffic_at(m_, car).motion_response_z_fixed())) >> 2));
        engine(kPlaySound, {0xffffffff, 0x2000, 0});
        thiscall(kBail, who, {1, 1});
    } else {
        std::int32_t vx = s(static_cast<Word>(traffic_at(m_, car).motion_response_x_fixed()));
        std::int32_t vz = s(static_cast<Word>(traffic_at(m_, car).motion_response_z_fixed()));
        const std::int32_t ax = vx < 0 ? -vx : vx;
        const std::int32_t az = vz < 0 ? -vz : vz;
        if (ax > az) {
            vx += vx >> 1;
            const std::int32_t first = rnd(u(vx >> 1));
            const std::int32_t second = rnd(0x10);
            std::int32_t kick = first + (vx >> 2);
            if ((second & ~7) != 0) {
                kick = -kick;
            }
            vz += kick;
        } else {
            vz += vz >> 1;
            const std::int32_t first = rnd(u(vz >> 1));
            const std::int32_t second = rnd(0x10);
            std::int32_t kick = first + (vz >> 2);
            if ((second & ~7) != 0) {
                kick = -kick;
            }
            vx += kick;
        }
        set_word_at(m_, who + 0x4c, u(vx));
        set_word_at(m_, who + 0x50, static_cast<Word>(traffic_at(m_, car).motion_response_y_fixed()) - 0x16800);
        set_word_at(m_, who + 0x54, u(vz));
        thiscall(kBail, who, {1, 1});
        engine(kPlaySound, {0x72, 0x2000, 0});
    }
    set_word_at(m_, who + 0x29b8, car);
    request_blood_splat(who, 1, 0x64);
    thiscall(kVibrate, who, {0xc, 1, 0xff});
    skater_grunt();
    switch (type) {
        case 0xd5:
            engine(kPlaySound, {static_cast<Word>(rnd(4)) + 0x73, 0x2000, 0});
            break;
        case 0xd6:
            engine(kPlaySound, {static_cast<Word>(rnd(4)) + 0x7d, 0x2000, 0});
            break;
        case 0xd7:
            engine(kPlaySound, {static_cast<Word>(rnd(2)) + 0x124, 0x2000, 0});
            break;
        case 0xd8:
            engine(kPlaySound, {0x78, 0x2000, 0});
            break;
        case 0xd9:
            engine(kPlaySound, {static_cast<Word>(rnd(4)) + 0x105, 0x2000, 0});
            break;
        case 0xdb:
            engine(kPlaySound, {static_cast<Word>(rnd(2)) + 0x14a, 0x2000, 0});
            engine(kPlaySound, {0x2f, 0x2000, 0});
            break;
        case 0xda:
        case 0xdc:
            engine(kPlaySound, {0xffffffff, 0x2000, 0});
            break;
        default:
            break;
    }
}

// 0x004136e0 (cdecl, four arguments: the car and its direction): the car's horn or passing sound, once, when a skater is beside it. The skaters within 2000 units (the octagonal distance) and
// between the two sideways tests (the signs of the cross products of the offset with the direction turned either way) trigger it; the latch (+0x1e4) keeps it from repeating while the skater
// stays there and is cleared once none is. Nothing happens while a movie plays.
void WorldKit::traffic_proximity(Word car, Word dx, Word dz) {
    if ((engine_value(kVideoPlaying) & 0xff) != 0) {
        return;
    }
    const std::int32_t count = s(global_word(m_, kPlayerCount));
    for (std::int32_t k = 0; k < count; ++k) {
        const Word player = word_at(m_, kPlayers + 4 * static_cast<Word>(k));
        if (s(engine_value(kCrapDistance, {car + 8, player + 8})) >= 0x7d0) {
            continue;
        }
        const Word offset_x = word_at(m_, player + 8) - word_at(m_, car + 8);
        const Word offset_z = word_at(m_, player + 0x10) - word_at(m_, car + 0x10);
        const Word sum = dz + dx;
        if (s(engine_value(kCrossSign, {dx - dz, offset_x, sum, offset_z})) <= 0) {
            continue;
        }
        if (s(engine_value(kCrossSign, {sum, offset_x, dz - dx, offset_z})) <= 0) {
            continue;
        }
        if (word_at(m_, car + 0x1e4) != 0) {
            return;
        }
        const Word type = static_cast<Word>(half_at(m_, car + 0x3c)) - 0xd5u;
        if (type <= 7) {
            Word sound = 0xffffffff;
            bool play = true;
            switch (type) {
            case 0: {   // the taxi: three horns
                const Word pick = engine_value(kRandom, {3});
                if (pick == 0) {
                    sound = 0x6e;
                } else if (pick == 1) {
                    sound = 0x6f;
                } else if (pick == 2) {
                    sound = 0x70;
                } else {
                    play = false;
                }
                break;
            }
            case 1:
                sound = 0x7a;
                break;
            case 2:
                sound = 0x122 + (engine_value(kRandom, {2}) != 0 ? 1 : 0);
                break;
            case 3:
                sound = 0xfb;
                break;
            case 4:
                sound = 0x110 + (engine_value(kRandom, {2}) != 0 ? 0xe : 0);
                break;
            case 6:
                sound = 0x148 + (engine_value(kRandom, {2}) != 0 ? 1 : 0);
                break;
            default:   // 5 and 7: the sound word -1
                break;
            }
            if (play) {
                engine(kPlaySound, {sound, 0x2000, 0});
            }
        }
        set_word_at(m_, car + 0x1e4, 1);
        return;
    }
    set_word_at(m_, car + 0x1e4, 0);
}

// 0x00412960 (thiscall, no arguments; the car's vtable slot 3): one frame of a traffic car. A car with a voice moves it every `period` frames (the voice time scale is scaled for the call). While a
// movie plays the car only follows the recorded position; otherwise it drives along its road: the segment from its node to the one its (single) link names, the velocity from the direction
// (the first segment) and the speed, the progress along the dominant axis (the car slows over the last 1/16th of a segment), a segment passed on to the next with what is left over of the
// frame's step, and at last the car turns towards the direction (the yaw a quarter, the pitch an eighth of the error a frame, snapping within 0x80), meets the skaters (the horn), and records
// where it is. The callees are services; the three vectors retail keeps on its stack are in the scratch window.
void WorldKit::update_traffic_object(Word self) {
    const Word period = word_at(m_, self + 0x1d4);
    if (period != 0xffffffff && word_at(m_, self + 0x1d0) != 0xffffffff) {
        const Word saved = global_word(m_, kSoundScale);
        set_global_word(m_, kSoundScale, u(s(saved * global_word(m_, kTrafficTimeScale)) / 100));
        // F1.5G: the gated update sees the clock on every other 60 fps frame, always the same parity; an even period fires on multiples, so with the flag use the even clock (retail's 30 fps clock steps by 2)
        const std::int32_t clock = s(global_word(m_, kAnimationClock)) & (framepacing::pacing().count_30hz ? ~1 : ~0);
        if (clock - divide(clock, s(period)) * s(period) == 0) {
            engine(kModifyPos, {word_at(m_, self + 0x1d0), self + 8, 0});
        }
        set_global_word(m_, kSoundScale, saved);
    }
    if ((engine_value(kVideoPlaying) & 0xff) != 0) {
        engine(kBuildTransform, {self});
        engine(kLoadCarPosition, {global_word(m_, kPlayers), word_at(m_, self + 0x1e0), self + 8, self + 0x14});
        return;
    }
    engine(kBuildTransform, {self});
    engine(kTrafficCheck, {self});

    const auto stamp = [this](Word line) {
        set_global_word(m_, kAssertionLine, line);
        set_string(kAssertionFile, kCarFile);
    };
    bool first = true;
    std::int32_t dot = 0;      // the first segment's direction dotted with the car's velocity: the speed the later segments keep
    std::int32_t carry = 0;    // the step's speed: the speed itself on the first segment, what was left over of the step on the next
    std::int32_t direction[3];
    for (;;) {
        const Word node = word_at(m_, self + 0x1dc);
        const Word links = engine_value(kGetNodeLinks, {node});
        stamp(0x2a6);
        check(half_at(m_, links) == 1, kLinkMessage);
        const Word next = half_at(m_, links + 2);
        engine_value(kDecodeNodePosition, {kFrom, node});
        set_word_at(m_, kFrom + 4, word_at(m_, kFrom + 4) - kRoadDrop);
        engine_value(kDecodeNodePosition, {kTo, next});
        set_word_at(m_, kTo + 4, word_at(m_, kTo + 4) - kRoadDrop);
        for (Word k = 0; k < 3; ++k) {
            direction[k] = s(word_at(m_, kTo + 4 * k) - word_at(m_, kFrom + 4 * k));
        }
        normalize_q12(direction, &integer_sqrt);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kDirection + 4 * k, u(direction[k]));
        }

        std::int32_t drive;
        if (first) {
            first = false;
            dot = s(engine_value(kVectorDot, {kDirection, self + 0x4c}));
            carry = dot < 0 ? 0x1000 : (dot < 0x50000 ? dot + 0x1000 : dot);
            drive = carry;
        } else {
            drive = dot;
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0x4c + 4 * k, engine_value(kMulQ12, {u(direction[k]), u(drive)}));
        }

        // the dominant axis (the first of equals) measures the progress along the segment
        std::int32_t magnitude[3];
        for (Word k = 0; k < 3; ++k) {
            magnitude[k] = direction[k] < 0 ? -direction[k] : direction[k];
        }
        Word axis = 0;
        if (magnitude[1] > magnitude[0]) {
            axis = 1;
        }
        if (magnitude[2] > magnitude[axis]) {
            axis = 2;
        }
        const std::int32_t length = s(u(divide(s(word_at(m_, kTo + 4 * axis) - word_at(m_, kFrom + 4 * axis)), direction[axis])) << 12);
        std::int32_t progress = s(u(divide(s(word_at(m_, self + 8 + 4 * axis) - word_at(m_, kFrom + 4 * axis)), direction[axis])) << 12);

        std::int32_t step = s(global_word(m_, kAnimationTimeScale) * u(carry)) >> 8;
        if (progress > length - (length >> 4)) {
            step -= step >> 4;
            for (Word k = 0; k < 3; ++k) {
                const std::int32_t velocity = s(word_at(m_, self + 0x4c + 4 * k));
                set_word_at(m_, self + 0x4c + 4 * k, u(velocity - (velocity >> 4)));
            }
        }
        progress = s(u(progress) + u(step));
        if (progress >= 0 && progress <= length) {
            for (Word k = 0; k < 3; ++k) {
                const Word moved = engine_value(kMulQ12, {u(direction[k]), u(step)});
                set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 8 + 4 * k) + moved);
            }
            break;
        }
        stamp(0x30e);
        check(progress >= 0, kBackwardsMessage);
        const Word onward = engine_value(kGetNodeLinks, {next});
        stamp(0x312);
        check(half_at(m_, onward) == 1, kOneLinkMessage);
        carry = s(u(progress) - u(length));
        set_word_at(m_, self + 0x1dc, next);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 8 + 4 * k, word_at(m_, kTo + 4 * k));
        }
        if (carry == 0) {
            break;
        }
    }

    // turn towards the direction: the wrapped difference of the target and the current angle, snapped within 0x80, else a fraction of it
    const auto turn = [this, self](Word field, Word target, unsigned shift) {
        const std::int32_t current = static_cast<std::int16_t>(half_at(m_, self + field));
        const Word wanted = target & 0xfff;
        std::int32_t difference = s(wanted) - current;
        if (difference < -0x800) {
            difference += 0x1000;
        }
        if (difference > 0x7ff) {
            difference -= 0x1000;
        }
        if ((difference < 0 ? -difference : difference) < 0x80) {
            set_half_at(m_, self + field, wanted);
        } else {
            set_half_at(m_, self + field, u((difference >> shift) + static_cast<std::int32_t>(half_at(m_, self + field))) & 0xfff);
        }
    };
    turn(0x16, engine_value(kRatan2, {u(-direction[0]), u(-direction[2])}), 2);
    turn(0x14, engine_value(kRatan2, {u(direction[1]), 0x1000}), 3);
    traffic_proximity(self, u(direction[0]), u(direction[2]));
    engine(kStoreCarPosition, {global_word(m_, kPlayers), word_at(m_, self + 0x1e0), word_at(m_, self + 8), word_at(m_, self + 0xc), word_at(m_, self + 0x10), word_at(m_, self + 0x14), half_at(m_, self + 0x18)});
}

}  // namespace opentony::runtime::worldkit
