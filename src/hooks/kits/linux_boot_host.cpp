#include "linux_boot_host.hpp"
#include "linux_guest_memory.hpp"

#include "kit_bridge.hpp"
#include "draw_kit.hpp"
#include "frame_pacing.hpp"
#include "guest_format.hpp"
#include "pkr_asset.hpp"
#include "seam_addresses_generated.hpp"
#include "standalone_thunks_generated.hpp"
#include "../../launcher/thpsource2/core/ini.hpp"

#ifdef OPENTONY_LINUX_MUSIC
#include "audio_game.hpp"
#include "linux_music.hpp"
#endif

#ifdef OPENTONY_LINUX_SDL
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"
#include "../../hal/sdl/sdl_video.h"
#include "standalone_gamepad.hpp"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

void opentony_standalone_video_hal(const OtHalVideoBackend* video);

namespace opentony::kitdll {
namespace {

const auto clock_origin = std::chrono::steady_clock::now();
std::uint32_t control_word = 0x0009001f;
constexpr std::uint32_t kTimerState = 0x006a05a0;
constexpr std::uint32_t kTimerCallback = 0x004dace0;
constexpr std::uint32_t kSimTick = 0x0056e31c;
constexpr std::uint32_t kGameTick = 0x0056e320;
constexpr std::uint32_t kFrameAccumulator = 0x006a0590;
constexpr std::uint32_t kGameAccumulator = 0x006a0598;
constexpr std::uint32_t kLastSceneTick = 0x0056e840;
constexpr std::uint32_t kPauseFlag = 0x00561c04;
constexpr std::uint32_t kPauseFlag2 = 0x0056a8e0;
constexpr std::uint32_t kPackagePreload = 0x004e9780;
constexpr std::uint32_t kStreamClose = 0x00500e7b;
constexpr std::uint32_t kStreamRead = 0x00502358;
constexpr std::uint32_t kWorkingDirectory = 0x006a71bc;
constexpr std::uint32_t kPackageName = 0x006a72bc;
constexpr std::uint32_t kGameDirectory = 0x029da3c4;
constexpr std::uint32_t kOptionsPath = 0x029d8378;
constexpr std::uint32_t kBackendOpen = 0x004e75b0;
constexpr std::uint32_t kBackendSeek = 0x004e79f0;
constexpr std::uint32_t kBackendRead = 0x004e7ad0;
constexpr std::uint32_t kBackendClose = 0x004e7bc0;
constexpr std::uint32_t kFailed = 0xffffffffu;

const char* settings_file() {
    const char* path = std::getenv("OPENTONY_SETTINGS");
    return path != nullptr && *path != '\0' ? path : "THawk2.ini";
}

runtime::framepacing::Pacing configured_pacing() {
    runtime::framepacing::Pacing pacing;
    const char* value = std::getenv("OT_FRAME_RATE");
    if (value != nullptr && (std::strcmp(value, "60") == 0 || std::strcmp(value, "120") == 0)) {
        const bool high = std::strcmp(value, "120") == 0;
        pacing.limiter_ticks = 1;
        pacing.exact_clock = true;
        pacing.count_30hz = true;
        pacing.period_ticks = high ? 0.5 : 1.0;
        pacing.sub_tick = high;
        pacing.carry_fraction = high;
    }
    return pacing;
}

template <std::size_t>
using Word = std::uint32_t;

template <std::size_t... I>
std::uint32_t invoke(const NativeEntry& native, const std::uint32_t* args, std::index_sequence<I...>) {
    if (native.convention == Convention::Thiscall) {
        using Function = std::uint32_t (*)(std::uint32_t, std::uint32_t, Word<I>...);
        return reinterpret_cast<Function>(native.function)(args[0], 0, args[I + 1]...);
    }
    using Function = std::uint32_t (*)(Word<I>...);
    return reinterpret_cast<Function>(native.function)(args[I]...);
}

template <std::size_t... N>
std::uint32_t dispatch(const NativeEntry& native, const std::uint32_t* args, std::index_sequence<N...>) {
    using Function = std::uint32_t (*)(const NativeEntry&, const std::uint32_t*);
    static constexpr Function calls[] = {
        +[](const NativeEntry& entry, const std::uint32_t* words) {
            return invoke(entry, words, std::make_index_sequence<N>{});
        }...
    };
    return calls[native.stack_args](native, args);
}

class BootHost final : public runtime::ServiceHost {
public:
    BootHost(LinuxGuestMemory& arena, const std::filesystem::path& game_dir)
        : arena_(arena), package_(game_dir.empty() ? game_dir : std::filesystem::absolute(game_dir) / "ALL.PKR"),
          settings_(thpsource2::IniFile::load(settings_file()))
#ifdef OPENTONY_LINUX_MUSIC
          , music_(game_dir)
#endif
    {
        memory_.make_live(nullptr, false, arena.base());
        services_.attach(this);
        make_kits(memory_, services_);
    }

