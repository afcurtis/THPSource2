#include "hal_runtime.h"

#include "audio_hal.h"
#include "hal_adapters.h"
#include "movie_hal.h"
#include "video_hal.h"
#include "video_mirror.h"
#include "video_renderer.h"
#include "video_trace.h"
#include "../clock.h"
#include "../hook_runtime.h"
#include "../../hal/sdl/sdl_api.h"
#include "../../hal/sdl/sdl_backends.h"

#include <string.h>

// Platform HAL bootstrap: configuration, the startup/shutdown entry sites and
// the dynamically loaded SDL3.dll. The services themselves live in
// src/hal/sdl (backends) and src/hooks/hal (adapters from retail hook points).

namespace {

const unsigned kStubOffset = 32u;

OtHalConfig g_config;
char g_dll_dir[MAX_PATH];
int g_sdl_started = 0;
int g_started = 0;
int g_version = 0;

const OtHalSpec *g_spec = 0;
int g_build = -1;
unsigned char *g_slots = 0;
// Entry sites left unpatched because a service configured for SDL replaces
// their function; patched after all if that service stays retail.
uint8_t g_site_deferred[OT_HAL_MAX_SITES];
// Per service: its hook points are patched.
uint8_t g_installed[OT_HAL_SERVICE_COUNT];
const OtHalClockBackend *g_clock = 0;
const OtHalWindowBackend *g_window = 0;
const OtHalInputBackend *g_input = 0;
int g_window_adopted = 0;
// What ran at some point (the stats are written again after shutdown).
int g_clock_ran = 0;
int g_window_was_adopted = 0;
int g_input_ran = 0;
int g_video_ran = 0;
int g_movie_ran = 0;
int g_audio_ran = 0;

bool install_service(int service);
bool install_site(const OtHalSite &site, int build, unsigned char *slot);
void install_deferred_sites();

typedef int(__cdecl *OnexitFunction)(void);
typedef OnexitFunction(__cdecl *OnexitRegister)(OnexitFunction);
OnexitRegister g_onexit = 0; // the game's CRT _onexit (services.hal.onexit)
int __cdecl hal_onexit(void);

bool text_is(const char *text, unsigned length, const char *word) {
    unsigned index = 0;
    for (; index < length && word[index] != 0; ++index) {
        char left = text[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (left != word[index]) {
            return false;
        }
    }
    return index == length && word[index] == 0;
}

int parse_backend(const char *text, unsigned length, int fallback) {
    if (text_is(text, length, "retail")) return OT_HAL_RETAIL;
    if (text_is(text, length, "sdl")) return OT_HAL_SDL;
    return fallback;
}

uint8_t parse_video_backend(const char *text, unsigned length) {
    if (text_is(text, length, "gpu")) return OT_HAL_VIDEO_BACKEND_GPU;
    if (text_is(text, length, "gl")) return OT_HAL_VIDEO_BACKEND_GL;
    if (!text_is(text, length, "auto")) {
        ot_log("hal: unknown video_backend '%s' (auto|gpu|gl); using auto", text);
    }
    return OT_HAL_VIDEO_BACKEND_AUTO;
}

// One OPENTONY_HAL token: service=backend | all=backend | none | probe.
void apply_token(const char *token, unsigned length) {
    const char *equals = static_cast<const char *>(memchr(token, '=', length));
    if (length == 0) {
        return;
    }
    if (equals == 0) {
        if (text_is(token, length, "none")) {
            memset(g_config.backend, OT_HAL_RETAIL, sizeof(g_config.backend));
        } else if (text_is(token, length, "probe")) {
            g_config.sdl_probe = 1;
        } else {
            ot_log("hal: ignoring '%s'", token);
        }
        return;
    }
    const unsigned key_length = static_cast<unsigned>(equals - token);
    const int backend = parse_backend(equals + 1, length - key_length - 1, -1);
    if (backend < 0) {
        ot_log("hal: unknown backend in '%s' (retail|sdl)", token);
        return;
    }
    if (text_is(token, key_length, "all")) {
        memset(g_config.backend, backend, sizeof(g_config.backend));
        return;
    }
    const int service = ot_hal_service_parse(token, key_length);
    if (service < 0) {
        ot_log("hal: unknown service in '%s'", token);
        return;
    }
    g_config.backend[service] = static_cast<uint8_t>(backend);
}

bool needs_sdl() {
    if (g_config.sdl_probe || ot_video_mirror_enabled()) {
        return true;
    }
    for (int service = 0; service < OT_HAL_SERVICE_COUNT; ++service) {
        if (g_config.backend[service] == OT_HAL_SDL) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ SDL3.dll

bool load_sdl() {
    const char *missing = 0;
    if (!ot_sdl_load(g_dll_dir, &missing)) {
        ot_log("hal: SDL3.dll unavailable: %s not found (next to opentony.dll or on the search path)",
            missing != 0 ? missing : "?");
        return false;
    }
    return true;
}

// Test switch OPENTONY_HAL_TEST_GAMEPAD=1: attach an SDL virtual gamepad
// before the input service starts and press it on a fixed schedule of input
// polls (south button, then the left stick right), so the gamepad path can be
// smoke-tested without a device.
int g_test_gamepad = 0;
SDL_JoystickID g_test_pad_id = 0;
SDL_Joystick *g_test_pad = 0;

void attach_test_gamepad() {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "OpenTony test gamepad";
    g_test_pad_id = OT_SDL(SDL_AttachVirtualJoystick)(&desc);
    g_test_pad = g_test_pad_id != 0 ? OT_SDL(SDL_OpenJoystick)(g_test_pad_id) : 0;
    ot_log("hal: test gamepad %s", g_test_pad != 0 ? "attached" : OT_SDL(SDL_GetError)());
}

// Test switch OPENTONY_HAL_QUIT_AFTER_MS: close the game window this long after
// startup, exercising the orderly exit (WINMAIN_WndProc -> WINMAIN_ShutDown).
DWORD g_quit_after_ms = 0;
DWORD g_started_tick = 0;
volatile LONG g_quit_posted = 0;
DWORD g_quit_tick = 0;
int g_watchdog_done = 0;
HANDLE g_game_thread = 0;

// Still alive 10 s after the test WM_CLOSE: log where the game thread is (its
// EIP and the return addresses into the game image found on its stack).
void log_game_thread() {
    CONTEXT context;
    DWORD stack_copy[4096];
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    if (g_game_thread == 0 || SuspendThread(g_game_thread) == static_cast<DWORD>(-1)) {
        ot_log("hal: watchdog: game thread gone");
        return;
    }
    // Copy only while suspended; log (which takes locks) after resuming.
    const BOOL have_context = GetThreadContext(g_game_thread, &context);
    int words = 0;
    if (have_context) {
        const DWORD *stack = reinterpret_cast<const DWORD *>(static_cast<UINT_PTR>(context.Esp));
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(stack, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT) {
            const UINT_PTR end = reinterpret_cast<UINT_PTR>(info.BaseAddress) + info.RegionSize;
            words = static_cast<int>((end - reinterpret_cast<UINT_PTR>(stack)) / sizeof(DWORD));
            if (words > 4096) {
                words = 4096;
            }
            memcpy(stack_copy, stack, static_cast<size_t>(words) * sizeof(DWORD));
        }
    }
    ResumeThread(g_game_thread);
    char line[512];
    int length = wsprintfA(line, "hal: watchdog: game thread alive %u ms after WM_CLOSE; eip %08X; game frames",
        static_cast<unsigned>(GetTickCount() - g_quit_tick), static_cast<unsigned>(context.Eip));
    int found = 0;
    for (int index = 0; index < words && found < 20; ++index) {
        if (stack_copy[index] >= 0x00401000u && stack_copy[index] < 0x00520000u) {
            length += wsprintfA(line + length, " %08X", static_cast<unsigned>(stack_copy[index]));
            ++found;
        }
    }
    ot_log("%s", line);
}

BOOL CALLBACK close_own_windows(HWND window, LPARAM) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && IsWindowVisible(window)) {
        PostMessageA(window, WM_CLOSE, 0, 0);
        ot_log("hal: posted WM_CLOSE to window %08X (OPENTONY_HAL_QUIT_AFTER_MS)",
            static_cast<unsigned>(reinterpret_cast<UINT_PTR>(window)));
    }
    return TRUE;
}

void startup() {
    if (g_started) {
        return;
    }
    g_started = 1;
    g_started_tick = GetTickCount();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread,
        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    if (!needs_sdl()) {
        ot_log("hal: every service retail; SDL not loaded");
        return;
    }
    if (!load_sdl()) {
        ot_log("hal: SDL unavailable; every service stays retail");
        memset(g_config.backend, OT_HAL_RETAIL, sizeof(g_config.backend));
        return;
    }
    g_version = OT_SDL(SDL_GetVersion)();
    const bool window = g_config.backend[OT_HAL_WINDOW] == OT_HAL_SDL;
    const bool input = g_config.backend[OT_HAL_INPUT] == OT_HAL_SDL;
    if (window) {
        ot_hal_sdl_window_hints();
    }
    if (!OT_SDL(SDL_Init)(SDL_INIT_EVENTS)) {
        ot_log("hal: SDL_Init failed: %s", OT_SDL(SDL_GetError)());
        memset(g_config.backend, OT_HAL_RETAIL, sizeof(g_config.backend));
        return;
    }
    g_sdl_started = 1;
    ot_log("hal: SDL %d.%d.%d (%s) initialized on thread %u", SDL_VERSIONNUM_MAJOR(g_version),
        SDL_VERSIONNUM_MINOR(g_version), SDL_VERSIONNUM_MICRO(g_version), OT_SDL(SDL_GetRevision)(),
        static_cast<unsigned>(GetCurrentThreadId()));
    // Stop on the game thread however the game exits: its own exit() runs the
    // CRT exit table before any DLL is detached.
    if (g_onexit != 0) {
        g_onexit(&hal_onexit);
    }
    if (g_installed[OT_HAL_CLOCK]) {
        g_clock = &ot_hal_sdl_clock;
        g_clock_ran = 1;
        ot_clock_set_time(g_clock);
    }
    if (ot_video_mirror_enabled()) {
        if (window) {
            ot_log("video mirror: needs window=retail (its window is the window service's); off");
        } else {
            ot_video_mirror_start();
        }
    }
    if (window) {
        if (OT_SDL(SDL_InitSubSystem)(ot_hal_sdl_window_subsystems())) {
            g_window = &ot_hal_sdl_window;
            ot_log("hal: window service ready (adopts MainWindow before DirectDraw sees it)");
        } else {
            ot_log("hal: SDL video init failed (%s); window stays retail", OT_SDL(SDL_GetError)());
            g_config.backend[OT_HAL_WINDOW] = OT_HAL_RETAIL;
        }
    }
    if (g_config.backend[OT_HAL_VIDEO] == OT_HAL_SDL) {
        if (g_window == 0) {
            ot_log("hal: video needs the SDL window service; video stays retail");
            g_config.backend[OT_HAL_VIDEO] = OT_HAL_RETAIL;
        } else {
            ot_video_hal_prepare(); // before MainWindow is adopted: it needs an OpenGL pixel format
            if (install_service(OT_HAL_VIDEO)) {
                g_video_ran = 1;
            } else {
                g_config.backend[OT_HAL_VIDEO] = OT_HAL_RETAIL;
            }
        }
        if (g_config.backend[OT_HAL_VIDEO] != OT_HAL_SDL) {
            install_deferred_sites();
        }
    }
    if (g_config.backend[OT_HAL_MOVIE] == OT_HAL_SDL) {
        if (g_config.backend[OT_HAL_VIDEO] != OT_HAL_SDL) {
            ot_log("hal: movie needs the SDL video service; movie stays retail");
            g_config.backend[OT_HAL_MOVIE] = OT_HAL_RETAIL;
        } else {
            ot_movie_hal_prepare();
            if (!ot_movie_hal_ready()) {
                ot_log("hal: binkw32.dll exports BinkService/BinkWait not found; movie stays retail");
                g_config.backend[OT_HAL_MOVIE] = OT_HAL_RETAIL;
            } else if (install_service(OT_HAL_MOVIE)) {
                g_movie_ran = 1;
            } else {
                // e.g. the capture recorder's own movie skip is patched in.
                g_config.backend[OT_HAL_MOVIE] = OT_HAL_RETAIL;
            }
        }
    }
    if (g_config.backend[OT_HAL_AUDIO] == OT_HAL_SDL) {
        // Audio_Init runs later in Game_Startup: the stand-in device must be
        // ready by then. OPENTONY_AUDIO_DRIVER picks SDL's driver (e.g.
        // directsound, or disk with OPENTONY_AUDIO_FILE: the mixed output
        // written to a raw float32 stereo file, for tests).
        char setting[MAX_PATH];
        if (GetEnvironmentVariableA("OPENTONY_AUDIO_DRIVER", setting, sizeof(setting)) - 1u < sizeof(setting) - 1u) {
            OT_SDL(SDL_SetHint)(SDL_HINT_AUDIO_DRIVER, setting);
        }
        if (GetEnvironmentVariableA("OPENTONY_AUDIO_FILE", setting, sizeof(setting)) - 1u < sizeof(setting) - 1u) {
            OT_SDL(SDL_SetHint)(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, setting);
        }
        if (!ot_hal_sdl_audio.init()) {
            ot_log("hal: SDL audio init failed (%s); audio stays retail", OT_SDL(SDL_GetError)());
            g_config.backend[OT_HAL_AUDIO] = OT_HAL_RETAIL;
        } else {
            ot_audio_hal_start(&ot_hal_sdl_audio);
            if (install_service(OT_HAL_AUDIO)) {
                g_audio_ran = 1;
                char drivers[160];
                int length = 0;
                drivers[0] = 0;
                for (int index = 0; index < OT_SDL(SDL_GetNumAudioDrivers)() && length < 120; ++index) {
                    length += wsprintfA(drivers + length, " %s", OT_SDL(SDL_GetAudioDriver)(index));
                }
                ot_log("hal: audio service on SDL audio driver '%s' (available:%s)", ot_hal_sdl_audio_driver(), drivers);
            } else {
                ot_audio_hal_stop();
                g_config.backend[OT_HAL_AUDIO] = OT_HAL_RETAIL;
            }
        }
    }
    if (input) {
        if (g_window == 0) {
            ot_log("hal: input needs the SDL window service; input stays retail");
            g_config.backend[OT_HAL_INPUT] = OT_HAL_RETAIL;
        } else if (!OT_SDL(SDL_InitSubSystem)(ot_hal_sdl_input_subsystems())) {
            ot_log("hal: SDL gamepad init failed (%s); input stays retail", OT_SDL(SDL_GetError)());
            g_config.backend[OT_HAL_INPUT] = OT_HAL_RETAIL;
        } else {
            g_input = &ot_hal_sdl_input;
            if (g_test_gamepad) {
                attach_test_gamepad();
            }
            if (!ot_hal_input_adapter_start() || !install_service(OT_HAL_INPUT)) {
                ot_hal_input_adapter_stop();
                g_input = 0;
                g_config.backend[OT_HAL_INPUT] = OT_HAL_RETAIL;
            } else {
                g_input_ran = 1;
            }
        }
    }
}

void shutdown() {
    if (!g_sdl_started) {
        return;
    }
    ot_hal_input_adapter_stop();
    ot_audio_hal_stop();
    ot_video_mirror_stop();
    ot_video_hal_stop();
    if (g_test_pad != 0) {
        OT_SDL(SDL_CloseJoystick)(g_test_pad);
        OT_SDL(SDL_DetachVirtualJoystick)(g_test_pad_id);
        g_test_pad = 0;
    }
    if (g_window != 0) {
        g_window->destroy();
        g_window_adopted = 0;
    }
    if (g_clock != 0) {
        ot_clock_set_time(0); // pace with Win32 time again: SDL is going away
        g_clock = 0;
    }
    g_input = 0;
    g_window = 0;
    OT_SDL(SDL_Quit)();
    g_sdl_started = 0;
    ot_log("hal: SDL shut down on thread %u", static_cast<unsigned>(GetCurrentThreadId()));
}

int __cdecl hal_onexit(void) {
    ot_log("hal: game exit table");
    shutdown();
    return 0;
}

// MainWindow exists and DirectDraw has not seen it yet: adopt it.
void window_ready() {
    if (g_window == 0 || g_window_adopted) {
        return;
    }
    const uint32_t address = ot_hal_symbol(OT_HAL_SYM_MainWindow);
    HWND window = address != 0 ? *reinterpret_cast<HWND *>(static_cast<UINT_PTR>(address)) : 0;
    if (window != 0 && g_window->adopt(window)) {
        g_window_adopted = 1;
        g_window_was_adopted = 1;
        ot_log("hal: window service adopted MainWindow %08X",
            static_cast<unsigned>(reinterpret_cast<UINT_PTR>(window)));
    } else {
        ot_log("hal: could not adopt MainWindow %08X: %s",
            static_cast<unsigned>(reinterpret_cast<UINT_PTR>(window)), OT_SDL(SDL_GetError)());
    }
}

const char *role_name(uint32_t role) {
    return role == OT_HAL_ROLE_STARTUP    ? "startup"
           : role == OT_HAL_ROLE_SHUTDOWN ? "shutdown"
           : role == OT_HAL_ROLE_WINDOW   ? "window"
                                          : role == OT_HAL_ROLE_VIDEO     ? "video"
                                          : role == OT_HAL_ROLE_DRAW_LIST ? "draw_list"
                                                                          : "texture";
}

// The Direct3D device exists (D3D_ClearBuffers; also every renderer restart).
void video_ready() {
    if (g_installed[OT_HAL_VIDEO]) {
        return; // the stand-in device: nothing to trace or report
    }
    static int logged = 0;
    if (!logged) {
        // What retail's device left behind (they differ between Windows
        // drivers and Wine), for comparing retail with video=sdl.
        logged = 1;
        const uint32_t *caps = reinterpret_cast<const uint32_t *>(ot_hal_data(OT_HAL_SYM_D3DTextureCaps));
        unsigned short control = 0;
        __asm fnstcw control;
        ot_log("hal: video device ready: texture caps pow2=%u square=%u max=%ux%u min=%ux%u; x87 control %04X",
            caps[0], caps[1], caps[2], caps[3], caps[4], caps[5], static_cast<unsigned>(control));
    }
    const uint32_t address = ot_hal_symbol(OT_HAL_SYM_D3DDevice);
    if (address != 0 && ot_video_trace_enabled()) {
        ot_video_trace_attach(*reinterpret_cast<void **>(static_cast<UINT_PTR>(address)));
    }
}

extern "C" void __cdecl ot_hal_site_arg(uint32_t role, uint32_t argument) {
    if (role == OT_HAL_ROLE_DRAW_LIST) {
        ot_video_trace_draw_list(reinterpret_cast<const void *>(static_cast<UINT_PTR>(argument)));
    } else if (role == OT_HAL_ROLE_TEXTURE && argument != 0 && ot_video_renderer_backend() != 0) {
        ot_video_renderer_forget(*reinterpret_cast<const uint32_t *>(static_cast<UINT_PTR>(argument)));
    }
}

extern "C" void __cdecl ot_hal_site(uint32_t role) {
    if (role == OT_HAL_ROLE_VIDEO) {
        video_ready(); // every clear: no log line
        return;
    }
    ot_log("hal: %s site reached on thread %u", role_name(role), static_cast<unsigned>(GetCurrentThreadId()));
    if (role == OT_HAL_ROLE_STARTUP) {
        startup();
    } else if (role == OT_HAL_ROLE_SHUTDOWN) {
        // WINMAIN_ShutDown only ever runs from the WM_CLOSE handler, and its
        // own D3D_Shutdown(1) normally ends the process itself (calls the
        // CRT exit()) once a Release reports outstanding references -- which
        // is why this site is reached at all only on the rare path where it
        // doesn't: a latent retail bug (reproduced on real hardware, no HAL
        // involved) where D3D_Shutdown returns instead of exiting and
        // WINMAIN_WndProc goes on to free PCMemBuffer while the game loop
        // keeps running, crashing on stale state the next frame. Reaching
        // here always means the window is closing anyway, so force that
        // deterministically instead of letting control fall back into
        // WINMAIN_WndProc to race with it.
        if (!g_window_adopted) {
            // Safe to clean up now: not inside SDL's subclassed window
            // procedure (SDL_Quit there would free the window data that
            // procedure returns into). ExitProcess below makes this optional
            // either way -- Windows reclaims every OS-level resource on
            // process exit regardless of whether SDL, the video backend or
            // the audio streams were ever asked to close gracefully.
            shutdown();
        }
        ot_log("hal: WINMAIN_ShutDown returned without exiting (retail's own quit bug); forcing exit");
        ExitProcess(0);
    } else if (role == OT_HAL_ROLE_WINDOW) {
        window_ready();
    }
}

// ------------------------------------------------------------------ patching

unsigned char *emit_rel(unsigned char *at, unsigned char opcode, const void *to) {
    const LONG relative = static_cast<LONG>(static_cast<const unsigned char *>(to) - (at + 5));
    at[0] = opcode;
    memcpy(at + 1, &relative, sizeof(relative));
    return at + 5;
}

extern "C" void __cdecl ot_hal_site_arg(uint32_t role, uint32_t argument);

// pushad; pushfd; push dword [esp + 0x28] (the first argument); push role;
// call ot_hal_site_arg; add esp, 8; popfd; popad
unsigned char *emit_callback_arg(unsigned char *at, uint32_t role) {
    *at++ = 0x60;
    *at++ = 0x9c;
    *at++ = 0xff;
    *at++ = 0x74;
    *at++ = 0x24;
    *at++ = 0x28;
    *at++ = 0x68;
    memcpy(at, &role, sizeof(role));
    at += 4;
    at = emit_rel(at, 0xe8, reinterpret_cast<const void *>(&ot_hal_site_arg));
    *at++ = 0x83;
    *at++ = 0xc4;
    *at++ = 0x08;
    *at++ = 0x9d;
    *at++ = 0x61;
    return at;
}

// pushad; pushfd; push role; call ot_hal_site; add esp, 4; popfd; popad
unsigned char *emit_callback(unsigned char *at, uint32_t role) {
    *at++ = 0x60;
    *at++ = 0x9c;
    *at++ = 0x68;
    memcpy(at, &role, sizeof(role));
    at += 4;
    at = emit_rel(at, 0xe8, reinterpret_cast<const void *>(&ot_hal_site));
    *at++ = 0x83;
    *at++ = 0xc4;
    *at++ = 0x04;
    *at++ = 0x9d;
    *at++ = 0x61;
    return at;
}

bool install_site(const OtHalSite &site, int build, unsigned char *slot) {
    const OtPatchSite &where = site.build[build];
    unsigned char *target = reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(where.address));
    if (where.address == 0 || site.overwrite < 5 || site.overwrite > OT_HOOK_MAX_OVERWRITE) {
        ot_log("hal: site %s not available in this build", site.name);
        return false;
    }
    if (memcmp(target, where.expected, site.overwrite) != 0) {
        ot_log("hal: site %s bytes differ at %08X; left alone", site.name, static_cast<unsigned>(where.address));
        return false;
    }
    // trampoline: original prologue + jmp back
    memcpy(slot, target, site.overwrite);
    emit_rel(slot + site.overwrite, 0xe9, target + site.overwrite);
    unsigned char *stub = slot + kStubOffset;
    if (site.kind == OT_HAL_SITE_BEFORE) {
        emit_rel(emit_callback(stub, site.role), 0xe9, slot);
    } else if (site.kind == OT_HAL_SITE_BEFORE_ARG) {
        emit_rel(emit_callback_arg(stub, site.role), 0xe9, slot);
    } else {
        unsigned char *at = emit_rel(stub, 0xe8, slot); // run the function
        at = emit_callback(at, site.role);
        *at = 0xc3; // ret to the original caller
    }
    FlushInstructionCache(GetCurrentProcess(), slot, 96);
    unsigned char patch[OT_HOOK_MAX_OVERWRITE];
    memset(patch, 0x90, sizeof(patch)); // jmp stub, NOP-filled to the overwrite length
    const LONG relative = static_cast<LONG>(stub - (target + 5));
    patch[0] = 0xe9;
    memcpy(patch + 1, &relative, sizeof(relative));
    DWORD old_protect;
    DWORD ignored;
    VirtualProtect(target, site.overwrite, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy(target, patch, site.overwrite);
    FlushInstructionCache(GetCurrentProcess(), target, site.overwrite);
    VirtualProtect(target, site.overwrite, old_protect, &ignored);
    ot_log("hal: %s site %s at %08X", role_name(site.role), site.name, static_cast<unsigned>(where.address));
    return true;
}

void install_deferred_sites() {
    for (uint32_t index = 0; g_spec != 0 && index < g_spec->site_count && index < OT_HAL_MAX_SITES; ++index) {
        if (g_site_deferred[index]) {
            g_site_deferred[index] = 0;
            install_site(g_spec->sites[index], g_build, g_slots + index * 96u);
        }
    }
}

// A service configured for SDL replaces the function this site is on.
bool site_replaced(const OtHalSite &site, int build) {
    for (uint32_t index = 0; index < g_spec->replacement_count; ++index) {
        const OtHalReplacement &replacement = g_spec->replacements[index];
        if (g_config.backend[replacement.service] == OT_HAL_SDL &&
            replacement.build[build].address == site.build[build].address) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------- replacements

bool replacement_ready(const OtHalReplacement &replacement) {
    const OtPatchSite &where = replacement.build[g_build];
    if (where.address == 0 || replacement.overwrite < 5 || replacement.overwrite > OT_HOOK_MAX_OVERWRITE ||
        replacement.adapter == 0 || *replacement.adapter == 0) {
        ot_log("hal: %s not available in this build", replacement.name);
        return false;
    }
    if (memcmp(reinterpret_cast<const void *>(static_cast<UINT_PTR>(where.address)), where.expected,
            replacement.overwrite) != 0) {
        ot_log("hal: %s bytes differ at %08X", replacement.name, static_cast<unsigned>(where.address));
        return false;
    }
    return true;
}

void patch_replacement(const OtHalReplacement &replacement) {
    unsigned char *target =
        reinterpret_cast<unsigned char *>(static_cast<UINT_PTR>(replacement.build[g_build].address));
    unsigned char patch[OT_HOOK_MAX_OVERWRITE];
    memset(patch, 0xcc, sizeof(patch)); // jmp adapter; the rest is never reached
    const LONG relative = static_cast<LONG>(static_cast<unsigned char *>(*replacement.adapter) - (target + 5));
    patch[0] = 0xe9;
    memcpy(patch + 1, &relative, sizeof(relative));
    DWORD old_protect;
    DWORD ignored;
    VirtualProtect(target, replacement.overwrite, PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy(target, patch, replacement.overwrite);
    FlushInstructionCache(GetCurrentProcess(), target, replacement.overwrite);
    VirtualProtect(target, replacement.overwrite, old_protect, &ignored);
    ot_log("hal: %-24s %08X -> adapter", replacement.name,
        static_cast<unsigned>(replacement.build[g_build].address));
}

// All or nothing: every hook point of the service, and every symbol (they
// are few and shared between the adapters).
bool install_service(int service) {
    if (g_spec == 0 || g_build < 0) {
        return false;
    }
    for (uint32_t index = 0; index < g_spec->symbol_count; ++index) {
        if (g_spec->symbols[index].address[g_build] == 0) {
            ot_log("hal: symbol %s absent in this build; %s stays retail", g_spec->symbols[index].name,
                ot_hal_service_name(service));
            return false;
        }
    }
    uint32_t count = 0;
    for (uint32_t index = 0; index < g_spec->replacement_count; ++index) {
        const OtHalReplacement &replacement = g_spec->replacements[index];
        if (static_cast<int>(replacement.service) != service) {
            continue;
        }
        if (!replacement_ready(replacement)) {
            ot_log("hal: %s stays retail", ot_hal_service_name(service));
            return false;
        }
        ++count;
    }
    for (uint32_t index = 0; index < g_spec->replacement_count; ++index) {
        if (static_cast<int>(g_spec->replacements[index].service) == service) {
            patch_replacement(g_spec->replacements[index]);
        }
    }
    g_installed[service] = 1;
    ot_log("hal: %s service: %u hook points replaced", ot_hal_service_name(service), static_cast<unsigned>(count));
    return true;
}

} // namespace

void ot_hal_configure(const char *ini) {
    char value[512];
    memset(&g_config, 0, sizeof(g_config));
    if (ini != 0) {
        for (int service = 0; service < OT_HAL_SERVICE_COUNT; ++service) {
            GetPrivateProfileStringA("hal", ot_hal_service_name(service), "retail", value, sizeof(value), ini);
            g_config.backend[service] = static_cast<uint8_t>(
                parse_backend(value, static_cast<unsigned>(lstrlenA(value)), OT_HAL_RETAIL));
        }
        GetPrivateProfileStringA("hal", "sdl_probe", "off", value, sizeof(value), ini);
        g_config.sdl_probe = static_cast<uint8_t>(text_is(value, static_cast<unsigned>(lstrlenA(value)), "on"));
        GetPrivateProfileStringA("hal", "video_backend", "auto", value, sizeof(value), ini);
        g_config.video_backend = parse_video_backend(value, static_cast<unsigned>(lstrlenA(value)));
    }
    char video_backend_env[16];
    const DWORD video_backend_length =
        GetEnvironmentVariableA("OPENTONY_VIDEO_BACKEND", video_backend_env, sizeof(video_backend_env));
    if (video_backend_length != 0 && video_backend_length < sizeof(video_backend_env)) {
        g_config.video_backend =
            parse_video_backend(video_backend_env, static_cast<unsigned>(video_backend_length));
        ot_log("config: OPENTONY_VIDEO_BACKEND=%s", video_backend_env);
    }
    const DWORD length = GetEnvironmentVariableA("OPENTONY_HAL", value, sizeof(value));
    if (length != 0 && length < sizeof(value)) {
        ot_log("config: OPENTONY_HAL=%s", value);
        const char *start = value;
        for (const char *at = value;; ++at) {
            if (*at == 0 || *at == ',' || *at == ';' || *at == ' ') {
                apply_token(start, static_cast<unsigned>(at - start));
                if (*at == 0) {
                    break;
                }
                start = at + 1;
            }
        }
    }
    ot_video_trace_configure();
    ot_video_mirror_configure();
    if (g_config.backend[OT_HAL_INPUT] == OT_HAL_SDL && g_config.backend[OT_HAL_WINDOW] != OT_HAL_SDL) {
        // SDL sees keyboard input only through a window it knows.
        g_config.backend[OT_HAL_WINDOW] = OT_HAL_SDL;
        ot_log("hal: input=sdl needs window=sdl; enabled");
    }
    if (g_config.backend[OT_HAL_MOVIE] == OT_HAL_SDL && g_config.backend[OT_HAL_VIDEO] != OT_HAL_SDL) {
        // Movie frames are presented through the video service.
        g_config.backend[OT_HAL_VIDEO] = OT_HAL_SDL;
        ot_log("hal: movie=sdl needs video=sdl; enabled");
    }
    if (g_config.backend[OT_HAL_VIDEO] == OT_HAL_SDL && g_config.backend[OT_HAL_MOVIE] != OT_HAL_SDL) {
        // Retail's full-screen movies decode into the primary surface, which
        // video=sdl replaces with an offscreen stand-in.
        g_config.backend[OT_HAL_MOVIE] = OT_HAL_SDL;
        ot_log("hal: video=sdl needs movie=sdl to show the full-screen movies; enabled");
    }
    if (g_config.backend[OT_HAL_VIDEO] == OT_HAL_SDL && g_config.backend[OT_HAL_WINDOW] != OT_HAL_SDL) {
        // The frame is presented on the adopted game window.
        g_config.backend[OT_HAL_WINDOW] = OT_HAL_SDL;
        ot_log("hal: video=sdl needs window=sdl; enabled");
    }
    char line[256];
    ot_hal_describe(line, sizeof(line));
    ot_log("hal: %s", line);
    char test[16];
    // 1 = attach and press on a schedule, 2 = attach only, 3 = press east
    // (the default joypad binding of the menu's accept) on the schedule.
    g_test_gamepad = GetEnvironmentVariableA("OPENTONY_HAL_TEST_GAMEPAD", test, sizeof(test)) != 0 &&
                             (test[0] == '1' || test[0] == '2' || test[0] == '3')
                         ? test[0] - '0'
                         : 0;
    char quit[16];
    const DWORD quit_length = GetEnvironmentVariableA("OPENTONY_HAL_QUIT_AFTER_MS", quit, sizeof(quit));
    if (quit_length != 0 && quit_length < sizeof(quit)) {
        for (const char *digit = quit; *digit >= '0' && *digit <= '9'; ++digit) {
            g_quit_after_ms = g_quit_after_ms * 10u + static_cast<DWORD>(*digit - '0');
        }
    }
}

void ot_hal_poll(void) {
    if (g_quit_after_ms != 0 && g_started && GetTickCount() - g_started_tick >= g_quit_after_ms &&
        InterlockedExchange(const_cast<LONG *>(&g_quit_posted), 1) == 0) {
        g_quit_tick = GetTickCount();
        EnumWindows(close_own_windows, 0);
    }
    if (g_quit_posted && !g_watchdog_done && GetTickCount() - g_quit_tick > 10000u) {
        g_watchdog_done = 1;
        log_game_thread();
    }
}

const OtHalConfig *ot_hal_config(void) {
    return &g_config;
}

void ot_hal_disable(int service) {
    if (service >= 0 && service < OT_HAL_SERVICE_COUNT && !g_installed[service]) {
        g_config.backend[service] = OT_HAL_RETAIL;
    }
}

int ot_hal_install(const OtHalSpec *spec, int build, unsigned char *slots, const char *dll_dir) {
    lstrcpynA(g_dll_dir, dll_dir, MAX_PATH);
    g_spec = spec;
    g_build = build;
    g_onexit = reinterpret_cast<OnexitRegister>(static_cast<UINT_PTR>(spec->onexit[build]));
    g_slots = slots;
    int installed = 0;
    for (uint32_t index = 0; index < spec->site_count && index < OT_HAL_MAX_SITES; ++index) {
        if (site_replaced(spec->sites[index], build)) {
            g_site_deferred[index] = 1;
            ot_log("hal: site %s left to its SDL adapter", spec->sites[index].name);
            continue;
        }
        installed += install_site(spec->sites[index], build, slots + index * 96u) ? 1 : 0;
    }
    // The clock adapters need no SDL to be installed: they start on Win32
    // time and switch to the SDL clock when SDL starts. Input waits for SDL.
    if (g_config.backend[OT_HAL_CLOCK] == OT_HAL_SDL && !install_service(OT_HAL_CLOCK)) {
        g_config.backend[OT_HAL_CLOCK] = OT_HAL_RETAIL;
    }
    return installed;
}

int ot_hal_clock_names(const OtHalSpec *spec, const char **names, int capacity) {
    int count = 0;
    if (g_config.backend[OT_HAL_CLOCK] != OT_HAL_SDL) {
        return 0;
    }
    for (uint32_t index = 0; index < spec->replacement_count && count < capacity; ++index) {
        if (spec->replacements[index].service == OT_HAL_CLOCK) {
            names[count++] = spec->replacements[index].name;
        }
    }
    return count;
}

uint32_t ot_hal_symbol(int index) {
    if (g_spec == 0 || g_build < 0 || index < 0 || static_cast<uint32_t>(index) >= g_spec->symbol_count) {
        return 0;
    }
    return g_spec->symbols[index].address[g_build];
}

const OtHalClockBackend *ot_hal_clock_backend(void) {
    return g_clock;
}

const OtHalWindowBackend *ot_hal_window_backend(void) {
    return g_window;
}

const OtHalInputBackend *ot_hal_input_backend(void) {
    return g_input;
}

void ot_hal_adopt_main_window(void) {
    window_ready();
}

void ot_hal_test_input_poll(uint32_t poll) {
    if (g_test_pad == 0 || g_test_gamepad == 2) {
        return;
    }
    const uint32_t phase = poll % 120u;
    if (g_test_gamepad == 3) {
        OT_SDL(SDL_SetJoystickVirtualButton)(g_test_pad, SDL_GAMEPAD_BUTTON_EAST, phase >= 60u && phase < 70u);
        return;
    }
    OT_SDL(SDL_SetJoystickVirtualButton)(g_test_pad, SDL_GAMEPAD_BUTTON_SOUTH, phase >= 60u && phase < 70u);
    OT_SDL(SDL_SetJoystickVirtualAxis)(g_test_pad, SDL_GAMEPAD_AXIS_LEFTX,
        static_cast<Sint16>(phase >= 90u && phase < 100u ? SDL_JOYSTICK_AXIS_MAX : 0));
}

int ot_hal_describe(char *out, int size) {
    char line[256];
    int length = 0;
    for (int service = 0; service < OT_HAL_SERVICE_COUNT; ++service) {
        length += wsprintfA(line + length, "%s%s=%s", service ? " " : "", ot_hal_service_name(service),
            g_config.backend[service] == OT_HAL_SDL ? "sdl" : "retail");
    }
    length += wsprintfA(line + length, " sdl=%s", g_sdl_started ? "running" : (ot_sdl_api.loaded ? "loaded" : "off"));
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}

int ot_hal_describe_services(char *out, int size) {
    char line[3000];
    int length = wsprintfA(line, "clock=%s window=%s",
        g_clock_ran ? "sdl" : (g_installed[OT_HAL_CLOCK] ? "win32-time" : "retail"),
        g_window_was_adopted ? "adopted" : (g_window ? "ready" : "retail"));
    if (g_input_ran) {
        const OtHalSdlStats *stats = ot_hal_sdl_stats();
        length += wsprintfA(line + length, " ");
        length += ot_hal_input_adapter_describe(line + length, static_cast<int>(sizeof(line)) - length);
        length += wsprintfA(line + length, " focused=%u unfocused=%u events=%u helper_msgs=%u pads_opened=%u",
            stats->focused_polls, stats->unfocused_polls, stats->events, stats->helper_messages, stats->pads_opened);
    }
    if (ot_video_mirror_enabled()) {
        length += wsprintfA(line + length, " ");
        length += ot_video_mirror_describe(line + length, static_cast<int>(sizeof(line)) - length);
    }
    if (g_video_ran) {
        length += wsprintfA(line + length, " video=sdl ");
        length += ot_video_hal_describe(line + length, static_cast<int>(sizeof(line)) - length);
    }
    if (g_audio_ran) {
        const OtHalSdlAudioStats *audio = ot_hal_sdl_audio_stats();
        length += wsprintfA(line + length, " audio=sdl driver=%s sounds=%u voices=%u live=%u writes=%u frames=%u ",
            ot_hal_sdl_audio_driver(), audio->sounds, audio->voices, audio->voices_live, audio->sound_writes,
            audio->frames_rendered);
        length += ot_audio_hal_describe(line + length, static_cast<int>(sizeof(line)) - length);
    }
    if (g_movie_ran) {
        length += wsprintfA(line + length, " movie=sdl ");
        length += ot_movie_hal_describe(line + length, static_cast<int>(sizeof(line)) - length);
    }
    lstrcpynA(out, line, size);
    return length < size ? length : size - 1;
}
