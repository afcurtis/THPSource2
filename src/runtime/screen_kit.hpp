#pragma once

#include "frontend_text.hpp"
#include "guest_view.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10e1 / 4.10e2: what the screens of the front end share (the create-a-skater screens, the trick screen): the retail globals they read, the names of the
// services they report, and the helpers every screen is built from. A screen class derives from ScreenKit and gets the window base, the menu toolkit,
// the font code and the skater manager as members (m_, s_, f_, w_, menu_, sk_), plus: reporting an engine call by its address, allocating, loading fonts,
// the prompt windows and the background picture, a virtual call on a toolkit object, and the frame loop the screens run.
//
// Service names beyond menu_toolkit.hpp and frontend_window.hpp
constexpr const char* kSvcCrtNew = "crt_new";               // 0x005002fe (size) -> block (scripted)
constexpr const char* kSvcFileOpen = "file_open";           // 0x00448dd0 (name, 1) -> handle (scripted); the name is the text
constexpr const char* kSvcFontLoad = "font_load";           // 0x0044ada0 (name) -> font (scripted), the name is the text
constexpr const char* kSvcFontUnload = "font_unload";       // 0x0044b0a0 (name), the name is the text
constexpr const char* kSvcSprintf = "sprintf";              // 0x005002a1 (dest, format...): the text a name is copied into (scripted below)
constexpr const char* kSvcAssertFile = "assert";

// Retail globals
constexpr std::uint32_t kPadCurrent = 0x0056a888;           // the active player (gActivePlayer): also the pad the editor listens to
constexpr std::uint32_t kGlobalDatabase = 0x0056a87c;       // the current texture-set database (0 outside the editor)
constexpr std::uint32_t kGlobalCachedDatabase = 0x00561cb4;
constexpr std::uint32_t kGlobalSimTick = 0x0056e31c;        // SimulationPublicTick
constexpr std::uint32_t kGlobalLastTick = 0x00568624;
constexpr std::uint32_t kGlobalFrontFlag = 0x0056114c;
constexpr std::uint32_t kGlobalRenderer = 0x00560fd4;       // the renderer object (+0x88: its viewport)
constexpr std::uint32_t kGlobalScreenWidth = 0x029da394;
constexpr std::uint32_t kGlobalScreenHeight = 0x029da398;
constexpr std::uint32_t kGlobalCustomSkater = 0x00568648;   // the custom skater object the stat editor owns
constexpr std::uint32_t kGlobalEditorMode = 0x0056a878;     // the editor's mode argument
constexpr std::uint32_t kPadSpinLeft = 0x0056b008;          // pad 0's two spin buttons (+0x16c per pad)
constexpr std::uint32_t kPadSpinRight = 0x0056b018;
constexpr std::uint32_t kPadUp = 0x0056b099;
constexpr std::uint32_t kPadUp2 = 0x0056b0a9;
constexpr std::uint32_t kPadConfirmEdge = 0x0056b029;
constexpr std::uint32_t kPadBackEdge = 0x0056aff9;

// sprintf for the formats the screens use: %s takes the next string, %d the next number (%6d pads it with spaces to six places), %% is a percent sign.
std::string retail_format(const std::string& format, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers);

// The scratch region the screens' stack locals (a formatted line, a name) live in during a replay (outside every region a replay compares).
constexpr std::uint32_t kScratchLine = 0x7ff00400;

