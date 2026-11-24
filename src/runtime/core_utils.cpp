#include "core_views.hpp"
#include "../trg/retail_random.hpp"
#include "host_options.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

// Phase 4.14c: utils.cpp, the vector helpers and the angle math. Every routine works on retail memory at the addresses the callers hand it; the Q12 trigonometry, the arctangent and the GTE are the
// 4.14a MathKit, the square root (0x004f53b0, an x87 fsqrt that other code owns) and the collision queries are services.

namespace opentony::runtime::core {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kInfoScratch = pcplatform::kScratchBase + 0x400;     // where the port builds the query record the retail code keeps in a stack local

inline Word mul(Word a, Word b) { return a * b; }
inline std::int32_t sar(std::int32_t v, unsigned n) { return v >> n; }

}  // namespace

// 0x004f53b0: the truncated square root. The 4.14c replays take it from the service; a kit that owns the function (4.15a ported it) asks for the native one (`use_native_square_root`).
Word CoreKit::square_root(Word value) {
    if (!native_square_root_) {
        return engine_value(kIntegerSqrt, {value});
    }
    const auto signed_value = static_cast<std::int32_t>(value);
    if (signed_value <= 0) {
        return 0;
    }
    auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(signed_value)));
    while (root * root > signed_value) {
        --root;
    }
    while ((root + 1) * (root + 1) <= signed_value) {
        ++root;
    }
    return static_cast<Word>(root);
}

void CoreKit::fail_division(const char* where) const {
    throw std::runtime_error(std::string("division by zero in ") + where);
}

void CoreKit::initial_rand(Word seed) {
    set_global_word(m_, globals::kRandA, 0x12b9b0a1);
    set_global_word(m_, globals::kRandState, seed);
    set_global_word(m_, globals::kRandB, 0x0aa2fb3f);
}

// 0x004c9340 (milestone 9: the cold boot reaches it through Career_ResetSaveImage): the generator of src/trg/retail_random.hpp, whose state is these three words
std::int32_t CoreKit::random(std::int32_t range) {
    trg::RetailRandom generator({global_word(m_, globals::kRandState), global_word(m_, globals::kRandA), global_word(m_, globals::kRandB)});
    const std::int32_t result = generator.next(range);
    set_global_word(m_, globals::kRandState, generator.state().a);
    set_global_word(m_, globals::kRandA, generator.state().b);
    set_global_word(m_, globals::kRandB, generator.state().c);
    return result;
}

// 0x004c9390: the input is read again after the first store (an output that is also the input sees the new x)
void CoreKit::rotate_y(Word out, Word in, std::int32_t angle) {
    const RuntimeIntVector3View source(m_, in);
    const RuntimeIntVector3View result(m_, out);
    const std::int32_t cos1 = math_.cos_q12(angle);
    const std::int32_t x1 = sar(source.x(), 3);
    const std::int32_t sin1 = math_.sin_q12(angle);
    const std::int32_t z1 = sar(source.z(), 3);
    result.set_x(sar(s(mul(u(cos1), u(x1)) + mul(u(sin1), u(z1))), 9));
    result.set_y(source.y());
    const std::int32_t sin2 = math_.sin_q12(angle);
    const std::int32_t x2 = sar(source.x(), 3);
    const std::int32_t cos2 = math_.cos_q12(angle);
    const std::int32_t z2 = sar(source.z(), 3);
    result.set_z(sar(s(mul(u(cos2), u(z2)) - mul(u(sin2), u(x2))), 9));
}

// 0x004c9400
void CoreKit::rotate_y_q12(Word out, Word in, std::int32_t angle) {
    const RuntimeIntVector3View source(m_, in);
    const RuntimeIntVector3View result(m_, out);
    const std::int32_t cos1 = math_.cos_q12(angle);
    const std::int32_t x1 = sar(source.x(), 12);
    const std::int32_t sin1 = math_.sin_q12(angle);
    const std::int32_t z1 = sar(source.z(), 12);
    result.set_x(s(mul(u(cos1), u(x1)) + mul(u(sin1), u(z1))));
    result.set_y(source.y());
    const std::int32_t sin2 = math_.sin_q12(angle);
    const std::int32_t x2 = sar(source.x(), 12);
    const std::int32_t cos2 = math_.cos_q12(angle);
    const std::int32_t z2 = sar(source.z(), 12);
    result.set_z(s(mul(u(cos2), u(z2)) - mul(u(sin2), u(x2))));
}

