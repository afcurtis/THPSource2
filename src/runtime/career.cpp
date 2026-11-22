#include "auto_views.hpp"
#include "career.hpp"

#include <cstdio>
#include <vector>

namespace opentony::runtime::career {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kFlagBit8 = 0x8000;
constexpr std::uint32_t kMedalGold = 0x400;
constexpr std::uint32_t kMedalAny = 0x1c00;  // gold, silver, bronze
constexpr std::uint32_t kSkaterLoopEnd = 0x0053c1d4;  // the unlock loops run to here: 21 rows, the last is the table tail
constexpr std::uint32_t kSkaterLoopStart = skater::kRows + skater::kRowFlags;

// Service names (the vocabulary shared with the retail capture).
constexpr const char* kSfx = "sfx";                    // SFX_PlayX(id, volume, pan)
constexpr const char* kMessage = "message";            // FUN_004736f0(text, style, 1, 0, 0)
constexpr const char* kMessagePrepare = "message_prepare";  // FUN_004736c0(text)
constexpr const char* kPlayerEffect = "cheat_effect";  // FUN_0048fd60(0xffffff88, a, b), stdcall
constexpr const char* kDiscoStart = "disco_start";     // FUN_004be350()

}  // namespace

// ---- helpers ----------------------------------------------------------------------------------

void Career::message(std::uint32_t text) {
    std::string content;
    if (m_.contains(text)) {
        try {
            content = m_.cstring(text);
        } catch (const MemoryFault&) {
        }
    }
    s_.note(kMessage, {text, kMessageStyle, 1, 0, 0}, content);
}

// FUN_005002a1 (sprintf) with the "%d of %d %s" shape (0x00525184); `noun` is a string pointer.
void Career::format_progress(std::uint32_t destination, std::int32_t a, std::int32_t b, std::uint32_t noun) {
    // The C runtime prints a null %s argument as "(null)".
    const std::string text = noun == 0 ? std::string("(null)") : m_.contains(noun) ? m_.cstring(noun) : std::string();
    char buffer[256];
    const int n = std::snprintf(buffer, sizeof buffer, "%d of %d %s", a, b, text.c_str());
    std::vector<std::uint8_t> bytes(buffer, buffer + n);
    bytes.push_back(0);
    m_.write(destination, bytes);
}

// sprintf(destination, "%d of %d Trick Gaps", a, b) (0x00525230).
void Career::format_trick_progress(std::uint32_t destination, std::int32_t a, std::int32_t b) {
    char buffer[64];
    const int n = std::snprintf(buffer, sizeof buffer, "%d of %d Trick Gaps", a, b);
    std::vector<std::uint8_t> bytes(buffer, buffer + n);
    bytes.push_back(0);
    m_.write(destination, bytes);
}

// ---- goals (tapes) ----------------------------------------------------------------------------

bool Career::got(int goal) {
    const std::uint32_t rec = current_record();
    check_goal(goal);
    return (short_at(m_, mask_address(rec, current_level())) & (1 << (goal & 31))) != 0;
}

bool Career::just_got(int goal) {
    check_goal(goal);
    return ((1u << (goal & 31)) & global_word(m_, kGoalsGot)) != 0;
}

int Career::level_tapes(std::uint32_t rec, int level) {
    s_.check(rec != 0, kMsgNoProgress);
    int count = 0;
    for (int goal = 0; goal < 10; ++goal) {
        if ((short_at(m_, mask_address(rec, level)) & (1 << goal)) != 0) {
            ++count;
        }
    }
    return count;
}

int Career::num_level_goals() { return level_tapes(current_record(), current_level()); }

int Career::count_money() {
    const std::uint32_t rec = current_record();
    int count = 0;
    for (int half = 0; half < 2; ++half) {
        for (int bit = 0; bit < 32; ++bit) {
            s_.check(current_level() < 0xd, kMsgBadMoneyMask);
            const std::uint32_t word =
                word_at(m_, rec + kRecMoneyMasks + static_cast<std::uint32_t>(half + current_level() * 2) * 4);
            if ((word & (1u << bit)) != 0) {
                ++count;
            }
        }
    }
    return count;
}

void Career::check_score() {
    const std::int32_t score = int_at(m_, player() + kPlayerScore);
    for (int goal = 0; goal < 10; ++goal) {
        const std::uint32_t row = goal_row(current_level(), goal);
        if (int_at(m_, row + kGoalType) == 0 && int_at(m_, row + kGoalThreshold) <= score) {
            if (!got(goal)) {
                give_goal(goal);
            }
        }
    }
}

void Career::give_goal(int goal) {
    const int level = current_level();
    const std::uint32_t rec = current_record();
    s_.check(global_int(m_, kGameMode) == 1, kMsgTapeNotCareer);
    check_goal(goal);
    s_.check(level >= 0 && level <= 0xc, kMsgInvalidLevel);
    const std::uint32_t bit = 1u << (goal & 31);
    s_.check((bit & static_cast<std::uint32_t>(short_at(m_, mask_address(rec, level)))) == 0, kMsgTapeTwice);
    s_.note(kSfx, {0x17c, 0x2000, 0});
    message(word_at(m_, goal_row(current_level(), goal) + kGoalText));
    set_global_word(m_, kGoalsGot, global_word(m_, kGoalsGot) | bit);
    set_half_at(m_, mask_address(rec, level), half_at(m_, mask_address(rec, level)) | (1u << (goal & 31)));
    // The record's tape count: goals got on every level that is not a contest.
    int tapes = 0;
    for (int l = 0; l < static_cast<int>(kLevelCount); ++l) {
        for (int g = 0; g < 10; ++g) {
            if (contest_flag(l) == 0 && (short_at(m_, mask_address(rec, l)) & (1 << g)) != 0) {
                ++tapes;
            }
        }
    }
    set_byte_at(m_, rec + kRecTapes, static_cast<std::uint32_t>(tapes));
    set_word_at(m_, player() + kPlayerTapes, word_at(m_, player() + kPlayerTapes) + 1);
    const std::uint32_t reward = word_at(m_, goal_row(current_level(), goal) + kGoalReward);
    set_word_at(m_, rec + kRecCashTotal, word_at(m_, rec + kRecCashTotal) + reward);
    set_word_at(m_, rec + kRecCash, word_at(m_, rec + kRecCash) + reward);
    check_clear();
}

