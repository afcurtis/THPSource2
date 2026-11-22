// A Warehouse recording in the standalone (milestone 10f): the retail recorder's bootstrap and frame boundaries (src/capture/win32/hooks.cpp, the in-process capture of
// docs/CAPTURE_INPROC.md) for a program with no retail code, so a scenario of re/scenarios/warehouse-*.yml runs here and what it records is compared with the retail
// recording of the same scenario (tony/standalone_warehouse.py).
//
//   OT_SCENARIO=<file>       the scenario, text, one line a fact (tony standalone warehouse writes it from the scenario's yml):
//                                level <index>          the level the launch is told to run (12 = the Warehouse)
//                                skater <row>           the player's skater, chosen at the level select (0..19, the select screen's row; unset: the slot's)
//                                look <0..3>            the skater's look (a look the row has no model for, or one still locked, is left alone; the run says which it got)
//                                frames <n>             the physics frames to record
//                                sha256 <hex>           the retail executable's identity, as the capture header carries it
//                                action <mask> <start> <hold>   the low word of the action mask, from recorded frame <start> for <hold> frames (hex or decimal)
//   OT_SCENARIO_OUT=<file>   where the capture goes: the .otcap of the retail recorder (the same wire layout, so tony.capture decodes it); every frame is written as
//                            it is recorded, because a FATAL ends the process at once and the frames before it are the evidence
//
// What the capture DLL does at the retail code, and where it is done here (each retail seam is a call the Kits make through the resolver, or the poll):
//   frontend_key_state 0x004e41b0 (the main menu's Q test, once)    -> scenario_before_call: the key byte is set before the test
//   frontend_play_result 0x004532aa (the main menu's answer)        -> scenario_after_call on 0x0046aee0: the answer becomes 0x26 (outcome 4: the level select)
//   frontend_level_select 0x0045355d (the level select, skipped)    -> scenario_before_call on the screen's pre-load, call and unload: skipped, the answer is the level
//   launch_level 0x004544a0 (the launch's first argument)           -> scenario_before_call: the level (the Kit calls the launch itself, so the level select's answer is what reaches it)
//   frontend_summary 0x0047fb36 (the goals screen's log line)       -> scenario_before_call on printf_log: Enter is pressed for five tests of the key and released for fifteen
//   action_mask_injection 0x00489a15 (after the poll's bindings)    -> scenario_after_bindings: the low word of the mask of the frame being recorded
//   physics_frame 0x0049e680 (the player blob before and after)     -> scenario_before_call / scenario_after_call on Skater_PhysicsFrame
// Not recorded yet (they are inside functions the Kits own, and no frame runs before milestone 10g): the timer-update and clock-read samples, the simulation-time
// store, the shared-random calls. A frame is the player blob before and after, the input, the timing snapshot and the two physics-boundary timer samples.
#include "kit_bridge.hpp"

#include "shared_buffer.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "guest_arena.hpp"
#include "standalone_options.hpp"

namespace opentony::kitdll {
namespace {

constexpr std::uint32_t kPlayerTable = 0x0056a858;
constexpr std::uint32_t kActionMask = 0x006a3f1c;
constexpr std::uint32_t kAirControl = 0x0056b7f0;
constexpr std::uint32_t kAnimationClock = 0x005685f4;
constexpr std::uint32_t kAnimationScale = 0x0056865c;
constexpr std::uint32_t kAnimationScaleSquare = 0x00568804;
constexpr std::uint32_t kAnimationClockAccumulator = 0x00568810;
constexpr std::uint32_t kSimulationTime = 0x0056e320;
constexpr std::uint32_t kTimingDelta = 0x0056a93c;
constexpr std::uint32_t kTimerState = 0x006a05a0;
constexpr std::uint32_t kTimerPublicAccumulator = 0x006a0590;
constexpr std::uint32_t kTimerSimulationAccumulator = 0x006a0598;
constexpr std::uint32_t kTimerPublicTick = 0x0056e31c;
constexpr std::uint32_t kPauseGateA = 0x00561c04;
constexpr std::uint32_t kPauseGateB = 0x0056a8e0;
constexpr std::uint32_t kKeyboardState = 0x006a43e4;
constexpr std::uint32_t kControlMode = 0x006a6964;
constexpr std::uint32_t kSkaterSlot = 0x0056c584;      // the first player's slot entry: character, custom slot, costume (standalone_options.cpp)

constexpr std::uint32_t kMainMenuScreen = 0x0046aee0;
constexpr std::uint32_t kKeyDown = 0x004e41b0;
constexpr std::uint32_t kPreLoad = 0x004a90f0;
constexpr std::uint32_t kPreUnload = 0x004a9330;
constexpr std::uint32_t kLevelSelectScreen = 0x004588f0;
constexpr std::uint32_t kLaunchLevel = 0x004544a0;
constexpr std::uint32_t kPrintfLog = 0x004cf850;
constexpr std::uint32_t kSummaryLogFormat = 0x00535ad4;   // NETMENU_InitSummary's log line: the goals screen is up
constexpr std::uint8_t kSummaryScanCode = 0x1c;           // Enter
constexpr std::uint32_t kPhysicsFrame = 0x0049e680;
constexpr std::uint32_t kLevelSelectPre = 0x0052cc64;   // the level select's .pre file name
constexpr std::uint32_t kPlayResult = 0x26;             // the main menu's answer that leads to the level select (0x26 - 0x20 = 6: outcome 4)
constexpr std::uint8_t kConfirmScanCode = 0x10;

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

struct Scenario {
    bool loaded = false;
    bool active = false;
    std::uint32_t level = 0;
    int skater = -1;
    int look = -1;
    std::uint32_t frames = 0;
    std::uint8_t sha256[32] = {};
    std::vector<CaptureActionInterval> actions;
    std::string output;

