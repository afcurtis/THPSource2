#include "auto_views.hpp"
#include "host_options.hpp"
#include "render_kit.hpp"
#include "seam_addresses_generated.hpp"
// Phase 4.15e: the double-buffered display environments of the renderer (`db.cpp`, 0x0042fc60..0x004305e0). Two 0xb4-byte records (the draw environment at +0 .. +0x5c and the display
// environment at +0x5c, with the ordering table pointers at +0x84 and +0xb0) alternate every frame; the PSX library calls that apply them (PutDrawEnv, PutDispEnv, ClearOTag, DrawSync,
// VSync) are services. Each assertion writes its line and the file name into the assertion globals (0x00568638, 0x0056881c), which the replay does not compare.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kOtSize = 0x00561154;                // dword: the ordering table length (2 + 4096 * players, plus 200 in the two-player mode)
constexpr Word kOtBytes = 0x00561150;               // dword: the bytes the table and primitive area take
constexpr Word kSkyColour = 0x0056114c;             // dword: the clear colour (-1: none)
constexpr Word kBufferA = 0x00560fe0;               // the first buffer record
constexpr Word kBufferB = 0x00561094;               // the second (0xb4 further)
constexpr Word kCurrent = 0x00560fd4;               // the buffer being drawn
constexpr Word kPrevious = 0x00560fd8;              // the buffer being displayed
constexpr Word kPrimitivePointer = 0x00560fd0;      // dword: where the next primitive goes
constexpr Word kPrimitiveLast = 0x00560fc8;
constexpr Word kPrimitiveArea = 0x00561090;         // dword: the area allocated for the primitives
constexpr Word kTableA = 0x00561064;                // dword: the first ordering table's allocation
constexpr Word kTableB = 0x00561118;
constexpr Word kBufferStride = 0xb4;
constexpr Word kBuffersEnd = 0x005611c0;
constexpr Word kClearColourWord = 0x00563a60;       // the background colour
constexpr Word kScreenWidth = 0x00527d50;
constexpr Word kScreenHeight = 0x00527d54;

// the engine functions the display reaches
constexpr Word kDrawSync = 0x004e8740;              // (mode)
constexpr Word kPutDispEnv = 0x004e86f0;
constexpr Word kPutDrawEnv = 0x004e8700;
constexpr Word kSetDefDrawEnv = 0x004e8720;
constexpr Word kSetDefDispEnv = 0x004e8710;
constexpr Word kResetGraph = 0x004e8a00;
constexpr Word kSetDispMask = 0x004e8850;
constexpr Word kClearImage = 0x004e8840;
constexpr Word kInstallCallback = 0x004e8790;
constexpr Word kSetSky = 0x004d0bc0;
constexpr Word kPresent = seam::hal::D3D_PresentFrame;
constexpr Word kClearOrderingTable = 0x004d3b00;
constexpr Word kDrawPolygonList = 0x004d3160;
constexpr Word kFinishBuffer = 0x004f39e0;
constexpr Word kSetDrawOffset = 0x004e87f0;
constexpr Word kSyncPrimitives = 0x00466be0;
constexpr Word kMarkDisplayed = 0x004e87a0;
constexpr Word kTrimBuffer = 0x00464690;
constexpr Word kEndFrame = 0x004e89f0;
constexpr Word kSetVideoMode = 0x004c9300;
constexpr Word kFlipBuffers = 0x004dbea0;
constexpr Word kPrimitivesUsed = 0x00561148;
constexpr Word kSetFrameCount = 0x00561070;

}  // namespace

// the clear colour of the frame with red and blue swapped (the display wants 0xaabbggrr)
Word RenderKit::display_colour() {
    const Word word = global_word(m_, kClearColourWord);
    return global_byte(m_, kClearColourWord + 2) | ((word & 0xff) << 16) | (word & 0xff00ff00);
}

// 0x0042fc60 Db_OTSize
Word RenderKit::db_ot_size() { return global_word(m_, kOtSize); }

// 0x0042fc70 Db_UpdateSky: the sky colour (red/green/blue bytes of the word) goes to both display records
void RenderKit::db_update_sky() {
    const Word sky = global_word(m_, kSkyColour);
    if (sky == 0xffffffff) {
        set_global_byte(m_, 0x00560ff8, 0);
        set_global_byte(m_, 0x005610ac, 0);
        return;
    }
    set_global_byte(m_, 0x00560ff8, 1);
    set_global_byte(m_, 0x00560ff9, sky >> 16);
    set_global_byte(m_, 0x00560ffa, sky >> 8);
    set_global_byte(m_, 0x00560ffb, sky);
    set_global_byte(m_, 0x005610ac, 1);
    set_global_byte(m_, 0x005610ad, sky >> 16);
    set_global_byte(m_, 0x005610ae, sky >> 8);
    set_global_byte(m_, 0x005610af, sky);
}

