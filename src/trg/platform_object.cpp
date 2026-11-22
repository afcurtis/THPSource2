// Native model of the type-1 subtype-0x0192 level object: platforms, doors and
// the bouncy props (platform.cpp). Phase 4.4c.
//
// Addresses: TRG_ConstructSubtype0x0192 0x0049f250, update 0x0049fef0 (vtable
// slot +0x0c), CPlatform__BouncyAI 0x0049f780, CPlatform__Bounce 0x0049f4c0,
// CPlatform__Hit 0x004a0360 (slot +0x18), command handler 0x004a07e0 (+0x1c),
// field handler 0x004a1060 (+0x20), variable handler 0x004a1270 (+0x24),
// CPlatform__DoPhysics 0x004a03e0, FUN_004a0710 (set move-to target).
#include "object_script.hpp"

#include "../camera/camera_math.hpp"
#include "../camera/camera_system.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace opentony::trg {
namespace {

using camera::LookAngles;
using camera::Q16Vec3;

// Flag bits of the word at +0x178.
constexpr std::uint32_t kFlagMoveTo = 0x01;    // a move-to target is pending (FUN_004a0710)
constexpr std::uint32_t kFlagBox = 0x04;       // 0x4302 recorded a box
constexpr std::uint32_t kFlagBouncy = 0x10;    // 0x4309 selected a bounce kind: run the bounce simulation
constexpr std::uint32_t kFlagFlying = 0x20;    // CPlatform__Bounce started the flight
// Bits of the state word at +0x04 the platform commands touch.
constexpr std::uint16_t kStateScaled = 0x0200;

[[nodiscard]] std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
[[nodiscard]] std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
// `sar r32, n`
[[nodiscard]] std::int32_t sar(std::int32_t value, unsigned count) noexcept {
    return value >= 0 ? static_cast<std::int32_t>(static_cast<std::uint32_t>(value) >> count)
                      : static_cast<std::int32_t>(~(~static_cast<std::uint32_t>(value) >> count));
}
[[nodiscard]] std::int16_t low16(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffU));
}
[[nodiscard]] std::int32_t iabs(std::int32_t value) noexcept {
    const std::int32_t mask = sar(value, 31);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value ^ mask) - static_cast<std::uint32_t>(mask));
}

// FUN_004ca8f0: length of a fixed-point vector. Each component is shifted right
// by 12 and truncated to 16 bits, squared, summed, and square-rooted through the
// x87 helper 0x004f53b0 (non-positive sums give 0).
[[nodiscard]] std::int32_t vector_length(const ScriptVec3& v) noexcept {
    std::uint32_t sum = 0;
    for (const std::int32_t component : v) {
        const std::int32_t narrow = low16(sar(component, 12));
        sum += static_cast<std::uint32_t>(narrow * narrow);
    }
    if (static_cast<std::int32_t>(sum) <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(camera::isqrt_u32_x87(sum));
}

// 0x004f5fc0: (a * b) / 4096 through x87, truncated toward zero.
[[nodiscard]] std::int32_t q12_mul(std::int32_t a, std::int32_t b) noexcept {
    return camera::truncate_x87_result(
        static_cast<long double>(a) * static_cast<long double>(b) * 0.000244140625L);
}

// 0x004f5f90: (a . b) / 4096 through x87, truncated toward zero.
[[nodiscard]] std::int32_t q12_dot(const ScriptVec3& a, const ScriptVec3& b) noexcept {
    const long double sum = static_cast<long double>(a[2]) * b[2] + static_cast<long double>(a[1]) * b[1]
        + static_cast<long double>(a[0]) * b[0];
    return camera::truncate_x87_result(sum * 0.000244140625L);
}

// 0x004ca9b0: components inside [-0x800, 0x800] become zero.
void snap_near_zero(ScriptVec3& v) noexcept {
    for (std::int32_t& component : v) {
        if (component >= -0x800 && component <= 0x800) {
            component = 0;
        }
    }
}

// 0x004cab10 (Math_Vector3DecayShift): each component loses component >> count.
void decay_shift(ScriptVec3& v, const std::array<std::uint8_t, 3>& counts) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        v[axis] = wrap_sub(v[axis], sar(v[axis], counts[axis] & 31U));
    }
}