// 0x004c9470: the vector in whole units goes through the GTE's square (IR1..3 -> MAC), the sum is square-rooted by the service, and the vector is divided by that length
void CoreKit::unit_vector(Word out, Word in) {
    const RuntimeIntVector3View source(m_, in);
    const RuntimeIntVector3View result(m_, out);
    const RuntimeGteRegisterFileView gte = gte_registers(m_);
    const Word x = ux(source);
    const Word y = uy(source);
    const Word z = uz(source);
    gte.ir().set_x(static_cast<std::int16_t>(sar(s(x), 12)));
    gte.ir().set_y(static_cast<std::int16_t>(sar(s(y), 12)));
    gte.ir().set_z(static_cast<std::int16_t>(sar(s(z), 12)));
    math_.sqr0();
    const Word sum = uy(gte.mac()) + uz(gte.mac()) + ux(gte.mac());
    const std::int32_t length = s(square_root(sum));
    if (length == 0) {
        set_vector(result, 0, 0, 0);
        return;
    }
    // (idiv: INT_MIN / -1 faults in retail; the length is a square root, never negative)
    result.set_x(source.x() / length);
    result.set_y(source.y() / length);
    result.set_z(source.z() / length);
}

// 0x004c9500
void CoreKit::direction_from_angles(Word out, std::int32_t scale, Word angles) {
    const RuntimeShortVector3View look(m_, angles);
    const RuntimeIntVector3View result(m_, out);
    const std::int32_t pitch = static_cast<std::int32_t>(u16_of(look.x())) & 0xfff;
    const std::int32_t yaw = static_cast<std::int32_t>(u16_of(look.y())) & 0xfff;
    const std::int32_t cos_pitch = math_.cos_q12(pitch);
    const std::int32_t sin_yaw = math_.sin_q12(yaw);
    result.set_x(s(0u - mul(u(sar(s(mul(u(cos_pitch), u(scale))), 12)), u(sin_yaw))));
    const std::int32_t sin_pitch = math_.sin_q12(static_cast<std::int32_t>(u16_of(look.x()) & 0xfff));
    result.set_y(s(mul(u(sin_pitch), u(scale))));
    const std::int32_t cos_pitch2 = math_.cos_q12(static_cast<std::int32_t>(u16_of(look.x()) & 0xfff));
    const std::int32_t cos_yaw = math_.cos_q12(static_cast<std::int32_t>(u16_of(look.y()) & 0xfff));
    result.set_z(s(0u - mul(u(sar(s(mul(u(cos_pitch2), u(scale))), 12)), u(cos_yaw))));
}

// 0x004c9590 / 0x004c95e0: the distance in whole units, squared, summed and square-rooted by the service
Word CoreKit::distance(Word a, Word b) {
    const RuntimeIntVector3View first(m_, a);
    const RuntimeIntVector3View second(m_, b);
    const std::int32_t dx = sar(s(ux(first) - ux(second)), 12);
    const std::int32_t dy = sar(s(uy(first) - uy(second)), 12);
    const std::int32_t dz = sar(s(uz(first) - uz(second)), 12);
    const Word sum = mul(u(dz), u(dz)) + mul(u(dy), u(dy)) + mul(u(dx), u(dx));
    return square_root(sum);
}

Word CoreKit::distance_xz(Word a, Word b) {
    const RuntimeIntVector3View first(m_, a);
    const RuntimeIntVector3View second(m_, b);
    const std::int32_t dx = sar(s(ux(first) - ux(second)), 12);
    const std::int32_t dz = sar(s(uz(first) - uz(second)), 12);
    const Word sum = mul(u(dz), u(dz)) + mul(u(dx), u(dx));
    return square_root(sum);
}

// 0x004c9620 (Utils_CrapDist): the octagonal approximation, in whole units
Word CoreKit::crap_distance(Word a, Word b) {
    const RuntimeIntVector3View first(m_, a);
    const RuntimeIntVector3View second(m_, b);
    std::int32_t x = s(ux(first) - ux(second));
    std::int32_t y = s(uy(first) - uy(second));
    std::int32_t z = s(uz(first) - uz(second));
    if (x < 0) {
        x = s(0u - u(x));
    }
    if (y < 0) {
        y = s(0u - u(y));
    }
    if (z < 0) {
        z = s(0u - u(z));
    }
    if (y <= x) {
        if (x < z) {
            return u(sar(sar(x, 1) + z + sar(y, 2), 12));
        }
        if (z < y) {
            return u(sar(sar(y, 1) + x + sar(z, 2), 12));
        }
        return u(sar(sar(y, 2) + x + sar(z, 1), 12));
    }
    if (y < z) {
        return u(sar(sar(x, 2) + z + sar(y, 1), 12));
    }
    if (z < x) {
        return u(sar(sar(x, 1) + y + sar(z, 2), 12));
    }
    return u(sar(sar(x, 2) + y + sar(z, 1), 12));
}

