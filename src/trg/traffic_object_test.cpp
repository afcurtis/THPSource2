// Traffic cars (subtypes 0xd5..0xdc): construction, road following, turning and
// the proximity sounds, on a hand-built two-node road. The retail trace
// (traffic_object_retail_trace_test) covers New York's taxis and cable cars.
#include "object_script.hpp"

#include "tests/test_check.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace opentony::trg;

void put16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}
void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

// Two type-1000 nodes: a road from A (link to B) and back (B links to A), at the
// given positions in world units.
TrgFile make_road(std::array<std::int32_t, 3> a, std::array<std::int32_t, 3> b) {
    const std::size_t table = 12 + 3 * 4;
    const std::vector<std::size_t> starts{table, table + 40};  // two 40-byte nodes
    std::vector<std::byte> bytes(table + 80 + 4, std::byte{0});
    put32(bytes, 0, 0x4752545fU);
    put32(bytes, 4, 2);
    put32(bytes, 8, 3);
    put32(bytes, 12, static_cast<std::uint32_t>(starts[0]));
    put32(bytes, 16, static_cast<std::uint32_t>(starts[1]));
    put32(bytes, 20, static_cast<std::uint32_t>(table + 80));
    const std::array<std::array<std::int32_t, 3>, 2> positions{a, b};
    const std::array<std::uint16_t, 2> targets{1, 0};
    for (std::size_t index = 0; index < 2; ++index) {
        const std::size_t begin = starts[index];
        put16(bytes, begin, 1000);
        put16(bytes, begin + 2, 1);
        put16(bytes, begin + 4, targets[index]);
        const std::size_t position = (begin + 1 * 2 + 7) & ~static_cast<std::size_t>(3);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put32(bytes, position + axis * 4, static_cast<std::uint32_t>(positions[index][axis]));
        }
    }
    put16(bytes, table + 80, 0xff);
    return TrgFile::parse(bytes);
}

struct Recorder final : ObjectScriptServices {
    std::vector<std::string> events;
    std::vector<std::string> assertions;
    std::uint32_t draw{};
    ScriptVec3 player{};
    std::int32_t clock{1};
    void on_assertion(std::string_view message) override { assertions.emplace_back(message); }
    [[nodiscard]] std::int32_t animation_clock() const override { return clock; }
    [[nodiscard]] std::int32_t animation_time_scale() const override { return 0x100; }
    [[nodiscard]] ScriptVec3 player_position(std::size_t) const override { return player; }
    [[nodiscard]] std::uint32_t random(std::uint16_t) override { return draw; }
    void play_sound(std::int16_t id) override { events.push_back("sound " + std::to_string(id)); }
    void update_sound_position(std::int32_t handle, const ScriptVec3&) override {
        events.push_back("refresh " + std::to_string(handle));
    }
    void save_car_position(std::uint32_t index, const ScriptVec3&, const std::array<std::uint16_t, 3>&) override {
        events.push_back("save " + std::to_string(index));
    }
    void traffic_interaction(const ScriptObject&) override { events.push_back("interact"); }
};

struct Road {
    TrgFile file;
    Recorder services;
    ObjectScriptVm vm;
    ScriptObject car;
    explicit Road(std::uint16_t subtype, std::array<std::int32_t, 3> a = {0, 0, 0}, std::array<std::int32_t, 3> b = {100, 0, 0})
        : file(make_road(a, b)),
          vm(file, services, ScriptClass::Traffic),
          car(ScriptObject::construct_traffic(
              2, subtype, 0, file.node_position(0), {0, 0, 0}, 4)) {
        car.position[1] = static_cast<std::int32_t>(static_cast<std::uint32_t>(car.position[1]) - 0x6e000U);
    }
};

void construction() {
    const ScriptObject car = ScriptObject::construct_traffic(9, 0xd5, 3, {1, 2, 3}, {4, 5, 6}, 7);
    CHECK(car.node == 9);
    CHECK(car.subtype == 0xd5);
    CHECK(car.traffic.road_node == 3);
    CHECK(car.traffic.sequence == 7);
    CHECK(car.traffic.sound_period == 1);
    CHECK(car.state == 0x0002);
    CHECK(car.list_flags == 0x0012);
    CHECK(car.field_108 == 0x10000);
    CHECK(car.field_b4 == 0x3c);
    CHECK(ScriptObject::construct_traffic(0, 0xd6, 0, {}, {}, 0).traffic.sound_period == 0);  // police
}