// 0x004cad60 / 0x004cae10 / 0x004cad80 / 0x004caef0: the packed 16-bit helpers.
using Angles = std::array<std::int16_t, 3>;
void mask12(Angles& a) noexcept {
    for (std::int16_t& value : a) {
        value = static_cast<std::int16_t>(value & 0x0fff);
    }
}
void add16(Angles& a, const Angles& b) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        a[axis] = low16(a[axis] + b[axis]);
    }
}
void snap16(Angles& a) noexcept {
    for (std::int16_t& value : a) {
        if (value >= -1 && value <= 1) {
            value = 0;
        }
    }
}
void decay16(Angles& a, const std::array<std::uint8_t, 3>& counts) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        a[axis] = low16(a[axis] - sar(a[axis], counts[axis] & 31U));
    }
}

[[nodiscard]] Angles angles_of(const ScriptObject& object) noexcept {
    return {static_cast<std::int16_t>(object.params[0]), static_cast<std::int16_t>(object.params[1]),
            static_cast<std::int16_t>(object.params[2])};
}
void store_angles(ScriptObject& object, const Angles& a) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        object.params[axis] = static_cast<std::uint16_t>(a[axis]);
    }
}

// Utils_TurnTowards (0x004c9880): the wrapped angle differences drive the two
// angular accelerations (or clear the two angular velocities when both are 0).
void turn_towards(const Angles& current, Angles& angular_velocity, Angles& angular_acceleration,
                  const LookAngles& look, std::int32_t speed) noexcept {
    const auto wrap = [](std::int32_t value) noexcept {
        std::int16_t narrow = low16(value);
        if (narrow < -0x800) {
            narrow = low16(narrow + 0x1000);
        }
        if (narrow > 0x800) {
            narrow = low16(narrow - 0x1000);
        }
        return narrow;
    };
    const std::int16_t first = wrap(static_cast<std::int32_t>(look.first) - current[0]);
    const std::int16_t second = wrap(static_cast<std::int32_t>(look.second) - current[1]);
    if (first == 0 && second == 0) {
        angular_velocity[0] = 0;
        angular_velocity[1] = 0;
        return;
    }
    angular_acceleration[0] = low16(sar(static_cast<std::int32_t>(first) * speed, 8));
    angular_acceleration[1] = low16(sar(static_cast<std::int32_t>(second) * speed, 8));
}

// FUN_004e80e0 (called as FUN_004f5390): the rotation matrix of three angles,
// composed as identity, then Y (angle 1), X (angle 0), Z (angle 2). Each step
// multiplies the matrix by the axis rotation, with the arithmetic >> 12 of the
// retail matrix multiply (0x004e3130). Elements are row-major.
using Matrix = std::array<std::int16_t, 9>;
[[nodiscard]] Matrix rotation_matrix(const Angles& angles) noexcept {
    Matrix m{};
    m[0] = 0x1000;
    m[4] = 0x1000;
    m[8] = 0x1000;
    const auto apply = [&m](const std::array<std::array<std::int32_t, 3>, 3>& columns) {
        Matrix next{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                std::int64_t sum = 0;
                for (std::size_t inner = 0; inner < 3; ++inner) {
                    sum += static_cast<std::int64_t>(m[row * 3 + inner]) * columns[column][inner];
                }
                next[row * 3 + column] = low16(static_cast<std::int32_t>(sum >> 12));
            }
        }
        m = next;
    };
    const auto cs = [](std::int32_t angle) {
        return std::pair<std::int32_t, std::int32_t>{
            static_cast<std::int16_t>(camera::cos_angle_q12(angle)),
            static_cast<std::int16_t>(camera::sin_angle_q12(angle))};
    };
    {   // FUN_004e7de0: Y
        const auto [c, s] = cs(angles[1]);
        apply({{{c, 0, -s}, {0, 0x1000, 0}, {s, 0, c}}});
    }
    {   // FUN_004e7c60: X
        const auto [c, s] = cs(angles[0]);
        apply({{{0x1000, 0, 0}, {0, c, s}, {0, -s, c}}});
    }
    {   // FUN_004e7f60: Z
        const auto [c, s] = cs(angles[2]);
        apply({{{c, s, 0}, {-s, c, 0}, {0, 0, 0x1000}}});
    }
    return m;
}

[[nodiscard]] Q16Vec3 to_q16(const ScriptVec3& v) noexcept { return {v[0], v[1], v[2]}; }
[[nodiscard]] LookAngles look_towards(const ScriptVec3& target, const ScriptVec3& origin) noexcept {
    return camera::build_look_angles(to_q16(target), to_q16(origin));
}
[[nodiscard]] ScriptVec3 direction(const LookAngles& look, std::int32_t scalar) noexcept {
    const camera::Q12Vec3 d = camera::direction_from_angles_raw(look, scalar);
    return {d.x, d.y, d.z};
}

} // namespace

