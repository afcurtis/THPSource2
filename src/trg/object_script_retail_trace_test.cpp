// Replays a retail level-object script trace (see
// scripts/make_script_trace_fixture.py) through the native script interpreter.
//
//   opentony_object_script_retail_trace_test FIXTURE.txt LEVEL.TRG
//
// Every retail update of a subtype-0xcb object is replayed from the raw object
// image retail entered it with (no state carried between updates). The native
// interpreter must dispatch the same commands with the same cursors, read the
// same variables, make the same side-effect calls in the same order, and leave
// the object with the same modelled fields as retail did.
//
// Exit codes: 0 all updates match, 1 mismatch, 2 usage/format error, 77 the
// level file is not available (extract the game data first).
#include "object_script.hpp"
#include "world_objects.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::trg;

namespace {

constexpr std::size_t kImageWords = 0x200 / 4;
using Image = std::array<std::uint32_t, kImageWords>;

std::uint32_t rd32(const Image& image, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(image.data()) + offset, 4);
    return value;
}
std::uint16_t rd16(const Image& image, std::size_t offset) {
    std::uint16_t value = 0;
    std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(image.data()) + offset, 2);
    return value;
}
std::uint8_t rd8(const Image& image, std::size_t offset) {
    return reinterpret_cast<const std::uint8_t*>(image.data())[offset];
}

struct RetailCommand {
    std::uint16_t opcode{};
    std::uint32_t cursor_after{};
};
struct RetailVariable {
    std::uint16_t id{};
    std::uint32_t result{};
};
struct RetailCall {
    std::string name;
    std::vector<std::int64_t> values;
};
struct RetailUpdate {
    long seq{};
    std::uint32_t object{};
    std::uint32_t dt{};
    std::uint32_t sim{};
    std::uint32_t mode{};
    bool has_target{};
    bool moved{};
    ScriptVec3 target{};
    Image entry{};
    Image exit{};
    std::vector<RetailCommand> commands;
    std::vector<RetailVariable> variables;
    std::vector<RetailCall> calls;
    std::vector<std::pair<std::string, std::int64_t>> queries;
};

void apply_delta(Image& image, std::istringstream& tokens) {
    std::string token;
    while (tokens >> token) {
        const std::size_t colon = token.find(':');
        const std::size_t index = std::stoul(token.substr(0, colon));
        image.at(index) = static_cast<std::uint32_t>(std::stoul(token.substr(colon + 1), nullptr, 16));
    }
}

// Rebuilds a ScriptObject from a retail object image. Retail keeps absolute
// pointers; the native cursor space is file offsets.
ScriptObject object_from_image(const Image& image, std::uint32_t base) {
    ScriptObject object;
    object.state = rd16(image, 0x04);
    object.position = {static_cast<std::int32_t>(rd32(image, 0x08)), static_cast<std::int32_t>(rd32(image, 0x0c)),
                       static_cast<std::int32_t>(rd32(image, 0x10))};
    object.params = {rd16(image, 0x14), rd16(image, 0x16), rd16(image, 0x18)};
    object.model_index = rd16(image, 0x1a);
    object.region = rd8(image, 0x1f);
    object.velocity = {static_cast<std::int32_t>(rd32(image, 0x4c)), static_cast<std::int32_t>(rd32(image, 0x50)),
                       static_cast<std::int32_t>(rd32(image, 0x54))};
    object.event_word = rd16(image, 0x68);
    object.list_flags = rd16(image, 0x6a);
    object.field_ae = rd16(image, 0xae);
    object.node = rd16(image, 0xb0);
    object.field_b4 = rd16(image, 0xb4);
    object.field_108 = static_cast<std::int32_t>(rd32(image, 0x108));
    object.active = rd8(image, 0x16c);
    object.byte_16f = rd8(image, 0x16f);
    object.byte_170 = rd8(image, 0x170);
    object.death_pulse_sent = rd8(image, 0x171);
    object.byte_172 = rd8(image, 0x172);
    object.byte_173 = rd8(image, 0x173);
    object.signal_latch = rd16(image, 0x174);
    object.flags = rd32(image, 0x178);
    object.cursor = rd32(image, 0x17c) - base;
    for (std::size_t label = 0; label < kScriptLabelCount; ++label) {
        const std::uint32_t value = rd32(image, 0x180 + label * 4);
        object.labels[label] = value == 0 ? 0 : value - base;
    }
    for (std::size_t index = 0; index < kScriptAttributeCount; ++index) {
        object.attributes[index] = rd16(image, 0x1a0 + index * 2);
    }
    object.field_1ac = rd16(image, 0x1ac);
    object.height_1ae = rd16(image, 0x1ae);
    for (std::size_t index = 0; index < kScriptRegisterCount; ++index) {
        object.registers[index] = rd16(image, 0x1b0 + index * 2);
    }
    object.field_1bc = rd16(image, 0x1bc);
    object.field_1be = rd16(image, 0x1be);
    object.wait = rd32(image, 0x1c0);
    object.context = rd32(image, 0x1d8);
    object.field_1dc = rd32(image, 0x1dc);
    object.field_1e0 = rd32(image, 0x1e0);
    object.sound_handle = static_cast<std::int32_t>(rd32(image, 0x1e8));
    object.sound_period = static_cast<std::int32_t>(rd32(image, 0x1ec));
    object.sound_scale = rd32(image, 0x1f0);
    return object;
}

