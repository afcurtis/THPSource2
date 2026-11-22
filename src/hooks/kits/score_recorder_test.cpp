// The score file's recorder (score_recorder.hpp) driven with made-up values, the file read back through the launcher's reader (src/launcher/scores.hpp):
// which runs are written (clean, cheat on, score 0, two players, one run twice), and the held run of a place in the table (accept with changed letters, the place
// reset without an accept, another run ending, the program ending).
#include "score_recorder.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace sr = opentony::kitdll::scores;
namespace sc = opentony::scores;

namespace {

int g_failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

sr::RunEnd run(std::uint32_t mode, std::int32_t level, std::int64_t score, std::int32_t place = -1) {
    sr::RunEnd r;
    r.mode = mode;
    r.level = level;
    r.table = static_cast<std::uint32_t>(level);
    r.place = place;
    r.score = score;
    r.skater = 1;
    r.initials = "AFC";
    return r;
}

// a run played: some polls at status 0, then its end
void play(sr::Recorder& recorder, bool cheat = false) {
    for (int k = 0; k < 5; ++k) {
        recorder.poll(sr::kStatusPlaying, -1, cheat);
    }
}

void test_cheat_rule() {
    std::uint8_t table[4 * sr::kCheatCount] = {};
    table[4 * 8 + 2] = 100;   // Weight, normal
    CHECK(!sr::cheat_on(table));
    for (int k = 0; k < 3; ++k) {   // skater unlocks: no on/off
        table[4 * k] = 1;
        table[4 * k + 2] = 1;
    }
    table[4 * 7 + 2] = 1;           // STUD: max stats are allowed
    CHECK(!sr::cheat_on(table));
    table[4 * 8 + 2] = 0;           // a table not yet set up
    CHECK(!sr::cheat_on(table));
    table[4 * 8 + 2] = 150;         // Weight changed
    CHECK(sr::cheat_on(table));
    table[4 * 8 + 2] = 100;
    for (int k : {3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15, 16}) {
        table[4 * k + 2] = 1;
        CHECK(sr::cheat_on(table));
        table[4 * k + 2] = 0;
    }
    table[4 * 12] = 1;              // unlocked but off
    CHECK(!sr::cheat_on(table));
}

void test_runs(const fs::path& scratch) {
    const fs::path file = scratch / "runs.tsv";
    std::vector<std::string> log;
    sr::Recorder recorder(file, [&](const std::string& line) { log.push_back(line); });

    // a clean single session, no place: written at once
    play(recorder);
    recorder.run_end(run(3, 12, 48210), 0);
    // the same run reported again with no play between: one run, one line
    recorder.run_end(run(3, 12, 48210), 0);
    CHECK(!log.empty() && log.back().find("ignored") != std::string::npos);
    // a career level
    play(recorder);
    recorder.run_end(run(1, 0, 9000), 0);
    // a cheat on at a poll in the level, switched off before the end: not written
    recorder.poll(sr::kStatusPause, -1, true);
    play(recorder);
    recorder.run_end(run(3, 12, 70000), 0);
    CHECK(log.back() == "scores: run not recorded (cheat on)");
    // the next run is clean again
    play(recorder);
    recorder.run_end(run(3, 12, 1234), 0);
    // a cheat on at the call itself
    play(recorder);
    sr::RunEnd cheated = run(3, 12, 5555);
    cheated.cheat_on = true;
    recorder.run_end(cheated, 0);
    // a cheat on only in the menus between runs (not in a level): the run counts
    recorder.poll(0x0e, -1, true);
    play(recorder);
    recorder.run_end(run(3, 5, 4321), 0);
    // score 0, two players, free skate: not written
    play(recorder);
    recorder.run_end(run(3, 12, 0), 0);
    CHECK(log.back() == "scores: run not recorded (score 0)");
    play(recorder);
    sr::RunEnd two = run(3, 12, 800);
    two.players = 2;
    recorder.run_end(two, 0);
    CHECK(log.back() == "scores: run not recorded (two players)");
    play(recorder);
    recorder.run_end(run(2, 12, 800), 0);
    // a park editor level: table 13, "Custom park"
    play(recorder);
    sr::RunEnd park = run(3, 4, 321);
    park.table = 13;
    recorder.run_end(park, 0);

    const sc::File back = sc::load(file);
    CHECK(back.bad_lines == 0 && back.records.size() == 5);
    if (back.records.size() == 5) {
        CHECK(back.records[0].mode == sc::Mode::Single && back.records[0].level == 12 && back.records[0].score == 48210 && back.records[0].initials == "AFC" &&
              back.records[0].skater_name == "Bob Burnquist");
        CHECK(back.records[1].mode == sc::Mode::Career && back.records[1].level == 0 && back.records[1].level_name == "Hangar");
        CHECK(back.records[2].score == 1234 && back.records[3].score == 4321 && back.records[3].level_name == "Skate Street");
        CHECK(back.records[4].level == 13 && back.records[4].level_name == "Custom park");
    }
}

void test_held_runs(const fs::path& scratch) {
    const fs::path file = scratch / "held.tsv";
    std::vector<std::string> log;
    sr::Recorder recorder(file, [&](const std::string& line) { log.push_back(line); });

    // placed: held, then accepted with changed letters
    play(recorder);
    recorder.run_end(run(3, 12, 50000, 0), 0);
    CHECK(recorder.has_pending() && !fs::exists(file));
    recorder.poll(0x0d, 0, false);   // the statistics, the place still set
    recorder.poll(0x2e, 0, false);   // the entry
    recorder.accepted(12, 1, "XYZ"); // another row: not this run
    CHECK(recorder.has_pending());
    recorder.accepted(12, 0, "B B");
    CHECK(!recorder.has_pending());
    recorder.poll(0x0e, -1, false);
    // placed, and the place is reset without an accept: written with the starting letters
    play(recorder);
    recorder.run_end(run(1, 3, 60000, 2), 0);
    recorder.poll(0x0d, 2, false);
    recorder.poll(0x0e, -1, false);
    CHECK(!recorder.has_pending());
    // placed, and another run ends before the entry is done
    play(recorder);
    recorder.run_end(run(3, 12, 70000, 0), 0);
    for (int k = 0; k < 5; ++k) {
        recorder.poll(sr::kStatusPlaying, 0, false);   // played again with the place still set: still held
    }
    CHECK(recorder.has_pending());
    recorder.run_end(run(3, 12, 100, -1), 0);
    // placed, and the program ends on the entry screen
    play(recorder);
    recorder.run_end(run(3, 12, 80000, 0), 0);
    recorder.poll(0x2e, 0, false);
    recorder.shutdown();
    CHECK(!recorder.has_pending());
    recorder.shutdown();   // twice is harmless

    const sc::File back = sc::load(file);
    CHECK(back.bad_lines == 0 && back.records.size() == 5);
    if (back.records.size() == 5) {
        CHECK(back.records[0].score == 50000 && back.records[0].initials == "B B");
        CHECK(back.records[1].score == 60000 && back.records[1].initials == "AFC" && back.records[1].mode == sc::Mode::Career);
        CHECK(back.records[2].score == 70000 && back.records[2].initials == "AFC");
        CHECK(back.records[3].score == 100);
        CHECK(back.records[4].score == 80000 && back.records[4].initials == "AFC");
    }
}

// the quick start's sessions: a speed run's career runs are not score runs, its time is written at once; a competition's line; a cheat keeps either off
void test_sessions(const fs::path& scratch) {
    const fs::path file = scratch / "sessions.tsv";
    std::vector<std::string> log;
    std::vector<sc::Record> written;
    sr::Recorder recorder(file, [&](const std::string& line) { log.push_back(line); }, [&](const sc::Record& r) { written.push_back(r); });
    recorder.set_speed_run(true);
    play(recorder);
    recorder.run_end(run(1, 0, 912400), 0);
    CHECK(log.back() == "scores: run not recorded (a speed run: its time is the record)");
    play(recorder);
    recorder.run_end(run(3, 0, 500), 0);   // (a single session would still count)
    sc::Record speed = sc::make_record(sc::Mode::SpeedRun, 0, 1, "AFC", 912400, 0);
    speed.time_ms = 245320;
    recorder.session(speed, false);
    CHECK(log.back() == "scores: recorded speedrun Hangar 4:05.32 (score 912400) 'AFC'");
    recorder.session(speed, true);
    CHECK(log.back() == "scores: speedrun not recorded (cheat on)");
    sc::Record comp = sc::make_record(sc::Mode::Competition, 3, 1, "AFC", 151230, 0);
    comp.judged = 1786;
    comp.medal = "gold";
    // a run held for its initials is written first
    recorder.set_speed_run(false);
    play(recorder);
    recorder.run_end(run(1, 0, 70000, 0), 0);
    CHECK(recorder.has_pending());
    recorder.session(comp, false);
    CHECK(!recorder.has_pending());
    CHECK(log.back() == "scores: recorded competition New York City 178.6 gold (best heat 151230) 'AFC'");
    const sc::File back = sc::load(file);
    CHECK(back.bad_lines == 0 && back.records.size() == 4 && written.size() == 4);
    if (back.records.size() == 4) {
        CHECK(back.records[0].mode == sc::Mode::Single && back.records[1].mode == sc::Mode::SpeedRun && back.records[1].time_ms == 245320);
        CHECK(back.records[2].mode == sc::Mode::Career && back.records[3].mode == sc::Mode::Competition && back.records[3].judged == 1786);
    }
}

// a file that cannot be written: logged, never fatal
void test_unwritable(const fs::path& scratch) {
    std::vector<std::string> log;
    sr::Recorder recorder(scratch / "no such folder" / "x.tsv", [&](const std::string& line) { log.push_back(line); });
    play(recorder);
    recorder.run_end(run(3, 12, 10), 0);
    CHECK(!log.empty() && log.back().find("cannot write") != std::string::npos);
}

sr::Entry entry(const char* letters, std::uint8_t skater, std::uint32_t score) {
    sr::Entry e;
    for (int k = 0; k < 3; ++k) {
        e.letters[k] = letters[k];
    }
    e.skater = skater;
    e.score = score;
    return e;
}

// the game's own tables filled from the file (H7)
void test_merge() {
    sr::Table table;   // the Hangar's defaults: 40000 down to 5000
    for (std::uint32_t k = 0; k < 8; ++k) {
        table[k] = entry("DEF", 2, (8 - k) * 5000);
    }
    std::vector<sr::Entry> runs;
    for (const sr::Entry& run : {entry("AFC", 0, 12000), entry("AFC", 1, 50000), entry("BOB", 1, 3000), entry("AFC", 0, 12000), entry("ZZZ", 3, 20000)}) {
        sr::keep_run(runs, run);
    }
    CHECK(runs.size() == 5 && runs[0].score == 50000 && runs[4].score == 3000);
    const sr::Table merged = sr::merge_table(table, runs);
    // 50000 AFC, 40000, 35000, 30000, 25000, 20000 DEF (the table's row first on a tie), 20000 ZZZ, 15000 DEF; the 12000 twice and 3000 fall out
    CHECK(merged[0] == entry("AFC", 1, 50000) && merged[1].score == 40000 && merged[5] == entry("DEF", 2, 20000) && merged[6] == entry("ZZZ", 3, 20000) &&
          merged[7].score == 15000);
    // a second pass changes nothing, and a run the table already has (the game's own insert, or a save holding it) is not put in twice
    CHECK(sr::merge_table(merged, runs) == merged);
    sr::Table saved = table;
    saved[0] = entry("AFC", 1, 50000);
    saved[1] = entry("DEF", 2, 40000);
    const sr::Table again = sr::merge_table(saved, runs);
    int fifty = 0;
    for (const sr::Entry& e : again) {
        fifty += e.score == 50000 ? 1 : 0;
    }
    CHECK(fifty == 1);
    // the same score twice in the file is two runs: both go in when they beat the table
    std::vector<sr::Entry> twice;
    sr::keep_run(twice, entry("AFC", 0, 90000));
    sr::keep_run(twice, entry("AFC", 0, 90000));
    const sr::Table both = sr::merge_table(table, twice);
    CHECK(both[0] == entry("AFC", 0, 90000) && both[1] == entry("AFC", 0, 90000) && both[2].score == 40000);
    // only the best kKeptRuns are kept
    std::vector<sr::Entry> many;
    for (std::uint32_t k = 0; k < 40; ++k) {
        sr::keep_run(many, entry("AAA", 0, 1000 + k));
    }
    CHECK(many.size() == sr::kKeptRuns && many.front().score == 1039 && many.back().score == 1039 - sr::kKeptRuns + 1);
    // records the game could not show are left out; letters are upper-cased and anything else is a space
    sc::Record r = sc::make_record(sc::Mode::Single, 0, 25, "afc", 100, 0);
    CHECK(!sr::entry_of(r));
    r.skater = 3;
    r.initials = "a7";
    const auto e = sr::entry_of(r);
    CHECK(e && e->letters[0] == 'A' && e->letters[1] == ' ' && e->letters[2] == ' ' && e->skater == 3 && e->score == 100);
    r.score = 0x70000001;
    CHECK(!sr::entry_of(r));
}

}  // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / ("score_recorder_test_" + std::to_string(std::rand()));
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    test_cheat_rule();
    test_runs(scratch);
    test_held_runs(scratch);
    test_sessions(scratch);
    test_unwritable(scratch);
    test_merge();
    fs::remove_all(scratch);
    if (g_failures != 0) {
        std::fprintf(stderr, "score_recorder_test: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("score_recorder_test: ok\n");
    return 0;
}
