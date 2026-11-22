// Replays retail forced calls (Phase 4.15f) through the native create-a-skater object layer (createobj_*.cpp: the texture database and manager, the part database, the spooler, the
// skater object, the part stitcher's weld, the custom definition holder and the leftovers of the tricks and the front end):
//
//   opentony_createobj_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of cobj_specs*.py) holds the retail memory regions the ported code touches and,
// for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "createobj_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>
#include <string>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::createobj;
using opentony::runtime::pcplatform::kScratchBase;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    CreateObjKit kit;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), kit(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, 0x3000);                    // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using ObjAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.kit

const std::map<std::uint32_t, ObjAdapter>& adapters() {
    static const std::map<std::uint32_t, ObjAdapter> table = {
        // ---- the texture database, the manager and the part database (createobj_small.cpp)
        {0x0041d790, {"TextureDatabase_Construct", 0xffffffff, false, A(G.texture_database_construct(self))}},
        {0x0041d7b0, {"TextureDatabase_ScalarDelete", 0xffffffff, false, A(G.texture_database_scalar_delete(self, a[0]))}},
        {0x0041d7d0, {"TextureDatabase_Destruct", 0, false, V(G.texture_database_destruct(self))}},
        {0x0041db50, {"TextureSetManager_Initialize", 0xffffffff, false, A(G.texture_manager_construct(self))}},
        {0x0041dbc0, {"TextureSetManager_ScalarDelete", 0xffffffff, false, A(G.texture_manager_scalar_delete(self, a[0]))}},
        {0x0041dbe0, {"TextureSetManager_Destroy", 0, false, V(G.texture_manager_destruct(self))}},
        {0x0041f350, {"PartDatabase_ScalarDelete", 0xffffffff, false, A(G.part_database_scalar_delete(self, a[0]))}},
        {0x0041f370, {"PartDatabase_Destroy", 0, false, V(G.part_database_destruct(self))}},
        // ---- the spooler
        {0x00424550, {"Spooler_Construct", 0xffffffff, false, A(G.spooler_construct(self))}},
        {0x004245a0, {"Spooler_ScalarDelete", 0xffffffff, false, A(G.spooler_scalar_delete(self, a[0]))}},
        {0x004245c0, {"Spooler_Destruct", 0, false, V(G.spooler_destruct(self))}},
        {0x00424610, {"Spooler_Clear", 0, false, V(G.spooler_clear(self))}},
        {0x00424630, {"Spooler_Pump", 0xff, false, A(G.spooler_pump(self))}},
        {0x00422b10, {"Create_SpoolTexturesThunk", 0xff, false, A(G.spooler_pump(kSpooler))}},
        {0x00422b20, {"Create_SpoolerUpdate", 0, false, V(G.spooler_clear(kSpooler))}},
        {0x00423b10, {"Create_SpoolerUpdateThunk", 0xffffffff, false, A(G.spooler_construct(kSpooler))}},
        {0x00423bd0, {"Create_ResetSpool", 0, false, V(G.reset_spool())}},
        // ---- the materials and the textures
        {0x0041f900, {"Part_CollectChecksums", 0, false, V(G.collect_checksums(self, a[0], a[1], a[2], a[3]))}},
        {0x00421910, {"CreatePart_SelectMaterialChecksum", 0xffffffff, false, A(G.select_material_checksum(self, a[0], a[1]))}},
        {0x00421ba0, {"CreatePart_FindMaterialChecksum", 0xffffffff, false, A(G.find_material_checksum(self, a[0], a[1], a[2]))}},
        {0x00422020, {"CreatePart_SelectSlotChecksum", 0xffffffff, false, A(G.select_slot_checksum(self, a[0], a[1]))}},
        {0x00420420, {"MySkater__FindBoardChecksum", 0, false, V(G.find_board_checksum(self, a[0], a[1]))}},
        {0x004221a0, {"CreatePart_ApplyTexture", 0, false, V(G.apply_texture(self, a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00422340, {"CreatePart_ApplyPreviewTexture", 0, false, V(G.apply_preview_texture(self, a[0], a[1], a[2], a[3]))}},
        {0x00422b30, {"CreatePart_AttachLoadedBitmap", 0, false, V(G.attach_loaded_bitmap(self, a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x004208a0, {"TextureSet_ApplyBodyGroup", 0, false, V(G.apply_body_group(self, a[0], a[1], a[2]))}},
        {0x00422d80, {"TextureSet_ApplyPreviewGroups", 0, false, V(G.apply_preview_groups(self, a[0], a[1], a[2], a[3]))}},
        // ---- the part database and the skater object
        {0x0041f1e0, {"Create_SpoolTextures", 0xffffffff, false, A(G.part_database_construct(self, a[0], a[1]))}},
        {0x0041f4b0, {"MySkater__ProcessSpoolerEntry", 0, false, V(G.process_spooler_entry(self, a[0]))}},
        {0x0041f7c0, {"MySkater__Free2", 0, false, V(G.find_animation_indexes(self, a[0]))}},
        {0x0041fc80, {"PartDatabase_BuildChecksums", 0, false, V(G.build_checksums(self, a[0]))}},
        {0x0041ff50, {"MySkater__Construct", 0xffffffff, false, A(G.skater_construct(self, a[0], a[1], a[2]))}},
        {0x004200b0, {"MySkater__ScalarDelete", 0xffffffff, false, A(G.skater_scalar_delete(self, a[0]))}},
        {0x004200d0, {"MySkater__Free", 0, false, V(G.skater_destruct(self))}},
        {0x004206c0, {"MySkater__ReleasePartDatabase", 0, false, V(G.release_part_database(self))}},
        {0x00420c10, {"MySkater__AttachRegion", 0, false, V(G.attach_region(self, a[0]))}},
        {0x00420fa0, {"Collision_InitModelCacheSlot", 0, false, V(G.init_model_cache_slot(self))}},
        {0x00421410, {"MySkater__ResetTextureNames", 0, false, V(G.reset_texture_names(self))}},
        {0x00421460, {"MySkater__AssignModelPartOrder", 0, false, V(G.assign_model_part_order(self, a[0]))}},
        {0x00421690, {"MySkater__StitchParts", 0, false, V(G.stitch_parts(self))}},
        {0x00422e30, {"MySkater__ApplyBodyScale", 0, false, V(G.apply_body_scale(self, a[0]))}},
        {0x00422ec0, {"MySkater__Delete", 0, false, V(G.skater_delete(a[0]))}},
        {0x00423be0, {"CustomSkater_BuildObject", 0, false, V(G.build_object(a[0], a[1]))}},
        // ---- the part stitcher
        {0x00424900, {"PartStitcher_Construct", 0xffffffff, false, A(G.stitcher_construct(self, a[0], a[1], a[2]))}},
        {0x00424a60, {"PartStitcher_ScalarDelete", 0xffffffff, false, A(G.stitcher_scalar_delete(self, a[0]))}},
        {0x00424a80, {"PartStitcher_Destroy", 0, false, V(G.stitcher_destruct(self))}},
        {0x00425100, {"PartStitcher_Finish", 0, false, V((void)0)}},
        {0x004252c0, {"PartStitcher_Weld", 0, false, V(G.weld(a[0], a[1], a[2], a[3]))}},
        {0x004254c0, {"PartStitcher_WeldPair", 0, false, V(G.weld_pair(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        // ---- the custom definition holder and the custom skaters
        {0x0042f880, {"CustomDefinition_Construct", 0xffffffff, false, A(G.definition_construct(self))}},
        {0x0042f8b0, {"CustomDefinition_ScalarDelete", 0xffffffff, false, A(G.definition_scalar_delete(self, a[0]))}},
        {0x0042f8d0, {"CustomDefinition_Destroy", 0, false, V(G.definition_destruct(self))}},
        {0x0042f950, {"CustomSkater_LoadDefinitionPsx", 0, false, V(G.load_definition_psx(self, a[0], a[1], a[2]))}},
        {0x0042fa10, {"CustomSkaters_LoadAll", 0, false, V(G.load_all_definitions(self))}},
        {0x004236d0, {"CustomSkaters_InitAll", 0, false, V(G.init_all_custom_skaters())}},
        {0x00423800, {"CustomSkater_Create", 0xffffffff, false, A(G.custom_skater_create(a[0], a[1], a[2], a[3], a[4]))}},
        {0x00423b50, {"CustomSkaters_FreeAll", 0xffffffff, false, A(G.free_all_custom_skaters(a[0], a[1]))}},
        // ---- the bindings screen and the career loading screen
        {0x0041c2d0, {"Game_MainLoop", 0xffffffff, false, A(G.keyconfig_run())}},
        {0x0041c490, {"KeyConfig_Nothing", 0, false, V((void)0)}},
        {0x0041c4a0, {"KeyConfig_Done", 0xff, false, A(G.keyconfig_done(self))}},
        {0x0041c4b0, {"Create_LoadPSH", 0xffffffff, false, A(G.keyconfig_construct(self))}},
        {0x0041cda0, {"KeyConfig_ScalarDelete", 0xffffffff, false, A(G.keyconfig_scalar_delete(self, a[0]))}},
        {0x0041cdc0, {"KeyConfig_Destruct", 0, false, V(G.keyconfig_destruct(self))}},
        {0x0041ceb0, {"KeyConfig_SetRow", 0, false, V(G.keyconfig_set_row(self, a[0], a[1]))}},
        {0x0041d060, {"KeyConfig_Update", 0, false, V(G.keyconfig_update(self))}},
        {0x0041d0f0, {"KeyConfig_Step", 0, false, V(G.keyconfig_step(self))}},
        {0x0041d4f0, {"KeyConfig_SetButton", 0, false, V(G.keyconfig_set_button(a[0], a[1]))}},
        {0x0041d580, {"KeyConfig_Assign", 0, false, V(G.keyconfig_assign(a[0], a[1], a[2]))}},
        {0x0041d6f0, {"KeyConfig_Poll", 0xff, false, A(G.keyconfig_poll(self, a[0]))}},
        {0x00415b30, {"Career_LoadingScreen", 0, false, V(G.career_loading_screen(a[0]))}},
        // ---- the record bank, the singleton, strings and the tricks
        {0x004b0b50, {"RecordBank_Construct", 0xffffffff, false, A(G.record_bank_construct(self))}},
        {0x004b0b90, {"RecordBank_ScalarDelete", 0xffffffff, false, A(G.record_bank_scalar_delete(self, a[0]))}},
        {0x004b0bb0, {"RecordBank_Destruct", 0, false, V(G.record_bank_destruct(self))}},
        {0x004c43b0, {"Singleton_ConstructGlobal", 0xffffffff, false, A(G.singleton_construct_global())}},
        {0x004c43e0, {"Singleton_Destruct", 0, false, V(G.singleton_destruct(self))}},
        {0x004c43f0, {"Singleton_Construct", 0xffffffff, false, A(G.singleton_construct(self))}},
        {0x004c4400, {"Singleton_ScalarDelete", 0xffffffff, false, A(G.singleton_scalar_delete(self, a[0]))}},
        {0x004b8340, {"String_ToLowerInPlace", 0, false, V(G.string_to_lower(a[0]))}},
        {0x004b8360, {"Char_IsBlank", 0xffffffff, false, A(G.is_blank(a[0]))}},
        {0x004b8380, {"LevelEvent_SelectPlayers", 0, false, V(G.select_versus_players())}},
        {0x004b83b0, {"Mess_AddViewportMessage", 0, false, V(G.add_viewport_message(a[0], a[1]))}},
        {0x00416050, {"TrickConfig_FindByCharacter", 0xffffffff, false, A(G.find_trick_config(a[0]))}},
        {0x004bb770, {"TrickManager_DeletingDestructor", 0xffffffff, false, A(G.trick_manager_scalar_delete(self, a[0]))}},
        {0x004bb790, {"TrickManager_Destructor", 0, false, V(G.trick_manager_destruct(self))}},
        {0x004bd1e0, {"TrickRuntime_BuildPlayerTables", 0, false, V(G.build_player_tables())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "create-a-skater object layer retail forced calls");
}
