// Unit tests of Db_Init / Db_Reset (render_display.cpp) on hand-built memory, for the polygon arena's scale (HostOptions::poly_buffer_scale, docs: THPS2
// post_phase_5/polybuffer.md P3). The retail forced-call replay (render_retail_calls_test.cpp, render-calls-rdbA/B) is the evidence the default matches the
// original; these pin that the default asks for exactly retail's blocks, and that a scaled area comes from the C runtime's heap, keeps its size while it is
// held and goes back there.
#include "host_options.hpp"
#include "render_kit.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::renderkit;
using Call = RetailServices::Call;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr std::uint32_t kCurrentLevel = 0x0056a898;
constexpr std::uint32_t kPlayers = 0x00533f3c;
constexpr std::uint32_t kLevelTable = 0x0053c27c;   // ExtraPolyBuffer[players - 1] at + 4 * (level * 107 + players)
constexpr std::uint32_t kOtBytes = 0x00561150;
constexpr std::uint32_t kArea = 0x00561090;
constexpr std::uint32_t kSecondBuffer = 0x00561144;
constexpr std::uint32_t kTableA = 0x00561064;
constexpr std::uint32_t kTableB = 0x00561118;

constexpr std::uint32_t kLevel = 1;                 // School II
constexpr std::uint32_t kExtra = 0x2000;
constexpr std::uint32_t kBytes = kExtra + 0x145000;
constexpr std::uint32_t kOtAlloc = ((1u << 12) + 2) * 8 + 2;
constexpr std::uint32_t kGameArea = 0x00700000;     // what the game's heap answers
constexpr std::uint32_t kGuestArea = 0x20000000;    // what the C runtime's answers
constexpr std::uint32_t kTable1 = 0x00a00000;
constexpr std::uint32_t kTable2 = 0x00a10000;

struct World {
    RetailMemory mem;
    RetailServices svc;
    RenderKit render{mem, svc};

    World() {
        mem.add_region(0x00400000, 0x002c0000);
        mem.set32(kCurrentLevel, kLevel);
        mem.set32(kPlayers, 1);
        mem.set32(kLevelTable + 4 * (kLevel * 107 + 1), kExtra);
        host_options().poly_buffer_scale = 1;
        host_options().poly_buffer_area_scale = 0;
    }
    ~World() {
        host_options().poly_buffer_scale = 1;
        host_options().poly_buffer_area_scale = 0;
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // the allocator calls (the display's engine calls, DrawSync, are not what is tested)
    std::vector<Call> memory_calls() const {
        std::vector<Call> out;
        for (const auto& call : svc.calls()) {
            if (call.name == "alloc" || call.name == "mem_delete" || call.name == "crt_new" || call.name == "crt_delete") {
                out.push_back(call);
            }
        }
        return out;
    }
};

void test_default_is_retail() {
    World w;
    w.svc.script("alloc", static_cast<std::int32_t>(kGameArea));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable1));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable2));
    w.render.db_init(0, 0, 0);
    const std::vector<Call> init = {
        {"alloc", {kBytes * 2, 1, 1, 0}, ""},
        {"alloc", {kOtAlloc, 1, 1, 0}, ""},
        {"alloc", {kOtAlloc, 1, 1, 0}, ""},
    };
    CHECK(w.svc.calls() == init);   // every call db_init makes, in order: retail's three blocks from the game's heap
    CHECK(w.mem.u32(kOtBytes) == kBytes);
    CHECK(w.mem.u32(kArea) == kGameArea);
    CHECK(w.mem.u32(kSecondBuffer) == kGameArea + kBytes);
    CHECK(w.mem.u32(kTableA) == kTable1 && w.mem.u32(kTableB) == kTable2);
    CHECK(host_options().poly_buffer_area_scale == 0);

    w.svc.clear();
    w.render.db_reset();
    const std::vector<Call> reset = {
        {"mem_delete", {kGameArea}, ""},
        {"mem_delete", {kTable1}, ""},
        {"mem_delete", {kTable2}, ""},
    };
    CHECK(w.memory_calls() == reset);
    CHECK(w.mem.u32(kArea) == 0);
}