// 0x004c96d0: the cosine (Q12) of the angle at b between a and c, from the dot product of the whole-unit offsets and the two octagonal distances; 10000 when either is farther than 0x9c40
Word CoreKit::cosine_between(Word a, Word b, Word c) {
    const Word first = crap_distance(a, b);
    const Word second = crap_distance(c, b);
    if (first > 0x9c40 || second > 0x9c40) {
        return 10000;
    }
    const RuntimeIntVector3View pa(m_, a);
    const RuntimeIntVector3View pb(m_, b);
    const RuntimeIntVector3View pc(m_, c);
    const std::int32_t ay = sar(s(uy(pa) - uy(pb)), 12);
    const std::int32_t cy = sar(s(uy(pc) - uy(pb)), 12);
    const std::int32_t az = sar(s(uz(pa) - uz(pb)), 12);
    const std::int32_t cz = sar(s(uz(pc) - uz(pb)), 12);
    const std::int32_t ax = sar(s(ux(pa) - ux(pb)), 12);
    const std::int32_t cx = sar(s(ux(pc) - ux(pb)), 12);
    const Word dot = mul(u(ay), u(cy)) + mul(u(az), u(cz)) + mul(u(ax), u(cx));
    if (first == 0 || second == 0) {
        fail_division("Utils_VectorCosineQ12");
    }
    return ((dot / first) << 12) / second;
}

// 0x004c9770: the pitch (angles[0]) and yaw (angles[1]) of the line from one point to another, both in 4096 per turn, and the length of its horizontal part
Word CoreKit::look_angles(Word angles, Word from, Word to) {
    const RuntimeShortVector3View look(m_, angles);
    const RuntimeIntVector3View start(m_, from);
    const RuntimeIntVector3View end(m_, to);
    const std::int32_t dx = sar(s(ux(end) - ux(start)), 12);
    const std::int32_t dy = sar(s(uy(end) - uy(start)), 12);
    const std::int32_t dz = sar(s(uz(end) - uz(start)), 12);
    if (dz == 0) {
        look.set_y(static_cast<std::int16_t>(dx > 0 ? 0xfc00u : 0x400u));
    } else if (dz > 0) {
        const std::int32_t slope = s(u(dx) << 12) / dz;
        const std::int32_t angle = math_.atan_slope(s(0u - u(slope)));
        look.set_y(static_cast<std::int16_t>(u(0x800 - angle)));
    } else {
        const std::int32_t slope = s(u(dx) << 12) / dz;
        look.set_y(static_cast<std::int16_t>(u(math_.atan_slope(slope))));
    }
    const Word sum = mul(u(dz), u(dz)) + mul(u(dx), u(dx));
    const Word length = square_root(sum);
    if (length == 0) {
        look.set_x(static_cast<std::int16_t>(dy <= 0 ? u(-0x400) : 0x400u));
    } else if (dy > 0) {
        look.set_x(static_cast<std::int16_t>(u(math_.atan_slope(s((u(dy) << 12) / length)))));
    } else {
        look.set_x(static_cast<std::int16_t>(0u - u(math_.atan_slope(s(((0u - u(dy)) << 12) / length)))));
    }
    look.set_x(static_cast<std::int16_t>(u16_of(look.x()) & 0xfff));
    look.set_y(static_cast<std::int16_t>(u16_of(look.y()) & 0xfff));
    look.set_z(0);
    return length;
}

