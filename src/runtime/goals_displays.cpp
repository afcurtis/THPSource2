#include "auto_views.hpp"
#include "goals.hpp"

#include <algorithm>
#include <cstdio>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kHumans = 0xa4;
constexpr std::uint32_t kCounterJudges = 0xa8;
constexpr std::uint32_t kCounterStandings = 0xac;
constexpr std::uint32_t kCounterEnd = 0xb0;
constexpr std::uint32_t kFlagSound = 0xb4;
constexpr std::uint32_t kFlagAverage = 0xb6;
constexpr std::uint32_t kFlagCounted = 0xb7;
constexpr std::uint32_t kJudgeScores = 0xe0;
constexpr std::uint32_t kJudgeHigh = 0xea;
constexpr std::uint32_t kJudgeLow = 0xeb;
constexpr std::uint32_t kOrder = 0xec;
constexpr std::uint32_t kHeat = 0xf4;
constexpr std::uint32_t kUp = 0xf5;
constexpr std::uint32_t kMedalMask = 0xf8;
constexpr std::uint32_t kPrize = 0x104;
constexpr std::uint32_t kMedals = 0x108;
constexpr std::uint32_t kMarqueeText = 0x114;
constexpr std::uint32_t kMarqueeScroll = 0x164;
constexpr std::uint32_t kStartSound = 0x168;
constexpr std::uint32_t kVideoPlaying = 0x169;
constexpr std::uint32_t kPaid = 0x16a;

// a participant e: the object + 4 + e * 0x14; the fields are relative to the object
std::uint32_t part(std::uint32_t self, std::uint32_t e) { return self + 4 + 0x14 * e; }

// The total of a participant's three heats: the two best (the compares retail makes, on unsigned words).
std::uint32_t top_two(std::uint32_t s0, std::uint32_t s1, std::uint32_t s2) {
    if (s1 > s0) {
        if (s0 < s2) {
            return s2 + s1;
        }
        return s0 + s1;
    }
    if (s1 == s0) {
        return s0 + s1;
    }
    if (s1 >= s2) {
        return s0 + s1;
    }
    return s0 + s2;
}

std::string score_text(std::int32_t value) { return retail_format("%d.%d", {}, {value / 10, value % 10}); }

}  // namespace

// 0x00419da0: the medal of a win. In the career the prize money is the medal's cash for each medal not won yet, and the career is told.
void GoalScreens::comp_win_lose_display(std::uint32_t self, std::int32_t medal) {
    if (goals_displays_comp_at(m_, self).field_108() != 0) {
        return;
    }
    goals_displays_comp_at(m_, self).set_field_16a(0);
    if (global_int(m_, career::kGameMode) == 1) {
        const std::uint32_t rec = career::record(int_at(m_, player0() + career::kPlayerCharacter));
        const std::int32_t level = global_int(m_, career::kCurrentLevel);
        const std::uint32_t row = career::level(level);
        auto mask = [&] { return half_at(m_, rec + career::kRecLevelMasks + 2 * u(level)); };
        goals_displays_comp_at(m_, self).set_field_104(0);
        bool award = false;
        const std::int32_t sel = medal - 10;
        if (sel == 0 || sel == 1 || sel == 2) {
            if (sel == 0) {
                if ((mask() & 0x400) != 0) {
                    award = true;
                } else {
                    goals_displays_comp_at(m_, self).set_field_104(static_cast<Word>(word_at(m_, row + career::kLvGoldCash)));
                }
            }
            if (!award && sel <= 1) {
                if ((mask() & 0x400) != 0 || (mask() & 0x800) != 0) {
                    award = true;
                } else {
                    goals_displays_comp_at(m_, self).set_field_104(static_cast<Word>(goals_displays_comp_at(m_, self).field_104() + word_at(m_, row + career::kLvSilverCash)));
                }
            }
            if (!award) {
                if ((mask() & 0x400) != 0 || (mask() & 0x800) != 0 || (mask() & 0x1000) != 0) {
                    award = true;
                } else {
                    goals_displays_comp_at(m_, self).set_field_104(static_cast<Word>(goals_displays_comp_at(m_, self).field_104() + word_at(m_, row + career::kLvBronzeCash)));
                }
            }
        }
        set_word_at(m_, player0() + career::kPlayerMedalFlag, 1);
        career_.give_medal(level, medal);
    }
    goals_displays_comp_at(m_, self).set_field_108(static_cast<Word>(medal_create(medal, 0, -5, 0x8c, 0x800, 0xa)));
}