int Career::goal_index(int type) {
    for (int goal = 0; goal < 10; ++goal) {
        if (int_at(m_, goal_row(current_level(), goal) + kGoalType) == type) {
            return goal;
        }
    }
    return -1;
}

void Career::give_goal_type(int type) {
    const int index = goal_index(type);
    s_.check(index != -1, kMsgIllegalGoalType);
    give_goal(goal_index(type));
}

void Career::check_clear() {
    if (global_int(m_, kCashIconCount) <= count_money()) {
        if (contest_flag(current_level()) != 0) {
            const std::uint32_t rec = current_record();
            const std::uint32_t at = mask_address(rec, current_level());
            set_half_at(m_, at, half_at(m_, at) | kFlagBit8);
            return;
        }
        if (!got_goal_type(kGoalTypeAll)) {
            if (num_level_goals() == 9) {
                give_goal_type(kGoalTypeAll);
            }
        }
    }
}

bool Career::got_goal_type(int type) {
    const int index = goal_index(type);
    if (index == -1) {
        return false;
    }
    return got(index);
}

void Career::give_medal(int level, int medal_bit) {
    const std::uint32_t rec = current_record();
    s_.check(global_int(m_, kGameMode) == 1, kMsgMedalNotCareer);
    s_.check(level >= 0 && level <= 0xc, kMsgInvalidLevel);
    const std::uint32_t at = mask_address(rec, level);
    set_half_at(m_, at, half_at(m_, at) | (1u << (static_cast<unsigned>(medal_bit) & 31)));
    int golds = 0;
    for (int l = 0; l < static_cast<int>(kLevelCount); ++l) {
        if (contest_flag(l) != 0 && (half_at(m_, mask_address(rec, l)) & kMedalGold) != 0) {
            ++golds;
        }
    }
    set_byte_at(m_, rec + kRecGolds, static_cast<std::uint32_t>(golds));
}

// ---- gaps -------------------------------------------------------------------------------------

bool Career::gap_active(std::uint32_t gap) {
    const std::int32_t id = short_at(m_, gap + 4) % 10000;
    return int_at(m_, level(current_level()) + kLvGapLow) <= id && id <= int_at(m_, level(current_level()) + kLvGapHigh);
}

bool Career::gap_is_goal(std::uint32_t gap) { return short_at(m_, gap + 2) == 1 && gap_active(gap); }

bool Career::gap_is_trick(std::uint32_t gap) { return short_at(m_, gap + 2) == 2 && gap_active(gap); }

int Career::gap_ordinal(std::uint32_t gap, bool (Career::*filter)(std::uint32_t), std::uint32_t not_found) {
    int ordinal = 0;
    std::uint32_t at = global_word(m_, kGapTablePointer);
    while (short_at(m_, at + 4) != -1) {
        if (gap_active(at) && (filter == nullptr || (this->*filter)(at))) {
            if (at == gap) {
                return ordinal;
            }
            ++ordinal;
        }
        at += kGapRecordSize;
    }
    s_.assert_failed(not_found);
    return 0;  // retail returns whatever the assert routine leaves in eax
}

int Career::gap_goal_number(std::uint32_t gap) {
    s_.check(gap_active(gap), kMsgNotGoalGap);
    return gap_ordinal(gap, &Career::gap_is_goal, kMsgGapNotFound);
}

int Career::gap_trick_number(std::uint32_t gap) {
    s_.check(gap_active(gap), kMsgGapNotInLevel);
    return gap_ordinal(gap, &Career::gap_is_trick, kMsgGapNotFound);
}

int Career::gap_number(std::uint32_t gap) { return gap_ordinal(gap, nullptr, kMsgGapNotFound); }

bool Career::got_goal_gap(std::uint32_t gap) {
    return ((1u << (gap_goal_number(gap) & 31)) & global_word(m_, kGoalGapsGot)) != 0;
}

bool Career::got_trick_gap(std::uint32_t gap) {
    return ((1u << (gap_trick_number(gap) & 31)) & global_word(m_, kTrickGapsGot)) != 0;
}

void Career::give_goal_gap(std::uint32_t gap) {
    const int n = gap_goal_number(gap);
    set_global_word(m_, kGoalGapsGot, global_word(m_, kGoalGapsGot) | (1u << (n & 31)));
}

void Career::give_trick_gap(std::uint32_t gap) {
    const int n = gap_trick_number(gap);
    set_global_word(m_, kTrickGapsGot, global_word(m_, kTrickGapsGot) | (1u << (n & 31)));
}

std::uint32_t Career::anyone_got_gap(std::uint32_t gap) {
    const int n = gap_number(gap);
    const std::uint32_t at = kGapBits + static_cast<std::uint32_t>(current_level() * 3 + (n >> 5)) * 4;
    return word_at(m_, at) & (1u << (n & 31));
}

