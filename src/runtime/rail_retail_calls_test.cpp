// Replays retail forced calls (Phase 4.15a) through the native rail layer (rail_nodes.cpp: the node accessors, the lookup table and the runtime record list of rail.cpp):
//
//   opentony_rail_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of rail_specs.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "rail_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::rail;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    RailKit rail;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), rail(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
        if (!mem.contains(mathkit::gte::kIr, 0x10)) {
            mem.add_region(0x006a3e00, 0x120);                   // the GTE register file Math_NormalizeFixed12 squares through (the 4.15a fixtures did not capture it)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using RailAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.rail

const std::map<std::uint32_t, RailAdapter>& adapters() {
    static const std::map<std::uint32_t, RailAdapter> table = {
        {0x004a9f70, {"TRG_ReadRailTriggerWord", 0xffff, false, A(G.read_trigger_word(a[0]))}},
        {0x004a9fd0, {"Rail_Type", 0xffffffff, false, A(G.rail_type(a[0]))}},
        {0x004a9ff0, {"Rail_FlagsPointer", 0xffffffff, false, A(G.flags_pointer(a[0]))}},
        {0x004aa040, {"Rail_IsRailNode", 0xffffffff, false, A(G.is_rail_node(a[0]))}},
        {0x004aa090, {"Rail_GetLink", 0xffff, false, A(G.get_link(a[0], a[1]))}},
        {0x004aa1c0, {"Rail_NumLinks", 0xffff, false, A(G.num_links(a[0]))}},
        {0x004aa220, {"Rail_NextNode", 0xffff, false, A(G.next_node(a[0]))}},
        {0x004aa290, {"Rail_PrevNode", 0xffff, false, A(G.prev_node(a[0]))}},
        {0x004aa330, {"Rail_PrevNodeQuick", 0xffff, false, A(G.prev_node_quick(a[0], a[1]))}},
        {0x004aa3c0, {"Rail_SetState", 0, false, V(G.set_state(a[0], a[1]))}},
        {0x004aa410, {"Rail_ClearState", 0, false, V(G.clear_state(a[0]))}},
        {0x004aa420, {"Rail_ActivateNode", 0, false, V(G.activate_node(a[0]))}},
        {0x004aa430, {"GetIndexOfNode", 0xffffffff, false, A(G.index_of_node(a[0]))}},
        {0x004aa4b0, {"SetupLookup", 0, false, V(G.setup_lookup())}},
        {0x004aa870, {"Rail_Init", 0, false, V(G.rail_init())}},
        {0x004aa8a0, {"Rail_DeleteAll", 0, false, V(G.delete_all())}},
        {0x004aaf10, {"CRail_destruct", 0xffffffff, false, A(G.crail_destruct(self, a[0]))}},
        {0x004aaf30, {"CRail_destroy", 0, false, V(G.crail_destroy(self))}},
        {0x004aa5d0, {"Rail_CreateRails", 0, false, V(G.create_rails())}},
        {0x004aa8c0, {"Rail_CreateRuntimeRecord", 0xffffffff, false, A(G.create_runtime_record(self, a[0]))}},
        {0x004629f0, {"M3dColij_FlushCache", 0, false, V(G.flush_cache())}},
        {0x00462490, {"M3D_AdvanceCollisionQueryGeneration", 0, false, V(G.advance_query_generation())}},
        {0x00463580, {"M3dColij_CacheCanTake", 0xff, false, A(G.cache_can_take(S(a[0])))}},
        {0x00463690, {"M3dColij_CacheRemoveOldest", 0, false, V(G.cache_remove_oldest())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "rail layer retail forced calls");
}
