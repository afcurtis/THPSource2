// The seams of a standalone program (Phase 5.4a): the out-of-scope retail functions (re/symbols/seams.yml) that native code
// calls, implemented here instead of by the retail image. A seam with no implementation stops the process at its first call
// with its address, so the list below is the measured work still to do. docs/HOOKS.md, "Standalone".
//
// What each implementation stands in for is the retail function's contract as the natives use it, not a copy of its body.
#include "guest_arena.hpp"
#include "guest_format.hpp"
#include "../../runtime/frame_pacing.hpp"
#include "kit_bridge.hpp"
#include "seam_addresses_generated.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace opentony::kitdll {
namespace {

using Args = std::vector<std::uint32_t>;

constexpr std::uint32_t kSimTick = 0x0056e31c;  // SimulationPublicTick: advanced by Timer_UpdateFrameClock from the clock below
constexpr std::uint32_t kGameTick = 0x0056e320;       // SimulationTime: stops while paused
constexpr std::uint32_t kFrameAccumulator = 0x006a0590;   // doubles, in ticks (SimulationTimeTimerCallback)
constexpr std::uint32_t kGameAccumulator = 0x006a0598;
constexpr std::uint32_t kLastSceneTick = 0x0056e840;   // PcSystem::begin_scene: the tick word when the last scene began
constexpr std::uint32_t kPauseFlag = 0x00561c04;
constexpr std::uint32_t kPauseFlag2 = 0x0056a8e0;
constexpr std::uint32_t kTimerState =0x006a05a0;     // TimerState: +0 the multimedia timer's id (0: none), +4 its interval in ms
constexpr std::uint32_t kTimerCallback = 0x004dace0;  // SimulationTimeTimerCallback (a native)
constexpr std::uint16_t kTimerThreadControl = 0x027f; // the process default the winmm thread runs at (53 bits)

// The clock the C runtime's clock() answers, in milliseconds. Lockstep (the default): it advances 1/60 s per Game_FrameTick call, so a
// run is the same on every machine (retail reads the wall clock; Timer_UpdateFrameClock turns the difference into 60 Hz ticks).
// Realtime (8a, ot_standalone_set_platform): the wall clock from the first read, which is what a window needs.
std::uint64_t g_clock_micros = 0;
std::uint64_t g_timer_delivered = 0;   // realtime: the multimedia timer's ticks delivered so far

std::uint64_t wall_micros() {
    static LARGE_INTEGER frequency, epoch;
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&epoch);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<std::uint64_t>((now.QuadPart - epoch.QuadPart) * 1000000 / frequency.QuadPart);
}

std::uint64_t now_micros() { return platform_realtime() ? wall_micros() : g_clock_micros; }

// Realtime with a frame rate set: wait until the wall clock reaches `deadline` (microseconds of wall_micros). A high-resolution waitable timer to 1 ms before it (about 0.5 ms
// accurate), then a yield to it; measured 0 to 1 us late. CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (0x2) is Windows 10 1803: without it, Sleep(1) while more than 4 ms remain, then
// the yield.
void wait_until(std::uint64_t deadline) {
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
    for (std::uint64_t at = wall_micros(); at < deadline; at = wall_micros()) {
        if (deadline - at > 1500 && timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((deadline - at - 1000) * 10);   // 100 ns units, relative
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, 20);
                continue;
            }
        }
        if (deadline - at > 4000) {
            Sleep(1);
        } else {
            SwitchToThread();
        }
    }
}

std::uint32_t g_frames = 0;

// the memory image of ot_standalone_set_dump
std::string g_dump_path;
std::uint32_t g_dump_tick = 0;
std::uint32_t g_dump_base = 0;
std::uint32_t g_dump_size = 0;
std::uint32_t g_ticks = 0;

