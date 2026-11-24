// The cold boot (milestone 9): from the static data alone (no session image) to the game's own `main`, as the retail executable gets there.
//
// Retail: the C runtime's entry (0x00502f74) runs `_cinit` (0x0050140c: the C initializers 0x0052411c..0x00524134, then the C++ dynamic initializers
// 0x00524000..0x00524118) and calls WinMain, `Game_Startup` (0x004f7e30), which registers the window class, checks the disc, runs PCinit, builds the
// options file's path, creates the window, starts DirectDraw, the options, Direct3D, DirectInput, DirectSound, the primitive dispatch, the texture
// formats and the multimedia timer, and calls `main` (0x0046a770). Everything `main` reaches is a native or a seam.
//
// Here: the C++ initializers run in table order (a native through the resolver, a `jmp` stub's body as transcribed in
// standalone_static_init_generated.cpp, a constructor stub as written below); the C initializers are the C runtime's own (the host's runtime has its
// own). `Game_Startup` is replaced, not reconstructed (scope.yml: platform-hal, a hook point): its calls into game code are made in its order, and its
// DirectX, DirectInput and winmm calls are stood in for by what they leave in the game's memory, read from its disassembly. Where the value is the
// device's answer (the texture formats, the timer's capabilities, the device's caps), the standalone presents the device the retail recordings ran
// on (Wine's Direct3D under the headless display, re/config/wine.yml), so the answer is that device's, said at each place.
//
// docs/HOOKS.md, "Cold boot"; PHASE_5_SUMMARY.md, "9 result".
#include "kit_bridge.hpp"
#include "standalone_options.hpp"
#include "standalone_static_init.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "guest_arena.hpp"

namespace opentony::kitdll {
namespace {

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

std::uint32_t call(std::uint32_t address, std::initializer_list<std::uint32_t> args = {}) { return call_native_or_seam(address, std::vector<std::uint32_t>(args)); }

void log(const char* text) {
    std::printf("boot: %s\n", text);
    std::fflush(stdout);
}

// ---- the C++ dynamic initializers ---------------------------------------------------------------------------------------------------------------

constexpr std::uint32_t kInitializers = 0x00524000;     // __xc_a: a null first slot, then the functions, to __xc_z
constexpr std::uint32_t kInitializersEnd = 0x00524118;
constexpr std::uint32_t kAtExit = 0x00500260;           // atexit(function), the host's (standalone_host.cpp)

// The SDX input wrappers' constructors (out of scope with the DirectInput layer, platform-hal): each stores its vtable and clears the fields its
// destructor and the device calls test. From the disassembly of each.
void construct_keyboard(std::uint32_t self) {         // 0x004ec4a0 on 0x006a43e0
    retail<std::uint32_t>(self) = 0x00519a1c;
    retail<std::uint8_t>(self + 0x10c) = 0;
    retail<std::uint32_t>(self + 0x108) = 0;
}

void construct_mouse(std::uint32_t self) {            // 0x004ec6e0 on 0x006a3f20: the fields, then 0x004ec760 (more fields) and SDXMouse_SetBounds(0, 0, 640, 480)
    retail<std::uint32_t>(self) = 0x00519a20;
    retail<std::uint32_t>(self + 0x28) = 0;
    retail<std::uint8_t>(self + 0x50) = 0;
    for (const std::uint32_t field : {0x30u, 0x34u, 0x04u, 0x08u, 0x10u, 0x38u, 0x3cu, 0x40u, 0x44u, 0x48u, 0x4cu}) {
        retail<std::uint32_t>(self + field) = 0;
    }
    call(0x004ec930, {self, 0, 0, 0x280, 0x1e0});
    retail<std::uint32_t>(self + 0x0c) = 0x100;
}

void construct_joystick(std::uint32_t self) {         // 0x004ecc70 on 0x006a42e8
    retail<std::uint32_t>(self) = 0x00519a24;
    retail<std::uint8_t>(self + 0xec) = 0;
    retail<std::uint8_t>(self + 0x60) = 0;
    retail<std::uint32_t>(self + 0x64) = 0;
    retail<std::uint32_t>(self + 0x68) = 0;
    for (std::uint32_t k = 0; k < 0x20; ++k) {
        retail<std::uint32_t>(self + 0x6c + 4 * k) = 0;
    }
    retail<std::uint32_t>(self + 0x58) = 0;
}

void construct_effect_device(std::uint32_t self) {    // 0x004ed400, the two force-feedback devices at 0x006a3f78 (0x1b4 bytes each)
    retail<std::uint32_t>(self) = 0x00519a28;
    retail<std::uint32_t>(self + 0x4) = 0;
    retail<std::uint8_t>(self + 0x1b0) = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        retail<std::uint32_t>(self + 0x130 + 4 * k) = 0;
    }
    for (std::uint32_t k = 0; k < 5; ++k) {
        retail<std::uint32_t>(self + 0x140 + 4 * k) = 0;
    }
    for (std::uint32_t k = 0; k < 0xe; ++k) {
        retail<std::uint32_t>(self + 0x154 + 4 * k) = 0;
    }
    retail<std::uint32_t>(self + 0x19c) = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        retail<std::uint32_t>(self + 0x18c + 4 * k) = retail<std::uint32_t>(0x0051a4c8 + 4 * k);
    }
}

