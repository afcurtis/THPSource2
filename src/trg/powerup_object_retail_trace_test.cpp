// Replays a retail power-up trace (see scripts/make_powerup_trace_fixture.py)
// through the native power-up object.
//
//   opentony_powerup_object_retail_trace_test FIXTURE.txt LEVEL.TRG
//
// Every Powerup_Update and CPowerUp__TakeEffect call replays from the raw 0x100-byte
// object image retail entered it with. The native object must make the same
// side-effect calls in order and leave the object with the same modelled fields.
// The constructor is checked against the first image of every power-up.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error, 77 the level file is
// not available.
#include "object_script.hpp"

#include <array>
#include <cstdint>
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

constexpr std::size_t kImageWords = 0x100 / 4;
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

struct Call {
    std::string name;
    std::vector<std::int64_t> values;
};
struct Record {
    long seq{};
    bool take{};
    std::uint32_t object{};
    std::int32_t counter{};
    std::int32_t scale{};
    std::uint32_t players{};
    std::uint32_t limit{};
    std::uint32_t mode{};
    std::uint32_t skater{};
    bool primary{};
    std::uint32_t skater_f64{};
    std::uint32_t skater_b8{};
    Image entry{};
    Image exit{};
    std::vector<Call> calls;
    std::vector<std::pair<std::string, std::int64_t>> queries;
};

void apply_delta(Image& image, std::istringstream& tokens) {
    std::string token;
    while (tokens >> token) {
        const std::size_t colon = token.find(':');
        image.at(std::stoul(token.substr(0, colon))) =
            static_cast<std::uint32_t>(std::stoul(token.substr(colon + 1), nullptr, 16));
    }
}

ScriptObject object_from_image(const Image& image) {
    ScriptObject object;
    ScriptObject::Powerup& p = object.powerup;
    object.state = rd16(image, 0x04);
    object.position = {rds32(image, 0x08), rds32(image, 0x0c), rds32(image, 0x10)};
    object.params = {rd16(image, 0x14), rd16(image, 0x16), rd16(image, 0x18)};
    object.model_index = rd16(image, 0x1a);
    object.region = rd8(image, 0x1f);
    object.subtype = rd16(image, 0x3c);
    object.velocity = {rds32(image, 0x4c), rds32(image, 0x50), rds32(image, 0x54)};
    object.platform.acceleration = {rds32(image, 0x58), rds32(image, 0x5c), rds32(image, 0x60)};
    object.platform.velocity_decay = {rd8(image, 0x6c), rd8(image, 0x6d), rd8(image, 0x6e)};
    object.list_flags = rd16(image, 0x6a);
    object.platform.angular_velocity = {rds16(image, 0x70), rds16(image, 0x72), rds16(image, 0x74)};
    object.field_ae = rd16(image, 0xae);
    object.node = rd16(image, 0xb0);
    object.distance = rd32(image, 0xb8);
    p.glow = rd32(image, 0xc8);
    p.glow_base = rd16(image, 0xcc);
    p.glow_amplitude = rd16(image, 0xce);
    p.link_flag = rd8(image, 0xd0);
    p.visual = rd8(image, 0xd1);
    p.motion = rd8(image, 0xd2);
    p.settled = rd8(image, 0xd3);
    p.drop = rd8(image, 0xd4);
    p.link_node = rd16(image, 0xd6);
    p.ground = rds32(image, 0xd8);
    p.home = {rds32(image, 0xdc), rds32(image, 0xe0), rds32(image, 0xe4)};
    p.phase = rd16(image, 0xe8);
    p.amplitude_base = rd16(image, 0xea);
    p.amplitude = rd16(image, 0xec);
    p.rate = rd16(image, 0xee);
    p.timer = rd16(image, 0xf0);
    p.follow_scale = rd16(image, 0xf6);
    p.age = rds32(image, 0xfc);
    p.shadow = {rds32(image, 0x90), rds32(image, 0x94), rds32(image, 0x98)};
    p.shadow_scale = rd16(image, 0xa8);
    p.shadow_height = rd16(image, 0xaa);
    return object;
}