// 0x004a07e0
bool ObjectScriptVm::execute_platform(ScriptObject& object, std::uint16_t opcode) {
    ScriptObject::Platform& p = object.platform;
    switch (opcode) {
    case 0x4205:
        die(object);
        return false;
    case 0x4220: {
        const std::uint32_t address = (object.cursor + 3U) & ~3U;
        ScriptVec3 target{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::uint32_t low = peek_word(address + static_cast<std::uint32_t>(axis) * 4U);
            const std::uint32_t high = peek_word(address + static_cast<std::uint32_t>(axis) * 4U + 2U);
            target[axis] = static_cast<std::int32_t>((low | (high << 16U)) << 12U);
        }
        object.cursor = address + 12U;
        set_platform_target(object, target);
        return true;
    }
    case 0x4221: {
        std::uint16_t word = read_word(object);
        std::uint32_t node = word;
        if ((word & 0x2000U) != 0) {
            node = variable(object, word);
        }
        // TRG_DecodeNodePosition asserts on a node type it cannot place and
        // leaves the (zeroed) result untouched.
        ScriptVec3 target{};
        try {
            target = file_.node_position(node & 0xffffU);
        } catch (const FormatError&) {
            assert_that(false, "Unrecognized node type in Trig_GetPosition");
        }
        set_platform_target(object, target);
        return true;
    }
    case 0x4222: {
        const std::uint32_t address = (object.cursor + 3U) & ~3U;
        ScriptVec3 target{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::uint32_t low = peek_word(address + static_cast<std::uint32_t>(axis) * 4U);
            const std::uint32_t high = peek_word(address + static_cast<std::uint32_t>(axis) * 4U + 2U);
            const std::int32_t offset = static_cast<std::int32_t>((low | (high << 16U)) << 12U);
            target[axis] = wrap_add(offset, object.position[axis]);
        }
        object.cursor = address + 12U;
        set_platform_target(object, target);
        return true;
    }
    case 0x4300:
        object.cursor -= 2U;
        return false;
    case 0x4301:
        object.state = static_cast<std::uint16_t>(object.state | 0x01U);
        die(object);
        services_.shatter_item(object, 0);
        return false;
    case 0x4302: {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::uint32_t word = read_word(object);
            p.box[axis] = sar(static_cast<std::int32_t>(word << 12U), 1);
        }
        object.flags |= kFlagBox;
        return true;
    }
    case 0x4303:
        object.cursor += 2U;
        return true;
    case 0x4304:
        platform_scale_animation(object, 0, 3);
        return true;
    case 0x4305: {
        const std::int16_t value = static_cast<std::int16_t>(read_word(object));
        if (value != 0) {
            object.state = static_cast<std::uint16_t>(object.state | 0x08U);
        } else {
            object.state = static_cast<std::uint16_t>(object.state & 0xfff7U);
        }
        return true;
    }
    case 0x4306:
        platform_scale_animation(object, 0, 1);
        return true;
    case 0x4307:
        platform_scale_animation(object, 1, 1);
        return true;
    case 0x4308:
        platform_scale_animation(object, 2, 1);
        return true;
    case 0x4309: {
        const std::uint16_t kind = read_word(object);
        p.kind = kind;
        if (kind == 0) {
            object.flags &= ~kFlagBouncy;
        } else {
            object.flags |= kFlagBouncy;
        }
        const ScriptModelInfo info = services_.model_info(object.region, object.model_index);
        p.extent[0] = static_cast<std::uint16_t>(info.box[0] - info.box[1]);
        p.extent[1] = static_cast<std::uint16_t>(info.box[2] - info.box[3]);
        p.extent[2] = static_cast<std::uint16_t>(info.box[4] - info.box[5]);
        return true;
    }
    case 0x4507: {
        if (p.sound_handle != -1) {
            services_.stop_sound(p.sound_handle);
        }
        const std::int16_t id = static_cast<std::int16_t>(read_word(object));
        p.sound_handle = services_.start_object_sound(id, false);
        p.sound_period = -1;
        return true;
    }
    case 0x4508: {
        if (p.sound_handle != -1) {
            services_.stop_sound(p.sound_handle);
        }
        const std::int16_t id = static_cast<std::int16_t>(read_word(object));
        p.sound_period = static_cast<std::int32_t>(read_word(object));
        p.sound_handle = services_.start_object_sound(id, true);
        return true;
    }
    case 0x4509:
        if (p.sound_handle != -1) {
            services_.stop_sound(p.sound_handle);
        }
        p.sound_handle = -1;
        return true;
    default:
        return execute_base(object, opcode);
    }
}

