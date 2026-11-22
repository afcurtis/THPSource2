// Native model of the collectable power-up object (type-5 nodes; powerup.cpp).
// Phase 4.4c.
//
// Addresses: Powerup_Construct 0x004a7c50 (via Powerup_CreateFromTrigger
// 0x004a8e50), Powerup_Update 0x004a8ac0 (vtable 0x00519684 slot +0x0c),
// CPowerUp__CheckAge 0x004a8620, CPowerUp__DoPhysics 0x004a8300,
// CPowerUp__TakeEffect 0x004a86f0, hide (slot +0x04) 0x004a8290, die (slot +0x08)
// 0x004a82e0, CPowerUp__SetNode 0x004a82c0.
#include "object_script.hpp"

#include "world_objects.hpp"

#include "../camera/camera_math.hpp"

#include <cstdint>

namespace opentony::trg {
namespace {

[[nodiscard]] std::int32_t sar(std::int32_t value, unsigned count) noexcept {
    return value >= 0 ? static_cast<std::int32_t>(static_cast<std::uint32_t>(value) >> count)
                      : static_cast<std::int32_t>(~(~static_cast<std::uint32_t>(value) >> count));
}
[[nodiscard]] std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
[[nodiscard]] std::int16_t low16(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffU));
}

// Retail item-region slots: the power-up models live in the "items" region, the
// medals in "skmedals" (CurrentItemsRegion, CurrentSkMedalsRegion).
constexpr std::uint8_t kItemsRegion = 1;
constexpr std::uint8_t kMedalsRegion = 2;

} // namespace