// A `call CONSTRUCTOR; jmp REGISTER` stub: the constructor, then REGISTER, `push DESTRUCTOR; call atexit`. Most stub callees are `mov ecx, GLOBAL; jmp
// CLASS_CONSTRUCTOR`: the class's constructor (a native, a thiscall) is called on the global here, since the callee's own native reports the jump without
// the object. The camera ones are natives that call their constructor themselves; the input wrappers' are written above.
struct ConstructorStub {
    std::uint32_t function;      // the slot's function
    std::uint32_t constructor;   // the native to call, or 0 for a constructor written above
    std::uint32_t object;        // ECX for a class constructor, 0 for a native that takes no arguments
    void (*by_hand)();
    std::uint32_t destructor;    // what REGISTER hands to atexit
};

const ConstructorStub kConstructorStubs[] = {
    {0x0040b560, 0x0040b570, 0, nullptr, 0x0040b5a0},            // Camera_QuaternionIdentity
    {0x0040b5b0, 0x0040b5c0, 0, nullptr, 0x0040b5f0},            // Camera_ResetA
    {0x0040b600, 0x0040b610, 0, nullptr, 0x0040b640},            // Camera_ResetB
    {0x004195a0, 0x0041ad40, 0x0055fe28, nullptr, 0x004195d0},   // Competition_Constructor
    {0x00423b00, 0x00424550, 0x005601d0, nullptr, 0x00423b30},   // Spooler_Construct
    {0x0044b170, 0x00449ca0, 0x005619e0, nullptr, 0x0044b1a0},   // Font_Construct
    {0x0044b1b0, 0x004b9730, 0x005617a0, nullptr, 0x0044b1e0},   // FallingTextEffect_Construct
    {0x004b0b10, 0x004b0b50, 0x0056c580, nullptr, 0x004b0b40},   // RecordBank_Construct
    {0x004c43a0, 0x004c43f0, 0x0056db98, nullptr, 0x004c43d0},   // Singleton_Construct
    {0x004e3d20, 0x004ebf10, 0x006a4500, nullptr, 0x004e3d50},   // Stream_Construct
    {0x004e3d60, 0, 0, [] { construct_keyboard(0x006a43e0); }, 0x004e3d90},
    {0x004e3da0, 0, 0, [] { construct_mouse(0x006a3f20); }, 0x004e3dd0},
    {0x004e3de0, 0, 0, [] { construct_joystick(0x006a42e8); }, 0x004e3e10},
    // the vector constructor iterator over the two force-feedback devices (0x006a3f78, 0x1b4 bytes, 2, 0x004ed400, destructor 0x004ed4b0)
    {0x004e3e20, 0, 0, [] { construct_effect_device(0x006a3f78); construct_effect_device(0x006a3f78 + 0x1b4); }, 0x004e3e60},
};