// 0x004c9880: two 3-short angle records are passed by value (the first two shorts in one stack word, the third in the next); the turn needed from the first to the second, wrapped to a half turn, scaled
void CoreKit::turn_towards(Word from_xy, Word /*from_z*/, Word zero_out, Word out, Word to_xy, Word /*to_z*/, std::int32_t scale) {
    auto lo = [](Word w) { return static_cast<std::int16_t>(w & 0xffff); };
    auto hi = [](Word w) { return static_cast<std::int16_t>(w >> 16); };
    std::int16_t a = static_cast<std::int16_t>(lo(to_xy) - lo(from_xy));
    std::int16_t b = static_cast<std::int16_t>(hi(to_xy) - hi(from_xy));
    if (a < -0x800) {
        a = static_cast<std::int16_t>(a + 0x1000);
    }
    if (a > 0x800) {
        a = static_cast<std::int16_t>(a - 0x1000);
    }
    if (b < -0x800) {
        b = static_cast<std::int16_t>(b + 0x1000);
    }
    if (b > 0x800) {
        b = static_cast<std::int16_t>(b - 0x1000);
    }
    if (a == 0 && b == 0) {
        const RuntimeShortVector3View cleared(m_, zero_out);
        cleared.set_x(0);
        cleared.set_y(0);
        return;
    }
    const RuntimeShortVector3View turn(m_, out);
    turn.set_x(static_cast<std::int16_t>(sar(s(mul(u(a), u(scale))), 8)));
    turn.set_y(static_cast<std::int16_t>(sar(s(mul(u(b), u(scale))), 8)));
}

void CoreKit::clear_explosion_stack() { set_global_word(m_, globals::kExplosionStack, 0); }

// 0x004c9f50: equal ignoring ASCII case over at most `count` characters (or up to the terminator); two null names are equal, one null is not
Word CoreKit::name_equal(Word a, Word b, Word count) {
    if (a == 0) {
        return b == 0 ? 1u : 0u;
    }
    if (b == 0) {
        return 0;
    }
    char first;
    char second;
    do {
        first = static_cast<char>(byte_at(m_, a));
        second = static_cast<char>(byte_at(m_, b));
        ++a;
        ++b;
        if (first > '@' && first < '[') {
            first = static_cast<char>(first + ' ');
        }
        if (second > '@' && second < '[') {
            second = static_cast<char>(second + ' ');
        }
        if (first != second) {
            return 0;
        }
    } while (first != '\0' && --count != 0);
    return first == second ? 1u : 0u;
}

// the difference of two angles wrapped into [-0x800, 0x800]
std::int32_t CoreKit::wrap_half_turn(std::int32_t angle) const {
    if (angle < -0x800) {
        angle += 0x1000;
    }
    if (angle > 0x800) {
        angle -= 0x1000;
    }
    return angle;
}

// 0x004c9fd0: the distance to the target if it is within the limits (the angle limits against the object's angles at +0x14 and +0x16), else 0
Word CoreKit::platform_ai(Word object, Word target, std::int32_t pitch_limit, std::int32_t yaw_limit, std::int32_t distance_limit) {
    const Word angles = pcplatform::kScratchBase + 0x40;
    const RuntimeShortVector3View look(m_, angles);
    const RuntimeListObjectView self = object_at(m_, object);
    look.set_x(0);
    look.set_y(0);
    look.set_z(0);
    const std::int32_t reach = s(look_angles(angles, self.position_address(), object_at(m_, target).position_address()));
    if (reach > distance_limit) {
        return 0;
    }
    std::int32_t gap = wrap_half_turn(look.y() - self.yaw_angle());
    if (gap < 0) {
        gap = -gap;
    }
    if (gap > pitch_limit) {
        return 0;
    }
    gap = wrap_half_turn(look.x() - self.pitch_angle());
    if (gap < 0) {
        gap = -gap;
    }
    if (gap > yaw_limit) {
        return 0;
    }
    return u(reach == 0 ? 1 : reach);
}

// 0x004ca0a0: the arccosine of value / 4096 in 4096 per turn (MathKit's, which the skater's ground physics shares)
Word CoreKit::acos_ratio_to_angle(std::int32_t value) { return math_.acos_ratio_to_angle(value); }

// the zone query (a service) fills in three fields of the record the caller keeps in a local: the body it hit, the slope of the surface and the height; the probe scripts the three as dwords
// in that order
void CoreKit::query_result(Word info) {
    const auto name = pcplatform::engine_name(kZoneCollision);
    if (s_.live()) {
        return;
    }
    const SLineInfoView query(m_, info);
    query.set_hit_body(GuestPtr<guest::RuntimeCollisionObject>{u(s_.value(name))});
    query.set_hit_normal(1, static_cast<std::int16_t>(u(s_.value(name))));
    query.set_hit_position(1, static_cast<std::int32_t>(u(s_.value(name))));
}

