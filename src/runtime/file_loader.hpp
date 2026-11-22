#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.11a2: the run of fileio.cpp's neighbours (0x00448dd0..0x0044a060): the whole-file loader, the step of the file layer's read, the screen fade, the font
// object's lifetime and a leftover font test screen. (FileIO_Init, OpenResource, LoadResource, SyncResource, Exists and the directory calls around them are native in
// src/assets/resource_runtime.*; here they are services.)
namespace fileio {

// ---- the file layer's words ------------------------------------------------------------------------------------------------
constexpr std::uint32_t kLoading = 0x005615d0;          // a read is in progress
constexpr std::uint32_t kReadDone = 0x005615cc;         // bytes read so far
constexpr std::uint32_t kReadSize = 0x00561544;         // bytes to read
constexpr std::uint32_t kReadBuffer = 0x005615c8;
constexpr std::uint32_t kReadHandle = 0x00561560;       // a 16-bit handle
constexpr std::uint32_t kReadPending = 0x005615d4;
constexpr std::uint32_t kReadRetries = 0x005615ec;
constexpr std::uint32_t kReadStamp = 0x00561584;        // the value of kLoading when the step began
constexpr std::uint32_t kReadMark = 0x0056155c;
constexpr std::uint32_t kSpoolerBusy = 0x005615e0;      // the other loader owns the file layer
constexpr std::uint32_t kStepping = 0x005615dc;         // the player spool manager is stepping the file layer
constexpr std::uint32_t kFileSize = 0x0056158c;         // the size of the file last read
constexpr std::uint32_t kUnusedFlag = 0x0052b9ec;       // written by FileIO_SetFlag, read by nothing

// ---- the fade -------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kFadeC = 0x00561600;            // the three colour channels, 16.16
constexpr std::uint32_t kFadeB = 0x005615fc;
constexpr std::uint32_t kFadeA = 0x00561604;
constexpr std::uint32_t kFadeStepA = 0x005615f8;
constexpr std::uint32_t kFadeStepB = 0x005615f4;
constexpr std::uint32_t kFadeStepC = 0x005615f0;
constexpr std::uint32_t kFadeLevel = 0x00561608;        // byte: the strongest fade asked for
constexpr std::uint32_t kFadeLayer = 0x0056160c;        // the draw list layer
constexpr std::uint32_t kFadeFrames = 0x00561610;       // frames of fade left
constexpr std::uint32_t kFadeFlash = 0x00561614;        // 1: a flash (an additive fade up), 0: a fade down
constexpr std::uint32_t kFadeFlashFrames = 0x00561618;  // frames of flash left

constexpr std::uint32_t kArenaCursor = 0x00560fd0;
constexpr std::uint32_t kArenaEnd = 0x00568628;
constexpr std::uint32_t kRenderer = 0x00560fd4;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kFadeWidth = 0x00527d50;        // 16-bit words: the size the fade covers
constexpr std::uint32_t kFadeHeight = 0x00527d54;

constexpr std::uint32_t kVtFont = 0x005189f4;
constexpr std::uint32_t kVtBase = 0x005184b4;

// ---- font test screen ---------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kTestExit = 0x0056b0d9;         // byte: leave the screen
constexpr std::uint32_t kTestUp = 0x0056b038;
constexpr std::uint32_t kTestDown = 0x0056b058;
constexpr std::uint32_t kTestFontName = 0x00525458;     // "mainf.fnt"
constexpr std::uint32_t kTestText = 0x0052bd18;         // "Hello world"

constexpr std::uint32_t kScratchRect = 0x005f0100;      // where the native text places the rectangle retail builds in a local (the probe records the stack address as this)

constexpr std::uint32_t kMsgNullName = 0x0052bab4;
constexpr std::uint32_t kMsgBadSize = 0x0052ba90;
constexpr std::uint32_t kMsgUnknownHeap = 0x0052ba7c;
constexpr std::uint32_t kMsgNoMemory = 0x0052ba4c;
constexpr std::uint32_t kMsgCloseFailed = 0x0052bc48;
constexpr std::uint32_t kMsgSpoolerUsing = 0x0052bc58;

class FileLoader final {
public:
    FileLoader(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- fileio.cpp ----
    std::uint32_t open_load(std::uint32_t name, std::uint32_t heap);                 // 0x00448dd0 (the buffer)
    void set_unused_flag(std::uint32_t value) { guest::write_scalar<std::uint32_t>(m_, kUnusedFlag, static_cast<std::uint32_t>(value)); }      // 0x00448f40
    void finish_read();                                                              // 0x004493f0
    void file_continue();                                                            // 0x004494b0

    // ---- the fade ----
    void fade_reset();                                                               // 0x004497e0
    [[nodiscard]] bool fade_idle();                                                  // 0x00449800
    void fade_start(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t frames, std::uint32_t level, std::uint32_t layer);   // 0x00449810
    void fade_start_flash(std::int32_t frames, std::uint32_t layer);                 // 0x004498a0
    void fade_update();                                                              // 0x004498f0
    void fade_draw();                                                                // 0x004499b0

    // ---- the font object and the font test screen ----
    std::uint32_t font_construct(std::uint32_t self);                                // 0x00449ca0
    std::uint32_t font_scalar_delete(std::uint32_t self, std::uint32_t flags);       // 0x00449cb0
    void font_base_destruct(std::uint32_t self) { guest::write_scalar<std::uint32_t>(m_, self, static_cast<std::uint32_t>(kVtBase)); }         // 0x0044a060
    void font_test_loop();                                                           // 0x00449b40

private:
    void service(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {});
    [[nodiscard]] std::uint32_t service_value(std::uint32_t address);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace fileio
}  // namespace opentony::runtime