void Career::give_gap(std::uint32_t gap) {
    const int n = gap_number(gap);
    const std::uint32_t at = kGapBits + static_cast<std::uint32_t>(current_level() * 3 + (n >> 5)) * 4;
    set_word_at(m_, at, word_at(m_, at) | (1u << (n & 31)));
}

int Career::count_bits(std::uint32_t value) {
    int count = 0;
    for (; value != 0; value >>= 1) {
        if ((value & 1) != 0) {
            ++count;
        }
    }
    return count;
}

int Career::num_goal_gaps_got() { return count_bits(global_word(m_, kGoalGapsGot)); }

int Career::num_trick_gaps_got() { return count_bits(global_word(m_, kTrickGapsGot)); }

void Career::award_goal_gap(std::uint32_t gap) {
    give_goal_gap(gap);
    const int got_count = num_goal_gaps_got();
    if (got_count == global_int(m_, kGoalGapCount)) {
        give_goal_type(kGoalTypeGoalGaps);
        return;
    }
    const int index = goal_index(kGoalTypeGoalGaps);
    format_progress(kGapText, got_count, global_int(m_, kGoalGapCount), word_at(m_, goal_row(current_level(), index) + kGoalNoun));
    message(kGapText);
}

void Career::award_trick_gap(std::uint32_t gap) {
    s_.check(gap_active(gap), kMsgGapNotInLevel);   // (the decompiler lost this first assertion)
    s_.check(gap_is_trick(gap), kMsgNotTrickGap);
    if (gap_is_trick(gap)) {
        if (!got_trick_gap(gap)) {
            if (!got_goal_type(kGoalTypeTrickGaps)) {
                give_trick_gap(gap);
                const int got_count = num_trick_gaps_got();
                if (got_count == global_int(m_, kTrickGapCount)) {
                    give_goal_type(kGoalTypeTrickGaps);
                    return;
                }
                format_trick_progress(kTrickGapText, got_count, global_int(m_, kTrickGapCount));
                message(kTrickGapText);
                return;
            }
        }
    }
    s_.check(!got_trick_gap(gap), kMsgGapAwardedTwice);
    s_.check(!got_goal_type(kGoalTypeTrickGaps), kMsgGapAwardedWhenGotGoal);
    s_.assert_failed(kMsgUnhandledTrickGap);
}

// 0x00414c50 Gap_CompleteDefinition(gap): only the early levels (0-9) and level 13 count gaps, and not in the custom park. In every
// game mode but 1 to 3 nothing more happens; modes 1 to 3 give the gap to the character; in career mode (1) a goal gap not yet got (and the gap goal not yet got) is parked on
// the player at +0x3028, after the gap parked there before is awarded, and a trick gap not yet got (the trick-gap goal not yet got) is parked at +0x3024.
// (Like the rest of the career, it leaves the assertion macro's line and file name words, 0x00568638 and 0x0056881c, alone: the gates mask them.)
void Career::complete_gap(std::uint32_t gap) {
    const std::int32_t level_now = current_level();
    if (!(level_now < 10 || level_now == 13) || global_byte(m_, kCustomParkFlag) != 0) {
        return;
    }
    s_.check(gap_active(gap), kMsgGapNotInLevel);
    std::uint32_t mode = global_word(m_, kGameMode);
    if (mode != 1 && mode != 3 && mode != 2) {
        return;
    }
    give_gap(gap);
    if (global_word(m_, kGameMode) != 1) {
        return;
    }
    if (gap_is_goal(gap) && !got_goal_gap(gap) && !got_goal_type(kGoalTypeGoalGaps)) {
        const std::uint32_t pending = word_at(m_, player() + kPlayerPendingGoalGap);
        if (pending != 0) {
            award_goal_gap(pending);
        }
        s_.note("printf_log", {kMsgGapCompleted});   // (the gap's name is its second argument; only the format is reported)
        set_word_at(m_, player() + kPlayerPendingGoalGap, gap);
        return;
    }
    if (gap_is_trick(gap) && !got_trick_gap(gap) && !got_goal_type(kGoalTypeTrickGaps)) {
        set_word_at(m_, player() + kPlayerPendingTrickGap, gap);
    }
}

int Career::count_gaps() {
    int total = 0;
    for (std::uint32_t at = kGapBits; at < 0x0056a29c; at += 4) {
        total += count_bits(word_at(m_, at));
    }
    return total;
}

bool Career::got_all_gaps() { return global_int(m_, kTotalGaps) <= count_gaps(); }

// ---- cash icons and pickups -------------------------------------------------------------------

int Career::money_number(int node) {
    s_.check(node >= 0 && node < global_int(m_, kNodeCount), kMsgBadNode);
    const std::uint32_t at = word_at(m_, global_word(m_, kNodeList) + static_cast<std::uint32_t>(node) * 4);
    const std::int16_t type = short_at(m_, at);
    s_.check(type == 5, kMsgNotPowerup);
    return half_at(m_, at + 2) >> 8;
}

bool Career::got_money(int node) {
    (void)current_record();
    s_.check(node >= 0 && node < global_int(m_, kNodeCount), kMsgBadNode);
    const int number = money_number(node);
    std::uint32_t mask;
    if (s_.query("video_playing") == 0) {
        const std::uint32_t rec = record(int_at(m_, player() + kPlayerCharacter));
        s_.check(current_level() < 0xd, kMsgBadMoneyMask);
        mask = word_at(m_, rec + kRecMoneyMasks + static_cast<std::uint32_t>((number >> 5) + current_level() * 2) * 4);
    } else {
        mask = word_at(m_, kMoneyMaskVideo + static_cast<std::uint32_t>(number >> 5) * 4);
    }
    return ((1u << (number & 31)) & mask) != 0;
}

