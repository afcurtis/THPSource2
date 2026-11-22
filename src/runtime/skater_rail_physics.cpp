#include "frame_pacing.hpp"
#include "skater_views.hpp"

#include <array>

// Phase 4.15a: the skater on a rail (physics.cpp, 0x00494210): every tick the skater slides along the segment of the rail it is on (the rail is a chain of nodes whose positions the trigger layer decodes),
// carrying its speed from segment to segment (up to five in a tick, and only round a turn that is not too sharp), balances on it (a bar that swings with the speed and the stats and that the pad pushes
// back), bails when the bar swings too far (unless a ledge is within reach, when it drops onto the ledge) and leaves the rail when the segments run out or a wall is hit. The node decoder, the collision
// queries, the random numbers and the cheat query are services; the rail walks are the rail layer's functions.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;
using Vec = std::array<Word, 3>;

namespace gte = mathkit::gte;

constexpr Word kRandom = 0x004c9340;               // Rnd(n) -> a number below n
constexpr Word kDecodeNodePosition = 0x004c8650;   // TRG_DecodeNodePosition(out, node)
constexpr Word kRebuildRecovery = 0x0049d080;      // Skater_RebuildOrientationRecovery() (thiscall)
constexpr Word kZeroVector = 0x0056af28;
constexpr Word kQueryFlag = 0x00567c7c;            // set while a query is made on the world's behalf
constexpr Word kStepScale = 0x00568804;
constexpr Word kGravityCheat = 0x0056b7ec;
constexpr Word kMsgNoSegment = 0x00536da0;         // "Null mCurrentRailSegemnt"
constexpr Word kMsgNoNext = 0x00536d7c;            // "No rail segment when one expected"
constexpr Word kLogAboveLedge = 0x00536d5c;        // "Hit something above the ledge"
constexpr Word kLogBelowLedge = 0x00536d3c;        // "Hit something below the ledge"
constexpr Word kLogWall = 0x00536d14;              // "%d: Hit wall on rail, so leaving"

constexpr Word kLocals = pcplatform::kScratchBase + 0xa00;
constexpr Word kDir = kLocals;                     // the direction of the segment (Q12)
constexpr Word kWork = kLocals + 0x10;             // working vectors
constexpr Word kWorkA = kLocals + 0x20;
constexpr Word kWorkB = kLocals + 0x30;
constexpr Word kAngles = kLocals + 0x40;
constexpr Word kMatrix = kLocals + 0x50;
constexpr Word kWork0 = kLocals + 0x70;
constexpr Word kWork1 = kLocals + 0x80;

constexpr Word kHitBody = 0x68;
constexpr Word kHitPosition = 0x6c;
constexpr Word kNormal = 0x78;

inline Word mulw(std::int32_t a, std::int32_t b) { return static_cast<Word>(a) * static_cast<Word>(b); }
inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

}  // namespace

// the position the trigger layer decodes for a node (three dwords through the scratch the retail code keeps in a local)
std::array<std::uint32_t, 3> SkaterKit::decode_node_position(Word node) {
    const std::string name = pcplatform::engine_name(kDecodeNodePosition);
    s_.note(name, {rail::kPositionScratch, node});
    if (s_.live()) {
        // (the real service wrote the position to the scratch and answers once per call: the scripted words are replay-only)
        return {global_word(m_, rail::kPositionScratch), global_word(m_, rail::kPositionScratch + 4), global_word(m_, rail::kPositionScratch + 8)};
    }
    Vec v;
    for (Word k = 0; k < 3; ++k) {
        v[k] = static_cast<Word>(s_.value(name));
    }
    (void)s_.value(name);          // (the answer: the address after the node's position)
    return v;
}

// a vector of Q12 components made a unit vector (`Math_NormalizeFixed12`)
std::array<std::uint32_t, 3> SkaterKit::unit_of(const std::array<std::uint32_t, 3>& v) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kWork + 4 * k, v[k]);
    }
    normalize_fixed12(kWork);
    return {global_word(m_, kWork), global_word(m_, kWork + 4), global_word(m_, kWork + 8)};
}