// Powerup_Construct (0x004a7c50), called by TRG_CreateObject for a type-5 node
// through Powerup_CreateFromTrigger: the node supplies the subtype, position,
// bit 2 of the flags (the word after the position is zero) and the timer.
ScriptObject ObjectScriptVm::create_powerup(
    std::uint16_t node, std::uint16_t subtype, const ScriptVec3& position, const ScriptVec3& velocity,
    std::uint8_t flags, std::uint16_t timer) {
    ScriptObject object;
    ScriptObject::Powerup& p = object.powerup;
    object.node = node;
    object.subtype = subtype;
    object.state = 0;
    object.list_flags = 0x001a;  // base constructor FUN_00480060
    p.timer = timer;
    p.home = position;
    object.position = position;
    if ((flags & 8U) == 0) {
        object.velocity = velocity;
    } else {
        object.velocity[1] = static_cast<std::int32_t>(0xfffc4000U);
    }
    object.platform.velocity_decay = {5, 5, 5};
    object.platform.angular_velocity[1] = 100;
    object.platform.acceleration[1] = 0x8000;
    p.rate = static_cast<std::uint16_t>(static_cast<std::int32_t>(services_.random(0x14)) + 0x5a);
    p.amplitude_base = 0x32;
    p.amplitude = 0x32;
    if ((flags & 0x10U) == 0) {
        if ((flags & 4U) != 0) {
            p.motion = 1;
            p.ground = services_.ground_height(p.home);
        }
    } else {
        p.motion = 1;
        p.settled = 1;
    }
    object.field_ae = 300;

    const auto model = [&](std::uint32_t checksum, std::uint8_t region) {
        object.model_index = services_.find_model_index(checksum, region);
        object.region = region;
    };
    bool resolved = true;
    switch (subtype) {
    case 4: p.visual = 1; model(0x2328a71c, kItemsRegion); break;
    case 5: p.visual = 1; model(0x311d55d4, kItemsRegion); break;
    case 6: p.visual = 1; model(0x2ebf22ca, kItemsRegion); break;
    case 10: p.visual = 1; model(0x29b68a16, kItemsRegion); break;
    case 0xf: p.visual = 1; model(0x34524351, kItemsRegion); break;
    case 0x10:
    case 0x12:
        object.platform.angular_velocity[1] = 0x96;
        p.visual = 1;
        model(0x7c1b2c4a, kItemsRegion);
        object.field_ae = 500;
        break;
    case 0x18:
        object.platform.angular_velocity[1] = 200;
        p.visual = 1;
        model(0x694ed947, kItemsRegion);
        if (services_.career_got_money(node)) {
            p.timer = 1;
        }
        break;
    case 0x19:
        object.platform.angular_velocity[1] = 200;
        p.visual = 1;
        model(0x260f4f80, kItemsRegion);
        if (services_.career_got_money(node)) {
            p.timer = 1;
        }
        break;
    case 0x1a:
        object.platform.angular_velocity[1] = 200;
        p.visual = 1;
        model(0xcc4e141f, kItemsRegion);
        if (services_.career_got_money(node)) {
            p.timer = 1;
        }
        break;
    case 0x21: {
        p.visual = 1;
        std::uint32_t checksum = 0;
        std::uint8_t region = 0;
        services_.level_pickup_model(checksum, region);
        if (checksum == 0) {
            model(0x2328a71c, kItemsRegion);
            object.platform.angular_velocity[1] = 400;
        } else {
            model(checksum, region);
        }
        break;
    }
    case 0x664: p.visual = 1; model(0x54636518, kMedalsRegion); break;
    case 0x665: p.visual = 1; model(0xba6d0434, kMedalsRegion); break;
    case 0x666: p.visual = 1; model(0x2364558e, kMedalsRegion); break;
    default:
        assert_that(false, "Bad powerup type");
        resolved = false;
        break;
    }
    if (resolved) {
        // Which pickups the level has already cleared hide themselves.
        const std::uint32_t bits = services_.goal_progress_bits();
        if (services_.game_mode_selector() != 1) {
            switch (subtype) {
            case 4: case 5: case 6: case 10: case 0xf: case 0x10:
            case 0x18: case 0x19: case 0x1a: case 0x21:
                p.timer = 1;
                break;
            default:
                break;
            }
        } else {
            switch (subtype) {
            case 4: case 5: case 6: case 10: case 0xf:
                if ((bits & 2U) != 0) {
                    p.timer = 1;
                }
                break;
            case 0x10:
                if ((bits & 1U) != 0) {
                    p.timer = 1;
                }
                break;
            case 0x21:
                if ((bits & 4U) != 0) {
                    p.timer = 1;
                }
                break;
            default:
                break;
            }
        }
    }
    if (p.visual == 0) {
        object.state = static_cast<std::uint16_t>(object.state | 0x41U);  // maybe_destroy_model
        object.list_flags = static_cast<std::uint16_t>(object.list_flags & 0xfff7U);
    }
    const std::uint32_t level = services_.current_level();
    if (level > 9 && level != 0xd) {
        object.state = static_cast<std::uint16_t>(object.state | 0x41U);
        object.list_flags = static_cast<std::uint16_t>((object.list_flags & 0xfff7U) | 0x40U);
    }
    return object;
}

// Vtable slot +0x04 (0x004a8290): the object leaves the drawn set and frees its glow.
void ObjectScriptVm::hide_powerup(ScriptObject& object) {
    object.list_flags = static_cast<std::uint16_t>(object.list_flags & 0xfff7U);
    if (object.powerup.glow != 0) {
        services_.destroy_powerup_glow(object.powerup.glow);
    }
    object.powerup.glow = 0;
}

// Vtable slot +0x08 (0x004a82e0)
void ObjectScriptVm::die_powerup(ScriptObject& object) {
    if ((object.list_flags & 0x40U) == 0) {
        object.list_flags = static_cast<std::uint16_t>(object.list_flags | 0x40U);
        object.state = static_cast<std::uint16_t>(object.state | 0x41U);
        hide_powerup(object);
    }
}

