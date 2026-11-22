// Replays retail forced calls (Phase 4.11c) through the native park generator:
//
//   opentony_level_gen_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the levelgen_specs groups) holds the retail memory regions the ported code touches
// and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "gen_objs.hpp"
#include "level_gen.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    genobjs::GenObjs gen;
    levelgen::LevelGen lg;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), gen(mem, svc), lg(mem, svc, gen) {
        if (!mem.contains(levelgen::kScratchName, levelgen::kScratchBytes)) {
            mem.add_region(levelgen::kScratchName & 0xffffff00, levelgen::kScratchBytes * 2);
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using LevelAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define L c.lg

// the fifth point of a triangle's element is a copy of an uninitialised stack local (see the in-model, 4.11b)
Unspecified element_gaps(Ctx& c) { return c.gen.unspecified(); }

Unspecified rails_gaps(Ctx& c) {
    // the second polygon list's deadline word holds the live clock of the retail run
    if (c.lg.rails_scratch_list() == 0) {
        return {};
    }
    return {{c.lg.rails_scratch_list() + 8, 4}};
}

const std::map<std::uint32_t, LevelAdapter>& adapters() {
    static const std::map<std::uint32_t, LevelAdapter> table = {
        // ---- construction, the maps and the source models ----
        {0x004397d0, {"LevelGen_Initialize", 0xffffffff, false, A(L.construct(self, a[0]))}},
        {0x00439900, {"LevelGen_Destruct", 0, false, V(L.destruct(self))}},
        {0x004399a0, {"LevelGen_InitStatics", 0, false, V(L.init_statics(a[0]))}},
        {0x00439b70, {"LevelGen_LoadModels", 0, false, V(L.load_models(self, a[0])), element_gaps}},
        {0x00439e10, {"LevelGen_UnloadModels", 0, false, V(L.unload_models(self))}},
        {0x00439ed0, {"LevelGen_ClearSource", 0, false, V(L.clear_source(self))}},
        {0x00439ef0, {"LevelGen_AllocMaps", 0, false, V(L.alloc_maps(self))}},
        {0x0043a100, {"LevelGen_FreeMaps", 0, false, V(L.free_maps(self))}},
        {0x0043a240, {"LevelGen_Resize", 0, false, V(L.resize(self, a[0]))}},
        {0x0043a280, {"LevelGen_SelectVariant", 0, false, V(L.select_variant(self, a[0], a[1], a[2])), element_gaps}},
        {0x0043a3e0, {"LevelGen_BuildFloorTable", 0, false, V(L.build_floor_table(self))}},
        // ---- pieces, gaps and starts ----
        {0x0043aa30, {"LevelGen_ReleasePieces", 0, false, V(L.release_pieces(self))}},
        {0x0043ab00, {"LevelGen_BuildPieceObjects", 0, false, V(L.build_piece_objects(self))}},
        {0x0043b080, {"LevelGen_PrunePieces", 0, false, V(L.prune_pieces(self))}},
        {0x0043b0f0, {"LevelGen_ComputeNeighbours", 0, false, V(L.compute_neighbours(self))}},
        {0x0043b3c0, {"LevelGen_FreeFloorPieces", 0, false, V(L.free_floor_pieces(self))}},
        {0x0043b410, {"LevelGen_BuildGridPieceObjects", 0, false, V(L.build_floor_objects(self))}},
        {0x0043b850, {"LevelGen_FlagFloorPieces", 0, false, V(L.flag_floor_pieces(self))}},
        {0x0043b880, {"LevelGen_FreeGaps", 0, false, V(L.free_gaps(self))}},
        {0x0043b8d0, {"LevelGen_BuildItemGapObjects", 0, false, V(L.build_gap_objects(self))}},
        {0x0043bba0, {"LevelGen_FreeStarts", 0, false, V(L.free_starts(self))}},
        {0x0043bbf0, {"LevelGen_BuildStarts", 0, false, V(L.build_starts(self))}},
        {0x0043bcd0, {"LevelGen_LoadRails", 0, false, V(L.load_rails(self, a[0]))}},
        {0x0043bf80, {"LevelGen_UnloadOutPsx", 0, false, V(L.unload_out_psx(a[0]))}},
        // ---- the PRK buffer ----
        {0x0043c050, {"CustomPark_ParseBuffer", 0, false, V(L.parse_buffer(a[0], a[1]))}},
        {0x0043c480, {"CustomPark_SerializeBuffer", 0xffffffff, false, A(L.serialize_buffer(a[0]))}},
        {0x0043c6b0, {"LevelGen_TranslateReferences", 0, false, V(L.translate_references(a[0]))}},
        // ---- editing ----
        {0x0043c770, {"LevelGen_PlacePiece", 0, false, V(L.place_piece(self, a[0], a[1], a[2], a[3], a[4]))}},
        {0x0043cc30, {"LevelGen_AddItem", 0xffffffff, false, A(L.add_item(a[0], a[1], a[2], a[3]))}},
        {0x0043cd90, {"LevelGen_RefreshItemGaps", 0, false, V(L.refresh_item_gaps(self, a[0], a[1]))}},
        {0x0043ce00, {"LevelGen_RemoveItemsAt", 0, false, V(L.remove_items_at(a[0], a[1], a[2]))}},
        {0x0043ce60, {"LevelGen_SetFlash", 0, false, V(L.set_flash(self, a[0]))}},
        {0x0043cf20, {"LevelGen_RemoveTop", 0xff, false, A(L.remove_top(self, a[0], a[1], a[2], a[3]))}},
        // ---- the region ----
        {0x0043d400, {"LevelGen_FinalizeRuntimeRegion", 0xffffffff, false, A(L.finalize_region(self, a[0], a[1])), [](Ctx& c) { Unspecified gaps = c.gen.unspecified(); for (const auto& g : rails_gaps(c)) { gaps.push_back(g); } return gaps; }, {"engine_0045d760"}}},
        {0x0043e080, {"PSX_DestroyObjectRecordArray", 0xffffffff, false, A(L.destroy_object_array(self, a[0]))}},
        {0x0043e0e0, {"LevelGen_Rasterise", 0xffffffff, false, A(L.rasterise(self, S(a[0]), S(a[1]), S(a[2]), S(a[3]), S(a[4]), a[5], a[6]))}},
        {0x0043e2d0, {"LevelGen_CreateZonePacket", 0xffffffff, false, A(L.create_zone_packet(self, a[0], a[1], a[2]))}},
        {0x0043e7c0, {"LevelGen_CreateRails", 0, false, V(L.create_rails(self)), rails_gaps, {"engine_0045d760"}}},
        {0x0043e980, {"LevelGen_BuildNodes", 0, false, V(L.build_nodes(self))}},
        {0x0043f1b0, {"LevelGen_MarkEdges", 0, false, V(L.mark_edges(self, a[0], a[1]))}},
        {0x0043f400, {"LevelGen_SetCellFlags", 0, false, V(L.set_cell_flags(self, a[0], a[1]))}},
        {0x0043f540, {"LevelGen_SetFloorFlags", 0, false, V(L.set_floor_flags(self, a[0]))}},
        {0x0043f5a0, {"LevelGen_MarkGaps", 0, false, V(L.mark_gaps(self, a[0]))}},
        {0x0043f5e0, {"LevelGen_SetFog", 0, false, V(L.set_fog(self, a[0]))}},
        {0x0043f6c0, {"LevelGen_DimFog", 0, false, V(L.dim_fog(self))}},
        {0x0043f710, {"LevelGen_ZoneRoom", 0xffffffff, false, A(L.zone_room())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "park generator retail forced calls");
}
