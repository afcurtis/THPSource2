// Runs every level-object script in a TRG file through the native script
// interpreter against a deterministic pseudo-world and reports what happened.
//
//   opentony_trg_scripts FILE.TRG [--frames N] [--histogram]
//
// The pseudo-world varies random results, career flags, the game mode and the
// signal bit from frame to frame so that both sides of most conditionals run.
// A shipped file is expected to produce no assertions and no format faults.
#include "object_script.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {

using namespace opentony::trg;

class PseudoWorld final : public ObjectScriptServices {
public:
    std::size_t effects{};
    std::size_t sounds{};
    std::size_t messages{};
    std::size_t animations{};
    std::size_t assertions{};
    std::string first_assertion;

    void advance() { state_ = state_ * 1664525U + 1013904223U; }

    void on_assertion(std::string_view message) override {
        if (assertions++ == 0) {
            first_assertion = std::string(message);
        }
    }
    [[nodiscard]] std::uint32_t game_mode_selector() const override { return (state_ >> 20U) & 1U; }
    [[nodiscard]] std::uint32_t random(std::uint16_t range) override {
        advance();
        return range == 0 ? 0U : (state_ >> 8U) % range;
    }
    [[nodiscard]] bool has_target() const override { return true; }
    [[nodiscard]] ScriptVec3 target_position() const override { return {0x100000, 0x20000, -0x300000}; }
    [[nodiscard]] std::uint32_t xz_distance(const ScriptVec3&, const ScriptVec3&) const override {
        return (state_ >> 12U) & 0x3fffU;
    }
    [[nodiscard]] std::uint32_t line_of_sight(const ScriptVec3&, const ScriptVec3&) const override {
        return (state_ >> 5U) & 1U;
    }
    [[nodiscard]] bool level_flag(std::uint16_t flag) const override { return ((state_ >> (flag + 3U)) & 1U) != 0; }
    [[nodiscard]] std::uint32_t level_goal_bits() const override { return state_ >> 7U; }
    void play_sound(std::int16_t) override { ++sounds; }
    void play_positional_sound(std::int16_t, const ScriptVec3&) override { ++sounds; }
    [[nodiscard]] std::int32_t start_object_sound(std::int16_t, bool) override {
        ++sounds;
        return 1;
    }
    void on_repeated_effect(const ScriptRepeatedEffect&) override { ++effects; }
    void on_flash(const ScriptFlashEvent&) override { ++effects; }
    void animation_cycle(std::uint16_t, std::uint16_t) override { ++animations; }
    void animation_start(std::uint16_t) override { ++animations; }
    // Platform (0x0192) objects: time, models and collision vary from frame to
    // frame so that the move-to, bounce and physics branches all run.
    [[nodiscard]] std::uint32_t simulation_time() const override { return time_; }
    [[nodiscard]] std::int32_t tick_counter() const override { return static_cast<std::int32_t>(time_ >> 1U); }
    [[nodiscard]] ScriptModelInfo model_info(std::uint8_t, std::uint16_t) override {
        return {(state_ & 0x10U) != 0, {0x30, -0x30, 0x40, -0x40, 0x30, -0x30}};
    }
    [[nodiscard]] ScriptCollisionHit line_collision(const ScriptVec3&, const ScriptVec3& end) override {
        advance();
        const std::uint32_t pick = (state_ >> 9U) & 7U;
        if (pick < 3) {
            return {};
        }
        ScriptCollisionHit hit{true, end, {0, 0, 0}};
        hit.normal[pick % 3] = pick < 6 ? 0x1000 : -0x1000;
        return hit;
    }
    void shatter_item(const ScriptObject&, int) override { ++effects; }
    void tick_time() { time_ += 2U; }
    void signal_node_links(std::uint16_t) override { ++messages; }
    void execute_node_links(std::uint16_t) override { ++messages; }
    void signal_objects_at_node(std::uint16_t) override { ++messages; }
    void send_pulse(std::uint16_t) override { ++messages; }

private:
    std::uint32_t state_{0x12345678U};
    std::uint32_t time_{1000};
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: opentony_trg_scripts FILE.TRG [--frames N] [--histogram]\n";
        return 2;
    }
    std::size_t frames = 600;
    bool histogram = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--histogram") {
            histogram = true;
        } else if (argument == "--frames" && index + 1 < argc) {
            frames = static_cast<std::size_t>(std::stoul(argv[++index]));
        }
    }
    try {
        const TrgFile file = TrgFile::load(argv[1]);
        PseudoWorld world;
        ObjectScriptVm vm(file, world, ScriptClass::ScriptObjectCb);
        std::map<std::uint16_t, std::size_t> opcodes;
        std::size_t commands = 0;
        vm.set_command_trace([&](std::uint32_t, std::uint16_t opcode) {
            ++opcodes[opcode];
            ++commands;
        });

        std::size_t objects = 0;
        std::size_t script_points = 0;
        std::size_t finished = 0;
        std::size_t died = 0;
        std::size_t faults = 0;
        auto drive = [&](ScriptObject object) {
            try {
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    world.advance();
                    if (frame % 5 == 4) {
                        object.event_word = 1;  // let wait-for-signal commands through
                    }
                    (void)vm.update(object, 0x10000U);
                    if ((object.list_flags & 0x40U) != 0) {
                        ++died;
                        return;
                    }
                }
                if (object.active == 0) {
                    ++finished;
                }
            } catch (const FormatError& error) {
                ++faults;
                std::cerr << "fault: " << error.what() << '\n';
            }
        };

        for (const NodeView& node : file.nodes()) {
            if (node.type == 1 && file.node_subtype(node.index) == 0xcb) {
                ++objects;
                const std::array<std::uint16_t, 3> params = file.node_orientation(node.index);
                drive(ScriptObject::construct_cb(
                    static_cast<std::uint16_t>(node.index),
                    file.node_factory_cursor_offset(node.index),
                    file.node_position(node.index),
                    params));
            } else if (node.type == 1000) {
                ++script_points;
                drive(ScriptObject::construct_cb(
                    static_cast<std::uint16_t>(node.index),
                    vm.script_point_cursor(node.index),
                    {0, 0, 0},
                    {0, 0, 0}));
            }
        }
        std::cout << "objects=" << objects << " script_points=" << script_points
                  << " commands=" << commands << " finished=" << finished << " died=" << died
                  << " faults=" << faults << " assertions=" << world.assertions
                  << " distinct_opcodes=" << opcodes.size() << " sounds=" << world.sounds
                  << " effects=" << world.effects << " messages=" << world.messages
                  << " animations=" << world.animations << '\n';
        if (world.assertions != 0) {
            std::cerr << "first assertion: " << world.first_assertion << '\n';
        }

        // The platform (0x0192) objects: the same frames, the platform update.
        PseudoWorld platform_world;
        ObjectScriptVm platform_vm(file, platform_world, ScriptClass::Platform);
        std::map<std::uint16_t, std::size_t> platform_opcodes;
        std::map<std::uint16_t, std::size_t> platform_types;
        std::map<std::uint16_t, std::size_t> platform_kinds;
        std::size_t platform_commands = 0;
        platform_vm.set_command_trace([&](std::uint32_t, std::uint16_t opcode) {
            ++platform_opcodes[opcode];
            ++platform_commands;
        });
        std::size_t platform_objects = 0;
        std::size_t platform_died = 0;
        std::size_t platform_flying = 0;
        std::size_t platform_faults = 0;
        for (const NodeView& node : file.nodes()) {
            if (node.type != 1 || file.node_subtype(node.index) != 0x192) {
                continue;
            }
            ++platform_objects;
            ScriptObject object = ScriptObject::construct_platform(
                static_cast<std::uint16_t>(node.index), file.node_factory_cursor_offset(node.index),
                file.node_position(node.index), file.node_orientation(node.index));
            try {
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    platform_world.advance();
                    platform_world.tick_time();
                    if (frame % 5 == 4) {
                        object.event_word = 1;
                    }
                    if (frame == 20 && (object.flags & 0x10U) != 0) {
                        platform_vm.bounce_platform(object, 0, {0x20000, 0, 0x10000});
                    }
                    if ((object.flags & 0x20U) != 0) {
                        ++platform_flying;
                    }
                    (void)platform_vm.update_platform(object);
                    if ((object.list_flags & 0x40U) != 0) {
                        ++platform_died;
                        break;
                    }
                }
                ++platform_types[object.attributes[2]];
                ++platform_kinds[object.platform.kind];
            } catch (const FormatError& error) {
                ++platform_faults;
                std::cerr << "platform fault: " << error.what() << '\n';
            }
        }
        // Traffic cars: each drives 600 frames along its road (a player crosses
        // the road from time to time to trigger the proximity sounds).
        PseudoWorld traffic_world;
        ObjectScriptVm traffic_vm(file, traffic_world, ScriptClass::Traffic);
        std::size_t traffic_cars = 0;
        std::size_t traffic_faults = 0;
        std::size_t traffic_segments = 0;
        for (const NodeView& node : file.nodes()) {
            const std::uint16_t subtype = node.type == 1 ? file.node_subtype(node.index) : 0;
            if (subtype < 0xd5 || subtype > 0xdc) {
                continue;
            }
            ++traffic_cars;
            try {
                const std::vector<std::uint16_t> road = file.links(node.index);
                ScriptVec3 where = file.node_position(road.front());
                where[1] -= 0x6e000;
                ScriptObject car = ScriptObject::construct_traffic(
                    static_cast<std::uint16_t>(node.index), subtype, road.front(), where,
                    file.node_orientation(node.index), static_cast<std::uint32_t>(traffic_cars));
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    traffic_world.advance();
                    traffic_world.tick_time();
                    const std::int32_t before = car.traffic.road_node;
                    traffic_vm.update_traffic(car);
                    if (car.traffic.road_node != before) {
                        ++traffic_segments;
                    }
                }
            } catch (const FormatError& error) {
                ++traffic_faults;
                std::cerr << "traffic fault: " << error.what() << '\n';
            }
        }
        const std::size_t traffic_assertions = traffic_world.assertions + traffic_vm.assertion_count();
        std::cout << "traffic_cars=" << traffic_cars << " traffic_segments=" << traffic_segments
                  << " traffic_faults=" << traffic_faults << " traffic_assertions=" << traffic_assertions << '\n';
        platform_faults += traffic_faults;
        // Power-ups: each type-5 node's pickup hovers (or drops) for 600 frames.
        PseudoWorld powerup_world;
        ObjectScriptVm powerup_vm(file, powerup_world, ScriptClass::Powerup);
        std::size_t powerups = 0;
        std::size_t powerup_faults = 0;
        std::size_t powerups_invalid = 0;
        for (const NodeView& node : file.nodes()) {
            if (node.type != 5) {
                continue;
            }
            const std::uint16_t subtype = file.node_subtype(node.index);
            if (histogram) {
                std::cout << "powerup_subtype 0x" << std::hex << subtype << std::dec << '\n';
            }
            // Powerup_Construct's dispatch (0x004a7db3) reaches its "Bad powerup type" assertion
            // for subtypes 0x13..0x17, which Downhill Jam's nodes carry: retail asserts too.
            if (subtype >= 0x13 && subtype <= 0x17) {
                ++powerups_invalid;
                continue;
            }
            ++powerups;
            try {
                const std::array<std::uint16_t, 3> words = file.node_orientation(node.index);
                ScriptObject pickup = powerup_vm.create_powerup(
                    static_cast<std::uint16_t>(node.index), file.node_subtype(node.index),
                    file.node_position(node.index), {}, words[1] == 0 ? 4 : 0, words[2]);
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    powerup_world.advance();
                    powerup_world.tick_time();
                    powerup_vm.update_powerup(pickup);
                    if ((pickup.list_flags & 0x40U) != 0) {
                        break;
                    }
                }
            } catch (const FormatError& error) {
                ++powerup_faults;
                std::cerr << "powerup fault: " << error.what() << '\n';
            }
        }
        const std::size_t powerup_assertions = powerup_world.assertions + powerup_vm.assertion_count();
        std::cout << "powerups=" << powerups << " powerups_invalid=" << powerups_invalid
                  << " powerup_faults=" << powerup_faults
                  << " powerup_assertions=" << powerup_assertions << '\n';
        if (powerup_world.assertions != 0) {
            std::cerr << "first power-up assertion: " << powerup_world.first_assertion << '\n';
        }
        platform_faults += powerup_faults;
        const std::size_t platform_assertions =
            platform_world.assertions + platform_vm.assertion_count() + traffic_assertions + powerup_assertions;
        std::cout << "platform_objects=" << platform_objects << " platform_commands=" << platform_commands
                  << " platform_died=" << platform_died << " platform_flying=" << platform_flying
                  << " platform_faults=" << platform_faults << " platform_assertions=" << platform_assertions
                  << " platform_distinct_opcodes=" << platform_opcodes.size() << '\n';
        if (platform_world.assertions != 0) {
            std::cerr << "first platform assertion: " << platform_world.first_assertion << '\n';
        }
        if (histogram) {
            for (const auto& [opcode, count] : opcodes) {
                std::cout << "opcode 0x" << std::hex << opcode << std::dec << " " << count << '\n';
            }
            for (const auto& [opcode, count] : platform_opcodes) {
                std::cout << "platform_opcode 0x" << std::hex << opcode << std::dec << " " << count << '\n';
            }
            for (const auto& [type, count] : platform_types) {
                std::cout << "platform_physics_type " << type << " " << count << '\n';
            }
            for (const auto& [kind, count] : platform_kinds) {
                std::cout << "platform_kind 0x" << std::hex << kind << std::dec << " " << count << '\n';
            }
        }
        return (faults != 0 || world.assertions != 0 || platform_faults != 0 || platform_assertions != 0) ? 1 : 0;
    } catch (const FormatError& error) {
        std::cerr << "TRG error: " << error.what() << '\n';
        return 1;
    }
}
