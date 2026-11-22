#include "object_script.hpp"

#include "tests/test_check.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using opentony::trg::ObjectScriptServices;
using opentony::trg::ObjectScriptVm;
using opentony::trg::ScriptObject;
using opentony::trg::ScriptVec3;
using opentony::trg::TrgFile;

using Words = std::vector<std::uint16_t>;

void put16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

// Builds a TRG file whose nodes are all type-1000 script points, one per
// script, each with `links` as its link list. The nodes start on 4-byte
// boundaries so the position records align exactly as retail expects.
TrgFile make_file(const std::vector<Words>& scripts, const std::vector<Words>& link_lists = {}) {
    const std::size_t table_end = 12 + (scripts.size() + 1) * 4;
    std::vector<std::size_t> starts;
    std::size_t cursor = table_end;
    std::vector<std::size_t> sizes;
    for (std::size_t index = 0; index < scripts.size(); ++index) {
        const std::size_t links = index < link_lists.size() ? link_lists[index].size() : 0;
        // type, link count, links..., pad to 4 for position, 12 bytes position,
        // 6 bytes of parameters, commands.
        const std::size_t position = (cursor + links * 2 + 7) & ~static_cast<std::size_t>(3);
        std::size_t end = position + 12 + 6 + scripts[index].size() * 2;
        end = (end + 3) & ~static_cast<std::size_t>(3);
        starts.push_back(cursor);
        sizes.push_back(end - cursor);
        cursor = end;
    }
    std::vector<std::byte> bytes(cursor + 4, std::byte{0});
    put32(bytes, 0, 0x4752545fU);
    put32(bytes, 4, 2);
    put32(bytes, 8, static_cast<std::uint32_t>(scripts.size() + 1));
    for (std::size_t index = 0; index < scripts.size(); ++index) {
        put32(bytes, 12 + index * 4, static_cast<std::uint32_t>(starts[index]));
    }
    put32(bytes, 12 + scripts.size() * 4, static_cast<std::uint32_t>(cursor));
    for (std::size_t index = 0; index < scripts.size(); ++index) {
        const std::size_t links = index < link_lists.size() ? link_lists[index].size() : 0;
        const std::size_t begin = starts[index];
        put16(bytes, begin, 1000);
        put16(bytes, begin + 2, static_cast<std::uint16_t>(links));
        for (std::size_t link = 0; link < links; ++link) {
            put16(bytes, begin + 4 + link * 2, link_lists[index][link]);
        }
        const std::size_t position = (begin + links * 2 + 7) & ~static_cast<std::size_t>(3);
        std::size_t at = position + 12 + 6;
        for (const std::uint16_t word : scripts[index]) {
            put16(bytes, at, word);
            at += 2;
        }
    }
    put16(bytes, cursor, 0xff);
    return TrgFile::parse(bytes);
}

struct Recorder final : ObjectScriptServices {
    std::vector<std::string> assertions;
    std::vector<std::string> events;
    std::uint32_t mode{0};
    bool has_target_flag{false};
    ScriptVec3 target{};
    std::uint32_t random_value{0};

    void on_assertion(std::string_view message) override { assertions.emplace_back(message); }
    [[nodiscard]] std::uint32_t game_mode_selector() const override { return mode; }
    [[nodiscard]] std::uint32_t random(std::uint16_t range) override {
        events.push_back("random " + std::to_string(range));
        return random_value;
    }
    [[nodiscard]] bool has_target() const override { return has_target_flag; }
    [[nodiscard]] ScriptVec3 target_position() const override { return target; }
    void play_sound(std::int16_t id) override { events.push_back("sound " + std::to_string(id)); }
    void animation_cycle(std::uint16_t a, std::uint16_t b) override {
        events.push_back("cycle " + std::to_string(a) + " " + std::to_string(b));
    }
    void animation_start(std::uint16_t a) override { events.push_back("start " + std::to_string(a)); }
    void signal_node_links(std::uint16_t node) override { events.push_back("signal-links " + std::to_string(node)); }
    void execute_node_links(std::uint16_t node) override { events.push_back("exec-links " + std::to_string(node)); }
    void signal_objects_at_node(std::uint16_t node) override { events.push_back("signal-objects " + std::to_string(node)); }
    void send_pulse(std::uint16_t node) override { events.push_back("pulse " + std::to_string(node)); }
    void apply_camera_shake(const ScriptVec3&, std::uint32_t type) override {
        events.push_back("shake " + std::to_string(type));
    }
    void on_repeated_effect(const opentony::trg::ScriptRepeatedEffect& effect) override {
        events.push_back("effect " + std::to_string(effect.count));
    }
    void on_flash(const opentony::trg::ScriptFlashEvent& flash) override {
        events.push_back("flash " + std::to_string(flash.c0) + "," + std::to_string(flash.c1) + ","
            + std::to_string(flash.c2) + "," + std::to_string(flash.duration));
    }
};