// FUN_004a0710: a changed move-to target becomes the target and raises flag 1.
void ObjectScriptVm::set_platform_target(ScriptObject& object, const ScriptVec3& target) {
    if (object.platform.target != target) {
        object.platform.target = target;
        object.flags |= kFlagMoveTo;
    }
}

// Commands 0x4304 (all axes) and 0x4306..0x4308 (one axis): animate the model
// scale by `amount` percent over `frames` update frames.
void ObjectScriptVm::platform_scale_animation(ScriptObject& object, std::size_t first_axis, std::size_t axis_count) {
    ScriptObject::Platform& p = object.platform;
    if ((object.state & kStateScaled) == 0) {
        object.state = static_cast<std::uint16_t>(object.state | kStateScaled);
        p.scale = {0x1000, 0x1000, 0x1000};
    }
    const std::int16_t amount = static_cast<std::int16_t>(read_word(object));
    const std::uint16_t frames = read_word(object);
    std::int32_t step;
    const std::int32_t numerator = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(amount)) << 12U);
    if (frames == 0) {
        step = numerator / 100;
    } else {
        step = numerator / static_cast<std::int32_t>(static_cast<std::uint32_t>(frames) * 100U);
    }
    const std::int16_t narrow = low16(step);
    for (std::size_t axis = first_axis; axis < first_axis + axis_count; ++axis) {
        p.scale_frames[axis] = frames;
        p.scale_step[axis] = narrow;
        p.scale[axis] = low16(p.scale[axis] + narrow);
    }
}

// 0x004a1060
bool ObjectScriptVm::platform_field(ScriptObject& object, std::uint16_t id) {
    ScriptObject::Platform& p = object.platform;
    const auto model_flag = [&]() {
        const ScriptModelInfo info = services_.model_info(object.region, object.model_index);
        if (info.flag_0x10) {
            object.state = static_cast<std::uint16_t>(object.state | 0x20U);
        } else {
            object.state = static_cast<std::uint16_t>(object.state & 0xffdfU);
        }
    };
    switch (id) {
    case 0x2123:
        p.byte_16e = static_cast<std::uint8_t>(peek_word(object.cursor));
        object.cursor += 2U;
        return true;
    case 0x2124:
        object.model_index = read_word(object);
        model_flag();
        return true;
    case 0x2127:
        for (std::size_t axis = 0; axis < 3; ++axis) {
            p.angular_velocity[axis] = static_cast<std::int16_t>(read_operand_word(object));
        }
        return true;
    case 0x2128:
        for (std::size_t axis = 0; axis < 3; ++axis) {
            p.angular_acceleration[axis] = static_cast<std::int16_t>(read_operand_word(object));
        }
        return true;
    case 0x212f: {
        const std::uint32_t address = (object.cursor + 3U) & ~3U;
        const std::uint32_t checksum =
            static_cast<std::uint32_t>(peek_word(address)) | (static_cast<std::uint32_t>(peek_word(address + 2U)) << 16U);
        object.model_index = services_.find_model_index(checksum, object.region);
        model_flag();
        object.state = static_cast<std::uint16_t>(object.state & 0xfffeU);
        object.cursor = address + 4U;
        return true;
    }
    case 0x2134:
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::int32_t value = static_cast<std::int16_t>(read_operand_word(object));
            object.velocity[axis] = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 12U);
        }
        return true;
    case 0x2137:
        for (std::size_t axis = 0; axis < 3; ++axis) {
            object.params[axis] = static_cast<std::uint16_t>(read_operand_word(object));
        }
        return true;
    default:
        return false;
    }
}

