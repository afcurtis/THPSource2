// Replays a retail traffic-car trace (see scripts/make_traffic_trace_fixture.py)
// through the native car update.
//
//   opentony_traffic_object_retail_trace_test FIXTURE.txt LEVEL.TRG
//
// Every retail update (0x00412960) replays from the raw 0x1e8-byte object image
// retail entered it with. The native car must make the same side-effect calls
// and Rnd draws in order and leave the object with the same position, angles,
// velocity, road node and proximity latch.
//
// Exit codes: 0 all updates match, 1 mismatch, 2 usage/format error, 77 the
// level file is not available.
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

constexpr std::size_t kImageWords = 0x1e8 / 4;
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
std::int32_t rds32(const Image& image, std::size_t offset) { return static_cast<std::int32_t>(rd32(image, offset)); }

struct Call {
    std::string name;
    std::vector<std::int64_t> values;
};
struct Update {
    long seq{};
    std::uint32_t object{};
    std::int32_t clock{};
    std::int32_t scale{};
    std::uint32_t players{};
    ScriptVec3 player{};
    Image entry{};
    Image exit{};
    std::vector<Call> calls;
    std::vector<std::int64_t> draws;
    bool video{};
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
    object.state = rd16(image, 0x04);
    object.position = {rds32(image, 0x08), rds32(image, 0x0c), rds32(image, 0x10)};
    object.params = {rd16(image, 0x14), rd16(image, 0x16), rd16(image, 0x18)};
    object.subtype = rd16(image, 0x3c);
    object.velocity = {rds32(image, 0x4c), rds32(image, 0x50), rds32(image, 0x54)};
    object.list_flags = rd16(image, 0x6a);
    object.node = rd16(image, 0xb0);
    object.traffic.sound_handle = rds32(image, 0x1d0);
    object.traffic.sound_period = rds32(image, 0x1d4);
    object.traffic.road_node = rds32(image, 0x1dc);
    object.traffic.sequence = rd32(image, 0x1e0);
    object.traffic.latch = rds32(image, 0x1e4);
    return object;
}

class Replay final : public ObjectScriptServices {
public:
    void load(const Update& update) {
        update_ = &update;
        draws_.assign(update.draws.begin(), update.draws.end());
        events.clear();
        underflow = 0;
        assertions = 0;
    }
    std::vector<Call> events;
    std::size_t underflow{};
    std::size_t assertions{};
    std::size_t draws_left() const { return draws_.size(); }

    void on_assertion(std::string_view) override { ++assertions; }
    [[nodiscard]] std::int32_t animation_clock() const override { return update_->clock; }
    [[nodiscard]] std::int32_t animation_time_scale() const override { return update_->scale; }
    [[nodiscard]] std::size_t player_count() const override { return update_->players; }
    [[nodiscard]] ScriptVec3 player_position(std::size_t) const override { return update_->player; }
    [[nodiscard]] bool video_playing() const override { return update_->video; }
    [[nodiscard]] std::uint32_t random(std::uint16_t) override {
        if (draws_.empty()) {
            ++underflow;
            return 0;
        }
        const std::int64_t value = draws_.front();
        draws_.pop_front();
        return static_cast<std::uint32_t>(value);
    }
    void play_sound(std::int16_t id) override { events.push_back({"sfx_play", {id}}); }
    void update_sound_position(std::int32_t handle, const ScriptVec3& p) override {
        events.push_back({"sfx_modify_pos", {handle, p[0], p[1], p[2]}});
    }
    void traffic_interaction(const ScriptObject&) override { events.push_back({"interaction", {}}); }
    void save_car_position(std::uint32_t index, const ScriptVec3& p, const std::array<std::uint16_t, 3>& a) override {
        events.push_back({"save_position", {index, p[0], p[1], p[2],
                                            static_cast<std::int64_t>(a[0] | (static_cast<std::uint32_t>(a[1]) << 16U)),
                                            a[2]}});
    }

private:
    const Update* update_{};
    std::deque<std::int64_t> draws_;
};

