// dinput.dll proxy: the injection path for opentony.dll.
//
// Both supported THawk2.exe builds import DINPUT.dll!DirectInputCreateA, so a
// dinput.dll next to the executable is loaded during static-import
// initialization. On attach it loads opentony.dll from the same directory;
// DirectInput calls are forwarded to the system dinput.dll, or to another
// proxy named by `[proxy] chain=` in opentony.ini (for example a renamed
// Cumulative Patch dinput.dll), so OpenTony can sit in front of other mods.
//
// Wine prefers its builtin dinput; run with WINEDLLOVERRIDES=dinput=n,b.

#include <windows.h>

#include <string.h>

namespace {

typedef HRESULT(WINAPI *DirectInputCreateAFn)(HINSTANCE, DWORD, void **, void *);
typedef HRESULT(WINAPI *DirectInputCreateWFn)(HINSTANCE, DWORD, void **, void *);
typedef HRESULT(WINAPI *DirectInputCreateExFn)(HINSTANCE, DWORD, const GUID &, void **, void *);
typedef HRESULT(WINAPI *DllCanUnloadNowFn)(void);
typedef HRESULT(WINAPI *DllGetClassObjectFn)(const GUID &, const GUID &, void **);

const HRESULT kFail = (HRESULT)0x80004005L; // E_FAIL

HMODULE g_self = 0;
HMODULE g_forward = 0;
char g_dir[MAX_PATH];

void module_directory(HMODULE module, char *out) {
    char *slash;
    GetModuleFileNameA(module, out, MAX_PATH);
    slash = strrchr(out, '\\');
    if (slash == 0) {
        slash = strrchr(out, '/');
    }
    if (slash != 0) {
        slash[1] = 0;
    } else {
        out[0] = 0;
    }
}

HMODULE forward_module() {
    char path[MAX_PATH];
    char ini[MAX_PATH];
    char chain[MAX_PATH];

    if (g_forward != 0) {
        return g_forward;
    }
    lstrcpynA(ini, g_dir, MAX_PATH - 16);
    lstrcatA(ini, "opentony.ini");
    GetPrivateProfileStringA("proxy", "chain", "", chain, sizeof(chain), ini);
    if (chain[0] != 0) {
        if (strchr(chain, ':') == 0 && chain[0] != '\\') {
            lstrcpynA(path, g_dir, MAX_PATH);
            lstrcatA(path, chain);
        } else {
            lstrcpynA(path, chain, MAX_PATH);
        }
        g_forward = LoadLibraryA(path);
        if (g_forward == g_self) {
            g_forward = 0;
        }
    }
    if (g_forward == 0) {
        // Load by full path: loading "dinput.dll" by name would find us.
        GetSystemDirectoryA(path, MAX_PATH - 16);
        lstrcatA(path, "\\dinput.dll");
        g_forward = LoadLibraryA(path);
    }
    return g_forward;
}

FARPROC forward(const char *name) {
    const HMODULE module = forward_module();
    return module != 0 ? GetProcAddress(module, name) : 0;
}

} // namespace

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE instance, DWORD version, void **out, void *outer) {
    const DirectInputCreateAFn function = (DirectInputCreateAFn)forward("DirectInputCreateA");
    return function != 0 ? function(instance, version, out, outer) : kFail;
}

extern "C" HRESULT WINAPI DirectInputCreateW(HINSTANCE instance, DWORD version, void **out, void *outer) {
    const DirectInputCreateWFn function = (DirectInputCreateWFn)forward("DirectInputCreateW");
    return function != 0 ? function(instance, version, out, outer) : kFail;
}

extern "C" HRESULT WINAPI DirectInputCreateEx(
    HINSTANCE instance, DWORD version, const GUID &iid, void **out, void *outer) {
    const DirectInputCreateExFn function = (DirectInputCreateExFn)forward("DirectInputCreateEx");
    return function != 0 ? function(instance, version, iid, out, outer) : kFail;
}

extern "C" HRESULT WINAPI DllCanUnloadNow(void) {
    return (HRESULT)1L; // S_FALSE: the game keeps DirectInput for its lifetime
}

extern "C" HRESULT WINAPI DllGetClassObject(const GUID &clsid, const GUID &iid, void **out) {
    const DllGetClassObjectFn function = (DllGetClassObjectFn)forward("DllGetClassObject");
    return function != 0 ? function(clsid, iid, out) : kFail;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        module_directory(instance, g_dir);
        lstrcpynA(path, g_dir, MAX_PATH - 16);
        lstrcatA(path, "opentony.dll");
        if (LoadLibraryA(path) == 0) {
            OutputDebugStringA("OpenTony dinput proxy: opentony.dll not loaded\n");
        }
    }
    return TRUE;
}