// CPlatform__DoPhysics (0x004a03e0)
void ObjectScriptVm::platform_physics(ScriptObject& object) {
    ScriptObject::Platform& p = object.platform;
    const std::uint16_t type = object.attributes[2];
    // +0x80..+0x84 (the shorts the retail routine zeroes first) are the top of
    // the angular acceleration block; nothing reads them afterwards.
    const auto step = [&]() {
        std::array<std::int32_t, 3> velocity{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            std::int32_t value = wrap_add(p.acceleration[axis], object.velocity[axis]);
            if (value <= 0x800 && value >= -0x800) {
                value = 0;
            }
            velocity[axis] = value;
        }
        object.velocity = velocity;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            object.position[axis] = wrap_add(object.position[axis], velocity[axis]);
        }
        Angles angles = angles_of(object);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            angles[axis] = static_cast<std::int16_t>(low16(angles[axis] + p.angular_velocity[axis]) & 0x0fff);
        }
        store_angles(object, angles);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            std::int16_t value = low16(p.angular_velocity[axis] + p.angular_acceleration[axis]);
            if (value >= -1 && value <= 1) {
                value = 0;
            }
            p.angular_velocity[axis] = value;
        }
    };
    if (type == 0) {
        if (object.wait == 0) {
            step();
            return;
        }
        // Waiting objects advance one physics step per two ticks of simulation
        // time that elapsed since the wait began.
        const std::uint32_t simulation = services_.simulation_time();
        if (simulation <= object.field_1e0 + object.field_1dc * 2U) {
            return;
        }
        while (true) {
            ++object.field_1dc;
            if (object.field_1dc == object.wait) {
                return;
            }
            step();
            if (simulation <= object.field_1e0 + object.field_1dc * 2U) {
                return;
            }
        }
    }
    if (type != 1 && type != 2) {
        assert_that(false, "unknown type in platform physics");
        return;
    }
    // Types 1 and 2: velocity integrates the acceleration with damping.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        object.velocity[axis] = wrap_add(object.velocity[axis], p.acceleration[axis]);
    }
    decay_shift(object.velocity, p.velocity_decay);
    snap_near_zero(object.velocity);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        object.position[axis] = wrap_add(object.position[axis], object.velocity[axis]);
    }
    Angles angles = angles_of(object);
    add16(angles, p.angular_velocity);
    mask12(angles);
    store_angles(object, angles);
    add16(p.angular_velocity, p.angular_acceleration);
    decay16(p.angular_velocity, p.angular_decay);
    snap16(p.angular_velocity);
}

// The flag-1 half of the update: steer towards the pending target.
void ObjectScriptVm::platform_move_to(ScriptObject& object) {
    ScriptObject::Platform& p = object.platform;
    const std::uint16_t type = object.attributes[2];
    const ScriptVec3 delta{wrap_sub(object.position[0], p.target[0]), wrap_sub(object.position[1], p.target[1]),
                           wrap_sub(object.position[2], p.target[2])};
    const std::int32_t distance = vector_length(delta);
    if (distance <= static_cast<std::int32_t>(object.attributes[0])) {
        object.position = p.target;
        if (type == 0 || type == 1) {
            object.velocity = {0, 0, 0};
        }
        object.flags &= ~kFlagMoveTo;
        return;
    }
    if (type == 0) {
        const LookAngles look = look_towards(p.target, object.position);
        object.velocity = direction(look, object.attributes[0]);
        return;
    }
    if (type != 1 && type != 2) {
        return;
    }
    const LookAngles look = look_towards(p.target, object.position);
    const ScriptVec3 to_target{wrap_sub(p.target[0], object.position[0]), wrap_sub(p.target[1], object.position[1]),
                               wrap_sub(p.target[2], object.position[2])};
    const std::int32_t length = vector_length(to_target);
    std::int32_t speed = 0x1fe0 / length;
    if (speed < -0x100) {
        speed = -0x100;
    } else if (speed > 0x100) {
        speed = 0x100;
    } else if (speed > 0) {
        if (speed < 4) {
            speed = 4;
        }
    } else if (speed < 0 && speed > -4) {
        speed = -4;
    }
    const Angles current = angles_of(object);
    turn_towards(current, p.angular_velocity, p.angular_acceleration, look, speed);
    object.params[0] = 0;
    p.acceleration = direction(look, object.attributes[0]);
    if (type != 1) {
        return;
    }
    std::int32_t bank = 0;
    if ((object.flags & kFlagFlying) != 0) {
        std::int32_t d = (static_cast<std::int32_t>(look.second) - static_cast<std::int16_t>(object.params[1])) & 0xfff;
        if (static_cast<std::int16_t>(d) > 0x800) {
            d -= 0x1000;
        }
        if (static_cast<std::int16_t>(d) > 0x400) {
            d = 0x400;
        } else if (static_cast<std::int16_t>(d) < -0x400) {
            d = -0x400;
        }
        bank = d;
    }
    std::int32_t roll = (sar(-bank, 1) & 0xfff) - static_cast<std::int16_t>(object.params[2]);
    if (static_cast<std::int16_t>(roll) < -0x800) {
        roll += 0x1000;
    }
    if (static_cast<std::int16_t>(roll) > 0x800) {
        roll -= 0x1000;
    }
    if (static_cast<std::int16_t>(roll) == 0) {
        p.angular_velocity[2] = 0;
    } else {
        p.angular_acceleration[2] = low16(sar(static_cast<std::int32_t>(static_cast<std::int16_t>(roll)) * 32, 8));
    }
}

