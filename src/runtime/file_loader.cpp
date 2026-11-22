#include "auto_views.hpp"
#include "file_loader.hpp"
#include "seam_addresses_generated.hpp"

#include "player_spooler.hpp"

namespace opentony::runtime::fileio {

using namespace opentony::runtime::autoviews;

namespace {

// the engine entry points this run reaches (reported by address)
constexpr std::uint32_t kNew = 0x00466b70;
constexpr std::uint32_t kDelete = 0x00466bb0;               // operator delete(block)
constexpr std::uint32_t kFileOpenResource = 0x00449030;     // FileIO_OpenResource(name): the size
constexpr std::uint32_t kFileLoadResource = 0x00449230;     // FileIO_LoadResource(buffer)
constexpr std::uint32_t kFileSyncResource = 0x00449660;     // FileIO_SyncResource()
constexpr std::uint32_t kMemShrink = 0x0046f4e0;            // the block is cut to a size
constexpr std::uint32_t kBackendClose = 0x004e7bc0;         // FileIO_BackendClose(handle)
constexpr std::uint32_t kBackendRead = 0x004e7ad0;          // read(handle, buffer, size): -1 on failure
constexpr std::uint32_t kPump = 0x00489a10;                 // the engine's per-wait pump
constexpr std::uint32_t kDrawRect = 0x004e8730;             // draw a rectangle (rect, colour, 0, 0)
constexpr std::uint32_t kProgress = 0x0045d690;             // LoadingScreen_UpdateProgress()
constexpr const char* kAlloc = "alloc";                     // FileIO_AllocLoadedBuffer(size, heap, 1, 0)

}  // namespace

void FileLoader::service(std::uint32_t address, std::vector<std::uint32_t> args, std::string text) {
    s_.note(pspool::service_name(address), std::move(args), std::move(text));
}

std::uint32_t FileLoader::service_value(std::uint32_t address) { return static_cast<std::uint32_t>(s_.value(pspool::service_name(address))); }

// 0x00448dd0: read a whole file into a block of the heap (0 or 1) and cut the block to the file's size.
std::uint32_t FileLoader::open_load(std::uint32_t name, std::uint32_t heap) {
    s_.check(name != 0, kMsgNullName);
    service(kFileOpenResource, {name});
    const std::int32_t size = static_cast<std::int32_t>(service_value(kFileOpenResource));
    s_.check(size > 0 && size < 2000000, kMsgBadSize);
    s_.check(heap == 0 || heap == 1, kMsgUnknownHeap);
    s_.note(kAlloc, {static_cast<std::uint32_t>(size), heap, 1, 0});
    const std::uint32_t buffer = static_cast<std::uint32_t>(s_.value(kAlloc));
    s_.check(buffer != 0, kMsgNoMemory);
    service(kFileLoadResource, {buffer});
    service(kFileSyncResource);
    service(kMemShrink, {buffer, (global_word(m_, kFileSize) + 3) & 0xfffffc});
    return buffer;
}

// 0x004493f0: close the file that was read and say that nothing is loading.
void FileLoader::finish_read() {
    const std::int32_t handle = static_cast<std::int16_t>(global_half(m_, kReadHandle));
    service(kBackendClose, {static_cast<std::uint32_t>(handle)});
    const std::int32_t result = static_cast<std::int32_t>(service_value(kBackendClose));
    s_.check(result >= 0, kMsgCloseFailed);
    set_global_word(m_, kLoading, 0);
    set_global_word(m_, kReadPending, 0);
}

// 0x004494b0: one step of the file layer's read: the whole file is read in one call, the next step closes it; a failed read is retried and draws the error bars.
void FileLoader::file_continue() {
    if (global_word(m_, kSpoolerBusy) != 0) {
        set_global_word(m_, kLoading, 0);
        return;
    }
    s_.check(global_byte(m_, kStepping) == 0, kMsgSpoolerUsing);
    set_global_word(m_, kReadStamp, global_word(m_, kLoading));
    bool failed = false;
    if (global_word(m_, kLoading) != 0) {
        if (global_word(m_, kReadDone) == global_word(m_, kReadSize)) {
            const std::int32_t handle = static_cast<std::int16_t>(global_half(m_, kReadHandle));
            service(kBackendClose, {static_cast<std::uint32_t>(handle)});
            const std::int32_t result = static_cast<std::int32_t>(service_value(kBackendClose));
            s_.check(result >= 0, kMsgCloseFailed);
            set_global_word(m_, kLoading, 0);
            set_global_word(m_, kReadPending, 0);
            set_global_word(m_, kReadRetries, 0);
            service(kProgress);
            return;
        }
        service(kPump);
        const std::int32_t handle = static_cast<std::int16_t>(global_half(m_, kReadHandle));
        service(kBackendRead, {static_cast<std::uint32_t>(handle), global_word(m_, kReadBuffer), global_word(m_, kReadSize)});
        const std::int32_t result = static_cast<std::int32_t>(service_value(kBackendRead));
        if (result != -1) {
            set_global_word(m_, kReadRetries, 0);
            set_global_word(m_, kReadDone, global_word(m_, kReadDone) + global_word(m_, kReadSize));
        } else {
            set_global_word(m_, kReadRetries, global_word(m_, kReadRetries) + 1);
            failed = true;
        }
    }
    set_global_word(m_, kReadMark, 0);
    if (failed) {
        // two bars: a rectangle {0x1e5, 0x12, 10, 10} and the same one 0x100 lower in its second short (built in a local: the call is handed its address)
        auto rect = [](std::uint16_t h) {
            const std::uint16_t words[4] = {0x1e5, h, 10, 10};
            std::string text(8, '\0');
            for (int i = 0; i < 4; ++i) {
                text[2 * i] = static_cast<char>(words[i] & 0xff);
                text[2 * i + 1] = static_cast<char>(words[i] >> 8);
            }
            return text;
        };
        service(kDrawRect, {kScratchRect, 0xff, 0, 0}, rect(0x12));
        service(kDrawRect, {kScratchRect, 0xff, 0, 0}, rect(0x112));
    }
    service(kProgress);
}

// ---- the fade -------------------------------------------------------------------------------------------------------------

// 0x004497e0
void FileLoader::fade_reset() {
    set_global_word(m_, kFadeFrames, 0);
    set_global_word(m_, kFadeFlashFrames, 0);
    set_global_word(m_, kFadeFlash, 0);
    set_global_byte(m_, kFadeLevel, 0);
}

// 0x00449800
bool FileLoader::fade_idle() { return global_word(m_, kFadeFlashFrames) == 0; }

// 0x00449810: fade the screen from colour (a, b, c) over `frames` frames, unless a stronger fade (`level`) is already running.
void FileLoader::fade_start(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t frames, std::uint32_t level, std::uint32_t layer) {
    if (static_cast<std::uint8_t>(level) < global_byte(m_, kFadeLevel) || frames == 0) {
        return;
    }
    const std::uint32_t from_a = (a & 0xff) << 16;
    const std::uint32_t from_b = (b & 0xff) << 16;
    const std::uint32_t from_c = (c & 0xff) << 16;
    set_global_word(m_, kFadeA, from_a);
    set_global_word(m_, kFadeB, from_b);
    set_global_word(m_, kFadeFrames, frames);
    set_global_word(m_, kFadeC, from_c);
    set_global_byte(m_, kFadeLevel, level);
    set_global_word(m_, kFadeStepA, from_a / frames);
    set_global_word(m_, kFadeStepB, from_b / frames);
    set_global_word(m_, kFadeStepC, from_c / frames);
    set_global_word(m_, kFadeLayer, layer);
}

// 0x004498a0: a flash: the colour rises from black to white over `frames` frames.
void FileLoader::fade_start_flash(std::int32_t frames, std::uint32_t layer) {
    set_global_word(m_, kFadeC, 0);
    set_global_word(m_, kFadeB, 0);
    set_global_word(m_, kFadeA, 0);
    set_global_word(m_, kFadeFlash, 1);
    set_global_word(m_, kFadeFlashFrames, static_cast<std::uint32_t>(frames));
    set_global_byte(m_, kFadeLevel, 0xff);
    set_global_word(m_, kFadeStepA, static_cast<std::uint32_t>(0xff0000 / frames));
    set_global_word(m_, kFadeLayer, layer);
}

// 0x004498f0: one frame of the fade.
void FileLoader::fade_update() {
    if (global_word(m_, kFadeFlash) != 0) {
        if (global_word(m_, kFadeFlashFrames) == 0) {
            return;
        }
        const std::uint32_t left = global_word(m_, kFadeFlashFrames) - 1;
        set_global_word(m_, kFadeFlashFrames, left);
        if (left == 0) {
            set_global_word(m_, kFadeC, 0xff0000);
            set_global_word(m_, kFadeB, 0xff0000);
            set_global_word(m_, kFadeA, 0xff0000);
            return;
        }
        const std::uint32_t step = global_word(m_, kFadeStepA);
        set_global_word(m_, kFadeA, global_word(m_, kFadeA) + step);
        set_global_word(m_, kFadeB, global_word(m_, kFadeB) + step);
        set_global_word(m_, kFadeC, global_word(m_, kFadeC) + step);
        return;
    }
    if (global_word(m_, kFadeFrames) == 0) {
        return;
    }
    const std::uint32_t left = global_word(m_, kFadeFrames) - 1;
    set_global_word(m_, kFadeFrames, left);
    if (left == 0) {
        set_global_byte(m_, kFadeLevel, 0);
        return;
    }
    set_global_word(m_, kFadeA, global_word(m_, kFadeA) - global_word(m_, kFadeStepA));
    set_global_word(m_, kFadeB, global_word(m_, kFadeB) - global_word(m_, kFadeStepB));
    set_global_word(m_, kFadeC, global_word(m_, kFadeC) - global_word(m_, kFadeStepC));
}

// 0x004499b0: draw the fade as a translucent rectangle over the screen: two packets at the draw list's cursor, linked on the head of the fade's layer.
void FileLoader::fade_draw() {
    if (global_word(m_, kFadeFrames) == 0 && global_word(m_, kFadeFlash) == 0) {
        return;
    }
    const std::uint32_t at = global_word(m_, kArenaCursor);
    const std::uint32_t second = at + 0x1c;
    if (second + 0x10 > global_word(m_, kArenaEnd)) {
        return;
    }
    set_global_word(m_, kArenaCursor, second + 0x10);
    set_byte_at(m_, at + 7, 0x2a);
    set_byte_at(m_, at + 4, global_word(m_, kFadeA) >> 16);
    set_byte_at(m_, at + 5, global_word(m_, kFadeB) >> 16);
    set_byte_at(m_, at + 6, global_word(m_, kFadeC) >> 16);
    const std::uint32_t width = global_half(m_, kFadeWidth);
    const std::uint32_t height = global_half(m_, kFadeHeight);
    const std::uint32_t screen_w = global_word(m_, kScreenWidth);
    const std::uint32_t screen_h = global_word(m_, kScreenHeight);
    auto scale_x = [&](std::uint32_t value) { return static_cast<std::uint16_t>((static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(value))) * screen_w) >> 9); };
    auto scale_y = [&](std::uint32_t value) {
        const std::uint32_t product = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(value))) * screen_h;
        return static_cast<std::uint16_t>((static_cast<std::uint64_t>(0x88888889u) * product >> 32) >> 7);
    };
    set_half_at(m_, at + 0x08, 0);
    set_half_at(m_, at + 0x0a, 0);
    set_half_at(m_, at + 0x0c, scale_x(width));
    set_half_at(m_, at + 0x0e, 0);
    set_half_at(m_, at + 0x10, 0);
    set_half_at(m_, at + 0x12, scale_y(height));
    set_half_at(m_, at + 0x14, scale_x(width));
    set_half_at(m_, at + 0x16, scale_y(height));
    set_word_at(m_, at + 0x18, 0xbf800000);
    const std::uint32_t renderer = global_word(m_, kRenderer);
    const std::uint32_t layer = global_word(m_, kFadeLayer);
    std::uint32_t heads = word_at(m_, renderer + 0x84);
    set_word_at(m_, at, word_at(m_, heads + layer * 8));
    heads = word_at(m_, global_word(m_, kRenderer) + 0x84);
    set_word_at(m_, heads + layer * 8, at);
    set_word_at(m_, second, 0x1000000);
    set_word_at(m_, second + 4, global_word(m_, kFadeFlash) == 0 ? 0xe1000620 : 0xe1000640);
    set_word_at(m_, second + 0xc, 0);
    set_word_at(m_, second + 8, 0xbf800000);
    heads = word_at(m_, global_word(m_, kRenderer) + 0x84);
    set_word_at(m_, second, word_at(m_, heads + global_word(m_, kFadeLayer) * 8));
    heads = word_at(m_, global_word(m_, kRenderer) + 0x84);
    set_word_at(m_, heads + global_word(m_, kFadeLayer) * 8, second);
}