void run_dynamic_initializers() {
    unsigned natives = 0, transcribed = 0, stubs = 0;
    for (std::uint32_t slot = kInitializers + 4; slot < kInitializersEnd; slot += 4) {
        const std::uint32_t function = retail<std::uint32_t>(slot);
        if (function == 0) {
            continue;
        }
        bool done = false;
        for (std::size_t k = 0; k < kTranscribedInitializerCount && !done; ++k) {
            if (kTranscribedInitializers[k].slot == slot) {
                kTranscribedInitializers[k].body();
                ++transcribed;
                done = true;
            }
        }
        for (const ConstructorStub& stub : kConstructorStubs) {
            if (done || stub.function != function) {
                continue;
            }
            if (stub.by_hand != nullptr) {
                stub.by_hand();
            } else if (stub.object != 0) {
                call(stub.constructor, {stub.object});
            } else {
                call(stub.constructor);
            }
            call(kAtExit, {stub.destructor});
            ++stubs;
            done = true;
        }
        if (!done) {
            call(function);   // a native: the slot's function is the whole initializer
            ++natives;
        }
    }
    char text[128];
    std::snprintf(text, sizeof text, "static initializers: %u natives, %u transcribed, %u constructor stubs", natives, transcribed, stubs);
    log(text);
}

// ---- Game_Startup's platform calls ------------------------------------------------------------------------------------------------------------

constexpr std::uint32_t kCommandLine = 0x029d8370;   // WinMain's lpCmdLine, kept by Game_Startup
constexpr std::uint32_t kMainWindow = 0x029d8374;
constexpr std::uint32_t kOptionsPath = 0x029d8378;   // the working directory, "\" (0x00540c34), "TH2_OPT.CFG" (0x0055c344)
constexpr std::uint32_t kWorkingDirectory = 0x006a71bc;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;


// WINMAIN_InitDirectDraw (0x004f8130): the display-mode list cleared (0x7c1 words at 0x029d8478), DirectDraw created, the cooperative level and the
// 640 x 480 x 16 display mode set (the screen words default to that size), the flipping primary and its back buffer made. The surfaces are the HAL's.
void init_direct_draw() {
    for (std::uint32_t k = 0; k < 0x7c1; ++k) {
        retail<std::uint32_t>(0x029d8478 + 4 * k) = 0;
    }
    if (retail<std::uint32_t>(kScreenWidth) == 0 && retail<std::uint32_t>(kScreenHeight) == 0) {
        retail<std::uint32_t>(kScreenHeight) = static_cast<std::uint32_t>(display().height);   // 640 x 480 unless the program made a wider display (standalone_options.hpp)
        retail<std::uint32_t>(kScreenWidth) = static_cast<std::uint32_t>(display().width);
    }
}

// WINMAIN_InitDirect3D(1) (0x004f7110): the device enumeration chooses the hardware device, the z-buffer is made, and the device's caps are kept:
// D3DTextureCaps (0x006a052c) and 0x006a0530 from its texture caps bits 1 and 5, 0x006a053c / 0x006a0540 from two of its fields, 0x006a0534 / 0x006a0538
// = 0x100. The values are those of the device the recordings ran on (Wine's hardware device: perspective-correct textures 1, bit 5 clear, 1, 1).
void init_direct3d() {
    retail<std::uint32_t>(0x006a052c) = 1;
    retail<std::uint32_t>(0x006a0530) = 0;
    retail<std::uint32_t>(0x006a053c) = 1;
    retail<std::uint32_t>(0x006a0540) = 1;
    retail<std::uint32_t>(0x006a0534) = 0x100;
    retail<std::uint32_t>(0x006a0538) = 0x100;
}

// The software renderer's start-up (0x004f2720, out of scope with the renderer it starts; DrawKit has no software renderer, but the game's memory has its
// state): three words and the two at 0x029d68ac cleared, then 0x004f2740: its 11-word texture record at 0x006a9690 cleared and set to a 64 x 64 16-bit
// texture (+0x18 and +0x1a = 64, flag 0x10 at +0x10, the pixels at +8 = 0x006a7690), whose pixels are a grid: white (0xffff) on every fourth row and column,
// 0x801f between.
void init_software_renderer() {
    for (const std::uint32_t word : {0x0054ca38u, 0x0054ca34u, 0x0054ca30u, 0x029d68acu, 0x029d68b0u}) {
        retail<std::uint32_t>(word) = 0;
    }
    constexpr std::uint32_t kRecord = 0x006a9690, kPixels = 0x006a7690;
    for (std::uint32_t k = 0; k < 0xb; ++k) {
        retail<std::uint32_t>(kRecord + 4 * k) = 0;
    }
    retail<std::uint16_t>(kRecord + 0x18) = 0x40;
    retail<std::uint16_t>(kRecord + 0x1a) = 0x40;
    retail<std::uint32_t>(kRecord + 0x10) = retail<std::uint32_t>(kRecord + 0x10) | 0x10;
    retail<std::uint32_t>(kRecord + 8) = kPixels;
    for (std::uint32_t row = 0; kPixels + row * 0x80 < kRecord; ++row) {
        for (std::uint32_t column = 0; column < 0x40; ++column) {
            retail<std::uint16_t>(kPixels + row * 0x80 + 2 * column) = (row & 3) != 0 && (column & 3) != 0 ? 0x801f : 0xffff;
        }
    }
}

