// The score file's recorder (docs/LAUNCHER.md, "Scores"; THPS2 post_phase_5/highscores.md H4, H5): which finished runs become a line, and when. Portable, no game
// memory: standalone_scores.cpp reads the game's words and feeds them in (the hooks of runtime/score_hooks.hpp and one call a poll), opentony_score_recorder_test
// drives it with made-up values.
//
// A run is written when it finished in mode 1 or 3 (the hook only reports those), with one player, a score above 0, and no cheat on at any poll in a level since the
// previous run ended. A run that placed in the level's table of eight is held until the entry screen's accept, so the file has the letters the player chose; it is
// written with the letters the entry started from if the entry is left any other way: the place reset without an accept, the next run ending, the program ending.
// A crash loses at most that one held run.
#pragma once

#include "../../launcher/scores.hpp"

#include <array>
#include <cstdint>
#include <ctime>
#include <vector>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace opentony::kitdll::scores {

namespace sc = opentony::scores;

constexpr int kCheatCount = 17;
constexpr std::uint32_t kStatusPlaying = 0;   // GStatus (0x00561bfc): playing
constexpr std::uint32_t kStatusPause = 1;     // the pause menu, where cheat codes are typed

// The cheat list (0x0056a374, 17 entries of 4 bytes: unlocked, the character that earned it, the value at +2), as H2 read it: entries 0-2 unlock skaters and have no
// on/off; 3-6 and 9-16 are on when +2 is not 0; 8 (Weight) is a value, 100 normal (a new career's reset writes 100; 0 is a table not yet set up); 7 is STUD (stats at
// 10), allowed on the board.
inline bool cheat_on(const std::uint8_t* table) {
    for (int k = 3; k < kCheatCount; ++k) {
        const std::uint8_t value = table[4 * k + 2];
        if (k == 7) {
            continue;
        }
        if (k == 8 ? value != 100 && value != 0 : value != 0) {
            return true;
        }
    }
    return false;
}

struct RunEnd {
    std::uint32_t mode = 0;      // 1 career, 3 single session
    std::int32_t level = 0;
    std::uint32_t table = 0;     // 0..12, 13 a park editor level
    std::int32_t place = -1;
    std::int64_t score = 0;
    std::int32_t skater = -1;
    std::uint32_t players = 1;   // 0x00533f3c, or 2 when the second player's object is there
    bool cheat_on = false;       // at the call
    std::string initials;        // 0x0056a408..a: the letters the table row was given
};

// ---- the game's own tables filled from the file (H7) ----------------------------------------------------------------------------------------------------------
//
// A row of a score table (0x00569ebc: 14 tables of 8 rows of 8 bytes): three letters, the character number, the score dword.
struct Entry {
    char letters[3] = {' ', ' ', ' '};
    std::uint8_t skater = 0;
    std::uint32_t score = 0;
    bool operator==(const Entry& other) const {
        return letters[0] == other.letters[0] && letters[1] == other.letters[1] && letters[2] == other.letters[2] && skater == other.skater && score == other.score;
    }
};
using Table = std::array<Entry, 8>;
constexpr std::size_t kKeptRuns = 16;   // a table's best file runs worth keeping: at most 8 of them can already be in the table, so the best 8 others are among these

// A file run as a table row, or nothing when the game could not show it: a skater number outside its 20 rows, a score the HUD would not show (above 0x70000000).
// Letters are upper-cased; anything the entry screen cannot make (it offers A to Z and a space) becomes a space.
inline std::optional<Entry> entry_of(const sc::Record& record) {
    if (record.skater < 0 || record.skater >= static_cast<int>(sc::kSkaterNames.size()) || record.score <= 0 || record.score > 0x70000000) {
        return std::nullopt;
    }
    Entry e;
    for (std::size_t k = 0; k < 3; ++k) {
        char c = k < record.initials.size() ? record.initials[k] : ' ';
        c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        e.letters[k] = c >= 'A' && c <= 'Z' ? c : ' ';
    }
    e.skater = static_cast<std::uint8_t>(record.skater);
    e.score = static_cast<std::uint32_t>(record.score);
    return e;
}

// Keeps a table's best file runs, best first (ties: the earlier run first), at most kKeptRuns
inline void keep_run(std::vector<Entry>& kept, const Entry& run) {
    auto at = kept.begin();
    while (at != kept.end() && at->score >= run.score) {
        ++at;
    }
    kept.insert(at, run);
    if (kept.size() > kKeptRuns) {
        kept.pop_back();
    }
}

// The best eight of the table's rows and the file's runs, by score; ties keep the table's rows first, in their order, then the runs in theirs. A run that is already in
// the table (the same letters, skater and score: a run of this session the game inserted itself, or a save that holds it) is not put in again; each row answers for
// one run, so the same score twice in the file can be there twice.
inline Table merge_table(const Table& table, const std::vector<Entry>& runs) {
    bool matched[8] = {};
    std::vector<Entry> extra;
    for (const Entry& run : runs) {
        bool found = false;
        for (std::size_t k = 0; k < table.size() && !found; ++k) {
            if (!matched[k] && table[k] == run) {
                matched[k] = found = true;
            }
        }
        if (!found) {
            extra.push_back(run);
        }
    }
    Table out;
    std::size_t row = 0, run = 0;
    for (std::size_t k = 0; k < out.size(); ++k) {
        const bool take_run = run < extra.size() && (row >= table.size() || extra[run].score > table[row].score);
        out[k] = take_run ? extra[run++] : table[row++];
    }
    return out;
}