struct Fixture {
    TrgFile file;
    Recorder services;
    ObjectScriptVm vm;
    ScriptObject object;

    explicit Fixture(const std::vector<Words>& scripts, const std::vector<Words>& links = {})
        : file(make_file(scripts, links)),
          vm(file, services),
          object(ScriptObject::construct_cb(0, 0, {0, 0, 0}, {0, 0, 0})) {
        object.cursor = vm.script_point_cursor(0);
    }

    opentony::trg::ScriptRunStats run() { return vm.run(object, object.cursor); }
};

constexpr std::uint16_t kEnd = 0x4100;
constexpr std::uint16_t kEndif = 0x4120;

void terminator_and_registers() {
    // reg[1] = 5; reg[1] += 3; reg[1] -= 2
    Fixture f({{0x2120, 1, 5, 0x4110, 0x2120, 1, 3, 0x4111, 0x2120, 1, 2, kEnd}});
    const auto stats = f.run();
    CHECK(stats.finished);
    CHECK(!stats.yielded);
    CHECK(f.object.active == 0);
    CHECK(f.object.registers[1] == 6);
    CHECK(f.services.assertions.empty());
    CHECK(stats.commands == 3);
}

void position_read_modify_write() {
    // X = 4; Y = -2 (0xfffe immediate); X += Y-independent 5 through the scratch buffer
    Fixture f({{0x2140, 4, 0x2141, 0xfffe, 0x4110, 0x2140, 5, kEnd}});
    f.run();
    CHECK(f.object.position[0] == 9 * 4096);
    CHECK(f.object.position[1] == -2 * 4096);
    CHECK(f.services.assertions.empty());
}

void variable_reads_and_conditionals() {
    // reg[0] = X-position variable (7); if reg[0] == 7 { sound 3 } endif;
    // if reg[0] > 9 { sound 4 } endif
    Fixture g({{
        0x2120, 0, 0x2140,           // reg[0] = variable X (>>12)
        0x4114, 0x2120, 0, 7,        // if reg[0] == 7  (variable read consumes the index word)
        0x4290, 3,
        kEndif,
        0x4112, 0x2120, 0, 9,        // if reg[0] > 9   (false, skipped)
        0x4290, 4,
        kEndif,
        kEnd,
    }});
    g.object.position[0] = 7 * 4096;
    g.run();
    CHECK(g.object.registers[0] == 7);
    CHECK(g.services.events == std::vector<std::string>{"sound 3"});
}

void nested_conditionals_skip_whole_block() {
    Fixture f({{
        0x4114, 1, 2,                    // false
        0x4114, 1, 1, 0x4290, 1, kEndif, // nested, must be skipped as a unit
        0x4290, 2,
        kEndif,
        0x4290, 5,
        kEnd,
    }});
    f.run();
    CHECK(f.services.events == std::vector<std::string>{"sound 5"});
    CHECK(f.services.assertions.empty());
}

void labels_and_goto_loop() {
    Fixture f({{
        0x2120, 0, 0,               // reg0 = 0
        0x4104, 0,                  // label 0
        0x4110, 0x2120, 0, 1,       // reg0 += 1
        0x4113, 0x2120, 0, 3,       // if reg0 < 3
        0x4101, 0,                  //   goto 0
        kEndif,
        0x4290, 9,
        kEnd,
    }});
    f.run();
    CHECK(f.object.registers[0] == 3);
    CHECK(f.services.events == std::vector<std::string>{"sound 9"});
}

void label_prescan_and_forward_goto() {
    Fixture f({{
        0x4105,                     // scan labels up to the terminator
        0x4101, 2,                  // goto label 2 (defined later)
        0x4290, 1,
        0x4104, 2,
        0x4290, 2,
        kEnd,
    }});
    f.run();
    CHECK(f.services.events == std::vector<std::string>{"sound 2"});
}

void wait_and_resume() {
    // wait 2 (fixed 12-bit units), then a sound
    Fixture f({{0x4280, 2, 0x4290, 6, kEnd}});
    auto stats = f.run();
    CHECK(stats.yielded);
    CHECK(f.object.wait == 2U << 12U);
    CHECK(f.services.events.empty());
    stats = f.vm.update(f.object, 4096);          // 1 unit left
    CHECK(f.object.wait == 4096);
    CHECK(f.services.events.empty());
    stats = f.vm.update(f.object, 4096);          // reaches zero
    CHECK(f.object.wait == 0);
    CHECK(f.services.events.empty());
    stats = f.vm.update(f.object, 4096);          // runs the rest
    CHECK(stats.finished);
    CHECK(f.services.events == std::vector<std::string>{"sound 6"});
    CHECK(f.object.active == 0);
}

void wait_for_signal_rewinds_until_signalled() {
    Fixture f({{0x4281, 0x4290, 8, kEnd}});
    f.vm.update(f.object, 0);
    CHECK(f.services.events.empty());
    f.vm.update(f.object, 0);
    CHECK(f.services.events.empty());
    f.object.event_word = 1;
    f.vm.update(f.object, 0);
    CHECK(f.object.byte_170 == 1);
    CHECK(f.object.event_word == 0);
    CHECK(f.services.events == std::vector<std::string>{"sound 8"});
}