// 0x00494210 (fastcall)
void SkaterKit::update_rail_physics(Word self) {
    const Word info = collision::kInfoScratch;
    const Word position = self + skater::kPosition;
    const Word old_position = self + 0xbc;
    const Word velocity = self + trick::kMovement;
    const Word acceleration = self + 0x58;
    const auto read = [this, self](Word field) { return word_at(m_, self + field); };
    const auto write = [this, self](Word field, Word value) { set_word_at(m_, self + field, value); };
    const auto sub = [](const Vec& a, const Vec& b) { return Vec{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto store = [this](Word at, const Vec& v) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, at + 4 * k, v[k]);
        }
    };
    const auto absolute = [](std::int32_t v) { return v < 0 ? -v : v; };
    const auto stat = [this, self](Word selector) { return profile_value(self, selector); };
    const std::int32_t time_scale = s(global_word(m_, globals::kAnimationTimeScale));

    write(0x2edc, global_word(m_, globals::kAnimationClock));
    Vec dir{0, 0, 0};
    std::int32_t along = 0;
    bool first = true;
    bool moved = false;
    write(0x2f14, 0);
    vibrate(self, 2, 0, 1);
    if (read(0x2c0c) != 0) {
        spawn_tag_glass(self);
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, acceleration + 4 * k, 0);
    }
    std::int32_t pull = 13000;
    if ((engine_value(kGetCheat, {0xc}) & 0xff) != 0) {
        pull = s(mulw(pull, 150)) / 100;
    }
    if ((engine_value(kGetCheat, {0xe}) & 0xff) != 0) {
        pull = s(mulw(pull, 50)) / 100;
    }
    if (global_word(m_, kGravityCheat) != 0) {
        pull = s(mulw(pull, 50)) / 200;
    }
    const Vec gravity{0, u(pull), 0};
    advance_point_timer_when_moved(self);

    // ---- along the rail: up to five segments ----
    Word iteration = 1;
    for (;;) {
        register_rail_trick_object(self, half_at(m_, self + 0x2ea0));
        check(read(0x2ea0) != 0, kMsgNoSegment);
        const Word next = rail_.next_node(half_at(m_, self + 0x2ea0)) & 0xffff;
        check(next != 0, kMsgNoNext);
        const Vec start = decode_node_position(read(0x2ea0));
        const Vec end = decode_node_position(next);
        dir = unit_of(sub(end, start));
        store(kDir, dir);
        Word axis = 0;
        {
            const std::int32_t ax = absolute(s(dir[0]));
            const std::int32_t ay = absolute(s(dir[1]));
            const std::int32_t az = absolute(s(dir[2]));
            if (ax < ay) {
                axis = 1;
                if (ay < az) {
                    axis = 2;
                }
            } else if (ax < az) {
                axis = 2;
            }
        }
        along = math_.dot_q12(kDir, velocity);
        if (first) {
            first = false;
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, acceleration + 4 * k, word_at(m_, acceleration + 4 * k) + gravity[k]);
            }
            if (along == 0) {
                along = 1000;
            }
            if (absolute(along) < 60000) {
                if (dir[1] == 0) {
                    along = (along * 5) / 4;
                } else {
                    along += (s(dir[1]) >= 0 ? 1 : -1) * 2000;
                }
            }
        }
        const std::int32_t pulled = math_.dot_q12(kDir, acceleration);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, acceleration + 4 * k, u(math_.mul_q12(s(dir[k]), pulled)));
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, velocity + 4 * k, u(math_.mul_q12(s(dir[k]), along)));
        }
        const std::int32_t segment = s(u(s(end[axis] - start[axis]) / s(dir[axis])) << 12);
        const std::int32_t reached = s(u(s(word_at(m_, position + 4 * axis) - start[axis]) / s(dir[axis])) * 0x1000u + u(along));
        if (reached >= 0) {
            if (reached <= segment) {
                // inside the segment: the move of the tick
                for (Word k = 0; k < 3; ++k) {
                    const std::int32_t from_acceleration = (s(mulw(s(word_at(m_, acceleration + 4 * k)), s(global_word(m_, kStepScale)))) >> 8) / 2;
                    const std::int32_t from_velocity = s(mulw(s(word_at(m_, velocity + 4 * k)), time_scale)) >> 8;
                    set_word_at(m_, position + 4 * k, word_at(m_, position + 4 * k) + u(from_velocity) + u(from_acceleration));
                }
                break;
            }
            // past the end of the segment: on to the next one, when the rail does not turn too sharply
            const Word after = rail_.next_node(next) & 0xffff;
            if (after == 0) {
                leave_rail(self);
                break;
            }
            const Vec turn_start = decode_node_position(next);
            const Vec turn_end = decode_node_position(after);
            const Vec onward = unit_of(sub(turn_end, turn_start));
            const Vec flat_now = unit_of(Vec{dir[0], 0, dir[2]});
            store(kWorkA, flat_now);
            const Vec flat_onward = unit_of(Vec{onward[0], 0, onward[2]});
            store(kWorkB, flat_onward);
            if (math_.dot_q12(kWorkA, kWorkB) < 0xa49) {
                leave_rail(self);
                break;
            }
            write(0x2ea0, next);
            write(0x2e8c, rail_.rail_type(next));
            store(position, end);
            write(0xc, read(0xc) + 0xfffe7000u);
            moved = true;
            const std::int32_t carried = reached - segment;
            if (carried == 0) {
                break;
            }
            if (++iteration > 5) {
                leave_rail(self);
                break;
            }
            continue;
        }
        // before the start of the segment: back to the node before
        const Word previous = rail_.prev_node_quick(half_at(m_, self + 0x2ea0), half_at(m_, self + 0x2e9c)) & 0xffff;
        if (previous == 0) {
            leave_rail(self);
            break;
        }
        const Vec before = decode_node_position(previous);
        const Vec here = decode_node_position(read(0x2ea0));
        const Vec behind = unit_of(sub(here, before));
        store(kWorkA, behind);
        if (math_.dot_q12(kDir, kWorkA) < 0xa49) {
            leave_rail(self);
            break;
        }
        write(0x2ea0, previous);
        write(0x2e8c, rail_.rail_type(previous));
        store(position, start);
        write(0xc, read(0xc) + 0xfffe7000u);
        moved = true;
        if (++iteration > 5) {
            leave_rail(self);
            break;
        }
    }

    // ---- the direction the skater faces on the rail ----
    if (along < 0) {
        for (Word k = 0; k < 3; ++k) {
            write(0x2eb4 + 4 * k, dir[k]);
        }
    } else {
        for (Word k = 0; k < 3; ++k) {
            write(0x2eb4 + 4 * k, 0u - dir[k]);
        }
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kWork + 4 * k, mulw(s(read(0x2eb4 + 4 * k)), s(read(0x2ed8))));
    }
    if (math_.dot_q12(kWork, self + skater::kBasis) < 0 && (read(0x2ecc) != read(0xbc) || read(0x2ed0) != read(0xc0) || read(0x2ed4) != read(0xc4))) {
        write(0x2ed8, 0u - read(0x2ed8));
        write(0x2c78, read(0x2c78) == 0 ? 1 : 0);
        write(0x2c7c, read(0x2c7c) == 0 ? 1 : 0);
    }
    for (Word k = 0; k < 3; ++k) {
        write(skater::kBasis + 4 * k, mulw(s(read(0x2eb4 + 4 * k)), s(read(0x2ed8))));
    }
    rebuild_sideways_basis(self);

    // ---- the balance bar ----
    {
        const std::int32_t divisor_a = s(u(stat(7)) * 2100u + 400000u) / 10000;
        const std::int32_t swung = s(mulw(s(read(0x2ef0)) / divisor_a, (s(read(0x2efc)) >> 12) + 0x3c));
        const std::int32_t divisor_b = s(mulw(stat(7) + 350, 2000)) / 10000;
        write(0x2ef0, read(0x2ef0) + u(framepacing::scaled_carry(framepacing::CarrySite::rail_swing, self, s(mulw(swung / divisor_b, time_scale)))));   // retail: >> 8 (frame_pacing.hpp)
        const std::int32_t divisor_c = s(mulw(stat(7) + 350, 2000)) / 10000;
        const std::int32_t pushed = s(mulw((s(read(0x2efc)) >> 12) + 0x3c, s(read(0x2ef4)))) / divisor_c;
        write(0x2ef0, read(0x2ef0) + u(framepacing::scaled_carry(framepacing::CarrySite::rail_push, self, s(mulw(pushed, time_scale)))));
        if (framepacing::frame_count_step([&] { return global_word(m_, 0x0056a940); }) != 0) {      // one random kick of the bar per 30 Hz frame (frame_pacing.hpp)
            const std::int32_t size = absolute(s(read(0x2ef4)));
            const std::int32_t random = s(engine_value(kRandom, {u(size)}));
            write(0x2ef4, read(0x2ef4) + u((random - (size >> 1)) / 10));
        }
        if ((engine_value(kGetCheat, {5}) & 0xff) != 0) {
            write(0x2ef0, 0);
        }
    }
    {
        const Word pad = read(0x2ccc);
        const bool left = byte_at(m_, pad + 0x80) != 0;
        const bool right = byte_at(m_, pad + 0x90) != 0;
        const std::int32_t stick = static_cast<std::int8_t>(skater_at(m_, self).field_31a1());
        if (!left && stick > -0x29 && !right && stick < 0x29) {
            write(0x2eec, 1);
        }
        bool settled = false;
        if ((left || stick < -0x28) && read(0x2eec) != 0) {
            write(0x2ef4, read(0x2ef4) - u(s(mulw(s(mulw(s(read(0x2ed8)), time_scale)), 0x14)) >> 8));
            settled = true;
        }
        if (!settled) {
            if ((!right && stick < 0x29) || read(0x2eec) == 0) {
                const std::int32_t size = absolute(s(read(0x2ef4)));
                if (size < s(mulw(200 - stat(7), 1000)) / 10000 && framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); })) {   // F2C: the bar's re-seed (a fresh random each frame) once per 30 Hz frame, as the kick above
                    const std::int32_t low = s(800000u - u(stat(7)) * 3300u) / 10000;
                    const std::int32_t random = s(engine_value(kRandom, {u(low)}));
                    const std::int32_t spread = s(u(stat(7)) * 3300u - 800000u) / 10000;
                    write(0x2ef4, u(random + spread / 2));
                }
            } else {
                write(0x2ef4, read(0x2ef4) + u(s(mulw(s(mulw(s(read(0x2ed8)), time_scale)), 0x14)) >> 8));
            }
        }
    }

    // ---- a bar swung too far: a ledge to drop onto, or a bail ----
    const auto zone_hit = [this, info] { return word_at(m_, info + kHitBody) != 0; };
    if (absolute(s(read(0x2ef0))) > 4000) {
        const Word saved_flag = global_word(m_, kQueryFlag);
        if (read(0x2bfc) != 0) {
            const Word onto = rail_.next_node(half_at(m_, self + 0x2ea0)) & 0xffff;
            const Vec from = decode_node_position(read(0x2ea0));
            const Vec to = decode_node_position(onto);
            const Vec ahead = unit_of(sub(to, from));
            store(kWorkA, ahead);
            const Word agreement = u(math_.dot_q12(kWorkA, velocity));
            if ((((read(0x2bfc) - 2) ^ read(0x2ef0) ^ agreement) & 0x80000000u) != 0) {
                const std::int32_t direction = s(read(0x2ef0)) >= 0 ? -1 : 1;
                std::int32_t push[3];
                for (Word k = 0; k < 3; ++k) {
                    push[k] = s(mulw(s(mulw(s(read(0x3100 + 4 * k)), direction)), 0x14));
                }
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, info + collision::line::kStart + 4 * k, word_at(m_, position + 4 * k));
                    set_word_at(m_, info + collision::line::kEnd + 4 * k, word_at(m_, position + 4 * k));
                }
                set_word_at(m_, info + collision::line::kStart, word_at(m_, info + collision::line::kStart) + u(push[0]));
                set_word_at(m_, info + collision::line::kStart + 8, word_at(m_, info + collision::line::kStart + 8) + u(push[2]));
                set_word_at(m_, info + collision::line::kEnd, word_at(m_, info + collision::line::kEnd) - u(push[0]));
                set_word_at(m_, info + collision::line::kEnd + 8, word_at(m_, info + collision::line::kEnd + 8) - u(push[2]));
                set_word_at(m_, info + collision::line::kStart + 4, word_at(m_, info + collision::line::kStart + 4) - 0x28000u);
                set_word_at(m_, info + collision::line::kEnd + 4, word_at(m_, info + collision::line::kEnd + 4) - 0x28000u);
                set_global_word(m_, kQueryFlag, 0);
                zone_query(info);
                if (zone_hit()) {
                    log_line(kLogAboveLedge);
                    set_word_at(m_, info + collision::line::kStart + 4, word_at(m_, info + collision::line::kStart + 4) + 0x50000u);
                    set_word_at(m_, info + collision::line::kEnd + 4, word_at(m_, info + collision::line::kEnd + 4) + 0x50000u);
                    zone_query(info);
                    if (zone_hit()) {
                        log_line(kLogBelowLedge);
                    }
                    set_global_half(m_, kAngles, 0);
                    set_global_half(m_, kAngles + 2, (read(0x2ef0) == 0 || s(read(0x2ef0)) > 0 ? 0x200u : 0xe00u));
                    set_global_half(m_, kAngles + 4, 0);
                    math_.rotation_from_angles(kAngles, kMatrix);
                    rotate_orientation(self, kMatrix);
                    fru_from_rotation_matrix(self);
                    set_half_at(m_, self + 0x80, half_at(m_, info + kNormal));
                    set_half_at(m_, self + 0x82, half_at(m_, info + kNormal + 2));
                    write(0x3130, 0);
                    set_half_at(m_, self + 0x84, half_at(m_, info + kNormal + 4));
                    write(0x3138, u(sext16(half_at(m_, info + kNormal + 2))));
                    write(0x3134, u(sext16(half_at(m_, info + kNormal))));
                    write(0x313c, u(sext16(half_at(m_, info + kNormal + 4))));
                    engine(kRebuildRecovery, {self});
                    const std::int32_t speed = s(sqrt_truncated(u(math_.dot_q12(velocity, velocity))) << 6);
                    for (Word k = 0; k < 3; ++k) {
                        set_word_at(m_, velocity + 4 * k, 0u - u(math_.mul_q12(s(read(skater::kBasis + 4 * k)), speed)));
                    }
                    for (Word k = 0; k < 3; ++k) {
                        set_word_at(m_, position + 4 * k, word_at(m_, info + kHitPosition + 4 * k));
                    }
                    for (Word k = 0; k < 3; ++k) {
                        set_word_at(m_, position + 4 * k, word_at(m_, position + 4 * k) + mulw(s(read(0x310c + 4 * k)), 0x1e));
                    }
                    write(0x29d0, 0);
                    leave_rail(self);
                    state_transition(self, 0);
                    return;
                }
                set_global_word(m_, kQueryFlag, saved_flag);
            }
        }
        bail(self, u(sext16(skater_at(m_, self).field_29f2())), 0);
        skater_at(m_, self).set_field_29f2(0);
        write(0x3038, read(0x3038) + 1);
    }

    // ---- the lean of the board with the bar ----
    set_global_half(m_, kAngles, 0);
    set_global_half(m_, kAngles + 2, 0);
    set_global_half(m_, kAngles + 4, u(s(read(0x2ef0)) / 10) & 0xfff);
    math_.rotation_from_angles(kAngles, kMatrix);
    rotate_orientation(self, kMatrix);
    fru_from_rotation_matrix(self);

    // ---- a wall in the way: off the rail ----
    if (read(0x2e90) != 0 && s(read(0x2efc) & 0xfffff000u) > 0x2000 && !moved) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, info + collision::line::kStart + 4 * k, word_at(m_, old_position + 4 * k));
            set_word_at(m_, info + collision::line::kEnd + 4 * k, word_at(m_, position + 4 * k));
        }
        for (Word k = 0; k < 3; ++k) {
            const Word away = mulw(s(mulw(s(0u - read(0x2eb4 + 4 * k)), 1)), 0x78);
            set_word_at(m_, info + collision::line::kEnd + 4 * k, word_at(m_, info + collision::line::kEnd + 4 * k) + away);
        }
        Vec travelled{word_at(m_, position) - word_at(m_, old_position), word_at(m_, position + 4) - word_at(m_, old_position + 4), word_at(m_, position + 8) - word_at(m_, old_position + 8)};
        if (travelled[0] != global_word(m_, kZeroVector) || travelled[1] != global_word(m_, kZeroVector + 4) || travelled[2] != global_word(m_, kZeroVector + 8)) {
            set_word_at(m_, info + collision::line::kEnd + 4, word_at(m_, info + collision::line::kEnd + 4) - 0x32000u);
            set_word_at(m_, info + collision::line::kStart + 4, word_at(m_, info + collision::line::kStart + 4) - 0x32000u);
            const Word saved = global_word(m_, kQueryFlag);
            set_global_word(m_, kQueryFlag, 1);
            zone_query(info);
            set_global_word(m_, kQueryFlag, saved);
            if (zone_hit()) {
                log_line(kLogWall);
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, position + 4 * k, word_at(m_, old_position + 4 * k));
                }
                write(0x2f50, global_word(m_, globals::kAnimationClock));
                leave_rail(self);
            }
        }
    }
}