// Replays retail's world: query results come from the trace.
class ReplayServices final : public ObjectScriptServices {
public:
    explicit ReplayServices(const TrgFile& file) : file_(file) {}

    void load(const RetailUpdate& update) {
        mode_ = update.mode;
        has_target_ = update.has_target;
        target_ = update.target;
        sim_ = update.sim;
        variable_results_.clear();
        for (const RetailVariable& variable : update.variables) {
            variable_results_[variable.id].push_back(variable.result);
        }
        queries_.clear();
        for (const auto& [name, value] : update.queries) {
            queries_[name].push_back(value);
        }
        effect_slots_ = false;
        for (const RetailCall& call : update.calls) {
            if (call.name == "effect_spawn") {
                effect_slots_ = true;
            }
        }
        events.clear();
        assertions = 0;
    }

    std::vector<RetailCall> events;
    std::size_t assertions{};

    void on_assertion(std::string_view) override { ++assertions; }
    [[nodiscard]] std::uint32_t game_mode_selector() const override { return mode_; }
    [[nodiscard]] std::uint32_t simulation_time() const override { return sim_; }
    [[nodiscard]] bool has_target() const override { return has_target_; }
    [[nodiscard]] ScriptVec3 target_position() const override { return target_; }
    [[nodiscard]] std::uint32_t random(std::uint16_t) override { return next_variable(0x2129); }
    [[nodiscard]] std::uint32_t xz_distance(const ScriptVec3&, const ScriptVec3&) const override {
        return const_cast<ReplayServices*>(this)->next_variable(0x2132);
    }
    [[nodiscard]] std::uint32_t line_of_sight(const ScriptVec3&, const ScriptVec3&) const override {
        return const_cast<ReplayServices*>(this)->next_variable(0x2133);
    }
    [[nodiscard]] std::uint16_t global_2136() const override {
        return static_cast<std::uint16_t>(const_cast<ReplayServices*>(this)->next_variable(0x2136));
    }
    [[nodiscard]] bool video_playing() const override {
        return const_cast<ReplayServices*>(this)->next_query("video_playing") != 0;
    }
    [[nodiscard]] bool career_got_goal_type(std::uint32_t) const override {
        return const_cast<ReplayServices*>(this)->next_query("got_goal_type") != 0;
    }
    [[nodiscard]] bool effect_slots_available() const override { return effect_slots_; }