// D3D_InitPrimitiveDispatch (0x004d41e0): D3DMODEL_Startup, the software renderer's start-up and Init_PolyBuf, the seven device-state caches to -1, the
// bound texture (0x006a04c0) and TexpageBlendMode (0x006a04bc) to 0, every one of the 256 dispatch entries to the do-nothing handler 0x004d7420 and
// seventeen to their handlers, then a device call that sets the render state block at 0x0061d740 (the HAL's).
void init_primitive_dispatch() {
    call(0x004d2da0);   // D3DMODEL_Startup
    init_software_renderer();
    call(0x004d7430);   // Init_PolyBuf
    for (const std::uint32_t cache : {0x00549da0u, 0x00549da4u, 0x00549dacu, 0x00549da8u, 0x00549db0u, 0x00549db4u, 0x00549db8u}) {
        retail<std::uint32_t>(cache) = 0xffffffffu;
    }
    retail<std::uint32_t>(0x006a04c0) = 0;
    retail<std::uint32_t>(0x006a04bc) = 0;
    constexpr std::uint32_t kTable = 0x0065f82c;   // D3DPrimitiveDispatchTable
    for (std::uint32_t k = 0; k < 0x100; ++k) {
        retail<std::uint32_t>(kTable + 4 * k) = 0x004d7420;
    }
    static const std::pair<std::uint32_t, std::uint32_t> kHandlers[] = {
        {0x0065faec, 0x004d68b0}, {0x0065f8ac, 0x004d42f0}, {0x0065f8bc, 0x004d45f0}, {0x0065f8cc, 0x004d49e0}, {0x0065f8dc, 0x004d4bf0},
        {0x0065f8ec, 0x004d5040}, {0x0065f8fc, 0x004d5280}, {0x0065f90c, 0x004d56c0}, {0x0065f91c, 0x004d5960}, {0x0065f92c, 0x004d6560},
        {0x0065f94c, 0x004d6120}, {0x0065f95c, 0x004d6320}, {0x0065f96c, 0x004d66f0}, {0x0065f9ac, 0x004d5e40}, {0x0065f9cc, 0x004d6090},
        {0x0065f9ec, 0x004d60c0}, {0x0065fa0c, 0x004d60f0},
    };
    for (const auto& [entry, handler] : kHandlers) {
        retail<std::uint32_t>(entry) = handler;
    }
    // IDirect3DDevice7::GetCaps into the D3DDEVICEDESC7 at 0x0061d740: the device the recordings ran on (Wine's hardware device under the headless
    // display) as it describes itself, word for word; the words after the 55th stay 0. The game reads its limits from here (the texture sizes, the
    // primitive caps).
    static const std::uint32_t kDeviceDescription[] = {
        0x0009aff1, 0x00000038, 0x00000072, 0x003363b9, 0x000000ff, 0x00001fff, 0x000007ff, 0x000000ff,
        0x000c528a, 0x00000d9f, 0x0703073f, 0x000000ff, 0x0000001f, 0x00000020, 0x00000020, 0x00000038,
        0x00000072, 0x003363b9, 0x000000ff, 0x00001fff, 0x000007ff, 0x000000ff, 0x000c528a, 0x00000d9f,
        0x0703073f, 0x000000ff, 0x0000001f, 0x00000020, 0x00000020, 0x00000700, 0x00000600, 0x00000001,
        0x00000001, 0x00004000, 0x00004000, 0x00008000, 0x00004000, 0x00000010, 0xc7000000, 0xc7000000,
        0x47000000, 0x47000000, 0x00000000, 0x000000ff, 0x00100008, 0x00fef7ff, 0x00080008, 0x00000008,
        0x501502f9, 0xf5049e78, 0x11d24861, 0xa00007a4, 0xa82906c9, 0x00040008, 0x0000003f,
    };
    for (std::size_t k = 0; k < std::size(kDeviceDescription); ++k) {
        retail<std::uint32_t>(0x0061d740 + 4 * static_cast<std::uint32_t>(k)) = kDeviceDescription[k];
    }
}

