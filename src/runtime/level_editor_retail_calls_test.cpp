// Replays retail forced calls (Phase 4.11d) through the native park editor:
//
//   opentony_level_editor_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the editor_specs groups) holds the retail memory regions the ported code touches
// and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "level_editor.hpp"
#include "retail_calls_replay.hpp"

#include <cstring>
#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    leveleditor::LevelEditor ed;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), ed(mem, svc) {
        if (!mem.contains(leveleditor::kScratchFile, 0x10)) {
            mem.add_region(leveleditor::kScratchFile & 0xffffff00, leveleditor::kScratchBytes * 4);
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using EditorAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define E c.ed

// the loop copies the live clock (a timer thread moves it on while the retail run is stopped in the debugger): the two words are not compared
Unspecified clock_gaps(Ctx&) { return {{leveleditor::kSimTick, 4}, {leveleditor::kLastTick, 4}}; }

const std::map<std::uint32_t, EditorAdapter>& adapters() {
    static const std::map<std::uint32_t, EditorAdapter> table = {
        // ---- editorglobal.cpp ----
        {0x004326a0, {"EditorMaps_Alloc", 0, false, V(E.alloc_maps())}},
        {0x004327a0, {"EditorMaps_Free", 0, false, V(E.free_maps())}},
        {0x00432840, {"EditorGlobal_NothingA", 0, false, V(E.nothing_a())}},
        {0x00432850, {"EditorGlobal_NothingB", 0, false, V(E.nothing_b())}},
        {0x00432860, {"EditorGlobal_Command", 0, false, V(E.command(a[0], a[1])), clock_gaps}},
        {0x004328c0, {"EditorObjects_SetShade", 0, false, V(E.set_shade(a[0], a[1]))}},
        {0x00432960, {"EditorBuffer_Alloc", 0xffffffff, false, A(E.alloc_buffer(a[0]))}},
        {0x00432980, {"EditorGlobal_SetParkName", 0, false, V(E.set_park_name(a[0], a[1]))}},
        {0x004329d0, {"ParkMenu_Construct", 0xffffffff, false, A(E.menu_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x00432b60, {"ParkMenu_Delete", 0xffffffff, false, A(E.menu_delete(self, a[0]))}},
        {0x00432b80, {"ParkMenu_Destroy", 0, false, V(E.menu_destroy(self))}},
        {0x00432c10, {"ParkMenu_Update", 0, false, V(E.menu_update(self))}},
        {0x00432c60, {"CustomPark_ProcessState", 0xffffffff, false, A(E.menu_process(self))}},
        {0x00432f60, {"ParkMenu_LoadByName", 0xff, false, A(E.menu_load_by_name(self, a[0]))}},
        {0x00433090, {"ParkMenu_SetMode", 0, false, V(E.menu_set_mode(self, a[0]))}},
        {0x00433160, {"CDParks_LoadLabels", 0, false, V(E.load_labels(self))}},
        // ---- leveleditor.cpp: loading, saving and the loop ----
        {0x004331d0, {"EditorGlobal_InitCommands", 0, false, V(E.init_commands(a[0]))}},
        {0x00433200, {"EditorGlobal_CommandNothing", 0, false, V(E.command_nothing())}},
        {0x00433210, {"EditorGlobal_CommandRun", 0, false, V(E.command_run()), clock_gaps}},
        {0x00433220, {"CustomPark_Discard", 0, false, V(E.discard_park())}},
        {0x00433250, {"CustomPark_UnloadPsx", 0, false, V(E.unload_psx())}},
        {0x00433260, {"CustomPark_Save", 0, false, V(E.save_park(a[1]))}},
        {0x00433280, {"CustomPark_LoadBuffer", 0, false, V(E.load_buffer(a[0], a[1]))}},
        {0x004333c0, {"LevelEditor_Run", 0, false, V(E.run()), clock_gaps}},
        // ---- leveleditor.cpp: the interface object ----
        {0x00436520, {"LevelEditor_UiDestruct", 0, false, V(E.ui_destruct(self))}},
        {0x004365e0, {"LevelEditor_UiDraw", 0, false, V(E.ui_draw(self))}},
        {0x004369d0, {"LevelEditor_UiUpdate", 0, false, V(E.ui_update(self))}},
        {0x00436c60, {"SelectManager__SetPiece", 0, false, V(E.select_piece(self, a[0]))}},
        {0x00436da0, {"SelectManager__StepPiece", 0, false, V(E.step_piece(self, S(a[0])))}},
        {0x00436e00, {"SelectManager__PieceSelectable", 0xff, false, A(E.piece_selectable(self, a[0]))}},
        {0x00436e30, {"SelectManager__SetCategory", 0, false, V(E.select_category(self, a[0]))}},
        {0x00436fa0, {"SelectManager__ChangeCategory", 0, false, V(E.step_category(self, S(a[0])))}},
        {0x00436fd0, {"LevelEditor_UiLayout", 0, false, V(E.ui_layout(self, a[0]))}},
        {0x004371d0, {"SelectManager__PieceChar", 0xff, false, A(E.piece_char(self))}},
        {0x004371f0, {"SelectManager__SetLibrary", 0, false, V(E.set_library(self, a[0], a[1]))}},
        {0x00437220, {"LevelEditor_SetLimits", 0, false, V(E.set_limits(self))}},
        // ---- leveleditor.cpp: the camera ----
        {0x00437270, {"EditorCamera_Init", 0, false, A(E.camera_init(self))}},
        {0x004372c0, {"EditorCamera_Nothing", 0, false, V(E.nothing_c())}},
        {0x004372d0, {"EditorCamera_SetTarget", 0, false, V(E.camera_set_target(self, a[0], a[1], a[2]))}},
        {0x004372f0, {"EditorCamera_NextQuarter", 0, false, V(E.camera_next_quarter(self))}},
        {0x00437310, {"EditorCamera_Update", 0, false, V(E.camera_update(self))}},
        {0x00437580, {"EditorCamera_View", 0, false, V(E.camera_view(self, a[0], a[1], a[2]))}},
        {0x00437650, {"EditorCamera_Direction", 0xffffffff, false, A(E.camera_direction(self, a[0]))}},
        {0x00437720, {"EditorCamera_Look", 0xff, false, A(E.camera_look(self, a[0]))}},
        // ---- leveleditor.cpp: the cursor ----
        {0x004377d0, {"EditorCursor_Construct", 0xffffffff, false, A(E.cursor_construct(self))}},
        {0x00437840, {"EditorCursor_ElementConstruct", 0xffffffff, false, A(E.cursor_element_construct(self))}},
        {0x00437850, {"EditorCursor_ElementDestruct", 0, false, A(E.cursor_element_destruct(self))}},
        {0x00437860, {"EditorCursor_Destruct", 0, false, V(E.cursor_destruct(self))}},
        {0x004378d0, {"EditorCursor_Init", 0, false, V(E.cursor_init(self, a[0]))}},
        {0x00437950, {"EditorCursor_SetModel", 0xff, false, A(E.cursor_set_model(self, a[0]))}},
        {0x00437c30, {"EditorCursor_Rotate", 0, false, V(E.cursor_rotate(self, a[0]))}},
        {0x00437cd0, {"EditorCursor_MoveTo", 0xff, false, A(E.cursor_move_to(self, a[0], a[1], a[2]))}},
        {0x00437d30, {"EditorCursor_Step", 0xff, false, A(E.cursor_step(self, a[0]))}},
        {0x00437dc0, {"EditorCursor_Update", 0, false, V(E.cursor_update(self))}},
        {0x00438320, {"EditorCursor_MarkGaps", 0, false, V(E.mark_cursor_gaps(self))}},
        {0x00438360, {"EditorCursor_FindItem", 0xffffffff, false, A(E.find_item(self, a[0]))}},
        {0x00438400, {"EditorCursor_Scaled", 0xffffffff, false, [](Ctx& c, const Args&, std::uint32_t self) -> std::uint32_t {
            const float v = c.ed.cursor_scaled(self);
            std::uint32_t bits;
            std::memcpy(&bits, &v, sizeof bits);
            return bits;
        }}},
        {0x00438410, {"EditorCursor_Animate", 0, false, V(E.cursor_animate(self))}},
        {0x004385b0, {"EditorCursor_Draw", 0, false, V(E.cursor_draw(self))}},
        {0x00438610, {"EditorCursor_Place", 0xff, false, A(E.cursor_place(self))}},
        {0x00438c50, {"EditorCursor_TryAddItem", 0xff, false, A(E.try_add_item(self, a[0], a[1]))}},
        {0x00438ce0, {"EditorCursor_RemoveSelected", 0xff, false, A(E.remove_selected(self))}},
        {0x00438dc0, {"EditorCursor_RemoveItemUnder", 0xff, false, A(E.remove_item_under(self))}},
        {0x00438e00, {"EditorCursor_TopPiece", 0xffffffff, false, A(E.top_piece(self))}},
        {0x00438e60, {"EditorCursor_FlagPieces", 0, false, V(E.flag_pieces(self, a[0]))}},
        {0x00439110, {"EditorCursor_SetLabel", 0, false, V(E.cursor_set_label(self, a[0], a[1]))}},
        {0x00439130, {"EditorCursor_Indicator", 0, false, V(E.indicator(self))}},
        {0x00439540, {"EditorCursor_DrawBase", 0, false, V(E.draw_base(self))}},
        // ---- leveleditor.cpp: the editor object (4.11e) ----
        {0x004334d0, {"LevelEditor_Construct", 0xffffffff, false, A(E.editor_construct(self))}},
        {0x004338e0, {"LevelEditor_Destruct", 0, false, V(E.editor_destruct(self))}},
        {0x00433a40, {"LevelEditor__Draw", 0, false, V(E.editor_draw(self))}},
        {0x00433cb0, {"LevelEditor__Update", 0xff, false, A(E.editor_update(self))}},
        {0x00434090, {"LevelEditor__HandleInput", 0, false, V(E.handle_input(self))}},
        {0x004343e0, {"LevelEditor__AdjustGap", 0, false, V(E.adjust_gap(self))}},
        {0x00434710, {"LevelEditor__ResetState", 0, false, V(E.reset_state(self))}},
        {0x00434750, {"LevelEditor__RunMenus", 0, false, V(E.run_menus(self))}},
        {0x00435830, {"LevelEditor__UpdateMenu", 0, false, V(E.update_menu(self))}},
        {0x00435be0, {"LevelEditor__PlaceItem", 0, false, V(E.place_item(self))}},
        {0x00435e50, {"LevelEditor__RemoveItem", 0, false, V(E.remove_item(self))}},
        {0x00435f20, {"LevelEditor__RefreshBudget", 0, false, V(E.refresh_budget(self))}},
        {0x00435f60, {"LevelEditor_PiecesLeft", 0xffffffff, false, A(E.pieces_left())}},
        {0x00435fa0, {"LevelEditor_UiConstruct", 0xffffffff, false, A(E.ui_construct(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "park editor retail forced calls");
}