    void play_sound(std::int16_t id) override { events.push_back({"sfx_play", {id, 0x2000, 0}}); }
    void play_positional_sound(std::int16_t id, const ScriptVec3& position) override {
        events.push_back({"sfx_positional", {id, position[0], position[1], position[2]}});
    }
    [[nodiscard]] std::int32_t start_object_sound(std::int16_t id, bool positional) override {
        // The handle retail returns is opaque; the value is replaced by the
        // exit image comparison below.
        events.push_back({positional ? "sfx_positional" : "sfx_play", {id}});
        return 0;
    }
    void stop_sound(std::int32_t handle) override { events.push_back({"stop_sound", {handle}}); }
    void on_repeated_effect(const ScriptRepeatedEffect& effect) override {
        for (std::uint16_t index = 0; index < effect.count; ++index) {
            events.push_back({"effect_spawn", {}});
        }
    }
    void on_flash(const ScriptFlashEvent& flash) override {
        events.push_back({"flash", {flash.c0, flash.c1, flash.c2, flash.duration, 0, flash.sort}});
    }
    void animation_cycle(std::uint16_t index, std::uint16_t direction) override {
        events.push_back({"anim_cycle", {index, direction}});
    }
    void animation_start(std::uint16_t index) override { events.push_back({"anim_start", {index, 0, -1, -1}}); }
    [[nodiscard]] bool create_powerup(const ScriptPowerupRequest& request) override {
        events.push_back({"powerup_create", {request.kind, 0, request.word0, request.word1, -1}});
        return false;
    }
    void on_goal_event(std::uint16_t argument) override { events.push_back({"goal_event", {argument}}); }
    void signal_node_links(std::uint16_t node) override { events.push_back(link_event("signal_links", node)); }
    void execute_node_links(std::uint16_t node) override { events.push_back(link_event("exec_list", node)); }
    void signal_objects_at_node(std::uint16_t node) override {
        // Baddy list and game-object list, in that order (0x004c5b30 twice).
        events.push_back({"signal_node", {0, node}});
        events.push_back({"signal_node", {0, node}});
    }
    void send_pulse(std::uint16_t node) override { events.push_back({"send_pulse", {node}}); }

private:
    const TrgFile& file_;
    std::uint32_t mode_{};
    bool has_target_{};
    ScriptVec3 target_{};
    std::uint32_t sim_{};
    bool effect_slots_{};
    std::map<std::uint16_t, std::deque<std::uint32_t>> variable_results_;
    std::map<std::string, std::deque<std::int64_t>> queries_;

    std::uint32_t next_variable(std::uint16_t id) {
        auto& queue = variable_results_[id];
        if (queue.empty()) {
            return 0;
        }
        const std::uint32_t value = queue.front();
        queue.pop_front();
        return value;
    }
    std::int64_t next_query(const std::string& name) {
        auto& queue = queries_[name];
        if (queue.empty()) {
            return 0;
        }
        const std::int64_t value = queue.front();
        queue.pop_front();
        return value;
    }
    [[nodiscard]] RetailCall link_event(const char* name, std::uint16_t node) const {
        RetailCall call{name, {}};
        const std::vector<std::uint16_t> links = file_.links(node);
        call.values.push_back(static_cast<std::int64_t>(links.size()));
        for (const std::uint16_t link : links) {
            call.values.push_back(link);
        }
        return call;
    }
};

std::string describe(const RetailCall& call) {
    std::string text = call.name;
    for (const std::int64_t value : call.values) {
        text += " " + std::to_string(value);
    }
    return text;
}

