#include "auto_views.hpp"
#include "goals.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

// the competition object's fields
constexpr std::uint32_t kHumans = 0xa4;
constexpr std::uint32_t kCounterJudges = 0xa8;
constexpr std::uint32_t kCounterStandings = 0xac;
constexpr std::uint32_t kCounterEnd = 0xb0;
constexpr std::uint32_t kFlagSound = 0xb4;
constexpr std::uint32_t kOrder = 0xec;
constexpr std::uint32_t kHeat = 0xf4;
constexpr std::uint32_t kUp = 0xf5;
constexpr std::uint32_t kPrevious = 0xf6;
constexpr std::uint32_t kMedals = 0x108;
constexpr std::uint32_t kVideoPlaying = 0x169;
constexpr std::uint32_t kPartBase = 0x04;
constexpr std::uint32_t kPartStride = 0x14;
constexpr std::uint32_t kJudgeBase = 0xb8;
constexpr std::uint32_t kJudgeStride = 8;

}  // namespace

// ---- the competition object ------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t GoalScreens::comp_construct_global() { return comp_construct(competition::kObject); }

std::uint32_t GoalScreens::comp_construct(std::uint32_t self) {
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(kVtRoot));
    // the participants and the judges are vector-constructed (a CRT helper, a seam); their element constructors set the vtable of each element
    engine(seam::crt::FUN_00500400, {self + kPartBase, kPartStride, 8, 0x0041adf0, 0x0041ae00});
    engine(seam::crt::FUN_00500400, {self + kJudgeBase, kJudgeStride, 5, 0x0041ae10, 0x0041ae20});
    goals_competition_comp_at(m_, self).set_field_108(0);
    set_word_at(m_, self + kMedals + 4, 0);
    set_word_at(m_, self + kMedals + 8, 0);
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(goals::kVtCompetition));
    return self;
}

std::uint32_t GoalScreens::comp_destroy(std::uint32_t self) {
    engine(seam::crt::FUN_0050030c, {self + kJudgeBase, kJudgeStride, 5, 0x0041ae20});
    engine(seam::crt::FUN_0050030c, {self + kPartBase, kPartStride, 8, 0x0041ae00});
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(kVtRoot));
    return self;
}

std::uint32_t GoalScreens::comp_delete(std::uint32_t self, std::uint32_t flag) {
    comp_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

std::uint32_t GoalScreens::comp_participant_construct(std::uint32_t self) {
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(kVtRoot));
    return self;
}

std::uint32_t GoalScreens::comp_participant_destroy(std::uint32_t self) {
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(goals::kVtParticipantDead));
    return self;
}

std::uint32_t GoalScreens::comp_judge_destroy(std::uint32_t self) {
    goals_competition_comp_at(m_, self).set_field_0(static_cast<Word>(goals::kVtJudgeDead));
    return self;
}