// 0x004ca0c0 / 0x004ca140: the collision query record is built in a local; the zone query fills in the hit
Word CoreKit::line_of_sight(Word a, Word b) {
    const Word info = kInfoScratch;
    const SLineInfoView query(m_, info);
    const RuntimeIntVector3View from(m_, a);
    const RuntimeIntVector3View to(m_, b);
    query.set_start_x(from.x());
    query.set_start_y(from.y());
    query.set_start_z(from.z());
    query.set_end_x(to.x());
    query.set_end_y(to.y());
    query.set_end_z(to.z());
    set_global_word(m_, globals::kCollisionBusy, 1);
    engine(kInitCollisionQuery, {info});
    engine(kZoneCollision, {info, 1});
    query_result(info);
    const bool clear = query.hit_body().address == 0;
    set_global_word(m_, globals::kCollisionBusy, 0);
    return clear ? 1u : 0u;
}

Word CoreKit::ground_height(Word position, std::int32_t up, std::int32_t down) {
    const Word info = kInfoScratch;
    const SLineInfoView query(m_, info);
    const RuntimeIntVector3View at(m_, position);
    const Word x = ux(at);
    const Word y = uy(at);
    const Word z = uz(at);
    query.set_start_x(static_cast<std::int32_t>(x));
    query.set_start_z(static_cast<std::int32_t>(z));
    query.set_end_y(static_cast<std::int32_t>((u(down) << 12) + y));
    query.set_start_y(static_cast<std::int32_t>(y - (u(up) << 12)));
    query.set_end_x(static_cast<std::int32_t>(x));
    query.set_end_z(static_cast<std::int32_t>(z));
    engine(kInitCollisionQuery, {info});
    query.set_query_mask_mode(0);
    engine(kZoneCollision, {info, 1});
    query_result(info);
    if (query.hit_body().address != 0 && query.hit_normal(1) <= -0xc00) {
        return u(query.hit_position(1));
    }
    return 0xffffffffu;
}

// 0x004ca1e0: every object of a list inside the box (the object's own test first): killed through virtual slot 2, or deleted through slot 0 with a flag
Word CoreKit::kill_objects_in_box(Word low, Word high, Word list, Word destroy) {
    const RuntimeIntVector3View min(m_, low);
    const RuntimeIntVector3View max(m_, high);
    Word count = 0;
    Word object = list;
    while (object != 0) {
        const Word next = next_object(m_, object);
        if (engine_value(0x004805f0, {object}) == 0) {
            const RuntimeIntVector3View at = object_at(m_, object).position();
            const std::int32_t x = at.x();
            const std::int32_t y = at.y();
            const std::int32_t z = at.z();
            if (!(x < min.x() || x > max.x() || z < min.z() || z > max.z() || y < min.y() || y > max.y())) {
                if ((destroy & 0xff) != 0) {
                    virtual_call(object, 2);
                } else {
                    virtual_call(object, 0, {1});
                }
                ++count;
            }
        }
        object = next;
    }
    return count;
}

Word CoreKit::apply_bounds_to_lists(Word low, Word high) {
    Word total = kill_objects_in_box(low, high, global_word(m_, globals::kBaddyList), 1);
    total += kill_objects_in_box(low, high, global_word(m_, globals::kGameList), 1);
    total += kill_objects_in_box(low, high, global_word(m_, globals::kPowerUpList), 1);
    total += kill_objects_in_box(low, high, global_word(m_, globals::kActiveList), 0);
    return total;
}