// 0x0041a270: the three-medals screen
void GoalScreens::comp_medals_display(std::uint32_t self) {
    mess_delete_all();
    mess_scale(0x100);
    mess_rgb(0x7a, 0x7a, 0x7a);
    mess_justify(0);
    if (comp_.all_golds_won()) {
        mess_text(0x100, 0x28, text_var(0x0052e63c));
        mess_text(0x100, 0x3c, text_var(0x0052e640));
        mess_text(0x100, 0x50, text_var(0x0052e644));
        mess_text(0x100, 0xb4, text_var(0x0052e648));
        mess_text(0x100, 0xc8, text_var(0x0052e64c));
        if (goals_displays_comp_at(m_, self).field_108() == 0) {
            sfx(0xde);
            sfx(0x30);
            goals_displays_comp_at(m_, self).set_field_108(static_cast<Word>(medal_create(10, -30, 5, 0x8c, 0x600, 3)));
        }
        if (word_at(m_, self + kMedals + 4) == 0) {
            set_word_at(m_, self + kMedals + 4, medal_create(10, 0, 5, 0x8c, 0x600, 3));
        }
        if (word_at(m_, self + kMedals + 8) == 0) {
            set_word_at(m_, self + kMedals + 8, medal_create(10, 30, 5, 0x8c, 0x600, 3));
        }
    } else {
        mess_text(0x100, 0x28, text_var(0x0052e650));
        mess_text(0x100, 0x3c, text_var(0x0052e654));
        mess_text(0x100, 0x50, text_var(0x0052e658));
        mess_text(0x100, 0xb4, text_var(0x0052e65c));
        const std::uint32_t rec = career::record(int_at(m_, player0() + career::kPlayerCharacter));
        std::vector<std::int32_t> types;                                  // the medal each level with one earned (gold, silver or bronze)
        for (std::uint32_t l = 0; l < career::kLevelCount; ++l) {
            const std::uint32_t mask = half_at(m_, rec + career::kRecLevelMasks + 2 * l);
            if ((mask & 0x400) != 0) {
                types.push_back(10);
            } else if ((mask & 0x800) != 0) {
                types.push_back(11);
            } else if ((mask & 0x1000) != 0) {
                types.push_back(12);
            }
        }
        types.resize(std::max<std::size_t>(types.size(), 3), 0);
        if (goals_displays_comp_at(m_, self).field_108() == 0) {
            goals_displays_comp_at(m_, self).set_field_108(static_cast<Word>(medal_create(types[0], -30, 5, 0x8c, 0x500, 5)));
        }
        if (word_at(m_, self + kMedals + 4) == 0) {
            set_word_at(m_, self + kMedals + 4, medal_create(types[1], 0, 5, 0x8c, 0x500, 5));
        }
        if (word_at(m_, self + kMedals + 8) == 0) {
            set_word_at(m_, self + kMedals + 8, medal_create(types[2], 30, 5, 0x8c, 0x500, 5));
        }
    }
    check(goals_displays_comp_at(m_, self).field_108() != 0, 0x00525840);                       // "no medal 1"
    check(word_at(m_, self + kMedals + 4) != 0, 0x00525834);
    check(word_at(m_, self + kMedals + 8) != 0, 0x00525828);
    for (std::uint32_t k = 0; k < 3; ++k) {
        vcall_item("medal_update", word_at(m_, self + kMedals + 4 * k), 3);   // the medal's own update (a power-up: slot 3, Powerup_Update 0x004a8ac0)
    }
    if (comp_.medal_display_done() && goals_displays_comp_at(m_, self).field_169() == 0) {
        engine(0x004dc0c0);
    }
}

