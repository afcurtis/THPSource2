// Native model of the traffic car object (type-1 subtypes 0xd5..0xdc; car.cpp).
// Phase 4.4c.
//
// Addresses: TRG_ConstructTrafficObject 0x00412640, TRG_UpdateTrafficObject
// 0x00412960 (vtable 0x005184e0 slot +0x0c), TRG_CheckTrafficProximity
// 0x004136e0, Math_NormalizeFixed12 0x00465f60, orig_ratan2 0x004e8560,
// FUN_00412930 (wrapped angle difference), FUN_004f5f50 (sign of a 2D cross
// product).
#include "object_script.hpp"

#include "world_objects.hpp"

#include "../camera/camera_math.hpp"

#include <cmath>
#include <cstdint>

namespace opentony::trg {
namespace {

[[nodiscard]] std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
[[nodiscard]] std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
[[nodiscard]] std::int32_t sar(std::int32_t value, unsigned count) noexcept {
    return value >= 0 ? static_cast<std::int32_t>(static_cast<std::uint32_t>(value) >> count)
                      : static_cast<std::int32_t>(~(~static_cast<std::uint32_t>(value) >> count));
}
[[nodiscard]] std::int32_t shl(std::int32_t value, unsigned count) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << count);
}
[[nodiscard]] std::int32_t iabs(std::int32_t value) noexcept {
    const std::int32_t mask = sar(value, 31);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value ^ mask) - static_cast<std::uint32_t>(mask));
}
// 0x004f5fc0
[[nodiscard]] std::int32_t q12_mul(std::int32_t a, std::int32_t b) noexcept {
    return camera::truncate_x87_result(static_cast<long double>(a) * static_cast<long double>(b) * 0.000244140625L);
}
// 0x004f5f90
[[nodiscard]] std::int32_t q12_dot(const ScriptVec3& a, const ScriptVec3& b) noexcept {
    const long double sum = static_cast<long double>(a[2]) * b[2] + static_cast<long double>(a[1]) * b[1]
        + static_cast<long double>(a[0]) * b[0];
    return camera::truncate_x87_result(sum * 0.000244140625L);
}
// 0x004f5f50: sign of (a*b + c*d) / 4096.
[[nodiscard]] int cross_sign(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) noexcept {
    const long double sum = static_cast<long double>(a) * b + static_cast<long double>(c) * d;
    const std::int32_t value = camera::truncate_x87_result(sum * 0.000244140625L);
    return value == 0 ? 0 : (value > 0 ? 1 : -1);
}
// 0x004e8560: atan2(a, b) in 4096ths of a turn, truncated.
[[nodiscard]] std::int32_t ratan2(std::int32_t a, std::int32_t b) noexcept {
    return camera::truncate_x87_result(
        std::atan2(static_cast<long double>(a), static_cast<long double>(b)) * 651.89862876367124L);
}
// FUN_00412930: target - current wrapped into [-0x800, 0x7ff].
[[nodiscard]] std::int32_t angle_difference(std::int32_t current, std::int32_t target) noexcept {
    std::int32_t value = target - current;
    if (value < -0x800) {
        value += 0x1000;
    }
    if (value > 0x7ff) {
        value -= 0x1000;
    }
    return value;
}
[[nodiscard]] std::int32_t isqrt_positive(std::int32_t sum) noexcept {
    return sum <= 0 ? 0 : static_cast<std::int32_t>(camera::isqrt_u32_x87(static_cast<std::uint32_t>(sum)));
}

// Math_NormalizeFixed12 (0x00465f60): scale a vector down until every
// component is within 0x6882, then to unit length in Q12 (or (0x1000, 0, 0)).
void normalize_fixed12(ScriptVec3& v) noexcept {
    const auto out_of_range = [&v]() {
        for (const std::int32_t component : v) {
            if (component > 0x6882 || component < -0x6882) {
                return true;
            }
        }
        return false;
    };
    while (out_of_range()) {
        for (std::int32_t& component : v) {
            component = sar(component, 1);
        }
    }
    std::int32_t sum = 0;
    for (const std::int32_t component : v) {
        const std::int32_t narrow = static_cast<std::int16_t>(component);
        sum = wrap_add(sum, narrow * narrow);
    }
    const std::int32_t length = isqrt_positive(sum);
    if (length != 0) {
        for (std::int32_t& component : v) {
            component = shl(component, 12) / length;
        }
        return;
    }
    v = {0x1000, 0, 0};
}