void Career::give_money(int node, int amount) {
    if (s_.query("video_playing") != 0) {
        return;
    }
    const std::uint32_t rec = current_record();
    s_.check(node >= 0 && node < global_int(m_, kNodeCount), kMsgBadNode);
    const int number = money_number(node);
    s_.check(current_level() < 0xd, kMsgBadMoneyMask);
    const std::uint32_t at =
        rec + kRecMoneyMasks + static_cast<std::uint32_t>((number >> 5) + current_level() * 2) * 4;
    set_word_at(m_, at, word_at(m_, at) | (1u << (number & 31)));
    set_word_at(m_, rec + kRecCashTotal, word_at(m_, rec + kRecCashTotal) + static_cast<std::uint32_t>(amount));
    set_word_at(m_, rec + kRecCash, word_at(m_, rec + kRecCash) + static_cast<std::uint32_t>(amount));
    check_clear();
    if (global_int(m_, kCashIconCount) == count_money()) {
        if (contest_flag(current_level()) == 0 && got_goal_type(kGoalTypeAll)) {
            return;
        }
        message(global_word(m_, kCashIconsClearedText));
    }
}

void Career::get_level_pickup() {
    set_global_word(m_, kPickupsGot, global_word(m_, kPickupsGot) + 1);
    const int index = goal_index(kGoalTypePickups);
    if (index == -1) {
        return;
    }
    if (global_int(m_, kPickupsGot) == global_int(m_, kLevelPickups)) {
        give_goal(index);
        return;
    }
    format_progress(kPickupText, global_int(m_, kPickupsGot), global_int(m_, kLevelPickups),
                    word_at(m_, goal_row(current_level(), index) + kGoalNoun));
    s_.note(kMessagePrepare, {kPickupText});
    message(kPickupText);
}

void Career::count_things() {
    const std::uint32_t rec = current_record();
    set_global_word(m_, kLiveCash, word_at(m_, rec + kRecCashTotal));
    set_global_word(m_, kHighestOpen, static_cast<std::uint32_t>(highest_open_level(1, false)));
    set_global_word(m_, kActiveGapCount, 0);
    set_global_word(m_, kGoalGapCount, 0);
    set_global_word(m_, kTrickGapCount, 0);
    set_global_word(m_, kCashIconCount, 0);
    set_global_word(m_, kGameCleared, game_cleared(sk_.current_character()) ? 1u : 0u);
    set_global_word(m_, kLevelPickups, 0);
    set_global_word(m_, kPickupsGot, 0);
    set_global_word(m_, kGoalsGot, 0);
    set_global_word(m_, kGoalGapsGot, 0);
    set_global_word(m_, kTrickGapsGot, 0);
    set_global_word(m_, kTotalGaps, 0);
    std::uint32_t at = global_word(m_, kGapTablePointer);
    std::int16_t id = short_at(m_, at + 4);
    while (id != -1) {
        const std::int32_t gap_id = short_at(m_, at + 4) % 10000;
        const bool in_hidden_levels = (int_at(m_, level(8) + kLvGapLow) <= gap_id && gap_id <= int_at(m_, level(8) + kLvGapHigh)) ||
                                      (int_at(m_, level(9) + kLvGapLow) <= gap_id && gap_id <= int_at(m_, level(9) + kLvGapHigh));
        if (!in_hidden_levels && gap_id >= 0) {
            set_global_word(m_, kTotalGaps, global_word(m_, kTotalGaps) + 1);
        }
        if (gap_active(at)) {
            set_global_word(m_, kActiveGapCount, global_word(m_, kActiveGapCount) + 1);
            if (gap_is_goal(at)) {
                set_global_word(m_, kGoalGapCount, global_word(m_, kGoalGapCount) + 1);
            } else if (gap_is_trick(at)) {
                set_global_word(m_, kTrickGapCount, global_word(m_, kTrickGapCount) + 1);
            }
        }
        id = short_at(m_, at + kGapRecordSize + 4);
        at += kGapRecordSize;
    }
    s_.check(global_int(m_, kActiveGapCount) < 0x61, kMsgTooManyGaps);
    const std::int32_t nodes = global_int(m_, kNodeCount);
    for (int i = 0; i < nodes; ++i) {
        const std::uint32_t node = word_at(m_, global_word(m_, kNodeList) + static_cast<std::uint32_t>(i) * 4);
        if (short_at(m_, node) == 5) {
            const std::uint8_t kind = byte_at(m_, node + 2);
            std::uint16_t word = kind;
            if (kind == 0x18 || kind == 0x19 || kind == 0x1a) {
                word = static_cast<std::uint16_t>((global_word(m_, kCashIconCount) & 0xff) << 8 | kind);
                set_half_at(m_, node + 2, word);
                set_global_word(m_, kCashIconCount, global_word(m_, kCashIconCount) + 1);
            }
            if (word == 0x21) {
                set_global_word(m_, kLevelPickups, global_word(m_, kLevelPickups) + 1);
            }
        }
    }
}

// ---- stat points ------------------------------------------------------------------------------

