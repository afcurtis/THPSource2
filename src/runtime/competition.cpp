#include "auto_views.hpp"
#include "competition.hpp"
#include "host_options.hpp"

#include <stdexcept>

namespace opentony::runtime::competition {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::uint32_t kPlayers = career::kPlayers;
constexpr std::uint32_t kHumanFlag = 0x08;      // + participant * 0x14 from the object
constexpr std::uint32_t kBracket = 0x0c;
constexpr std::uint32_t kParticipantStride = 0x14;
constexpr std::uint32_t kScores = 0x0e;         // u16 x 3 per participant, stride 10 u16
constexpr std::uint32_t kJudgeBonus = 0xbd;     // judge j: +0xbc favoured, +0xbd bonus, +0xbe disliked, +0xbf penalty
constexpr std::uint32_t kJudgeScores = 0xe0;
constexpr std::uint32_t kJudgeLow = 0xeb;
constexpr std::uint32_t kJudgeHigh = 0xea;
constexpr std::uint32_t kOrder = 0xec;
constexpr std::uint32_t kHeat = 0xf4;
constexpr std::uint32_t kUp = 0xf5;
constexpr std::uint32_t kPrevious = 0xf6;
constexpr std::uint32_t kHumans = 0xa4;
constexpr std::uint32_t kPlayerDifficulty = 0x174;  // u16 in the skater object: scales the random deduction

// Unsigned division as retail does it: a zero divisor faults the process.
std::uint32_t udiv(std::uint32_t a, std::uint32_t b) {
    if (b == 0) {
        throw std::domain_error("retail divides by zero here");
    }
    return a / b;
}

}  // namespace

int Competition::rand(int limit) {
    s_.note("rand", {static_cast<std::uint32_t>(limit)});
    return s_.value("rand");
}

bool Competition::all_medals_won() {
    const std::uint32_t rec = career::record(int_at(m_, global_word(m_, kPlayers) + career::kPlayerCharacter));
    std::uint32_t count = 0;
    for (int l = 0; l < static_cast<int>(career::kLevelCount); ++l) {
        if (byte_at(m_, career::level(l) + career::kLvContest) != 0 &&
            (half_at(m_, rec + career::kRecLevelMasks + static_cast<std::uint32_t>(l) * 2) & 0x1c00) != 0) {
            ++count;
        }
    }
    s_.check(count < 4, kMsgTooManyMedals);
    return count == 3;
}

bool Competition::all_golds_won() {
    const std::uint32_t rec = career::record(int_at(m_, global_word(m_, kPlayers) + career::kPlayerCharacter));
    std::uint32_t count = 0;
    for (int l = 0; l < static_cast<int>(career::kLevelCount); ++l) {
        if (byte_at(m_, career::level(l) + career::kLvContest) != 0 &&
            (half_at(m_, rec + career::kRecLevelMasks + static_cast<std::uint32_t>(l) * 2) & 0x400) != 0) {
            ++count;
        }
    }
    s_.check(count < 4, kMsgTooManyMedals);
    return count == 3;
}

void Competition::assign_heat_order() {
    for (std::uint32_t e = 0; e < 8; ++e) {
        set_byte_at(m_, kObject + kBracket + e * kParticipantStride, 0xff);
    }
    // The computer riders are dealt brackets 0 .. n-1 in a random order among the free seats.
    for (std::int8_t bracket = static_cast<std::int8_t>(8 - static_cast<std::int8_t>(global_byte(m_, kObject + kHumans))); bracket != 0;
         --bracket) {
        std::int8_t pick = static_cast<std::int8_t>(rand(bracket));
        std::uint8_t seat = 0;
        do {
            const std::uint32_t base = kObject + static_cast<std::uint32_t>(seat) * kParticipantStride;
            if (byte_at(m_, base + kHumanFlag) == 0 && byte_at(m_, base + kBracket) == 0xff) {
                if (pick == 0) {
                    set_byte_at(m_, base + kBracket, static_cast<std::uint32_t>(bracket - 1));
                    seat = 8;
                }
                --pick;
            }
            ++seat;
        } while (seat < 8);
    }
}