    std::uint32_t resolve(std::uint32_t address, const std::vector<std::uint32_t>& args, bool virtual_call = false,
                          const std::string& text = {}) {
#ifdef OPENTONY_LINUX_MUSIC
        namespace audio = runtime::pcplatform::audio;
        if (address == seam::bink::StartBinkMovie && args.size() >= 2 && (args[1] & 0xff) != 0) {
            const auto name = !text.empty() ? text : memory_.cstring(args[0], 0x100);
            if (music_.open(name)) {
                memory_.set32(audio::kMovieHandle, 1);
                return 1;
            }
            memory_.set32(audio::kMovieHandle, 0);
            return 0;
        }
        if (address == seam::bink::CreateBinkSurface) {
            return music_.start();
        }
        if (address == seam::crt::CRT_beginthread && !args.empty() && args[0] == seam::bink::PCMOVIE_StreamThread) {
            music_active_ = true;  // The frame pump services the queue on Linux.
            return 1;
        }
        if (address == seam::bink::CloseBinkMovie) {
            stop_music();
            return 0;
        }
#endif
        // Use the same startup movie skip as the recorded boot-menu scenario.
        if (address == 0x004e5ec0 || address == seam::bink::StartBinkMovie || address == seam::bink::CloseBinkMovie) {
            return 0;
        }
        if (address == seam::bink::BinkMovieUpdate) {
            // No clip was opened on this host. Follow the standalone's end-of-clip path.
            return resolve(0x004e7140, {});  // PCMOVIE_StopFMV
        }
        const auto* end = kNatives + kNativeCount;
        const auto* native = std::lower_bound(kNatives, end, address, [](const NativeEntry& entry, std::uint32_t value) {
            return entry.address < value;
        });
        if (native != end && native->address == address) {
            constexpr std::size_t kMaxStackArgs = 16;
            const bool has_self = native->convention == Convention::Thiscall;
            const std::size_t required = native->stack_args + (has_self ? 1u : 0u);
            const bool skip_self = !has_self && (virtual_call ||
                (native->convention == Convention::Stdcall && args.size() == required + 1));
            const auto words = skip_self && !args.empty() ? std::span(args).subspan(1) : std::span(args);
            if (native->stack_args > kMaxStackArgs || words.size() < required ||
                (native->convention != Convention::Cdecl && words.size() != required)) {
                throw runtime::ServiceFault(std::string(native->name) + ": unexpected argument count");
            }
            const auto saved = x87_control();
            x87_load(t_caller_x87);
            if (address == 0x0046aee0) {  // MainMenu_RunWrapper
                menu_active_ = true;
            }
            const auto result = dispatch(*native, words.data(), std::make_index_sequence<kMaxStackArgs + 1>{});
            if (address == 0x0046aee0) {
                menu_active_ = false;
            }
            if (address == 0x00413f70) {  // Options_Init
                apply_audio_level("OT_SFX_LEVEL", 0x0055fb3c, 0x0052be44, 0x0056a2e8, 0x555);
                apply_audio_level("OT_MUSIC_LEVEL", 0x0055fb40, 0x0052be48, 0x0056a2ec, 0x15);
            }
            x87_load(saved);
            return result;
        }

        if (address == seam::crt::CRT_atexit && args.size() == 1) {
            if (args.front() == 0) {
                return 0xffffffffu;
            }
            exit_callbacks_.push_back(args.front());
            return 0;
        }
        if (address == seam::crt::FUN_00500400 && args.size() >= 4) {  // CRT vector constructor iterator
            if (args[3] != 0) {
                for (std::uint32_t index = 0; index < args[2]; ++index) {
                    resolve(args[3], {args[0] + index * args[1]});
                }
            }
            return 0;
        }
        if (address == seam::hal::PCinit && args.empty()) {
            return pc_init();
        }
        if (address == seam::hal::CD_Recheck) {
            // The standalone reads the installed package directly.
            if (!std::filesystem::is_regular_file(package_)) {
                throw runtime::ServiceFault("game package is missing: " + package_.string());
            }
            return 0;
        }
        if (address == seam::crt::PKR2_OpenPackageStream && args.size() == 2) {
            const std::string_view mode = arena::text(args[1]);
            const bool reset_options = args[0] == kOptionsPath && mode.starts_with('w');
            if (!reset_options && (arena::text(args[0]) != package_.string() || mode != "rb")) {
                throw runtime::ServiceFault("unsupported package stream path or mode");
            }
            for (std::size_t slot = 0; slot < streams_.size(); ++slot) {
                if (streams_[slot] == nullptr) {
                    streams_[slot].reset(reset_options ? std::tmpfile() : std::fopen(package_.c_str(), "rb"));
                    if (reset_options && streams_[slot] != nullptr && std::getenv("OPENTONY_SETTINGS") == nullptr) {
                        settings_ = thpsource2::IniFile{};
                    }
                    return streams_[slot] == nullptr ? 0 : static_cast<std::uint32_t>(slot + 1);
                }
            }
            return 0;
        }
        if (address == kStreamRead && args.size() == 4 && stream(args[3]) != nullptr) {
            const auto items = std::fread(arena::host<void>(args[0]), args[1], args[2], stream(args[3]));
            if (items != args[2] && args[1] != 0) {
                throw runtime::ServiceFault("short read in " + package_.string());
            }
            return static_cast<std::uint32_t>(items);
        }
        if (address == kStreamClose && args.size() == 1) {
            return stream(args[0]) == nullptr ? 0xffffffffu :
                static_cast<std::uint32_t>(std::fclose(streams_[args[0] - 1].release()));
        }
        if ((address == seam::crt::CRT_operator_new || address == seam::crt::CRT_malloc) && args.size() == 1) {
            const auto block = arena_.heap().allocate(args[0]);
            if (block == 0) {
                throw runtime::ServiceFault("guest heap allocation failed");
            }
            return block;
        }
        if ((address == seam::crt::FUN_005002f3 || address == seam::crt::CRT_free) && args.size() == 1) {
            arena_.heap().release(args[0]);
            return 0;
        }
        if (address == seam::hal::WINMAIN_LoadSetting && args.size() == 4) {
            const auto value = settings_.get_string(arena::text(args[0]), arena::text(args[1]), arena::text(args[2])).substr(0, 255);
            memory_.write(args[3], {reinterpret_cast<const std::uint8_t*>(value.c_str()), value.size() + 1});
            return static_cast<std::uint32_t>(value.size());
        }
        if (address == seam::hal::WINMAIN_SaveSetting && args.size() == 3) {
            settings_.set(arena::text(args[0]), arena::text(args[1]), arena::text(args[2]));
            return 0;
        }
        if (address == seam::hal::WINMAIN_EnumDisplayModes) {
            constexpr std::uint32_t kModeList = 0x029d8478;
            constexpr std::uint32_t kModeRecord = kModeList + 4;
            memory_.set32(kModeList, 1);
            memory_.fill(kModeRecord, 0, 0x7c);
            memory_.set32(kModeRecord, 0x7c);
            memory_.set32(kModeRecord + 0x08, 480);
            memory_.set32(kModeRecord + 0x0c, 640);
            memory_.set32(kModeRecord + 0x48, 0x20);
            memory_.set32(kModeRecord + 0x4c, 0x40);
            memory_.set32(kModeRecord + 0x54, 16);
            return 0;
        }
        if (address == seam::hal::WINMAIN_SwitchResolution) {
            memory_.set32(0x029da394, 640);
            memory_.set32(0x029da398, 480);
            return 0;
        }
        if (address == seam::hal::WINMAIN_SwitchRenderer) {
            return 0;  // The standalone exposes one renderer through the HAL.
        }
        if (address == seam::hal::printf_log && !args.empty()) {
            std::fputs(arena::text(args[0]), stderr);
            return 0;
        }
        if (address == seam::crt::CRT_sprintf && args.size() >= 2) {
            std::size_t next = 2;
            const auto formatted = runtime::guest::format(arena::text(args[1]), {
                [&] {
                    if (next == args.size()) {
                        throw runtime::ServiceFault("sprintf: too few guest arguments");
                    }
                    return args[next++];
                },
                [this](std::uint32_t address) { return readable_text(address); }
            });
            memory_.write(args[0], {reinterpret_cast<const std::uint8_t*>(formatted.c_str()), formatted.size() + 1});
            return static_cast<std::uint32_t>(formatted.size());
        }
        if (address == seam::crt::CRT_vsnprintf && args.size() >= 4) {
            std::uint32_t list = args[3];
            const auto formatted = runtime::guest::format(arena::text(args[2]), {
                [&] {
                    const auto word = memory_.u32(list);
                    list += 4;
                    return word;
                },
                [this](std::uint32_t address) { return readable_text(address); }
            });
            const std::size_t capacity = args[1];
            const auto length = std::min(capacity, formatted.size() + 1);
            if (length != 0) {
                memory_.write(args[0], {reinterpret_cast<const std::uint8_t*>(formatted.c_str()), length});
            }
            return formatted.size() > capacity ? kFailed : static_cast<std::uint32_t>(formatted.size());
        }
        if (address == seam::hal::Game_FrameTick || address == seam::hal::WINMAIN_Sleep) {
#ifdef OPENTONY_LINUX_MUSIC
            if (music_active_ && (memory_.u8(runtime::pcplatform::audio::kMovieThreadFlag) != 0 || !music_.pump())) {
                stop_music();
            }
#endif
#ifdef OPENTONY_LINUX_SDL
            OtHalWindowEvent events[16];
            const auto count = ot_hal_sdl_window.pump(events, std::size(events));
            for (std::uint32_t index = 0; index < count; ++index) {
                if (events[index].type == OT_HAL_WINDOW_EVENT_CLOSE_REQUESTED || events[index].type == OT_HAL_WINDOW_EVENT_QUIT) {
                    std::exit(EXIT_SUCCESS);
                }
            }
#endif
            if (memory_.u32(kTimerState) != 0) {
                deliver_tick();
            }
            return memory_.u32(kSimTick);
        }
        if (address == seam::hal::PCInput_Poll) {
#ifdef OPENTONY_LINUX_SDL
            OtHalInputState state{};
            ot_hal_sdl_input.poll(&state);
            memory_.write(0x006a43e4, state.keys);
            gamepad::update_joystick(state.pads[0]);
            memory_.set8(0x006a44f8, (state.mouse.buttons & OT_MOUSE_LEFT) != 0);
            if (state.mouse.dx || state.mouse.dy || state.mouse.buttons) {
                std::int32_t width = 0;
                std::int32_t height = 0;
                ot_hal_sdl_window.get_size(&width, &height);
                if (width > 0 && height > 0) {
                    const char* aspect = std::getenv("OT_ASPECT");
                    const bool stretch = aspect != nullptr && std::string_view(aspect) == "stretch";
                    const double scale = std::min(width / 640.0, height / 480.0);
                    const double shown_width = stretch ? width : 640 * scale;
                    const double shown_height = stretch ? height : 480 * scale;
                    mouse_x_ = std::clamp(static_cast<int>((state.mouse.x - (width - shown_width) / 2) * 640 / shown_width), 0, 639);
                    mouse_y_ = std::clamp(static_cast<int>((state.mouse.y - (height - shown_height) / 2) * 480 / shown_height), 0, 479);
                }
            }
#endif
            return resolve(0x004e42c0, {});  // PCInput_BuildActionMask
        }
        if (address == seam::hal::SDXKeyboard_KeyIsDown && args.size() == 2) {
            for (std::uint32_t index = 0; index < 256; ++index) {
                const auto code = memory_.u8(args[1] + index);
                if (code == 0) {
                    break;
                }
                if ((memory_.u8(args[0] + 4 + code) & 0x80) != 0) {
                    return 1;
                }
            }
            return 0;
        }
        if (address == seam::crt::FUN_00502ccb) {
            return static_cast<std::uint32_t>(standalone_clock_micros() / 1000);
        }
        if (address == 0x005015a6 && args.size() >= 2) {  // _controlfp
            control_word = (control_word & ~args[1]) | (args[0] & args[1]);
            return control_word;
        }
        switch (address) {
        case seam::hal::PCInput_ActuatorOn:
        case seam::hal::PCInput_ActuatorOff:
            return 0;  // Startup runs with the DirectInput force-feedback devices absent.
        case seam::net::FUN_004db1a0:
        case seam::net::NetGame_PollMessages:
        case seam::net::NetGame_NextMessage:
        case seam::net::NetGame_Latency:
        case seam::net::NetGame_SendPacket:
        case seam::net::FUN_004dba20:
        case seam::net::FUN_004dbcc0:
        case seam::net::NetGame_SessionStart:
        case seam::net::FUN_004ddbf0:
        case seam::net::FUN_004de790:  // No remote player to receive a skater-hit message.
            return 0;  // The standalone boots in single-player mode.
        case seam::net::FUN_004dbc50:
            return kFailed;
        case 0x0050246f:  // _findfirst: the boot probe has an empty save profile.
            return kFailed;
        default:
            break;
        }
        std::uint32_t audio_result = 0;
        if (file_service(address, args, audio_result)) {
            return audio_result;
        }
        if (call_audio_seam(address, args, {}, audio_result)) {
            return audio_result;
        }
        const auto* thunk = std::lower_bound(std::begin(kThunks), std::end(kThunks), address,
            [](const Thunk& entry, std::uint32_t value) { return entry.address < value; });
        if (thunk != std::end(kThunks) && thunk->address == address) {
            return resolve(thunk->target, args, virtual_call);
        }
        char hex[9];
        std::snprintf(hex, sizeof hex, "%08x", address);
        throw runtime::ServiceFault(std::string("Linux host has no service for 0x") + hex);
    }

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string& text) override {
#ifdef OPENTONY_LINUX_MUSIC
        if (name == "win32_BinkSetVolume" && args.size() >= 2) {
            music_.volume(static_cast<std::int32_t>(args[1]));
            return 0;
        }
        if (name == "win32_BinkPause" && args.size() >= 2) {
            music_.pause((args[1] & 0xff) != 0);
            return 0;
        }
        if (name == "win32_BinkSetPan") {
            return 0;  // The music stream is stereo and centered.
        }
        if (name == "win32_Sleep" && music_active_ && memory_.u8(runtime::pcplatform::audio::kMovieThreadFlag) != 0) {
            stop_music();
            return 0;
        }
#endif
        if (name == "win32_GetCursorPos" && args.size() == 1 && args[0] != 0) {
            memory_.set32(args[0], mouse_x_);
            memory_.set32(args[0] + 4, mouse_y_);
            return 1;
        }
        std::uint32_t surface_result = 0;
        if (call_surface_service(memory_, name, args, surface_result)) {
            return surface_result;
        }
        constexpr std::pair<std::string_view, std::uint32_t> virtual_services[] = {
            {"font_draw", 1}, {"sprite_draw", 3}, {"blood_draw", 3},
            {"font_sprite_draw", 3}, {"font_sprite_delete", 0}, {"virtual_delete", 0},
            {"element_update", 1}, {"element_draw", 2}, {"element_snap_x", 5},
            {"element_snap_y", 6}, {"element_hook", 7},
            {"effect_bind", 1}, {"effect_unbind", 2}, {"effect_init", 3},
            {"effect_update", 4}, {"effect_finished", 7},
            {"font_effect_position", 5}, {"font_effect_colour", 6}, {"font_effect_glyph", 8},
            {"dialog_v1", 1}, {"dialog_v2", 2},
        };
        auto words = args;
        if ((name == "font_draw" && words.size() == 3) ||
            ((name == "font_text_extent" || name == "font_text_width") && words.size() == 1)) {
            constexpr std::uint32_t kTextScratch = 0x7ff0f800;
            const auto line = text.substr(0, 255);
            memory_.write(kTextScratch, {reinterpret_cast<const std::uint8_t*>(line.c_str()), line.size() + 1});
            words.push_back(kTextScratch);
            if (name == "font_draw") {
                words.push_back(0);
            }
        }
        for (const auto& [service, slot] : virtual_services) {
            if (name == service && !words.empty()) {
                return resolve(memory_.u32(memory_.u32(words.front()) + slot * 4), words, true);
            }
        }
        const std::string_view service(name);
        const bool virtual_call = service.starts_with("vcall_");
        if (virtual_call || service.starts_with("engine_")) {
            const auto hex = service.substr(virtual_call ? 6 : 7);
            std::uint32_t address = 0;
            const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), address, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == hex.data() + hex.size()) {
                return resolve(address, words, virtual_call, text);
            }
        }
        for (const auto& entry : std::span(kLiveCalls, kLiveCallCount)) {
            if (name == entry.service) {
                std::vector<std::uint32_t> full(entry.lead, entry.lead_value);
                full.insert(full.end(), words.begin(), words.end());
                return resolve(entry.address, full, false, text);
            }
        }
        for (const auto& entry : std::span(kServices, kServiceCount)) {
            if (name == entry.name) {
                return resolve(entry.address, words, false, text);
            }
        }
        throw runtime::ServiceFault("Linux host has no service named " + name);
    }

    bool file_service(std::uint32_t address, const std::vector<std::uint32_t>& args, std::uint32_t& result) {
        if (address == kBackendOpen) {
            result = kFailed;
            if (!args.empty() && (args.size() < 2 || args[1] == 0)) {
                result = open_resource(arena::text(args[0]));
            }
            return true;
        }
        if (address != kBackendSeek && address != kBackendRead && address != kBackendClose) {
            return false;
        }
        result = kFailed;
        if (args.empty() || args[0] >= resources_.size() || !resources_[args[0]]) {
            return true;
        }
        auto& slot = resources_[args[0]];
        if (address == kBackendClose) {
            slot.reset();
            result = 0;
        } else if (address == kBackendSeek && args.size() == 3 && args[2] <= 2) {
            const std::int64_t origin = args[2] == 0 ? 0 :
                args[2] == 1 ? slot->position : static_cast<std::uint32_t>(slot->bytes.size());
            const auto next = origin + static_cast<std::int32_t>(args[1]);
            if (next >= 0 && next < kFailed) {
                slot->position = result = static_cast<std::uint32_t>(next);
            }
        } else if (address == kBackendRead && args.size() == 3 && args[2] < kFailed - slot->position) {
            const auto available = slot->position < slot->bytes.size() ? slot->bytes.size() - slot->position : 0;
            const auto count = std::min<std::size_t>(available, args[2]);
            if (count != 0) {
                std::memcpy(arena::host<void>(args[1]), slot->bytes.data() + slot->position, count);
            }
            slot->position += args[2];
            result = args[2];
        }
        return true;
    }