// 0x0041a780: the final standings and the winner's text
void GoalScreens::comp_results_display(std::uint32_t self) {
    mess_delete_all();
    if (goals_displays_comp_at(m_, self).field_f4() < 3) {
        do {
            for (std::uint32_t p = 0; p < 8; ++p) {
                comp_.ai_score(static_cast<int>(p));
            }
            goals_displays_comp_at(m_, self).set_field_f4(static_cast<std::uint8_t>(goals_displays_comp_at(m_, self).field_f4() + 1));
            for (std::uint32_t e = 0; e < 8; ++e) {
                const std::uint32_t at = part(self, e);
                set_half_at(m_, at + 0x10, top_two(half_at(m_, at + 0x0a), half_at(m_, at + 0x0c), half_at(m_, at + 0x0e)));
            }
        } while (goals_displays_comp_at(m_, self).field_f4() < 3);
    }
    comp_.sort_ranking();
    for (std::uint32_t i = 0; i < 8; ++i) {
        set_byte_at(m_, part(self, byte_at(m_, self + kOrder + i)) + 8, i);
    }
    const std::uint32_t rank = global_byte(m_, competition::kHumanMode) != 0 ? byte_at(m_, part(self, goals_displays_comp_at(m_, self).field_f5()) + 8) : byte_at(m_, part(self, 0) + 8);
    mess_scale(0x100);
    mess_rgb(0x7a, 0x7a, 0x7a);
    if (global_byte(m_, 0x0056dcad) == 0xff) {
        return;
    }
    if (rank == 0 || rank == 1 || rank == 2) {
        mess_text(0x100, 0x28, text_var(rank == 0 ? 0x0052e660 : rank == 1 ? 0x0052e664 : 0x0052e668));
        comp_win_lose_display(self, rank == 0 ? 0xa : rank == 1 ? 0xb : 0xc);
        if (goals_displays_comp_at(m_, self).field_b4() == 0) {
            sfx(0xde);
            sfx(0x30);
            goals_displays_comp_at(m_, self).set_field_b4(1);
        }
    } else {
        if (goals_displays_comp_at(m_, self).field_b4() == 0) {
            sfx(0x2f);
            goals_displays_comp_at(m_, self).set_field_b4(1);
        }
        mess_text(0x100, 0x28, text_var(0x0052e66c));
        const std::uint32_t order0 = goals_displays_comp_at(m_, self).field_ec(), order1 = byte_at(m_, self + kOrder + 1), order2 = byte_at(m_, self + kOrder + 2);
        std::uint32_t first = order2, second = order2, third = order2;
        for (std::uint32_t e = 0; e < 8; ++e) {
            if (order0 == e) {
                first = e;
            }
            if (order1 == e) {
                second = e;
            }
            if (order2 == e) {
                third = e;
            }
        }
        mess_rgb(0xb6, 0x84, 0x24);
        mess_scale(0xa0);
        const std::uint32_t places[3] = {first, second, third};
        const std::uint32_t texts[3] = {0x0052e670, 0x0052e674, 0x0052e678};
        const std::int32_t ys[3] = {0x3c, 0x46, 0x50};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t name = sk_.display_name(static_cast<int>(byte_at(m_, part(self, places[k]) + 6)));
            mess_line(0x100, ys[k], retail_format("%s: %s", {m_.cstring(name, 0x100), m_.cstring(word_at(m_, texts[k]), 0x100)}, {}));
        }
        mess_rgb(0x7a, 0x7a, 0x7a);
        const std::string rank_line = retail_format("%dth", {}, {static_cast<std::int32_t>(rank) + 1});
        mess_scale(0x180);
        std::string name = m_.cstring(sk_.display_name(static_cast<int>(byte_at(m_, part(self, 0) + 6))), 0x100);
        f_.ellipsize_text(goals::kMessFont, name, 0x1e0);
        mess_line(0x100, 0x78, name);
        mess_line(0x100, 0x96, rank_line);
    }
    const std::uint32_t medal = goals_displays_comp_at(m_, self).field_108();
    if (medal == 0) {
        if (comp_.medal_display_done() && goals_displays_comp_at(m_, self).field_169() == 0) {
            engine(0x004dc0c0);
        }
        return;
    }
    // the medal's own update: retail calls its vtable's slot 3 (a power-up, Powerup_Update 0x004a8ac0). As a bare service it had no address in the standalone, which
    // stopped the game at the end of every competition with a medal
    vcall_item("medal_update", medal, 3);
    const std::uint32_t rec = career::record(int_at(m_, player0() + career::kPlayerCharacter));
    goals_displays_comp_at(m_, self).set_field_f8(0);
    std::int32_t level = global_int(m_, career::kCurrentLevel);
    if ((half_at(m_, rec + career::kRecLevelMasks + 2 * u(level)) & 0x1c00) != 0) {
        goals_displays_comp_at(m_, self).set_field_f8(static_cast<Word>(1u << (u(level) & 31)));
        level = global_int(m_, career::kCurrentLevel);
    }
    std::uint32_t slot = 0;
    for (std::uint32_t l = 0; l < career::kLevelCount; ++l) {
        if (byte_at(m_, career::level(static_cast<int>(l)) + career::kLvContest) != 0) {
            set_word_at(m_, goals::kContestLevelMasks + 4 * slot, 1u << l);
            ++slot;
        }
    }
    const bool last_level = level == 7;
    const std::int32_t prize = static_cast<std::int32_t>(goals_displays_comp_at(m_, self).field_104());
    if (prize != 0 && global_word(m_, goals::kEndSettle) == 0) {
        comp_prize_money_display(self, prize);
    }
    mess_scale(0x100);
    mess_rgb(0x7a, 0x7a, 0x7a);
    if (last_level) {
        if (global_word(m_, goals::kEndSettle) == 0) {
            if (goals_displays_comp_at(m_, self).field_b0() > 0xb4) {
                set_global_word(m_, goals::kEndSettle, 1);
                sfx(0xde);
                sfx(0x30);
            } else {
                set_word_at(m_, goals_displays_comp_at(m_, self).field_108() + 0xfc, 0);
            }
        } else {
            const std::uint32_t object = goals_displays_comp_at(m_, self).field_108();
            set_half_at(m_, object + 0xec, 2);
            set_half_at(m_, object + 0xea, 2);
            set_word_at(m_, object + 0x0c, word_at(m_, object + 0xe0) - 0x5000);
            mess_text(0x100, 0x9b, text_var(0x0052e2ec));
            mess_text(0x100, 0xaf, text_var(0x0052e2f0));
            mess_text(0x100, 0xc3, text_var(0x0052e2f4));
        }
    }
    if (comp_.medal_display_done() && goals_displays_comp_at(m_, self).field_169() == 0) {
        engine(0x004dc0c0);
    }
}

