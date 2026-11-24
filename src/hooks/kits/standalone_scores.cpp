// The score file (docs/LAUNCHER.md, "Scores"): with OT_SCORES_FILE set, every finished career level run and single session is appended to it the moment the run ends,
// with no save. The rules are score_recorder.hpp's; this file reads the game's words for it and sets the two hooks of runtime/score_hooks.hpp. Unset, nothing is
// installed: the hooks stay null and the game runs retail's code with nothing more read.
#include "standalone_scores.hpp"

#include "score_recorder.hpp"

#include "../../runtime/host_options.hpp"
#include "../../runtime/score_hooks.hpp"
#include "guest_arena.hpp"
#include "standalone_options.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace opentony::kitdll {

namespace {

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kStatus = 0x00561bfc;          // GStatus, the in-level front end's screen
constexpr std::uint32_t kPlace = 0x0052bf34;           // the place the last run went into, -1 none (the entry screen edits that row)
constexpr std::uint32_t kCheats = 0x0056a374;          // 17 entries of 4 bytes
constexpr std::uint32_t kPlayers = 0x00533f3c;         // the number of players
constexpr std::uint32_t kSecondPlayer = 0x0056a85c;    // the second player's object, 0 without one
constexpr std::uint32_t kInitials = 0x0056a408;        // the letters a new table row is given (H1 keeps the launcher's there)
constexpr std::uint32_t kHighScores = 0x00569ebc;      // 14 tables of 8 rows of 8 bytes: three letters, the character, the score

constexpr std::uint32_t kStatusEntry = 0x2e;           // the high-score entry: the game edits a row's letters

void log_line(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

bool g_checked = false;
std::unique_ptr<scores::Recorder> g_recorder;
bool g_in_game = false;                                // OT_SCORES_IN_GAME: the game's own tables show the file's runs
std::vector<scores::Entry> g_file_runs[opentony::scores::kLevelCount];   // each table's best file runs (score_recorder.hpp keep_run)

void keep_record(const opentony::scores::Record& record) {
    if (!opentony::scores::on_score_board(record)) {
        return;   // a speed run's or a competition's trick score is not a score run
    }
    if (const auto entry = scores::entry_of(record); entry && record.level >= 0 && record.level < opentony::scores::kLevelCount) {
        scores::keep_run(g_file_runs[record.level], *entry);
    }
}

// The 14 tables merged with the file's runs (score_recorder.hpp merge_table); only the bytes that differ are written. Never while a row is being edited or a run
// is held for its entry: the place word is set from the run's insert to the entry's accept. Retail's own writes (a new career's reset, a save's load) are seen
// here as tables that lack the file's runs, and they get them back at the next poll.
void fill_tables(std::uint32_t status, std::int32_t place) {
    if (!g_in_game || place != -1 || status == kStatusEntry || (g_recorder && g_recorder->has_pending())) {
        return;
    }
    int changed = 0;
    for (std::uint32_t t = 0; t < static_cast<std::uint32_t>(opentony::scores::kLevelCount); ++t) {
        if (g_file_runs[t].empty()) {
            continue;
        }
        scores::Table table;
        for (std::uint32_t k = 0; k < 8; ++k) {
            const std::uint32_t row = kHighScores + (t * 8 + k) * 8;
            for (std::uint32_t c = 0; c < 3; ++c) {
                table[k].letters[c] = static_cast<char>(retail<std::uint8_t>(row + c));
            }
            table[k].skater = retail<std::uint8_t>(row + 3);
            table[k].score = retail<std::uint32_t>(row + 4);
        }
        bool set_up = false;   // every table has scores from the boot's reset on; before it the image is zeros, and the reset would overwrite what was written
        for (const scores::Entry& e : table) {
            set_up = set_up || e.score != 0;
        }
        if (!set_up) {
            continue;
        }
        const scores::Table merged = scores::merge_table(table, g_file_runs[t]);
        for (std::uint32_t k = 0; k < 8; ++k) {
            if (merged[k] == table[k]) {
                continue;
            }
            const std::uint32_t row = kHighScores + (t * 8 + k) * 8;
            for (std::uint32_t c = 0; c < 3; ++c) {
                retail<std::uint8_t>(row + c) = static_cast<std::uint8_t>(merged[k].letters[c]);
            }
            retail<std::uint8_t>(row + 3) = merged[k].skater;
            retail<std::uint32_t>(row + 4) = merged[k].score;
            ++changed;
        }
    }
    if (changed != 0) {
        log_line("scores: " + std::to_string(changed) + " row" + (changed == 1 ? "" : "s") + " of the game's tables filled from the score file");
    }
}

// OT_SCORES_FILE as the wide string Windows has it (the install path may hold letters outside the code page)
std::wstring environment_wide(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), length);
    value.resize(got < length ? got : 0);
    return value;
}