#ifdef OPENTONY_LINUX_SDL
    void presented() {
        if (!menu_active_ || ++menu_frames_ != 60) {
            return;
        }
        std::puts("Linux host: main menu rendered 60 frames");
        if (const char* path = std::getenv("OT_LINUX_MENU_SCREENSHOT")) {
            OtHalImage image{};
            if (!ot_hal_sdl_video_gl.read_back(&image)) {
                throw runtime::ServiceFault("could not read back the main menu frame");
            }
            SDL_Surface* pixels = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888, image.pixels, image.pitch);
            const bool saved = pixels != nullptr && SDL_SaveBMP(pixels, path);
            SDL_DestroySurface(pixels);
            if (!saved) {
                throw runtime::ServiceFault(std::string("could not save menu frame: ") + SDL_GetError());
            }
            std::printf("Linux host: saved menu frame to %s\n", path);
            std::exit(EXIT_SUCCESS);
        }
        std::fflush(stdout);
    }
#endif

private:
    void apply_audio_level(const char* key, std::uint32_t slider, std::uint32_t mixer, std::uint32_t save, std::uint32_t multiplier) {
        if (const char* value = std::getenv(key); value != nullptr && *value != '\0') {
            const auto level = static_cast<std::uint32_t>(std::clamp(std::atoi(value), 0, 10)) * 6 / 5;
            memory_.set32(slider, level);
            memory_.set32(mixer, level * multiplier);
            memory_.set32(save, level);
        }
    }

