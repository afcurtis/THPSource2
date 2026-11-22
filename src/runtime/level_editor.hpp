#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime {

// Phase 4.11d: the park editor's state and piece operations on retail addresses: editorglobal.cpp (0x004326a0..0x00432fff: the park menu and the load/save state
// machine, the editor's maps) and the small-method half of leveleditor.cpp (0x00433090..0x00433fff and 0x00436520..0x004395a0: the editor loop, the HUD, the piece
// selection, the camera and the cursor with the placing, removing and rotating of pieces).
//
// Everything outside these functions is reached through `engine()`: the park generator (4.11c), the generator objects (4.11b), the menu toolkit (4.10c), the fonts, the
// file layer and the renderer.  A service is reported by name and its scripted result returned (the replay of a retail capture); a host that sets a dispatcher
// (`set_dispatch`) gets the address and the arguments instead and runs the real routine.  `this` of a thiscall comes first in the arguments.
namespace leveleditor {

// ---- statics -------------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kMapState = 0x005614bc;            // 0 none, 1 allocated, 2 loaded, 3 loaded from the buffer
constexpr std::uint32_t kFlashOn = 0x005614c4;             // byte
constexpr std::uint32_t kShadeCache = 0x00561170;          // byte: the shade the base objects have now
constexpr std::uint32_t kObjectCount = 0x005614c0;
constexpr std::uint32_t kSlotRow = 0x005611a4;
constexpr std::uint32_t kTheme = 0x005614c8;
constexpr std::uint32_t kVersion = 0x005283a0;
constexpr std::uint32_t kRowsSource = 0x005614cc;
constexpr std::uint32_t kItemsSource = 0x005614d0;
constexpr std::uint32_t kRows = 0x005614d4;
constexpr std::uint32_t kItems = 0x005614d8;
constexpr std::uint32_t kGridW = 0x00528398;
constexpr std::uint32_t kGridD = 0x0052839c;
constexpr std::uint32_t kCellSize = 0x00561488;            // the cell's width and its depth (kCellDepth)
constexpr std::uint32_t kCellDepth = 0x0056148c;
constexpr std::uint32_t kParkName = 0x0053d7b4;            // "Custom park name goes here": the name buffer
constexpr std::uint32_t kParkIsNew = 0x00561450;           // byte
constexpr std::uint32_t kParkNamePointer = 0x0052eb20;     // the string the empty park is called
constexpr std::uint32_t kNameFormat = 0x005256dc;          // "%s"
constexpr std::uint32_t kNameDirty = 0x0056acfb;           // byte
constexpr std::uint32_t kActivePlayer = 0x0056a888;
constexpr std::uint32_t kMmuManager = 0x0056ad00;
constexpr std::uint32_t kObjectTables = 0x0056d438;        // per region slot (0x44 bytes): +0 the object records, +8 the raw region
constexpr std::uint32_t kPlaceHolder = 0x005613bc;         // (the generator's last region slot)
constexpr std::uint32_t kLineCursor = 0x00560fd0;          // the polygon arena: the next free byte and the end of the arena
constexpr std::uint32_t kLineEnd = 0x00568628;
constexpr std::uint32_t kRenderer = 0x00560fd4;
constexpr std::uint32_t kItemRecords = 0x005614d8;
constexpr std::uint32_t kStartPiece = 0x005611a8;
constexpr std::uint32_t kBaseType = 0x0056116c;
constexpr std::uint32_t kMessageBuffer = 0x005613c0;        // the editor's message line and its second line
constexpr std::uint32_t kMessageBuffer2 = 0x0056145c;
constexpr std::uint32_t kBlink = 0x005611b8;               // byte
constexpr std::uint32_t kSimTick = 0x0056e31c;
constexpr std::uint32_t kLastTick = 0x00568624;
constexpr std::uint32_t kPolygonFlag = 0x00564384;
constexpr std::uint32_t kFadeFlag = 0x00561ca9;            // byte
constexpr std::uint32_t kCameraX = 0x0055f7cc;             // the camera's centre (clamped to keep the park in view)
constexpr std::uint32_t kCameraZ = 0x0055f7d4;
constexpr std::uint32_t kViewPosition = 0x0055f7fc;
constexpr std::uint32_t kPadTurn = 0x0056b0e8;             // pad bytes the camera listens to
constexpr std::uint32_t kPadTurnEdge = 0x0056b009;
constexpr std::uint32_t kPadLeft = 0x0056b078;
constexpr std::uint32_t kPadRight = 0x0056b088;
constexpr std::uint32_t kPadUp = 0x0056b098;
constexpr std::uint32_t kPadDown = 0x0056b0a8;
constexpr std::uint32_t kPieceNames = 0x00528820;          // the piece library the select manager walks (0x25 bytes a piece)
constexpr std::uint32_t kPieceBytes = 0x25;
constexpr std::uint32_t kPieceFlags = 0x00528844;
constexpr std::uint32_t kCategoryNames = 0x00529e38;       // 0x22 bytes a category
constexpr std::uint32_t kCategoryBytes = 0x22;
constexpr std::uint32_t kVersionLimits = 0x00518854;       // per version: models available; per theme and version: models used
constexpr std::uint32_t kThemeLimits = 0x00518868;
constexpr std::uint32_t kHudTop = 0x00527d50;
constexpr std::uint32_t kHudLeft = 0x00527d54;

// where the native code builds the locals retail keeps on the stack (the replay adds the region; the probe records the stack addresses as these)
constexpr std::uint32_t kScratchFile = 0x7ff00900;         // the park's file name (0x10)
constexpr std::uint32_t kScratchOut = 0x7ff00920;          // an out slot (the generator object a load made)
constexpr std::uint32_t kScratchParse = 0x7ff00930;        // the two words the buffer parser writes
constexpr std::uint32_t kScratchIndex = 0x7ff00940;        // the label index sscanf reads
constexpr std::uint32_t kScratchList = 0x7ff00a00;         // the 50 indices of the menu (0xc8 bytes)
constexpr std::uint32_t kScratchLimit = 0x7ff00950;        // the slot RemoveTop keeps the lowest layer in
constexpr std::uint32_t kScratchAngles = 0x7ff00a40;       // the camera's angles (three shorts) and its offset vector (three words)
constexpr std::uint32_t kScratchVector = 0x7ff00a50;
constexpr std::uint32_t kScratchMatrix = 0x7ff00a60;       // the view's rotation (0x20 bytes) and position (three words) when only the direction is wanted
constexpr std::uint32_t kScratchPosition = 0x7ff00a80;
constexpr std::uint32_t kScratchBytes = 0x200;
// Phase 4.11e: the editor object builds menus from arrays on its stack; the replay keeps them here
constexpr std::uint32_t kScratchTexts = 0x7ff00d00;        // the pointers to a menu's texts (up to 16 words)
constexpr std::uint32_t kScratchIds = 0x7ff00d40;          // the numbers the menu's entries answer with (up to 16 words)
constexpr std::uint32_t kScratchStrings = 0x7ff00d80;      // sixteen strings of 16 bytes (the points menu)
constexpr std::uint32_t kScratchLook = 0x7ff00e80;         // the camera's three angles (a dword and a word)
constexpr std::uint32_t kScratchLine = 0x7ff00ea0;         // a formatted line of text (0x40)
constexpr std::uint32_t kScratchCardName = 0x7ff00ee0;     // the park name the memory card scan writes (0x20)
constexpr std::uint32_t kScratchAdded = 0x7ff00f00;        // the item index TryAddItem writes
constexpr std::uint32_t kScratchGapName = 0x7ff00f10;      // a gap's name formatted for a menu (0x30)
constexpr std::uint32_t kScratchEmpty = 0x7ff00f40;        // an empty string
constexpr std::uint32_t kScratchEnd = 0x7ff01000;
constexpr std::uint32_t kViewMatrix = 0x0055f7fc;
constexpr std::uint32_t kMsgNoPiece = 0x0052a6b0;

// ---- assertion messages (the address of the format string names the assertion) --------------------------------------------------------
constexpr std::uint32_t kMsgMapInMemory = 0x00528754;
constexpr std::uint32_t kMsgMapFreed = 0x00528790;
constexpr std::uint32_t kMsgLoadSaving = 0x005287f8;
constexpr std::uint32_t kMsgGridZ = 0x0052a730;
constexpr std::uint32_t kMsgCursorOut = 0x0052a750;
constexpr std::uint32_t kMsgWhatModel = 0x0052a788;
constexpr std::uint32_t kMsgWeirdY = 0x0052a768;
constexpr std::uint32_t kMsgBadCursor = 0x0052a7a8;
constexpr std::uint32_t kMsgBaseGrid = 0x0052a7bc;
constexpr std::uint32_t kMsgLastPiece = 0x0052a6e4;
constexpr std::uint32_t kMsgMemCopy = 0x00534bd8;

class LevelEditor final {
public:
    using Dispatch = std::function<std::uint32_t(std::uint32_t address, const std::vector<std::uint32_t>& args)>;

