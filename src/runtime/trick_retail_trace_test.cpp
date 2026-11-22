// Replays retail trick traces (see scripts/make_trick_trace_fixture.py) through the native
// TrickScript_Update / TrickScript_Execute.
//
//   opentony_trick_retail_trace_test FIXTURE.txt TRICKS.BIN
//
// Every TrickScript_Update replays from the raw 0x3538-byte skater image retail entered it
// with. The native code must (1) leave the image word for word equal to retail's at the
// Execute entry (or at the exit when no script ran), (2) make the same service calls in the
// same order, and (3) leave the image equal at the Execute return and at the update exit.
// Services outside 4.7a (animation, sound, Bail, ...) are seams: their retail effect on the
// image is applied from the trace. Every Execute that ran is then replayed a second time on
// its own from retail's Execute-entry image.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error, 77 TRICKS.BIN not available.
#include "trick_script.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;

namespace {

constexpr std::size_t kWords = kSkaterImageSize / 4;
using Words = std::vector<std::uint32_t>;

const std::map<std::string, TrickService> kServices{
    {"anim_start", TrickService::AnimStart}, {"anim_start_default_end", TrickService::AnimStartDefaultEnd},
    {"anim_range", TrickService::AnimRange}, {"anim_full", TrickService::AnimFull},
    {"state_transition", TrickService::StateTransition}, {"point_timer", TrickService::PointTimer},
    {"bail", TrickService::Bail}, {"take_off", TrickService::TakeOff}, {"land", TrickService::Land},
    {"gap_apply", TrickService::GapApply}, {"board_on", TrickService::BoardOn},
    {"board_off", TrickService::BoardOff}, {"sfx_play", TrickService::SfxPlay},
    {"vibrate", TrickService::Vibrate}, {"rail_restart", TrickService::RailRestart},
    {"fn_46dd20", TrickService::Fn46dd20}, {"fn_431260", TrickService::Fn431260},
    {"maybe_manual", TrickService::MaybeManual}, {"assert", TrickService::Assert}, {"rnd", TrickService::Rnd},
};

struct Event {
    TrickService service{};
    std::uint32_t eax{};
    std::array<std::uint32_t, 6> args{};
    Words pre;
    Words post;
};

struct Record {
    long seq{};
    long frame{};
    std::uint32_t clock{};
    std::int32_t time_scale{};
    std::int32_t step{};
    std::int32_t latch{};
    std::uint32_t base{};
    std::size_t table{};  // index into the table list
    RetailImage controller{kControllerImageSize};
    Words entry, exec_in, exec_out, exit;
    bool has_exec{};
    std::size_t events_before_exec{};
    std::vector<Event> events;
};

void apply_diff(Words& image, std::istringstream& tokens) {
    std::string token;
    while (tokens >> token) {
        const std::size_t colon = token.find(':');
        image.at(std::stoul(token.substr(0, colon))) =
            static_cast<std::uint32_t>(std::stoul(token.substr(colon + 1), nullptr, 16));
    }
}

RetailImage to_image(const Words& words) {
    RetailImage image(kSkaterImageSize);
    std::memcpy(image.bytes().data(), words.data(), kSkaterImageSize);
    return image;
}

Words to_words(const RetailImage& image) {
    Words words(kWords);
    std::memcpy(words.data(), image.bytes().data(), kSkaterImageSize);
    return words;
}

struct Mismatch {
    std::string what;
};

std::string describe(const Words& native, const Words& retail) {
    std::ostringstream text;
    int shown = 0;
    if (native.size() != retail.size()) {
        return " (retail image missing)";
    }
    for (std::size_t i = 0; i < kWords && shown < 8; ++i) {
        if (native[i] != retail[i]) {
            text << " +0x" << std::hex << i * 4 << ": native=0x" << native[i] << " retail=0x" << retail[i];
            ++shown;
        }
    }
    return text.str();
}

void expect_same(const RetailImage& native, const Words& retail, const char* where) {
    const Words words = to_words(native);
    if (words != retail) {
        throw Mismatch{std::string("image differs at ") + where + ":" + describe(words, retail)};
    }
}

class ReplayServices final : public TrickServices {
public:
    ReplayServices(const Record& record, std::size_t first_event) : record_(record), next_(first_event) {}