#ifdef OPENTONY_LINUX_MUSIC
    void stop_music() {
        music_.close();
        music_active_ = false;
        memory_.set32(runtime::pcplatform::audio::kMovieHandle, 0);
        memory_.set8(runtime::pcplatform::audio::kMoviePlaying, 0);
        memory_.set8(runtime::pcplatform::audio::kMovieThreadFlag, 0);
    }
#endif
    void advance_ticks(double step, double frame) {
        arena::at<double>(kFrameAccumulator) = frame;
        memory_.set32(kSimTick, static_cast<std::uint32_t>(frame));
        if (memory_.u32(kPauseFlag) == 0 && memory_.u32(kPauseFlag2) == 0) {
            arena::at<double>(kGameAccumulator) = arena::at<double>(kGameAccumulator) + step;
        }
        memory_.set32(kGameTick, static_cast<std::uint32_t>(arena::at<double>(kGameAccumulator)));
    }

    void deliver_tick() {
        namespace pacing = runtime::framepacing;
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        if (!pacing::pacing().exact_clock) {
            const auto interval = std::chrono::milliseconds(std::max(memory_.u32(kTimerState + 4), 1u));
            next_tick_ = std::max(next_tick_ + interval, now);
            std::this_thread::sleep_until(next_tick_);
            resolve(kTimerCallback, {0, 0, kTimerState, 0, 0});
            return;
        }

        const auto before = memory_.u32(kSimTick);
        const double step = last_delivery_ == Clock::time_point{} ? 0.0 :
            std::clamp(std::chrono::duration<double>(now - last_delivery_).count() * 60.0, 0.0, 4.0);
        const double frame = arena::at<double>(kFrameAccumulator) + step;
        advance_ticks(step, frame);
        last_delivery_ = now;
        const auto last_scene = memory_.u32(kLastSceneTick);
        const bool sub_tick = pacing::pacing().sub_tick;
        const auto since_scene = pacing::count_delivery(sub_tick ? pacing::sub_tick_state().last_scene : last_scene);
        double target = sub_tick ? pacing::scene_due(frame) : pacing::whole_tick_due(frame, last_scene, pacing::limiter_ticks());
        if (target <= frame && since_scene >= pacing::kIdleDelivery && memory_.u32(kSimTick) == before) {
            target = std::floor(frame) + 1.0;
        }
        if (target > frame) {
            const double left = target - frame;
            last_delivery_ = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(left / 60.0));
            std::this_thread::sleep_until(last_delivery_);
            advance_ticks(left, target);
        }
    }

    const char* readable_text(std::uint32_t address) const {
        const auto heap_end = arena_.heap().stats().committed_end;
        const std::pair<std::uint32_t, std::uint32_t> ranges[] = {
            {arena::kImageBase, arena::kZonesEnd},
            {arena::kHeapBase, heap_end},
            {arena::kScratchWindow, arena::kScratchWindow + arena::kScratchWindowSize},
        };
        for (const auto& [first, end] : ranges) {
            if (address >= first && address < end) {
                const char* text = arena::text(address);
                return std::memchr(text, 0, std::min(end - address, 0x10000u)) ? text : nullptr;
            }
        }
        return nullptr;
    }

    struct Resource {
        std::span<const std::byte> bytes;
        std::uint32_t position = 0;
    };

    std::uint32_t open_resource(std::string path) {
        const auto slot = std::find_if(resources_.begin(), resources_.end(), [](const auto& item) { return !item; });
        if (slot == resources_.end()) {
            return kFailed;
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        const std::size_t start = path.size() > 1 && path[1] == ':' ? 2 : 0;
        const auto first = path.find_first_not_of('/', start);
        if (first == std::string::npos) {
            return kFailed;
        }
        const std::string_view relative(path.data() + first, path.size() - first);
        const auto slash = relative.rfind('/');
        const auto directory = slash == std::string_view::npos ? std::string_view{} : relative.substr(0, slash);
        const auto name = slash == std::string_view::npos ? relative : relative.substr(slash + 1);
        if (!archive_) {
            archive_.emplace(assets::PkrArchive::load(package_.string()));
        }
        const auto* entry = archive_->find_resource(directory, name);
        if (entry == nullptr) {
            return kFailed;
        }
        const auto index = static_cast<std::size_t>(entry - archive_->entries().data());
        slot->emplace(Resource{archive_->stored_payload(index)});
        return static_cast<std::uint32_t>(slot - resources_.begin());
    }

    struct CloseFile {
        void operator()(std::FILE* file) const { std::fclose(file); }
    };

    std::FILE* stream(std::uint32_t handle) const {
        return handle > 0 && handle <= streams_.size() ? streams_[handle - 1].get() : nullptr;
    }

    std::uint32_t pc_init() {
        const auto working = std::filesystem::current_path().string();
        const auto root = package_.parent_path().string();
        const auto package = package_.string();
        if (working.size() > 240 || package.size() > 255) {
            throw runtime::ServiceFault("game or working directory exceeds the guest path limit");
        }
        for (const std::uint32_t table : {0x006a6d44u, 0x006a716cu, 0x006a7194u, 0x006a6d1cu}) {
            memory_.fill(table, 0, 40);
        }
        memory_.fill(0x006a6d10, 0, 10);
        const auto write_path = [this](std::uint32_t address, const std::string& path) {
            memory_.write(address, {reinterpret_cast<const std::uint8_t*>(path.c_str()), path.size() + 1});
        };
        write_path(kWorkingDirectory, working);
        write_path(kGameDirectory, root);
        write_path(kPackageName, package);
        if (resolve(kPackagePreload, {kPackageName}) == 0) {
            throw runtime::ServiceFault("cannot preload " + package);
        }
        std::printf("Linux host: preloaded %s\n", package.c_str());
        return 0;
    }

    LinuxGuestMemory& arena_;
    std::filesystem::path package_;
    thpsource2::IniFile settings_;
#ifdef OPENTONY_LINUX_MUSIC
    LinuxMusic music_;
    bool music_active_ = false;
#endif
    std::array<std::unique_ptr<std::FILE, CloseFile>, 16> streams_;
    std::optional<assets::PkrArchive> archive_;
    std::array<std::optional<Resource>, 10> resources_;
    std::int32_t mouse_x_ = 320;
    std::int32_t mouse_y_ = 240;
    std::chrono::steady_clock::time_point next_tick_{};
    std::chrono::steady_clock::time_point last_delivery_{};
    bool menu_active_ = false;
#ifdef OPENTONY_LINUX_SDL
    std::uint32_t menu_frames_ = 0;
#endif
    RetailMemory memory_;
    RetailServices services_;
    std::vector<std::uint32_t> exit_callbacks_;
};

