// Unit tests of the native career select, handicap and multi-player select screens (Phase 4.10f3) on hand-built memory. The retail forced-call replay
// (select_screens_retail_calls_test.cpp) is the evidence the functions match the original; these tests pin the behaviour a reader would expect: what each choice
// of the career screen does, how the sound's pan slides, how a handicap changes and when the two players are ready, and what the multi-player list answers.
#include "career_select.hpp"
#include "handicap.hpp"
#include "level_select.hpp"
#include "multi_select.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::frontend;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr std::uint32_t kHeap = 0x10000000;
constexpr std::uint32_t kWindow = kHeap + 0x10000;
constexpr std::uint32_t kList = kHeap + 0x20000;
constexpr std::uint32_t kItems = kHeap + 0x21000;
constexpr std::uint32_t kPanelA = kHeap + 0x30000;
constexpr std::uint32_t kPanelB = kHeap + 0x31000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    FontTools fonts{mem, svc};
    WindowBase window{mem, svc, fonts};
    MenuToolkit menu{mem, svc, fonts, window};
    skater::SkaterManager skaters{mem, svc};
    CareerSelect career{mem, svc, fonts, window, menu, skaters};
    HandicapScreen handicap{mem, svc, fonts, window, menu, skaters};
    MultiSelect multi{mem, svc, fonts, window, menu, skaters};
    LevelSelect level{mem, svc, fonts, window, menu, skaters};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.add_region(0x029d0000, 0x00010000);
        mem.add_region(kHeap, 0x00100000);
    }
    std::vector<RetailServices::Call> calls_named(const std::string& name) const {
        std::vector<RetailServices::Call> out;
        for (const auto& call : svc.calls()) {
            if (call.name == name) {
                out.push_back(call);
            }
        }
        return out;
    }
    // A list (a menu list object's child array) of rows with these ids; the row at `highlighted` is the lit one.
    void make_list(std::uint32_t list, const std::vector<std::uint32_t>& ids, std::size_t highlighted) {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint32_t row = kItems + 0x100 * static_cast<std::uint32_t>(i);
            mem.set32(row + 8, ids[i]);
            mem.set8(row + 0xe, i == highlighted ? 1 : 0);
            mem.set32(list + 0x14 + 4 * static_cast<std::uint32_t>(i), row);
        }
    }
};

void test_career_select_choices() {
    // (id 0 is New career: it clears the career flag, resets the save image and goes on; 3 is Continue: it goes on)
    {
        World w;
        w.mem.set32(kWindow + 0x348, kList);
        w.make_list(kList, {3, 0, 1}, 1);
        w.mem.set32(kWindow + 0x4d8, 0xffffffffu);
        w.mem.set8(0x0052cb3d, 1);
        w.mem.set8(kPadConfirmEdge, 1);
        CHECK(w.career.poll(kWindow) == 1);
        CHECK(w.mem.u8(0x0052cb3d) == 0 && w.mem.u8(kPadConfirmEdge) == 0);
        CHECK(w.calls_named("engine_004138f0").size() == 1);
    }
    {
        World w;
        w.mem.set32(kWindow + 0x348, kList);
        w.make_list(kList, {3, 0, 1}, 2);
        w.mem.set32(kWindow + 0x4d8, 0xffffffffu);
        w.mem.set8(kPadConfirmEdge, 1);
        CHECK(w.career.poll(kWindow) == 0);                     // Load career opens the card in load mode
        CHECK(w.mem.s32(kWindow + 0x4d8) == 1);
        CHECK(w.calls_named("engine_0047a6c0").size() == 1);
    }
    {
        World w;
        w.mem.set32(kWindow + 0x348, kList);
        w.make_list(kList, {3, 0, 1}, 0);
        w.mem.set32(kWindow + 0x4d8, 0xffffffffu);
        w.mem.set8(kPadConfirmEdge, 1);
        CHECK(w.career.poll(kWindow) == 1);                     // Continue goes on without touching the save image
        CHECK(w.calls_named("engine_004138f0").empty());
    }
    {
        World w;
        w.mem.set32(kWindow + 0x4d8, 0xffffffffu);
        w.mem.set8(kPadBackEdge, 1);
        CHECK(w.career.poll(kWindow) == 2);                     // back leaves with its sound
        CHECK(w.mem.u8(kPadBackEdge) == 0);
        CHECK(w.calls_named("sfx").size() == 1);
    }
}

