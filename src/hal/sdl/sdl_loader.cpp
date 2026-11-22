#include "sdl_api.h"

#include <windows.h>

// Loads SDL3.dll and fills ot_sdl_api (OT_SDL_DYNAMIC builds): shared by the
// hook DLL (src/hooks/hal/hal_runtime.cpp) and the Win32 tools.

OtSdlApi ot_sdl_api;

namespace {
HMODULE g_module = 0;
}

int ot_sdl_load(const char *directory, const char **missing) {
    char path[MAX_PATH];
    *missing = 0;
    if (ot_sdl_api.loaded) {
        return 1;
    }
    path[0] = 0;
    if (directory != 0) {
        lstrcpynA(path, directory, MAX_PATH);
    }
    if (lstrlenA(path) + 9 < MAX_PATH) {
        lstrcatA(path, "SDL3.dll");
    }
    g_module = LoadLibraryA(path);
    if (g_module == 0) {
        g_module = LoadLibraryA("SDL3.dll");
    }
    if (g_module == 0) {
        *missing = "SDL3.dll";
        return 0;
    }
#define OT_SDL_RESOLVE(name)                                                                             \
    ot_sdl_api.name = reinterpret_cast<decltype(ot_sdl_api.name)>(                                       \
        reinterpret_cast<void *>(GetProcAddress(g_module, #name)));                                      \
    if (ot_sdl_api.name == 0 && *missing == 0) {                                                         \
        *missing = #name;                                                                                \
    }
    OT_SDL_FUNCTIONS(OT_SDL_RESOLVE)
#undef OT_SDL_RESOLVE
    if (*missing != 0) {
        FreeLibrary(g_module);
        g_module = 0;
        memset(&ot_sdl_api, 0, sizeof(ot_sdl_api));
        return 0;
    }
    ot_sdl_api.loaded = 1;
    return 1;
}