bool Career::buy_point(int stat, int cost) {
    const std::uint32_t rec = current_record();
    s_.check(stat >= 0 && stat <= 9, kMsgStatRange);
    const std::uint32_t stat_at = rec + kRecStats + static_cast<std::uint32_t>(stat);
    if (sbyte_at(m_, stat_at) > 9) {
        return false;
    }
    if (!sk_.custom_with_flag(sk_.current_character())) {
        if (int_at(m_, rec + kRecCash) < cost) {
            return false;
        }
        set_byte_at(m_, stat_at, byte_at(m_, stat_at) + 1u);
        set_word_at(m_, rec + kRecCash, word_at(m_, rec + kRecCash) - static_cast<std::uint32_t>(cost));
        return true;
    }
    const std::uint32_t custom = skater::kCustomRecords;
    std::uint32_t at = custom + static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
                       skater::kCustomPoints;
    if (sbyte_at(m_, at) < 1) {
        return false;
    }
    at = custom + static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
         skater::kCustomPoints;
    set_byte_at(m_, at, byte_at(m_, at) - 1u);
    const std::uint32_t stats = custom +
        static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
        skater::kCustomStats + static_cast<std::uint32_t>(stat) * 4;
    set_word_at(m_, stats, word_at(m_, stats) + 1);
    set_byte_at(m_, stat_at, byte_at(m_, stat_at) + 1u);
    return true;
}

bool Career::sell_point(int stat) {
    const std::uint32_t rec = current_record();
    s_.check(stat >= 0 && stat <= 9, kMsgStatRange);
    const std::uint32_t stat_at = rec + kRecStats + static_cast<std::uint32_t>(stat);
    if (sbyte_at(m_, stat_at) < 1) {
        return false;
    }
    if (!sk_.custom_with_flag(sk_.current_character())) {
        set_byte_at(m_, stat_at, byte_at(m_, stat_at) - 1u);
        const int refund = point_cost();
        set_word_at(m_, rec + kRecCash, word_at(m_, rec + kRecCash) + static_cast<std::uint32_t>(refund));
        return true;
    }
    const std::uint32_t custom = skater::kCustomRecords;
    std::uint32_t at = custom + static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
                       skater::kCustomPoints;
    if (sbyte_at(m_, at) > 4) {
        return false;
    }
    at = custom + static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
         skater::kCustomPoints;
    set_byte_at(m_, at, byte_at(m_, at) + 1u);
    const std::uint32_t stats = custom +
        static_cast<std::uint32_t>(sk_.custom_index(sk_.current_character())) * skater::kCustomStride +
        skater::kCustomStats + static_cast<std::uint32_t>(stat) * 4;
    set_word_at(m_, stats, word_at(m_, stats) - 1);
    set_byte_at(m_, stat_at, byte_at(m_, stat_at) - 1u);
    return true;
}

int Career::point_cost() {
    const std::uint32_t rec = current_record();
    const int base_row = sk_.map_enabled_index(0);
    int spent = 0;
    int base = 0;
    for (int i = 0; i < 10; ++i) {
        spent += sbyte_at(m_, rec + kRecStats + static_cast<std::uint32_t>(i));
        base += byte_at(m_, skater::row(base_row) + skater::kRowStats + static_cast<std::uint32_t>(i));
    }
    const int character = sk_.current_character();
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & skater::kFlagCustom) != 0) {
        const int custom = sk_.custom_index(sk_.current_character());
        const int points = sbyte_at(m_, skater::kCustomRecords + static_cast<std::uint32_t>(custom) * skater::kCustomStride + skater::kCustomPoints);
        return (((spent - base) - 5 + points) * 5 + 5) * 10;
    }
    return (((spent - base) - 5) * 5 + 5) * 10;
}

// ---- levels -----------------------------------------------------------------------------------

int Career::num_levels_with_medals(std::uint32_t rec) {
    int count = 0;
    for (int l = 0; l < static_cast<int>(kLevelCount); ++l) {
        if (contest_flag(l) != 0 && (half_at(m_, mask_address(rec, l)) & kMedalAny) != 0) {
            ++count;
        }
    }
    return count;
}

bool Career::level_open_career_mode(int level_index, std::uint32_t rec) {
    if (global_int(m_, kAllLevelsOpen) != 0) {
        return true;
    }
    if (level_index < 10 || level_index == 0xd) {
        if (level_index < 0) {
            return true;
        }
        for (int i = 0; i <= level_index; ++i) {
            const std::uint32_t row = level(i);
            if (int_at(m_, row + kLvMedalsNeeded) == 0) {
                if (int_at(m_, rec + kRecCashTotal) < int_at(m_, row + kLvCashNeeded)) {
                    return false;
                }
            } else if (num_levels_with_medals(rec) < int_at(m_, row + kLvMedalsNeeded)) {
                return false;
            }
        }
        return true;
    }
    std::uint16_t mask;
    if (level_index == 10) {
        mask = half_at(m_, rec + 0x10);
    } else if (level_index == 0xb) {
        mask = half_at(m_, rec + 0x16);
    } else {
        if (level_index != 0xc) {
            return false;
        }
        return (half_at(m_, rec + 0x1a) & 0x400) != 0;
    }
    return (mask & 0x400) != 0;
}

bool Career::level_open(int level_index, int mode) {
    if (global_int(m_, kAllLevelsOpen) != 0) {
        return true;
    }
    if (level_index == 8) {
        if ((global_byte(m_, kUnlockFlags) & 8) != 0) {
            return true;
        }
    } else if (level_index == 9 && (global_byte(m_, kUnlockFlags) & 0x10) != 0) {
        return true;
    }
    if (mode == 1) {
        return level_open_career_mode(level_index, current_record());
    }
    if (mode < 2 || mode > 4) {
        if (level_index < 4) {
            return true;
        }
    }
    for (int character = 0; character < 0x14; ++character) {
        if (level_open_career_mode(level_index, record(character))) {
            return true;
        }
    }
    return false;
}