// 0x0049fef0
ScriptRunStats ObjectScriptVm::update_platform(ScriptObject& object) {
    ScriptRunStats stats;
    ScriptObject::Platform& p = object.platform;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (p.scale_frames[axis] != 0) {
            p.scale[axis] = low16(p.scale[axis] + p.scale_step[axis]);
            --p.scale_frames[axis];
        }
    }
    const std::uint16_t event = object.event_word;
    p.acceleration = {0, 0, 0};
    object.signal_latch = event;
    if ((event & 1U) != 0) {
        ++object.byte_170;
        object.event_word = static_cast<std::uint16_t>(event & 0xfffeU);
    }
    if ((object.flags & kFlagBouncy) != 0) {
        bouncy_ai(object);
        return stats;
    }
    if (object.active == 0) {
        // FUN_004a07d0 does nothing.
    } else if (object.wait == 0) {
        stats = run(object, object.cursor);
    } else if (object.wait <= object.field_1dc) {
        object.wait = 0;
        object.field_1dc = 0;
        object.field_1e0 = 0;
    }
    if ((object.flags & kFlagMoveTo) != 0) {
        platform_move_to(object);
    } else if (object.attributes[2] == 1) {
        std::int32_t roll = -static_cast<std::int32_t>(static_cast<std::int16_t>(object.params[2]));
        if (static_cast<std::int16_t>(roll) < -0x800) {
            roll += 0x1000;
        }
        if (static_cast<std::int16_t>(roll) > 0x800) {
            roll -= 0x1000;
        }
        if (static_cast<std::int16_t>(roll) == 0) {
            p.angular_velocity[2] = 0;
        } else {
            p.angular_acceleration[2] = low16(sar(static_cast<std::int32_t>(static_cast<std::int16_t>(roll)) * 32, 8));
        }
    }
    if (p.byte_16e != 0) {
        const ScriptVec3 extra = services_.platform_acceleration();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            p.acceleration[axis] = wrap_add(p.acceleration[axis], extra[axis]);
        }
    }
    platform_physics(object);
    if (p.sound_period != -1 && p.sound_period != 0
        && services_.tick_counter() % p.sound_period == 0) {
        services_.update_sound_position(p.sound_handle, object.position);
    }
    return stats;
}

// 0x004a0360
void ObjectScriptVm::hit_platform(ScriptObject& object, std::int16_t damage) {
    ++object.byte_16f;
    if (object.attributes[1] != 0 && static_cast<std::int16_t>(object.field_b4) > 0) {
        const std::int16_t remaining = static_cast<std::int16_t>(static_cast<std::int16_t>(object.field_b4) - damage);
        object.field_b4 = static_cast<std::uint16_t>(remaining);
        if (remaining < 1) {
            die(object);
            services_.shatter_item(object, 1);
            return;
        }
        services_.item_flash(3, 0xff, 0xff, 0xff);
    }
}