// 0x0042fcd0 Db_CallBack: the vertical blank callback: when a buffer is waiting to be shown and the GPU is idle, mark it displayed
void RenderKit::db_callback() {
    if (global_word(m_, 0x00561058) == 0 && global_word(m_, 0x0056110c) == 0) {
        return;
    }
    if (engine_value(kDrawSync, {1}) != 0) {
        return;
    }
    Word at = 0x00561058;
    do {
        if (word_at(m_, at) != 0) {
            set_word_at(m_, at, 0);
            set_word_at(m_, at + 4, 1);
        }
        at += kBufferStride;
    } while (static_cast<std::int32_t>(at) < static_cast<std::int32_t>(kBuffersEnd));
}

// 0x0042fd20 Db_Vblank: applies the display environment of a finished buffer and starts the GPU on the buffer that is ready
void RenderKit::db_vblank() {
    check(global_word(m_, 0x0056105c) == 0 || global_word(m_, 0x00561110) == 0, 0x00527da0);
    Word at = 0x0056105c;
    Word offset = 0;
    do {
        if (word_at(m_, at) != 0) {
            engine(0x004e86f0, {offset + 0x005610f0});
            set_word_at(m_, at, 0);
            set_word_at(m_, at - 0xc, 1);
            set_word_at(m_, offset + 0x00561104, 0);
            set_word_at(m_, at + 4, global_word(m_, 0x0056e31c));
        }
        offset -= kBufferStride;
        at += kBufferStride;
    } while (static_cast<std::int32_t>(offset) > -0x168);
    check(global_word(m_, 0x00561054) == 0 || global_word(m_, 0x00561108) == 0, 0x00527d80);
    Word buffer = 0x00561058;
    Word other = 0;
    do {
        if (word_at(m_, buffer - 4) != 0 && word_at(m_, buffer) == 0 && word_at(m_, other + 0x0056110c) == 0 && word_at(m_, buffer - 8) == 0) {
            check(true, 0x00527d58);
            set_word_at(m_, buffer - 4, 0);
            set_word_at(m_, buffer, 1);
            engine(kPutDrawEnv, {buffer - 0x78});
            const Word size = db_ot_size();
            engine(kDrawPolygonList, {word_at(m_, buffer + 0xc) + size * 8 - 8});
        }
        other -= kBufferStride;
        buffer += kBufferStride;
    } while (static_cast<std::int32_t>(other) > -0x168);
}

// 0x0042fed0 Db_WaitRender: waits for the drawn buffer, then clears its table and swaps the primitive pointer
void RenderKit::db_wait_render() {
    Word current = global_word(m_, kCurrent);
    set_global_word(m_, 0x00560fc8, current);
    if (word_at(m_, current + 0x78) != 0) {
        (void)engine_value(kDrawSync, {0});
        db_callback();
        current = global_word(m_, kCurrent);
    }
    check(word_at(m_, current + 0x78) == 0, 0x00527dec);
    check(word_at(m_, global_word(m_, kCurrent) + 0x74) == 0, 0x00527dd4);
    const Word size = db_ot_size();
    engine(kClearOrderingTable, {word_at(m_, global_word(m_, kCurrent) + 0x84), size});
    set_global_word(m_, kPrimitivePointer, word_at(m_, global_word(m_, kCurrent) + 0xb0));
    engine(kSyncPrimitives);
}

// 0x0042ffc0 Db_Render: closes the frame's primitives, links the sky and queues the buffer for the next vertical blank
void RenderKit::db_render() {
    engine(kSetSky, {display_colour(), global_word(m_, kSkyColour)});
    if (word_at(m_, global_word(m_, kPrevious) + 0x74) != 0) {
        engine(kSetVideoMode, {1});
    }
    check(word_at(m_, global_word(m_, kPrevious) + 0x74) == 0, 0x00527e4c);
    check(word_at(m_, global_word(m_, kCurrent) + 0x74) == 0, 0x00527e2c);
    check(word_at(m_, global_word(m_, kCurrent) + 0x78) == 0, 0x00527e0c);
    Word current = global_word(m_, kCurrent);
    engine(kSetDrawOffset, {current + 0x9c, current + 0x94});
    current = global_word(m_, kCurrent);
    set_word_at(m_, current + 0xac, 0xbf800000);
    current = global_word(m_, kCurrent);
    set_word_at(m_, current + 0x9c, word_at(m_, word_at(m_, current + 0x84) + 8));
    current = global_word(m_, kCurrent);
    set_word_at(m_, word_at(m_, current + 0x84) + 8, current + 0x9c);
    const Word size = db_ot_size();
    engine(kFinishBuffer, {word_at(m_, global_word(m_, kCurrent) + 0x84), size});
    set_word_at(m_, global_word(m_, kCurrent) + 0x74, 1);
    if (global_word(m_, kCurrent) == kBufferA) {
        set_global_word(m_, kCurrent, kBufferB);
        set_global_word(m_, kPrevious, kBufferA);
    } else {
        set_global_word(m_, kCurrent, kBufferA);
        set_global_word(m_, kPrevious, kBufferB);
    }
    db_vblank();
    db_callback();
    engine(kPresent);
}