// The clock moves a game tick and the multimedia timer's due tick is delivered; the answer is SimulationPublicTick. Game_FrameTick does this after its message pump and
// WINMAIN_Sleep (8c) does it alone: a wait loop that reads the tick back (PcSystem::begin_scene, the frame limiters) ends when the timer has run.
std::uint32_t deliver_tick(const char* site) {
    // 10g s22: OT_DELIVERY_LOG=<first>:<last> logs each delivery in that range with its site, the native that reached it and the input polls so far (a measurement mode)
    static const std::pair<std::uint32_t, std::uint32_t> logged = [] {
        const char* text = std::getenv("OT_DELIVERY_LOG");
        if (text == nullptr || *text == '\0') {
            return std::pair<std::uint32_t, std::uint32_t>{1, 0};
        }
        char* next = nullptr;
        const auto first = static_cast<std::uint32_t>(std::strtoul(text, &next, 10));
        const auto last = *next == ':' ? static_cast<std::uint32_t>(std::strtoul(next + 1, nullptr, 10)) : first;
        return std::pair<std::uint32_t, std::uint32_t>{first, last};
    }();
    static std::uint32_t delivered = 0;
    ++delivered;
    if (delivered >= logged.first && delivered <= logged.second) {
        // the player's skater (0x0056a960 holds it): position (Q12) and state, for a comparison of frame rates at equal ticks (framerate.md)
        const std::uint32_t skater = arena::at<std::uint32_t>(0x0056a960);
        std::int32_t sx = 0, sy = 0, sz = 0, sstate = -1, svy = 0, say = 0;
        if (skater >= 0x01000000u && skater < 0x40000000u) {
            sx = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 8));
            sy = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 12));
            sz = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 16));
            sstate = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 0x30b8));
            svy = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 0x50));
            say = static_cast<std::int32_t>(arena::at<std::uint32_t>(skater + 0x5c));
        }
        // OT_DELIVERY_WORDS=0x2a8,0x2bd8,...: more skater words (hex offsets), printed as " w <offset>=<value>" (a measurement mode: F1.5 compares scores and trick state at equal ticks)
        std::string extra_words;
        static const std::vector<std::uint32_t> words = [] {
            std::vector<std::uint32_t> list;
            if (const char* text = std::getenv("OT_DELIVERY_WORDS")) {
                for (const char* at = text; *at != '\0';) {
                    char* next = nullptr;
                    list.push_back(static_cast<std::uint32_t>(std::strtoul(at, &next, 16)));
                    at = (*next == ',') ? next + 1 : next;
                    if (next == at && *next != '\0') {
                        break;
                    }
                }
            }
            return list;
        }();
        if (skater >= 0x01000000u && skater < 0x40000000u) {
            for (const std::uint32_t offset : words) {
                char piece[48];
                std::snprintf(piece, sizeof piece, " w %x=%d", static_cast<unsigned>(offset), static_cast<int>(arena::at<std::uint32_t>(skater + offset)));
                extra_words += piece;
            }
        }
        // OT_DELIVERY_GWORDS=55fa40,...: absolute guest addresses (hex, e.g. the camera), printed as " g <address>=<value>" (a measurement mode: F1.5D)
        static const std::vector<std::uint32_t> gwords = [] {
            std::vector<std::uint32_t> list;
            if (const char* text = std::getenv("OT_DELIVERY_GWORDS")) {
                for (const char* at = text; *at != '\0';) {
                    char* next = nullptr;
                    list.push_back(static_cast<std::uint32_t>(std::strtoul(at, &next, 16)));
                    at = (*next == ',') ? next + 1 : next;
                    if (next == at && *next != '\0') {
                        break;
                    }
                }
            }
            return list;
        }();
        for (const std::uint32_t address : gwords) {
            char piece[48];
            std::snprintf(piece, sizeof piece, " g %x=%d", static_cast<unsigned>(address), static_cast<int>(arena::at<std::uint32_t>(address)));
            extra_words += piece;
        }
        // OT_DELIVERY_OBJECTS=5184e0,5194f8: the objects of the game list (head 0x0056af40, or OT_DELIVERY_LIST=<hex head>; next at +0x20; vtable 0 = any) with one of these vtables (hex), by index in the list: " o <index>:<vtable>=<x>,<y>,<z>" (a measurement mode: F1.5F)
        if (const char* text = std::getenv("OT_DELIVERY_OBJECTS")) {
            std::vector<std::uint32_t> vtables;
            for (const char* at = text; *at != '\0';) {
                char* next = nullptr;
                vtables.push_back(static_cast<std::uint32_t>(std::strtoul(at, &next, 16)));
                at = (*next == ',') ? next + 1 : next;
                if (next == at && *next != '\0') {
                    break;
                }
            }
            const char* head_text = std::getenv("OT_DELIVERY_LIST");
            const char* offset_text = std::getenv("OT_DELIVERY_OBJ_OFFSET");   // the position words to print: 8 (the state, default) or 0x12c (the drawn whole-unit position: F1.5F cars)
            const std::uint32_t at_offset = offset_text != nullptr ? static_cast<std::uint32_t>(std::strtoul(offset_text, nullptr, 16)) : 8u;
            std::uint32_t object = arena::at<std::uint32_t>(head_text != nullptr ? static_cast<std::uint32_t>(std::strtoul(head_text, nullptr, 16)) : 0x0056af40u);
            for (unsigned index = 0; object >= 0x01000000u && object < 0x40000000u && index < 400; ++index, object = arena::at<std::uint32_t>(object + 0x20)) {
                const std::uint32_t vtable = arena::at<std::uint32_t>(object);
                for (const std::uint32_t wanted : vtables) {
                    if (vtable == wanted || wanted == 0) {
                        char piece[96];
                        std::snprintf(piece, sizeof piece, " o %u:%x=%d,%d,%d", index, static_cast<unsigned>(vtable), static_cast<int>(arena::at<std::uint32_t>(object + at_offset)),
                                      static_cast<int>(arena::at<std::uint32_t>(object + at_offset + 4)), static_cast<int>(arena::at<std::uint32_t>(object + at_offset + 8)));
                        extra_words += piece;
                    }
                }
            }
        }
        std::printf("delivery %u: %s in %s [poll %u, sim %u, skater %d %d %d state %d vy %d ay %d%s]\n", static_cast<unsigned>(delivered), site, t_native != nullptr ? t_native : "-",
                    static_cast<unsigned>(platform_input_polls()), static_cast<unsigned>(arena::at<std::uint32_t>(kSimTick)), sx, sy, sz, sstate, svy, say, extra_words.c_str());
        std::fflush(stdout);
    }
    const bool realtime = platform_realtime();
    const bool sub_tick = runtime::framepacing::pacing().sub_tick;   // F2A (120 fps)
    // lockstep on the sub-tick clock: a second delivery inside a frame that already has its period adds no time (framepacing::lockstep_holds)
    const bool held = !realtime && sub_tick && runtime::framepacing::lockstep_holds(arena::at<double>(kFrameAccumulator));
    if (!realtime && !held) {
        g_clock_micros += sub_tick ? static_cast<std::uint64_t>(1000000.0 / 60.0 * runtime::framepacing::pacing().period_ticks) : 1000000 / 60;
    }
    // 7c: the multimedia timer. Retail's runs on the winmm thread every 16 ms; the hooked build's lockstep clock delivers one tick of it at each
    // Game_FrameTick instead (src/hooks/clock.cpp), and so does this seam, when a timer is running (TimerState's id), under the timer thread's control word.
    // Realtime: the ticks that have fallen due since the last call (TimerState's interval, in ms), at most four.
    if (realtime && runtime::framepacing::pacing().exact_clock) {
        // F1 (framerate.md): a frame rate is set. The ticks follow the wall clock exactly: what SimulationTimeTimerCallback does (add to the frame accumulator, and to the
        // game accumulator unless paused, then truncate both into the tick words), with the real time since the last delivery as the step instead of 0.96 per call
        // (the integer tick would move 24 calls in 25 and a one-tick limiter would show a 32 ms frame every 0.4 s). At most four ticks a step, as before.
        static std::uint64_t last_micros = 0;
        static bool timer_period_raised = false;
        if (!timer_period_raised) {
            timer_period_raised = true;
            if (HMODULE winmm = LoadLibraryA("winmm.dll")) {   // Sleep(1) lasts a whole 15.6 ms timer period otherwise
                if (auto begin = reinterpret_cast<UINT(WINAPI*)(UINT)>(GetProcAddress(winmm, "timeBeginPeriod"))) {
                    begin(1);
                }
            }
        }
        const std::uint64_t now = wall_micros();
        if (arena::at<std::uint32_t>(kTimerState) != 0) {
            const double step = std::min(static_cast<double>(now - last_micros) * 60.0 / 1000000.0, 4.0);
            const std::uint32_t before = arena::at<std::uint32_t>(kSimTick);
            arena::at<double>(kFrameAccumulator) = arena::at<double>(kFrameAccumulator) + step;
            if (arena::at<std::uint32_t>(kPauseFlag) == 0 && arena::at<std::uint32_t>(kPauseFlag2) == 0) {
                arena::at<double>(kGameAccumulator) = arena::at<double>(kGameAccumulator) + step;
            }
            arena::at<std::uint32_t>(kSimTick) = static_cast<std::uint32_t>(arena::at<double>(kFrameAccumulator));
            arena::at<std::uint32_t>(kGameTick) = static_cast<std::uint32_t>(arena::at<double>(kGameAccumulator));
            // F2D (120) and its review (60 too): a precise wait at the top of the frame. The limiter's deadline (the last scene + the period: half a tick at 120, the last scene tick
            // + 1 at 60) is waited for here, by the delivery that finds it ahead (Game_FrameTick at the top of a level frame), and not in PcSystem::begin_scene's loop after the
            // frame's input and update: the frame then reads its input just after the wait and shows it at once (at 60 the old wait held up to a frame between the two), and
            // begin_scene finds its deadline passed. Sleep(1) wakes 1 to 3 ms late (97 to 107 fps at 120); the wait is a high-resolution timer, then a yield (wait_until).
            const double frame_time = arena::at<double>(kFrameAccumulator);
            const double scene_mark =sub_tick ? runtime::framepacing::sub_tick_state().last_scene : static_cast<double>(arena::at<std::uint32_t>(kLastSceneTick));
            const std::uint32_t since_scene = runtime::framepacing::count_delivery(scene_mark);
            double target = sub_tick ? runtime::framepacing::scene_due(frame_time)
                                     : runtime::framepacing::whole_tick_due(frame_time, arena::at<std::uint32_t>(kLastSceneTick), runtime::framepacing::limiter_ticks());
            if (target <= frame_time && since_scene >= runtime::framepacing::kIdleDelivery && arena::at<std::uint32_t>(kSimTick) == before) {
                // a loop that waits for whole ticks and draws nothing (menus, loading): wait for the next whole tick, as Sleep(1) did, but to the tick
                target = std::floor(frame_time) + 1.0;
            }
            if (target > frame_time) {
                const double left = target - frame_time;
                const std::uint64_t deadline = now + static_cast<std::uint64_t>(left * 1000000.0 / 60.0);
                wait_until(deadline);
                // The wait ends at the deadline: the accumulators go to it now and the clock resumes from it, so the scene the limiter lets through is stamped with the deadline itself
                // (the frame accumulator exactly: the limiter compares with it). Left to the next call, the time the message pump and the loop take after the wait would be added first
                // and every frame would be that much longer (measured: 97 fps at 120).
                arena::at<double>(kFrameAccumulator) = target;
                if (arena::at<std::uint32_t>(kPauseFlag) == 0 && arena::at<std::uint32_t>(kPauseFlag2) == 0) {
                    arena::at<double>(kGameAccumulator) = arena::at<double>(kGameAccumulator) + left;
                }
                arena::at<std::uint32_t>(kSimTick) = static_cast<std::uint32_t>(arena::at<double>(kFrameAccumulator));
                arena::at<std::uint32_t>(kGameTick) = static_cast<std::uint32_t>(arena::at<double>(kGameAccumulator));
                last_micros = deadline;
                return arena::at<std::uint32_t>(kSimTick);
            }
        } else {
            Sleep(1);
        }
        last_micros = now;
    } else if (sub_tick) {
        // F2A: lockstep at a scene shorter than a tick (120 fps: half a tick). One delivery is one scene period, exactly: the arithmetic of the branch above with a fixed step
        // (the timer callback adds 0.96 tick a call and cannot add half of one).
        if (arena::at<std::uint32_t>(kTimerState) != 0) {
            const double step = held ? 0.0 : runtime::framepacing::pacing().period_ticks;
            arena::at<double>(kFrameAccumulator) = arena::at<double>(kFrameAccumulator) + step;
            if (arena::at<std::uint32_t>(kPauseFlag) == 0 && arena::at<std::uint32_t>(kPauseFlag2) == 0) {
                arena::at<double>(kGameAccumulator) = arena::at<double>(kGameAccumulator) + step;
            }
            arena::at<std::uint32_t>(kSimTick) = static_cast<std::uint32_t>(arena::at<double>(kFrameAccumulator));
            arena::at<std::uint32_t>(kGameTick) = static_cast<std::uint32_t>(arena::at<double>(kGameAccumulator));
            session_on_delivery();
        }
    } else if (arena::at<std::uint32_t>(kTimerState) != 0) {
        std::uint64_t ticks = 1;
        if (realtime) {
            const std::uint32_t configured = arena::at<std::uint32_t>(kTimerState + 4);
            const std::uint64_t interval = std::max<std::uint32_t>(configured, 1u);
            const std::uint64_t due = wall_micros() / (interval * 1000);
            ticks = due > g_timer_delivered ? std::min<std::uint64_t>(due - g_timer_delivered, 4) : 0;
            g_timer_delivered = due;
            if (ticks == 0) {
                Sleep(1);   // nothing is due: do not spin the frame limiter's loop at full speed
            }
        }
        const std::uint16_t caller = t_caller_x87;
        t_caller_x87 = kTimerThreadControl;
        for (std::uint64_t k = 0; k < ticks; ++k) {
            call_native_or_seam(kTimerCallback, {0, 0, kTimerState, 0, 0});
            session_on_delivery();   // 9: the session recorder counts and hashes deliveries as the hooked probe does
        }
        t_caller_x87 = caller;
    } else if (realtime) {
        Sleep(1);
    }
    return arena::at<std::uint32_t>(kSimTick);
}

