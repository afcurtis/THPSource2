// The platform (subtype 0x0192) commands, update and hit against hand-built
// scripts. The retail traces cover the bouncy props of Warehouse; the commands
// the shipped scripts use elsewhere (scale animation, move-to, shatter, halt)
// are pinned here from the disassembly.
#include "object_script.hpp"

#include "tests/test_check.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace opentony::trg;
using Words = std::vector<std::uint16_t>;

void put16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}
void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

// One type-1000 script point holding `script`; the commands start 2 bytes past
// a 4-byte boundary, so the first opcode's operands begin aligned.
TrgFile make_file(const Words& script) {
    const std::size_t begin = 12 + 2 * 4;
    const std::size_t position = (begin + 7) & ~static_cast<std::size_t>(3);
    std::size_t end = position + 12 + 6 + script.size() * 2;
    end = (end + 3) & ~static_cast<std::size_t>(3);
    std::vector<std::byte> bytes(end + 4, std::byte{0});
    put32(bytes, 0, 0x4752545fU);
    put32(bytes, 4, 2);
    put32(bytes, 8, 2);
    put32(bytes, 12, static_cast<std::uint32_t>(begin));
    put32(bytes, 16, static_cast<std::uint32_t>(end));
    put16(bytes, begin, 1000);
    put16(bytes, begin + 2, 0);
    std::size_t at = position + 18;
    for (const std::uint16_t word : script) {
        put16(bytes, at, word);
        at += 2;
    }
    put16(bytes, end, 0xff);
    return TrgFile::parse(bytes);
}

struct Recorder final : ObjectScriptServices {
    std::vector<std::string> assertions;
    std::vector<std::string> events;
    std::uint32_t random_value{};
    void on_assertion(std::string_view message) override { assertions.emplace_back(message); }
    [[nodiscard]] std::uint32_t random(std::uint16_t) override { return random_value; }
    void play_sound(std::int16_t id) override { events.push_back("sound " + std::to_string(id)); }
    void skater_grunt(std::uint32_t) override { events.push_back("grunt"); }
    void vibrate(std::int32_t a, std::int32_t b, std::int32_t c) override {
        events.push_back("vibrate " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c));
    }
    void shatter_item(const ScriptObject&, int mode) override { events.push_back("shatter " + std::to_string(mode)); }
    void item_flash(std::uint8_t a, std::uint8_t, std::uint8_t, std::uint8_t) override {
        events.push_back("flash " + std::to_string(a));
    }
};

struct Fixture {
    TrgFile file;
    Recorder services;
    ObjectScriptVm vm;
    ScriptObject object;
    explicit Fixture(const Words& script)
        : file(make_file(script)),
          vm(file, services, ScriptClass::Platform),
          object(ScriptObject::construct_platform(0, 0, {0, 0, 0}, {0, 0, 0})) {
        object.cursor = vm.script_point_cursor(0);
    }
    ScriptRunStats update() { return vm.update_platform(object); }
};

constexpr std::uint16_t kEnd = 0x4100;

void construction_matches_the_constructor() {
    const ScriptObject object = ScriptObject::construct_platform(7, 100, {1, 2, 3}, {4, 5, 6});
    CHECK(object.node == 7);
    CHECK(object.state == 0x0111);       // (state & ~0x02) | 0x111
    CHECK(object.list_flags == 0x0012);  // base flags, still suspend-when-far
    CHECK(object.attributes[0] == 0x20); // arrival distance
    CHECK(object.platform.kind == 0x192);
    CHECK(object.platform.sound_period == -1);
    CHECK(object.field_ae == 0x80);
}

void scale_animation_steps_each_frame() {
    // 0x4306: X scale +50% over 4 frames, applied immediately once and then per frame.
    Fixture f({0x4306, 50, 4, kEnd});
    f.update();
    CHECK((f.object.state & 0x200U) != 0);
    const std::int16_t step = static_cast<std::int16_t>((50 * 0x1000) / (4 * 100));
    CHECK(f.object.platform.scale_step[0] == step);
    CHECK(f.object.platform.scale_frames[0] == 4);
    CHECK(f.object.platform.scale[0] == 0x1000 + step);
    CHECK(f.object.platform.scale[1] == 0x1000);
    for (int frame = 0; frame < 4; ++frame) {
        f.update();
    }
    CHECK(f.object.platform.scale[0] == 0x1000 + step * 5);
    CHECK(f.object.platform.scale_frames[0] == 0);
    f.update();  // finished: no further change
    CHECK(f.object.platform.scale[0] == 0x1000 + step * 5);
    CHECK(f.services.assertions.empty());
}