void Competition::judge_score(int participant_arg) {
    const std::uint8_t participant = static_cast<std::uint8_t>(participant_arg);
    s_.check(true, kMsgJudgeCount);
    const std::uint32_t player = global_word(m_, kPlayers);
    const std::uint32_t score = word_at(m_, player + career::kPlayerScore);
    std::uint32_t t_mid;   // uVar13
    std::uint32_t t_high;  // uVar8
    std::uint32_t t_low;   // uVar10
    // the level whose judging applies: the level in play, or the one the host asks for (host_options.hpp judge_level: the quick start's Competition Session)
    const std::int32_t judged = host_options().judge_level >= 0 ? host_options().judge_level : global_int(m_, career::kCurrentLevel);
    if (global_byte(m_, kHumanMode) == 0) {
        const std::uint32_t row = career::level(judged);
        t_high = word_at(m_, row + 0x54);
        t_low = word_at(m_, row + 0x58);
        t_mid = word_at(m_, row + 0x50);
    } else {
        const std::uint32_t row = kJudgeRows + global_word(m_, kSetIndex) * 0x20;
        t_high = word_at(m_, row + 4);
        t_low = word_at(m_, row + 8);
        t_mid = word_at(m_, row);
    }
    const std::uint32_t l0 = global_half(m_, kLadder);
    const std::uint32_t l1 = global_half(m_, kLadder + 2);
    const std::uint32_t l2 = global_half(m_, kLadder + 4);
    const std::uint32_t l3 = global_half(m_, kLadder + 6);

    for (std::uint32_t judge = 0; judge < 5; ++judge) {
        const std::uint32_t at = kObject + kJudgeScores + judge * 2;
        const std::uint32_t extra = kObject + kJudgeBonus + judge * 8;
        std::uint32_t r;
        std::int16_t anchor;
        if (t_mid * 2 < score) {
            r = (udiv((score + t_mid * 0xffffe) * 0x1000, t_mid * 3) * (1000 - l0)) >> 12;
            anchor = static_cast<std::int16_t>(l0);
        } else if (t_mid < score) {
            r = (udiv((t_mid * 0xfffff + score) * 0x1000, t_mid) * (l0 - l1)) >> 12;
            anchor = static_cast<std::int16_t>(l1);
        } else if (t_high < score) {
            r = (udiv((t_high * 0xfffff + score) * 0x1000, t_mid - t_high) * (l1 - l2)) >> 12;
            anchor = static_cast<std::int16_t>(l2);
        } else if (t_low < score) {
            r = (udiv((t_low * 0xfffff + score) * 0x1000, t_high - t_low) * (l2 - l3)) >> 12;
            anchor = static_cast<std::int16_t>(l3);
        } else {
            r = (udiv(score << 12, t_low) * (l3 - 300)) >> 12;
            anchor = 300;
        }
        std::int16_t value = static_cast<std::int16_t>(static_cast<std::int16_t>(r) + anchor);
        set_half_at(m_, at, static_cast<std::uint16_t>(value));

        std::uint32_t deduction;
        if (judged == 2) {
            deduction = static_cast<std::uint32_t>(rand(3) + 5);
        } else {
            deduction = static_cast<std::uint32_t>(rand(10) + 10);
        }
        deduction *= half_at(m_, player + kPlayerDifficulty);
        if (300 < deduction) {
            deduction = 300;
        }
        if (deduction < static_cast<std::uint32_t>(static_cast<std::int32_t>(short_at(m_, at)))) {
            set_half_at(m_, at, static_cast<std::uint16_t>(short_at(m_, at) - static_cast<std::int16_t>(deduction)));
        } else {
            set_half_at(m_, at, 0);
        }
        if (8 < short_at(m_, at)) {
            const std::int16_t jitter = static_cast<std::int16_t>(rand(8));
            set_half_at(m_, at, static_cast<std::uint16_t>(short_at(m_, at) - jitter));
        }
        if (byte_at(m_, extra - 1) == participant) {
            set_half_at(m_, at, static_cast<std::uint16_t>(short_at(m_, at) + static_cast<std::int16_t>(byte_at(m_, extra))));
        }
        if (byte_at(m_, extra + 1) == participant) {
            if (static_cast<std::int16_t>(byte_at(m_, extra + 2)) < short_at(m_, at)) {
                set_half_at(m_, at, static_cast<std::uint16_t>(short_at(m_, at) - static_cast<std::int16_t>(byte_at(m_, extra + 2))));
            }
        }
        if (short_at(m_, at) < 0) {
            set_half_at(m_, at, 0);
        }
        if (999 < short_at(m_, at)) {
            set_half_at(m_, at, 999);
        }
        if (score == 0) {
            set_half_at(m_, at, 0);
        }
    }

    // Drop the lowest and the highest judge and average the other three.
    set_global_byte(m_, kObject + kJudgeLow, 0);
    for (int i = 0; i < 5; ++i) {
        if (short_at(m_, kObject + kJudgeScores + static_cast<std::uint32_t>(i) * 2) <
            short_at(m_, kObject + kJudgeScores + static_cast<std::uint32_t>(global_byte(m_, kObject + kJudgeLow)) * 2)) {
            set_global_byte(m_, kObject + kJudgeLow, static_cast<std::uint32_t>(i));
        }
    }
    const std::uint8_t low = global_byte(m_, kObject + kJudgeLow);
    set_global_byte(m_, kObject + kJudgeHigh, low == 0 ? 1 : 0);
    for (int i = 0; i < 5; ++i) {
        if (i != low && short_at(m_, kObject + kJudgeScores + static_cast<std::uint32_t>(global_byte(m_, kObject + kJudgeHigh)) * 2) <
                            short_at(m_, kObject + kJudgeScores + static_cast<std::uint32_t>(i) * 2)) {
            set_global_byte(m_, kObject + kJudgeHigh, static_cast<std::uint32_t>(i));
        }
    }
    std::int32_t sum = 0;
    for (int i = 0; i < 5; ++i) {
        if (i != low && i != global_byte(m_, kObject + kJudgeHigh)) {
            sum += short_at(m_, kObject + kJudgeScores + static_cast<std::uint32_t>(i) * 2);
        }
    }
    const std::uint32_t slot = kObject + kScores + (static_cast<std::uint32_t>(global_byte(m_, kObject + kHeat)) + participant * 10u) * 2;
    set_half_at(m_, slot, static_cast<std::uint16_t>(sum / 3));
}