std::uintptr_t guest_base = 0;
BootHost* boot_host = nullptr;
LinuxGuestMemory* guest_memory = nullptr;

#ifdef OPENTONY_LINUX_SDL
std::string_view env_setting(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

void run_with_sdl_window(bool try_boot) {
    if (!SDL_Init(ot_hal_sdl_window_subsystems() | ot_hal_sdl_input_subsystems())) {
        throw runtime::ServiceFault(SDL_GetError());
    }
    ot_hal_sdl_video_gl_hints();
    ot_hal_sdl_window_request_opengl(1);
    OtHalWindowDesc window{"THPSource2", 640, 480, try_boot ? OT_HAL_WINDOW_RESIZABLE : OT_HAL_WINDOW_HIDDEN};
    int width = 0;
    int height = 0;
    const auto resolution = env_setting("OT_WINDOW_SIZE");
    const bool custom_size = std::sscanf(resolution.data(), "%dx%d", &width, &height) == 2 &&
        width >= 320 && width <= 16384 && height >= 240 && height <= 16384;
    if (try_boot && custom_size) {
        window.width = width;
        window.height = height;
    }
    if (try_boot && env_setting("OT_WINDOW_MODE") == "fullscreen") {
        window.flags = OT_HAL_WINDOW_FULLSCREEN | (custom_size ? static_cast<unsigned>(OT_HAL_WINDOW_EXCLUSIVE) : 0u);
    } else if (try_boot && env_setting("OT_WINDOW_MODE") == "borderless") {
        window.flags = OT_HAL_WINDOW_BORDERLESS;
        if (!custom_size) {
            SDL_Rect bounds{};
            if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds)) {
                window.width = bounds.w;
                window.height = bounds.h;
            }
        }
    }
    const OtHalVideoMode mode{640, 480, 32, (window.flags & OT_HAL_WINDOW_FULLSCREEN) != 0, env_setting("OT_VSYNC") == "1"};
    OtHalSdlVideoOptions options{};
    const auto internal = env_setting("OT_INTERNAL_RESOLUTION");
    options.follow_window = internal == "window";
    if (internal == "psx") {
        options.target_width = 512;
        options.target_height = 240;
    }
    options.stretch = env_setting("OT_ASPECT") == "stretch";
    options.samples = std::clamp(std::atoi(env_setting("OT_MSAA").data()), 0, 8);
    const double supersample = std::strtod(env_setting("OT_SUPERSAMPLE").data(), nullptr);
    options.supersample = options.follow_window && supersample >= 1.0 ? static_cast<int>(std::min(supersample, 4.0) * 100 + 0.5) : 0;
    const auto anisotropy = env_setting("OT_ANISOTROPY");
    options.anisotropy = anisotropy == "trilinear" ? 1 : std::clamp(std::atoi(anisotropy.data()), 0, 16);
    const auto scaling = env_setting("OT_SCALING");
    options.present_filter = scaling == "crt" ? OT_HAL_SDL_PRESENT_CRT : scaling == "sharp" ? OT_HAL_SDL_PRESENT_SHARP :
        scaling == "nearest" ? OT_HAL_SDL_PRESENT_NEAREST : OT_HAL_SDL_PRESENT_LINEAR;
    const auto filter = env_setting("OT_TEXTURE_FILTER");
    options.texture_filter = filter == "sharp" ? OT_HAL_SDL_FILTER_NEAREST : filter == "smooth" ? OT_HAL_SDL_FILTER_LINEAR : OT_HAL_SDL_FILTER_AS_DRAWN;
    const auto occlusion = env_setting("OT_AMBIENT_OCCLUSION");
    options.ambient_occlusion = occlusion == "high" ? 2 : occlusion == "low" ? 1 : 0;
    options.alpha_to_coverage = env_setting("OT_ALPHA_TO_COVERAGE") == "1";
    options.sharp_2d = env_setting("OT_HUD_FILTER") == "sharp";
    options.psx_dither = env_setting("OT_PSX_DITHER") == "1";
    options.crt_curve = env_setting("OT_CRT_CURVE") == "1";
    options.texture_pack = std::getenv("OT_TEXTURE_PACK");
    ot_hal_sdl_video_gl_options(&options);
    const auto& video = ot_hal_sdl_video_gl;
    const bool created = ot_hal_sdl_window.create(&window) != 0;
    const bool initialized = created && video.init(&mode) != 0;
    if (!initialized) {
        const std::string error = created ? ot_hal_sdl_video_error() : SDL_GetError();
        ot_hal_sdl_window.destroy();
        SDL_Quit();
        throw runtime::ServiceFault("SDL window startup: " + error);
    }
    struct Shutdown {
        ~Shutdown() {
            opentony_standalone_video_hal(nullptr);
            ot_hal_sdl_input.shutdown();
            ot_hal_sdl_audio.shutdown();
            set_standalone_audio(nullptr);
            ot_hal_sdl_video_gl.shutdown();
            ot_hal_sdl_window.destroy();
            SDL_Quit();
        }
    } shutdown;
    video.begin_frame();
    video.clear(OT_HAL_CLEAR_COLOR | OT_HAL_CLEAR_DEPTH, 0xff000000u, 1.0f, nullptr);
    video.present();
    std::puts("Linux host: presented a frame through SDL3/OpenGL");
    std::fflush(stdout);
    if (try_boot) {
        const auto pacing = configured_pacing();
        runtime::framepacing::set_pacing(pacing);
        std::printf("Linux host: target %.0f FPS\n", 60.0 / pacing.period_ticks);
        if (!ot_hal_sdl_input.init()) {
            throw runtime::ServiceFault("SDL input startup failed");
        }
        opentony_standalone_video_hal(&video);
        runtime::drawkit::Output output{};
        output.video = &video;
        output.on_present = [](std::uint32_t, void*) { boot_host->presented(); };
        runtime::drawkit::set_output(output);
        set_standalone_audio(&ot_hal_sdl_audio);
        standalone_start_game();
    }
}
#endif

}  // namespace