void all_axis_scale_with_no_frames_applies_at_once() {
    Fixture f({0x4304, 0xfff6, 0, kEnd});  // -10% over 0 frames: divided by 100
    f.update();
    const std::int16_t step = static_cast<std::int16_t>((-10 * 0x1000) / 100);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CHECK(f.object.platform.scale[axis] == 0x1000 + step);
        CHECK(f.object.platform.scale_frames[axis] == 0);
    }
}

void move_to_steers_and_arrives() {
    // 0x4220 target 5000 world units along +x. Attribute 0 is both the speed
    // and the arrival distance; type 0 sets the velocity from the heading.
    Fixture f({0x4220, 5000, 0, 0, 0, 0, 0, 0x4300});
    f.object.attributes[0] = 0x1000;
    f.update();
    CHECK((f.object.flags & 1U) != 0);
    CHECK(f.object.platform.target[0] == 5000 * 4096);
    CHECK(f.object.velocity[0] > 0x800);  // heading +x, above the physics dead zone
    CHECK(f.object.position[0] == f.object.velocity[0]);
    // Once within the arrival distance the object snaps onto the target and stops.
    f.object.position = {5000 * 4096 - 0x800 * 4096, 0, 0};  // 2048 units away, inside 0x1000
    f.object.flags |= 1U;
    f.update();
    CHECK(f.object.position[0] == 5000 * 4096);
    CHECK(f.object.velocity == (ScriptVec3{0, 0, 0}));
    CHECK((f.object.flags & 1U) == 0);
}

void relative_target_adds_the_position() {
    Fixture f({0x4222, 3, 0, 0, 0, 0, 0, kEnd});
    f.object.position = {5 * 4096, 0, 0};
    f.update();
    CHECK(f.object.platform.target[0] == 8 * 4096);
}

void shatter_kills_and_yields() {
    Fixture f({0x4301, kEnd});
    const auto stats = f.update();
    CHECK(stats.yielded);
    CHECK((f.object.list_flags & 0x40U) != 0);  // dead
    CHECK((f.object.state & 1U) != 0);
    CHECK(f.services.events == std::vector<std::string>{"shatter 0"});
}

void die_command_yields_and_halt_repeats() {
    Fixture die({0x4205, kEnd});
    CHECK(die.update().yielded);
    CHECK((die.object.list_flags & 0x40U) != 0);

    Fixture halt({0x4300, kEnd});
    const std::uint32_t cursor = halt.object.cursor;
    CHECK(halt.update().yielded);
    CHECK(halt.object.cursor == cursor);  // the halt re-executes forever
    CHECK(halt.update().yielded);
    CHECK(halt.object.cursor == cursor);
}

void hit_damages_or_destroys() {
    Fixture f({kEnd});
    f.object.attributes[1] = 1;
    f.object.field_b4 = 5;
    f.vm.hit_platform(f.object, 2);
    CHECK(static_cast<std::int16_t>(f.object.field_b4) == 3);
    CHECK((f.object.list_flags & 0x40U) == 0);
    CHECK(f.object.byte_16f == 1);
    CHECK(f.services.events == std::vector<std::string>{"flash 3"});
    f.vm.hit_platform(f.object, 3);
    CHECK((f.object.list_flags & 0x40U) != 0);
    CHECK(f.services.events.back() == "shatter 1");

    Fixture unarmed({kEnd});  // no hit points flag: the hit only counts
    unarmed.vm.hit_platform(unarmed.object, 9);
    CHECK(unarmed.object.byte_16f == 1);
    CHECK((unarmed.object.list_flags & 0x40U) == 0);
}

void physics_type_zero_snaps_small_velocity() {
    Fixture f({kEnd});
    f.object.velocity = {0x400, 0x2000, -0x400};  // inside +-0x800 snaps to zero
    f.object.platform.angular_velocity = {1, 0, -1};
    f.update();
    CHECK(f.object.velocity == (ScriptVec3{0, 0x2000, 0}));
    CHECK(f.object.position == (ScriptVec3{0, 0x2000, 0}));
    CHECK(f.object.params[0] == 1);
    CHECK(f.object.params[2] == 0xfff);  // wraps into 12 bits
}

void physics_type_one_damps_velocity() {
    Fixture f({kEnd});
    f.object.attributes[2] = 1;
    f.object.platform.velocity_decay = {4, 4, 4};
    f.object.velocity = {0x10000, 0, 0};
    f.update();
    // velocity += acceleration (0), decays by v >> 4, then integrates.
    const std::int32_t expected = 0x10000 - (0x10000 >> 4);
    CHECK(f.object.velocity[0] == expected);
    CHECK(f.object.position[0] == expected);
}