std::uint32_t game_frame_tick(const Args&, const std::string&) {
    // a test driver's frame budget: OT_SPIKE_FRAMES frames of whatever loop calls this, then the process ends (a loop that never ends is
    // not a result)
    static const std::uint32_t limit = std::getenv("OT_SPIKE_FRAMES") != nullptr ? static_cast<std::uint32_t>(std::atoi(std::getenv("OT_SPIKE_FRAMES"))) : 0;
    if (limit != 0 && ++g_frames > limit) {
        std::printf("frame limit: %u frames of Game_FrameTick, sim tick %u, %u input polls\n", static_cast<unsigned>(limit),
                    static_cast<unsigned>(arena::at<std::uint32_t>(kSimTick)), static_cast<unsigned>(platform_input_polls()));
        std::fflush(stdout);
        ExitProcess(0);
    }
    // retail: the Windows message pump (PeekMessage / TranslateMessage / DispatchMessage; 0x75 when the queue is empty, the quit code
    // after WM_QUIT). The Kits store the result in SimulationPublicTick, which retail's timer callback and Timer_UpdateFrameClock
    // advance, so the seam answers the value that is already there; the clock moves a frame.
    // Realtime with a frame rate set, the delivery may wait for the frame's deadline (deliver_tick): the messages are taken after it, so the keyboard state the frame's input
    // poll reads is the one at the end of the wait, not up to a frame older (SDL's own message loop is off: keys reach it only through this pump). Otherwise retail's order.
    const bool waits_first = platform_realtime() && runtime::framepacing::pacing().exact_clock;
    if (!waits_first) {
        platform_pump_window();   // 8a: the HAL window's events (a closed window ends the program)
    }
    deliver_tick("Game_FrameTick");
    if (waits_first) {
        platform_pump_window();
    }
    // 7c: the memory image after that many ticks, taken where the session probe takes retail's (after the delivery), then the run ends
    if (!g_dump_path.empty() && ++g_ticks == g_dump_tick) {
        if (FILE* file = std::fopen(g_dump_path.c_str(), "wb")) {
            std::fwrite(arena::host<const void>(g_dump_base), 1, g_dump_size, file);
            std::fclose(file);
        }
        std::printf("dump: %u bytes from %08x after %u ticks to %s\n", static_cast<unsigned>(g_dump_size), static_cast<unsigned>(g_dump_base),
                    static_cast<unsigned>(g_ticks), g_dump_path.c_str());
        std::fflush(stdout);
        ExitProcess(0);
    }
    return arena::at<std::uint32_t>(kSimTick);
}

