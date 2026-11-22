// Unit tests of the native HUD code (Phase 4.10b): the trick score stack and the panel display, on hand-built memory. The
// retail forced-call replay (hud_retail_calls_test.cpp) is the evidence the code matches the original; these tests pin
// the behaviour a reader would expect and catch regressions without the fixtures.
#include "hud_panel.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::hud;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr std::uint32_t kSkater = 0x20000000;
constexpr std::uint32_t kPanelAt = kSkater + kPanelInSkater;
constexpr std::uint32_t kNames = 0x21000000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    Panel panel{mem, svc};

    World() {
        mem.add_region(kSkater, 0x3400);
        mem.add_region(kNames, 0x400);
        mem.add_region(0x00536160, 0x70);
        mem.add_region(kGameMode, 0x70);
        mem.add_region(kCareerFlag, 4);
        mem.add_region(kPointDelta, 4);
        mem.add_region(kAnimationClock, 4);
        mem.add_region(kPlayerOneAt, 4);
        mem.add_region(kCountdownMode, 4);
        mem.add_region(kSimulationTime, 4);
        mem.add_region(kReplayState, 4);
        mem.add_region(kViewportMode, 4);
        mem.add_region(kTenfoldFlag, 4);
        mem.add_region(kCurrentLevel, 4);
        mem.add_region(0x0052e32c, 8);
        mem.add_region(kScreenHeight, 4);
        mem.add_region(kScreenWidthScale, 8);
        mem.add_region(kArenaCursor, 8);
        mem.add_region(kArenaEnd, 4);
        mem.add_region(0x00560fe0, 0x90);
        mem.add_region(0x00561100, 0x40);
        mem.add_region(0x30000000, 0x100);
        mem.add_region(kLevelGoalTable, 0x400);
        // the retail tables the score reads
        const std::uint32_t score[] = {0x73, 100, 75, 50, 25, 10};
        for (std::uint32_t i = 0; i < 6; ++i) mem.set32(0x00536164 + 4 * i, score[i]);
        const std::uint32_t chain[] = {0, 1, 2, 4, 6, 10};
        for (std::uint32_t i = 0; i < 6; ++i) mem.set32(0x0053617c + 4 * i, chain[i]);
        for (std::uint32_t i = 0; i < 15; ++i) mem.set32(0x00536194 + 4 * i, i * 2);
        mem.set32(kPlayerOneAt, kSkater);
        mem.set32(kPanelAt + kOwner, kSkater);
        mem.set32(kPointDelta, 0x1000);
        mem.set32(kScreenHeight, 240);
        mem.set32(kScreenWidthScale, 640);
        mem.set32(kScreenWidthScale + 4, 480);
        mem.set32(kRenderState, 0x00560fe0);
        mem.set32(0x00560fe0 + 0x84, 0x00561100);
        mem.set32(kArenaCursor, 0x30000000);
        mem.set32(kArenaEnd, 0x30000100);
        mem.set32(kGameMode, 1);
        mem.set32(kNumPlayers, 1);
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::string name = i == 0 ? "Kickflip" : i == 1 ? "Heelflip" : i == 2 ? "Impossible" : "Manual";
            std::vector<std::uint8_t> bytes(name.begin(), name.end());
            bytes.push_back(0);
            mem.write(kNames + 0x40 * i, bytes);
        }
    }

    static constexpr std::uint32_t kPlayerOneAt = 0x0056a960;

    void entry(std::int32_t index, std::uint32_t name, std::int32_t points, std::int32_t kind = 0, std::int32_t flag = 1,
               std::int32_t history = 1, std::int32_t value = 0) {
        const std::uint32_t e = kPanelAt + kStackBase + static_cast<std::uint32_t>(index) * kEntrySize;
        mem.set32(e + kEntName, kNames + 0x40 * name);
        mem.set32(e + kEntPoints, static_cast<std::uint32_t>(points));
        mem.set32(e + kEntKind, static_cast<std::uint32_t>(kind));
        mem.set32(e + kEntFlag, static_cast<std::uint32_t>(flag));
        mem.set32(e + kEntHistory, static_cast<std::uint32_t>(history));
        mem.set32(e + kEntValue, static_cast<std::uint32_t>(value));
    }
    void count(std::int32_t n) { mem.set32(kPanelAt + kStackCount, static_cast<std::uint32_t>(n)); }
};

}  // namespace

