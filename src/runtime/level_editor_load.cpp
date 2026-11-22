#include "auto_views.hpp"
#include "level_editor.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// 0x004331d0: the editor's table of commands (the slots of a vtable-like record: the first empty function, then the run, discard, unload and save functions)
void LevelEditor::init_commands(std::uint32_t table) {
    set_word_at(m_, table, 0x00433200);
    set_word_at(m_, table + 0xc, 0x00433210);
    set_word_at(m_, table + 0x10, 0x00433220);
    set_word_at(m_, table + 0x14, 0x00433250);
    set_word_at(m_, table + 0x18, 0x00433260);
}

// 0x00433200
void LevelEditor::command_nothing() {}

// 0x00433210: a jump to the editor loop
void LevelEditor::command_run() { run(); }

// 0x00433220: make the editor's park, then throw it away (the generator object and its block).
void LevelEditor::discard_park() {
    // (retail reads the generator object back from the local the load stored it in; here the scratch window is shared with the loading screen's fonts, which the frames the
    // load runs redraw, so the object comes back as the return value)
    const std::uint32_t park = load_buffer(kScratchOut, 1);
    if (park != 0) {
        engine(0x00439900, {park});         // LevelGen_Destruct
        engine(0x004436d0, {park});         // GenObjs_Delete
    }
}

// 0x00433250
void LevelEditor::unload_psx() { engine(0x0043bf80, {0}); }

// 0x00433260: the park is written into the buffer the argument points at, and the size it took goes into the buffer's second word.
void LevelEditor::save_park(std::uint32_t buffer) { set_word_at(m_, buffer + 4, engine_value(0x0043c480, {buffer})); }

// 0x00433280: build a generator object for the editor's park.  The object is the first state: the statics are set up (state 1), or the saved buffer is parsed into them
// (state 3), then the variant is chosen, the region built and, when asked, the nodes of the rails.
std::uint32_t LevelEditor::load_buffer(std::uint32_t out, std::uint32_t build_nodes) {
    const std::uint32_t block = engine_value(0x004436a0, {0x3748});
    const std::uint32_t park = block != 0 ? engine_value(0x004397d0, {block, build_nodes}) : 0;
    set_word_at(m_, out, park);
    const std::uint32_t state = global_word(m_, kMapState);
    if (state == 3) {
        set_global_word(m_, kMapState, 2);
        const std::uint32_t buffer = alloc(0x1e00, 0, 1, 0);
        mem_copy(buffer, word_at(m_, global_word(m_, kRowsSource)), 0x1e00);
        engine(0x004399a0, {0});
        engine(0x0043c050, {buffer, kScratchParse});
        // the parser leaves the two words of the park (the variant and the version) in the caller's locals
        if (replay()) {
            set_global_word(m_, kScratchParse, static_cast<std::uint32_t>(s_.value("engine_0043c050")));
            set_global_word(m_, kScratchParse + 4, static_cast<std::uint32_t>(s_.value("engine_0043c050")));
        }
        free_block(buffer);
        const std::uint32_t first = global_word(m_, kScratchParse);
        const std::uint32_t second = global_word(m_, kScratchParse + 4);
        set_global_word(m_, kTheme, 0xffffffff);
        set_global_word(m_, kVersion, 0xffffffff);
        engine(0x0043a280, {park, second, first, 0});
    } else {
        if (state == 1) {
            engine(0x004399a0, {1});
            set_global_word(m_, kMapState, 2);
        }
        const std::uint32_t theme = global_word(m_, kTheme);
        const std::uint32_t version = global_word(m_, kVersion);
        set_global_word(m_, kTheme, 0xffffffff);
        set_global_word(m_, kVersion, 0xffffffff);
        engine(0x0043a280, {park, theme, version, 0});
    }
    engine(0x0043d400, {park, kPlaceHolder, 1});
    if ((build_nodes & 0xff) != 0) {
        engine(0x0043e980, {park});
    }
    return park;
}

// 0x004333c0: the editor: its object is made, then one frame after another until the editor says it is done or the quit combination is held.
void LevelEditor::run() {
    const std::uint32_t block = engine_value(0x004436a0, {0x1dc});
    const std::uint32_t editor = block != 0 ? engine_value(0x004334d0, {block}) : 0;
    engine(0x0045db40);
    set_global_byte(m_, kFadeFlag, 0);
    engine(0x004c8f30);
    engine(0x004e3f80, {2});
    engine(0x004dbd90);
    for (;;) {
        engine(seam::hal::Game_FrameTick);
        if (!dispatch_) {
            set_global_word(m_, kSimTick, static_cast<std::uint32_t>(s_.value("engine_004f7ce0")));       // Game_FrameTick: the engine moves the clock on
        }
        engine(0x004f5ff0);
        engine(0x004dae00);
        engine(0x004304b0);
        engine(0x00466be0);
        engine(0x00489a10);
        engine(0x00418150);
        if ((engine_value(0x00433cb0, {editor}) & 0xff) != 0) {
            break;
        }
        engine(0x00433a40, {editor});
        engine(0x0045f3f0);
        set_global_word(m_, kPolygonFlag, 0);
        engine(0x004e8740, {0});
        engine(0x004c9300, {1});
        set_global_word(m_, kLastTick, global_word(m_, kSimTick));
        engine(0x00430510);
        engine(0x00466c00);
        if ((engine_value(0x00452d30) & 0xff) != 0) {
            break;
        }
    }
    if (editor != 0) {
        engine(0x004338e0, {editor});
        engine(0x004436d0, {editor});
    }
    engine(0x004dbe60);
    engine(0x004e3f80, {1});
}

}  // namespace opentony::runtime::leveleditor