std::uint32_t crt_clock(const Args&, const std::string&) { return static_cast<std::uint32_t>(now_micros() / 1000); }

// ReadControllers (0x00489cc0) is a five-byte thunk: jmp PCInput_Poll (0x004e4a90), the seam hal.PCInput_Poll. A thunk is no function of the scope map, so natives call
// it by its address (Input_UpdateActionStates) and PCInput_Poll by its seam (the net layer); both reach the same body here: the devices of the program
// (standalone_platform.cpp: the HAL's keyboard, mouse and gamepad written where DirectInput would have written them), then the game's own bindings.
constexpr std::uint32_t kReadControllers = 0x00489cc0;

std::uint32_t read_controllers(const Args&, const std::string&) { return platform_poll_controllers(); }

// PCInput_SetCursorPos(x, y): the pointer put at a point of the display (standalone_platform.cpp)
std::uint32_t set_cursor_pos(const Args& args, const std::string&) {
    if (args.size() >= 2) {
        platform_set_cursor(static_cast<std::int32_t>(args[0]), static_cast<std::int32_t>(args[1]));
    }
    return 0;
}

// SDXKeyboard_KeyIsDown(this, chord): the chord test of the keyboard wrapper (standalone_platform.cpp)
std::uint32_t keyboard_key_is_down(const Args& args, const std::string&) { return args.size() >= 2 ? platform_key_is_down(args[0], args[1]) : 0; }

}  // namespace