int Career::highest_open_level(int mode, bool with_specials) {
    if (global_int(m_, kAllLevelsOpen) != 0) {
        return 0xc;
    }
    int highest = 0;
    for (int l = 0; l < 0xd; ++l) {
        if (with_specials || l < 10) {
            if (level_open(l, mode)) {
                highest = l;
            }
        }
    }
    return highest;
}

int Career::level_needs(int level_index) {
    const std::uint32_t rec = current_record();
    if (level_open_career_mode(level_index, rec)) {
        return 0;
    }
    const std::uint32_t row = level(level_index);
    if (int_at(m_, row + kLvMedalsNeeded) != 0) {
        return -int_at(m_, row + kLvMedalsNeeded);
    }
    return int_at(m_, row + kLvCashNeeded);
}

// ---- records ----------------------------------------------------------------------------------

bool Career::is_trick_purchased(std::uint32_t rec, int trick) {
    s_.check(rec != 0, kMsgNoProgress);
    s_.check(trick >= 0 && trick <= 0x7f, kMsgBadTrickIndex);
    const int biased = trick + ((trick >> 31) & 0x1f);
    return (word_at(m_, rec + kRecUnlocks + static_cast<std::uint32_t>(biased >> 5) * 4) & (1u << (trick & 31))) != 0;
}

int Career::count_gold_medals(std::uint32_t rec) {
    s_.check(rec != 0, kMsgNoProgress);
    int count = 0;
    for (int l = 0; l < static_cast<int>(kLevelCount); ++l) {
        if (contest_flag(l) != 0 && (half_at(m_, mask_address(rec, l)) & kMedalGold) != 0) {
            ++count;
        }
    }
    return count;
}

int Career::count_medals(std::uint32_t rec) {
    s_.check(rec != 0, kMsgNoProgress);
    int count = 0;
    for (int l = 0; l < static_cast<int>(kLevelCount); ++l) {
        if (contest_flag(l) != 0 && (half_at(m_, mask_address(rec, l)) & kMedalAny) != 0) {
            ++count;
        }
    }
    return count;
}

bool Career::game_cleared(int character) {
    const std::uint32_t rec = record(character);
    return byte_at(m_, rec + kRecTapes) == 0x32 && byte_at(m_, rec + kRecGolds) == 3 && (half_at(m_, rec + 0x10) & kFlagBit8) != 0 &&
           (half_at(m_, rec + 0x16) & kFlagBit8) != 0 && (half_at(m_, rec + 0x1a) & kFlagBit8) != 0;
}

void Career::clear_game(int character) {
    const std::uint32_t rec = record(character);
    set_word_at(m_, rec + kRecCashTotal, 900000);
    set_word_at(m_, rec + kRecCash, 500000);
    set_byte_at(m_, rec + kRecTapes, 0x32);
    set_byte_at(m_, rec + kRecGolds, 3);
    // Only levels 0-7: retail's loop ends when the contest-flag pointer passes 0x0053cf9f.
    for (int l = 0; l < 8; ++l) {
        s_.check(l < 0xd, kMsgBadMoneyMask);
        set_word_at(m_, rec + kRecMoneyMasks + static_cast<std::uint32_t>(l) * 8, 0xffffffffu);
        set_byte_at(m_, rec + kRecLevelBytes + static_cast<std::uint32_t>(l), 0xff);
        const bool contest = contest_flag(l) != 0;
        set_half_at(m_, mask_address(rec, l), (contest ? 0x9801u : 0u) + 0x3ffu);
    }
    set_global_word(m_, kHighestOpen, static_cast<std::uint32_t>(highest_open_level(1, false)));
    if (character == 0) {
        set_byte_at(m_, rec + kRecFlags, byte_at(m_, rec + kRecFlags) | 0x40u);
    }
}

void Career::clear_game_with_everyone() {
    for (int character = 0; character < 0x14; ++character) {
        clear_game(character);
    }
}

// ---- cheats and locked characters -------------------------------------------------------------

void Career::unlock_cheat(int cheat, int character) {
    const std::uint32_t entry = kCheats + static_cast<std::uint32_t>(cheat) * 4;
    set_byte_at(m_, entry, 1);
    if ((word_at(m_, skater::row(character) + skater::kRowFlags) & skater::kFlagCustom) != 0) {
        // Walk back to the first row of the run of custom skaters the character belongs to.
        std::uint8_t flags = byte_at(m_, skater::kRows - 0x14 + static_cast<std::uint32_t>(character) * skater::kRowStride);
        while ((flags & skater::kFlagCustom) != 0) {
            --character;
            flags = byte_at(m_, skater::kRows - 0x14 + static_cast<std::uint32_t>(character) * skater::kRowStride);
        }
    }
    set_byte_at(m_, entry + 1, static_cast<std::uint32_t>(character));
    if (cheat == 0) {
        unlock_character(0x200);
    } else if (cheat == 1) {
        unlock_character(0x100);
    } else if (cheat == 2) {
        unlock_character(0x40);
    }
}

bool Career::cheat_unlocked(int cheat) { return byte_at(m_, kCheats + static_cast<std::uint32_t>(cheat) * 4) != 0; }

std::uint8_t Career::cheat_state(int cheat) { return byte_at(m_, kCheats + static_cast<std::uint32_t>(cheat) * 4 + 2); }