// D3DTex_Init (0x004da840): the device's texture formats are enumerated (their count at 0x006a04d0) and the first 16-bit ARGB 1555 one is kept at
// 0x006a04d8 (a DDPIXELFORMAT of 8 words), then its four channels' bit counts (0x006a04f8..) and lowest bits (0x006a0508..). The device the
// recordings ran on lists 14 formats; its 1555 format is {0x20, DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0x7c00, 0x03e0, 0x001f, 0x8000}.
void init_textures() {
    retail<std::uint32_t>(0x006a04d0) = 14;
    static const std::uint32_t kFormat[8] = {0x20, 0x41, 0, 16, 0x7c00, 0x03e0, 0x001f, 0x8000};
    for (std::uint32_t k = 0; k < 8; ++k) {
        retail<std::uint32_t>(0x006a04d8 + 4 * k) = kFormat[k];
    }
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t mask = kFormat[4 + k];
        retail<std::uint32_t>(0x006a04f8 + 4 * k) = call(0x004d0a20, {mask});   // D3D_BitCount
        retail<std::uint32_t>(0x006a0508 + 4 * k) = call(0x004d0a40, {mask});   // D3D_LowestBit
    }
}

// D3DTimer_Init (0x004dabb0): the timer state cleared, timeGetDevCaps into TimerCaps (0x006a0588: the device the recordings ran on answers 1 and 0xffff
// ms), the period clamped to 16 ms (0x006a05a8), D3DTimer_StartEvent(&TimerState, 16, SimulationTimeTimerCallback) (0x004dad80: the callback at +0x10, the
// interval at +4, timeSetEvent's id at +0), TimerTickFrames = 16 x 0.06 = 0.96 (the double at 0x006a05b8), timeBeginPeriod, and _onexit(D3DTimer_Shutdown).
// The standalone has no multimedia timer: the clock (standalone_seams.cpp) runs the callback itself while the id is not 0. The id is the first one
// winmm hands out, 0x10, as in the recordings.
constexpr std::uint32_t kTimerState = 0x006a05a0;
constexpr std::uint32_t kTimerCaps = 0x006a0588;

void init_timer() {
    for (const std::uint32_t word : {kTimerState, kTimerState + 4, kTimerCaps, kTimerState + 8, kTimerState + 0xc, kTimerCaps + 4, kTimerState + 0x10}) {
        retail<std::uint32_t>(word) = 0;
    }
    retail<std::uint32_t>(kTimerCaps) = 1;
    retail<std::uint32_t>(kTimerCaps + 4) = 0xffff;
    std::uint32_t period = retail<std::uint32_t>(kTimerCaps + 4);
    if (16 < period) {
        period = retail<std::uint32_t>(kTimerCaps) < 0x11 ? 0x10 : retail<std::uint32_t>(kTimerCaps);
    }
    retail<std::uint32_t>(kTimerState + 8) = period;
    // D3DTimer_StartEvent(&TimerState, 16, callback): the callback, the interval (raised to the period when it is below it), timeSetEvent's id
    retail<std::uint32_t>(kTimerState + 0x10) = 0x004dace0;
    retail<std::uint32_t>(kTimerState + 4) = 16 < period ? period : 16;
    retail<std::uint32_t>(kTimerState) = 0x10;
    const double frames = static_cast<double>(retail<std::uint32_t>(kTimerState + 4)) * retail<double>(0x00519980) * retail<double>(0x00519978);
    retail<double>(0x006a05b8) = frames;
    call(kAtExit, {0x004dab80});   // _onexit(D3DTimer_Shutdown)
}