std::uintptr_t arena::base() noexcept { return guest_base; }
std::uint32_t arena::heap_allocate(std::uint32_t size) { return guest_memory->heap().allocate(size); }
void arena::heap_release(std::uint32_t address) { guest_memory->heap().release(address); }
arena::HeapBlock::HeapBlock(std::uint32_t size) : address_(heap_allocate(size)) {
    if (address_ == 0) {
        throw runtime::MemoryFault("guest heap allocation failed");
    }
}
arena::HeapBlock::~HeapBlock() { guest_memory->heap().release(address_); }

std::uint64_t standalone_clock_micros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - clock_origin).count();
}

unsigned standalone_precision() {
    const auto precision = control_word & 0x30000u;
    return precision == 0x20000u ? 0u : precision == 0x10000u ? 2u : 3u;
}

std::uint32_t call_native_or_seam(std::uint32_t address, const std::vector<std::uint32_t>& args) {
    return boot_host->resolve(address, args);
}

bool call_file_seam(std::uint32_t address, const std::vector<std::uint32_t>& args, const std::string&, std::uint32_t& result) {
    return boot_host->file_service(address, args, result);
}

[[noreturn]] void fatal(const char* hook, const char* what) noexcept {
    std::fprintf(stderr, "Linux host: %s: %s\n", hook, what);
    std::exit(EXIT_FAILURE);
}

void run_linux_initializers(LinuxGuestMemory& arena, const std::filesystem::path& game_dir, bool try_boot) {
    guest_base = arena.base();
    guest_memory = &arena;
    BootHost host(arena, game_dir);
    boot_host = &host;
    standalone_initialize();
    if (!game_dir.empty()) {
        standalone_prepare_platform();
        standalone_read_startup_options();
        std::puts("Linux host: loaded startup options");
#ifdef OPENTONY_LINUX_SDL
        run_with_sdl_window(try_boot);
#else
        if (try_boot) {
            throw runtime::ServiceFault("the boot probe requires OPENTONY_BUILD_SDL=ON");
        }
#endif
    }
    boot_host = nullptr;
    guest_memory = nullptr;
    guest_base = 0;
}

}  // namespace opentony::kitdll