bool same_call(const RetailCall& expected, const RetailCall& actual) {
    if (expected.name != actual.name) {
        return false;
    }
    // Retail effect/powerup argument lists carry pointers and constants the
    // native model does not reproduce; compare those calls by name (and the
    // arguments that carry script data).
    if (expected.name == "effect_spawn") {
        return true;
    }
    if (expected.name == "signal_node") {
        return expected.values.size() == 2 && expected.values[1] == actual.values[1];
    }
    if (expected.name == "stop_sound") {
        return true;
    }
    if (expected.name == "sfx_play" || expected.name == "sfx_positional") {
        // Object sounds (0x4507/0x4508) start through the same retail helpers
        // but pass the sound id as the only comparable value.
        return !expected.values.empty() && !actual.values.empty() && expected.values[0] == actual.values[0]
            && (actual.values.size() == 1 || expected.values == actual.values);
    }
    if (expected.name == "powerup_create") {
        return expected.values.size() == 5 && expected.values[0] == actual.values[0]
            && expected.values[2] == actual.values[2] && expected.values[3] == actual.values[3];
    }
    return expected.values == actual.values;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: opentony_object_script_retail_trace_test FIXTURE.txt LEVEL.TRG\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(argv[2])) {
        std::cout << "SKIP: level file " << argv[2] << " is not extracted\n";
        return 77;
    }
    std::ifstream fixture(argv[1]);
    if (!fixture) {
        std::cerr << "cannot open fixture " << argv[1] << '\n';
        return 2;
    }
    const TrgFile file = TrgFile::load(argv[2]);

    std::uint32_t base = 0;
    std::vector<RetailUpdate> updates;
    std::map<std::uint32_t, Image> previous_entry;
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream tokens(line.substr(2));
        const char kind = line[0];
        if (kind == 'B') {
            tokens >> base;
        } else if (kind == 'U') {
            RetailUpdate update;
            int target_flag = 0;
            tokens >> update.seq >> update.object >> update.dt >> update.sim >> update.mode >> target_flag
                >> update.target[0] >> update.target[1] >> update.target[2];
            update.has_target = target_flag != 0;
            int moved = 0;
            tokens >> moved;
            update.moved = moved != 0;
            updates.push_back(update);
        } else if (updates.empty()) {
            continue;
        } else if (kind == 'I') {
            RetailUpdate& update = updates.back();
            update.entry = previous_entry.count(update.object) != 0 ? previous_entry[update.object] : Image{};
            apply_delta(update.entry, tokens);
            previous_entry[update.object] = update.entry;
        } else if (kind == 'X') {
            RetailUpdate& update = updates.back();
            update.exit = update.entry;
            apply_delta(update.exit, tokens);
        } else if (kind == 'C') {
            RetailCommand command;
            tokens >> command.opcode >> command.cursor_after;
            updates.back().commands.push_back(command);
        } else if (kind == 'V') {
            RetailVariable variable;
            tokens >> variable.id >> variable.result;
            updates.back().variables.push_back(variable);
        } else if (kind == 'K') {
            RetailCall call;
            tokens >> call.name;
            std::int64_t value = 0;
            while (tokens >> value) {
                call.values.push_back(value);
            }
            updates.back().calls.push_back(call);
        } else if (kind == 'R') {
            std::string name;
            std::int64_t value = 0;
            tokens >> name >> value;
            updates.back().queries.emplace_back(name, value);
        }
    }

    ReplayServices services(file);
    ObjectScriptVm vm(file, services, ScriptClass::ScriptObjectCb);
    std::vector<RetailCommand> native_commands;
    std::vector<RetailVariable> native_variables;
    vm.set_command_trace([&](std::uint32_t address, std::uint16_t opcode) {
        native_commands.push_back({opcode, address + 2U + base});
    });
    vm.set_variable_trace([&](std::uint16_t id, std::uint32_t result) {
        native_variables.push_back({id, result & 0xffffU});
    });

    std::size_t matched = 0;
    std::size_t mismatched = 0;
    std::size_t commands = 0;
    std::size_t calls = 0;
    std::size_t variables = 0;
    std::size_t distances = 0;
    std::map<std::uint16_t, std::size_t> opcode_counts;
    auto fail = [&](const RetailUpdate& update, const std::string& what) {
        ++mismatched;
        if (mismatched <= 12) {
            std::cerr << "update " << update.seq << " (object 0x" << std::hex << update.object << std::dec
                      << "): " << what << '\n';
        }
    };

    for (const RetailUpdate& update : updates) {
        ScriptObject object = object_from_image(update.entry, base);
        // Ob_AI stores the nearest-player distance at +0xb8 just before the
        // update; the native distance formula must reproduce it.
        if (update.has_target && !update.moved) {
            const std::uint32_t expected_distance = crap_distance(update.target, object.position);
            if (expected_distance != rd32(update.entry, 0xb8)) {
                fail(update, "distance " + std::to_string(expected_distance) + " != retail "
                    + std::to_string(rd32(update.entry, 0xb8)));
            } else {
                ++distances;
            }
        }
        services.load(update);
        native_commands.clear();
        native_variables.clear();
        try {
            (void)vm.update(object, update.dt);
        } catch (const FormatError& error) {
            fail(update, std::string("native fault: ") + error.what());
            continue;
        }
        bool ok = true;

        if (native_commands.size() != update.commands.size()) {
            fail(update, "command count " + std::to_string(native_commands.size()) + " != retail "
                + std::to_string(update.commands.size()));
            ok = false;
        } else {
            for (std::size_t index = 0; index < native_commands.size(); ++index) {
                if (native_commands[index].opcode != update.commands[index].opcode
                    || native_commands[index].cursor_after != update.commands[index].cursor_after) {
                    fail(update, "command " + std::to_string(index) + " differs: native op 0x"
                        + std::to_string(native_commands[index].opcode) + " retail op "
                        + std::to_string(update.commands[index].opcode));
                    ok = false;
                    break;
                }
            }
        }
        if (native_variables.size() != update.variables.size()) {
            fail(update, "variable read count " + std::to_string(native_variables.size()) + " != retail "
                + std::to_string(update.variables.size()));
            ok = false;
        } else {
            for (std::size_t index = 0; index < native_variables.size(); ++index) {
                if (native_variables[index].id != update.variables[index].id
                    || native_variables[index].result != (update.variables[index].result & 0xffffU)) {
                    fail(update, "variable " + std::to_string(index) + " id " + std::to_string(update.variables[index].id)
                        + ": native " + std::to_string(native_variables[index].result) + " retail "
                        + std::to_string(update.variables[index].result & 0xffffU));
                    ok = false;
                    break;
                }
            }
        }
        if (services.events.size() != update.calls.size()) {
            fail(update, "side-effect call count " + std::to_string(services.events.size()) + " != retail "
                + std::to_string(update.calls.size()));
            ok = false;
        } else {
            for (std::size_t index = 0; index < services.events.size(); ++index) {
                if (!same_call(update.calls[index], services.events[index])) {
                    fail(update, "call " + std::to_string(index) + ": native [" + describe(services.events[index])
                        + "] retail [" + describe(update.calls[index]) + "]");
                    ok = false;
                    break;
                }
            }
        }

        // Modelled fields after the update.
        const ScriptObject expected = object_from_image(update.exit, base);
        auto check = [&](const char* field, std::uint64_t native, std::uint64_t retail) {
            if (native != retail) {
                fail(update, std::string("field ") + field + ": native " + std::to_string(native) + " retail "
                    + std::to_string(retail));
                ok = false;
            }
        };
        check("state", object.state, expected.state);
        check("position.x", static_cast<std::uint32_t>(object.position[0]), static_cast<std::uint32_t>(expected.position[0]));
        check("position.y", static_cast<std::uint32_t>(object.position[1]), static_cast<std::uint32_t>(expected.position[1]));
        check("position.z", static_cast<std::uint32_t>(object.position[2]), static_cast<std::uint32_t>(expected.position[2]));
        check("event_word", object.event_word, expected.event_word);
        check("list_flags", object.list_flags, expected.list_flags);
        check("node", object.node, expected.node);
        check("active", object.active, expected.active);
        check("byte_16f", object.byte_16f, expected.byte_16f);
        check("byte_170", object.byte_170, expected.byte_170);
        check("death_pulse_sent", object.death_pulse_sent, expected.death_pulse_sent);
        check("signal_latch", object.signal_latch, expected.signal_latch);
        check("flags", object.flags, expected.flags);
        check("cursor", object.cursor, expected.cursor);
        for (std::size_t label = 0; label < kScriptLabelCount; ++label) {
            check("label", object.labels[label], expected.labels[label]);
        }
        for (std::size_t index = 0; index < kScriptRegisterCount; ++index) {
            check("register", object.registers[index], expected.registers[index]);
        }
        check("wait", object.wait, expected.wait);
        check("field_b4", object.field_b4, expected.field_b4);
        check("field_ae", object.field_ae, expected.field_ae);
        check("sound_handle", static_cast<std::uint32_t>(object.sound_handle == 0 ? expected.sound_handle : object.sound_handle),
              static_cast<std::uint32_t>(expected.sound_handle));

        commands += update.commands.size();
        calls += update.calls.size();
        variables += update.variables.size();
        for (const RetailCommand& command : update.commands) {
            ++opcode_counts[command.opcode];
        }
        if (ok) {
            ++matched;
        }
    }
    std::cout << "updates=" << updates.size() << " matched=" << matched << " mismatched=" << mismatched
              << " commands=" << commands << " variable_reads=" << variables << " calls=" << calls
              << " distinct_opcodes=" << opcode_counts.size() << '\n';
    return mismatched == 0 && !updates.empty() ? 0 : 1;
}