std::uint64_t standalone_clock_micros() { return now_micros(); }
void standalone_advance_clock(std::uint64_t micros) {
    if (platform_realtime()) {
        Sleep(static_cast<DWORD>(std::max<std::uint64_t>(micros / 1000, 1)));   // a wait that is not over: a sleep
    } else {
        g_clock_micros += micros;
    }
}

namespace {

// printf_log / Debug_ReportError: debug text only (the Kits report the format, not the numbers behind it)
std::uint32_t log_text(const Args& args, const std::string& text) {
    constexpr std::uint32_t kAssertFormat = 0x00524198, kAssertLine = 0x00568638, kAssertFile = 0x0056881c;   // "ASSERT!: %s(%i):\t%s\n": the macro's stamp is its file and line
    if (!args.empty() && args[0] == kAssertFormat) {
        std::fprintf(stderr, "game: ASSERT %s(%u): %s\n", arena::host<const char>(kAssertFile),
                     *arena::host<const std::uint32_t>(kAssertLine), text.c_str());
    } else if (!text.empty()) {
        std::fprintf(stderr, "game: %s\n", text.c_str());
    } else if (!args.empty()) {
        // the call's argument words beyond the format are not passed to the seam: the format itself (its conversions unfilled) tells what the game said
        std::string format;
        if (const char* guest = arena::readable_text(args[0]); guest != nullptr) {
            for (std::size_t k = 0; guest[k] != '\0' && k < 200; ++k) {
                const char c = guest[k];
                format.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
            }
            while (!format.empty() && format.back() == ' ') {
                format.pop_back();
            }
        }
        if (format.empty()) {
            std::fprintf(stderr, "game: (log format at %08x)\n", static_cast<unsigned>(args[0]));
        } else {
            std::fprintf(stderr, "game: %s\n", format.c_str());
        }
    }
    return 0;
}

// 9: the cold boot places Init_AtStart's 6 MB block (the game's two memory zones, operator new(0x5dc000)) where the retail recordings have it, 0x04f00020 (the
// Wine heap's first region of that size, behind its 0x20-byte header), inside the range the program reserved at start (standalone_spike.cpp). Nothing in the
// game depends on the address, but the session trace hashes a word inside the zones as an offset from their start (session_hash.h), so a value that only
// looks like a zone pointer (0x05000000 in the fog tables) is rewritten in retail's image and would not be in a block elsewhere. The block is never freed
// before the process ends (Init_AtEnd runs at shut-down).
constexpr std::uint32_t kZoneBlockSize = 0x005dc000;
constexpr std::uint32_t kRetailZoneBlock = 0x04f00020;
bool g_zone_block_placed = false;

// 10f: a retail run that was started another way has the block elsewhere (the capture host starts the game suspended and injects its DLL: the Warehouse
// recordings' block is at 0x091e0020); OT_ZONE_BLOCK=<hex> places the cold boot's there, read by tony.standalone_warehouse from the retail run's own image
std::uint32_t zone_block() {
    static const std::uint32_t block = [] {
        const char* text = std::getenv("OT_ZONE_BLOCK");
        return text != nullptr && *text != '\0' ? static_cast<std::uint32_t>(std::strtoul(text, nullptr, 16)) : kRetailZoneBlock;
    }();
    return block;
}

}  // namespace