    // the frontend bootstrap: each step happens once, in this order
    bool menu_key_armed = true;
    bool play_armed = true;
    int level_select_step = 0;      // 0 waiting for the pre-load, 1 the screen, 2 the unload, 3 done
    bool launch_armed = true;
    bool summary_armed = true;
    bool summary_active = false;
    std::uint32_t summary_ticks = 0;

    // the recorder
    CaptureBuffer buffer{};
    std::vector<unsigned char> memory;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool initial_captured = false;
    bool gameplay_ready = false;          // a poll has run: the setup's own physics calls before it are not frames
    bool frame_active = false;
    std::uint32_t frame_index = 0;
    std::uint32_t injected_frame = 0xffffffffu;
    std::uint32_t player = 0;
    std::uint32_t input_mask = 0;
    std::uint32_t input_flags = 0;
    std::vector<std::uint8_t> before = std::vector<std::uint8_t>(OTCAP_PLAYER_BLOB_SIZE);
    CaptureTimingSnapshot timing_before{};
    std::vector<CaptureTimerSample> pending;      // the post-physics sample of the previous frame
    std::vector<CaptureTimerSample> samples;      // this frame's
} g_scenario;

void say(const std::string& text) {
    std::printf("scenario: %s\n", text.c_str());
    std::fflush(stdout);
}

[[noreturn]] void stop_with(int code, const std::string& text) {
    say(text);
    std::fflush(stdout);
    ExitProcess(static_cast<UINT>(code));
}

CaptureHeader* header() { return reinterpret_cast<CaptureHeader*>(g_scenario.buffer.mapping); }

bool parse_sha256(const std::string& text, std::uint8_t out[32]) {
    if (text.size() != 64) {
        return false;
    }
    for (int k = 0; k < 32; ++k) {
        unsigned value = 0;
        if (std::sscanf(text.c_str() + 2 * k, "%2x", &value) != 1) {
            return false;
        }
        out[k] = static_cast<std::uint8_t>(value);
    }
    return true;
}

void write_at(std::uint32_t offset, const void* bytes, std::uint32_t size) {
    DWORD written = 0;
    if (g_scenario.file == INVALID_HANDLE_VALUE || SetFilePointer(g_scenario.file, static_cast<LONG>(offset), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !WriteFile(g_scenario.file, bytes, size, &written, nullptr) || written != size) {
        stop_with(67, "cannot write the capture " + g_scenario.output);
    }
}

void write_header() { write_at(0, g_scenario.buffer.mapping, sizeof(CaptureHeader)); }

// the header, configuration and initial state of the retail host (src/capture/win32/capture_host.cpp), in a 64 MiB buffer whose used part goes to the file
void start_recorder() {
    Scenario& sc = g_scenario;
    // a longer run than the retail capture's 64 MiB holds (about 2500 frames) is a standalone-only experiment (OT_SCENARIO_MAPPING_MB): nothing compares it with retail
    std::uint32_t mapping_size = OTCAP_MAPPING_SIZE;
    if (const char* mb = std::getenv("OT_SCENARIO_MAPPING_MB")) {
        const unsigned long megabytes = std::strtoul(mb, nullptr, 10);
        if (megabytes >= 64 && megabytes <= 1024) {
            mapping_size = static_cast<std::uint32_t>(megabytes) * 1024u * 1024u;
        }
    }
    sc.memory.assign(mapping_size, 0);
    auto* base = sc.memory.data();
    auto* head = reinterpret_cast<CaptureHeader*>(base);
    auto* config = reinterpret_cast<CaptureConfig*>(base + OTCAP_HEADER_BYTES);
    auto* initial = reinterpret_cast<CaptureInitialState*>(reinterpret_cast<unsigned char*>(config) + OTCAP_CONFIG_BYTES);
    head->version = OTCAP_VERSION;
    std::memcpy(head->magic, OTCAP_MAGIC, OTCAP_MAGIC_SIZE);
    head->header_size = OTCAP_HEADER_BYTES;
    head->config_offset = OTCAP_HEADER_BYTES;
    head->config_size = OTCAP_CONFIG_BYTES;
    head->initial_state_offset = OTCAP_HEADER_BYTES + OTCAP_CONFIG_BYTES;
    head->initial_state_size = OTCAP_INITIAL_STATE_BYTES;
    head->data_offset = otcap_align4096(head->initial_state_offset + head->initial_state_size);
    head->mapping_size = mapping_size;
    if (sc.frames == 0 || sc.frames > otcap_max_frames(head->mapping_size, head->data_offset)) {
        stop_with(66, "frames " + std::to_string(sc.frames) + " is outside the capture's capacity");
    }
    head->bytes_used = head->data_offset;
    head->frame_limit = sc.frames;
    head->level_index = sc.level;
    head->image_base = 0x00400000u;
    head->player_blob_size = OTCAP_PLAYER_BLOB_SIZE;
    head->process_id = GetCurrentProcessId();
    head->status = OTCAP_STATUS_READY;
    std::memcpy(head->build_sha256, sc.sha256, sizeof sc.sha256);
    config->version = OTCAP_VERSION;
    config->size = OTCAP_CONFIG_BYTES;
    config->frame_limit = sc.frames;
    config->action_count = static_cast<std::uint32_t>(sc.actions.size() < OTCAP_MAX_ACTION_INTERVALS ? sc.actions.size() : OTCAP_MAX_ACTION_INTERVALS);   // the header holds 128; the lookup below has no limit
    config->level_index = sc.level;
    std::memcpy(config->build_sha256, sc.sha256, sizeof sc.sha256);
    for (std::size_t k = 0; k < sc.actions.size() && k < OTCAP_MAX_ACTION_INTERVALS; ++k) {
        config->actions[k] = sc.actions[k];
    }
    initial->size = OTCAP_INITIAL_STATE_BYTES;
    if (!ot_capture_buffer_init(&sc.buffer, base, mapping_size)) {
        stop_with(66, "the capture buffer refused its configuration");
    }
    sc.file = CreateFileA(sc.output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sc.file == INVALID_HANDLE_VALUE) {
        stop_with(66, "cannot create " + sc.output);
    }
    // the file has the mapping's size, as the host's saved mapping has: the decoder checks it
    if (SetFilePointer(sc.file, static_cast<LONG>(mapping_size), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER || !SetEndOfFile(sc.file)) {
        stop_with(66, "cannot size " + sc.output);
    }
    write_at(0, base, head->data_offset);
}

void load() {
    Scenario& sc = g_scenario;
    sc.loaded = true;
    const char* path = std::getenv("OT_SCENARIO");
    if (path == nullptr || *path == '\0') {
        return;
    }
    std::ifstream file(path);
    if (!file) {
        stop_with(66, std::string("cannot read OT_SCENARIO ") + path);
    }
    std::string line;
    while (std::getline(file, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string what;
        if (!(words >> what)) {
            continue;
        }
        if (what == "level") {
            words >> sc.level;
        } else if (what == "skater") {
            words >> sc.skater;
        } else if (what == "look") {
            words >> sc.look;
        } else if (what == "frames") {
            words >> sc.frames;
        } else if (what == "sha256") {
            std::string hex;
            words >> hex;
            if (!parse_sha256(hex, sc.sha256)) {
                stop_with(66, "sha256 is not 64 hexadecimal digits");
            }
        } else if (what == "action") {
            std::string mask, start, hold;
            words >> mask >> start >> hold;
            sc.actions.push_back({static_cast<std::uint32_t>(std::strtoul(mask.c_str(), nullptr, 0)), static_cast<std::uint32_t>(std::strtoul(start.c_str(), nullptr, 0)),
                                  static_cast<std::uint32_t>(std::strtoul(hold.c_str(), nullptr, 0))});
        } else {
            stop_with(66, "OT_SCENARIO: not a scenario line: " + line);
        }
    }
    if (const char* output = std::getenv("OT_SCENARIO_OUT")) {
        sc.output = output;
    }
    if (sc.output.empty()) {
        stop_with(66, "OT_SCENARIO needs OT_SCENARIO_OUT");
    }
    sc.active = true;
    start_recorder();
    say("level " + std::to_string(sc.level) + ", " + std::to_string(sc.frames) + " frames, " + std::to_string(sc.actions.size()) + " action intervals -> " + sc.output);
}

// ---- the recorder -----------------------------------------------------------------------------------------------------------------------------

CaptureTimingSnapshot read_timing() {
    CaptureTimingSnapshot timing{};
    timing.animation_clock = retail<std::uint32_t>(kAnimationClock);
    timing.animation_time_scale = retail<std::uint32_t>(kAnimationScale);
    timing.animation_time_scale_square = retail<std::uint32_t>(kAnimationScaleSquare);
    timing.animation_clock_accumulator = retail<std::uint32_t>(kAnimationClockAccumulator);
    timing.simulation_time = retail<std::uint32_t>(kSimulationTime);
    timing.timing_delta_q11 = retail<std::uint32_t>(kTimingDelta);
    return timing;
}

CaptureTimerSample read_timer_sample(std::uint32_t phase, std::uint32_t frame) {
    CaptureTimerSample sample{};
    sample.phase = phase;
    sample.frame_index = frame;
    sample.interval_ms = retail<std::uint32_t>(kTimerState + 0x04);
    sample.accumulated_ms = retail<std::uint32_t>(kTimerState + 0x0c);
    sample.public_tick = retail<std::uint32_t>(kTimerPublicTick);
    sample.simulation_time = retail<std::uint32_t>(kSimulationTime);
    sample.pause_gate_a = retail<std::uint32_t>(kPauseGateA);
    sample.pause_gate_b = retail<std::uint32_t>(kPauseGateB);
    std::memcpy(&sample.public_accumulator_raw, arena::host<const void>(kTimerPublicAccumulator), sizeof sample.public_accumulator_raw);
    std::memcpy(&sample.simulation_accumulator_raw, arena::host<const void>(kTimerSimulationAccumulator),
                sizeof sample.simulation_accumulator_raw);
    return sample;
}

void capture_initial_timer_state() {
    if (g_scenario.initial_captured) {
        return;
    }
    g_scenario.initial_captured = true;
    auto* head = header();
    auto* initial = reinterpret_cast<CaptureInitialState*>(g_scenario.buffer.mapping + head->initial_state_offset);
    initial->timer_handle = retail<std::uint32_t>(kTimerState + 0x00);
    initial->interval_ms = retail<std::uint32_t>(kTimerState + 0x04);
    initial->opaque_08 = retail<std::uint32_t>(kTimerState + 0x08);
    initial->accumulated_ms = retail<std::uint32_t>(kTimerState + 0x0c);
    initial->opaque_10 = retail<std::uint32_t>(kTimerState + 0x10);
    std::memcpy(&initial->public_accumulator_raw, arena::host<const void>(kTimerPublicAccumulator), sizeof initial->public_accumulator_raw);
    std::memcpy(&initial->simulation_accumulator_raw, arena::host<const void>(kTimerSimulationAccumulator),
                sizeof initial->simulation_accumulator_raw);
    initial->public_tick = retail<std::uint32_t>(kTimerPublicTick);
    initial->simulation_time = retail<std::uint32_t>(kSimulationTime);
    initial->simulation_pause_gate_a = retail<std::uint32_t>(kPauseGateA);
    initial->simulation_pause_gate_b = retail<std::uint32_t>(kPauseGateB);
    write_at(head->initial_state_offset, initial, sizeof *initial);
}

// ot_capture_physics_before: the first physics frame of the player in table entry zero after the first poll is a recorded frame
void physics_before(std::uint32_t player) {
    Scenario& sc = g_scenario;
    auto* head = header();
    if (sc.frame_active || !sc.gameplay_ready || (head->status != OTCAP_STATUS_READY && head->status != OTCAP_STATUS_CAPTURING) || head->frame_count >= head->frame_limit) {
        return;
    }
    sc.summary_active = false;   // (retail resets it here, before the player test)
    capture_initial_timer_state();
    if (player == 0 || player != retail<std::uint32_t>(kPlayerTable)) {
        return;   // the second skater, and the setup's calls before the player table has its first entry
    }
    sc.frame_index = head->frame_count;
    sc.samples = sc.pending;
    sc.pending.clear();
    sc.samples.push_back(read_timer_sample(OTCAP_TIMER_PHASE_PHYSICS_ENTRY, sc.frame_index));
    std::memcpy(sc.before.data(), arena::host<const void>(player), OTCAP_PLAYER_BLOB_SIZE);
    sc.timing_before = read_timing();
    sc.player = player;
    sc.input_mask = retail<std::uint32_t>(kActionMask);
    sc.input_flags = (retail<std::uint32_t>(kAirControl) != 0 ? OTCAP_INPUT_FLAG_AIR_CONTROL : 0u) | (sc.injected_frame == head->frame_count ? OTCAP_INPUT_FLAG_INJECTED : 0u);
    sc.frame_active = true;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&head->status), OTCAP_STATUS_CAPTURING);
    // retail's capture DLL retires its bootstrap hooks here (the first gameplay frame), the two movie entries among them: from now on the music opens
    session_end_movie_skip();
}

// ot_capture_physics_after: the frame is complete; the post-physics sample belongs to the next frame
void physics_after() {
    Scenario& sc = g_scenario;
    if (!sc.frame_active) {
        return;
    }
    auto* head = header();
    sc.pending.push_back(read_timer_sample(OTCAP_TIMER_PHASE_POST_PHYSICS, sc.frame_index + 1));
    std::vector<std::uint8_t> after(OTCAP_PLAYER_BLOB_SIZE);
    std::memcpy(after.data(), arena::host<const void>(sc.player), OTCAP_PLAYER_BLOB_SIZE);
    const CaptureTimingSnapshot timing_after = read_timing();
    if (!ot_capture_append_frame(&sc.buffer, sc.frame_index, sc.input_mask, sc.input_flags, sc.player, sc.before.data(), after.data(), &sc.timing_before, &timing_after, nullptr, 0,
                                 sc.samples.data(), static_cast<std::uint32_t>(sc.samples.size()))) {
        stop_with(68, "the capture refused frame " + std::to_string(sc.frame_index));
    }
    sc.frame_active = false;
    const bool complete = head->frame_count >= head->frame_limit;
    if (complete) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&head->status), OTCAP_STATUS_COMPLETE);
    }
    write_at(head->data_offset + sc.frame_index * OTCAP_FRAME_BYTES, sc.buffer.mapping + head->data_offset + sc.frame_index * OTCAP_FRAME_BYTES, OTCAP_FRAME_BYTES);
    write_header();
    if (complete) {
        CloseHandle(sc.file);
        sc.file = INVALID_HANDLE_VALUE;
        say("recorded " + std::to_string(head->frame_count) + " frames; stopping");
        session_stop();
    }
}