// 0x0049f4c0: the skater hit a bouncy prop.
void ObjectScriptVm::bounce_platform(ScriptObject& object, std::uint32_t skater_context, const ScriptVec3& skater_velocity) {
    ScriptObject::Platform& p = object.platform;
    if ((object.flags & kFlagBouncy) == 0 || (object.flags & kFlagFlying) != 0) {
        return;
    }
    // The skater's horizontal speed picks the launch strength.
    const std::int32_t speed = vector_length({skater_velocity[0], 0, skater_velocity[2]});
    object.state = static_cast<std::uint16_t>(object.state | 0x20U);
    object.flags |= kFlagFlying;
    p.bounce_count = 0;
    p.bounce_angles[1] = static_cast<std::int16_t>(services_.random(0x1000));
    p.bounce_angles[0] = low16(static_cast<std::int32_t>(services_.random(2)) * 0x800
                               + static_cast<std::int16_t>(object.params[0]) - 0x400);
    p.bounce_angles[2] = low16(static_cast<std::int32_t>(services_.random(2)) * 0x800
                               + static_cast<std::int16_t>(object.params[2]) - 0x400);
    std::int32_t launch_y = 0;
    switch (p.kind) {
    case 1:
    case 3: {
        services_.play_sound(p.kind == 1 ? 0x68 : 0x3c);
        services_.skater_grunt(skater_context);
        services_.vibrate(6, 1, 0x78);
        launch_y = speed < 0x28 ? static_cast<std::int32_t>(0xfffe0000U) : static_cast<std::int32_t>(0xfffc0000U);
        if (speed < 0x12) {
            p.bounce_angles[1] = low16(static_cast<std::int32_t>(services_.random(0x400))
                                       + static_cast<std::int16_t>(object.params[1]) - 0x200);
            p.bounce_angles[0] = static_cast<std::int16_t>((static_cast<std::int16_t>(object.params[0]) + 0x200) & 0xc00);
            p.bounce_angles[2] = static_cast<std::int16_t>((static_cast<std::int16_t>(object.params[2]) + 0x200) & 0xc00);
        }
        object.velocity = {skater_velocity[0], launch_y, skater_velocity[2]};
        return;
    }
    case 2:
    case 4:
    case 5: {
        if (p.kind == 2) {
            services_.play_sound(0x6a);
        } else if (p.kind == 4) {
            services_.skater_grunt(skater_context);
            services_.play_sound(0x52);
        }
        services_.vibrate(2, 1, 0xb4);
        object.velocity = {wrap_add(sar(skater_velocity[0], 1), skater_velocity[0]), static_cast<std::int32_t>(0xfffb0000U),
                           wrap_add(sar(skater_velocity[2], 1), skater_velocity[2])};
        const std::int16_t first = p.bounce_angles[0];
        if (iabs(first) == 0x800 && p.bounce_angles[2] == 0) {
            p.bounce_angles[2] = static_cast<std::int16_t>(object.params[2]);
        }
        if (first != 0) {
            return;
        }
        if (iabs(p.bounce_angles[2]) != 0x800) {
            return;
        }
        p.bounce_angles[0] = static_cast<std::int16_t>(object.params[0]);
        return;
    }
    default:
        assert_that(false, "unknown bouncy platform type");
        object.velocity = {skater_velocity[0], static_cast<std::int32_t>(0xfffc0000U), skater_velocity[2]};
        return;
    }
}