    LevelEditor(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // A host that can run the routines outside this file sets this; without it they are named services with scripted results.
    void set_dispatch(Dispatch dispatch) { dispatch_ = std::move(dispatch); }
    // A replay of a retail capture: the words a callee stored through a pointer are scripted values to apply here. With a dispatcher or a live host the callee stored them itself.
    [[nodiscard]] bool replay() const noexcept { return !dispatch_ && !s_.live(); }

    // ---- editorglobal.cpp: the maps and the park menu ----
    void alloc_maps();                                                                 // 0x004326a0
    void free_maps();                                                                  // 0x004327a0
    void nothing_a();                                                                  // 0x00432840
    void nothing_b();                                                                  // 0x00432850
    void command(std::uint32_t what, std::uint32_t argument);                          // 0x00432860 (cdecl)
    void set_shade(std::uint32_t shade, std::uint32_t slot);                           // 0x004328c0 (cdecl)
    std::uint32_t alloc_buffer(std::uint32_t size);                                    // 0x00432960 (cdecl)
    void set_park_name(std::uint32_t label, std::uint32_t reset);                      // 0x00432980 (cdecl)
    std::uint32_t menu_construct(std::uint32_t self, std::uint32_t mode, std::uint32_t buffer, std::uint32_t size, std::uint32_t player);   // 0x004329d0
    std::uint32_t menu_delete(std::uint32_t self, std::uint32_t flags);                // 0x00432b60
    void menu_destroy(std::uint32_t self);                                             // 0x00432b80
    std::uint32_t menu_update(std::uint32_t self);                                     // 0x00432c10
    std::uint32_t menu_process(std::uint32_t self);                                    // 0x00432c60
    std::uint32_t menu_load_by_name(std::uint32_t self, std::uint32_t name);           // 0x00432f60
    void menu_set_mode(std::uint32_t self, std::uint32_t mode);                        // 0x00433090
    void load_labels(std::uint32_t self);                                              // 0x00433160

    // ---- leveleditor.cpp: loading, saving and the loop ----
    void init_commands(std::uint32_t table);                                           // 0x004331d0 (cdecl; not listed by Ghidra)
    void command_nothing();                                                            // 0x00433200 (not listed by Ghidra)
    void command_run();                                                                // 0x00433210 (a jump to the loop; not listed by Ghidra)
    void discard_park();                                                               // 0x00433220
    void unload_psx();                                                                 // 0x00433250
    void save_park(std::uint32_t buffer);                                              // 0x00433260 (cdecl with an unused first argument)
    std::uint32_t load_buffer(std::uint32_t out, std::uint32_t build_nodes);           // 0x00433280 (cdecl); the park is also stored at `out`, and returned
    void run();                                                                        // 0x004333c0

    // ---- leveleditor.cpp: the interface object ----
    void ui_destruct(std::uint32_t self);                                              // 0x00436520
    void ui_draw(std::uint32_t self);                                                  // 0x004365e0
    void ui_update(std::uint32_t self);                                                // 0x004369d0
    void select_piece(std::uint32_t self, std::uint32_t index);                        // 0x00436c60
    void step_piece(std::uint32_t self, std::int32_t delta);                           // 0x00436da0
    bool piece_selectable(std::uint32_t self, std::uint32_t index);                    // 0x00436e00
    void select_category(std::uint32_t self, std::uint32_t category);                  // 0x00436e30
    void step_category(std::uint32_t self, std::int32_t delta);                        // 0x00436fa0
    void ui_layout(std::uint32_t self, std::uint32_t layout);                          // 0x00436fd0
    std::uint32_t piece_char(std::uint32_t self);                                      // 0x004371d0
    void set_library(std::uint32_t self, std::uint32_t first, std::uint32_t second);   // 0x004371f0
    void set_limits(std::uint32_t self);                                               // 0x00437220

    // ---- leveleditor.cpp: the camera ----
    std::uint32_t camera_init(std::uint32_t self);                                     // 0x00437270 (answers this)
    void nothing_c();                                                                  // 0x004372c0
    void camera_set_target(std::uint32_t self, std::uint32_t x, std::uint32_t y, std::uint32_t z);   // 0x004372d0
    void camera_next_quarter(std::uint32_t self);                                      // 0x004372f0
    void camera_update(std::uint32_t self);                                            // 0x00437310
    void camera_view(std::uint32_t self, std::uint32_t angle, std::uint32_t matrix, std::uint32_t position);   // 0x00437580
    std::uint32_t camera_direction(std::uint32_t self, std::uint32_t buttons);         // 0x00437650
    std::uint32_t camera_look(std::uint32_t self, std::uint32_t out);                  // 0x00437720

    // ---- leveleditor.cpp: the cursor ----
    std::uint32_t cursor_construct(std::uint32_t self);                                // 0x004377d0
    std::uint32_t cursor_element_construct(std::uint32_t self);                        // 0x00437840
    std::uint32_t cursor_element_destruct(std::uint32_t self);                         // 0x00437850 (a jump to the record destructor; not listed by Ghidra)
    void cursor_destruct(std::uint32_t self);                                          // 0x00437860
    void cursor_init(std::uint32_t self, std::uint32_t park);                          // 0x004378d0
    std::uint32_t cursor_set_model(std::uint32_t self, std::uint32_t model);           // 0x00437950
    void cursor_rotate(std::uint32_t self, std::uint32_t quarters);                    // 0x00437c30
    std::uint32_t cursor_move_to(std::uint32_t self, std::uint32_t x, std::uint32_t layer, std::uint32_t z);   // 0x00437cd0
    std::uint32_t cursor_step(std::uint32_t self, std::uint32_t buttons);              // 0x00437d30
    void cursor_update(std::uint32_t self);                                            // 0x00437dc0
    void mark_cursor_gaps(std::uint32_t self);                                         // 0x00438320
    std::int32_t find_item(std::uint32_t self, std::uint32_t end);                     // 0x00438360
    float cursor_scaled(std::uint32_t self);                                           // 0x00438400
    void cursor_animate(std::uint32_t self);                                           // 0x00438410
    void cursor_draw(std::uint32_t self);                                              // 0x004385b0
    bool cursor_place(std::uint32_t self);                                             // 0x00438610
    bool try_add_item(std::uint32_t self, std::uint32_t index, std::uint32_t result);  // 0x00438c50
    bool remove_selected(std::uint32_t self);                                          // 0x00438ce0
    bool remove_item_under(std::uint32_t self);                                        // 0x00438dc0
    std::uint32_t top_piece(std::uint32_t self);                                       // 0x00438e00
    void flag_pieces(std::uint32_t self, std::uint32_t on);                            // 0x00438e60
    void cursor_set_label(std::uint32_t self, std::uint32_t value, std::uint32_t small);   // 0x00439110
    void indicator(std::uint32_t self);                                                // 0x00439130
    void draw_base(std::uint32_t self);                                                // 0x00439540

    // ---- leveleditor.cpp: the editor object (4.11e; `self` is the 0x1dc-byte editor, which is also a Font: +0x38 is its font) ----
    std::uint32_t editor_construct(std::uint32_t self);                                // 0x004334d0
    void editor_destruct(std::uint32_t self);                                          // 0x004338e0
    void editor_draw(std::uint32_t self);                                              // 0x00433a40
    std::uint32_t editor_update(std::uint32_t self);                                   // 0x00433cb0 (1 when the editor is done)
    void handle_input(std::uint32_t self);                                             // 0x00434090
    void adjust_gap(std::uint32_t self);                                               // 0x004343e0
    void reset_state(std::uint32_t self);                                              // 0x00434710
    void run_menus(std::uint32_t self);                                                // 0x00434750
    void update_menu(std::uint32_t self);                                              // 0x00435830
    void place_item(std::uint32_t self);                                               // 0x00435be0
    void remove_item(std::uint32_t self);                                              // 0x00435e50
    void refresh_budget(std::uint32_t self);                                           // 0x00435f20
    std::int32_t pieces_left();                                                        // 0x00435f60 (cdecl)
    std::uint32_t ui_construct(std::uint32_t self);                                    // 0x00435fa0

private:
    void copy_font(std::uint32_t destination, std::uint32_t source);
    std::uint32_t menu_wrapper(const std::vector<std::uint32_t>& texts, std::uint32_t ids, std::uint32_t flag);
    // a service that writes a dword through its out pointer (when it is not null) and, when `answers`, returns a value: the script holds the dword first, then the answer
    std::uint32_t engine_out(std::uint32_t address, std::vector<std::uint32_t> args, std::uint32_t out, bool answers, std::string text = {});
    // a service that writes `size` bytes through a pointer and then answers a value
    std::uint32_t engine_fill_value(std::uint32_t address, std::vector<std::uint32_t> args, std::string text, std::uint32_t destination, std::uint32_t size);
    [[nodiscard]] std::uint32_t alloc(std::uint32_t size, std::uint32_t heap = 0, std::uint32_t one = 1, std::uint32_t last = 0);
    void free_block(std::uint32_t block);
    void log(std::uint32_t format);
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {});
    [[nodiscard]] std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {});
    [[nodiscard]] std::uint32_t virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}, std::string text = {});
    [[nodiscard]] std::uint32_t operator_new(std::uint32_t size);
    void sprintf_to(std::uint32_t destination, std::uint32_t format, std::uint32_t argument);
    void load_resource(std::uint32_t buffer, std::uint32_t size);
    void sprintf_list(std::uint32_t destination, std::uint32_t format, const std::vector<std::uint32_t>& arguments);
    void copy_string(std::uint32_t destination, std::uint32_t source);
    [[nodiscard]] std::string bytes_text(std::uint32_t at, std::uint32_t size);
    void engine_fill(std::uint32_t address, std::vector<std::uint32_t> args, std::string text, std::uint32_t destination, std::uint32_t size);
    void mem_copy(std::uint32_t destination, std::uint32_t source, std::uint32_t size);

    RetailMemory& m_;
    RetailServices& s_;
    Dispatch dispatch_;
};

}  // namespace leveleditor
}  // namespace opentony::runtime
