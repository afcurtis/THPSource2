#pragma once

#include "career.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>

namespace opentony::runtime {

// Phase 4.8: the judging and ranking rules of the competition mode (competition.cpp). The
// retail object (CCompetition, 0x0055fe28) holds eight participants of 0x14 bytes from +0x04,
// five judges of 8 bytes from +0xb8 and the current heat and participant; the functions below
// are the ones that decide scores and order. The window, marquee, medal objects and videos
// (Competition_Init's player setup, WinLose*/EndGame* logic and display) need the frontend and
// the engine's object lists and stay with the retail code (milestone 4.10).
//
//   participant e at +0x04 + e * 0x14: +0x04 is human, +0x05 human number, +0x06 character,
//     +0x07 costume, +0x08 heat bracket, +0x09 slot id, three heat scores (u16) from +0x0a,
//     the total (u16) at +0x10
//   judge j at +0xb8 + j * 8: +0x04 and +0x06 the participants it favours and dislikes (+0x05
//     and +0x07 the bonus and penalty), scores (s16) for the current heat at +0xe0 + j * 2
//   +0xea / +0xeb: the judge whose score is dropped (highest / lowest); +0xec + i: the
//     participant in ranking position i; +0xf4 heat; +0xf5 participant up; +0xf6 previous
namespace competition {

constexpr std::uint32_t kObject = 0x0055fe28;
constexpr std::uint32_t kHumanMode = 0x00560080;     // byte: nonzero in the multi-player competition
constexpr std::uint32_t kScoreChanged = 0x00560081;  // byte
constexpr std::uint32_t kSetIndex = 0x0056007c;      // dword: which row of the judging table
constexpr std::uint32_t kJudgeRows = 0x005185d8;     // 0x20-byte rows: three score thresholds
constexpr std::uint32_t kLadder = 0x00525774;        // five u16 score anchors the judges scale between

constexpr std::uint32_t kTagIt = 0x0056db5c;         // the skater who is "it" in the tag game
constexpr std::uint32_t kMsgNotTag = 0x00540a74;
constexpr std::uint32_t kMsgTooManyMedals = 0x00525784;
constexpr std::uint32_t kMsgJudgeCount = 0x005257d4;

class Competition final {
public:
    Competition(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    [[nodiscard]] bool all_medals_won();            // 0x00419460: three medals (any colour)
    [[nodiscard]] bool all_golds_won();             // 0x00419500: three golds
    void assign_heat_order();                       // 0x00419880: shuffle the computer riders' heat brackets
    void judge_score(int participant);              // 0x00419910: the five judges score the human's run
    void ai_score(int participant);                 // 0x00419cc0: a computer rider's run
    [[nodiscard]] bool medal_display_done();        // 0x00419f90
    [[nodiscard]] bool next_human();                // 0x00419fb0: hand over to the next human participant
    void sort_ranking();                            // 0x0041aee0

    // tag.cpp, 0x004b8410: the tag game handicaps every skater but the one who is "it" by that
    // skater's score (0..12000 maps to 0..120 stat points); stat 3 is penalised twice.
    [[nodiscard]] std::int32_t tag_stat_modifier(std::uint32_t skater, int stat, std::int32_t value);

private:
    [[nodiscard]] int rand(int limit);              // FUN_0040b500
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace competition
}  // namespace opentony::runtime