void test_career_select_card_steps_answer_for_themselves() {
    World w;
    w.mem.set32(kWindow + 0x4d8, 7);                            // a state that is neither choosing nor a card step comes back unchanged
    CHECK(w.career.poll(kWindow) == 7);
}

void test_the_front_end_sound_slides_toward_its_target() {
    World w;
    w.mem.set32(kFrontVoice, 3);
    w.mem.set32(kFrontPanCurrent, 0);
    w.mem.set32(kFrontPanTarget, 0x100);
    w.mem.set32(kFrontSoundScale, 255);
    w.mem.set32(kFrontSoundPan, 0x1234);
    w.career.update_menu_sound();
    CHECK(w.mem.s32(kFrontPanCurrent) == 0x88);                 // one step of 0x88
    CHECK(w.mem.u32(kFrontSoundPan) == 0x1234);                 // the pan word is put back
    w.career.update_menu_sound();
    CHECK(w.mem.s32(kFrontPanCurrent) == 0x100);                // clamped at the target
    // at a target of zero the voice is stopped
    w.mem.set32(kFrontPanCurrent, 0);
    w.mem.set32(kFrontPanTarget, 0);
    w.career.update_menu_sound();
    CHECK(w.mem.u32(kFrontVoice) == 0xffffffffu);
    CHECK(w.calls_named("engine_004ad9f0").size() == 1);
}

void test_a_handicap_moves_one_at_a_time_while_not_ready() {
    World w;
    w.mem.set32(kPanelA + 0x360, 0);
    w.mem.set8(kPanelA + 0x35c, 0);
    w.mem.set32(kPanelA + 0x34c, kHeap + 0x40000);              // the two rows of dots
    w.mem.set32(kPanelA + 0x350, kHeap + 0x40100);
    w.mem.set32(kHeap + 0x40000 + 0xc, 10);                     // (each row's maximum)
    w.mem.set32(kHeap + 0x40100 + 0xc, 10);
    w.mem.set32(kHandicapValues, 2);
    w.mem.set8(0x0056b079, 1);                                  // pad 0's left edge
    w.handicap.panel_update(kPanelA);
    CHECK(w.mem.s32(kHandicapValues) == 1);
    CHECK(w.mem.s32(kHeap + 0x40000 + 4) == 6 && w.mem.s32(kHeap + 0x40100 + 4) == 6);   // the dots show the handicap plus five
    CHECK(w.mem.u8(0x0056b079) == 0);
    CHECK(w.calls_named("engine_004ca8d0").size() == 1);        // limited to -5..5 by the engine
    // a ready player's handicap is fixed
    w.mem.set8(kPanelA + 0x35c, 1);
    w.mem.set8(0x0056b089, 1);
    w.handicap.panel_update(kPanelA);
    CHECK(w.mem.s32(kHandicapValues) == 1);
}

void test_confirm_readies_a_player_and_back_takes_it_back() {
    World w;
    w.mem.set32(kPanelA + 0x360, 0);
    w.mem.set8(kPanelA + 0x35c, 0);
    w.mem.set8(0x0056b029, 1);
    CHECK(w.handicap.panel_poll(kPanelA) == 1);
    CHECK(w.mem.u8(kPanelA + 0x35c) == 1);
    w.mem.set8(0x0056aff9, 1);
    CHECK(w.handicap.panel_poll(kPanelA) == 0);                 // back un-readies
    w.mem.set8(0x0056aff9, 1);
    CHECK(w.handicap.panel_poll(kPanelA) == 2);                 // back when not ready leaves the screen
}

void test_the_handicap_screen_needs_two_pads_and_both_ready() {
    World w;
    w.mem.set32(kWindow + 0x348, kPanelA);
    w.mem.set32(kWindow + 0x34c, kPanelB);
    w.mem.set32(kPanelA + 0x360, 0);
    w.mem.set32(kPanelB + 0x360, 1);
    w.mem.set32(kSecondPad, 0);
    CHECK(w.handicap.window_step(kWindow) == 2);                // a single pad has nothing to ask
    w.mem.set32(kSecondPad, 1);
    w.mem.set8(kPanelA + 0x35c, 1);
    w.mem.set8(kPanelB + 0x35c, 0);
    CHECK(w.handicap.window_step(kWindow) == 0);
    w.mem.set8(kPanelB + 0x35c, 1);
    CHECK(w.handicap.window_step(kWindow) == 1);
}