// 0x0041af40: the standings table: names, the three heats' scores and the total, counting up
void GoalScreens::comp_marquee_display(std::uint32_t self) {
    mess_delete_all();
    mess_scale(0x100);
    mess_justify(0);
    mess_rgb(0xb6, 0x84, 0x24);
    mess_line(0x100, 0x32, m_.cstring(global_word(m_, 0x0052e67c), 0x100));
    mess_justify(1);
    mess_scale(0x78);
    mess_text(0x130, 0x3e, text_var(0x0052e680));
    mess_line(0xe8, 0x46, retail_format("    1        2      3        %s", {m_.cstring(global_word(m_, 0x0052e684), 0x100)}, {}));
    mess_scale(200);
    const std::uint32_t up = goals_displays_comp_at(m_, self).field_f5();
    std::int32_t y = 0x50;
    for (std::uint32_t i = 0; i < 8; ++i, y += 0xe) {
        const std::uint32_t p = byte_at(m_, self + kOrder + i);
        std::string name = m_.cstring(sk_.display_name(static_cast<int>(byte_at(m_, part(self, p) + 6))), 0x100);
        f_.ellipsize_text(goals::kMessFont, name, 200);
        if (p == up) {
            mess_rgb(0xb6, 0x84, 0x24);
        } else {
            mess_rgb(0x7a, 0x7a, 0x7a);
        }
        mess_line(0x1e, y, name);
    }
    auto gold_or_grey = [&](std::uint32_t p) {
        if (p == up) {
            mess_rgb(0xb6, 0x84, 0x24);
        } else {
            mess_rgb(0x7a, 0x7a, 0x7a);
        }
    };
    auto score = [&](std::uint32_t p, std::uint32_t h) { return static_cast<std::int32_t>(half_at(m_, self + 0x0e + 0x14 * p + 2 * h)); };
    auto total_word = [&](std::uint32_t p) { return self + 0x14 + 0x14 * p; };
    std::int32_t x = 0xf2;
    for (std::uint32_t h = 0; h < 3; ++h, x += 0x32) {
        std::int32_t row_y = 0x50;
        std::uint32_t k = 0;
        for (std::uint32_t r = 0; r < 8; ++r, k += 0x14, row_y += 0xe) {
            const std::uint32_t counter = goals_displays_comp_at(m_, self).field_ac();
            if (counter > k) {
                if (counter == k + 1) {
                    sfx(0x34);
                }
                const std::uint32_t p = byte_at(m_, self + kOrder + r);
                if (goals_displays_comp_at(m_, self).field_f4() == 0) {
                    set_half_at(m_, total_word(p), half_at(m_, self + 0x0e + 0x14 * p));
                } else {
                    set_half_at(m_, total_word(p), top_two(half_at(m_, self + 0x0e + 0x14 * p), half_at(m_, self + 0x10 + 0x14 * p), half_at(m_, self + 0x12 + 0x14 * p)));
                }
            }
            comp_.sort_ranking();
            std::uint32_t shake = 0;
            if (goals_displays_comp_at(m_, self).field_f4() == 2) {
                if (counter < k + 0x14) {
                    if (counter >= k) {
                        shake = (counter % 0x14) * 4;
                    }
                } else if (counter >= k) {
                    shake = 0x64;
                }
            }
            const std::uint32_t p = byte_at(m_, self + kOrder + r);
            const std::uint32_t s0 = half_at(m_, self + 0x0e + 0x14 * p), s1 = half_at(m_, self + 0x10 + 0x14 * p), s2 = half_at(m_, self + 0x12 + 0x14 * p);
            std::uint32_t dropped;                                         // the heat with the lowest score
            if (s1 < s0) {
                dropped = s1 <= s2 ? 1 : 2;
            } else if (s0 <= s2) {
                dropped = 0;
            } else if (s1 > s0) {
                dropped = 2;
            } else {
                dropped = s1 <= s2 ? 1 : 2;
            }
            if (h == dropped) {
                const std::uint32_t c = (0x7a - shake) & 0xff;
                mess_rgb(c, c, c);
            } else {
                gold_or_grey(p);
            }
            const std::uint32_t heat = goals_displays_comp_at(m_, self).field_f4();
            if (h < heat) {
                mess_line(x, row_y, score_text(score(p, h)));
            }
            if (h == heat) {
                std::string text;
                if (goals_displays_comp_at(m_, self).field_ac() > k) {
                    text = score_text(score(p, h));
                } else {
                    gold_or_grey(p);
                    text = "-";
                }
                mess_line(x, row_y, text);
                gold_or_grey(p);
                if (goals_displays_comp_at(m_, self).field_ac() > k || goals_displays_comp_at(m_, self).field_f4() > 0) {
                    text = score_text(static_cast<std::int32_t>(half_at(m_, total_word(p))));
                } else {
                    text = "-";
                }
                mess_line(0x1a6, row_y, text);
            }
            if (h > heat) {
                mess_line(x + 0x14, row_y, "-");
            }
        }
    }
    goals_displays_comp_at(m_, self).set_field_b7(static_cast<std::uint8_t>(goals_displays_comp_at(m_, self).field_ac() > 0xa0 ? 1 : 0));
    mess_scale(0x100);
    if (goals_displays_comp_at(m_, self).field_b7() != 0) {
        if (global_byte(m_, competition::kHumanMode) != 0) {
            const std::uint32_t slot_id = byte_at(m_, part(self, goals_displays_comp_at(m_, self).field_f5()) + 9);
            const std::uint32_t humans = goals_displays_comp_at(m_, self).field_a4();
            std::uint32_t next = (slot_id + 2) & 0xff;
            if (next > humans) {
                next = 1;
            }
            const bool more = (goals_displays_comp_at(m_, self).field_f4() + 1 < 3) || (slot_id + 1 < humans);
            if (more) {
                const std::string line = retail_format(m_.cstring(global_word(m_, 0x0052e688), 0x100), {}, {static_cast<std::int32_t>(next)});
                mess_justify(0);
                mess_rgb(0xb6, 0x84, 0x24);
                mess_line(0x100, 0xc6, line);
            }
        }
        engine(0x004dc0c0);
    }
}