// 0x004ca2d0: the visibility word (+4) of every object of five lists is set or cleared when the object lies inside (`inside`) or outside the box. The bits: 0x41 / 0x01 / 0x40 (which
// owner the sweep acts for), cleared when `show` is set. The last list holds type-2 objects whose flag byte (+0x4a) is the result.
void CoreKit::update_visibility_bounds(Word low, Word high, Word show, Word inside) {
    const RuntimeIntVector3View min(m_, low);
    const RuntimeIntVector3View max(m_, high);
    const std::int32_t min_x = min.x();
    const std::int32_t min_y = min.y();
    const std::int32_t min_z = min.z();
    const std::int32_t max_x = max.x();
    const std::int32_t max_y = max.y();
    const std::int32_t max_z = max.z();
    const bool hide_mode = (show & 0xff) != 0;
    const bool inside_mode = (inside & 0xff) != 0;
    std::uint32_t set_bits;
    if (global_word(m_, globals::kVisibilityOwner) == 0 || global_word(m_, globals::kNumberOfPlayers) == 1 || global_word(m_, globals::kGameMode) == 7) {
        set_bits = 0x41;
    } else {
        set_bits = global_word(m_, globals::kVisibilityOwner) != global_word(m_, globals::kPlayerRecord) ? 0x0001u : 0x0040u;
    }
    std::uint32_t keep = ~set_bits;
    if (hide_mode) {
        set_bits = 0;
    } else {
        keep = 0xffffffffu;
    }
    auto is_inside = [&](const RuntimeIntVector3View& at) {
        const std::int32_t x = at.x();
        const std::int32_t y = at.y();
        const std::int32_t z = at.z();
        return !(x < min_x || x > max_x || z < min_z || z > max_z || y < min_y || y > max_y);
    };
    auto is_outside = [&](const RuntimeIntVector3View& at) {
        const std::int32_t x = at.x();
        const std::int32_t y = at.y();
        const std::int32_t z = at.z();
        return (x < min_x || x > max_x) && (z < min_z || z > max_z) && (y < min_y || y > max_y);
    };
    auto touch = [&](const RuntimeListObjectView& object) { object.set_visibility_flags(static_cast<std::uint16_t>((keep & object.visibility_flags()) | set_bits)); };
    // host side only: a box round a single object (an event's, Hangar's chopper debris) is not an area's (HostOptions::draw_hidden_areas, area_sized_box; whole units)
    const bool area_box = area_sized_box(min_x >> 12, min_z >> 12, max_x >> 12, max_z >> 12);
    for (Word at = global_word(m_, globals::kEnvironmentList); at != 0; at = next_object(m_, at)) {
        const RuntimeListObjectView object = object_at(m_, at);
        if (region_slot(m_, object.region()).region_type() == 0) {
            continue;
        }
        if (inside_mode ? is_inside(object.position()) : is_outside(object.position())) {
            touch(object);
            note_area_visibility(at, object.visibility_flags(), area_box);   // host side only (HostOptions::draw_hidden_areas)
        }
    }
    for (Word list : {globals::kPowerUpList, globals::kActiveList, globals::kGameList}) {
        for (Word at = global_word(m_, list); at != 0; at = next_object(m_, at)) {
            const RuntimeListObjectView object = object_at(m_, at);
            if (inside_mode ? is_inside(object.position()) : is_outside(object.position())) {
                touch(object);
            }
        }
    }
    for (Word at = global_word(m_, globals::kRailList); at != 0;) {
        const RuntimeTypeTaggedListNodeView node(m_, at);
        if (node.kind() != 2) {
            at = node.next().address;
            continue;
        }
        if (inside_mode ? is_inside(node.position()) : is_outside(node.position())) {
            if ((show & 0xff) != 0) {
                node.set_result_flag(1);
            } else if (global_word(m_, globals::kNumberOfPlayers) == 1) {
                node.set_result_flag(0);
            }
        }
        at = node.next().address;
    }
}