// CPowerUp__CheckAge (0x004a8620)
void ObjectScriptVm::powerup_check_age(ScriptObject& object) {
    ScriptObject::Powerup& p = object.powerup;
    ++p.age;
    if (p.timer == 0xffffU) {
        return;
    }
    if (p.timer != 0) {
        --p.timer;
    }
    const std::uint16_t left = p.timer;
    const std::int32_t counter = services_.tick_counter();
    const auto blink_off = [&]() {
        object.state = static_cast<std::uint16_t>(object.state | 0x41U);  // maybe_destroy_model
        hide_powerup(object);
    };
    if (left < 0x3c) {
        p.amplitude = static_cast<std::uint16_t>(low16(static_cast<std::int32_t>(p.amplitude_base * left) / 0x3c));
        if (left > 0x1e) {
            if ((counter & 2) == 0) {
                blink_off();
            } else {
                object.state = static_cast<std::uint16_t>(object.state & 0xfffeU);
            }
        }
    }
    if (p.timer <= 0x1e) {
        if ((counter & 1) == 0) {
            blink_off();
        } else {
            object.state = static_cast<std::uint16_t>(object.state & 0xfffeU);
        }
    }
    if (p.timer == 0) {
        die_powerup(object);
    }
}

// CPowerUp__DoPhysics (0x004a8300)
void ObjectScriptVm::powerup_physics(ScriptObject& object) {
    ScriptObject::Powerup& p = object.powerup;
    const std::int32_t scale = services_.animation_time_scale();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        object.params[axis] = static_cast<std::uint16_t>(
            low16(static_cast<std::int16_t>(object.params[axis]) + sar(object.platform.angular_velocity[axis] * scale, 8)));
        object.params[axis] = static_cast<std::uint16_t>(object.params[axis] & 0x0fffU);
    }
    const std::int32_t counter = services_.tick_counter();
    if (p.motion == 0) {
        // Hovering: bob on a sine of the phase.
        object.position = p.home;
        const std::int32_t swing = camera::sin_angle_q12(p.phase & 0xfff) * static_cast<std::int32_t>(p.amplitude);
        std::int32_t rounded = swing + ((swing >> 31) & 0xfff);
        rounded = sar(rounded, 12);
        object.position[1] = wrap_add(object.position[1], static_cast<std::int32_t>(static_cast<std::uint32_t>(rounded) << 12U));
        p.phase = static_cast<std::uint16_t>(low16(p.phase + sar(static_cast<std::int32_t>(p.rate) * scale, 8)));
    } else if (p.settled == 0 && p.ground < 0 && p.ground > -5) {
        // Waiting for ground under it: look again every other frame.
        if ((counter & 1) != 0) {
            const std::int32_t found = services_.ground_height(p.home);
            if (found == -1) {
                --p.ground;
                if (p.ground <= -5) {
                    p.settled = 1;
                }
            } else {
                p.ground = found;
            }
        }
    } else {
        // Thrown or dropped: fall with drag, stop on the ground.
        for (std::size_t axis = 0; axis < 3; ++axis) {
            p.home[axis] = wrap_add(p.home[axis], object.velocity[axis]);
        }
        std::int32_t vy = wrap_add(object.velocity[1], object.platform.acceleration[1]);
        vy = vy - sar(vy, object.platform.velocity_decay[1] & 31U);
        object.velocity[1] = vy;
        if (p.settled == 0) {
            const std::int32_t limit = p.ground - static_cast<std::int32_t>(static_cast<std::uint32_t>(p.drop) << 12U);
            if (p.home[1] > limit && vy > 0) {
                p.home[1] = limit;
                object.velocity = {0, 0, 0};
                p.motion = 0;
            }
        }
        object.position = p.home;
    }
    // (The camera-following branch, +0xf6 != 0, is not modelled.)
    p.shadow = {p.home[0], p.ground, p.home[2]};
    const std::int32_t height = sar(p.ground - object.position[1], 12);
    p.shadow_height = static_cast<std::uint16_t>(low16(height));
    if (low16(height) != 0) {
        const std::int32_t scaled = static_cast<std::int32_t>(low16(height)) * 60;
        p.shadow_scale = static_cast<std::uint16_t>(0x8c - sar(scaled + ((scaled >> 31) & 0x7f), 7));
    } else {
        p.shadow_scale = 0;
    }
}

