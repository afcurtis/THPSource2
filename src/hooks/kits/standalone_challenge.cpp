// The quick start's two career sessions (standalone_options.hpp; docs/LAUNCHER.md, "Quick start"). Both are the game's own career mode (1) on the level the launcher
// chose, kept from the save file like every quick start (host_options.hpp quick_play), with the level select greyed. Nothing here runs in another mode.
//
// Goals Speed Run (OT_QUICK_MODE=speedrun). Each run starts with none of the level's goals and none of its cash: the career record's goal word and the two cash
// words of the level are cleared just before the run's objects read them (Game_PrepareVideoMasksWrapper, 0x00469900, which trg_parse_nodes calls as it builds the
// level; the letters, the tape and the pickups show from what it finds, the cash icons ask Career_GotMoney). The clock counts up: retail has the mode (the byte
// 0x029da3a4: SkaterKit::handle_timer and Panel::display_holding then take the skater's +0x2cc8 as the time itself, not as the deadline on the animation clock),
// and every poll writes the time so far there, the animation clock less its value at the run's first poll (60 a second of game time; it stands still in the pause
// menu and the level's menus). The score goals are the career's own (Panel::display checks them as the score goes up), the 100% goal comes with the last cash icon
// as it always does. The run's goals are every goal of the level but its 100% one ("100% Goals and Cash", type 7), or with OT_SPEED_RUN_CASH=1 all ten (so every
// cash icon too). When they are got the clock stops, the time comes up as a goal message would ("ALL GOALS 4:05.32", "100% 6:12.08"), the line is written to the
// score file (speedrun or speedrun_cash), and 2.5 seconds later the clock is let run out (the word 0, the count-up byte cleared) so the game ends the run its own way.
//
// Competition Session (OT_QUICK_MODE=competition). The level is made a contest for the session (the level table's byte +0x48, which the clock's 60-second heats,
// the competition's set-up and its screens all test), judged as The Bullring is (host_options.hpp judge_level: its score thresholds and deduction rule), and on a
// level with no opening shot of its own the shot holds behind the skater (hold_intro). The level's goals and cash are hidden by giving the record all ten goals and
// every cash icon of the level before each heat's objects are built: the letters, the tape, the pickups and the cash do not appear, and no goal message can come
// (the score goals are not checked on a contest, the count and gap goals test the goal first). The medal bits of the level are cleared as a competition starts, so
// the medal it gives is the one read. Each heat's trick score is read when the judges' screen comes up (status 0x13 / 0x14); after the third heat the end screen
// (status 0x17) has the standings: the player's total is the two best heats (as GoalScreens' standings add them), and the line is written once the medal is given,
// or when the screen is left without one.
#include "standalone_options.hpp"
#include "standalone_scores.hpp"

#include "kit_bridge.hpp"

#include "../../runtime/host_options.hpp"
#include "guest_arena.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace opentony::kitdll {