// 0x004ca690: the sound level of a source at `position` for the listener of the viewport being drawn: full inside `near_distance`, none beyond `far_distance`, a linear fade between; the low
// half is the left level and the high half the right level (the source's bearing against the listener's rotation), bit 31 set when the source is behind
Word CoreKit::spatial_attenuation(Word position, std::int32_t near_distance, std::int32_t far_distance) {
    const RuntimeViewportRecordView view = viewport(m_, global_word(m_, globals::kViewIndex));
    const RuntimeIntVector3View camera = view.camera_position();
    const Word listener_at = pcplatform::kScratchBase + 0x20;
    const RuntimeIntVector3View listener(m_, listener_at);
    set_vector(listener, ux(camera) << 12, uy(camera) << 12, uz(camera) << 12);
    const std::int32_t reach = s(crap_distance(position, listener_at));
    if (reach <= near_distance) {
        return 0x0fff0fff;
    }
    if (reach >= far_distance) {
        return 0;
    }
    const std::int32_t gap = far_distance - reach;
    std::int32_t level = s((u(gap) << 12) - u(gap)) / far_distance;       // (gap * 0xfff) / far; far is positive here (reach is not)
    const RuntimeGteRegisterFileView gte = gte_registers(m_);
    for (Word k = 0; k < 9; ++k) {
        gte.rotation().set_elements(k, view.rotation().elements(k));
    }
    const RuntimeIntVector3View source(m_, position);
    const std::int32_t view_x = camera.x();
    const std::int32_t view_y = camera.y();
    const std::int32_t view_z = camera.z();
    gte.ir().set_x(static_cast<std::int16_t>(sar(source.x(), 12) - view_x));
    gte.ir().set_y(static_cast<std::int16_t>(sar(source.y(), 12) - view_y));
    gte.ir().set_z(static_cast<std::int16_t>(sar(source.z(), 12) - view_z));
    math_.rtir();
    const std::int32_t ir1 = gte.ir().x();
    const std::int32_t ir3 = gte.ir().z();
    if (ir1 == 0 && ir3 == 0) {
        return (u(level) << 16) | u(level);
    }
    const std::int32_t bearing = math_.ratan2(ir3, ir1);
    const std::int32_t angle = (0x400 - bearing) & 0xfff;
    Word behind = 0;
    if (angle > 0x400 && angle < 0xc00) {
        behind = 0x80000000u;
        level -= level >> 4;
    }
    if (angle < 0x800) {
        const std::int32_t sine = math_.sin_q12(angle);
        return (u(level) << 16) | (u(level) - u(sar(s(mul(u(sine), u(level))), 12))) | behind;
    }
    const std::int32_t sine = math_.sin_q12(angle);
    return ((u(sar(s(mul(u(sine), u(level))), 12)) + u(level)) << 16) | u(level) | behind;
}

// 0x004ca890: a triangle pulse: rises from 0 at 0 to `scale` at half the period and falls back to 0 at the period; 0 outside [0, period)
Word CoreKit::pulse(std::int32_t value, std::int32_t period, std::int32_t scale) {
    if (value < period && value >= 0) {
        const std::int32_t half = period / 2;
        if (half == 0) {
            fail_division("Utils_Pulse");
        }
        if (value < half) {
            return u(s(mul(u(value), u(scale))) / half);
        }
        return u(s(mul(u(period - value), u(scale))) / half);
    }
    return 0;
}

void CoreKit::limit_range(Word value, std::int32_t low, std::int32_t high) {
    if (s(word_at(m_, value)) < low) {
        set_word_at(m_, value, u(low));
    }
    if (high < s(word_at(m_, value))) {
        set_word_at(m_, value, u(high));
    }
}

// 0x004ca8f0 / 0x004ca940: the squared length through the GTE (IR1..3 are the vector in whole units), the length from the service
Word CoreKit::vector_squared_length(Word vector) {
    const RuntimeIntVector3View v(m_, vector);
    const RuntimeGteRegisterFileView gte = gte_registers(m_);
    gte.ir().set_x(static_cast<std::int16_t>(sar(v.x(), 12)));
    gte.ir().set_y(static_cast<std::int16_t>(sar(v.y(), 12)));
    gte.ir().set_z(static_cast<std::int16_t>(sar(v.z(), 12)));
    math_.sqr0();
    return uy(gte.mac()) + uz(gte.mac()) + ux(gte.mac());
}

Word CoreKit::vector_length(Word vector) { return square_root(vector_squared_length(vector)); }

// 0x004ca980: divide by the length (when positive); the retail z is the already divided y divided once more
void CoreKit::vector_normalize(Word vector) {
    const RuntimeIntVector3View v(m_, vector);
    const std::int32_t length = s(vector_length(vector));
    if (length > 0) {
        v.set_x(v.x() / length);
        const std::int32_t y = v.y() / length;
        v.set_y(y);
        v.set_z(y / length);
    }
}

// 0x004ca9b0: components within +-0x800 become 0
void CoreKit::vector_dead_zone(Word vector) {
    const RuntimeIntVector3View v(m_, vector);
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t c = s(component(v, k));
        if (c > -0x801 && c < 0x801) {
            set_component(v, k, 0);
        }
    }
}

