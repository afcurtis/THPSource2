// The score file's two hooks (docs: THPS2 post_phase_5/highscores.md, H4 and H5; docs/LAUNCHER.md, "Scores"): host-side function pointers, null by default, that two
// ported functions call when they are set. Nothing in guest memory. With both null (the hooked build, every gate, a standalone without OT_SCORES_FILE) the functions
// are retail's, call for call: they read nothing more.
//
//   run_end         FrontScreens::switch_on_end_level (0x0044e0f0, front_states.cpp): a run in game mode 1 (a career level that is not a contest) or 3 (a single
//                   session) has finished and its score has gone through the level's table of eight. Called once, after the insert.
//   entry_accepted  FrontScreens::update, status 0x2e (front_update_late.cpp): the high-score entry's accept, before the place (0x0052bf34) is reset. The row's letters
//                   are the ones the player chose.
//
// The standalone sets them (hooks/kits/standalone_scores.cpp).
#pragma once

#include <cstdint>

namespace opentony::runtime::scorehooks {

struct RunEnd {
    std::uint32_t mode = 0;     // 0x00533f38: 1 career, 3 single session
    std::int32_t level = 0;     // 0x0056a898
    std::uint32_t table = 0;    // the score table: the level, or 13 for a park editor level (0x005614c4 nonzero)
    std::int32_t place = -1;    // 0..7, the row the run went into; -1 it placed nowhere (0x0052bf34)
    std::uint32_t score = 0;    // player +0x16c, the number the table was given
    std::uint32_t skater = 0;   // player +0x2cc0, the character number the row's byte 3 was given
};

struct Hooks {
    void (*run_end)(const RunEnd& run) = nullptr;
    void (*entry_accepted)(std::uint32_t table, std::int32_t place) = nullptr;
};

inline Hooks& hooks() {
    static Hooks value;
    return value;
}

}  // namespace opentony::runtime::scorehooks