// 0x0041b590: the heat is over: the judges' scores one by one, then their average
void GoalScreens::comp_end_game_display(std::uint32_t self) {
    mess_delete_all();
    mess_scale(0x100);
    mess_justify(0);
    mess_rgb(0xb6, 0x84, 0x24);
    const std::uint32_t slot_id = byte_at(m_, part(self, goals_displays_comp_at(m_, self).field_f5()) + 9);
    mess_line(0x100, 0x28, retail_format(m_.cstring(global_word(m_, 0x0052e690), 0x100), {}, {static_cast<std::int32_t>(slot_id) + 1, static_cast<std::int32_t>(goals_displays_comp_at(m_, self).field_f4()) + 1}));
    mess_text(0x100, 0x46, text_var(0x0052e694));
    mess_rgb(0x7a, 0x7a, 0x7a);
    mess_justify(1);
    const std::uint32_t counter = goals_displays_comp_at(m_, self).field_a8();
    std::int32_t ramp = 0;
    if (counter > 0x4b) {
        ramp = std::min<std::int32_t>(static_cast<std::int32_t>(counter * 4) - 0x12c, 0x64);
    }
    std::uint32_t threshold = 0;
    std::int32_t y = 0x5a;
    for (std::uint32_t j = 0; j < 5 && y < 0xbe; ++j, y += 0x14, threshold += 0xf) {
        if (goals_displays_comp_at(m_, self).field_a8() == threshold + 1) {
            sfx(0x34);
        }
        const std::string label = retail_format("%s %d", {m_.cstring(global_word(m_, 0x0052e698), 0x100)}, {static_cast<std::int32_t>(j) + 1});
        if (j != goals_displays_comp_at(m_, self).field_eb() && j != goals_displays_comp_at(m_, self).field_ea()) {
            mess_rgb(0x7a, 0x7a, 0x7a);
        } else {
            const std::uint32_t c = (0x7a - static_cast<std::uint32_t>(ramp)) & 0xff;
            mess_rgb(c, c, c);                                               // (the dropped scores fade out)
        }
        mess_line(0x96, y, label);
        if (goals_displays_comp_at(m_, self).field_a8() > threshold) {
            mess_line(0x136, y, score_text(short_at(m_, self + kJudgeScores + 2 * j)));
        }
    }
    if (ramp >= 0x64) {
        if (goals_displays_comp_at(m_, self).field_b6() == 0) {
            sfx(0x33);
        }
        mess_justify(0);
        mess_rgb(0xb6, 0x84, 0x24);
        std::int32_t sum = 0;
        for (std::uint32_t j = 0; j < 5; ++j) {
            if (j != goals_displays_comp_at(m_, self).field_eb() && j != goals_displays_comp_at(m_, self).field_ea()) {
                sum += short_at(m_, self + kJudgeScores + 2 * j);
            }
        }
        const std::int32_t average = sum / 3;
        mess_justify(0);
        mess_line(0xc8, 0xc3, retail_format("%s %d %s  -  ", {m_.cstring(global_word(m_, 0x0052e680), 0x100), m_.cstring(global_word(m_, 0x0052e6a4), 0x100)},
                                            {static_cast<std::int32_t>(goals_displays_comp_at(m_, self).field_f4()) + 1}));
        const std::string text = score_text(average);
        mess_scale(0x180);
        mess_line(0x163, 0xc3, text);
        goals_displays_comp_at(m_, self).set_field_b6(1);
    } else {
        goals_displays_comp_at(m_, self).set_field_b6(0);
    }
    mess_scale(0x100);
    engine(0x004dc0c0);
}

