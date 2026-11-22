// The score file (standalone_scores.cpp; docs/LAUNCHER.md, "Scores"). Nothing happens without OT_SCORES_FILE.
#pragma once

#include <cstdint>
#include <string>

namespace opentony::kitdll {

void scores_each_poll();   // every input poll (from options_each_poll): installs the recorder at the first, then feeds it the game's status, place and cheats
void scores_shutdown();    // the program ends: a run held for its initials entry is written with the letters it started from

// A finished Goals Speed Run (time_ms set) or Competition Session (judged set), from standalone_challenge.cpp: one line, the remembered initials
struct SessionResult {
    int level = 0;
    int skater = -1;
    std::int64_t score = 0;     // the speed run's trick score at its last goal; the competition's better counted heat
    std::int64_t time_ms = -1;
    int judged = -1;            // tenths
    std::string medal;          // gold, silver, bronze, none
    bool cash = false;          // a speed run of all ten goals, the 100% one (every cash icon) too: its own board
    bool cheat = false;         // a cheat was on at some poll of it
};
void scores_session(const SessionResult& result);
bool scores_cheat_on();     // a cheat that keeps a run off the board is on now

}  // namespace opentony::kitdll