std::uint32_t action_for_frame(std::uint32_t frame) {
    // ot_capture_action_mask's rule (the OR of every interval the frame falls in) over all the intervals, not the header's first 128
    std::uint32_t mask = 0;
    for (const CaptureActionInterval& action : g_scenario.actions) {
        if (frame >= action.start_frame && frame - action.start_frame < action.hold_frames) {
            mask |= action.action_mask;
        }
    }
    return mask;
}

// the goals screen's hook, run at each test of a key (0x004e41b0): Enter for the first five tests since the screen came up, then up for fifteen, then the hook is done
void summary_tick() {
    Scenario& sc = g_scenario;
    if (sc.summary_active && sc.summary_ticks < 20) {
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = sc.summary_ticks < 5 ? 0x80 : 0;
        if (++sc.summary_ticks == 20) {
            sc.summary_active = false;
        }
    }
}

}  // namespace

bool scenario_active() {
    if (!g_scenario.loaded) {
        load();
    }
    return g_scenario.active;
}

// before a call the resolver makes: the frontend's seams answer or change it, the physics frame is observed. `live` maps a Kit address to where the memory really is.
bool scenario_before_call(std::uint32_t address, std::vector<std::uint32_t>& args, const char* caller, std::uint32_t (*live)(std::uint32_t), std::uint32_t& result) {
    Scenario& sc = g_scenario;
    result = 0;
    if (address == kPhysicsFrame && !args.empty()) {
        physics_before(args[0]);
        return false;
    }
    if (address == kKeyDown) {
        if (sc.menu_key_armed && !args.empty() && args[0] == kConfirmScanCode && caller != nullptr && std::strcmp(caller, "MainMenu_RunWrapper") == 0) {
            sc.menu_key_armed = false;
            retail<std::uint8_t>(kKeyboardState + kConfirmScanCode) = 0x80;
            say("main menu: the confirm key is down");
        }
        summary_tick();
        return false;
    }
    if (address == kPrintfLog && sc.summary_armed && !args.empty() && args[0] == kSummaryLogFormat) {
        sc.summary_armed = false;
        sc.summary_active = true;
        sc.summary_ticks = 0;
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = 0x80;
        say("goals screen: Enter is down");
        return false;
    }
    // the level select, skipped as the retail hook jumps over it: the screen's pre-load, the screen (its answer is the level), its unload
    if (sc.level_select_step == 0 && address == kPreLoad && args.size() >= 2 && args[1] == kLevelSelectPre && !sc.play_armed) {
        sc.level_select_step = 1;
        return true;
    }
    if (sc.level_select_step == 1 && address == kLevelSelectScreen && args.size() >= 2) {
        *arena::host<volatile std::uint32_t>(live(args[1])) = sc.level;
        sc.level_select_step = 2;
        say("level select: skipped, level " + std::to_string(sc.level));
        if (sc.skater >= 0 || sc.look >= 0) {
            const int look = options_choose_skater(sc.skater, sc.look);
            say("skater " + std::to_string(retail<std::uint32_t>(kSkaterSlot)) + ", look " + std::to_string(look) + " (asked " + std::to_string(sc.skater) + ", " +
                std::to_string(sc.look) + ")");
        }
        return true;
    }
    if (sc.level_select_step == 2 && address == kPreUnload && args.size() >= 2 && args[1] == kLevelSelectPre) {
        sc.level_select_step = 3;
        return true;
    }
    if (sc.launch_armed && address == kLaunchLevel && !args.empty()) {
        sc.launch_armed = false;
        say("launch: level " + std::to_string(args[0]) + " becomes " + std::to_string(sc.level));
        args[0] = sc.level;
        return false;
    }
    return false;
}