namespace {

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kPrepareVideoMasks = 0x00469900;   // Game_PrepareVideoMasksWrapper: a run's objects are about to read the career's goals and cash
constexpr std::uint32_t kGameMode = 0x00533f38;            // LevelEventGameModeSelector: 1 the career
constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kPlayers = 0x0056a858;             // the first player's skater object
constexpr std::uint32_t kPlayerScore = 0x16c;
constexpr std::uint32_t kPlayerCharacter = 0x2cc0;
constexpr std::uint32_t kPlayerTime = 0x2cc8;              // the run's deadline on the animation clock; with the count-up byte, the time itself
constexpr std::uint32_t kAnimationClock = 0x005685f4;      // 60 a second of game time
constexpr std::uint32_t kTimeCountsUp = 0x029da3a4;        // byte (skater_kit.hpp kTimeCountsUp)
constexpr std::uint32_t kStatus = 0x00561bfc;              // GStatus: 0 playing, 1 the pause menu, ...
constexpr std::uint32_t kSkaterSlot = 0x0056c584;          // the first player's slot: the character
constexpr std::uint32_t kCareerRecords = 0x00568a6c;       // 0x104 bytes a character (career.hpp)
constexpr std::uint32_t kRecordStride = 0x104;
constexpr std::uint32_t kRecLevelMasks = 0x0c;             // u16 a level: goals 0-9, 0x400 gold, 0x800 silver, 0x1000 bronze
constexpr std::uint32_t kRecMoneyMasks = 0x44;             // two u32 a level: the cash icons got
constexpr std::uint16_t kAllGoals = 0x3ff;
constexpr std::uint16_t kGold = 0x400, kSilver = 0x800, kBronze = 0x1000;
constexpr std::uint32_t kLevelTable = 0x0053c1f8;          // 0x1ac bytes a level
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kLevelContest = 0x48;              // byte: a competition level
constexpr std::uint32_t kLevelGoals = 0xb8;                // ten rows of 0x18 bytes, the type first (career.hpp kLvGoals)
constexpr std::uint32_t kGoalStride = 0x18;
constexpr std::int32_t kGoalTypeAll = 7;                   // "100% Goals and Cash": given with the last cash icon once the other nine are got
constexpr std::uint32_t kLevelIntroPoint = 0x9c;           // three dwords: where the competition's opening shot starts (CameraKit::point_mode, record + 0x54)
constexpr std::uint32_t kLevelIntroTurns = 0xa8;           // three halves: its turns about z, x and y
constexpr int kBullring = 7;
constexpr std::uint32_t kCompetition = 0x0055fe28;         // CCompetition (competition.hpp)
constexpr std::uint32_t kCompHeat = kCompetition + 0xf4;   // byte: the heat, 0..2 (3 after the last)
constexpr std::uint32_t kCompParticipants = 8;
constexpr std::uint32_t kCompStride = 0x14;
constexpr std::uint32_t kCompHuman = 0x08;                 // + participant * 0x14: byte, a player's seat
constexpr std::uint32_t kCompHeatScores = 0x0e;            // + participant * 0x14: three u16, the judges' score of each heat in tenths
constexpr std::uint32_t kStatusJudgesA = 0x13;             // the heat is over: the judges' screen (FrontScreens::update)
constexpr std::uint32_t kStatusJudgesB = 0x14;
constexpr std::uint32_t kStatusCompetitionEnd = 0x17;      // after the third heat: the standings, the medal
constexpr std::uint32_t kMessAddMessage = 0x004736f0;      // Mess_AddMessage(text, style, 1, 0, 0): a career goal's message (Career::message)
constexpr std::uint32_t kMessageStyle = 0x00534f68;
constexpr std::int32_t kEndDelayTicks = 150;               // the time stays on screen this long (2.5 s of game time) before the run is ended

struct SpeedRun {
    bool armed = false;         // a run's objects were built: the clock starts at its next poll in play
    bool running = false;
    std::int32_t start = 0;     // the animation clock at the run's first poll
    std::int32_t elapsed = 0;
    bool finished = false;      // every goal got: the clock has stopped
    std::int32_t end_at = 0;    // the animation clock at which the run is ended
    bool ended = false;
    bool cheat = false;
    bool held = false;          // the goals screen is up: the clock is held at 0
};

struct Competition {
    std::int64_t heat_score[3] = {-1, -1, -1};   // each heat's trick score
    bool ending = false;        // the end screen of the third heat is up: the result waits for the medal
    int judged = -1;
    std::int64_t best_heat = 0;
    bool written = false;       // this competition's line is written
    bool cheat = false;
};

struct State {
    bool set_up = false;
    QuickMode mode = QuickMode::FreeSkate;
    int level = -1;
    std::uint32_t last_status = 0xffffffffu;
    SpeedRun run;
    std::uint16_t target = kAllGoals;   // the speed run's goals: every goal of the level but its 100% one (type 7), or with the cash all ten
    bool cash = false;
    Competition comp;
    std::uint32_t text = 0;     // the time's message text, in the guest heap
} g;

bool active() { return g.set_up && (g.mode == QuickMode::SpeedRun || g.mode == QuickMode::Competition); }

std::uint32_t record_of(std::uint32_t character) { return kCareerRecords + (character < 20 ? character : 0) * kRecordStride; }
std::uint32_t level_mask(std::uint32_t record) { return record + kRecLevelMasks + static_cast<std::uint32_t>(g.level) * 2; }
std::uint32_t money_masks(std::uint32_t record) { return record + kRecMoneyMasks + static_cast<std::uint32_t>(g.level) * 8; }
std::uint32_t level_row(int level) { return kLevelTable + static_cast<std::uint32_t>(level) * kLevelStride; }

void say(const std::string& line) {
    std::printf("challenge: %s\n", line.c_str());
    std::fflush(stdout);
}

std::string clock_text(std::int32_t ticks) {
    const std::int64_t hundredths = static_cast<std::int64_t>(ticks) * 100 / 60;
    char text[32];
    std::snprintf(text, sizeof text, "%lld:%02lld.%02lld", static_cast<long long>(hundredths / 6000), static_cast<long long>(hundredths / 100 % 60),
                  static_cast<long long>(hundredths % 100));
    return text;
}

// The time as the career's goal messages come up (Career::message): the big pop in the middle
void show_time(std::int32_t ticks) {
    if (g.text == 0) {
        g.text = arena::heap_allocate(32);
        if (g.text == 0) {
            return;
        }
    }
    const std::string text = (g.cash ? "100% " : "ALL GOALS ") + clock_text(ticks);
    volatile char* at = arena::host<volatile char>(g.text);
    for (std::size_t k = 0; k < text.size() && k < 31; ++k) {
        at[k] = text[k];
    }
    at[std::min<std::size_t>(text.size(), 31)] = 0;
    call_native_or_seam(kMessAddMessage, {g.text, kMessageStyle, 1, 0, 0});
}

// The judges' total of three heats: the two best, as the standings add them (goals_displays.cpp top_two)
int top_two(int a, int b, int c) {
    const int low = std::min({a, b, c});
    return a + b + c - low;
}

void speed_run_poll(std::uint32_t player, std::uint32_t status) {
    SpeedRun& r = g.run;
    const std::int32_t clock = retail<std::int32_t>(kAnimationClock);
    if (r.armed && status == 0) {
        r = SpeedRun{};
        r.running = true;
        r.start = clock;
        say("speed run: the clock starts (level " + std::to_string(g.level) + ")");
    }
    if (!r.running || r.ended) {
        return;
    }
    // the goals screen comes up after the run's first poll in play and waits for the player's Play: the clock stays at 0 until it is gone (a Retry has no screen)
    if (!r.finished && runtime::host_options().goals_screen_up) {
        if (!r.held) {
            r.held = true;
            say("speed run: the goals screen is up, the clock waits for Play");
        }
        r.start = clock;
    } else if (r.held) {
        r.held = false;
        r.start = clock;
        say("speed run: Play: the clock starts");
    }
    if ((status == 0 || status == 1) && scores_cheat_on()) {
        r.cheat = true;
    }
    if (!r.finished) {
        r.elapsed = std::max(0, clock - r.start);
        const std::uint32_t mask_at = level_mask(record_of(retail<std::uint32_t>(player + kPlayerCharacter)));
        // OT_SPEED_RUN_TEST=<ticks> (a test switch, docs/HOOKS.md): every goal is given at that time, where a headless run cannot get them
        static const long test_ticks = [] {
            const char* text = std::getenv("OT_SPEED_RUN_TEST");
            return text != nullptr && *text != '\0' ? std::strtol(text, nullptr, 10) : -1L;
        }();
        if (test_ticks >= 0 && r.elapsed >= test_ticks) {
            retail<std::uint16_t>(mask_at) = static_cast<std::uint16_t>(retail<std::uint16_t>(mask_at) | g.target);
        }
        const std::uint16_t mask = retail<std::uint16_t>(mask_at);
        if ((mask & g.target) == g.target) {
            r.finished = true;
            r.end_at = clock + kEndDelayTicks;
            show_time(r.elapsed);
            say(std::string("speed run: ") + (g.cash ? "every goal and the cash" : "every goal") + " in " + clock_text(r.elapsed) + " (" +
                std::to_string(r.elapsed) + " ticks)");
            SessionResult result;
            result.level = g.level;
            result.skater = static_cast<int>(retail<std::uint32_t>(player + kPlayerCharacter));
            result.score = static_cast<std::int64_t>(retail<std::uint32_t>(player + kPlayerScore));
            result.time_ms = static_cast<std::int64_t>(r.elapsed) * 1000 / 60;
            result.cash = g.cash;
            result.cheat = r.cheat;
            scores_session(result);
        }
    }
    if (r.finished && clock >= r.end_at) {
        // the clock runs out: the game's own end of the run
        r.ended = true;
        retail<std::uint8_t>(kTimeCountsUp) = 0;
        retail<std::uint32_t>(player + kPlayerTime) = 0;
        say("speed run: the run ends");
        return;
    }
    // (one more than the time: the trick code takes a word of 0 for a run that is over)
    const std::uint32_t shown = static_cast<std::uint32_t>(r.elapsed) + 1;
    if (retail<std::uint8_t>(kTimeCountsUp) != 1) {
        retail<std::uint8_t>(kTimeCountsUp) = 1;
    }
    if (retail<std::uint32_t>(player + kPlayerTime) != shown) {
        retail<std::uint32_t>(player + kPlayerTime) = shown;
    }
}

void competition_write(const char* medal) {
    Competition& c = g.comp;
    c.ending = false;
    c.written = true;
    const std::uint32_t player = retail<std::uint32_t>(kPlayers);
    SessionResult result;
    result.level = g.level;
    result.skater = player != 0 ? static_cast<int>(retail<std::uint32_t>(player + kPlayerCharacter)) : static_cast<int>(retail<std::uint32_t>(kSkaterSlot));
    result.score = c.best_heat;
    result.judged = c.judged;
    result.medal = medal;
    result.cheat = c.cheat;
    say("competition: total " + std::to_string(c.judged / 10) + "." + std::to_string(c.judged % 10) + ", " + medal + ", best counted heat " + std::to_string(c.best_heat));
    scores_session(result);
}

const char* medal_of(std::uint16_t mask) { return (mask & kGold) != 0 ? "gold" : (mask & kSilver) != 0 ? "silver" : (mask & kBronze) != 0 ? "bronze" : nullptr; }

void competition_poll(std::uint32_t player, std::uint32_t status) {
    Competition& c = g.comp;
    const std::uint32_t heat = retail<std::uint8_t>(kCompHeat);
    if ((status == 0 || status == 1) && scores_cheat_on()) {
        c.cheat = true;
    }
    // OT_COMPETITION_TEST=<score> (a test switch, docs/HOOKS.md): the trick score is held there while a heat is played, where a headless run scores nothing
    static const long test_score = [] {
        const char* text = std::getenv("OT_COMPETITION_TEST");
        return text != nullptr && *text != '\0' ? std::strtol(text, nullptr, 10) : -1L;
    }();
    if (test_score >= 0 && status == 0) {
        retail<std::uint32_t>(player + kPlayerScore) = static_cast<std::uint32_t>(test_score);
    }
    const auto judging = [](std::uint32_t s) { return s == kStatusJudgesA || s == kStatusJudgesB; };
    if (judging(status) && !judging(g.last_status) && heat < 3) {
        c.heat_score[heat] = static_cast<std::int64_t>(retail<std::uint32_t>(player + kPlayerScore));
        say("competition: heat " + std::to_string(heat + 1) + " trick score " + std::to_string(c.heat_score[heat]));
    }
    if (status == kStatusCompetitionEnd && heat >= 3 && !c.written && !c.ending) {
        int seat = -1;
        for (std::uint32_t e = 0; e < kCompParticipants && seat < 0; ++e) {
            if (retail<std::uint8_t>(kCompetition + e * kCompStride + kCompHuman) != 0) {
                seat = static_cast<int>(e);
            }
        }
        if (seat < 0) {
            c.written = true;
            say("competition: no player seat in the standings; not recorded");
            return;
        }
        int judged[3];
        for (std::uint32_t h = 0; h < 3; ++h) {
            judged[h] = retail<std::uint16_t>(kCompetition + static_cast<std::uint32_t>(seat) * kCompStride + kCompHeatScores + h * 2);
        }
        c.judged = top_two(judged[0], judged[1], judged[2]);
        // the two heats that count (the lowest judged one left out; of equal ones the later), and the better trick score of them
        int dropped = 2;
        for (int h = 1; h >= 0; --h) {
            if (judged[h] < judged[dropped]) {
                dropped = h;
            }
        }
        c.best_heat = 0;
        for (int h = 0; h < 3; ++h) {
            if (h != dropped) {
                c.best_heat = std::max(c.best_heat, c.heat_score[h]);
            }
        }
        c.ending = true;
    }
    if (c.ending) {
        const char* medal = medal_of(retail<std::uint16_t>(level_mask(record_of(retail<std::uint32_t>(player + kPlayerCharacter)))));
        if (medal != nullptr) {
            competition_write(medal);
        } else if (status != kStatusCompetitionEnd) {
            competition_write("none");
        }
    }
}

}  // namespace