void test_the_multi_player_list_answers_choice_or_back() {
    World w;
    CHECK(w.multi.poll() == 0);
    w.mem.set8(kPadConfirmEdge + 0x16c, 1);                     // pad 1's confirm counts too
    CHECK(w.multi.poll() == 1);
    CHECK(w.mem.u8(kPadConfirmEdge + 0x16c) == 0);
    w.mem.set8(kPadBackEdge, 1);
    CHECK(w.multi.poll() == 2);
    // the highlighted row's id
    w.mem.set32(kWindow + 0x1fc, kList);
    w.make_list(kList, {0xffffffffu, 5, 8, 7}, 2);
    CHECK(w.multi.selected_id(kWindow) == 8);
}

std::string cstring_at(World& w, std::uint32_t address) { return w.mem.cstring(address, 0x40); }

void write_text(World& w, std::uint32_t address, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        w.mem.set8(address + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(text[i]));
    }
    w.mem.set8(address + static_cast<std::uint32_t>(text.size()), 0);
}

void test_name_words_are_cut_at_a_space_a_hyphen_or_nine_characters() {
    World w;
    const std::uint32_t source = kHeap + 0x50000;
    const std::uint32_t dest = kHeap + 0x50100;
    write_text(w, source, "  Tony Hawk");
    CHECK(w.level.copy_name_word(dest, source, false) == 6);        // two spaces and "Tony"
    CHECK(cstring_at(w, dest) == "Tony");
    write_text(w, source, "Jamie-Thomas");
    CHECK(w.level.copy_name_word(dest, source, true) == 6);         // the hyphen ends the word
    CHECK(cstring_at(w, dest) == "Jamie-...");                      // more text is left: an ellipsis
    write_text(w, source, "Supercalifragilistic");
    CHECK(w.level.copy_name_word(dest, source, false) == 9);        // nine characters at most
    CHECK(cstring_at(w, dest) == "Supercali");
    write_text(w, source, "");
    CHECK(w.level.copy_name_word(dest, source, false) == 0);
    CHECK(cstring_at(w, dest) == " ");                              // an empty word is a space
}

void test_the_globe_turns_the_short_way_round() {
    World w;
    w.mem.set32(kPanelA + 0x84, 0x10);                              // some latitude target (not the free-run marker)
    w.mem.set32(kPanelA + 0x80, 0x10);
    w.mem.set32(kPanelA + 0x88, 0x0ff0);                            // longitude 4080
    w.mem.set32(kPanelA + 0x8c, 0x0010);                            // target 16: 32 steps forward through the wrap, not 4064 back
    w.level.globe_update(kPanelA);
    CHECK(w.mem.s32(kPanelA + 0x88) > 0x0ff0 || w.mem.s32(kPanelA + 0x88) < 0x100);   // it moved forward (a quarter of the 32)
    CHECK(w.mem.s32(kPanelA + 0x88) == 0x0ff8);
}

void test_the_level_screen_wraps_and_the_answer_comes_from_the_window() {
    World w;
    w.mem.set32(kWindow + 0x624, 5);
    w.mem.set8(kWindow + 0x620, 1);
    CHECK(w.level.step(kWindow, kList));
    CHECK(w.mem.u32(kList) == 5);
    w.mem.set8(kWindow + 0x620, 0);
    CHECK(!w.level.step(kWindow, kList));
}

}  // namespace

int main() {
    test_career_select_choices();
    test_career_select_card_steps_answer_for_themselves();
    test_the_front_end_sound_slides_toward_its_target();
    test_a_handicap_moves_one_at_a_time_while_not_ready();
    test_confirm_readies_a_player_and_back_takes_it_back();
    test_the_handicap_screen_needs_two_pads_and_both_ready();
    test_the_multi_player_list_answers_choice_or_back();
    test_name_words_are_cut_at_a_space_a_hyphen_or_nine_characters();
    test_the_globe_turns_the_short_way_round();
    test_the_level_screen_wraps_and_the_answer_comes_from_the_window();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("select screens tests passed");
    return 0;
}