std::uint32_t GoalScreens::comp_participant_delete(std::uint32_t self, std::uint32_t flag) {
    comp_participant_construct(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

std::uint32_t GoalScreens::comp_judge_delete(std::uint32_t self, std::uint32_t flag) {
    comp_participant_construct(self);   // (0x0041ae20 is the same store as 0x0041ae00)
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00419420: is the replay level flagged (byte 0x00560080 set, or the level's contest flag with game mode 1)
bool GoalScreens::replay_level_flag() const {
    if (global_byte(m_, competition::kHumanMode) != 0) {
        return true;
    }
    const std::int32_t level = global_int(m_, 0x00561c90);
    return byte_at(m_, career::level(level) + career::kLvContest) != 0 && global_int(m_, career::kGameMode) == 1;
}

void GoalScreens::comp_init_if_active(std::uint32_t self) {
    if (replay_level_flag()) {
        comp_init(self);
    }
}

void GoalScreens::comp_init(std::uint32_t self) {
    goals_competition_comp_at(m_, self).set_field_a8(0);
    goals_competition_comp_at(m_, self).set_field_ac(0);
    goals_competition_comp_at(m_, self).set_field_b0(0);
    goals_competition_comp_at(m_, self).set_field_b4(0);
    goals_competition_comp_at(m_, self).set_field_169(0);
    engine(0x004c9320, {global_word(m_, goals::kTickCounter)});            // Utils_InitialRand(the clock)
    goals_competition_comp_at(m_, self).set_field_a4(1);
    const std::int32_t slot = sk_.select_slot(-1);
    set_global_word(m_, goals::kHumanSlots, u(slot));
    // the characters already taken: the humans' manager slots (indexed, as retail does, by the slot)
    std::uint8_t used[0x100] = {};
    const std::uint32_t humans = goals_competition_comp_at(m_, self).field_a4();
    for (std::uint32_t i = 0; i < humans; ++i) {
        const std::uint32_t taken = word_at(m_, goals::kHumanSlots + 4 * i);
        if (taken < sizeof(used)) {
            used[taken] = 1;
        }
    }
    auto rand = [&](std::uint32_t limit) {
        s_.note("rand", {limit});
        return static_cast<std::uint32_t>(s_.value("rand"));
    };
    for (std::uint32_t e = 0; e < 8; ++e) {
        const std::uint32_t part = self + 0x0a + kPartStride * e;       // (the character byte; the fields before and after are relative to it)
        const std::uint32_t seat = word_at(m_, goals::kSeatTable + 4 * e);
        if (seat == 0xffffffff) {
            set_byte_at(m_, part - 2, 0);
            set_byte_at(m_, part, 0xff);
            std::uint32_t pick = 0;
            while (pick < 0x14 && used[pick] != 0) {
                ++pick;
            }
            if (pick < 0x14) {
                set_byte_at(m_, part, pick);
                const int costumes = sk_.count_costume_names(static_cast<int>(pick));
                set_byte_at(m_, part + 1, rand(static_cast<std::uint32_t>(costumes)));
                used[pick] = 1;
            }
            check(byte_at(m_, part) != 0xff, 0x005257b8);                     // "Couldnt allocate character"
            set_byte_at(m_, part - 1, 0xff);
            set_byte_at(m_, part + 3, 0xff);
        } else {
            set_byte_at(m_, part - 2, 1);
            set_byte_at(m_, part, byte_at(m_, goals::kHumanSlots + 4 * seat));
            set_byte_at(m_, part + 1, static_cast<std::uint32_t>(sk_.costume_of_slot(static_cast<int>(seat))));
            set_byte_at(m_, part - 1, seat & 1u);                                // (the C remainder: seat % 2)
            set_byte_at(m_, part + 3, seat);
        }
        set_word_at(m_, part + 4, 0);
        set_half_at(m_, part + 8, 0);
        set_half_at(m_, part + 0x0a, 0);
        set_byte_at(m_, self + kOrder + e, e);
    }
    goals_competition_comp_at(m_, self).set_field_f4(0);
    goals_competition_comp_at(m_, self).set_field_f5(0);
    sk_.select_character(0, static_cast<int>(goals_competition_comp_at(m_, self).field_a()));
    sk_.select_costume(0, static_cast<int>(byte_at(m_, self + kPartBase + kPartStride * goals_competition_comp_at(m_, self).field_f5() + 7)), true);
    comp_.assign_heat_order();
    const std::uint16_t ladder[5] = {0x3cf, 0x398, 0x366, 0x334, 0x208};
    for (std::uint32_t i = 0; i < 5; ++i) {
        set_half_at(m_, competition::kLadder + 2 * i, ladder[i]);
    }
    for (std::uint32_t j = 0; j < 5; ++j) {
        const std::uint32_t judge = self + kJudgeBase + kJudgeStride * j;
        for (;;) {                                                       // a favourite no earlier judge has
            const std::uint32_t pick = rand(8);
            set_byte_at(m_, judge + 4, pick);
            bool again = false;
            for (std::uint32_t k = 0; k < j && !again; ++k) {
                again = byte_at(m_, self + kJudgeBase + kJudgeStride * k + 4) == (pick & 0xff);
            }
            if (!again) {
                break;
            }
        }
        for (;;) {                                                       // a dislike no earlier judge has
            const std::uint32_t pick = rand(8);
            set_byte_at(m_, judge + 6, pick);
            bool again = false;
            for (std::uint32_t k = 0; k < j && !again; ++k) {
                again = byte_at(m_, self + kJudgeBase + kJudgeStride * k + 6) == (pick & 0xff);
            }
            if (!again) {
                break;
            }
        }
        set_byte_at(m_, judge + 5, rand(10));
        set_byte_at(m_, judge + 7, rand(10));
    }
}

void GoalScreens::comp_destroy_medals(std::uint32_t self) {
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::uint32_t medal = word_at(m_, self + kMedals + 4 * k);
        if (medal != 0) {
            vcall_item(kVirtualDelete, medal, 0, {1});
        }
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        set_word_at(m_, self + kMedals + 4 * k, 0);
    }
    const std::uint32_t character = word_at(m_, player0() + career::kPlayerCharacter);
    const std::uint32_t level = global_word(m_, career::kCurrentLevel);
    set_word_at(m_, goals::kMedalTotals + 4 * character, word_at(m_, goals::kMedalTotals + 4 * character) | (1u << (level & 31)));
}

void GoalScreens::menu_line_yes() {
    const std::uint32_t menu = global_word(m_, goals::kPlayerEntry);
    const std::uint32_t name = global_word(m_, goals::kEntryName);
    const std::uint32_t entry = engine_value(0x0044b800, {menu, name}, m_.cstring(name, 0x40));   // CMenu__FindEntry
    engine(0x0044b770, {menu, entry});                                                              // CMenu__SetLine
}

void GoalScreens::clear_pads_and_messages() {
    engine(0x00454e60);                  // PCMOVIE_StopFMV
    engine(0x00489610);                  // Pad_ClearAll
    engine(0x00489670, {0x0056aff8});    // Pad_ClearTriggers
    engine(0x00489670, {0x0056b164});
    engine(0x00489850, {0, 0});          // Pad_ActuatorOff
    engine(0x00489850, {0, 1});
    mess_delete_all();
}

// The state machine after the heats: the ending video plays, then the career goes on. `result` is where the screen's answer goes, `pressed` the button flag.
void GoalScreens::comp_win_lose_logic(std::uint32_t self, std::uint32_t result, std::uint32_t pressed) {
    if (goals_competition_comp_at(m_, self).field_169() != 0) {
        engine(0x004498f0);
        if (engine_value(0x00449800) == 0) {
            return;
        }
        goals_competition_comp_at(m_, self).set_field_169(0);
        comp_destroy_medals(self);
        clear_pads_and_messages();
        const std::uint32_t video = global_word(m_, goals::kFinalVideo);
        if (video == 1) {
            if (sk_.current_character() == 0 && sk_.current_costume() == 2) {
                set_global_word(m_, goals::kEndVideoIndex, 0xffffffdd);
                const std::uint32_t rec = career::record(int_at(m_, player0() + career::kPlayerCharacter));
                set_byte_at(m_, rec + 1, byte_at(m_, rec + 1) | 0x40);
            } else {
                set_global_word(m_, goals::kEndVideoIndex, word_at(m_, skater::row(int_at(m_, player0() + career::kPlayerCharacter)) + skater::kRowVideoId));
            }
            set_word_at(m_, result, 0x30);
            set_global_word(m_, goals::kEndNext, 7);
            return;
        }
        if (video == 2) {
            if ((engine_value(0x00452a80, {7}) & 0xff) == 0) {
                set_word_at(m_, result, 7);
            }
            return;
        }
        check(false, 0x00525814);                                           // "bad final video %d"
        set_word_at(m_, result, 7);
        return;
    }
    if (word_at(m_, pressed) == 0) {
        return;
    }
    if (!comp_.medal_display_done()) {
        return;
    }
    menu_line_yes();
    if (comp_.all_golds_won()) {
        set_global_byte(m_, goals::kEndFlagA, 1);
        set_global_word(m_, goals::kEndFlagB, 7);
        set_global_word(m_, goals::kEndFlagC, 1);
        set_global_word(m_, goals::kEndFlagD, 1);
        s_.note("printf_log", {0x00525804});
        set_global_word(m_, goals::kFinalVideo, 1);
        goals_competition_comp_at(m_, self).set_field_169(1);
        engine(0x004498a0, {0x20, 0});
        return;
    }
    s_.note("printf_log", {0x005257f4});
    set_global_word(m_, goals::kFinalVideo, 2);
    if ((engine_value(0x00452a80, {7}) & 0xff) == 0) {
        set_word_at(m_, result, 7);
    }
    comp_destroy_medals(self);
}

void GoalScreens::comp_end_game_logic(std::uint32_t self, std::uint32_t result, std::uint32_t pressed) {
    auto next_frame = [&] { goals_competition_comp_at(m_, self).set_field_b0(static_cast<Word>(goals_competition_comp_at(m_, self).field_b0() + 4)); };
    if (goals_competition_comp_at(m_, self).field_169() != 0) {
        engine(0x004498f0);
        if (engine_value(0x00449800) == 0) {
            next_frame();                                                    // (the video is still playing)
            return;
        }
        goals_competition_comp_at(m_, self).set_field_169(0);
        menu_line_yes();
        comp_destroy_medals(self);
        clear_pads_and_messages();
        set_global_word(m_, goals::kEndVideoIndex, 0xffffffef);
        set_global_word(m_, goals::kEndNext, 0x24);
        set_word_at(m_, result, 0x30);
        next_frame();
        return;
    }
    engine(0x0042f370);                                                      // Crowd_Stop
    if (word_at(m_, pressed) != 0) {
        if (goals_competition_comp_at(m_, self).field_108() == 0) {
            set_word_at(m_, result, 0x0e);
            comp_destroy_medals(self);
        } else if (comp_.medal_display_done()) {
            if (comp_.all_medals_won()) {
                goals_competition_comp_at(m_, self).set_field_169(1);
                engine(0x004498a0, {0x20, 0});
                next_frame();
                return;
            }
            if ((engine_value(0x00452a80, {7}) & 0xff) == 0) {
                set_word_at(m_, result, 7);
                menu_line_yes();
            }
            comp_destroy_medals(self);
        }
    }
    next_frame();
}

void GoalScreens::comp_reset_marquee(std::uint32_t self) {
    if (goals_competition_comp_at(m_, self).field_a8() == 0) {
        goals_competition_comp_at(m_, self).set_field_b6(0);
        const std::uint32_t player = player0();
        engine(0x004c9320, {word_at(m_, player + 8) + global_word(m_, goals::kTickCounter)});     // Utils_InitialRand
        comp_.judge_score(0);
    }
    goals_competition_comp_at(m_, self).set_field_a8(static_cast<Word>(goals_competition_comp_at(m_, self).field_a8() + 1));
}

}  // namespace opentony::runtime::frontend