void undefined_type_asserts() {
    Fixture f({kEnd});
    f.object.attributes[2] = 9;
    f.update();
    CHECK(f.services.assertions.size() == 1);
}

void signal_latch_counts_signals() {
    Fixture f({0x4300});
    f.object.event_word = 1;
    f.update();
    CHECK(f.object.signal_latch == 1);
    CHECK(f.object.byte_170 == 1);
    CHECK((f.object.event_word & 1U) == 0);
}

void wait_uses_the_timed_form() {
    // The platform constructor sets state bit 0x100, so 0x4280 stores the raw
    // count and starts counting simulation time instead of shifting it left.
    Fixture f({0x4280, 3, 0x4290, 1, kEnd});
    f.update();
    CHECK(f.object.wait == 3);
    CHECK(f.object.field_1dc == 0);
    // A wait no longer than the elapsed count clears without running the script.
    f.object.field_1dc = 3;
    f.update();
    CHECK(f.object.wait == 0);
}

// CPlatform__Bounce for the kinds the retail traces do not reach (2 and 4), and
// the retail-confirmed kinds 1, 3 and 5 by their launch formulas.
void bounce_kinds_launch_from_the_skater() {
    const auto launch = [](std::uint16_t kind, ScriptVec3 skater) {
        Fixture f({kEnd});
        f.object.flags |= 0x10;
        f.object.platform.kind = kind;
        f.object.params = {0x10, 0x20, 0x30};
        f.vm.bounce_platform(f.object, 7, skater);
        return f;
    };
    {   // kind 2: 1.5x horizontal velocity, straight up, sound 0x6a, no grunt
        Fixture f = launch(2, {0x100000, 0x777, 0x40000});
        CHECK(f.object.velocity == (ScriptVec3{0x180000, static_cast<std::int32_t>(0xfffb0000U), 0x60000}));
        CHECK((f.object.flags & 0x20U) != 0);
        CHECK(f.object.platform.bounce_count == 0);
        CHECK(f.services.events == (std::vector<std::string>{"sound 106", "vibrate 2,1,180"}));
        // Rnd draws are all 0 here: target angles are angle + 0*0x800 - 0x400.
        CHECK(f.object.platform.bounce_angles[0] == static_cast<std::int16_t>(0x10 - 0x400));
        CHECK(f.object.platform.bounce_angles[2] == static_cast<std::int16_t>(0x30 - 0x400));
    }
    {   // kind 4: grunt first, then sound 0x52
        Fixture f = launch(4, {0x100000, 0, 0x40000});
        CHECK(f.services.events == (std::vector<std::string>{"grunt", "sound 82", "vibrate 2,1,180"}));
        CHECK(f.object.velocity[1] == static_cast<std::int32_t>(0xfffb0000U));
    }
    {   // kind 5: vibration only
        Fixture f = launch(5, {0x100000, 0, 0});
        CHECK(f.services.events == (std::vector<std::string>{"vibrate 2,1,180"}));
    }
    {   // kinds 1 and 3: the skater's horizontal velocity, launched upward harder
        // when it is slow (speed < 0x28 units) and re-aimed when very slow (< 0x12).
        Fixture slow = launch(3, {0x1000, 0, 0x1000});  // speed 1 unit
        CHECK(slow.object.velocity == (ScriptVec3{0x1000, static_cast<std::int32_t>(0xfffe0000U), 0x1000}));
        CHECK(slow.services.events == (std::vector<std::string>{"sound 60", "grunt", "vibrate 6,1,120"}));
        Fixture fast = launch(1, {0x30000, 0, 0x30000});  // ~ 0x30 units
        CHECK(fast.object.velocity[1] == static_cast<std::int32_t>(0xfffc0000U));
        CHECK(fast.services.events.front() == "sound 104");
    }
    {   // a flying or non-bouncy object ignores the bounce
        Fixture f({kEnd});
        f.vm.bounce_platform(f.object, 0, {1, 2, 3});
        CHECK((f.object.flags & 0x20U) == 0);
        CHECK(f.services.events.empty());
    }
}

} // namespace

int main() {
    bounce_kinds_launch_from_the_skater();
    construction_matches_the_constructor();
    scale_animation_steps_each_frame();
    all_axis_scale_with_no_frames_applies_at_once();
    move_to_steers_and_arrives();
    relative_target_adds_the_position();
    shatter_kills_and_yields();
    die_command_yields_and_halt_repeats();
    hit_damages_or_destroys();
    physics_type_zero_snaps_small_velocity();
    physics_type_one_damps_velocity();
    undefined_type_asserts();
    signal_latch_counts_signals();
    wait_uses_the_timed_form();
    return 0;
}
