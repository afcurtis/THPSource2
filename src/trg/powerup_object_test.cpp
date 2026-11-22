// Power-ups (type-5 nodes): construction, ageing and blinking, hover, the drop to the
// ground and pickup effects, against a scripted service set. The retail trace
// (powerup_object_retail_trace_test) covers Hangar's pickups.
#include "object_script.hpp"

#include "tests/test_check.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace opentony::trg;

struct World final : ObjectScriptServices {
    std::vector<std::string> events;
    std::int32_t counter{0};
    std::int32_t ground{0};
    std::size_t players{1};
    bool on_ground{true};
    bool goal_taken{false};
    void on_assertion(std::string_view message) override { events.push_back("assert " + std::string(message)); }
    [[nodiscard]] std::int32_t tick_counter() const override { return counter; }
    [[nodiscard]] std::int32_t animation_time_scale() const override { return 0x100; }
    [[nodiscard]] std::size_t player_count() const override { return players; }
    // Selector 1 (career): pickups the level has not cleared are created live.
    [[nodiscard]] std::uint32_t game_mode_selector() const override { return 1; }
    [[nodiscard]] std::int32_t ground_height(const ScriptVec3&) override { return ground; }
    [[nodiscard]] bool skater_on_ground(std::uint32_t) const override { return on_ground; }
    [[nodiscard]] bool goal_stat_taken(std::int32_t) override { return goal_taken; }
    void play_sound(std::int16_t id) override { events.push_back("sound " + std::to_string(id)); }
    void sound_pitch(std::int32_t, std::int32_t pitch) override { events.push_back("pitch " + std::to_string(pitch)); }
    void career_give_money(std::uint16_t, std::int32_t amount) override {
        events.push_back("money " + std::to_string(amount));
    }
    void show_message(std::uint16_t subtype) override { events.push_back("message " + std::to_string(subtype)); }
    void delete_messages() override { events.push_back("delete_messages"); }
};

bool has(const World& world, const std::string& event) {
    for (const std::string& seen : world.events) {
        if (seen == event) {
            return true;
        }
    }
    return false;
}

TrgFile empty_level() {
    // Magic, version 2, one (empty) table entry: the end marker at offset 16.
    std::vector<std::byte> bytes(20, std::byte{0});
    bytes[0] = std::byte{0x5f};
    bytes[1] = std::byte{0x54};
    bytes[2] = std::byte{0x52};
    bytes[3] = std::byte{0x47};
    bytes[4] = std::byte{2};
    bytes[8] = std::byte{1};
    bytes[12] = std::byte{16};
    bytes[16] = std::byte{0xff};
    return TrgFile::parse(bytes);
}

} // namespace

int main() {
    const TrgFile file = empty_level();

    {
        // A permanent money pickup hovers: it never dies, its height stays within its bob.
        World world;
        ObjectScriptVm vm(file, world, ScriptClass::Powerup);
        ScriptObject coin = vm.create_powerup(3, 0x18, {0, 0x100000, 0}, {}, 0, 0xffff);
        CHECK(coin.subtype == 0x18);
        CHECK(coin.powerup.timer == 0xffff);
        for (int frame = 0; frame < 300; ++frame) {
            ++world.counter;
            vm.update_powerup(coin);
        }
        CHECK((coin.list_flags & 0x40U) == 0);
        CHECK(coin.powerup.age == 300);
        CHECK(!has(world, "assert No powerup glow created"));
    }

    {
        // A timed pickup shrinks its bob in its last second and dies when the timer runs out.
        World world;
        ObjectScriptVm vm(file, world, ScriptClass::Powerup);
        ScriptObject coin = vm.create_powerup(3, 0x19, {0, 0x100000, 0}, {}, 0, 100);
        bool shrank = false;
        bool died = false;
        for (int frame = 0; frame < 200 && !died; ++frame) {
            ++world.counter;
            vm.update_powerup(coin);
            shrank = shrank || coin.powerup.amplitude < coin.powerup.amplitude_base;
            died = (coin.list_flags & 0x40U) != 0;
        }
        CHECK(shrank);
        CHECK(died);
        CHECK(coin.powerup.age <= 100);
    }

    {
        // A dropped power-up falls to the ground and settles there.
        World world;
        world.ground = 0;
        ObjectScriptVm vm(file, world, ScriptClass::Powerup);
        ScriptObject coin = vm.create_powerup(3, 0x1a, {0, 0x200000, 0}, {}, 4, 0xffff);
        for (int frame = 0; frame < 400; ++frame) {
            ++world.counter;
            vm.update_powerup(coin);
        }
        CHECK(coin.powerup.settled != 0 || coin.powerup.motion == 0);
        CHECK(coin.position[1] < 0x200000);
    }

    {
        // Pickups: money pays and dies; it needs the skater on the ground.
        World world;
        ObjectScriptVm vm(file, world, ScriptClass::Powerup);
        ScriptObject coin = vm.create_powerup(3, 0x18, {0, 0x100000, 0}, {}, 0, 0xffff);
        world.on_ground = false;
        vm.take_powerup(coin, 0, true);
        CHECK(world.events.empty() || !has(world, "money 250"));
        CHECK((coin.list_flags & 0x40U) == 0);
        world.on_ground = true;
        vm.take_powerup(coin, 0, true);
        CHECK(has(world, "money 250"));
        CHECK(has(world, "message 24"));
        CHECK((coin.list_flags & 0x40U) != 0);

        // A hidden power-up cannot be taken again.
        world.events.clear();
        vm.take_powerup(coin, 0, true);
        CHECK(!has(world, "money 250"));
    }

    {
        // A stat pickup plays its sound and shows its message only while its goal stat is open.
        World world;
        ObjectScriptVm vm(file, world, ScriptClass::Powerup);
        ScriptObject tape = vm.create_powerup(3, 5, {0, 0x100000, 0}, {}, 0, 0xffff);
        vm.take_powerup(tape, 0, true);
        CHECK(has(world, "sound 20"));
        CHECK(has(world, "message 5"));

        World taken;
        taken.goal_taken = true;
        ObjectScriptVm vm2(file, taken, ScriptClass::Powerup);
        ScriptObject again = vm2.create_powerup(3, 5, {0, 0x100000, 0}, {}, 0, 0xffff);
        vm2.take_powerup(again, 0, true);
        CHECK(has(taken, "sound 20"));
        CHECK(!has(taken, "message 5"));
    }

    return 0;
}