void messaging_commands() {
    Fixture f({{0x42b1, 7, 0x42b2, 8, 0x42b3, 9, 0x42b4, 10, kEnd}});
    f.run();
    CHECK(f.services.events
        == (std::vector<std::string>{"signal-links 7", "exec-links 8", "signal-objects 9", "pulse 10"}));
}

void die_sends_the_death_pulse_once() {
    Fixture f({{0x4205, 0x4205, kEnd}});
    f.object.node = 3;
    f.run();
    CHECK(f.object.list_flags & 0x40);
    CHECK(f.object.death_pulse_sent == 1);
    CHECK(f.services.events == std::vector<std::string>{"exec-links 3"});
    // The finished script makes the frame update call Die again: no second pulse.
    f.vm.update(f.object, 0);
    CHECK(f.services.events.size() == 1);
}

void unknown_command_is_diagnosed_and_skipped() {
    Fixture f({{0x4fff, 0x4290, 1, kEnd}});
    f.run();
    CHECK(f.services.assertions.size() == 1);
    CHECK(f.services.events == std::vector<std::string>{"sound 1"});
}

void jump_to_a_script_point() {
    // Node 0 sets a register then jumps to node 1's commands.
    Fixture f({{0x2120, 0, 1, 0x4106, 1, kEnd}, {0x2120, 1, 2, kEnd}});
    f.run();
    CHECK(f.object.registers[0] == 1);
    CHECK(f.object.registers[1] == 2);
}

void goal_flag_and_mode_gating() {
    Fixture f({{0x4121, 3, kEnd}});
    f.services.mode = 0;
    f.run();
    CHECK(f.services.assertions.empty());
}

void random_variable_and_links_variable() {
    Fixture f({{0x2120, 0, 0x2129, 100, 0x2120, 1, 0x212b, 1, kEnd}, {kEnd}}, {{}, {5, 6}});
    f.services.random_value = 42;
    f.run();
    CHECK(f.object.registers[0] == 42);
    CHECK(f.object.registers[1] == 5);
    CHECK(f.services.events == std::vector<std::string>{"random 100"});
}

void attributes_and_state_flags() {
    Fixture f({{0x2125, 2, 0x1234, 0x2131, 1, 0x4204, 0x4203, kEnd}});
    f.run();
    CHECK(f.object.attributes[2] == 0x1234);
    CHECK((f.object.state & 0x08) != 0);
    // 0x4204 sets the low bits, 0x4203 clears bit 0 again.
    CHECK((f.object.state & 0x41) == 0x40);
}

void flags_tests_and_bad_registers() {
    Fixture f({{0x4227, 1, 0x4115, 2, 0x4290, 1, kEndif, 0x4116, 4, 0x4290, 2, kEndif, 0x2120, 9, 0, kEnd}});
    f.run();
    CHECK(f.services.events == (std::vector<std::string>{"sound 1", "sound 2"}));
    CHECK(f.services.assertions.size() == 1);   // register index 9 is out of bounds
}

void cb_position_and_effect_commands() {
    Fixture f({{0x4506, 0x4292, 3, 0x429c, 1, 2, 3, 40, 5, kEnd}});
    f.services.has_target_flag = true;
    f.services.target = {10, 20, 30};
    f.run();
    CHECK(f.object.position == (ScriptVec3{10, 20, 30}));
    CHECK(f.services.events == (std::vector<std::string>{"effect 3", "flash 1,2,3,40"}));
}

void frame_update_signal_latch() {
    Fixture f({{0x4290, 1, 0x4240, kEnd}});
    f.object.event_word = 3;
    f.vm.update(f.object, 0);
    CHECK(f.object.signal_latch == 3);
    CHECK(f.object.event_word == 2);
    CHECK(f.object.byte_170 == 1);
    CHECK(f.object.active == 0);
    CHECK(f.object.cursor > 0);
    // Inactive: the next frame update kills the object instead of running.
    f.vm.update(f.object, 0);
    CHECK((f.object.list_flags & 0x40) != 0);
}

} // namespace

int main() {
    terminator_and_registers();
    position_read_modify_write();
    variable_reads_and_conditionals();
    nested_conditionals_skip_whole_block();
    labels_and_goto_loop();
    label_prescan_and_forward_goto();
    wait_and_resume();
    wait_for_signal_rewinds_until_signalled();
    messaging_commands();
    die_sends_the_death_pulse_once();
    unknown_command_is_diagnosed_and_skipped();
    jump_to_a_script_point();
    goal_flag_and_mode_gating();
    random_variable_and_links_variable();
    attributes_and_state_flags();
    flags_tests_and_bad_registers();
    cb_position_and_effect_commands();
    frame_update_signal_latch();
    return 0;
}