// ---- the font object -------------------------------------------------------------------------------------------------------

// 0x00449ca0
std::uint32_t FileLoader::font_construct(std::uint32_t self) {
    set_word_at(m_, self, kVtFont);
    set_word_at(m_, self + 0x3c, 0xffffffffu);
    return self;
}

// 0x00449cb0
std::uint32_t FileLoader::font_scalar_delete(std::uint32_t self, std::uint32_t flags) {
    font_base_destruct(self);
    if ((flags & 1) != 0) {
        service(kDelete, {self});
    }
    return self;
}

// 0x00449b40: a leftover test screen: show "Hello world" in the main font on a blue field until the exit flag is set; the up and down flags move the text.
void FileLoader::font_test_loop() {
    constexpr std::uint32_t kSetup = 0x004302c0, kCounter = 0x004e8960, kFontFind = 0x0044ada0, kFrameA = seam::hal::Game_FrameTick, kFrameB = 0x004f5ff0, kFps = 0x004dae00,
                            kFrameC = 0x004304b0, kResetPrimitives = 0x004b0950, kSetDrawArea = 0x004b0800, kFillRect = 0x004b0600, kPresent = 0x004e8740,
                            kEndA = 0x00430510, kEndB = 0x00489a10, kEndC = 0x00466c00, kFontUnload = 0x0044b0a0;
    std::uint32_t local = 0;
    service(kSetup, {0, 0, 1, 0});
    service(kCounter, {1});
    (void)service_value(kCounter);
    service(kCounter, {0xffffffffu});
    (void)service_value(kCounter);
    service(kCounter, {0xffffffffu});
    std::uint32_t last = service_value(kCounter);
    service(kFontFind, {kTestFontName});
    const std::uint32_t font = service_value(kFontFind);
    std::uint32_t position = 0x1000;
    set_global_byte(m_, kTestExit, 0);
    set_global_byte(m_, kTestUp, 0);
    set_global_byte(m_, kTestDown, 0);
    for (;;) {
        service(kFrameA);
        const std::uint32_t pad_up = service_value(kFrameA);
        const std::uint32_t pad_down = service_value(kFrameA);
        set_global_byte(m_, kTestUp, pad_up);
        set_global_byte(m_, kTestDown, pad_down);
        service(kFrameB);
        service(kFps);
        service(kFrameC);
        service(kResetPrimitives);
        service(kSetDrawArea, {0, local, 0x200, 0xf0});
        service(kFillRect, {0, 0, 0x200, 0xf0, 0, 0, 0xff});
        local = 0x100 - local;
        if (global_byte(m_, kTestUp) != 0) {
            set_global_byte(m_, kTestUp, 0);
            position -= 0x10;
        }
        if (global_byte(m_, kTestDown) != 0) {
            set_global_byte(m_, kTestDown, 0);
            position += 0x10;
        }
        const std::uint32_t vtable = word_at(m_, font);
        set_word_at(m_, font + 4, 1);
        set_word_at(m_, font + 8, 1);
        set_byte_at(m_, font + 0x20, 0);
        set_word_at(m_, font + 0x30, position);
        set_word_at(m_, font + 0x14, 0xb6);
        set_word_at(m_, font + 0x18, 0x84);
        set_word_at(m_, font + 0x1c, 0x24);
        service(word_at(m_, vtable + 4), {font, 0x100, 0x78, kTestText, 0});
        service(kPresent, {0});
        service(kCounter, {0xffffffffu});
        std::uint32_t counter = service_value(kCounter);
        last += 2;
        while (static_cast<std::int32_t>(counter) < static_cast<std::int32_t>(last)) {
            service(kCounter, {0xffffffffu});
            counter = service_value(kCounter);
        }
        last = counter;
        service(kEndA);
        service(kEndB);
        service(kEndC);
        set_global_byte(m_, kTestExit, service_value(kEndC));
        if (global_byte(m_, kTestExit) != 0) {
            break;
        }
    }
    service(kFontUnload, {kTestFontName});
}

}  // namespace opentony::runtime::fileio