void scenario_after_call(std::uint32_t address, const std::vector<std::uint32_t>& args, std::uint32_t (*live)(std::uint32_t)) {
    Scenario& sc = g_scenario;
    if (address == kPhysicsFrame) {
        physics_after();
    } else if (address == kMainMenuScreen && sc.play_armed && args.size() >= 2) {
        sc.play_armed = false;
        *arena::host<volatile std::uint32_t>(live(args[1])) = kPlayResult;
        say("main menu: the answer is 0x26");
    }
}

// after the keyboard device update of a poll: DirectInput has written the real key bytes, so a key the scenario pulsed (the main menu's confirm key, the goals screen's
// Enter) is up again; the poll's tests (0x004e41b0) pulse it again where the retail hook does
void scenario_after_keyboard() {
    if (scenario_active()) {
        retail<std::uint8_t>(kKeyboardState + kConfirmScanCode) = 0;
        retail<std::uint8_t>(kKeyboardState + kSummaryScanCode) = 0;
    }
}

// before the poll's bindings: PCInput_BuildActionMask tests Enter once in the menu control mode (0x004e45e5), a call to the key test that the Kit makes inside itself
void scenario_before_bindings() {
    if (scenario_active() && retail<std::uint32_t>(kControlMode) != 0) {
        summary_tick();
    }
}

// after the poll's bindings: the low word of the mask of the frame being recorded (a frame is recorded after the first poll; the frames recorded so far number it)
void scenario_after_bindings() {
    if (!scenario_active()) {
        return;
    }
    Scenario& sc = g_scenario;
    auto* head = header();
    if (head->status != OTCAP_STATUS_READY && head->status != OTCAP_STATUS_CAPTURING) {
        return;
    }
    sc.gameplay_ready = true;
    const std::uint32_t mask = action_for_frame(head->frame_count);
    if (mask != 0) {
        sc.injected_frame = head->frame_count;
        retail<std::uint32_t>(kActionMask) = (retail<std::uint32_t>(kActionMask) & 0xffff0000u) | (mask & 0xffffu);
    }
}

}  // namespace opentony::kitdll
