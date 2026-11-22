// Replays retail forced calls (Phase 4.11a) through the native PSX region and spool code:
//
//   opentony_psx_spool_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the psx_specs groups) holds the retail memory
// regions the ported code touches and, for every case, the state, the arguments and what the retail function did; see
// retail_calls_replay.hpp for the format and the comparison.
#include "psx_spool.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    psx::PsxSpool spool;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), spool(mem, svc) {
        // what the probe's stubbed region parser publishes: a raw image that carries a parse block (+0x5c) gives its slot the words of the block
        spool.set_parse_hook([this](std::uint32_t slot) {
            const std::uint32_t base = psx::kSlotBase + slot * psx::kSlotStride;
            const std::uint32_t raw = mem.u32(base + psx::kSlotRaw);
            if (raw == 0 || mem.u32(raw + 0x5c) != 0xb10c0001) {
                return;
            }
            mem.set32(base + psx::kSlotAnimTables, mem.u32(raw + 0x60));
            mem.set32(base + psx::kSlotAnimHierarchy, mem.u32(raw + 0x64));
            mem.set32(base + psx::kSlotModels, mem.u32(raw + 0x68));
            mem.set16(base + psx::kSlotModelCount, mem.u32(raw + 0x6c));
            mem.set32(base + psx::kSlotRgb, mem.u32(raw + 0x70));
            mem.set32(base + psx::kSlotHooks, mem.u32(raw + 0x74));
            mem.set32(base + psx::kSlotWib, mem.u32(raw + 0x78));
            mem.set32(base + psx::kSlotPulse, mem.u32(raw + 0x7c));
            mem.set32(base + psx::kSlotChecksums, mem.u32(raw + 0x80));
        });
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using PsxAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }

const std::map<std::uint32_t, PsxAdapter>& adapters() {
    static const std::map<std::uint32_t, PsxAdapter> table = {
        // ---- scene materials ----
        {0x004b1f70, {"PSX_AllocSceneMaterial", 0xffffffff, false, A(c.spool.alloc_material(a[0]))}},
        {0x004b1fd0, {"PSX_UnlinkMaterial", 0, false, V(c.spool.unlink_material(a[0]))}},
        {0x004b2030, {"PSX_FindSceneMaterial", 0xffffffff, false, A(c.spool.find_material(a[0]))}},
        {0x004b2060, {"PSX_MaterialIteratorBegin", 0, false, V(c.spool.material_iter_begin())}},
        {0x004b2080, {"PSX_MaterialIteratorNext", 0xffffffff, false, A(c.spool.material_iter_next())}},
        {0x004b20f0, {"PSX_FindTrailingTables", 0xffffffff, false, A(c.spool.trailing_tables(a[0]))}},
        {0x004b2120, {"PSX_DecrementMaterialUsage", 0, false, V(c.spool.decrement_material_usage(S(a[0])))}},
        {0x004b2390, {"PSX_FreeUnusedMaterials", 0, false, V(c.spool.free_unused_materials())}},
        {0x004b2440, {"PSX_NothingA", 0, false, V(c.spool.nothing_a())}},
        // ---- the region table ----
        {0x004b1ac0, {"Spool_Init", 0, false, V(c.spool.init())}},
        {0x004b1c20, {"PSX_SaveRegionImage", 0, false, V(c.spool.save_region_image(S(a[0])))}},
        {0x004b1c70, {"PSX_RestoreRegionImage", 0, false, V(c.spool.restore_region_image(S(a[0])))}},
        {0x004b1bb0, {"PSX_FindFreeRegionSlot", 0xffffffff, false, A(c.spool.find_free_slot())}},
        {0x004b1d10, {"PSX_GetModelPointer", 0xffffffff, false, A(c.spool.model_pointer(a[0]))}},
        {0x004b1de0, {"PSX_FindModelIndexByChecksum", 0xffffffff, true, A(c.spool.find_model_index(a[0], S(a[1])))}},
        {0x004b1f40, {"PSX_GetModelNameChecksum", 0xffffffff, false, A(c.spool.model_name_checksum(a[0]))}},
        {0x004b3230, {"PSX_FindLoadedRegionByName", 0xffffffff, false, A(c.spool.find_region_by_name(a[0]))}},
        {0x004b3270, {"PSX_ClearNamedRegion", 0, false, V(c.spool.clear_named_region(a[0]))}},
        {0x004b32b0, {"Spool_ClearAllPSXs", 0, false, V(c.spool.clear_all_psxs())}},
        {0x004b32f0, {"PSX_ClearRegion", 0, false, V(c.spool.clear_region(S(a[0]), a[1], a[2]))}},
        {0x004b3460, {"Spool_ClearUnusedRegions", 0, false, V(c.spool.clear_unused_regions())}},
        {0x004b34b0, {"Spool_ClearEnvironmentRegions", 0, false, V(c.spool.clear_environment_regions())}},
        {0x004b4640, {"PSX_InitFreeSlot", 0xffffffff, false, A(c.spool.init_free_slot())}},
        {0x004b2ac0, {"PSX_AttachEnvironment", 0, false, V(c.spool.attach_environment(S(a[0])))}},
        {0x004b3090, {"PSX_DetachEnvironment", 0, false, V(c.spool.detach_environment(a[0]))}},
        {0x004b3990, {"Spool_GetEnvIndex", 0xffffffff, false, A(c.spool.env_index(S(a[0])))}},
        // ---- the named-resource list ----
        {0x004b34f0, {"BITS_ParseNamedResourceTag", 0, false, V(c.spool.parse_named_resource_tag(a[0], a[1]))}},
        {0x004b3680, {"BITS_FindNamedResource", 0xffffffff, false, A(c.spool.find_named_resource(a[0]))}},
        // ---- animation tables and model files ----
        {0x004b3e50, {"Spool_StripAnim", 0, false, V(c.spool.strip_anim(S(a[0])))}},
        {0x004b40a0, {"PSX_BuildAnimationTables", 0, false, V(c.spool.build_animation_tables(S(a[0]), a[1]))}},
        {0x004b43e0, {"Skater_LoadModelFile", 0xffffffff, false, A(c.spool.load_model_file(a[0], a[1]))}},
        {0x004b46a0, {"PSX_LoadAnimationResource", 0xffffffff, false, A(c.spool.load_animation_resource(a[0], S(a[1])))}},
        {0x004b47a0, {"PlayerSpool_LoadPartManifest", 0, false, A(c.spool.load_part_manifest(a[0], a[1], a[2], a[3], a[4]))}},
        {0x004b4940, {"PlayerSpool_ParsePartsManifest", 0, false, A(c.spool.parse_parts_manifest(a[0], a[1], a[2], a[3], a[4], S(a[5])))}},
        {0x004b4ad0, {"PSX_CollectModelMaterials", 0, false, V(c.spool.collect_model_materials(a[0], a[1], a[2], a[3]))}},
        {0x004b4e00, {"PSX_ScoreMaterialPaletteUsage", 0xffffffff, true, A(c.spool.score_material_palette_usage(a[0]))}},
        {0x004b4e70, {"PSX_SelectMaterialForModel", 0xffffffff, true, A(c.spool.select_material_for_model(a[0]))}},
        {0x004b5010, {"PSX_RelocateDefinitionPointers", 0, false, V(c.spool.relocate_definition_pointers(a[0], a[1]))}},
        {0x004b50a0, {"PlayerSpool_ModelSlot", 0xffffffff, false, A(c.spool.player_model_slot(a[0], a[1]))}},
        // ---- texture references ----
        {0x004b2b70, {"PSX_LoadTextureReferences", 0, false, V(c.spool.load_texture_references(a[0], a[1])), [](Ctx& c) { return Unspecified(c.spool.unspecified().begin(), c.spool.unspecified().end()); }}},
        {0x004b2f70, {"PSX_ClearTextureReferences", 0, false, V(c.spool.clear_texture_references(a[0]))}},
        // ---- the spool queue ----
        {0x004b3750, {"PSX_SpoolQueueCopyName", 0, false, V(c.spool.queue_copy_name(a[0], a[1]))}},
        {0x004b37a0, {"PSX_QueueResource", 0xffffffff, false, A(c.spool.queue_resource(a[0], S(a[1]), a[2], a[3]))}},
        {0x004b39b0, {"PSX_Spooler", 0xffffffff, true, A(c.spool.spooler())}},
        {0x004b3df0, {"PSX_DrainSpooler", 0, false, V(c.spool.drain_spooler())}},
        {0x004b3e20, {"PSX_SpoolBusy", 0xff, false, A(c.spool.busy())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "PSX spool retail forced calls");
}