// CPlatform__BouncyAI (0x0049f780): a thrown prop tumbles under drag, probing
// the level's collision with a line and a vertical line, and reflecting its
// velocity off whatever it hits.
void ObjectScriptVm::bouncy_ai(ScriptObject& object) {
    ScriptObject::Platform& p = object.platform;
    if (p.lifetime != 0) {
        --p.lifetime;
        if (p.lifetime == 0) {
            die(object);
            return;
        }
    }
    if ((object.flags & kFlagFlying) == 0) {
        return;
    }
    // Spin towards the target orientation.
    mask12(p.bounce_angles);
    Angles angles = angles_of(object);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        p.angular_velocity[axis] = low16(sar(static_cast<std::int32_t>(p.bounce_angles[axis]) - angles[axis], 5));
    }
    snap16(p.angular_velocity);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const std::int32_t rate = p.angular_velocity[axis];
        const std::int32_t remaining = static_cast<std::int32_t>(p.bounce_angles[axis]) - angles[axis];
        if (iabs(rate) < 0x28 && iabs(remaining) > 0x28) {
            p.angular_velocity[axis] = static_cast<std::int16_t>(rate >= 0 ? 0x28 : -0x28);
        }
    }
    add16(angles, p.angular_velocity);
    mask12(angles);
    store_angles(object, angles);
    const Matrix matrix = rotation_matrix(angles);
    // Vertical reach of the rotated model box.
    const std::int32_t radius = sar(
        iabs(matrix[3]) * static_cast<std::int32_t>(p.extent[0]) + iabs(matrix[4]) * static_cast<std::int32_t>(p.extent[1])
            + iabs(matrix[5]) * static_cast<std::int32_t>(p.extent[2]),
        1);

    ScriptVec3 velocity{object.velocity[0], wrap_add(object.velocity[1], 0x8000), object.velocity[2]};
    std::array<std::uint8_t, 3> counts{6, 6, 6};
    decay_shift(velocity, counts);
    snap_near_zero(velocity);
    ScriptVec3 end{wrap_add(object.position[0], velocity[0]), wrap_add(object.position[1], velocity[1]),
                   wrap_add(object.position[2], velocity[2])};
    const std::int32_t speed_xz = vector_length({velocity[0], 0, velocity[2]});

    for (int probe = 0; probe < 3; ++probe) {
        ScriptCollisionHit hit = services_.line_collision(object.position, end);
        if (!hit.hit && velocity[1] > 0) {
            hit = services_.line_collision(
                {end[0], wrap_sub(end[1], radius), end[2]}, {end[0], wrap_add(end[1], radius), end[2]});
        }
        if (!hit.hit) {
            object.position = end;
            object.velocity = velocity;
            break;
        }
        if (p.bounce_count < 5) {
            switch (p.kind) {
            case 1:
                services_.play_sound(services_.random(2) == 0 ? 0x67 : 0x66);
                break;
            case 2:
                services_.play_sound(services_.random(2) == 0 ? 0x6b : 0x6a);
                break;
            case 3:
                services_.play_sound(0x45);
                break;
            case 4:
                services_.play_sound(services_.random(2) == 0 ? 0x51 : 0x50);
                break;
            case 5:
                services_.play_sound(static_cast<std::int16_t>(0x53 + static_cast<std::int32_t>(services_.random(2))));
                break;
            default:
                break;
            }
        }
        const ScriptVec3 normal{hit.normal[0], hit.normal[1], hit.normal[2]};
        ++p.bounce_count;
        const std::int32_t along = q12_dot(velocity, normal);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            velocity[axis] = wrap_sub(velocity[axis], q12_mul(normal[axis], along) * 2);
        }
        std::array<std::uint8_t, 3> drag{3, 2, 3};
        if (iabs(velocity[1]) < 0xc000) {
            drag = {2, 0, 2};
        } else {
            p.bounce_angles[1] = static_cast<std::int16_t>(services_.random(0x1000));
            if (p.bounce_count == 1) {
                p.bounce_angles[0] = low16(p.bounce_angles[0] + static_cast<std::int32_t>(services_.random(2)) * 0x800 - 0x400);
                const bool paired = p.kind == 2 || p.kind == 4;
                if (paired) {
                    const std::int16_t first = p.bounce_angles[0];
                    const std::int16_t third = p.bounce_angles[2];
                    if ((first == 0x800 && third == 0) || (first != 0x800 && first == 0 && third == 0x800)) {
                        p.bounce_angles[0] = low16(static_cast<std::int32_t>(services_.random(2)) * 0x800 - 0x400);
                    }
                }
                p.bounce_angles[2] = low16(p.bounce_angles[2] + static_cast<std::int32_t>(services_.random(3)) * 0x400 - 0x400);
                if (paired) {
                    const std::int16_t first = p.bounce_angles[0];
                    const std::int16_t third = p.bounce_angles[2];
                    if ((iabs(first) == 0x800 && third == 0) || (first == 0 && iabs(third) == 0x800)) {
                        p.bounce_angles[2] = low16(static_cast<std::int32_t>(services_.random(2)) * 0x800 - 0x400);
                    }
                }
                if (speed_xz < 0x12) {
                    p.bounce_angles[1] = low16(static_cast<std::int32_t>(services_.random(0x400))
                                               + static_cast<std::int16_t>(object.params[1]) - 0x200);
                    p.bounce_angles[0] = static_cast<std::int16_t>((static_cast<std::int16_t>(object.params[0]) + 0x200) & 0xc00);
                    p.bounce_angles[2] = static_cast<std::int16_t>((static_cast<std::int16_t>(object.params[2]) + 0x200) & 0xc00);
                }
            }
        }
        decay_shift(velocity, drag);
        snap_near_zero(velocity);
        if (p.bounce_count > 0x12) {
            object.params[2] = static_cast<std::uint16_t>(p.bounce_angles[2]);
            object.params[0] = static_cast<std::uint16_t>(p.bounce_angles[0]);
            object.flags &= ~kFlagFlying;
            p.lifetime = 10;
            return;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            end[axis] = wrap_add(q12_mul(normal[axis], radius), hit.position[axis]);
        }
    }
    if (p.bounce_count > 0x12) {
        object.params[0] = static_cast<std::uint16_t>(p.bounce_angles[0]);
        object.params[2] = static_cast<std::uint16_t>(p.bounce_angles[2]);
        object.flags &= ~kFlagFlying;
        p.lifetime = 10;
    }
}

} // namespace opentony::trg