// 0x004aaf70 (cdecl, the skater): which rail the skater is near. Every rail record (the list at 0x0056b860: a state byte at +4, the first node at +6, the box of the chain at +8..+0x1c) whose box meets the box
// the skater moved through (made 100 units larger) has its segments walked; the segment service answers with the distance to the segment and the nearest point; the nearest one the skater is moving
// across (not along) and is above or may land on is the rail it grinds: its node, the point, the kind, the direction along it, and, from four short queries across the segment, whether there is a wall or
// nothing on its sides (+0x2bfc). The skater must hold the grind button (the first byte of its pad record) and not be on a rail already (+0x2e90) or bailing (+0x2f64)
void SkaterKit::rail_update(Word self) {
    constexpr Word kRecordHead = 0x0056b860;
    constexpr Word kSegmentService = 0x00465830;       // (old position, position, node a, node b, out, out, nearest point, out) -> the distance
    constexpr Word kLogClearing = 0x005376e0;          // "Clearing mDontStickTimer"
    constexpr Word kSegmentA = pcplatform::kScratchBase + 0xc00;
    constexpr Word kSegmentB = pcplatform::kScratchBase + 0xc10;
    constexpr Word kOut1 = pcplatform::kScratchBase + 0xc20;
    constexpr Word kOut2 = pcplatform::kScratchBase + 0xc30;
    constexpr Word kPointAt = pcplatform::kScratchBase + 0xc40;
    constexpr Word kOut4 = pcplatform::kScratchBase + 0xc50;
    const Word info = collision::kInfoScratch;
    const auto read = [this, self](Word field) { return word_at(m_, self + field); };
    const auto write = [this, self](Word field, Word value) { set_word_at(m_, self + field, value); };
    const auto sub = [](const Vec& a, const Vec& b) { return Vec{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto store = [this](Word at, const Vec& v) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, at + 4 * k, v[k]);
        }
    };
    const auto absolute = [](std::int32_t v) { return v < 0 ? -v : v; };
    if (byte_at(m_, read(0x2ccc)) == 0) {
        write(0x2e34, 0);
        return;
    }
    if (read(0x2e90) != 0 || read(0x2f64) != 0 || read(0x2e34) != 0) {
        return;
    }
    const std::int32_t pos[3] = {s(read(0x8)), s(read(0xc)), s(read(0x10))};
    const std::int32_t old[3] = {s(read(0xbc)), s(read(0xc0)), s(read(0xc4))};
    write(0x2ea4, 100000000);
    std::int32_t low[3];
    std::int32_t high[3];
    for (Word k = 0; k < 3; ++k) {
        low[k] = (pos[k] < old[k] ? pos[k] : old[k]) - 0x64000;
        high[k] = (pos[k] < old[k] ? old[k] : pos[k]) + 0x64000;
    }
    for (Word record = global_word(m_, kRecordHead); record != 0; record = word_at(m_, record + 0x24)) {
        if (byte_at(m_, record + 4) == 0 || int_at(m_, record + 8) > high[0] || low[0] > int_at(m_, record + 0x14) || int_at(m_, record + 0xc) > high[1] || low[1] > int_at(m_, record + 0x18) ||
            int_at(m_, record + 0x10) > high[2] || low[2] > int_at(m_, record + 0x1c) || !(s(read(0x2ec4)) < 1 || s(read(0x2ec8)) != sext16(half_at(m_, record + 6)))) {
            continue;
        }
        Word node = u(sext16(half_at(m_, record + 6)));
        Word current = rail_.next_node(node) & 0xffff;
        while (current != 0) {
            const Vec a = decode_node_position(node);
            const Vec b = decode_node_position(current);
            std::int32_t range_low[3];
            std::int32_t range_high[3];
            for (Word k = 0; k < 3; ++k) {
                range_low[k] = s(a[k]) < s(b[k]) ? s(a[k]) : s(b[k]);
                range_high[k] = s(a[k]) < s(b[k]) ? s(b[k]) : s(a[k]);
            }
            if (range_low[0] <= high[0] && low[0] <= range_high[0] && range_low[1] <= high[1] && low[1] <= range_high[1] && range_low[2] <= high[2] && low[2] <= range_high[2]) {
                store(kSegmentA, a);
                store(kSegmentB, b);
                const std::string service = pcplatform::engine_name(kSegmentService);
                s_.note(service, {self + 0xbc, self + 8, kSegmentA, kSegmentB, kOut1, kOut2, kPointAt, kOut4});
                Vec point;
                for (Word k = 0; k < 3; ++k) {
                    // (live, the service wrote the nearest point to kPointAt and answers once: the distance)
                    point[k] = s_.live() ? word_at(m_, kPointAt + 4 * k) : static_cast<Word>(s_.value(service));
                }
                const std::int32_t distance = static_cast<std::int32_t>(s_.value(service));
                const Vec heading = unit_of(Vec{read(0x4c), 0, read(0x54)});
                store(kWork0, heading);
                const Vec across = sub(b, a);
                const Vec along_flat = unit_of(Vec{across[0], 0, across[2]});
                store(kWork1, along_flat);
                const std::int32_t agreement = absolute(math_.dot_q12(kWork1, kWork0));
                const std::int32_t near_score = s(mulw(0x2000 - agreement, distance));
                const std::int32_t score = s(mulw(0x1032 - agreement, distance));
                if (score < s(read(0x2ea4)) && ((read(skater::kState) != 0 && read(skater::kState) != 2) || s(read(0xc)) < s(point[1]))) {
                    if (((near_score + ((near_score >> 31) & 0xfff)) >> 12) < 0x65) {
                        if (0xdac < agreement && read(0x2ec0) == 1) {
                            log_line(kLogClearing);
                            write(0x2ec0, 0);
                        }
                        const Vec edge = unit_of(sub(b, a));
                        store(kWork1, edge);
                        store(kWork0, Vec{read(0x4c), read(0x50), read(0x54)});
                        std::int32_t cosine = math_.dot_q12(kWork1, kWork0);
                        if (absolute(cosine) < 0x1000) {
                            store(kWork0, Vec{read(0x30f4), read(0x30f8), read(0x30fc)});
                            cosine = 0 - math_.dot_q12(kWork1, kWork0);
                        }
                        bool skip = false;
                        const auto same = [](const Vec& x, const Vec& y) { return x[0] == y[0] && x[1] == y[1] && x[2] == y[2]; };
                        if (cosine < 1) {
                            write(0x2c00, 1);
                            write(0x2eb4, edge[0]);
                            write(0x2eb8, edge[1]);
                            write(0x2ebc, edge[2]);
                            if (!same(point, b) && same(point, a)) {
                                skip = (rail_.prev_node(node) & 0xffff) == 0;
                            }
                        } else {
                            write(0x2eb4, 0u - edge[0]);
                            write(0x2eb8, 0u - edge[1]);
                            write(0x2ebc, 0u - edge[2]);
                            write(0x2c00, 0xffffffffu);
                            if (!same(point, a) && same(point, b)) {
                                skip = (rail_.next_node(current) & 0xffff) == 0;
                            }
                        }
                        if (!skip) {
                            write(0x2ea4, u(score));
                            write(0x2e94, 1);
                            write(0x2e9c, u(sext16(half_at(m_, record + 6))));
                            write(0x2ea8, point[0]);
                            write(0x2eac, point[1]);
                            write(0x2eb0, point[2]);
                            write(0x2ea0, node);
                            write(0x2e8c, rail_.rail_type(node & 0xffff));
                            // what lies beside the segment: a quarter of the way along it, ten units up, two short queries across it at each end of the quarter point, then two across at half the distance
                            const Vec from = decode_node_position(node);
                            const Vec to = decode_node_position(current);
                            Vec quarter{from[0] + to[0], from[1] + to[1], from[2] + to[2]};
                            for (Word k = 0; k < 3; ++k) {
                                quarter[k] = u(s(quarter[k]) / 2);
                            }
                            for (Word k = 0; k < 3; ++k) {
                                quarter[k] = quarter[k] - from[k];
                            }
                            for (Word k = 0; k < 3; ++k) {
                                quarter[k] = u(s(quarter[k]) / 2);
                            }
                            for (Word k = 0; k < 3; ++k) {
                                quarter[k] = quarter[k] + from[k];
                            }
                            quarter[1] -= 0xa000;
                            const Vec toward = sub(to, from);
                            const Vec side_unit = unit_of(Vec{toward[0], 0, toward[2]});
                            Vec side{side_unit[2], 0, 0u - side_unit[0]};
                            for (Word k = 0; k < 3; ++k) {
                                side[k] = mulw(s(side[k]), 10);
                            }
                            set_global_word(m_, kQueryFlag, 1);
                            const auto probe = [this, info](Word x, Word y, Word z, Word top) {
                                set_word_at(m_, info + collision::line::kStart, x);
                                set_word_at(m_, info + collision::line::kStart + 4, y);
                                set_word_at(m_, info + collision::line::kStart + 8, z);
                                set_word_at(m_, info + collision::line::kEnd, x);
                                set_word_at(m_, info + collision::line::kEnd + 4, y + top);
                                set_word_at(m_, info + collision::line::kEnd + 8, z);
                                set_word_at(m_, info + 0x68, 0);
                                zone_query(info);
                                return word_at(m_, info + 0x68) != 0;
                            };
                            bool first_side = probe(quarter[0] + side[0] * 0xfffffffeu, quarter[1], quarter[2] + side[2] * 0xfffffffeu, 0x1e000);
                            side[0] = side[0] * 2;
                            side[2] = side[2] * 2;
                            bool second_side = probe(side[0] + quarter[0], quarter[1], side[2] + quarter[2], 0x1e000);
                            if (second_side || first_side) {
                                if (second_side) {
                                    write(0x2bfc, 2);
                                }
                                if (first_side) {
                                    write(0x2bfc, 1);
                                }
                            } else {
                                side[0] = u((s(side[0]) + ((s(side[0]) >> 31) & 3)) >> 2);
                                side[2] = u((s(side[2]) + ((s(side[2]) >> 31) & 3)) >> 2);
                                first_side = probe(quarter[0] - side[0], quarter[1], quarter[2] - side[2], 0xf000);
                                second_side = probe(side[0] + quarter[0], quarter[1], side[2] + quarter[2], 0xf000);
                                if (second_side != first_side) {
                                    if (second_side) {
                                        write(0x2bfc, 2);
                                    }
                                    if (first_side) {
                                        write(0x2bfc, 1);
                                    }
                                } else {
                                    write(0x2bfc, 0);
                                }
                            }
                            set_global_word(m_, kQueryFlag, 0);
                        }
                    } else {
                        write(0x2ea4, u(score));
                        write(0x2e94, 0);
                    }
                }
            }
            if (current == u(sext16(half_at(m_, record + 6)))) {
                break;
            }
            node = current;
            current = rail_.next_node(current) & 0xffff;
        }
    }
}

}  // namespace opentony::runtime::skaterkit