class ScreenKit {
public:
    ScreenKit(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : m_(memory), s_(services), f_(fonts), w_(window), menu_(menu), sk_(skaters) {}

    // A screen loop calls its window's step, update, draw and deleting destructor through the window's vtable and so reaches the children's real
    // methods. With the seams on, the loops report those four calls as services instead (the replay of a retail capture does this: the methods are
    // validated on their own, the loop around them here).
    void set_loop_seams(bool on) { seams_ = on; }

protected:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        s_.note(engine_name(address), std::move(args), std::move(text));
    }
    std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        engine(address, std::move(args), std::move(text));
        return static_cast<std::uint32_t>(s_.value(engine_name(address)));
    }
    // PlayerSpool_LoadPartManifest(file, names, 0, &count, 0x20): the .psh file's part names (pointers into its text, written to names_at), their number (written to
    // count_at) and the block that holds the text, which the caller frees. The probe's stub reports the file alone and answers the count and then the block; live the
    // call is made in full, with the arrays where the caller keeps them.
    struct PartManifest {
        std::uint32_t count;
        std::uint32_t block;
    };
    PartManifest load_part_manifest(std::uint32_t file, std::uint32_t names_at, std::uint32_t count_at) {
        constexpr std::uint32_t kLoadPartManifest = 0x004b47a0;
        if (s_.live()) {
            const std::uint32_t block = engine_value(kLoadPartManifest, {file, names_at, 0, count_at, 0x20}, m_.cstring(file, 0x40));
            return {guest::read_scalar<std::uint32_t>(m_, count_at), block};
        }
        engine(kLoadPartManifest, {file}, m_.cstring(file, 0x40));
        const auto count = static_cast<std::uint32_t>(s_.value(engine_name(kLoadPartManifest)));
        return {count, static_cast<std::uint32_t>(s_.value(engine_name(kLoadPartManifest)))};
    }
    // A retail function that takes a string: the probe's stub reports the text as the call's text and no argument for it; live the function takes a pointer to the text
    // (the caller's own when it has one in memory already, else the text is written to a scratch line of pc_platform's window first).
    void engine_named(std::uint32_t address, std::vector<std::uint32_t> args, const std::string& text, std::uint32_t pointer = 0) {
        if (s_.live()) {
            if (pointer == 0) {
                constexpr std::uint32_t kLines = 0x7ff0b000;
                constexpr std::uint32_t kLineBytes = 0x80;
                pointer = kLines + kLineBytes * (named_line_++ & 15);
                std::vector<std::uint8_t> bytes(text.begin(), text.end());
                bytes.push_back(0);
                m_.write(pointer, bytes);
            }
            args.push_back(pointer);
        }
        engine(address, std::move(args), text);
    }
    std::uint32_t operator_new(std::uint32_t size) {
        s_.note(kOperatorNew, {size});
        return static_cast<std::uint32_t>(s_.value(kOperatorNew));
    }
    void operator_delete(std::uint32_t block) { s_.note(kOperatorDelete, {block}); }
    std::uint32_t crt_new(std::uint32_t size) {
        s_.note(kSvcCrtNew, {size});
        return static_cast<std::uint32_t>(s_.value(kSvcCrtNew));
    }
    void sfx(std::uint32_t sound) { s_.note(kSvcSfx, {sound, 0x2000, 0}); }
    void font_construct(std::uint32_t font) { s_.note(kSvcFontConstruct, {font}); }
    void font_destroy(std::uint32_t font) { s_.note(kSvcFontDestroy, {font}); }
    std::uint32_t find_font(std::uint32_t name) {
        s_.note(kSvcFontFind, {name}, m_.cstring(name, 0x40));
        return static_cast<std::uint32_t>(s_.value(kSvcFontFind));
    }
    // Font_Find then the copy every window constructor makes into its own Font object
    void copy_font_named(std::uint32_t dest, std::uint32_t name) { menu_.copy_font_fields(dest, find_font(name)); }
    void font_load(std::uint32_t name) {
        s_.note(kSvcFontLoad, {name}, m_.cstring(name, 0x40));
        (void)s_.value(kSvcFontLoad);
    }
    void font_unload(std::uint32_t name) { s_.note(kSvcFontUnload, {name}, m_.cstring(name, 0x40)); }
    // A prompt window (a button picture with its caption), built by the engine and added to the window by the caller.
    std::uint32_t prompt(std::uint32_t file, std::uint32_t caption, std::uint32_t font, std::int32_t x, std::int32_t y);
    // The background sprite (s2bgd4.bmp): crt_new, the file, the sprite constructor.
    std::uint32_t background_sprite(std::uint32_t name = 0x00525464);
    void vcall_list(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}) {
        menu_.vcall_list(name, object, slot, std::move(args));
    }
    void vcall_item(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}) {
        menu_.vcall_item(name, object, slot, std::move(args));
    }
    // A virtual delete (flag 1) on an object this port or the toolkit owns, or a service for any other.
    void delete_object(std::uint32_t object) { vcall_item("virtual_delete", object, 0, {1}); }
    // The screen loop all the screens share (Game_FrameTick ... PollHost), reporting each engine call; `step` answers 0, 1 or 2 per frame.
    std::int32_t frame_loop(const std::function<std::int32_t()>& step, const std::function<void()>& update, const std::function<void()>& draw);
    // Game_FrameTick: the engine advances the simulation clock; the new value comes from the script.
    void game_frame_tick();
    // The text a real sprintf leaves in its destination (the formatted line and the terminating zero)
    void write_string(std::uint32_t dest, const std::string& text);
    // A formatted line in the screens' stack buffer (kScratchLine, 0x100 bytes): returns its address
    std::uint32_t scratch_line(const std::string& text);
    // A second (or third) local buffer in the same scratch region, `offset` bytes after the first
    std::uint32_t scratch_text(std::uint32_t offset, const std::string& text);
    // The full-screen camera view (four 16-bit words of width, height, x, y, then the near and far planes and the field of view)
    void init_view(std::uint32_t view);
    void draw_background(std::uint32_t sprite);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    MenuToolkit& menu_;
    skater::SkaterManager& sk_;
    bool seams_ = false;
    std::uint32_t named_line_ = 0;
};

}  // namespace opentony::runtime::frontend