std::uint32_t standalone_zone_block() { return zone_block(); }

namespace {

// printf_log (0x004cf850, from its disassembly): the first call makes the log's path, the working directory, "\" (0x00540c34) and "THPCERR.LOG" (0x00548808),
// at 0x00548908 (whose first byte is '0' until then); while the flag 0x00548a08 is set it writes the log's header and clears the flag. The text itself goes to
// stderr here, not to the file.
std::uint32_t printf_log(const Args& args, const std::string& text) {
    constexpr std::uint32_t kLogPath = 0x00548908, kLogPathFresh = 0x00548a08;
    char* path = arena::host<char>(kLogPath);
    if (path[0] == '0') {
        GetCurrentDirectoryA(0x100, path);
        std::strcat(path, arena::text(0x00540c34));
        std::strcat(path, arena::text(0x00548808));
    }
    *arena::host<volatile std::uint32_t>(kLogPathFresh) = 0;
    return log_text(args, text);
}

std::uint32_t crt_new(const Args& args, const std::string&) {
    if (standalone_booting() && !args.empty() && args[0] == kZoneBlockSize && !g_zone_block_placed) {
        g_zone_block_placed = true;
        return zone_block();
    }
    // 11b (docs/PORT64_AUDIT.md D2): the guest heap (guest_arena.hpp), on both builds. Full is a fault: retail's operator new would fail there too,
    // and no caller of it checks
    const std::uint32_t size = args.empty() ? 1 : args[0];
    const std::uint32_t block = arena::heap_allocate(size);
    if (block == 0) {
        throw runtime::ServiceFault("the guest heap cannot hold a block of " + std::to_string(size) + " bytes");
    }
    return block;
}

std::uint32_t crt_free(const Args& args, const std::string&) {
    if (!args.empty() && args[0] == zone_block() && g_zone_block_placed) {
        return 0;   // the zone block of the cold boot is not the heap's
    }
    arena::heap_release(args.empty() ? 0 : args[0]);
    return 0;
}

// FUN_00500400, the vector constructor iterator (array, element size, count, constructor, destructor): the constructor, a thiscall, runs on each element in order. The destructor
// is retail's unwinding handler for a constructor that throws; nothing here throws.
std::uint32_t crt_vector_construct(const Args& args, const std::string&) {
    if (args.size() < 4 || args[3] == 0) {
        return 0;
    }
    for (std::uint32_t k = 0; k < args[2]; ++k) {
        call_native_or_seam(args[3], {args[0] + k * args[1]});
    }
    return 0;
}

// _controlfp(value, mask) (0x005015a6): the control word in the C runtime's encoding, (word & ~mask) | (value & mask), and the answer is the new word. It is kept as a
// value, not loaded into the x87 unit: the natives emulate the precision of their retail caller themselves (kit_bridge.hpp, X87Scope), and the one stretch of game code
// that runs at single precision, the ordering-table walk, computes at it in src/runtime/draw_kit.cpp. The start is Wine's default (53 bits, every exception masked).
constexpr std::uint32_t kControlFp = 0x005015a6;
std::uint32_t g_control_word = 0x0009001f;

std::uint32_t crt_controlfp(const Args& args, const std::string&) {
    if (args.size() >= 2) {
        g_control_word = (g_control_word & ~args[1]) | (args[0] & args[1]);
    }
    return g_control_word;
}

// sprintf / _vsnprintf of the game's C runtime (docs/PORT64_AUDIT.md 5.4 item 4): the format and the argument words are guest data, so the text is made
// by the guest formatter (src/runtime/guest_format.hpp) on both builds; the host's printf never sees a guest word.
runtime::guest::FormatSource format_source(std::function<std::uint32_t()> next_word) {
    return {std::move(next_word), [](std::uint32_t address) { return arena::readable_text(address); }};
}

// a sequence of argument words the caller pushed (sprintf's: args[first..]); a format that wants more is a fault, not stack garbage
std::function<std::uint32_t()> pushed_words(const Args& args, std::size_t first) {
    return [&args, at = first]() mutable -> std::uint32_t {
        if (at >= args.size()) {
            throw runtime::ServiceFault("printf: the format converts more arguments than the caller pushed");
        }
        return args[at++];
    };
}

std::uint32_t crt_sprintf(const Args& args, const std::string&) {
    if (args.size() < 2) {
        throw runtime::ServiceFault("sprintf: fewer than two arguments");
    }
    const std::string text = runtime::guest::format(arena::text(args[1]), format_source(pushed_words(args, 2)));
    std::memcpy(arena::chars(args[0]), text.c_str(), text.size() + 1);
    return static_cast<std::uint32_t>(text.size());
}

// (buffer, size, format, va_list): a 32-bit va_list is a pointer to the argument words in guest memory, which is what the caller passes. The C
// runtime's _vsnprintf: the text and a terminator when it fits, the text alone when it fills the buffer exactly, -1 when it does not fit.
std::uint32_t crt_vsnprintf(const Args& args, const std::string&) {
    if (args.size() < 4) {
        throw runtime::ServiceFault("_vsnprintf: fewer than four arguments");
    }
    const std::uint32_t capacity = args[1];
    std::uint32_t list = args[3];
    const std::string text = runtime::guest::format(arena::text(args[2]), format_source([&list] {
        const std::uint32_t word = arena::at<std::uint32_t>(list);
        list += 4;
        return word;
    }));
    char* buffer = arena::chars(args[0]);
    if (text.size() > capacity) {
        std::memcpy(buffer, text.data(), capacity);
        return 0xffffffffu;
    }
    std::memcpy(buffer, text.data(), text.size());
    if (text.size() < capacity) {
        buffer[text.size()] = '\0';
    }
    return static_cast<std::uint32_t>(text.size());
}

struct Seam {
    std::uint32_t address;
    std::uint32_t (*implementation)(const Args&, const std::string&);
};

// sorted by address
const Seam kSeams[] = {
    {seam::hal::printf_log, &printf_log},
    {seam::hal::Debug_ReportError, &log_text},
    {seam::hal::Game_FrameTick, &game_frame_tick},
    {kReadControllers, &read_controllers},
    {seam::hal::PCInput_Poll, &read_controllers},
    {seam::hal::PCInput_SetCursorPos, &set_cursor_pos},
    {seam::hal::SDXKeyboard_KeyIsDown, &keyboard_key_is_down},
    {seam::crt::CRT_sprintf, &crt_sprintf},
    {seam::crt::CRT_vsnprintf, &crt_vsnprintf},
    {seam::crt::CRT_operator_new, &crt_new},
    {seam::crt::FUN_005002f3, &crt_free},   // operator delete
    {seam::crt::FUN_00500400, &crt_vector_construct},
    {kControlFp, &crt_controlfp},
    {seam::crt::CRT_free, &crt_free},
    {seam::crt::CRT_malloc, &crt_new},
    {seam::crt::FUN_00502ccb, &crt_clock},
};

}  // namespace