// 0x0041b8b0: the heat start: "Player n get ready" / "go" and the marquee text sliding across
void GoalScreens::comp_start_display(std::uint32_t self) {
    if (goals_displays_comp_at(m_, self).field_164() == 0 && goals_displays_comp_at(m_, self).field_f4() == 0 && goals_displays_comp_at(m_, self).field_168() == 0 && global_byte(m_, competition::kHumanMode) == 0) {
        sfx(0x158);
        goals_displays_comp_at(m_, self).set_field_168(1);
    }
    if (goals_displays_comp_at(m_, self).field_114() == 0) {
        return;
    }
    if (goals_displays_comp_at(m_, self).field_a4() > 1) {
        mess_rgb(0x7a, 0x7a, 0x7a);
        std::int32_t scale = static_cast<std::int32_t>(goals_displays_comp_at(m_, self).field_164()) * 2;
        scale = scale < 0x32 ? 0x32 : (scale > 0x15e ? 0x15e : scale);
        mess_scale(u(scale));
        mess_justify(0);
        const std::int32_t player = static_cast<std::int32_t>(byte_at(m_, part(self, goals_displays_comp_at(m_, self).field_f5()) + 9)) + 1;
        std::string line;
        if (goals_displays_comp_at(m_, self).field_164() > 0x258) {
            mess_rgb(0x7a, 0xff, 0x7a);
            line = retail_format(m_.cstring(global_word(m_, 0x0052e69c), 0x100), {}, {player});
        } else {
            mess_rgb(0x7a, 0x7a, 0x7a);
            line = retail_format(m_.cstring(global_word(m_, 0x0052e6a0), 0x100), {}, {player});
        }
        mess_line(0x100, 0x78, line);
    }
    mess_rgb(0x7a, 0x7a, 0x7a);
    mess_scale(0x100);
    mess_justify(1);
    if (goals_displays_comp_at(m_, self).field_164() > 0x300) {
        goals_displays_comp_at(m_, self).set_field_164(0);
    }
    mess_text(0x200 - static_cast<std::int32_t>(goals_displays_comp_at(m_, self).field_164()), 0xc8, goals_displays_comp_at(m_, self).field_114());
    goals_displays_comp_at(m_, self).set_field_164(static_cast<Word>(goals_displays_comp_at(m_, self).field_164() + 5));
}