std::string describe(const Call& call) {
    std::string text = call.name;
    for (const std::int64_t value : call.values) {
        text += " " + std::to_string(value);
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: opentony_traffic_object_retail_trace_test FIXTURE.txt LEVEL.TRG\n";
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

    std::vector<Update> updates;
    std::map<std::uint32_t, Image> previous;
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#' || line[0] == 'S') {
            continue;
        }
        std::istringstream tokens(line.substr(2));
        const char kind = line[0];
        if (kind == 'U') {
            Update update;
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            std::uint32_t c = 0;
            tokens >> update.seq >> update.object >> update.clock >> update.scale >> update.players
                >> update.player[0] >> update.player[1] >> update.player[2] >> a >> b >> c;
            updates.push_back(update);
        } else if (updates.empty()) {
            continue;
        } else if (kind == 'I') {
            Update& update = updates.back();
            update.entry = previous.count(update.object) != 0 ? previous[update.object] : Image{};
            apply_delta(update.entry, tokens);
            previous[update.object] = update.entry;
        } else if (kind == 'X') {
            Update& update = updates.back();
            update.exit = update.entry;
            apply_delta(update.exit, tokens);
        } else if (kind == 'K') {
            Call call;
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
            // Video_Playing returns a char: only AL is meaningful.
            if (name == "video_playing" && (value & 0xff) != 0) {
                updates.back().video = true;
            }
        } else if (kind == 'N') {
            std::int64_t value = 0;
            tokens >> value;
            updates.back().draws.push_back(value);
        }
    }

    Replay replay;
    ObjectScriptVm vm(file, replay, ScriptClass::Traffic);
    std::size_t matched = 0;
    std::size_t mismatched = 0;
    std::size_t proximity_calls = 0;
    std::size_t sounds = 0;
    std::size_t draws = 0;
    std::size_t segment_changes = 0;
    auto fail = [&](const Update& update, const std::string& what) {
        ++mismatched;
        if (mismatched <= 16) {
            std::cerr << "update " << update.seq << " (car 0x" << std::hex << update.object << std::dec << "): " << what
                      << '\n';
        }
    };

    for (const Update& update : updates) {
        ScriptObject object = object_from_image(update.entry);
        replay.load(update);
        try {
            (void)vm.update_traffic(object);
        } catch (const FormatError& error) {
            fail(update, std::string("native fault: ") + error.what());
            continue;
        }
        bool ok = true;
        // The retail proximity helper is internal to the native update.
        std::vector<Call> expected;
        for (const Call& call : update.calls) {
            if (call.name == "proximity") {
                ++proximity_calls;
                continue;
            }
            Call trimmed = call;
            if (trimmed.name == "save_position") {
                trimmed.values.erase(trimmed.values.begin());  // the Player pointer
                trimmed.values.back() &= 0xffff;
            }
            expected.push_back(trimmed);
        }
        if (expected.size() != replay.events.size()) {
            std::string a;
            for (const Call& c : expected) a += " [" + describe(c) + "]";
            std::string b;
            for (const Call& c : replay.events) b += " [" + describe(c) + "]";
            fail(update, "calls: native" + b + " retail" + a);
            ok = false;
        } else {
            for (std::size_t index = 0; index < expected.size(); ++index) {
                if (expected[index].name != replay.events[index].name
                    || (expected[index].name != "interaction" && expected[index].values != replay.events[index].values)) {
                    fail(update, "call " + std::to_string(index) + ": native [" + describe(replay.events[index])
                        + "] retail [" + describe(expected[index]) + "]");
                    ok = false;
                    break;
                }
            }
        }
        if (replay.underflow != 0 || replay.draws_left() != 0) {
            fail(update, "Rnd draws differ");
            ok = false;
        }
        const ScriptObject want = object_from_image(update.exit);
        auto check = [&](const char* field, std::int64_t native, std::int64_t retail) {
            if (native != retail) {
                fail(update, std::string("field ") + field + ": native " + std::to_string(native) + " retail "
                    + std::to_string(retail));
                ok = false;
            }
        };
        for (std::size_t axis = 0; axis < 3; ++axis) {
            check("position", object.position[axis], want.position[axis]);
            check("velocity", object.velocity[axis], want.velocity[axis]);
            check("angle", object.params[axis], want.params[axis]);
        }
        check("road_node", object.traffic.road_node, want.traffic.road_node);
        check("latch", object.traffic.latch, want.traffic.latch);
        check("state", object.state, want.state);
        if (object.traffic.road_node != object_from_image(update.entry).traffic.road_node) {
            ++segment_changes;
        }
        for (const Call& call : update.calls) {
            if (call.name == "sfx_play") {
                ++sounds;
            }
        }
        draws += update.draws.size();
        if (ok) {
            ++matched;
        }
    }
    std::cout << "updates=" << updates.size() << " matched=" << matched << " mismatched=" << mismatched
              << " proximity_checks=" << proximity_calls << " sounds=" << sounds << " rnd_draws=" << draws
              << " segment_changes=" << segment_changes << '\n';
    return mismatched == 0 && !updates.empty() ? 0 : 1;
}
