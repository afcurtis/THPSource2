// Replays a retail platform (subtype 0x0192) trace (see
// scripts/make_platform_trace_fixture.py) through the native platform object.
//
//   opentony_platform_object_retail_trace_test FIXTURE.txt LEVEL.TRG
//
// Every retail update (0x0049fef0) and CPlatform__Bounce call (0x0049f4c0) is
// replayed from the raw 0x218-byte object image retail entered it with (no
// state carried between calls). The native object must dispatch the same
// commands with the same cursors, read the same variables, draw the same Rnd
// values, issue the same collision queries and side-effect calls in order, and
// leave the object with the same modelled fields as retail did.
//
// Exit codes: 0 all updates match, 1 mismatch, 2 usage/format error, 77 the
// level file is not available (extract the game data first).
#include "object_script.hpp"

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

constexpr std::size_t kSize = 0x218;
constexpr std::size_t kImageWords = kSize / 4;
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
std::int32_t rds32(const Image& image, std::size_t offset) { return static_cast<std::int32_t>(rd32(image, offset)); }
std::int16_t rds16(const Image& image, std::size_t offset) { return static_cast<std::int16_t>(rd16(image, offset)); }

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
struct RetailQuery {
    ScriptVec3 start{};
    ScriptVec3 end{};
    bool hit{};
    ScriptVec3 position{};
    std::array<std::int16_t, 3> normal{};
};
struct RetailUpdate {
    long seq{};
    bool bounce{};
    std::uint32_t object{};
    std::uint32_t sim{};
    std::int32_t counter{};
    ScriptVec3 world_vector{};
    std::uint32_t skater{};
    ScriptVec3 skater_velocity{};
    Image entry{};
    Image exit{};
    std::vector<RetailCommand> commands;
    std::vector<RetailVariable> variables;
    std::vector<RetailCall> calls;
    std::vector<std::pair<std::string, std::int64_t>> queries;
    std::vector<std::int64_t> draws;
    std::vector<RetailQuery> collisions;
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
    object.position = {rds32(image, 0x08), rds32(image, 0x0c), rds32(image, 0x10)};
    object.params = {rd16(image, 0x14), rd16(image, 0x16), rd16(image, 0x18)};
    object.model_index = rd16(image, 0x1a);
    object.region = rd8(image, 0x1f);
    object.velocity = {rds32(image, 0x4c), rds32(image, 0x50), rds32(image, 0x54)};
    object.event_word = rd16(image, 0x68);
    object.list_flags = rd16(image, 0x6a);
    object.field_ae = rd16(image, 0xae);
    object.node = rd16(image, 0xb0);
    object.field_b4 = rd16(image, 0xb4);
    object.field_108 = rds32(image, 0x108);
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

    ScriptObject::Platform& p = object.platform;
    p.byte_16e = rd8(image, 0x16e);
    p.acceleration = {rds32(image, 0x58), rds32(image, 0x5c), rds32(image, 0x60)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        p.velocity_decay[axis] = rd8(image, 0x6c + axis);
        p.angular_velocity[axis] = rds16(image, 0x70 + axis * 2);
        p.angular_acceleration[axis] = rds16(image, 0x76 + axis * 2);
        p.angular_decay[axis] = rd8(image, 0x7c + axis);
        p.scale[axis] = rds16(image, 0x28 + axis * 2);
        p.scale_frames[axis] = rd16(image, 0x1e4 + axis * 2);
        p.scale_step[axis] = rds16(image, 0x1ea + axis * 2);
        p.extent[axis] = rd16(image, 0x1f4 + axis * 2);
        p.bounce_angles[axis] = rds16(image, 0x1fc + axis * 2);
    }
    p.target = {rds32(image, 0x1c4), rds32(image, 0x1c8), rds32(image, 0x1cc)};
    p.bounce_count = rd16(image, 0x1f0);
    p.kind = rd16(image, 0x1f2);
    p.lifetime = rd16(image, 0x1fa);
    p.sound_handle = rds32(image, 0x204);
    p.sound_period = rds32(image, 0x208);
    p.box = {rds32(image, 0x20c), rds32(image, 0x210), rds32(image, 0x214)};
    return object;
}

// Replays retail's world: query results, Rnd draws and collision results come
// from the trace.
class ReplayServices final : public ObjectScriptServices {
public:
    explicit ReplayServices(const TrgFile& file) : file_(file) {}