// 0x0041ba50: the prize money: six digits settle one at a time from the left, then the cash is paid
void GoalScreens::comp_prize_money_display(std::uint32_t self, std::int32_t cash) {
    check(cash > 0, 0x005258b4);                                          // "tried to show zero or less cash!"
    const std::int32_t counter = goals_displays_comp_at(m_, self).field_b0();
    const std::uint32_t colour_r = (u(cash) & 0xffffff00u) | static_cast<std::uint8_t>(counter % 0x86 + 0x7a);
    char digits[32];
    std::snprintf(digits, sizeof(digits), "%6.6d", cash);
    mess_scale(0xa0);
    mess_rgb(0x7a, 0x7a, 0x7a);
    mess_text(0x100, 0xa0, text_var(0x0052e68c));
    mess_scale(0x100);
    mess_line(0xb0, 0xb4, "$");
    if (counter < 0x82 && counter > 0 && counter % 0x14 == 0) {
        sfx(0x34);
    }
    struct Column {
        std::int32_t settle;      // the counter at which the digit starts to settle
        std::int32_t x;
        std::int32_t over;        // the scale ramps down to 0x100 by settle + 10, up from settle
        std::int32_t under;
        std::int32_t cash_over;   // the column is drawn when the cash is above this
    };
    static const Column columns[6] = {{0x78, 0xc4, 0x1960, 0x1580, 99999}, {0x64, 0xd8, 0x15a0, 0x11c0, 9999}, {0x50, 0xec, 0x11e0, 0xe00, 999},
                                      {0x3c, 0x114, 0xe20, 0xa40, 99}, {0x28, 0x128, 0xa60, 0x680, 9}, {0x14, 0x13c, 0x6a0, 0x2c0, -1}};
    for (int c = 0; c < 6; ++c) {
        const Column& col = columns[c];
        std::string text;
        if (counter >= col.settle) {
            text = std::string(1, digits[c]);
            std::int32_t scale;
            if (counter > col.settle + 10) {
                scale = 0x100;
            } else if (counter > col.settle + 5) {
                scale = col.over - counter * 0x30;
            } else {
                scale = counter * 0x30 - col.under;
            }
            mess_scale(u(scale));
            mess_rgb(colour_r, 0x7a, 0x7a);
        } else {
            s_.note("rnd", {10});
            text = std::to_string(s_.value("rnd"));
        }
        if (c == 5 || cash > col.cash_over) {
            mess_line(col.x, 0xb4, text);
        }
        mess_scale(0x100);
        if (c == 5) {
            if (cash > 999) {
                mess_line(0x100, 0xba, ",");
            }
        }
    }
    if (goals_displays_comp_at(m_, self).field_16a() == 0) {
        const std::uint32_t rec = career::record(int_at(m_, player0() + career::kPlayerCharacter));
        set_word_at(m_, rec + 4, word_at(m_, rec + 4) + u(cash));
        set_word_at(m_, rec + 8, word_at(m_, rec + 8) + u(cash));
        goals_displays_comp_at(m_, self).set_field_16a(1);
    }
}

}  // namespace opentony::runtime::frontend