void drives_along_the_road_and_turns() {
    Road road(0xd8);
    const ScriptVec3 start = road.car.position;
    road.vm.update_traffic(road.car);
    // First frame from rest: speed 0x1000, one unit of travel along +x.
    CHECK(road.car.position[0] == start[0] + 0x1000);
    CHECK(road.car.position[1] == start[1]);
    CHECK(road.car.velocity == (ScriptVec3{0x1000, 0, 0}));
    // Facing -x of the direction: yaw moves a quarter of the way (0xc00 is 270 degrees).
    CHECK(road.car.params[1] == 0xf00);
    CHECK(road.car.params[0] == 0);
    CHECK(road.services.events == (std::vector<std::string>{"interact", "save 4"}));
    CHECK(road.services.assertions.empty());
}

void reaching_a_node_moves_on_to_the_next_segment() {
    Road road(0xd8, {0, 0, 0}, {1, 0, 0});  // a one-unit segment
    road.car.velocity = {0x100000, 0, 0};
    road.vm.update_traffic(road.car);
    // The car overshoots node 1, is placed on it and continues along the link back.
    CHECK(road.car.traffic.road_node == 1);
    CHECK(road.services.assertions.empty());
}

void proximity_sounds() {
    // Cable car: fixed sound 0xfb once, then the latch holds until the player leaves.
    Road cable(0xd8);
    cable.services.player = {cable.car.position[0] + (10 << 12), cable.car.position[1], cable.car.position[2]};
    cable.vm.update_traffic(cable.car);
    CHECK(cable.car.traffic.latch == 1);
    CHECK(cable.services.events == (std::vector<std::string>{"interact", "sound 251", "save 4"}));
    cable.services.events.clear();
    cable.vm.update_traffic(cable.car);
    CHECK(cable.services.events == (std::vector<std::string>{"interact", "save 4"}));  // latched
    cable.services.player = {0x40000000, 0, 0};
    cable.vm.update_traffic(cable.car);
    CHECK(cable.car.traffic.latch == 0);

    // Taxi: Rnd(3) picks one of three sounds.
    for (std::uint32_t draw = 0; draw < 3; ++draw) {
        Road taxi(0xd5);
        taxi.services.draw = draw;
        taxi.services.player = {taxi.car.position[0] + (10 << 12), taxi.car.position[1], taxi.car.position[2]};
        taxi.vm.update_traffic(taxi.car);
        CHECK(taxi.services.events[1] == "sound " + std::to_string(0x6e + draw));
    }
    // Bus: Rnd(2) -> 0x122 or 0x123; kart: 0x110 or 0x11e; bull: 0x148 or 0x149; police 0x7a.
    const auto sound_for = [](std::uint16_t subtype, std::uint32_t draw) {
        Road road(subtype);
        road.services.draw = draw;
        road.services.player = {road.car.position[0] + (10 << 12), road.car.position[1], road.car.position[2]};
        road.vm.update_traffic(road.car);
        return road.services.events.size() > 1 ? road.services.events[1] : std::string("none");
    };
    CHECK(sound_for(0xd7, 0) == "sound 290");
    CHECK(sound_for(0xd7, 1) == "sound 291");
    CHECK(sound_for(0xd9, 0) == "sound 272");
    CHECK(sound_for(0xd9, 1) == "sound 286");
    CHECK(sound_for(0xdb, 0) == "sound 328");
    CHECK(sound_for(0xdb, 1) == "sound 329");
    CHECK(sound_for(0xd6, 0) == "sound 122");
    CHECK(sound_for(0xda, 0) == "sound -1");
}

void a_player_out_of_range_or_behind_is_ignored() {
    Road far(0xd8);
    far.services.player = {far.car.position[0] + (5000 << 12), far.car.position[1], far.car.position[2]};
    far.vm.update_traffic(far.car);
    CHECK(far.car.traffic.latch == 0);
    Road behind(0xd8);  // in range but not beside the car's direction of travel
    behind.services.player = {behind.car.position[0] - (10 << 12), behind.car.position[1], behind.car.position[2]};
    behind.vm.update_traffic(behind.car);
    CHECK(behind.car.traffic.latch == 0);
}

void positional_sound_refresh() {
    Road road(0xd8);
    road.car.traffic.sound_handle = 77;
    road.services.clock = 6;
    road.vm.update_traffic(road.car);  // period 1: every frame
    CHECK(road.services.events.front() == "refresh 77");
}

} // namespace

int main() {
    construction();
    drives_along_the_road_and_turns();
    reaching_a_node_moves_on_to_the_next_segment();
    proximity_sounds();
    a_player_out_of_range_or_behind_is_ignored();
    positional_sound_refresh();
    return 0;
}