// The x87 precision the game has set with _controlfp (a value here, not the unit's: see crt_controlfp), as the unit encodes it: 0 = 24 bits, 2 = 53, 3 = 64. D3D sets 24 bits.
unsigned standalone_precision() {
    const std::uint32_t precision = g_control_word & 0x30000u;   // the C runtime's _MCW_PC: 0x20000 = 24 bits, 0x10000 = 53, 0 = 64
    return precision == 0x20000u ? 0u : precision == 0x10000u ? 2u : 3u;
}

std::uint32_t standalone_sleep_tick() {
    platform_drain_messages();   // a wait loop that sleeps here is the only pump for a while: keep the window answering
    return deliver_tick("WINMAIN_Sleep");
}

bool seam_implemented(std::uint32_t address) {
    if (has_movie_seam(address) || has_file_seam(address) || has_audio_seam(address) || has_host_seam(address)) {
        return true;
    }
    return std::any_of(std::begin(kSeams), std::end(kSeams), [address](const Seam& seam) { return seam.address == address; });
}

bool call_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string& text, std::uint32_t& result) {
    if (call_movie_seam(address, args, text, result) || call_file_seam(address, args, text, result) || call_audio_seam(address, args, text, result) ||
        call_host_seam(address, args, text, result)) {
        return true;
    }
    for (const Seam& seam : kSeams) {
        if (seam.address == address) {
            result = seam.implementation(args, text);
            return true;
        }
    }
    return false;
}

}  // namespace opentony::kitdll

// 7c: write `size` bytes from `base` to `path` after the `tick`-th Game_FrameTick (its timer delivery included), then end the process: the standalone's memory image,
// for the comparison with the session probe's at the same delivery (tony/standalone_frame.py)
extern "C" __declspec(dllexport) void __cdecl ot_standalone_set_dump(const char* path, std::uint32_t tick, std::uint32_t base, std::uint32_t size) {
    opentony::kitdll::g_dump_path = path != nullptr ? path : "";
    opentony::kitdll::g_dump_tick = tick;
    opentony::kitdll::g_dump_base = base;
    opentony::kitdll::g_dump_size = size;
    opentony::kitdll::g_ticks = 0;
}