void test_scaled_area_comes_from_the_guest_heap() {
    World w;
    host_options().poly_buffer_scale = 4;
    w.svc.script("crt_new", static_cast<std::int32_t>(kGuestArea));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable1));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable2));
    w.render.db_init(0, 0, 0);
    const std::vector<Call> init = {
        {"crt_new", {kBytes * 4 * 2 + kPolyBufferPad}, ""},
        {"alloc", {kOtAlloc, 1, 1, 0}, ""},   // the ordering tables stay the game's
        {"alloc", {kOtAlloc, 1, 1, 0}, ""},
    };
    CHECK(w.svc.calls() == init);
    CHECK(w.mem.u32(kOtBytes) == kBytes * 4);   // the capacity the guards and CalcPolyBufferEnd read
    CHECK(w.mem.u32(kArea) == kGuestArea);
    CHECK(w.mem.u32(kSecondBuffer) == kGuestArea + kBytes * 4);
    CHECK(host_options().poly_buffer_area_scale == 4);

    // Db_Init again without Db_Reset (game_level_loop, load_game, ...): the area is kept and the size stays the held area's, whatever the host asks now
    w.svc.clear();
    host_options().poly_buffer_scale = 1;
    w.render.db_init(0, 0, 0);
    CHECK(w.svc.calls().empty());
    CHECK(w.mem.u32(kOtBytes) == kBytes * 4);

    w.svc.clear();
    w.render.db_reset();
    const std::vector<Call> reset = {
        {"crt_delete", {kGuestArea}, ""},
        {"mem_delete", {kTable1}, ""},
        {"mem_delete", {kTable2}, ""},
    };
    CHECK(w.memory_calls() == reset);
    CHECK(w.mem.u32(kArea) == 0);
    CHECK(host_options().poly_buffer_area_scale == 0);

    // after the reset, the next area is the host's scale again: retail's here
    w.svc.clear();
    w.svc.script("alloc", static_cast<std::int32_t>(kGameArea));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable1));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable2));
    w.render.db_init(0, 0, 0);
    CHECK(!w.svc.calls().empty() && w.svc.calls().front() == (Call{"alloc", {kBytes * 2, 1, 1, 0}, ""}));
    CHECK(w.mem.u32(kOtBytes) == kBytes);
}

void test_held_game_area_keeps_retail_size() {
    // a level change while the game's own area is held writes the new level's size, as retail does, even with a scale asked for since
    World w;
    w.svc.script("alloc", static_cast<std::int32_t>(kGameArea));
    w.render.db_init(0, 0, 0);
    host_options().poly_buffer_scale = 4;
    w.mem.set32(kLevelTable + 4 * (kLevel * 107 + 1), kExtra + 0x100);
    w.svc.clear();
    w.render.db_init(0, 0, 0);
    CHECK(w.svc.calls().empty());
    CHECK(w.mem.u32(kOtBytes) == kBytes + 0x100);
}

// P4: a polygon that finds the arena full. With an enlarged area held it is dropped, counted and logged once per level; with the game's own area (retail, every gate) it throws as before.
void test_overflow_net() {
    constexpr std::uint32_t kCursor = 0x00560fd0;
    constexpr std::uint32_t kEnd = 0x00568628;
    constexpr std::uint32_t kFace = 0x00500000;   // never read: the slot is asked for first
    constexpr std::uint32_t kLogFormat = 0x00549d64;   // retail's "Poly Heap Overflow" line
    const auto logs = [](const World& w) {
        std::size_t n = 0;
        for (const auto& call : w.svc.calls()) {
            n += call.name == "printf_log" ? 1 : 0;
        }
        return n;
    };

    {
        World w;
        w.mem.set32(kCursor, 0x00600000);
        w.mem.set32(kEnd, 0x00600000);
        bool threw = false;
        try {
            w.render.model_build_polygon(kFace);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);   // retail's behaviour, with the game's own area
        CHECK(host_options().poly_overflows == 0 && !host_options().poly_overflow_logged);
        CHECK(w.mem.u32(kCursor) == 0x00600000);
    }

    World w;
    host_options().poly_buffer_scale = 4;
    w.svc.script("crt_new", static_cast<std::int32_t>(kGuestArea));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable1));
    w.svc.script("alloc", static_cast<std::int32_t>(kTable2));
    w.render.db_init(0, 0, 0);
    w.mem.set32(kCursor, 0x20100000);
    w.mem.set32(kEnd, 0x20100000);
    w.svc.clear();
    w.render.model_build_polygon(kFace);
    w.render.model_build_polygon(kFace);
    CHECK(host_options().poly_overflows == 2);
    CHECK(host_options().poly_overflow_logged);
    CHECK(logs(w) == 1);                                   // once per level, not per face
    CHECK(w.mem.u32(kCursor) == 0x20100000);               // nothing was taken
    const auto& first = w.svc.calls();
    CHECK(!first.empty() && first.front() == (Call{"printf_log", {kLogFormat}, ""}));

    // the next level (Init_Cleanup clears the two) logs again
    host_options().poly_overflows = 0;
    host_options().poly_overflow_logged = false;
    w.svc.clear();
    w.render.model_build_polygon(kFace);
    CHECK(host_options().poly_overflows == 1 && logs(w) == 1);
}

}  // namespace

int main() {
    test_overflow_net();
    test_default_is_retail();
    test_scaled_area_comes_from_the_guest_heap();
    test_held_game_area_keeps_retail_size();
    if (failures == 0) {
        std::printf("render_display_test: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