bool speed_run_level(int level) { return level == 0 || level == 1 || level == 3 || level == 4 || level == 6; }

void challenge_prepare_level() {
    const GameOptions& o = game_options();
    g = State{};
    g.mode = o.quick_mode;
    g.level = o.quick_level;
    g.set_up = true;
    if (!active() || g.level < 0 || g.level > 12) {
        g.set_up = false;
        return;
    }
    runtime::HostOptions& host = runtime::host_options();
    if (g.mode == QuickMode::SpeedRun) {
        host.speed_run = true;
        g.cash = o.speed_run_cash;
        g.target = 0;
        for (std::uint32_t goal = 0; goal < 10; ++goal) {
            const std::int32_t type = retail<std::int32_t>(level_row(g.level) + kLevelGoals + goal * kGoalStride);
            if (g.cash || type != kGoalTypeAll) {
                g.target = static_cast<std::uint16_t>(g.target | (1u << goal));
            }
        }
        say("speed run on level " + std::to_string(g.level) + (g.cash ? ": every goal and the cash" : ": every goal but the 100%") + " (goal mask " +
            std::to_string(g.target) + ")");
        return;
    }
    const std::uint32_t row = level_row(g.level);
    const bool contest = retail<std::uint8_t>(row + kLevelContest) != 0;
    retail<std::uint8_t>(row + kLevelContest) = 1;
    host.judge_level = kBullring;
    bool shot = false;
    for (std::uint32_t k = 0; k < 3; ++k) {
        shot = shot || retail<std::int32_t>(row + kLevelIntroPoint + 4 * k) != 0 || retail<std::int16_t>(row + kLevelIntroTurns + 2 * k) != 0;
    }
    host.hold_intro = !shot;
    say("competition session on level " + std::to_string(g.level) + (contest ? " (a contest level)" : " (made a contest)") + ", judged as The Bullring" +
        (shot ? "" : ", the opening shot held"));
}

