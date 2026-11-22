// The score file (docs/LAUNCHER.md, "Scores"): every finished career level run, single session, Goals Speed Run and Competition Session, one line a run, written
// by the game the moment the run ends (src/hooks/kits/standalone_scores.cpp) and read by the launcher's High Scores page. Shared by THPSource2.exe (64-bit) and the
// game's DLL (32-bit or 64-bit): the standard library only, no SDL, no Windows calls.
//
// <install>\Saves\highscores.tsv, UTF-8, tab separated:
//
//   # THPSource2 scores v1
//   date	mode	level	level_name	skater	skater_name	initials	score	time_ms	judged	medal
//   2026-11-15 21:04:33	single	12	Warehouse	0	Tony Hawk	AFC	48210
//   2026-11-20 18:02:10	speedrun	0	Hangar	0	Tony Hawk	AFC	912400	245320
//   2026-11-20 18:20:41	competition	3	New York City	0	Tony Hawk	AFC	151230		1786	gold
//
// The second line names the columns, and a reader takes them by name, so a later version can add columns (a reader ignores the ones it does not know) or move
// them. Lines starting with '#' are comments. A line that cannot be read is skipped and counted, never fatal: the file is the player's, plain text they may edit.
// The game only ever appends to it; a file whose column line predates the last three columns gets the new column line again before the first run that has them.
//
// The modes: career and single are a run's trick score. speedrun is a Goals Speed Run of every goal but the level's 100% one, speedrun_cash one of all ten (every
// cash icon too; a board of its own): `time_ms` the game time from the run's start to its last goal (the pause and the menus not counted), `score` the trick score
// at that moment (shown, never ranked). competition is a Competition Session's three heats: `judged` the judges' total
// in tenths (the two best heats, as the standings add them), `medal` gold, silver, bronze or none, `score` the trick score of the better of the two heats that count
// (shown on the competition board; a one-minute heat does not rank with the career and single runs).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace opentony::scores {

inline constexpr const char* kMagic = "# THPSource2 scores v1";
inline constexpr const char* kFileName = "highscores.tsv";
inline constexpr std::array<const char*, 11> kColumns = {"date", "mode", "level", "level_name", "skater", "skater_name", "initials", "score", "time_ms", "judged", "medal"};
inline constexpr std::size_t kFirstSessionColumn = 8;   // time_ms: the columns from here on came with the speed run and the competition
inline constexpr int kLevelCount = 14;      // the game's 14 score tables: levels 0..12 and 13 for every park editor level
inline constexpr int kCustomParkLevel = 13;

// The level and skater names the file carries, so it reads on its own. The launcher's own lists (core/settings.cpp kLevelNames, kSkaterNames: a test keeps
// them equal) plus "Custom park" for table 13.
inline constexpr std::array<const char*, kLevelCount> kLevelNames = {"Hangar", "School II", "Marseille", "New York City", "Venice Beach", "Skate Street",
                                                                     "Philadelphia", "The Bullring", "Chopper Drop", "Skatepark Heaven", "Downhill Jam",
                                                                     "Skate Park", "Warehouse", "Custom park"};
inline constexpr std::array<const char*, 20> kSkaterNames = {"Tony Hawk", "Bob Burnquist", "Steve Caballero", "Kareem Campbell", "Rune Glifberg", "Eric Koston",
                                                             "Bucky Lasek", "Rodney Mullen", "Chad Muska", "Andrew Reynolds", "Geoff Rowley", "Elissa Steamer",
                                                             "Jamie Thomas", "Custom Skater 1", "Custom Skater 2", "Custom Skater 3", "Custom Skater 4",
                                                             "Officer Dick", "Private Carrera", "Spider-Man"};

inline const char* level_name(int level) { return level >= 0 && level < kLevelCount ? kLevelNames[static_cast<std::size_t>(level)] : "Unknown level"; }
inline const char* skater_name(int skater) {
    return skater >= 0 && skater < static_cast<int>(kSkaterNames.size()) ? kSkaterNames[static_cast<std::size_t>(skater)] : "Unknown skater";
}