// 0x004301a0
void RenderKit::db_install_callback() { engine(kInstallCallback, {0x0042fcd0}); }

// 0x004301b0 Db_Reset: releases the primitive area and the two ordering tables
void RenderKit::db_reset() {
    (void)engine_value(kDrawSync, {0});
    const Word area = global_word(m_, kPrimitiveArea);
    if (area == 0) {
        return;
    }
    HostOptions& host = host_options();
    if (host.poly_buffer_area_scale > 1) {
        s_.note("crt_delete", {area});   // the enlarged area (db_init) is the C runtime's, not the game's heap's
        host.poly_buffer_area_scale = 0;
    } else {
        mem_free(area);
    }
    mem_free(global_word(m_, kTableA));
    mem_free(global_word(m_, kTableB));
    set_global_word(m_, kPrimitiveArea, 0);
}

// 0x004301f0 Db_Init: sizes the ordering table and primitive area from the level's player count and allocates them (the flag, the third argument's low byte, widens the table by 200 entries)
// The size of one buffer is the level record's ExtraPolyBuffer for the player count (the level record is 107 words; the level is 0x0056a898) plus 0x145000.
// HostOptions::poly_buffer_scale multiplies it and takes the area from the C runtime's heap instead of the game's; at 1 (every gate and scenario) this is retail.
// Several callers run Db_Init again without Db_Reset and keep the area: the size written is then the held area's scale, so it never outgrows the block.
void RenderKit::db_init(Word, Word, Word flag) {
    const Word level = global_word(m_, 0x0056a898);
    const Word index = level * 107 + global_word(m_, 0x00533f3c);
    Word bytes = word_at(m_, 0x0053c27c + 4 * index);
    const Word count = global_word(m_, 0x00533f3c);
    set_global_word(m_, kOtSize, (count << 12) + 2);
    bytes += 0x145000;
    HostOptions& host = host_options();
    const bool held = global_word(m_, kPrimitiveArea) != 0;
    const int scale = held ? host.poly_buffer_area_scale : host.poly_buffer_scale;
    if (scale > 1) {
        bytes *= static_cast<Word>(scale);
    }
    set_global_word(m_, kOtBytes, bytes);
    if ((flag & 0xff) != 0) {
        set_global_word(m_, kOtSize, global_word(m_, kOtSize) + 0xc8);
    }
    if (held) {
        return;
    }
    Word area = 0;
    if (scale > 1) {
        s_.note("crt_new", {bytes * 2 + kPolyBufferPad});
        area = static_cast<Word>(s_.value("crt_new"));
        host.poly_buffer_area_scale = scale;
    } else {
        area = alloc(bytes * 2);
    }
    set_global_word(m_, kPrimitiveArea, area);
    set_global_word(m_, 0x00561144, area + global_word(m_, kOtBytes));
    Word table = alloc(db_ot_size() * 8 + 2);
    set_global_word(m_, kTableA, table);
    set_global_word(m_, 0x00561068, table + 0x10);
    table = alloc(db_ot_size() * 8 + 2);
    set_global_word(m_, kTableB, table);
    set_global_word(m_, 0x0056111c, table + 0x10);
}