Word CoreKit::vector_add_out(Word out, Word a, Word b) {
    const RuntimeIntVector3View first(m_, a);
    const RuntimeIntVector3View second(m_, b);
    const Word x = ux(first) + ux(second);
    const Word y = uy(first) + uy(second);
    const Word z = uz(first) + uz(second);
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::vector_subtract_out(Word out, Word a, Word b) {
    const RuntimeIntVector3View first(m_, a);
    const RuntimeIntVector3View second(m_, b);
    const Word x = ux(first) - ux(second);
    const Word y = uy(first) - uy(second);
    const Word z = uz(first) - uz(second);
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::vector_mul_out(Word out, Word vector, Word scalar) {
    const RuntimeIntVector3View v(m_, vector);
    const Word k = word_at(m_, scalar);
    const Word x = ux(v) * k;
    const Word y = uy(v) * k;
    const Word z = uz(v) * k;
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::scalar_mul_out(Word out, Word scalar, Word vector) {
    const RuntimeIntVector3View v(m_, vector);
    const Word k = word_at(m_, scalar);
    const Word x = k * ux(v);
    const Word y = uy(v) * k;
    const Word z = uz(v) * k;
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::vector_div_out(Word out, Word vector, Word scalar) {
    const RuntimeIntVector3View v(m_, vector);
    const std::int32_t k = s(word_at(m_, scalar));
    if (k == 0) {
        fail_division("Math_Vector3DivScalarOut");
    }
    const std::int32_t x = v.x() / k;
    const std::int32_t y = v.y() / k;
    const std::int32_t z = v.z() / k;
    set_vector(RuntimeIntVector3View(m_, out), u(x), u(y), u(z));
    return out;
}

Word CoreKit::vector_shift_right_out(Word out, Word vector, Word count) {
    const RuntimeIntVector3View v(m_, vector);
    const unsigned n = word_at(m_, count) & 0x1f;
    const std::int32_t x = v.x() >> n;
    const std::int32_t y = v.y() >> n;
    const std::int32_t z = v.z() >> n;
    set_vector(RuntimeIntVector3View(m_, out), u(x), u(y), u(z));
    return out;
}

Word CoreKit::vector_shift_left_out(Word out, Word vector, Word count) {
    const RuntimeIntVector3View v(m_, vector);
    const unsigned n = word_at(m_, count) & 0x1f;
    const Word x = ux(v) << n;
    const Word y = uy(v) << n;
    const Word z = uz(v) << n;
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::vector_negate_out(Word vector, Word out) {
    const RuntimeIntVector3View v(m_, vector);
    const Word x = 0u - ux(v);
    const Word y = 0u - uy(v);
    const Word z = 0u - uz(v);
    set_vector(RuntimeIntVector3View(m_, out), x, y, z);
    return out;
}

Word CoreKit::vector_mask12(Word vector) {
    const RuntimeShortVector3View v(m_, vector);
    v.set_x(static_cast<std::int16_t>(u16_of(v.x()) & 0xfff));
    v.set_y(static_cast<std::int16_t>(u16_of(v.y()) & 0xfff));
    v.set_z(static_cast<std::int16_t>(u16_of(v.z()) & 0xfff));
    return 0xfff;
}


// 0x004ca620: is the object one of the environment list
Word CoreKit::is_environment_object(Word object) {
    for (Word at = global_word(m_, globals::kEnvironmentList); at != 0; at = next_object(m_, at)) {
        if (at == object) {
            return 1;
        }
    }
    return 0;
}

// 0x004ca650: a shuffle: every element is swapped with the one the random number names
void CoreKit::shuffle_shorts(Word array, std::int32_t count) {
    for (std::int32_t k = 0; k < count; ++k) {
        const Word at = array + 2 * static_cast<Word>(k);
        const std::uint16_t first = read_scalar<std::uint16_t>(m_, at);
        const Word other = array + 2 * static_cast<Word>(rnd(static_cast<Word>(count)));
        const std::uint16_t second = read_scalar<std::uint16_t>(m_, other);
        write_scalar<std::uint16_t>(m_, at, second);
        write_scalar<std::uint16_t>(m_, other, first);
    }
}

// 0x004c9c90: every object of a list without bit 6 of its flag byte (+0x6a) gets bit 2 of the flag word at +0x68
void CoreKit::flag_list_objects(Word head) {
    for (Word at = head; at != 0; at = next_object(m_, at)) {
        const RuntimeListObjectView object = object_at(m_, at);
        if ((object.protect_flags() & 0x40) == 0) {
            object.set_list_flags(static_cast<std::uint16_t>(object.list_flags() | 4));
        }
    }
}

// 0x004c9fb0
void CoreKit::copy_c_string(Word source, Word destination) {
    std::uint8_t c;
    do {
        c = byte_at(m_, source);
        set_byte_at(m_, destination, c);
        ++source;
        ++destination;
    } while (c != 0);
}

}  // namespace opentony::runtime::core