enum class Mode { Career, Single, SpeedRun, SpeedRunCash, Competition };
inline const char* mode_text(Mode mode) {
    switch (mode) {
    case Mode::Career: return "career";
    case Mode::SpeedRun: return "speedrun";
    case Mode::SpeedRunCash: return "speedrun_cash";
    case Mode::Competition: return "competition";
    default: return "single";
    }
}
inline bool is_speed_run(Mode mode) { return mode == Mode::SpeedRun || mode == Mode::SpeedRunCash; }

struct Record {
    std::string date;          // local date and time, "2026-11-15 21:04:33"
    Mode mode = Mode::Single;
    int level = 0;             // 0..12, 13 a park editor level
    std::string level_name;
    int skater = -1;           // the game's character number (the score table's byte 3), -1 unknown
    std::string skater_name;
    std::string initials;      // three letters, spaces kept
    std::int64_t score = 0;
    std::int64_t time_ms = -1; // a speed run's time; -1 none
    int judged = -1;           // a competition's judged total in tenths; -1 none
    std::string medal;         // a competition's medal: gold, silver, bronze, none
    std::size_t order = 0;     // the line's place among the file's runs (0 first): the reader's, not written. Ties go to the earlier run
};

// A run that ranks on the score board: career runs and single sessions. A speed run's score is the trick score of an open-ended run and a competition's that of a
// one-minute heat: each has its own board (by time, by the judges' total)
inline bool on_score_board(const Record& r) { return r.mode == Mode::Career || r.mode == Mode::Single; }

// A record for a run that ended now, its names filled in from the lists above
inline Record make_record(Mode mode, int level, int skater, std::string_view initials, std::int64_t score, std::time_t when) {
    Record r;
    char text[32] = {};
    if (const std::tm* local = std::localtime(&when); local != nullptr) {
        std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", local);
    }
    r.date = text;
    r.mode = mode;
    r.level = level;
    r.level_name = level_name(level);
    r.skater = skater;
    r.skater_name = skater_name(skater);
    r.initials = std::string(initials);
    r.score = score;
    return r;
}

namespace detail {

// a field never holds the file's separators
inline std::string clean(std::string_view text) {
    std::string out(text);
    std::replace_if(out.begin(), out.end(), [](char c) { return c == '\t' || c == '\r' || c == '\n'; }, ' ');
    return out;
}

inline std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) {
            return fields;
        }
        start = tab + 1;
    }
}

inline std::optional<std::int64_t> number(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (text.empty() || text.size() > 18) {
        return std::nullopt;
    }
    bool negative = false;
    if (text.front() == '-') {
        negative = true;
        text.remove_prefix(1);
        if (text.empty()) {
            return std::nullopt;
        }
    }
    std::int64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return negative ? -value : value;
}

}  // namespace detail

// The header lines a new file starts with
inline std::string header_text() {
    std::string text = std::string(kMagic) + "\n";
    for (std::size_t k = 0; k < kColumns.size(); ++k) {
        text += std::string(k == 0 ? "" : "\t") + kColumns[k];
    }
    return text + "\n";
}

// One run as a line, without its line end
// (a career or single run's line ends at its score, as before the session columns came)
inline std::string format_line(const Record& r) {
    std::string line = detail::clean(r.date) + "\t" + mode_text(r.mode) + "\t" + std::to_string(r.level) + "\t" + detail::clean(r.level_name) + "\t" +
                       std::to_string(r.skater) + "\t" + detail::clean(r.skater_name) + "\t" + detail::clean(r.initials) + "\t" + std::to_string(r.score);
    if (is_speed_run(r.mode) || r.mode == Mode::Competition) {
        line +="\t" + (r.time_ms >= 0 ? std::to_string(r.time_ms) : std::string()) + "\t" + (r.judged >= 0 ? std::to_string(r.judged) : std::string()) + "\t" +
                detail::clean(r.medal);
    }
    return line;
}