constexpr std::int32_t kRoadDrop = 0x6e000;  // Y offset of the road surface below the node

} // namespace

ScriptObject ScriptObject::construct_traffic(
    std::uint16_t node, std::uint16_t subtype, std::int32_t road_node, ScriptVec3 position,
    std::array<std::uint16_t, 3> params, std::uint32_t sequence) {
    ScriptObject object;
    object.node = node;
    object.subtype = subtype;
    object.position = position;
    object.params = params;
    // Base constructors leave state 0x02, list flags 0x12 and +0xae = 0x80; the
    // traffic constructor does not touch them.
    object.state = 0x0002;
    object.list_flags = 0x0012;
    object.field_ae = 0x0080;
    object.field_108 = 0x10000;
    object.field_1ac = 0x34;
    object.height_1ae = 0x20;
    object.field_b4 = 0x3c;
    object.traffic.road_node = road_node;
    object.traffic.sequence = sequence;
    object.traffic.sound_period = subtype == 0xd6 ? 0 : 1;
    return object;
}

// TRG_CheckTrafficProximity (0x004136e0): a player beside the car, inside the
// 2000-unit range and between the two sideways cross-product tests, makes the
// car play its horn or passing sound once.
void ObjectScriptVm::traffic_proximity(ScriptObject& object, const ScriptVec3& direction) {
    if (services_.video_playing()) {
        return;
    }
    const std::size_t players = services_.player_count();
    for (std::size_t index = 0; index < players; ++index) {
        const ScriptVec3 player = services_.player_position(index);
        if (crap_distance(object.position, player) >= 2000U) {
            continue;
        }
        const std::int32_t dx = wrap_sub(player[0], object.position[0]);
        const std::int32_t dz = wrap_sub(player[2], object.position[2]);
        const std::int32_t x = direction[0];
        const std::int32_t z = direction[2];
        if (cross_sign(x - z, dx, z + x, dz) > 0 && cross_sign(z + x, dx, z - x, dz) > 0) {
            if (object.traffic.latch != 0) {
                return;
            }
            std::int32_t sound = -2;  // -2: no sound selected
            switch (object.subtype) {
            case 0xd5: {
                const std::int32_t roll = static_cast<std::int32_t>(services_.random(3));
                if (roll == 0) {
                    sound = 0x6e;
                } else if (roll == 1) {
                    sound = 0x6f;
                } else if (roll == 2) {
                    sound = 0x70;
                }
                break;
            }
            case 0xd6:
                sound = 0x7a;
                break;
            case 0xd7:
                sound = (services_.random(2) != 0 ? 1 : 0) + 0x122;
                break;
            case 0xd8:
                sound = 0xfb;
                break;
            case 0xd9:
                sound = (services_.random(2) != 0 ? 0xe : 0) + 0x110;
                break;
            case 0xda:
            case 0xdc:
                sound = -1;
                break;
            case 0xdb:
                sound = (services_.random(2) != 0 ? 1 : 0) + 0x148;
                break;
            default:
                break;
            }
            if (sound != -2) {
                services_.play_sound(static_cast<std::int16_t>(sound));
            }
            object.traffic.latch = 1;
            return;
        }
    }
    object.traffic.latch = 0;
}