bool cheat_on_now() {
    std::uint8_t table[4 * scores::kCheatCount];
    for (std::uint32_t k = 0; k < sizeof table; ++k) {
        table[k] = retail<std::uint8_t>(kCheats + k);
    }
    return scores::cheat_on(table);
}

std::string letters_at(std::uint32_t address) {
    std::string text;
    for (std::uint32_t k = 0; k < 3; ++k) {
        const char c = static_cast<char>(retail<std::uint8_t>(address + k));
        text += c == 0 ? ' ' : c;
    }
    return text;
}

void on_run_end(const runtime::scorehooks::RunEnd& run) {
    if (!g_recorder) {
        return;
    }
    scores::RunEnd e;
    e.mode = run.mode;
    e.level = run.level;
    e.table = run.table;
    e.place = run.place;
    e.score = run.score;
    e.skater = static_cast<std::int32_t>(run.skater);
    e.players = retail<std::uint32_t>(kPlayers) == 1 && retail<std::uint32_t>(kSecondPlayer) == 0 ? 1 : 2;
    e.cheat_on = cheat_on_now();
    e.initials = letters_at(kInitials);
    g_recorder->run_end(e, std::time(nullptr));
}

void on_entry_accepted(std::uint32_t table, std::int32_t place) {
    if (!g_recorder || place < 0 || place > 7 || table > 13) {
        return;
    }
    g_recorder->accepted(table, place, letters_at(kHighScores + (static_cast<std::uint32_t>(place) + table * 8) * 8));
}

void install() {
    g_checked = true;
    const std::wstring file = environment_wide(L"OT_SCORES_FILE");
    if (file.empty()) {
        return;
    }
    g_recorder = std::make_unique<scores::Recorder>(std::filesystem::path(file), log_line, keep_record);
    g_recorder->set_speed_run(runtime::host_options().speed_run || game_options().quick_mode == QuickMode::SpeedRun);
    runtime::scorehooks::hooks().run_end = on_run_end;
    runtime::scorehooks::hooks().entry_accepted = on_entry_accepted;
    std::atexit([] { scores_shutdown(); });   // ExitProcess runs the DLL's exit list: a run still held for its initials is written
    log_line("scores: recording finished runs (OT_SCORES_FILE)");
    const char* in_game = std::getenv("OT_SCORES_IN_GAME");
    g_in_game = in_game != nullptr && *in_game != '\0' && std::strcmp(in_game, "0") != 0;
    if (g_in_game) {
        const opentony::scores::File runs = opentony::scores::load(std::filesystem::path(file));   // once: this session's runs are added as they are written
        for (const opentony::scores::Record& record : runs.records) {
            keep_record(record);
        }
        log_line("scores: the game's tables show the file's runs (" + std::to_string(runs.records.size()) + " read, " + std::to_string(runs.bad_lines) +
                 " lines left out)");
    }
}

}  // namespace

void scores_each_poll() {
    if (!g_checked) {
        install();
    }
    if (g_recorder) {
        const std::uint32_t status = retail<std::uint32_t>(kStatus);
        const std::int32_t place = static_cast<std::int32_t>(retail<std::uint32_t>(kPlace));
        g_recorder->poll(status, place, cheat_on_now());
        fill_tables(status, place);
    }
}

void scores_shutdown() {
    if (g_recorder) {
        g_recorder->shutdown();
    }
}

bool scores_cheat_on() { return cheat_on_now(); }

void scores_session(const SessionResult& result) {
    if (!g_checked) {
        install();
    }
    if (!g_recorder) {
        return;
    }
    const bool speed_run = result.time_ms >= 0;
    const opentony::scores::Mode mode = !speed_run ? opentony::scores::Mode::Competition : result.cash ? opentony::scores::Mode::SpeedRunCash : opentony::scores::Mode::SpeedRun;
    opentony::scores::Record record = opentony::scores::make_record(mode, result.level, result.skater, letters_at(kInitials), result.score, std::time(nullptr));
    record.time_ms = result.time_ms;
    record.judged = result.judged;
    record.medal = result.medal;
    g_recorder->session(record, result.cheat || cheat_on_now());
}

}  // namespace opentony::kitdll