std::uint8_t Career::get_cheat(int cheat) {
    s_.check(cheat >= 0 && cheat <= 0x10, kMsgCheatRange);
    if (s_.query("pack_new_free") == 0 && global_int(m_, kGameMode) != 7) {
        return byte_at(m_, kCheats + static_cast<std::uint32_t>(cheat) * 4 + 2);
    }
    return cheat != 8 ? 0 : 100;
}

void Career::set_cheat(int cheat, int value, std::uint32_t apply) {
    const std::uint32_t entry = kCheats + static_cast<std::uint32_t>(cheat) * 4 + 2;
    const std::int8_t old = sbyte_at(m_, entry);
    set_byte_at(m_, entry, static_cast<std::uint32_t>(value));
    if (static_cast<std::int8_t>(value) == old) {
        return;
    }
    const bool apply_now = (apply & 0xff) != 0;
    auto each_player = [&](auto&& body) {
        for (std::uint32_t at = kPlayers; at < kPlayers + 8; at += 4) {
            const std::uint32_t skater_object = word_at(m_, at);
            if (skater_object != 0) {
                body(skater_object);
            }
        }
    };
    if (static_cast<std::int8_t>(value) == 0) {
        switch (cheat) {
        case 3:
            set_global_word(m_, 0x0056a8ec, 0);
            return;
        case 4:
            set_global_word(m_, 0x0056a890, 0);
            if (apply_now) {
                each_player([&](std::uint32_t object) {
                    set_half_at(m_, object + 0x28, 0x1000);
                    set_half_at(m_, object + 0x2a, 0x1000);
                    set_half_at(m_, object + 0x2c, 0x1000);
                    s_.note(kPlayerEffect, {0xffffff88, 1, 1});
                });
                return;
            }
            break;
        case 7:
            set_global_word(m_, 0x0056b7f4, 0);
            return;
        case 9:
            set_global_word(m_, 0x0056437c, 0);
            return;
        case 0xb:
            if (apply_now) {
                each_player([&](std::uint32_t) { s_.note(kPlayerEffect, {0xffffff88, 1, 2}); });
            }
            break;
        case 0xf:
            set_global_word(m_, 0x0056db8c, 0);
            return;
        case 0x10:
            set_global_word(m_, 0x0056a88c, 0);
            return;
        default:
            break;
        }
    } else {
        switch (cheat) {
        case 4:
            if (get_cheat(0xb) != 0) {
                set_cheat(0xb, 0, apply);
            }
            set_global_word(m_, 0x0056a890, 1);
            if (apply_now) {
                each_player([&](std::uint32_t object) {
                    if ((word_at(m_, skater::row(int_at(m_, object + kPlayerCharacter)) + skater::kRowFlags) & 0x100) == 0) {
                        set_half_at(m_, object + 0x28, 0xb54);
                        set_half_at(m_, object + 0x2a, 0xb54);
                        set_half_at(m_, object + 0x2c, 0xb54);
                        s_.note(kPlayerEffect, {0xffffff88, 0, 1});
                    }
                });
                return;
            }
            break;
        case 7:
            set_global_word(m_, 0x0056b7f4, 10);
            return;
        case 0xb:
            if (get_cheat(4) != 0) {
                set_cheat(4, 0, apply);
            }
            if (apply_now) {
                each_player([&](std::uint32_t) { s_.note(kPlayerEffect, {0xffffff88, 0, 2}); });
                return;
            }
            break;
        case 0xf:
            set_global_word(m_, 0x0056db8c, 1);
            if (apply_now) {
                s_.note(kDiscoStart);
                return;
            }
            break;
        case 0x10:
            set_global_word(m_, 0x0056a88c, 1);
            return;
        default:
            break;
        }
    }
}

void Career::toggle_cheat(int cheat, std::uint32_t apply) {
    const std::uint8_t state = cheat_state(cheat);
    set_cheat(cheat, state == 0 ? 1 : 0, apply);
}

int Career::new_cheat() {
    const std::uint32_t character = global_word(m_, kNewCheatCharacter);
    const std::uint32_t flags = word_at(m_, skater::row(static_cast<int>(character)) + skater::kRowFlags);
    int cheat;
    if ((flags & 0x40) != 0) {
        cheat = 0xe;
    } else if ((flags & 0x80) != 0) {
        cheat = 0xf;
    } else if ((flags & 0x100) != 0) {
        cheat = 0x10;
    } else if (character == 0) {
        cheat = 0;
    } else if ((flags & skater::kFlagCustom) == 0) {
        cheat = 2;
        while (cheat_unlocked(cheat)) {
            ++cheat;
            s_.check(cheat < 0x11, skater::kMsgEmpty);
        }
    } else {
        cheat = 1;
    }
    s_.check(!cheat_unlocked(cheat), skater::kMsgEmpty);
    return cheat;
}

int Career::count_unlocked_cheats() {
    int count = 0;
    for (int cheat = 0; cheat < 0x11; ++cheat) {
        if (cheat_unlocked(cheat)) {
            ++count;
        }
    }
    return count;
}

std::uint32_t Career::cheat_name(int cheat) {
    static constexpr std::uint32_t kNames[17] = {0x0052e3e4, 0x0052e3e8, 0x0052e3ec, 0x0052e98c, 0x0052e3c0, 0x0052e3a4,
                                                 0x0052e3a8, 0x0052e3ac, 0x0052e3b0, 0x0052e3b4, 0x0052e3b8, 0x0052e3bc,
                                                 0x0052e3c4, 0x0052e3c8, 0x0052e3d8, 0x0052e3dc, 0x0052e3e0};
    if (cheat >= 0 && cheat < 17) {
        return word_at(m_, kNames[cheat]);
    }
    s_.assert_failed(kMsgUnknownCheat);
    return 0x005253ec;
}