void Competition::ai_score(int participant_arg) {
    const std::uint8_t participant = static_cast<std::uint8_t>(participant_arg);
    const std::uint32_t base = kObject + participant * kParticipantStride;
    if (byte_at(m_, base + kHumanFlag) != 0) {
        return;
    }
    const std::uint8_t bracket = byte_at(m_, base + kBracket);
    std::uint32_t value;
    if (bracket == 0) {
        value = global_half(m_, kLadder + 2);
    } else if (bracket == 1) {
        value = global_word(m_, kLadder + 4);   // retail reads the whole dword here; only the low 16 bits are stored
    } else if (bracket == 2) {
        value = global_half(m_, kLadder + 6);
    } else {
        const std::uint32_t floor = global_word(m_, kLadder + 8);
        value = static_cast<std::uint32_t>(rand(static_cast<int>(global_half(m_, kLadder + 6) - (floor & 0xffff)))) + floor;
    }
    value -= static_cast<std::uint32_t>(rand(0xf));
    for (std::uint32_t judge = 0; judge < 5; ++judge) {
        const std::uint32_t at = kObject + kJudgeBonus + judge * 8;
        if (participant == byte_at(m_, at - 1)) {
            value += byte_at(m_, at);
        }
        if (participant == byte_at(m_, at + 1)) {
            value -= byte_at(m_, at + 2);
        }
    }
    set_half_at(m_, kObject + kScores + (static_cast<std::uint32_t>(global_byte(m_, kObject + kHeat)) + participant * 10u) * 2, static_cast<std::uint16_t>(value));
}

bool Competition::medal_display_done() {
    const std::uint32_t medal = global_word(m_, kObject + 0x108);
    return !(medal != 0 && word_at(m_, medal + 0xfc) < 0x28);
}

bool Competition::next_human() {
    set_global_byte(m_, kObject + kPrevious, global_byte(m_, kObject + kUp));
    std::uint8_t seat;
    do {
        seat = static_cast<std::uint8_t>(global_byte(m_, kObject + kUp) + 1);
        set_global_byte(m_, kObject + kUp, seat);
        if (byte_at(m_, kObject + kHumanFlag + static_cast<std::uint32_t>(seat) * kParticipantStride) != 0) {
            break;
        }
    } while (seat < 8);
    set_global_word(m_, kObject + 0xb0, 0);
    set_global_word(m_, kObject + 0xac, 0);
    set_global_word(m_, kObject + 0xa8, 0);
    set_global_byte(m_, kObject + 0xb4, 0);
    set_global_byte(m_, kObject + 0xb6, 0);
    set_global_byte(m_, kObject + 0xb7, 0);
    bool found = seat < 8;
    if (!found) {
        set_global_byte(m_, kObject + kUp, 0);
    }
    if (global_byte(m_, kObject + kUp) != global_byte(m_, kObject + kPrevious)) {
        set_global_byte(m_, kScoreChanged, 1);
    }
    return found;
}

std::int32_t Competition::tag_stat_modifier(std::uint32_t skater, int stat, std::int32_t value) {
    s_.check(global_int(m_, career::kGameMode) == 9, kMsgNotTag);
    const std::uint32_t it = global_word(m_, kTagIt);
    if (skater == it) {
        return value;
    }
    std::int32_t score = int_at(m_, it + career::kPlayerScore);
    if (score < 0) {
        score = 0;
    } else if (12000 < score) {
        score = 12000;
    }
    const std::int32_t handicap = (score * 0x78) / 12000;
    value -= handicap;
    if (stat == 3) {
        return value - handicap;
    }
    return value < 0 ? 0 : value;
}

void Competition::sort_ranking() {
    // Selection sort of the ranking order by total score, highest first.
    auto total = [&](std::uint32_t participant) {
        return half_at(m_, kObject + (participant + 1) * kParticipantStride);
    };
    for (std::uint32_t i = 0; i < 7; ++i) {
        for (std::uint32_t j = i; j < 8; ++j) {
            const std::uint8_t first = byte_at(m_, kObject + kOrder + i);
            if (total(first) < total(byte_at(m_, kObject + kOrder + j))) {
                set_byte_at(m_, kObject + kOrder + i, byte_at(m_, kObject + kOrder + j));
                set_byte_at(m_, kObject + kOrder + j, first);
            }
        }
    }
}

}  // namespace opentony::runtime::competition