// Powerup_Update (0x004a8ac0)
void ObjectScriptVm::update_powerup(ScriptObject& object) {
    ScriptObject::Powerup& p = object.powerup;
    powerup_check_age(object);
    const std::uint32_t limit = services_.powerup_draw_limit();
    std::uint32_t distance = object.distance;
    if (services_.player_count() == 1) {
        if ((object.state & 1U) != 0) {
            return;
        }
    } else {
        if ((object.state & 0x41U) == 0x41U) {
            return;
        }
        if (distance > limit) {
            // Seen from the second player instead.
            distance = crap_distance(services_.player_position(1), object.position);
        }
    }
    if (distance > limit) {
        hide_powerup(object);
        return;
    }
    assert_that((object.list_flags & 4U) == 0, "Hey, I'm seedable!");
    powerup_physics(object);
    if (p.glow == 0) {
        p.glow = services_.create_powerup_glow(object.subtype);
        switch (object.subtype) {
        case 4: case 5: case 6: case 10: case 0xf: case 0x10:
            p.glow_base = 0x3c;
            p.glow_amplitude = 10;
            break;
        case 0x12:
            p.glow_base = 10;
            p.glow_amplitude = 3;
            break;
        default:
            break;
        }
    }
    assert_that(p.glow != 0, "No powerup glow created");
    const std::int32_t wobble = camera::sin_angle_q12(services_.tick_counter() * 0x96) * static_cast<std::int32_t>(p.glow_amplitude);
    const std::int32_t intensity = sar(wobble + ((wobble >> 31) & 0xfff), 12) + static_cast<std::int32_t>(p.glow_base);
    services_.update_powerup_glow(p.glow, object.position, intensity);
}

// CPowerUp__TakeEffect (0x004a86f0)
void ObjectScriptVm::take_powerup(ScriptObject& object, std::uint32_t skater, bool primary_skater) {
    ScriptObject::Powerup& p = object.powerup;
    const std::uint16_t hidden = (services_.player_count() == 1 || !primary_skater) ? 0x0001U : 0x0040U;
    if ((object.state & hidden) != 0) {
        return;
    }
    bool taken = false;
    switch (object.subtype) {
    case 4: case 5: case 6: case 10: case 0xe: case 0xf: case 0x10:
        taken = true;
        services_.delete_messages();
        switch (object.subtype) {
        case 4:
        case 5:
        case 6:
        case 10:
        case 0xf: {
            services_.play_sound(0x14);
            const std::int32_t index = object.subtype == 4 ? 1 : (object.subtype == 5 ? 0 : (object.subtype == 6 ? 2
                : (object.subtype == 10 ? 4 : 3)));
            if (!services_.goal_stat_taken(index)) {
                services_.show_message(object.subtype);
            }
            break;
        }
        case 0x10:
            services_.career_give_goal_type(6);
            break;
        default:
            break;
        }
        break;
    case 0x18: case 0x19: case 0x1a: case 0x21:
        if (services_.skater_on_ground(skater)) {
            taken = true;
            const bool grounded = services_.skater_not_in_trick(skater);
            if (grounded) {
                services_.skater_trick_takeoff();
            }
            const std::int32_t handle = services_.play_sound_handle(object.subtype == 0x21 ? 0x13c : 0x12);
            switch (object.subtype) {
            case 0x18:
                services_.show_message(0x18);
                services_.sound_pitch(handle, 900);
                services_.career_give_money(object.node, 250);
                break;
            case 0x19:
                services_.show_message(0x19);
                services_.sound_pitch(handle, 0x45a);
                services_.career_give_money(object.node, 50);
                break;
            case 0x1a:
                services_.show_message(0x1a);
                services_.sound_pitch(handle, 0x5dc);
                services_.career_give_money(object.node, 100);
                break;
            default:
                services_.career_level_pickup();
                services_.sound_pitch(handle, 0x45a);
                break;
            }
            if (grounded) {
                services_.skater_trick_land(skater);
            }
        }
        break;
    default:
        assert_that(false, "Unknown PowerUp type");
        break;
    }
    if (p.link_flag != 0) {
        services_.execute_node_links(p.link_node);
    }
    if (taken) {
        die_powerup(object);
    }
}

} // namespace opentony::trg
