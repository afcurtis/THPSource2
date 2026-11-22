// Replays retail forced calls (Phase 4.11b) through the native generator object layer:
//
//   opentony_gen_objs_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the gen_specs groups) holds the retail memory regions the ported code touches
// and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "gen_objs.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    genobjs::GenObjs gen;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), gen(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using GenAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.gen

Unspecified element_gaps(Ctx& c) { return c.gen.unspecified(); }

// the slots a function hands back through pointer arguments are read from the case's memory and written back
std::uint32_t edge_runs(Ctx& c, const Args& a) {
    std::uint32_t first = c.mem.u32(a[4]);
    std::uint32_t last = c.mem.u32(a[5]);
    std::uint32_t near_last = c.mem.u32(a[6]);
    std::uint32_t near_first = c.mem.u32(a[7]);
    const bool found = c.gen.poly_edge_runs(a[0], a[1], S(a[2]), S(a[3]), &first, &last, &near_last, &near_first);
    c.mem.set32(a[4], first);
    c.mem.set32(a[5], last);
    c.mem.set32(a[6], near_last);
    c.mem.set32(a[7], near_first);
    return found ? 1u : 0u;
}

const std::map<std::uint32_t, GenAdapter>& adapters() {
    static const std::map<std::uint32_t, GenAdapter> table = {
        // ---- genobjs.cpp: the model-info pool and the in-model ----
        {0x0043f730, {"ModelInfo_ClearTable", 0, false, V(G.model_info_clear(a[0]))}},
        {0x0043f7b0, {"EditorInModel_KillModelInfo", 0, false, V(G.model_info_free_all())}},
        {0x0043f820, {"EditorInModel_NewModelInfo", 0xffffffff, false, A(G.model_info_alloc())}},
        {0x0043f8e0, {"EditorInModel_Construct", 0xffffffff, false, A(G.in_construct(self))}},
        {0x0043f8f0, {"EditorInModel_DestructThunk", 0, false, V(G.in_destruct_thunk(self))}},
        {0x0043f900, {"EditorInModel_Destruct", 0, false, V(G.in_destruct(self))}},
        {0x0043f930, {"EditorInModel_SetData", 0, false, V((G.reset_unspecified(), G.in_set_data(self, a[0], a[1]))), element_gaps}},
        {0x0043fda0, {"EditorInModel_SetTransparency", 0, false, V(G.in_set_transparency(self, a[0]))}},
        // ---- the out-model's statics ----
        {0x0043fe00, {"EditorOutModel_Init", 0, false, V(G.out_init())}},
        {0x0043ff30, {"EditorOutModel_BuildTables", 0, false, V(G.out_build_tables())}},
        {0x00440010, {"EditorOutModel_Cleanup", 0, false, V(G.out_cleanup())}},
        {0x00440060, {"EditorOutModel_AllocNode", 0xffffffff, false, A(G.node_alloc())}},
        {0x00440170, {"EditorOutModel_SpareNodes", 0xffffffff, false, A(G.node_spare())}},
        // ---- the out-model ----
        {0x00440190, {"LevelGen_ConstructPieceObject", 0xffffffff, false, A(G.out_construct(self, a[0]))}},
        {0x00440290, {"EditorOutModel_ReleaseNodes", 0, false, V(G.out_release_nodes(self))}},
        {0x004402d0, {"EditorOutModel_ShowNodes", 0, false, V(G.out_show_nodes(self))}},
        {0x004402f0, {"EditorOutModel_HideAgainst", 0, false, V(G.out_hide_against(self, a[0], a[1]))}},
        {0x004403c0, {"EditorOutModel_HidePair", 0, false, V(G.out_hide_pair(a[0], a[1], a[2], a[3], a[4]))}},
        {0x004407c0, {"EditorOutModel_RemoveAllHiddenFaces", 0, false, V(G.out_remove_hidden(self))}},
        {0x00440920, {"EditorOutModel_SetModelDataSize", 0xffffffff, false, A(G.out_data_size(self, a[0]))}},
        {0x004409e0, {"LevelGen_LoadModelDescriptor", 0xffffffff, false, A(G.out_create_data(self, a[0], a[1]))}},
        {0x00440e80, {"EditorOutModel_MarkHiddenFaces", 0, false, V(G.out_mark_faces(self, a[0], a[1], a[2]))}},
        {0x00441050, {"EditorOutModel_SetFog", 0, false, V(G.out_set_fog(self, a[0], a[1]))}},
        // ---- gap members ----
        {0x004410b0, {"LevelGen_InitGapMember", 0xffffffff, false, A(G.gap_init(self))}},
        {0x004410d0, {"LevelGen_GapMemberNothing", 0, false, V(G.gap_nothing())}},
        {0x004410e0, {"LevelGen_ComputeGapMemberEdges", 0, false, V(G.gap_compute_edges(self, a[0], a[1], a[2]))}},
        {0x00441240, {"LevelGen_SetGapMemberConstants", 0xffffffff, false, A(G.gap_set_constants(self))}},
        {0x00441270, {"GapMember_BuildPrims", 0xffffffff, false, A(G.gap_build_prims(self, a[0]))}},
        {0x00441400, {"GapMember_BuildHeader", 0, false, V(G.gap_build_header(self))}},
        {0x00441630, {"LevelGen_SetGapMemberActive", 0, false, V(G.gap_set_active(self, a[0]))}},
        {0x00441640, {"GapMember_SetVisible", 0, false, V(G.gap_set_visible(self, a[0]))}},
        {0x00441670, {"GapMember_ReturnThis", 0xffffffff, false, A(G.gap_return_this(self))}},
        {0x00441680, {"GapMember_NothingB", 0, false, V(G.gap_nothing_b())}},
        {0x00441690, {"LevelGen_SetGapMemberDefaults", 0xffffffff, false, A(G.gap_set_defaults(self))}},
        {0x004416b0, {"GapPiece_BuildQuad", 0xffffffff, false, A(G.gap_build_quad(self, a[0]))}},
        // ---- points and polygons ----
        {0x004417c0, {"GenPoint_Init", 0xffffffff, false, A(G.point_init(self))}},
        {0x004417e0, {"GenPoint_Near", 0xff, false, A(G.point_near(self, a[0]))}},
        {0x00441830, {"GenPoly_Init", 0xffffffff, false, A(G.poly_init(self))}},
        {0x00441850, {"GenPoly_FreePoints", 0, false, V(G.poly_free_ring(self))}},
        {0x00441880, {"GenPoly_Append", 0, false, V(G.poly_append(self, a[0], a[1]))}},
        {0x004419c0, {"GenPoly_FixWinding", 0, false, V(G.poly_fix_winding(self))}},
        {0x00441aa0, {"GenPoly_Reverse", 0, false, V(G.poly_reverse(self))}},
        {0x00441ae0, {"GenPoly_CopyTransformed", 0xffffffff, false, A(G.poly_copy_transformed(self, a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00441cd0, {"GenPoly_TryMerge", 0xff, false, A(G.poly_try_merge(self, a[0], a[1]))}},
        {0x00441da0, {"GenPoly_JoinEnds", 0xff, false, A(G.poly_join_ends(self, a[0]))}},
        {0x00442100, {"GenPoly_EdgeRuns", 0xff, false, [](Ctx& c, const Args& a, std::uint32_t) -> std::uint32_t { return edge_runs(c, a); }}},
        {0x00442210, {"GenPoly_MergeLoops", 0xff, false, A(G.poly_merge_loops(self, a[0]))}},
        {0x00442670, {"GenPoly_CutBy", 0xff, false, A(G.poly_cut_by_at(self, a[0], a[1]))}},
        {0x00442780, {"GenPoly_Split", 0xffffffff, false, A(G.poly_split(self))}},
        {0x00442970, {"GenPoly_UpdateBounds", 0xffffffff, false, A(G.poly_update_bounds(self))}},
        {0x00442ae0, {"GenPoly_CutOut", 0xff, false, A(G.poly_cut_out_at(self, a[0], a[1]))}},
        {0x00442e90, {"GenPoint_EdgeStraight", 0xff, false, A(G.edge_straight(a[0], a[1], a[2], S(a[3])))}},
        {0x00442f40, {"GenPoly_Trim", 0xffffffff, false, A(G.poly_trim(self, a[0]))}},
        // ---- lists of polygons ----
        {0x004431f0, {"GenPolyList_Init", 0xffffffff, false, A(G.plist_init(self))}},
        {0x00443200, {"GenPolyList_FreeAllThunk", 0, false, V(G.plist_free_all_thunk(self))}},
        {0x00443210, {"GenPolyList_NewPoly", 0xffffffff, false, A(G.plist_new(self))}},
        {0x00443250, {"GenPolyList_FreeAll", 0, false, V(G.plist_free_all(self))}},
        {0x00443290, {"GenPolyList_Count", 0xffffffff, false, A(G.plist_count(self))}},
        {0x004432b0, {"GenPolyList_CountPoints", 0xffffffff, false, A(G.plist_points(self))}},
        {0x004432d0, {"GenPolyList_Merge", 0, false, V(G.plist_merge(self, a[0], a[1]))}},
        {0x004434e0, {"GenPolyList_Pair", 0xff, false, A(G.plist_pair(self, a[0], a[1]))}},
        {0x00443580, {"GenPolyList_Pop", 0xffffffff, false, A(G.plist_pop(self))}},
        {0x004435e0, {"GenPolyList_Append", 0, false, V(G.plist_append(self, a[0]))}},
        {0x004436a0, {"GenObjs_NewZeroed", 0xffffffff, false, A(G.new_zeroed(a[0]))}},
        {0x004436d0, {"GenObjs_Delete", 0, false, V(G.delete_block(a[0]))}},
        // ---- faceremoval.cpp ----
        {0x004436e0, {"FaceRemoval_Setup", 0, false, V(G.face_setup())}},
        {0x00443710, {"FaceRemoval_Cleanup", 0, false, V(G.face_cleanup())}},
        {0x00443730, {"FaceRemoval_NewPoint", 0xffffffff, false, A(G.point_new())}},
        {0x004437d0, {"FaceRemoval_ResetPoints", 0, false, V(G.points_reset())}},
        {0x004437f0, {"FaceRemoval_PrintPoint", 0, false, V(G.point_print(a[0]))}},
        {0x00443820, {"FaceRemoval_Overlap", 0xffffffff, false, A(G.faces_overlap(a[0], a[1]))}},
        {0x00443eb0, {"FaceRemoval_PointsEqual", 0xff, false, A(G.points_equal(a[0], a[1]))}},
        {0x00443f00, {"FaceRemoval_RingArea", 0xffffffff, false, A(G.points_area(a[0]))}},
        {0x00443f30, {"FaceRemoval_Cross", 0xffffffff, false, A(G.points_cross(a[0], a[1], a[2], a[3]))}},
        {0x00443f70, {"FaceRemoval_SameSide", 0xff, false, A(G.points_side(a[0], a[1], a[2], a[3]))}},
        {0x00443fc0, {"FaceRemoval_PrintRing", 0, false, V(G.ring_print(a[0]))}},
        // ---- editorpieces2.cpp ----
        {0x00444010, {"EditorPieces_ClearFlag", 0, false, V(G.pieces_clear_flag())}},
        {0x00444020, {"EditorPieces_Setup", 0, false, V(G.pieces_setup(a[0]))}},
        {0x004441c0, {"EditorPieces_Find", 0xffffffff, false, A(G.piece_find(a[0], a[1]))}},
        {0x00444210, {"EditorPieces_ThemeId", 0xffffffff, false, A(G.piece_theme_id(a[0], a[1]))}},
        {0x00444280, {"EditorPieces_IsSpecial", 0xff, false, A(G.piece_is_special(a[0]))}},
        {0x004442b0, {"EditorPieces_ListIndex", 0xffffffff, false, A(G.piece_list_index(a[0]))}},
        {0x00444380, {"LevelGen_TranslatePieceReference", 0xffffffff, false, A(G.piece_translate(a[0]))}},
        {0x00444410, {"EditorPieces_Kind", 0xff, false, A(G.piece_kind(a[0]))}},
        {0x00444450, {"EditorPieces_TurnedId", 0xffffffff, false, A(G.piece_turned(a[0], a[1], a[2]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "generator object layer retail forced calls");
}
