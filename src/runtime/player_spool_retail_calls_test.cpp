// Replays retail forced calls (Phase 4.11a2) through the native player spool code:
//
//   opentony_player_spool_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the pspool_specs groups) holds the retail memory regions the ported
// code touches and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "file_loader.hpp"
#include "player_spooler.hpp"
#include "pre_manager.hpp"
#include "skater_spool.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    pspool::PlayerSpooler spooler;
    skaterspool::SkaterSpool skater;
    pre::PreManager pre;
    fileio::FileLoader files;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), spooler(mem, svc), skater(mem, svc, spooler), pre(mem, svc), files(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using PlayerAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }

const std::map<std::uint32_t, PlayerAdapter>& adapters() {
    static const std::map<std::uint32_t, PlayerAdapter> table = {
        // ---- the manager (spooler.cpp) ----
        {0x004b50e0, {"PlayerSpool_Create", 0xffffffff, false, A(c.spooler.create())}},
        {0x004b5190, {"PlayerSpool_Destroy", 0, false, V(c.spooler.destroy())}},
        {0x004b51b0, {"PlayerSpool_RingOrder", 0, false, V(c.spooler.ring_order(a[0], S(a[1]), S(a[2])))}},
        {0x004b5200, {"PlayerSpool_Construct", 0xffffffff, false, A(c.spooler.construct(self))}},
        {0x004b5240, {"PlayerSpool_ScalarDelete", 0xffffffff, false, A(c.spooler.scalar_delete(self, a[0]))}},
        {0x004b5260, {"PlayerSpool_Destruct", 0, false, V(c.spooler.destruct(self))}},
        {0x004b52b0, {"PlayerSpool_Reset", 0, false, V(c.spooler.reset(self))}},
        {0x004b5300, {"PlayerSpool_ProcessPending", 0, false, V(c.spooler.process_pending(self))}},
        {0x004b5350, {"PlayerSpool_Finalize", 0, false, V(c.spooler.finalize(self))}},
        {0x004b5370, {"PlayerSpool_QueueResource", 0xffffffff, false, A(c.spooler.queue(self, a[0], a[1], a[2], a[3]))}},
        {0x004b5580, {"PlayerSpool_ForceFinalize", 0, false, V(c.spooler.force_finalize(self))}},
        {0x004b55a0, {"PlayerSpool_IsIdle", 0xff, false, A(c.spooler.idle(self))}},
        {0x004b55c0, {"PlayerSpool_Pump", 0, false, V(c.spooler.pump(self))}},
        {0x004b5620, {"PlayerSpool_LoadQueuedItem", 0, false, V(c.spooler.load_queued_item(self))}},
        {0x004b57d0, {"PlayerSpool_AdvanceEntry", 0, false, V(c.spooler.advance_entry(self))}},
        {0x004b5810, {"PlayerSpool_HasResource", 0xff, false, A(c.spooler.has_resource(self, a[0]))}},
        {0x004b5830, {"PlayerSpool_Acquire", 0xff, false, A(c.spooler.acquire(self, a[0]))}},
        {0x004b58a0, {"PlayerSpool_EntryReady", 0xff, false, A(c.spooler.entry_ready(self, S(a[0])))}},
        {0x004b5920, {"PlayerSpool_TakeBufferNamed", 0xffffffff, false, A(c.spooler.take_buffer_named(self, a[0]))}},
        {0x004b5990, {"PlayerSpool_TakeBuffer", 0xffffffff, false, A(c.spooler.take_buffer(self, S(a[0])))}},
        {0x004b5a00, {"PlayerSpool_ReleaseEntry", 0, false, V(c.spooler.release_entry(self, S(a[0])))}},
        {0x004b5b00, {"PlayerSpool_FindEntry", 0xffffffff, false, A(c.spooler.find_entry(self, a[0], a[1] != 0))}},
        // ---- the skater view's spool state (player_spool.cpp) ----
        {0x004a62b0, {"SkaterSpool_SetSlotBuffer", 0, false, V(c.skater.set_slot_buffer(self, S(a[0]), a[1]))}},
        {0x004a6340, {"SkaterSpool_Preview", 0xffffffff, false, A(c.skater.preview(self, S(a[0])))}},
        {0x004a63b0, {"SkaterSpool_SlotUsable", 0xff, false, A(c.skater.slot_usable(self, S(a[0])))}},
        {0x004a6440, {"SkaterSpool_InstallModel", 0, false, V(c.skater.install_model(self, S(a[0]), S(a[1])))}},
        {0x004a6560, {"SkaterSpool_ResolvePartIndices", 0, false, V(c.skater.resolve_part_indices(self, S(a[0]), a[1], S(a[2]), a[3], S(a[4])))}},
        {0x004a6720, {"SkaterSpool_ApplyPartIndices", 0, false, V(c.skater.apply_part_indices(self, a[0], S(a[1])))}},
        {0x004a67a0, {"SkaterSpool_LoadedCount", 0xffffffff, false, A(c.skater.loaded_count(self))}},
        {0x004a67c0, {"SkaterSpool_Begin", 0, false, V(c.skater.begin(self))}},
        {0x004a6880, {"SkaterSpool_Update", 0, false, V(c.skater.update(self, S(a[0])))}},
        {0x004a68f0, {"SkaterSpool_FinishLoads", 0, false, V(c.skater.finish_loads(self))}},
        {0x004a6bf0, {"PlayerSpool_LoadResources", 0, false, V(c.skater.load_resources(self, S(a[0])))}},
        // ---- the rest of pre.cpp ----
        {0x004a8f90, {"PRE_DestroyGlobal", 0, false, V(c.pre.destroy_global())}},
        {0x004a9060, {"PRE_ScalarDelete", 0xffffffff, false, A(c.pre.scalar_delete(self, a[0]))}},
        {0x004a9080, {"PRE_Destruct", 0, false, V(c.pre.destruct(self))}},
        {0x004a91d0, {"PRE_RegisterBuffer", 0, false, V(c.pre.register_buffer(self, a[0], a[1]))}},
        {0x004a9510, {"PRE_Find", 0xff, false, A(c.pre.find(self, a[0]))}},
        {0x004a9580, {"PRE_Dump", 0, false, V(c.pre.dump(self, a[0]))}},
        {0x004a95f0, {"PRE_DumpGlobal", 0, false, V(c.pre.dump_global(a[0]))}},
        {0x004a9610, {"PRE_ReturnZero", 0xffffffff, false, A(c.pre.return_zero())}},
        {0x004a9620, {"PRE_NothingA", 0, false, V(c.pre.nothing_a())}},
        {0x004a9630, {"PRE_NothingB", 0, false, V(c.pre.nothing_b())}},
        {0x004a9640, {"PRE_NothingC", 0, false, V(c.pre.nothing_c())}},
        // ---- the run of fileio.cpp's neighbours ----
        {0x00448dd0, {"FileIO_OpenLoad", 0xffffffff, false, A(c.files.open_load(a[0], a[1]))}},
        {0x00448f40, {"FileIO_SetUnusedFlag", 0, false, V(c.files.set_unused_flag(a[0]))}},
        {0x004493f0, {"FileIO_FinishRead", 0, false, V(c.files.finish_read())}},
        {0x004494b0, {"FileIO_Continue", 0, false, V(c.files.file_continue())}},
        {0x004497e0, {"Fade_Reset", 0, false, V(c.files.fade_reset())}},
        {0x00449800, {"Fade_IsIdle", 0xffffffff, false, A(c.files.fade_idle())}},
        {0x00449810, {"Fade_Start", 0, false, V(c.files.fade_start(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x004498a0, {"Fade_StartFlash", 0, false, V(c.files.fade_start_flash(S(a[0]), a[1]))}},
        {0x004498f0, {"Fade_Update", 0, false, V(c.files.fade_update())}},
        {0x004499b0, {"Fade_Draw", 0, false, V(c.files.fade_draw())}},
        {0x00449b40, {"FontTest_Loop", 0, false, V(c.files.font_test_loop())}},
        {0x00449ca0, {"Font_Construct", 0xffffffff, false, A(c.files.font_construct(self))}},
        {0x00449cb0, {"Font_ScalarDelete", 0xffffffff, false, A(c.files.font_scalar_delete(self, a[0]))}},
        {0x0044a060, {"Font_BaseDestruct", 0, false, V(c.files.font_base_destruct(self))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "player spool retail forced calls");
}