// Where each known column is in a line: -1 when the file does not have it. The file's column line sets it; before one, the order of kColumns.
struct Columns {
    std::array<int, kColumns.size()> at{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    bool has_session_columns() const {
        for (std::size_t k = kFirstSessionColumn; k < at.size(); ++k) {
            if (at[k] < 0) {
                return false;
            }
        }
        return true;
    }

    static std::optional<Columns> from_line(std::string_view line) {
        const std::vector<std::string_view> names = detail::split(line);
        Columns c;
        c.at.fill(-1);
        for (std::size_t k = 0; k < names.size(); ++k) {
            for (std::size_t j = 0; j < kColumns.size(); ++j) {
                if (names[k] == kColumns[j]) {
                    c.at[j] = static_cast<int>(k);
                }
            }
        }
        // a column line names at least mode, level and score, the three a run cannot do without
        if (c.at[1] < 0 || c.at[2] < 0 || c.at[7] < 0) {
            return std::nullopt;
        }
        return c;
    }
};

// One line as a run; nothing when it cannot be read. Mode, level and score must be there and valid; the other columns may be missing or empty.
inline std::optional<Record> parse_line(std::string_view line, const Columns& columns = Columns{}) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    const std::vector<std::string_view> fields = detail::split(line);
    const auto field = [&](std::size_t column) -> std::optional<std::string_view> {
        const int at = columns.at[column];
        if (at < 0 || static_cast<std::size_t>(at) >= fields.size()) {
            return std::nullopt;
        }
        return fields[static_cast<std::size_t>(at)];
    };
    Record r;
    const auto mode = field(1);
    if (!mode) {
        return std::nullopt;
    }
    if (*mode == "career") {
        r.mode = Mode::Career;
    } else if (*mode == "single") {
        r.mode = Mode::Single;
    } else if (*mode == "speedrun") {
        r.mode = Mode::SpeedRun;
    } else if (*mode == "speedrun_cash") {
        r.mode = Mode::SpeedRunCash;
    } else if (*mode == "competition") {
        r.mode = Mode::Competition;
    } else {
        return std::nullopt;
    }
    const auto time_ms = field(8) ? detail::number(*field(8)) : std::nullopt;
    const auto judged = field(9) ? detail::number(*field(9)) : std::nullopt;
    if (is_speed_run(r.mode)) {
        if (!time_ms || *time_ms < 0) {
            return std::nullopt;   // a speed run is its time
        }
        r.time_ms = *time_ms;
    }
    if (r.mode == Mode::Competition) {
        if (!judged || *judged < 0 || *judged > 100000) {
            return std::nullopt;   // a competition is its judged total
        }
        r.judged = static_cast<int>(*judged);
        r.medal = std::string(field(10).value_or(""));
    }
    const auto level = field(2) ? detail::number(*field(2)) : std::nullopt;
    const auto score = field(7) ? detail::number(*field(7)) : std::nullopt;
    if (!level || *level < 0 || *level >= kLevelCount || !score || *score < 0) {
        return std::nullopt;
    }
    r.level = static_cast<int>(*level);
    r.score = *score;
    r.date = std::string(field(0).value_or(""));
    r.level_name = field(3) && !field(3)->empty() ? std::string(*field(3)) : level_name(r.level);
    const auto skater = field(4) ? detail::number(*field(4)) : std::nullopt;
    r.skater = skater && *skater >= 0 && *skater < 1000 ? static_cast<int>(*skater) : -1;
    r.skater_name = field(5) && !field(5)->empty() ? std::string(*field(5)) : skater_name(r.skater);
    r.initials = std::string(field(6).value_or(""));
    return r;
}

struct File {
    std::vector<Record> records;   // in the file's order (oldest first, as the game appends)
    int bad_lines = 0;             // lines that are neither a comment, a column line nor a run
    bool missing = false;          // no file (or it cannot be opened)
};

inline File parse_text(std::string_view text) {
    File file;
    Columns columns;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef && static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.remove_prefix(3);   // a byte order mark an editor added
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (const auto named = Columns::from_line(line)) {   // a run's fields are never the words "mode", "level" and "score"
            columns = *named;
            continue;
        }
        if (auto record = parse_line(line, columns)) {
            record->order = file.records.size();
            file.records.push_back(std::move(*record));
        } else {
            ++file.bad_lines;
        }
    }
    return file;
}