// 0x004302c0 HiRes_ClearOTag: sets the graphics up for the hi-res mode: both buffers' environments, the sky, the ordering tables, and (when the flag is set and the screen is not already showing) the clear of the video memory
void RenderKit::hires_clear_otag(Word arg0, Word arg1, Word arg2, Word arg3) {
    const bool clear = (arg2 & 0xff) != 0;
    engine(kResetGraph);
    if (clear) {
        engine(kSetDispMask, {0});
    }
    (void)engine_value(kDrawSync, {0});
    set_global_word(m_, kSkyColour, 0);
    db_init(arg0, arg1, arg3);
    const Word width = global_word(m_, kScreenWidth);
    const Word height = global_word(m_, kScreenHeight);
    set_global_word(m_, kSetFrameCount, 0);
    set_global_word(m_, 0x00561124, 0x100);
    engine(kSetDefDrawEnv, {kBufferA, 0, 0, width, height});
    engine(kSetDefDrawEnv, {kBufferB, 0, 0x100, width, height});
    engine(kSetDefDispEnv, {0x0056103c, 0, 0x100, width, height});
    engine(kSetDefDispEnv, {0x005610f0, 0, 0, width, height});
    set_global_half(m_, 0x005610f8, 0);
    set_global_half(m_, 0x00561044, 0);
    set_global_half(m_, 0x005610fa, 0);
    set_global_half(m_, 0x00561046, 0);
    db_update_sky();
    engine(kPutDrawEnv, {kBufferA});
    engine(kPutDispEnv, {0x0056103c});
    Word at = 0x00561058;
    do {
        set_word_at(m_, at - 8, 0);
        set_word_at(m_, at, 0);
        set_word_at(m_, at + 4, 0);
        set_word_at(m_, at - 4, 0);
        set_half_at(m_, at + 0x1c, 0);
        set_half_at(m_, at + 0x1e, half_at(m_, at + 0x18));
        set_half_at(m_, at + 0x20, 0x200);
        set_half_at(m_, at + 0x22, global_half(m_, kScreenHeight));
        const Word size = db_ot_size();
        engine(kClearOrderingTable, {word_at(m_, at + 0xc), size});
        at += kBufferStride;
    } while (static_cast<std::int32_t>(at) < static_cast<std::int32_t>(kBuffersEnd));
    set_global_word(m_, kCurrent, kBufferB);
    set_global_word(m_, kPrevious, kBufferA);
    if (global_word(m_, 0x0056a89c) == 0 && clear) {
        // the video memory is cleared with two rectangles built in a local (width of the screen by 0x1ff and by 1)
        const Word rect = kScratch;
        set_half_at(m_, rect, 0);
        set_half_at(m_, rect + 2, 0);
        set_half_at(m_, rect + 4, global_half(m_, kScreenWidth));
        set_half_at(m_, rect + 6, 0x1ff);
        engine(kClearImage, {rect, 0, 0, 0}, bytes_text(rect, 8));
        (void)engine_value(kDrawSync, {0});
        set_half_at(m_, rect + 2, 0x1ff);
        set_half_at(m_, rect + 6, 1);
        engine(kClearImage, {rect, 0, 0, 0}, bytes_text(rect, 8));
        (void)engine_value(kDrawSync, {0});
    }
    if (clear) {
        engine(kSetDispMask, {1});
    }
    engine(kTrimBuffer, {global_word(m_, kScreenWidth), global_word(m_, kScreenHeight)});
    engine(kEndFrame);
}

// 0x004304b0 Db_FlipClear: swaps the buffers and clears the new current buffer's table
void RenderKit::db_flip_clear() {
    const Word old = global_word(m_, kCurrent);
    set_global_word(m_, kCurrent, old == kBufferA ? kBufferB : kBufferA);
    set_global_word(m_, kPrevious, old);
    const Word size = db_ot_size();
    engine(kClearOrderingTable, {word_at(m_, global_word(m_, kCurrent) + 0x84), size});
    set_global_word(m_, kPrimitivePointer, word_at(m_, global_word(m_, kCurrent) + 0xb0));
    engine(kSyncPrimitives);
    engine(kFlipBuffers);
}

// 0x00430510 Db_DisplayDraw: shows the drawn buffer immediately (the loading screens): sky, display and draw environment, the table, the list
void RenderKit::db_display_draw() {
    engine(kSetSky, {display_colour(), global_word(m_, kSkyColour)});
    engine(kMarkDisplayed, {1});
    engine(kPutDispEnv, {global_word(m_, kCurrent) + 0x5c});
    engine(kPutDrawEnv, {global_word(m_, kCurrent)});
    const Word size = db_ot_size();
    engine(kFinishBuffer, {word_at(m_, global_word(m_, kCurrent) + 0x84), size});
    if (global_word(m_, kPrimitivesUsed) != 0) {
        (void)engine_value(kDrawSync, {0});
    }
    const Word last = db_ot_size();
    engine(kDrawPolygonList, {word_at(m_, global_word(m_, kCurrent) + 0x84) + last * 8 - 8});
    engine(kPresent);
}

// 0x004305b0: swaps the buffers and applies the new current buffer's display environment
void RenderKit::db_flip() {
    const Word old = global_word(m_, kCurrent);
    set_global_word(m_, kPrevious, old);
    const Word next = old == kBufferA ? kBufferB : kBufferA;
    set_global_word(m_, kCurrent, next);
    engine(kPutDispEnv, {next + 0x5c});
}

}  // namespace opentony::runtime::renderkit