    std::uint32_t call(RetailImage& skater, const TrickServiceCall& call) override {
        if (next_ >= record_.events.size()) {
            throw Mismatch{"native made an extra service call " + std::to_string(static_cast<int>(call.service))};
        }
        const Event& event = record_.events[next_++];
        if (event.service != call.service) {
            throw Mismatch{"service " + std::to_string(static_cast<int>(call.service)) + " where retail called "
                           + std::to_string(static_cast<int>(event.service))};
        }
        const std::size_t count = trick_service_arg_count(call.service);
        for (std::size_t i = 0; i < count; ++i) {
            if (call.args[i] != event.args[i]) {
                throw Mismatch{"service " + std::to_string(static_cast<int>(call.service)) + " argument "
                               + std::to_string(i) + ": native=" + std::to_string(call.args[i])
                               + " retail=" + std::to_string(event.args[i])};
            }
        }
        // Seam: apply the service's own effect on the skater image.
        for (std::size_t i = 0; i < kWords; ++i) {
            if (event.pre[i] != event.post[i]) {
                skater.set32(i * 4, event.post[i]);
            }
        }
        ++replayed;
        return event.eax;
    }
    [[nodiscard]] bool exhausted() const { return next_ == record_.events.size(); }
    [[nodiscard]] std::size_t position() const { return next_; }
    std::size_t replayed{};

private:
    const Record& record_;
    std::size_t next_{};
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: opentony_trick_retail_trace_test FIXTURE.txt TRICKS.BIN\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(argv[2])) {
        std::cout << "SKIP: " << argv[2] << " is not extracted\n";
        return 77;
    }
    std::vector<std::uint8_t> tricks;
    {
        std::ifstream in(argv[2], std::ios::binary);
        tricks.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::ifstream fixture(argv[1]);
    if (!fixture) {
        std::cerr << "cannot open fixture " << argv[1] << '\n';
        return 2;
    }

    std::vector<std::vector<std::uint16_t>> tables;
    std::vector<Record> records;
    Words written(kWords, 0);
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#' || line[0] == 'S') {
            continue;
        }
        std::istringstream tokens(line.size() > 2 ? line.substr(2) : "");
        const char kind = line[0];
        if (kind == 'T') {
            std::vector<std::uint16_t> table;
            std::string word;
            while (tokens >> word) {
                table.push_back(static_cast<std::uint16_t>(std::stoul(word, nullptr, 16)));
            }
            tables.push_back(std::move(table));
        } else if (kind == 'U') {
            Record record;
            tokens >> record.seq >> record.frame >> record.clock >> record.time_scale >> record.step >> record.latch
                >> record.base;
            record.table = tables.size() - 1;
            records.push_back(std::move(record));
        } else if (records.empty()) {
            continue;
        } else if (kind == 'C') {
            std::string hex;
            tokens >> hex;
            for (std::size_t i = 0; i < kControllerImageSize; ++i) {
                records.back().controller.set8(i, static_cast<std::uint32_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16)));
            }
        } else if (kind == 'K') {
            Event event;
            std::string name;
            tokens >> name >> event.eax;
            for (auto& arg : event.args) {
                tokens >> arg;
            }
            const auto found = kServices.find(name);
            if (found == kServices.end()) {
                std::cerr << "unknown service " << name << '\n';
                return 2;
            }
            event.service = found->second;
            records.back().events.push_back(std::move(event));
        } else {
            apply_diff(written, tokens);
            Record& record = records.back();
            if (kind == 'I') {
                record.entry = written;
            } else if (kind == 'E') {
                record.exec_in = written;
                record.has_exec = true;
                record.events_before_exec = record.events.size();
            } else if (kind == 'P') {
                record.events.back().pre = written;
            } else if (kind == 'R') {
                record.events.back().post = written;
            } else if (kind == 'O') {
                record.exec_out = written;
            } else if (kind == 'X') {
                record.exit = written;
            }
        }
    }
    if (records.empty()) {
        std::cerr << "fixture has no records\n";
        return 2;
    }

    std::array<std::uint32_t, 256> opcodes{};
    std::size_t executes = 0;
    std::size_t services = 0;
    std::size_t exec_only = 0;
    std::size_t failures = 0;
    std::map<std::string, std::size_t> reasons;
    for (const Record& record : records) {
        TrickWorld world;
        world.tricks_bin = tricks;
        world.tricks_base = record.base;
        world.clock = record.clock;
        world.time_scale = record.time_scale;
        world.step = record.step;
        world.event_latch = record.latch;
        world.controller = &record.controller;
        world.key_table = tables.at(record.table);
        world.opcode_counts = &opcodes;
        try {
            // Whole update.
            RetailImage skater = to_image(record.entry);
            ReplayServices replay(record, 0);
            trick_input_refresh_history(skater, world, replay);
            trick_input_select_sequence(skater, world, replay);
            trick_script_start_pending(skater, world, replay);
            if (record.has_exec) {
                expect_same(skater, record.exec_in, "the Execute entry");
                if (replay.position() != record.events_before_exec) {
                    throw Mismatch{"service calls before Execute differ"};
                }
                trick_script_execute(skater, world, replay);
                expect_same(skater, record.exec_out, "the Execute return");
                ++executes;
            } else if (skater.s32(0x29c8) != 0) {
                throw Mismatch{"native runs a script where retail did not"};
            }
            expect_same(skater, record.exit, "the update exit");
            if (!replay.exhausted()) {
                throw Mismatch{"retail made service calls native did not"};
            }
            services += replay.replayed;

            // Execute on its own from retail's entry image.
            if (record.has_exec) {
                RetailImage alone = to_image(record.exec_in);
                ReplayServices only(record, record.events_before_exec);
                trick_script_execute(alone, world, only);
                expect_same(alone, record.exec_out, "the stand-alone Execute return");
                ++exec_only;
            }
        } catch (const Mismatch& mismatch) {
            ++failures;
            ++reasons[mismatch.what.substr(0, 90)];
            if (failures <= 8) {
                std::cerr << "MISMATCH seq " << record.seq << " frame " << record.frame << ": " << mismatch.what << '\n';
            }
        } catch (const std::exception& error) {
            ++failures;
            ++reasons[std::string("exception: ") + error.what()];
            if (failures <= 8) {
                std::cerr << "ERROR seq " << record.seq << " frame " << record.frame << ": " << error.what() << '\n';
            }
        }
    }

    std::cout << "updates=" << records.size() << " executes=" << executes << " stand_alone=" << exec_only
              << " services=" << services << " failures=" << failures << '\n';
    std::cout << "opcodes:";
    for (std::size_t op = 0; op < opcodes.size(); ++op) {
        if (opcodes[op] != 0) {
            std::printf(" %02zx:%u", op, opcodes[op]);
        }
    }
    std::cout << '\n';
    for (const auto& [reason, count] : reasons) {
        std::cout << "  " << count << "x " << reason << '\n';
    }
    return failures == 0 ? 0 : 1;
}