class Replay final : public ObjectScriptServices {
public:
    void load(const Record& record) {
        record_ = &record;
        queries_.clear();
        for (const auto& [name, value] : record.queries) {
            queries_[name].push_back(value);
        }
        events.clear();
        assertions = 0;
    }
    std::vector<Call> events;
    std::size_t assertions{};

    void on_assertion(std::string_view) override { ++assertions; }
    [[nodiscard]] std::int32_t tick_counter() const override { return record_->counter; }
    [[nodiscard]] std::int32_t animation_time_scale() const override { return record_->scale; }
    [[nodiscard]] std::size_t player_count() const override { return record_->players; }
    [[nodiscard]] std::uint32_t powerup_draw_limit() const override { return record_->limit; }
    [[nodiscard]] std::uint32_t game_mode_selector() const override { return record_->mode; }
    [[nodiscard]] std::int32_t ground_height(const ScriptVec3&) override {
        return static_cast<std::int32_t>(next("ground_height"));
    }
    [[nodiscard]] bool goal_stat_taken(std::int32_t) override { return next("goal_stat") != 0; }
    [[nodiscard]] bool skater_on_ground(std::uint32_t) const override { return record_->skater_f64 == 0; }
    [[nodiscard]] bool skater_not_in_trick(std::uint32_t) const override { return record_->skater_b8 == 0; }
    [[nodiscard]] std::uint32_t create_powerup_glow(std::uint16_t) override { return 0x1234; }
    void play_sound(std::int16_t id) override { events.push_back({"sfx_play", {id}}); }
    [[nodiscard]] std::int32_t play_sound_handle(std::int16_t id) override {
        events.push_back({"sfx_play", {id}});
        return 7;
    }
    void sound_pitch(std::int32_t, std::int32_t pitch) override { events.push_back({"sfx_pitch", {pitch}}); }
    void career_give_money(std::uint16_t, std::int32_t amount) override { events.push_back({"give_money", {amount}}); }
    // Retail's level-pickup path is a goal-type grant followed by its message: one service.
    void career_level_pickup() override {
        events.push_back({"give_goal_type", {}});
        events.push_back({"message", {}});
    }
    void career_give_goal_type(std::int32_t type) override { events.push_back({"give_goal_type", {type}}); }
    void show_message(std::uint16_t) override { events.push_back({"message", {}}); }
    void delete_messages() override { events.push_back({"delete_messages", {}}); }
    void skater_trick_takeoff() override { events.push_back({"trick_takeoff", {}}); }
    void skater_trick_land(std::uint32_t) override { events.push_back({"trick_land", {}}); }
    void execute_node_links(std::uint16_t) override { events.push_back({"exec_list", {}}); }

private:
    const Record* record_{};
    std::map<std::string, std::deque<std::int64_t>> queries_;
    std::int64_t next(const std::string& name) {
        auto& queue = queries_[name];
        if (queue.empty()) {
            return 0;
        }
        const std::int64_t value = queue.front();
        queue.pop_front();
        return value;
    }
};

std::string describe(const Call& call) {
    std::string text = call.name;
    for (const std::int64_t value : call.values) {
        text += " " + std::to_string(value);
    }
    return text;
}