inline File load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        File file;
        file.missing = true;
        return file;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_text(text);
}

// Appends one run: the header lines first when the file is new or empty, a line end first when the last line has none (an edit by hand), then the line, in
// one write, flushed and closed. Never rewrites or truncates the file. False (and `error` says why) when it could not be written.
inline bool append(const std::filesystem::path& path, const Record& record, std::string* error = nullptr) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    const std::uintmax_t size = exists ? std::filesystem::file_size(path, ec) : 0;
    std::string text;
    if (!exists || ec || size == 0) {
        text = header_text();
    } else {
        std::ifstream in(path, std::ios::binary);
        const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!old.empty() && old.back() != '\n') {
            text = "\n";
        }
        if (is_speed_run(record.mode) || record.mode == Mode::Competition) {
            // the file's columns as a reader will take them at its end: a column line from before the session columns would hide this run's time or total
            Columns columns;
            bool named = false;
            std::size_t start = 0;
            while (start < old.size()) {
                std::size_t end = old.find('\n', start);
                if (end == std::string::npos) {
                    end = old.size();
                }
                std::string_view line(old.data() + start, end - start);
                start = end + 1;
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                if (!line.empty() && line.front() != '#') {
                    if (const auto found = Columns::from_line(line)) {
                        columns = *found;
                        named = true;
                    }
                }
            }
            if (named && !columns.has_session_columns()) {
                const std::string header = header_text();
                text += header.substr(header.find('\n') + 1);   // the column line again (not the magic line)
            }
        }
    }
    text += format_line(record) + "\n";
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        if (error != nullptr) {
            *error = "cannot open the file for appending";
        }
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        if (error != nullptr) {
            *error = "the write failed";
        }
        return false;
    }
    return true;
}

// The boards. The score board ranks career runs and single sessions by trick score; the speed run boards rank speed runs by time, the fastest first; the
// competition board ranks competitions by the judges' total, then the trick score.
enum class Board { Scores, SpeedRuns, SpeedRunsCash, Competitions };

inline bool on_board(const Record& r, Board board) {
    switch (board) {
    case Board::SpeedRuns: return r.mode == Mode::SpeedRun;
    case Board::SpeedRunsCash: return r.mode == Mode::SpeedRunCash;
    case Board::Competitions: return r.mode == Mode::Competition;
    default: return on_score_board(r);
    }
}

// a ranks above b (ties: the earlier run first)
inline bool better(const Record& a, const Record& b) { return a.score != b.score ? a.score > b.score : a.order < b.order; }
inline bool better_on(Board board, const Record& a, const Record& b) {
    if ((board == Board::SpeedRuns || board == Board::SpeedRunsCash) && a.time_ms != b.time_ms) {
        return a.time_ms < b.time_ms;
    }
    if (board == Board::Competitions && a.judged != b.judged) {
        return a.judged > b.judged;
    }
    return better(a, b);
}

// The best `count` runs of one level on a board, best first
inline std::vector<Record> best(const std::vector<Record>& records, int level, Board board, std::size_t count) {
    std::vector<Record> out;
    for (const Record& r : records) {
        if (r.level == level && on_board(r, board)) {
            out.push_back(r);
        }
    }
    std::stable_sort(out.begin(), out.end(), [board](const Record& a, const Record& b) { return better_on(board, a, b); });
    if (out.size() > count) {
        out.resize(count);
    }
    return out;
}

// The best run of every level that has one on a board, by level number
inline std::vector<Record> best_per_level(const std::vector<Record>& records, Board board) {
    std::vector<Record> out;
    for (int level = 0; level < kLevelCount; ++level) {
        const std::vector<Record> top = best(records, level, board, 1);
        if (!top.empty()) {
            out.push_back(top.front());
        }
    }
    return out;
}