int main() {
    // a lone trick scores its points once: (100 * (0 + 2) / 2) * 100% / 100
    {
        World w;
        w.entry(0, 0, 100);
        w.count(1);
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 100);
        // the stack multiplier of one entry is 2 / 2
        CHECK(w.panel.compute_landed_value(kPanelAt) == 100);
        // a Perfect landing adds half again (the landing quality must be non-zero)
        w.mem.set32(kSkater + kSkLandQuality, 1);
        w.mem.set32(kSkater + kSkPerfect, 1);
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 150);
        w.mem.set32(kSkater + kSkPerfect, 0);
        w.mem.set32(kSkater + kSkSloppy, 1);
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 75);
    }
    // repeats of the same trick are worth less: the history count picks the percentage (75% for the second time)
    {
        World w;
        w.entry(0, 0, 100, 0, 1, 2);
        w.count(1);
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 75);
        w.entry(0, 0, 100, 0, 1, 5);
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 10);   // the fifth repeat is capped at the last percentage
    }
    // a chain of entries: the closing values of the chain raise every multiplier in it
    {
        World w;
        w.entry(0, 0, 100, 0, 1, 1, 2);
        w.entry(1, 1, 100, 1, 1, 1, 2);
        w.count(2);
        CHECK(w.panel.chain_value(kPanelAt, 0) == 4);
        CHECK(w.panel.chain_value_if_first(kPanelAt, 0) == 4);
        CHECK(w.panel.chain_value_if_first(kPanelAt, 1) == 0);
        CHECK(w.panel.sum_entry_values(kPanelAt) == 4);
        // chain value 4 -> table[4] = 6: (100 * (6 + 2) / 2) twice
        CHECK(w.panel.compute_stack_sum(kPanelAt) == 800);
    }
    // landing commits the stack: totals, best combo, and the highlight service
    {
        World w;
        w.entry(0, 0, 100);
        w.count(1);
        w.mem.set32(kCareerFlag, 1);
        w.mem.set8(kPanelAt + kTextNames, 'X');
        w.panel.commit_landed_stack(kPanelAt);
        CHECK(w.mem.u32(kPanelAt + kLandedValue) == 100);
        CHECK(w.mem.u32(kPanelAt + kComboTotal) == 100);
        CHECK(w.mem.u32(kPanelAt + kBestScore) == 100);
        CHECK(w.mem.u16(kPanelAt + kTrickCount) == 1);
        CHECK(w.mem.u32(kPanelAt + kStackCount) == 0);
        CHECK(w.mem.u32(kPanelAt + kStackTimer) == 0x28);
        CHECK(!w.svc.calls().empty() && w.svc.calls().back().name == kSvcHighlightEnd);
        CHECK(w.svc.calls().back().args == (std::vector<std::uint32_t>{kSkater, 100}));
    }
    // a bail takes the stack back: the count goes to zero and the highlight is shortened by the landed value
    {
        World w;
        w.entry(0, 0, 100);
        w.count(1);
        w.panel.rollback_stack(kPanelAt);
        CHECK(w.mem.u32(kPanelAt + kStackCount) == 0);
        CHECK(w.mem.u16(kPanelAt + kBailCount) == 1);
        CHECK(w.svc.calls().front().name == kSvcHighlightEnd);
        CHECK(w.svc.calls().front().args[1] == static_cast<std::uint32_t>(-100));
    }
    // pushing a trick adds an entry with ten times the points in the tenfold mode and asks for the trick history
    {
        World w;
        w.mem.set32(kTenfoldFlag, 1);
        w.entry(0, 0, 100);
        w.entry(1, 2, 50);
        w.count(2);
        w.svc.script(kSvcHistoryAdd, 3);
        w.panel.push_entry(kPanelAt, kNames + 0x40, 7, 1, 1, 0);
        CHECK(w.mem.u32(kPanelAt + kStackCount) == 3);
        const std::uint32_t pushed = kPanelAt + kStackBase + 2 * kEntrySize;
        CHECK(w.mem.u32(pushed + kEntPoints) == 70);
        CHECK(w.mem.u32(pushed + kEntName) == kNames + 0x40);
        CHECK(w.mem.u32(pushed + kEntKind) == 1);
        CHECK(w.mem.u32(pushed + kEntHistory) == 3);
        CHECK(w.mem.u32(pushed + kEntCountdown) == 0x12);
        bool asked = false;
        for (const auto& call : w.svc.calls()) asked = asked || call.name == kSvcHistoryAdd;
        CHECK(asked);
    }
    // the point timer bails past its limit and otherwise feeds the newest entry
    {
        World w;
        w.entry(0, 0, 100);
        w.count(1);
        w.mem.set32(kSkater + kSkStackCountMirror, 1);
        w.mem.set32(kSkater + kSkPointLimit, 0);
        w.panel.advance_point_timer(kSkater, 0x800);
        CHECK(w.mem.u32(kSkater + kSkPointTimer) == 0x1000);
        CHECK(w.mem.u32(kPanelAt + kStackBase + kEntPoints) == 100 + 0x800);
        World bail;
        bail.entry(0, 0, 100);
        bail.count(1);
        bail.svc.script(kSvcProfileValue, 0);
        bail.mem.set32(kSkater + kSkPointLimit, 1);
        bail.mem.set32(kSkater + kSkPointTimer, 0x7ff000);
        bail.panel.advance_point_timer(kSkater, 0x1000);
        bool bailed = false;
        for (const auto& call : bail.svc.calls()) bailed = bailed || call.name == kSvcBail;
        CHECK(bailed);
        CHECK(bail.mem.u32(kPanelAt + kStackBase + kEntPoints) == 100);
    }
    // the text builder joins the names newest last with " + " and puts the repeat count in front of repeats
    {
        World w;
        w.entry(0, 0, 100, 1);
        w.entry(1, 1, 100, 0);
        w.count(2);
        w.svc.script(kSvcTextWidth, 10);
        w.svc.script(kSvcTextWidth, 10);
        w.panel.refresh_text(kPanelAt);
        CHECK(w.mem.cstring(kPanelAt + kTextNames + 2 * kBestNameStride) == "Kickflip + Heelflip");
    }
    // the timer shows minutes and seconds and is drawn through the font service
    {
        World w;
        w.mem.set32(kGameMode, 1);
        w.mem.set32(kAnimationClock, 0);
        w.mem.set32(kSkater + 0x2cc8, 3600 + 5 * 60);   // 1:05
        w.mem.set32(kSimulationTime, 0);
        w.mem.set32(kSkater + 0x2cc4, 1);
        w.panel.display_holding(kSkater, 0x100, 0x30);
        bool drew = false;
        for (const auto& call : w.svc.calls()) drew = drew || (call.name == kSvcFontDraw && call.text == "1:05");
        CHECK(drew);
    }
    // a flat poly links into its bucket and scales to the screen; a full arena returns null
    {
        World w;
        const std::uint32_t poly = w.panel.draw_flat_poly(256, 0, 256, 120, 30, 30, 30, 1);
        CHECK(poly == 0x30000000);
        CHECK(w.mem.u32(kArenaCursor) == 0x3000001c);
        CHECK(w.mem.u8(poly + 4) == 30 && w.mem.u8(poly + 7) == 0x28);
        CHECK(w.mem.s16(poly + 0x08) == 256 * 640 / 512);
        CHECK(w.mem.s16(poly + 0x12) == 120 * 480 / 240);
        CHECK(w.mem.u32(0x00561100 + 8) == poly);
        w.mem.set32(kArenaEnd, 0x30000010);
        CHECK(w.panel.draw_flat_poly(0, 0, 1, 1, 0, 0, 0, 0) == 0);
    }
    if (failures == 0) {
        std::puts("hud tests passed");
    }
    return failures == 0 ? 0 : 1;
}