// PCInput_Initialize(window, instance) (0x004e4690): the controller defaults and the bindings (natives), then the DirectInput devices: the keyboard,
// the mouse (its bounds 0, 0, 0x260, 0x1c0) and the joysticks. The devices are the HAL's (standalone_platform.cpp); the game's wrappers are left as a
// machine with no joystick leaves them.
void init_input() {
    call(0x00413f30, {0});   // Options_ResetControllerDefaults(0)
    call(0x00413f30, {1});
    call(0x004e3e80);        // PCInput_ResetBindings
    call(0x004e4d10);        // PCInput_LoadBindings
    // the devices' wrappers linked as their creation links them (0x004ec500 the keyboard, 0x004ec790 the mouse): each holds the input object (+0x104,
    // +0x24) and the input object holds them (+8, +0xc) and the instance (+0x10). Their DirectInput objects and the mouse's event handle are the OS's.
    constexpr std::uint32_t kInput = 0x006a4500, kKeyboardWrapper = 0x006a43e0, kMouseWrapper = 0x006a3f20;
    retail<std::uint32_t>(kInput + 8) = kKeyboardWrapper;
    retail<std::uint32_t>(kInput + 0xc) = kMouseWrapper;
    retail<std::uint32_t>(kInput + 0x10) = 0x00400000;
    retail<std::uint32_t>(kKeyboardWrapper + 0x104) = kInput;
    retail<std::uint8_t>(kKeyboardWrapper + 0x10c) = 1;   // acquired
    retail<std::uint32_t>(kMouseWrapper + 0x24) = kInput;
    call(0x004ec930, {kMouseWrapper, 0, 0, 0x260, 0x1c0});   // SDXMouse_SetBounds
    retail<std::uint32_t>(0x006a3f2c) = 0x80;
}

}  // namespace

bool g_booting = false;

// The whole start-up, then `main`; the answer is main's (the process ends in the clock's frame budget or the recorder's limit before that, as a rule).
std::uint32_t standalone_boot() {
    g_booting = true;
    run_dynamic_initializers();
    // Game_Startup (0x004f7e30), in its order
    // the (empty) command line is a guest block for the process's lifetime, as Windows keeps retail's
    const std::uint32_t command_line = arena::heap_allocate(16);
    if (command_line == 0) {
        throw runtime::MemoryFault("boot: no room for the command line");
    }
    arena::at<std::uint8_t>(command_line) = 0;
    retail<std::uint32_t>(kCommandLine) = command_line;
    call(0x004f7d80, {retail<std::uint32_t>(kCommandLine), 0x00400000});   // WINMAIN_Nothing(command line, instance)
    call(0x004e73f0);                                                       // PCinit
    std::strcpy(arena::chars(kOptionsPath), arena::text(kWorkingDirectory));
    std::strcat(arena::chars(kOptionsPath), arena::text(0x00540c34));
    std::strcat(arena::chars(kOptionsPath), arena::text(0x0055c344));
    retail<std::uint32_t>(kMainWindow) = 0;   // the HAL's window; the game passes the handle only to the platform calls stood in for here
    init_direct_draw();
    call(0x004f6b90);   // WINMAIN_LoadOptions
    call(0x004f6c60);   // WINMAIN_SaveOptions
    options_after_load();   // the player's options that replace what the file holds (standalone_options.cpp); nothing without them
    call(0x004f7ce0);   // Game_FrameTick
    init_direct3d();
    init_input();
    options_after_input();
    call(0x004f27c0, {retail<std::uint32_t>(kMainWindow)});   // Audio_Init (standalone_audio.cpp)
    // Game_FrameTick: retail goes on when it answers 0x75 (an empty message queue) and shuts down otherwise; the seam answers SimulationPublicTick
    // (standalone_seams.cpp), and a closed window ends the program there
    call(0x004f7ce0);
    call(0x004d0ec0);   // D3D_ClearBuffers
    init_primitive_dispatch();
    init_textures();
    init_timer();
    call(0x004dd890, {0x00400000});   // NetGame_Reset(instance)
    retail<std::uint8_t>(0x029da3a5) = 0;
    call(0x004e3f80, {1});            // PCInput_SetControlMode(1)
    call(0x004f7ce0);                 // Game_FrameTick: the first delivery of the timer
    call(0x005015a6, {0, 0});         // _controlfp(0, 0)
    log("main");
    return call(0x0046a770);          // main
}

bool standalone_booting() { return g_booting; }

}  // namespace opentony::kitdll

// 9: the cold boot, for a program that has laid out the static data and attached the Kits (opentony_spike.exe, OT_SPIKE_CALL=boot)
extern "C" __declspec(dllexport) std::uint32_t __cdecl ot_standalone_boot() {
    return opentony::kitdll::standalone_boot();
}