bool challenge_watches(std::uint32_t address) { return address == kPrepareVideoMasks && g.set_up; }

void challenge_before_call(std::uint32_t) {
    if (!active() || retail<std::uint32_t>(kGameMode) != 1 || static_cast<int>(retail<std::uint32_t>(kCurrentLevel)) != g.level) {
        return;
    }
    const std::uint32_t record = record_of(retail<std::uint32_t>(kSkaterSlot));
    if (g.mode == QuickMode::SpeedRun) {
        retail<std::uint16_t>(level_mask(record)) = 0;
        retail<std::uint32_t>(money_masks(record)) = 0;
        retail<std::uint32_t>(money_masks(record) + 4) = 0;
        g.run.armed = true;
        g.run.running = false;
        say("speed run: a run starts with no goals and no cash");
        return;
    }
    const std::uint32_t heat = retail<std::uint8_t>(kCompHeat);
    std::uint16_t mask = static_cast<std::uint16_t>(retail<std::uint16_t>(level_mask(record)) | kAllGoals);
    if (heat == 0) {
        // the first heat (its objects are built twice, the second time after its judges): the medal the competition gives is the one read at its end; a new
        // competition (the last one written, or this one past its first heat) starts its record again
        mask = static_cast<std::uint16_t>(mask & ~(kGold | kSilver | kBronze));
        if (g.comp.written || g.comp.heat_score[1] >= 0 || g.comp.heat_score[2] >= 0) {
            g.comp = Competition{};
        }
    }
    retail<std::uint16_t>(level_mask(record)) = mask;
    retail<std::uint32_t>(money_masks(record)) = 0xffffffffu;
    retail<std::uint32_t>(money_masks(record) + 4) = 0xffffffffu;
    say("competition: heat " + std::to_string(heat + 1) + " starts with the goals and cash hidden");
}

void challenge_each_poll() {
    if (!active()) {
        return;
    }
    const std::uint32_t status = retail<std::uint32_t>(kStatus);
    const std::uint32_t player = retail<std::uint32_t>(kPlayers);
    if (retail<std::uint32_t>(kGameMode) == 1 && player != 0 && static_cast<int>(retail<std::uint32_t>(kCurrentLevel)) == g.level) {
        if (g.mode == QuickMode::SpeedRun) {
            speed_run_poll(player, status);
        } else {
            competition_poll(player, status);
        }
    }
    g.last_status = status;
}

void challenge_shutdown() {
    if (active() && g.mode == QuickMode::Competition && g.comp.ending) {
        competition_write("none");
    }
}

}  // namespace opentony::kitdll