int Career::cheat_type(int cheat) noexcept {
    if (cheat > -1) {
        if (cheat < 3) {
            return 0;
        }
        if (cheat == 8) {
            return 2;
        }
    }
    return 1;
}

void Career::unlock_character(std::uint32_t mask) {
    for (std::uint32_t flags = kSkaterLoopStart; flags < kSkaterLoopEnd; flags += skater::kRowStride) {
        if ((word_at(m_, flags) & mask) != 0) {
            set_word_at(m_, flags - 4, 1);
            set_word_at(m_, flags, word_at(m_, flags) & 0xffffffe7u);
        }
    }
}

bool Career::character_is_unlocked(std::uint32_t mask) {
    int character = 0;
    for (std::uint32_t flags = kSkaterLoopStart; flags < kSkaterLoopEnd; flags += skater::kRowStride, ++character) {
        if ((mask & 0x200) == 0) {
            if ((mask & word_at(m_, flags)) != 0) {
                return word_at(m_, flags - 4) != 0;
            }
        } else if ((mask & word_at(m_, flags)) != 0) {
            return ((~word_at(m_, flags)) >> 3 & 1) != 0;
        }
    }
    s_.assert_failed(skater::kMsgEmpty);
    return false;
}

void Career::apply_cheats() {
    if (cheat_unlocked(0)) {
        unlock_character(0x200);
    }
    if (cheat_unlocked(1)) {
        unlock_character(0x100);
    }
    if (cheat_unlocked(2)) {
        unlock_character(0x40);
    }
    if ((global_byte(m_, kUnlockFlags) & 4) != 0) {
        unlock_character(0x80);
    }
}

void Career::capture_skater_defaults() {
    int i = 0;
    for (std::uint32_t flags = kSkaterLoopStart; flags < kSkaterLoopEnd; flags += skater::kRowStride, i += 4) {
        set_word_at(m_, kSkaterUnlockedCopy + static_cast<std::uint32_t>(i), word_at(m_, flags - 4));
        set_word_at(m_, kSkaterFlagsCopy + static_cast<std::uint32_t>(i), word_at(m_, flags));
    }
}

void Career::reset_runtime_state() {
    set_global_word(m_, 0x00533f9c, 1);
    set_global_word(m_, 0x00533fa0, 1);
    for (std::uint32_t address : {0x0056a8e4u, 0x0056a8e8u, 0x0056a8ecu, 0x0056a8f0u, 0x0056a900u, 0x0056a8f4u,
                                  0x0056a8f8u, 0x0056b7f4u, 0x0056b760u, 0x0056db8cu, 0x0056a86cu, 0x0056a868u,
                                  0x0056a890u, 0x0056a904u, 0x0056a88cu}) {
        set_word_at(m_, address, 0);
    }
    int i = 0;
    for (std::uint32_t flags = kSkaterLoopStart; flags < kSkaterLoopEnd; flags += skater::kRowStride, i += 4) {
        set_word_at(m_, flags - 4, word_at(m_, kSkaterUnlockedCopy + static_cast<std::uint32_t>(i)));
        set_word_at(m_, flags, word_at(m_, kSkaterFlagsCopy + static_cast<std::uint32_t>(i)));
    }
}

// ---- options ----------------------------------------------------------------------------------

void Career::reset_controller_defaults(int player_index) {
    const std::uint32_t p = static_cast<std::uint32_t>(player_index);
    set_word_at(m_, 0x0055fc2c + p * 4, 1);  // AutoKickState
    set_word_at(m_, 0x0055fc34 + p * 4, 1);  // VibrationState
    for (std::uint32_t button = 0; button < 4; ++button) {
        set_word_at(m_, 0x0055fc0c + p * 16 + button * 4, button);
    }
}

void Career::options_init() {
    set_global_word(m_, 0x0055fb3c, 0xc);
    set_global_word(m_, 0x0052be44, 0x3ffc);
    set_global_word(m_, 0x0055fb40, 6);
    set_global_word(m_, 0x0052be48, 0x7e);
    set_global_word(m_, 0x0055fcc0, 1);
    set_global_word(m_, 0x0055fcc4, 1);
    for (int player_index = 0; player_index < 2; ++player_index) {
        reset_controller_defaults(player_index);
    }
    std::string horse = m_.cstring(global_word(m_, 0x0052ea98));  // sprintf(0x0055fccc, "HORSE")
    horse.push_back('\0');
    m_.write(0x0055fccc, std::vector<std::uint8_t>(horse.begin(), horse.end()));
    set_global_word(m_, 0x0055fcc8, 5);
    m_.write(0x0055fc5c, std::vector<std::uint8_t>{'A', 'A', 'A', 0});  // sprintf(0x0055fc5c, "AAA")
    for (std::uint32_t i = 0; i < 2; ++i) {
        set_byte_at(m_, 0x0055faf8 + i, 0);
        set_byte_at(m_, 0x0055fb36 + i, 0);
        set_byte_at(m_, 0x0055fb38 + i, 0);
        set_byte_at(m_, 0x0055fb3a + i, 1);
    }
    for (std::uint32_t i = 0; i < 0x14; ++i) {
        set_byte_at(m_, 0x0055fafa + i, 0);
        set_byte_at(m_, 0x0055fb0e + i, i);
        set_byte_at(m_, 0x0055fb22 + i, 1);
    }
}

}  // namespace opentony::runtime::career