// TRG_UpdateTrafficObject (0x00412960)
ScriptRunStats ObjectScriptVm::update_traffic(ScriptObject& object) {
    ScriptRunStats stats;
    ScriptObject::Traffic& car = object.traffic;
    if (car.sound_period != -1 && car.sound_handle != -1 && car.sound_period != 0
        && services_.animation_clock() % car.sound_period == 0) {
        services_.update_sound_position(car.sound_handle, object.position);
    }
    if (services_.video_playing()) {
        services_.restore_car_position(car.sequence, object.position, object.params);
        return stats;
    }
    services_.traffic_interaction(object);

    bool first_segment = true;
    std::int32_t dot = 0;
    std::int32_t overshoot = 0;  // iStack_40: the speed, then the distance carried past a node
    while (true) {
        const std::vector<std::uint16_t> links = file_.links(static_cast<std::size_t>(car.road_node));
        assert_that(links.size() == 1, "Current road incorrectly linked");
        if (links.empty()) {
            return stats;
        }
        const std::uint16_t next = links.front();
        ScriptVec3 from = file_.node_position(static_cast<std::size_t>(car.road_node));
        from[1] = wrap_sub(from[1], kRoadDrop);
        ScriptVec3 to = file_.node_position(next);
        to[1] = wrap_sub(to[1], kRoadDrop);
        ScriptVec3 direction{wrap_sub(to[0], from[0]), wrap_sub(to[1], from[1]), wrap_sub(to[2], from[2])};
        normalize_fixed12(direction);

        std::int32_t speed = 0;
        std::int32_t drive = 0;
        if (first_segment) {
            first_segment = false;
            dot = q12_dot(direction, object.velocity);
            if (dot < 0) {
                drive = 0;
                speed = drive + 0x1000;
            } else {
                speed = dot;
                drive = dot;
                if (dot < 0x50000) {
                    speed = drive + 0x1000;
                }
            }
            object.velocity[0] = q12_mul(direction[0], speed);
            drive = speed;
        } else {
            object.velocity[0] = q12_mul(direction[0], dot);
            drive = dot;
            speed = overshoot;
        }
        object.velocity[1] = q12_mul(direction[1], drive);
        object.velocity[2] = q12_mul(direction[2], drive);

        // The dominant axis measures progress along the segment.
        int axis = 0;
        const std::int32_t ax = iabs(direction[0]);
        const std::int32_t ay = iabs(direction[1]);
        const std::int32_t az = iabs(direction[2]);
        if (ax < ay) {
            axis = 1;
            if (ay < az) {
                axis = 2;
            }
        } else if (ax < az) {
            axis = 2;
        }
        const std::int32_t length = shl((to[static_cast<std::size_t>(axis)] - from[static_cast<std::size_t>(axis)])
                                            / direction[static_cast<std::size_t>(axis)], 12);
        std::int32_t travelled = shl((object.position[static_cast<std::size_t>(axis)] - from[static_cast<std::size_t>(axis)])
                                         / direction[static_cast<std::size_t>(axis)], 12);

        const std::int32_t scale = services_.animation_time_scale();
        std::int32_t step = sar(scale * speed, 8);
        if (length - sar(length, 4) < travelled) {
            step = step - sar(scale * speed, 12);
            for (std::int32_t& component : object.velocity) {
                component = wrap_sub(component, sar(component, 4));
            }
        }
        travelled = wrap_add(travelled, step);
        if (travelled < 0 || length < travelled) {
            assert_that(travelled >= 0, "Oops, car going backwards!");
            const std::vector<std::uint16_t> onward = file_.links(next);
            assert_that(onward.size() == 1, "Road must have exactly 1 link");
            overshoot = travelled - length;
            car.road_node = next;
            object.position = to;
            continue;
        }
        for (std::size_t index = 0; index < 3; ++index) {
            object.position[index] = wrap_add(object.position[index], q12_mul(direction[index], step));
        }

        // Turn to face the road: yaw with a quarter of the error per frame
        // (snapping within 0x80), pitch with an eighth.
        const std::int32_t yaw = ratan2(-direction[0], -direction[2]) & 0xfff;
        const std::int32_t yaw_error = angle_difference(static_cast<std::int16_t>(object.params[1]), yaw);
        if (iabs(yaw_error) < 0x80) {
            object.params[1] = static_cast<std::uint16_t>(yaw);
        } else {
            object.params[1] = static_cast<std::uint16_t>(
                (sar(yaw_error, 2) + static_cast<std::int16_t>(object.params[1])) & 0xfff);
        }
        const std::int32_t pitch = ratan2(direction[1], 0x1000) & 0xfff;
        const std::int32_t pitch_error = angle_difference(static_cast<std::int16_t>(object.params[0]), pitch);
        if (iabs(pitch_error) < 0x80) {
            object.params[0] = static_cast<std::uint16_t>(pitch);
        } else {
            object.params[0] = static_cast<std::uint16_t>(
                (sar(pitch_error, 3) + static_cast<std::int16_t>(object.params[0])) & 0xfff);
        }
        traffic_proximity(object, direction);
        services_.save_car_position(car.sequence, object.position, object.params);
        return stats;
    }
}

} // namespace opentony::trg