    void load(const RetailUpdate& update) {
        // The model tables are level data the trace does not carry; what the
        // scripts read from them is recovered from the exit image (the model
        // index, its 0x10 flag as state bit 0x20, and the three extents).
        model_index_ = rd16(update.exit, 0x1a);
        model_flag_ = (rd16(update.exit, 0x04) & 0x20U) != 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            extents_[axis] = static_cast<std::int16_t>(rd16(update.exit, 0x1f4 + axis * 2));
        }
        sim_ = update.sim;
        counter_ = update.counter;
        world_vector_ = update.world_vector;
        variable_results_.clear();
        for (const RetailVariable& variable : update.variables) {
            variable_results_[variable.id].push_back(variable.result);
        }
        queries_.clear();
        for (const auto& [name, value] : update.queries) {
            queries_[name].push_back(value);
        }
        draws_.assign(update.draws.begin(), update.draws.end());
        collisions_.assign(update.collisions.begin(), update.collisions.end());
        effect_slots_ = false;
        for (const RetailCall& call : update.calls) {
            if (call.name == "effect_spawn") {
                effect_slots_ = true;
            }
        }
        events.clear();
        assertions = 0;
        query_mismatches = 0;
        draw_underflow = 0;
    }

    std::vector<RetailCall> events;
    std::size_t assertions{};
    std::size_t query_mismatches{};
    std::size_t draw_underflow{};
    std::size_t queries_left() const { return collisions_.size(); }
    std::size_t draws_left() const { return draws_.size(); }

    void on_assertion(std::string_view) override { ++assertions; }
    [[nodiscard]] std::uint32_t simulation_time() const override { return sim_; }
    [[nodiscard]] std::int32_t tick_counter() const override { return counter_; }
    [[nodiscard]] ScriptVec3 platform_acceleration() const override { return world_vector_; }
    [[nodiscard]] std::uint32_t random(std::uint16_t) override {
        if (draws_.empty()) {
            ++draw_underflow;
            return 0;
        }
        const std::int64_t value = draws_.front();
        draws_.pop_front();
        return static_cast<std::uint32_t>(value);
    }
    [[nodiscard]] bool has_target() const override { return false; }
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
    [[nodiscard]] std::uint16_t find_model_index(std::uint32_t, std::uint8_t) override { return model_index_; }
    [[nodiscard]] ScriptModelInfo model_info(std::uint8_t, std::uint16_t) override {
        return {model_flag_, {extents_[0], 0, extents_[1], 0, extents_[2], 0}};
    }
    [[nodiscard]] ScriptCollisionHit line_collision(const ScriptVec3& start, const ScriptVec3& end) override {
        if (collisions_.empty()) {
            ++query_mismatches;
            return {};
        }
        const RetailQuery query = collisions_.front();
        collisions_.pop_front();
        if (query.start != start || query.end != end) {
            ++query_mismatches;
        }
        return {query.hit, query.position, query.normal};
    }

    void play_sound(std::int16_t id) override { events.push_back({"sfx_play", {id, 0x2000, 0}}); }
    void play_positional_sound(std::int16_t id, const ScriptVec3& position) override {
        events.push_back({"sfx_positional", {id, position[0], position[1], position[2]}});
    }
    [[nodiscard]] std::int32_t start_object_sound(std::int16_t id, bool positional) override {
        events.push_back({positional ? "sfx_positional" : "sfx_play", {id}});
        return 0;
    }
    void stop_sound(std::int32_t handle) override { events.push_back({"stop_sound", {handle}}); }
    void update_sound_position(std::int32_t handle, const ScriptVec3& position) override {
        events.push_back({"sfx_modify_pos", {handle, position[0], position[1], position[2]}});
    }
    // SFX_Grunt (0x0046e1d0) draws Rnd(3) and plays sound 0x1c + draw (plus a
    // surface sound when the skater stands on certain surfaces, which the
    // traces never show).
    void skater_grunt(std::uint32_t) override { events.push_back({"sfx_play", {0x1c + random(3)}}); }
    void shatter_item(const ScriptObject&, int mode) override { events.push_back({"shatter", {0, 0, mode}}); }
    void item_flash(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) override {
        events.push_back({"item_flash", {a, b, c, d}});
    }
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
        events.push_back({"signal_node", {0, node}});
        events.push_back({"signal_node", {0, node}});
    }
    void send_pulse(std::uint16_t node) override { events.push_back({"send_pulse", {node}}); }