// The new high scores: the runs from `first_order` on (the session that just ended) that are now the best of their level on their board, Scores first, then
// the speed runs, the 100% speed runs and the competitions. A run that only ties the best does not count (ties go to the earlier run).
inline std::vector<Record> new_bests(const std::vector<Record>& records, std::size_t first_order) {
    std::vector<Record> out;
    for (const Board board : {Board::Scores, Board::SpeedRuns, Board::SpeedRunsCash, Board::Competitions}) {
        for (const Record& top : best_per_level(records, board)) {
            if (top.order >= first_order) {
                out.push_back(top);
            }
        }
    }
    return out;
}

// The board a run belongs to
inline Board board_of(const Record& r) {
    switch (r.mode) {
    case Mode::SpeedRun: return Board::SpeedRuns;
    case Mode::SpeedRunCash: return Board::SpeedRunsCash;
    case Mode::Competition: return Board::Competitions;
    default: return Board::Scores;
    }
}

// Every run from `first_order` on (the session that just ended), in the order they were written: records or not
inline std::vector<Record> session_runs(const std::vector<Record>& records, std::size_t first_order) {
    std::vector<Record> out;
    for (const Record& r : records) {
        if (r.order >= first_order) {
            out.push_back(r);
        }
    }
    return out;
}

// Where a run stands among its level's runs on its own board: rank 1 is the best, `of` how many runs the level has there (ties go to the earlier run)
struct Placing {
    int rank = 0;
    int of = 0;
};
inline Placing placing(const std::vector<Record>& records, const Record& run) {
    const Board board = board_of(run);
    Placing out;
    out.rank = 1;
    for (const Record& r : records) {
        if (r.level != run.level || !on_board(r, board)) {
            continue;
        }
        ++out.of;
        if (r.order != run.order && better_on(board, r, run)) {
            ++out.rank;
        }
    }
    return out;
}

// The last `count` runs of a board, the newest first
inline std::vector<Record> recent(const std::vector<Record>& records, Board board, std::size_t count) {
    std::vector<Record> out;
    for (auto it = records.rbegin(); it != records.rend() && out.size() < count; ++it) {
        if (on_board(*it, board)) {
            out.push_back(*it);
        }
    }
    return out;
}

// The score board's views of one mode, or (`mode` empty) of every mode on it
inline std::vector<Record> best(const std::vector<Record>& records, int level, std::optional<Mode> mode, std::size_t count) {
    std::vector<Record> out;
    for (const Record& r : best(records, level, Board::Scores, records.size())) {
        if (!mode || r.mode == *mode) {
            out.push_back(r);
        }
    }
    if (out.size() > count) {
        out.resize(count);
    }
    return out;
}

inline std::vector<Record> best_per_level(const std::vector<Record>& records, std::optional<Mode> mode) {
    std::vector<Record> out;
    for (int level = 0; level < kLevelCount; ++level) {
        const std::vector<Record> top = best(records, level, mode, 1);
        if (!top.empty()) {
            out.push_back(top.front());
        }
    }
    return out;
}

inline std::vector<Record> recent(const std::vector<Record>& records, std::optional<Mode> mode, std::size_t count) {
    std::vector<Record> out;
    for (auto it = records.rbegin(); it != records.rend() && out.size() < count; ++it) {
        if (on_score_board(*it) && (!mode || it->mode == *mode)) {
            out.push_back(*it);
        }
    }
    return out;
}

// "4:05.32": a speed run's time in minutes, seconds and hundredths
inline std::string run_time(std::int64_t ms) {
    if (ms < 0) {
        return "-";
    }
    char text[32];
    const std::int64_t hundredths = ms / 10;
    std::snprintf(text, sizeof text, "%lld:%02lld.%02lld", static_cast<long long>(hundredths / 6000), static_cast<long long>(hundredths / 100 % 60),
                  static_cast<long long>(hundredths % 100));
    return text;
}

// "178.6": a judged total in tenths, as the game's standings show a heat
inline std::string judged_text(int tenths) {
    if (tenths < 0) {
        return "-";
    }
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

// "48,210"
inline std::string thousands(std::int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
        digits.insert(static_cast<std::size_t>(at), ",");
    }
    return (value < 0 ? "-" : "") + digits;
}

}  // namespace opentony::scores