// Compare a retail call with the native event: pointers and opaque handles in the
// retail arguments are not comparable.
bool same_call(const Call& retail, const Call& native) {
    if (retail.name != native.name) {
        return false;
    }
    if (retail.name == "sfx_play") {
        return !retail.values.empty() && retail.values[0] == native.values[0];
    }
    if (retail.name == "sfx_pitch") {
        return retail.values.size() == 2 && retail.values[1] == native.values[0];
    }
    if (retail.name == "give_money") {
        return !retail.values.empty() && retail.values.back() == native.values[0];
    }
    if (retail.name == "give_goal_type") {
        return native.values.empty() || (retail.values.size() == 1 && retail.values[0] == native.values[0]);
    }
    return true;  // message, delete_messages, trick_takeoff, trick_land, exec_list
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: opentony_powerup_object_retail_trace_test FIXTURE.txt LEVEL.TRG\n";
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

    std::vector<Record> records;
    std::map<std::uint32_t, Image> previous;
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#' || line[0] == 'S') {
            continue;
        }
        std::istringstream tokens(line.substr(2));
        const char kind = line[0];
        if (kind == 'U') {
            Record record;
            std::string function;
            int primary = 0;
            tokens >> record.seq >> function >> record.object >> record.counter >> record.scale >> record.players
                >> record.limit >> record.mode >> record.skater >> primary >> record.skater_f64 >> record.skater_b8;
            record.take = function == "take";
            record.primary = primary != 0;
            records.push_back(record);
        } else if (records.empty()) {
            continue;
        } else if (kind == 'I') {
            Record& record = records.back();
            record.entry = previous.count(record.object) != 0 ? previous[record.object] : Image{};
            apply_delta(record.entry, tokens);
            previous[record.object] = record.entry;
        } else if (kind == 'X') {
            Record& record = records.back();
            record.exit = record.entry;
            apply_delta(record.exit, tokens);
        } else if (kind == 'K') {
            Call call;
            tokens >> call.name;
            std::int64_t value = 0;
            while (tokens >> value) {
                call.values.push_back(value);
            }
            records.back().calls.push_back(call);
        } else if (kind == 'G') {
            std::string name;
            std::int64_t value = 0;
            tokens >> name >> value;
            records.back().queries.emplace_back(name, value);
        }
    }

    Replay replay;
    ObjectScriptVm vm(file, replay, ScriptClass::Powerup);
    std::size_t matched = 0;
    std::size_t mismatched = 0;
    std::size_t takes = 0;
    std::size_t effects = 0;
    std::size_t grounded = 0;
    std::map<std::uint16_t, std::size_t> subtypes;
    auto fail = [&](const Record& record, const std::string& what) {
        ++mismatched;
        if (mismatched <= 16) {
            std::cerr << (record.take ? "take " : "update ") << record.seq << " (object 0x" << std::hex << record.object
                      << std::dec << "): " << what << '\n';
        }
    };

    for (const Record& record : records) {
        ScriptObject object = object_from_image(record.entry);
        replay.load(record);
        if (record.take) {
            vm.take_powerup(object, record.skater, record.primary);
            ++takes;
        } else {
            vm.update_powerup(object);
        }
        bool ok = true;
        if (replay.events.size() != record.calls.size()) {
            std::string a;
            for (const Call& c : record.calls) a += " [" + describe(c) + "]";
            std::string b;
            for (const Call& c : replay.events) b += " [" + describe(c) + "]";
            fail(record, "calls: native" + b + " retail" + a);
            ok = false;
        } else {
            for (std::size_t index = 0; index < record.calls.size(); ++index) {
                if (!same_call(record.calls[index], replay.events[index])) {
                    fail(record, "call " + std::to_string(index) + ": native [" + describe(replay.events[index])
                        + "] retail [" + describe(record.calls[index]) + "]");
                    ok = false;
                    break;
                }
            }
        }
        const ScriptObject want = object_from_image(record.exit);
        auto check = [&](const char* field, std::int64_t native, std::int64_t retail) {
            if (native != retail) {
                fail(record, std::string("field ") + field + ": native " + std::to_string(native) + " retail "
                    + std::to_string(retail));
                ok = false;
            }
        };
        check("state", object.state, want.state);
        check("list_flags", object.list_flags, want.list_flags);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check("position", object.position[axis], want.position[axis]);
            check("angle", object.params[axis], want.params[axis]);
            check("velocity", object.velocity[axis], want.velocity[axis]);
            check("home", object.powerup.home[axis], want.powerup.home[axis]);
            check("shadow", object.powerup.shadow[axis], want.powerup.shadow[axis]);
        }
        check("ground", object.powerup.ground, want.powerup.ground);
        check("motion", object.powerup.motion, want.powerup.motion);
        check("settled", object.powerup.settled, want.powerup.settled);
        check("phase", object.powerup.phase, want.powerup.phase);
        check("amplitude", object.powerup.amplitude, want.powerup.amplitude);
        check("timer", object.powerup.timer, want.powerup.timer);
        check("age", object.powerup.age, want.powerup.age);
        check("shadow_scale", object.powerup.shadow_scale, want.powerup.shadow_scale);
        check("shadow_height", object.powerup.shadow_height, want.powerup.shadow_height);
        check("glow_present", object.powerup.glow != 0, want.powerup.glow != 0);
        check("glow_base", object.powerup.glow_base, want.powerup.glow_base);
        check("glow_amplitude", object.powerup.glow_amplitude, want.powerup.glow_amplitude);
        ++subtypes[object.subtype];
        effects += record.calls.size();
        if (object.powerup.motion != 0) {
            ++grounded;
        }
        if (ok) {
            ++matched;
        }
    }

    // The constructor against the first image of every power-up (age 0).
    std::size_t constructed = 0;
    std::size_t construct_mismatches = 0;
    std::map<std::uint32_t, bool> seen;
    for (const Record& record : records) {
        if (seen[record.object] || record.take) {
            continue;
        }
        seen[record.object] = true;
        const ScriptObject image = object_from_image(record.entry);
        if (image.powerup.age != 0) {
            continue;
        }
        struct Construct final : ObjectScriptServices {
            const ScriptObject* image{};
            std::uint32_t mode{};
            [[nodiscard]] std::uint32_t game_mode_selector() const override { return mode; }
            [[nodiscard]] std::uint32_t random(std::uint16_t) override { return image->powerup.rate - 0x5a; }
            [[nodiscard]] std::uint16_t find_model_index(std::uint32_t, std::uint8_t) override { return image->model_index; }
            void level_pickup_model(std::uint32_t& checksum, std::uint8_t& region) const override {
                checksum = 1;
                region = image->region;
            }
            [[nodiscard]] std::int32_t ground_height(const ScriptVec3&) override { return image->powerup.ground; }
        } services;
        services.image = &image;
        services.mode = record.mode;
        ObjectScriptVm builder(file, services, ScriptClass::Powerup);
        const std::uint8_t flags = image.powerup.motion != 0 ? 4 : 0;
        const ScriptObject made = builder.create_powerup(
            image.node, image.subtype, image.powerup.home, {}, flags, 0xffff);
        ++constructed;
        bool same = made.model_index == image.model_index && made.region == image.region
            && made.platform.velocity_decay == image.platform.velocity_decay
            && made.platform.angular_velocity[1] == image.platform.angular_velocity[1]
            && made.platform.acceleration[1] == image.platform.acceleration[1]
            && made.powerup.visual == image.powerup.visual && made.field_ae == image.field_ae
            && made.powerup.amplitude_base == image.powerup.amplitude_base
            && made.powerup.rate == image.powerup.rate && made.list_flags == image.list_flags
            && made.powerup.timer == image.powerup.timer;
        if (!same) {
            ++construct_mismatches;
            if (construct_mismatches <= 4) {
                std::cerr << "constructor of node " << image.node << " subtype 0x" << std::hex << image.subtype
                          << std::dec << " differs from its first image\n";
            }
        }
    }

    std::cout << "records=" << records.size() << " matched=" << matched << " mismatched=" << mismatched
              << " takes=" << takes << " calls=" << effects << " subtypes=" << subtypes.size()
              << " constructed=" << constructed << " construct_mismatches=" << construct_mismatches << '\n';
    return (mismatched == 0 && construct_mismatches == 0 && !records.empty()) ? 0 : 1;
}