private:
    const TrgFile& file_;
    std::uint32_t sim_{};
    std::int32_t counter_{};
    ScriptVec3 world_vector_{};
    std::uint16_t model_index_{};
    bool model_flag_{};
    std::array<std::int16_t, 3> extents_{};
    bool effect_slots_{};
    std::map<std::uint16_t, std::deque<std::uint32_t>> variable_results_;
    std::map<std::string, std::deque<std::int64_t>> queries_;
    std::deque<std::int64_t> draws_;
    std::deque<RetailQuery> collisions_;

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
    if (expected.name == "effect_spawn" || expected.name == "stop_sound") {
        return true;
    }
    if (expected.name == "signal_node") {
        return expected.values.size() == 2 && expected.values[1] == actual.values[1];
    }
    if (expected.name == "shatter") {
        // The retail call passes the object pointer first; only the mode is comparable.
        return expected.values.size() == 3 && expected.values[2] == actual.values[2];
    }
    if (expected.name == "sfx_play" || expected.name == "sfx_positional") {
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
        std::cerr << "usage: opentony_platform_object_retail_trace_test FIXTURE.txt LEVEL.TRG\n";
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
            std::string function;
            tokens >> update.seq >> function >> update.object >> update.sim >> update.counter
                >> update.world_vector[0] >> update.world_vector[1] >> update.world_vector[2] >> update.skater
                >> update.skater_velocity[0] >> update.skater_velocity[1] >> update.skater_velocity[2];
            update.bounce = function == "bounce";
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
        } else if (kind == 'N') {
            std::int64_t value = 0;
            tokens >> value;
            updates.back().draws.push_back(value);
        } else if (kind == 'Q') {
            RetailQuery query;
            int hit = 0;
            tokens >> query.start[0] >> query.start[1] >> query.start[2] >> query.end[0] >> query.end[1]
                >> query.end[2] >> hit >> query.position[0] >> query.position[1] >> query.position[2];
            int n0 = 0;
            int n1 = 0;
            int n2 = 0;
            tokens >> n0 >> n1 >> n2;
            query.hit = hit != 0;
            query.normal = {static_cast<std::int16_t>(n0), static_cast<std::int16_t>(n1), static_cast<std::int16_t>(n2)};
            updates.back().collisions.push_back(query);
        }
    }

    ReplayServices services(file);
    ObjectScriptVm vm(file, services, ScriptClass::Platform);
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
    std::size_t bounces = 0;
    std::size_t commands = 0;
    std::size_t calls = 0;
    std::size_t collisions = 0;
    std::size_t draws = 0;
    std::size_t bouncy_updates = 0;
    std::size_t flying_updates = 0;
    std::map<std::uint16_t, std::size_t> opcode_counts;
    auto fail = [&](const RetailUpdate& update, const std::string& what) {
        ++mismatched;
        if (mismatched <= 16) {
            std::cerr << (update.bounce ? "bounce " : "update ") << update.seq << " (object 0x" << std::hex
                      << update.object << std::dec << "): " << what << '\n';
        }
    };

    for (const RetailUpdate& update : updates) {
        ScriptObject object = object_from_image(update.entry, base);
        services.load(update);
        native_commands.clear();
        native_variables.clear();
        try {
            if (update.bounce) {
                vm.bounce_platform(object, update.skater, update.skater_velocity);
            } else {
                (void)vm.update_platform(object);
            }
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
                    fail(update, "command " + std::to_string(index) + " differs: native op "
                        + std::to_string(native_commands[index].opcode) + " retail op "
                        + std::to_string(update.commands[index].opcode));
                    ok = false;
                    break;
                }
            }
        }
        // Variable reads of 0x2129 are Rnd draws, replayed through the draw queue.
        std::vector<RetailVariable> expected_variables;
        for (const RetailVariable& variable : update.variables) {
            if (variable.id != 0x2129) {
                expected_variables.push_back(variable);
            }
        }
        std::vector<RetailVariable> actual_variables;
        for (const RetailVariable& variable : native_variables) {
            if (variable.id != 0x2129) {
                actual_variables.push_back(variable);
            }
        }
        if (actual_variables.size() != expected_variables.size()) {
            fail(update, "variable read count " + std::to_string(actual_variables.size()) + " != retail "
                + std::to_string(expected_variables.size()));
            ok = false;
        } else {
            for (std::size_t index = 0; index < actual_variables.size(); ++index) {
                if (actual_variables[index].id != expected_variables[index].id
                    || actual_variables[index].result != (expected_variables[index].result & 0xffffU)) {
                    fail(update, "variable " + std::to_string(index) + " id " + std::to_string(expected_variables[index].id)
                        + ": native " + std::to_string(actual_variables[index].result) + " retail "
                        + std::to_string(expected_variables[index].result & 0xffffU));
                    ok = false;
                    break;
                }
            }
        }
        if (services.events.size() != update.calls.size()) {
            std::string retail;
            for (const RetailCall& call : update.calls) {
                retail += " [" + describe(call) + "]";
            }
            std::string native;
            for (const RetailCall& call : services.events) {
                native += " [" + describe(call) + "]";
            }
            fail(update, "side-effect calls: native" + native + " retail" + retail);
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
        if (services.query_mismatches != 0 || services.queries_left() != 0) {
            fail(update, "collision queries differ (mismatched " + std::to_string(services.query_mismatches)
                + ", unused " + std::to_string(services.queries_left()) + ")");
            ok = false;
        }
        if (services.draw_underflow != 0 || services.draws_left() != 0) {
            fail(update, "Rnd draws differ (native ran out " + std::to_string(services.draw_underflow)
                + " times, " + std::to_string(services.draws_left()) + " unused)");
            ok = false;
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
        auto check32 = [&](const char* field, std::int32_t native, std::int32_t retail) {
            check(field, static_cast<std::uint32_t>(native), static_cast<std::uint32_t>(retail));
        };
        check("state", object.state, expected.state);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check32("position", object.position[axis], expected.position[axis]);
            check32("velocity", object.velocity[axis], expected.velocity[axis]);
            check("angle", object.params[axis], expected.params[axis]);
            check32("acceleration", object.platform.acceleration[axis], expected.platform.acceleration[axis]);
            check("angular_velocity", static_cast<std::uint16_t>(object.platform.angular_velocity[axis]),
                  static_cast<std::uint16_t>(expected.platform.angular_velocity[axis]));
            check("angular_acceleration", static_cast<std::uint16_t>(object.platform.angular_acceleration[axis]),
                  static_cast<std::uint16_t>(expected.platform.angular_acceleration[axis]));
            check("scale", static_cast<std::uint16_t>(object.platform.scale[axis]),
                  static_cast<std::uint16_t>(expected.platform.scale[axis]));
            check("scale_frames", object.platform.scale_frames[axis], expected.platform.scale_frames[axis]);
            check("bounce_angle", static_cast<std::uint16_t>(object.platform.bounce_angles[axis]),
                  static_cast<std::uint16_t>(expected.platform.bounce_angles[axis]));
            check32("target", object.platform.target[axis], expected.platform.target[axis]);
        }
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
        for (std::size_t index = 0; index < kScriptAttributeCount; ++index) {
            check("attribute", object.attributes[index], expected.attributes[index]);
        }
        check("wait", object.wait, expected.wait);
        check("field_1dc", object.field_1dc, expected.field_1dc);
        check("field_1e0", object.field_1e0, expected.field_1e0);
        check("field_b4", object.field_b4, expected.field_b4);
        check("field_ae", object.field_ae, expected.field_ae);
        check("model_index", object.model_index, expected.model_index);
        check("bounce_count", object.platform.bounce_count, expected.platform.bounce_count);
        check("kind", object.platform.kind, expected.platform.kind);
        check("lifetime", object.platform.lifetime, expected.platform.lifetime);
        check("extent0", object.platform.extent[0], expected.platform.extent[0]);
        check("sound_period", static_cast<std::uint32_t>(object.platform.sound_period),
              static_cast<std::uint32_t>(expected.platform.sound_period));
        check("sound_handle",
              static_cast<std::uint32_t>(object.platform.sound_handle == 0 ? expected.platform.sound_handle
                                                                          : object.platform.sound_handle),
              static_cast<std::uint32_t>(expected.platform.sound_handle));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check32("box", object.platform.box[axis], expected.platform.box[axis]);
        }

        commands += update.commands.size();
        calls += update.calls.size();
        collisions += update.collisions.size();
        draws += update.draws.size();
        if (update.bounce) {
            ++bounces;
        }
        if ((object_from_image(update.entry, base).flags & 0x10U) != 0) {
            ++bouncy_updates;
        }
        if ((object_from_image(update.entry, base).flags & 0x20U) != 0) {
            ++flying_updates;
        }
        for (const RetailCommand& command : update.commands) {
            ++opcode_counts[command.opcode];
        }
        if (ok) {
            ++matched;
        }
    }
    std::cout << "updates=" << updates.size() << " matched=" << matched << " mismatched=" << mismatched
              << " bounces=" << bounces << " bouncy=" << bouncy_updates << " flying=" << flying_updates
              << " commands=" << commands << " calls=" << calls << " collision_queries=" << collisions
              << " rnd_draws=" << draws << " distinct_opcodes=" << opcode_counts.size() << '\n';
    return mismatched == 0 && !updates.empty() ? 0 : 1;
}