class Recorder {
public:
    using Log = std::function<void(const std::string&)>;
    using Written = std::function<void(const sc::Record&)>;

    Recorder(std::filesystem::path file, Log log, Written written = nullptr) : file_(std::move(file)), log_(std::move(log)), written_(std::move(written)) {}

    // every input poll: the front end's status, the place word 0x0052bf34, whether a cheat is on
    void poll(std::uint32_t status, std::int32_t place, bool cheat) {
        if (cheat && (status == kStatusPlaying || status == kStatusPause)) {
            dirty_ = true;
        }
        if (status == kStatusPlaying) {
            played_ = true;
        }
        if (pending_ && place == -1) {
            write_pending("the entry was left without an accept");
        }
    }

    void run_end(const RunEnd& run, std::time_t now) {
        if (pending_) {
            write_pending("another run ended first");
        }
        if (!played_) {
            log("scores: run end ignored (the game was not played since the last one)");
            return;
        }
        played_ = false;
        const bool dirty = dirty_ || run.cheat_on;
        dirty_ = false;
        if (run.mode != 1 && run.mode != 3) {
            log("scores: run not recorded (mode " + std::to_string(run.mode) + ")");
            return;
        }
        if (run.mode == 1 && speed_run_) {
            log("scores: run not recorded (a speed run: its time is the record)");
            return;
        }
        if (run.players != 1) {
            log("scores: run not recorded (two players)");
            return;
        }
        if (run.score <= 0) {
            log("scores: run not recorded (score 0)");
            return;
        }
        if (dirty) {
            log("scores: run not recorded (cheat on)");
            return;
        }
        const int level = run.table == static_cast<std::uint32_t>(sc::kCustomParkLevel) ? sc::kCustomParkLevel : run.level;
        sc::Record record = sc::make_record(run.mode == 1 ? sc::Mode::Career : sc::Mode::Single, level, run.skater, run.initials, run.score, now);
        if (run.place >= 0) {
            pending_ = std::move(record);
            pending_table_ = run.table;
            pending_place_ = run.place;
            log("scores: run held for the initials entry (place " + std::to_string(run.place + 1) + ")");
            return;
        }
        write(record);
    }

    // the entry screen's accept: the row's letters as the player left them
    void accepted(std::uint32_t table, std::int32_t place, const std::string& letters) {
        if (!pending_ || pending_table_ != table || pending_place_ != place) {
            return;
        }
        pending_->initials = letters;
        sc::Record record = std::move(*pending_);
        pending_.reset();
        write(record);
    }

    // A Goals Speed Run's career runs are not score runs (the run is open-ended): its time is written by `session` instead
    void set_speed_run(bool on) { speed_run_ = on; }

    // A finished Goals Speed Run or Competition Session (mode speedrun or competition, its time or total filled in), written at once: its letters are the remembered
    // ones, as there is no entry screen. Not written when a cheat was on during it (`cheat`, the caller's watch) or at the call.
    void session(const sc::Record& record, bool cheat) {
        if (pending_) {
            write_pending("a session ended first");
        }
        if (cheat) {
            log(std::string("scores: ") + sc::mode_text(record.mode) + " not recorded (cheat on)");
            return;
        }
        write(record);
    }

    // the program ends
    void shutdown() {
        if (pending_) {
            write_pending("the game is closing");
        }
    }

    bool has_pending() const { return pending_.has_value(); }

private:
    void write_pending(const char* why) {
        sc::Record record = std::move(*pending_);
        pending_.reset();
        log(std::string("scores: held run written with its starting initials (") + why + ")");
        write(record);
    }

    void write(const sc::Record& record) {
        std::string error;
        if (sc::append(file_, record, &error)) {
            std::string what = std::to_string(record.score);
            if (sc::is_speed_run(record.mode)) {
                what = sc::run_time(record.time_ms) + " (score " + what + ")";
            } else if (record.mode == sc::Mode::Competition) {
                what = sc::judged_text(record.judged) + " " + (record.medal.empty() ? "none" : record.medal) + " (best heat " + what + ")";
            }
            log("scores: recorded " + std::string(sc::mode_text(record.mode)) + " " + record.level_name + " " + what + " '" + record.initials + "'");
            if (written_) {
                written_(record);
            }
        } else {
            log("scores: cannot write the score file (" + error + ")");
        }
    }

    void log(const std::string& line) const {
        if (log_) {
            log_(line);
        }
    }

    std::filesystem::path file_;
    Log log_;
    Written written_;
    bool speed_run_ = false;
    bool dirty_ = false;    // a cheat was on at a poll in a level since the last run ended
    bool played_ = false;   // the game was played (status 0) since the last run ended: one run, one line
    std::optional<sc::Record> pending_;
    std::uint32_t pending_table_ = 0;
    std::int32_t pending_place_ = -1;
};

}  // namespace opentony::kitdll::scores
