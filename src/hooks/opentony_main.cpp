#include "hook_runtime.h"

// opentony.dll: loaded by the dinput.dll proxy (dinput_proxy.cpp) while the
// game's static imports are initialized, i.e. before WinMain runs.
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ot_hooks_attach(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        ot_hooks_detach(reserved != 0);
    }
    return TRUE;
}
