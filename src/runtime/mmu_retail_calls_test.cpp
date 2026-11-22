// Replays retail forced calls (Phase 4.14b) through the native memory-card layer (mmu_manager.cpp: the manager's records, names, scan, save and load; mmu_setup.cpp: the set-up functions, the replay card
// helpers, the string helpers and the card backend):
//
//   opentony_mmu_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the group `mmu` of mmu_specs*.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "mmu_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::mmu;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    MmuKit kit;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), kit(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using MmuAdapter = Adapter<Ctx>;

// A(expr): the function returns expr (a full dword). B(expr): it returns a bool (the low byte). V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.kit

const std::map<std::uint32_t, MmuAdapter>& adapters() {
    static const std::map<std::uint32_t, MmuAdapter> table = {
        // ---- the manager ----
        {0x00473d20, {"Mmu_ValidateSaveActionType", 0xffffffff, false, A(G.validate_action_type(S(a[0]), S(a[1])))}},
        {0x00473e50, {"Mmu_ConstructCardState", 0, false, V(G.construct_card_state(self))}},
        {0x00473ee0, {"Mmu_ScalarDelete", 0xffffffff, false, A(G.scalar_delete(self, a[0]))}},
        {0x00473f10, {"Mmu_ResetVtable", 0, false, V(G.reset_vtable(self))}},
        {0x00473f30, {"Mmu_NewestFile", 0xffffffff, false, A(G.newest_file(self))}},
        {0x00474010, {"Mmu_FileCode", 0xffffffff, false, A(G.file_code(self, S(a[0])))}},
        {0x00474100, {"Mmu_EncodeCode", 0, false, V(G.encode_code(a[0], S(a[1])))}},
        {0x004741d0, {"Mmu_FileCount", 0xffffffff, false, A(G.file_count(self))}},
        {0x004741f0, {"Mmu_CountOfType", 0xffffffff, false, A(G.count_of_type(self, S(a[0])))}},
        {0x00474250, {"Mmu_FileRecord", 0xffffffff, false, A(G.file_record(self, S(a[0])))}},
        {0x004742e0, {"Mmu_DisplayName", 0xffffffff, false, A(G.display_name(self, S(a[0])))}},
        {0x00474380, {"Mmu_NameTaken", 0xffffffff, false, A(G.name_taken(self, a[0]))}},
        {0x004744a0, {"Mmu_NameTakenByOther", 0xffffffff, false, A(G.name_taken_by_other(self, a[0], S(a[1])))}},
        {0x004745d0, {"Mmu_FileExists", 0xffffffff, false, A(G.file_exists(self, a[0]))}},
        {0x004746d0, {"Mmu_FindFile", 0xffffffff, false, A(G.find_file(self, a[0]))}},
        {0x00474950, {"Mmu_MakeFileToken", 0, false, V(G.make_file_token(self, a[0], a[1]))}},
        {0x004747d0, {"Mmu_BuildSaveFilename", 0, false, V(G.build_save_filename(self, a[0], S(a[1]), a[2]))}},
        {0x00474a50, {"Mmu_FileType", 0xffffffff, false, A(G.file_type(self, S(a[0])))}},
        {0x00474ae0, {"Mmu_DescribeFile", 0, false, V(G.describe_file(self, S(a[0]), a[1], a[2]))}},
        {0x00474d50, {"Mmu_FreeBlocks", 0xffffffff, false, A(G.free_blocks(self))}},
        {0x00474d70, {"Mmu_ScanSaveFiles", 0xffffffff, false, A(G.scan_save_files(self, S(a[0])))}},
        {0x00475480, {"Mmu_MapCardResult", 0, false, V(G.map_card_result(self, a[0]))}},
        {0x00475620, {"Mmu_GetCardState", 0xffffffff, false, A(G.card_state(self))}},
        {0x00475640, {"Mmu_CheckCardReady", 0xff, false, A(G.card_ready(self))}},
        {0x00475660, {"Mmu_DeleteSaveFile", 0xff, false, A(G.delete_save_file(self, S(a[0])))}},
        {0x00475700, {"Mmu_BuildSaveHeader", 0, false, V(G.build_save_header(a[0], a[1], S(a[2]), a[3], a[4], a[5], a[6]))}},
        {0x00475840, {"Mmu_AddSaveBuffer", 0, false, V(G.add_save_buffer(self, a[0], a[1]))}},
        {0x004758e0, {"Mmu_SaveFile", 0xff, false, A(G.save_file(self, a[0], S(a[1]), S(a[2])))}},
        {0x00476200, {"Mmu_LoadFile", 0xff, false, A(G.load_file(self, S(a[0]), S(a[1])))}},
        // ---- the flags, the registration and the set-up ----
        {0x00479f20, {"Mmu_ScreenFlag8", 0xff, false, A(G.flag_8(self))}},
        {0x00479f40, {"Mmu_ScreenFlag4", 0xff, false, A(G.flag_4(self))}},
        {0x00479f90, {"Mmu_ScreenResult", 0xffffffff, false, A(G.screen_result(self))}},
        {0x0047a030, {"Mmu_RegisterSaveBuffer", 0, false, V(G.register_save_buffer(self, a[0], a[1]))}},
        {0x0047a060, {"MMU_Init", 0, false, V(G.init())}},
        {0x0047a080, {"Mmu_Shutdown", 0, false, V(G.shutdown())}},
        {0x0047a090, {"Mmu_RefreshCardState", 0, false, V(G.refresh_card_state())}},
        {0x0047a0b0, {"Mmu_SetupCareerSaveBuffer", 0, false, V(G.setup_career_save(a[0], a[1], a[2]))}},
        {0x0047a220, {"Mmu_SetupReplaySaveBuffers", 0, false, V(G.setup_replay_save(a[0], a[1], S(a[2]), a[3]))}},
        {0x0047a480, {"Mmu_SetupParkSaveBuffer", 0, false, V(G.setup_park_save(a[0], a[1], a[2], static_cast<std::uint8_t>(a[3])))}},
        {0x0047a6c0, {"Mmu_SetupCareerLoadBuffer", 0, false, V(G.setup_career_load(static_cast<std::uint8_t>(a[0]), static_cast<std::uint8_t>(a[1]), static_cast<std::uint8_t>(a[2])))}},
        {0x0047a8a0, {"Mmu_SetupReplayLoadBuffer", 0, false, V(G.setup_replay_load(a[0]))}},
        {0x0047aa50, {"Mmu_SetupParkLoadBuffer", 0, false, V(G.setup_park_load(a[0], a[1]))}},
        {0x0047ac40, {"Mmu_ScreenExists", 0xff, false, A(G.screen_exists())}},
        {0x0047ac60, {"Mmu_UpdateScreen", 0xffffffff, false, A(G.update_screen())}},
        {0x0047ace0, {"Mmu_DrawScreen", 0, false, V(G.draw_screen())}},
        {0x0047ad50, {"Mmu_ScreenFlagA", 0xff, false, A(G.screen_flag_8())}},
        {0x0047adb0, {"Mmu_ScreenFlagB", 0xff, false, A(G.screen_flag_4())}},
        {0x0047ae10, {"Mmu_FinalizeIngameScreen", 0xffffffff, false, A(G.finalize_screen(S(a[0])))}},
        {0x0047b190, {"Mmu_Nothing", 0, false, V(G.nothing())}},
        // ---- the replay card helpers and the string helpers ----
        {0x0047b1a0, {"Replay_SaveCardBuffers", 0, false, V(G.replay_save_buffers(a[0]))}},
        {0x0047b2d0, {"Replay_LoadHeader", 0xffffffff, false, A(G.replay_load_header(a[0]))}},
        {0x0047b400, {"Replay_LoadValidatedStream", 0xff, false, A(G.replay_load_stream(a[0], a[1], a[2]))}},
        {0x0047b6e0, {"Replay_LoadCardBuffers", 0xffffffff, false, A(G.replay_load_buffers(a[0], a[1], a[2]))}},
        {0x0047b7d0, {"Replay_BuildReturnBufferName", 0, false, V(G.copy_loaded_file_name(a[0]))}},
        {0x0047b850, {"Mmu_CopyParkName", 0, false, V(G.copy_park_name(a[0]))}},
        {0x0047b8d0, {"Mmu_ClearLoadedFlag", 0, false, V(G.clear_loaded_flag())}},
        {0x0047b8e0, {"Mmu_TypeLabel", 0, false, V(G.type_label(a[0], S(a[1])))}},
        {0x0047b9d0, {"Mmu_CopyString", 0, false, V(G.copy_string(a[0], a[1]))}},
        {0x0047ba00, {"Mmu_CopyStringAgain", 0, false, V(G.copy_string(a[0], a[1]))}},
        // ---- the card backend ----
        {0x004e8d40, {"Card_ScanDirectory", 0xffffffff, false, A(G.scan_directory(a[0], a[1], a[2], a[3], S(a[4]), S(a[5])))}},
        {0x004e8f00, {"Card_CreateFile", 0xffffffff, false, A(G.create_file(a[0], a[1]))}},
        {0x004e8f20, {"Card_RemoveFile", 0xff, false, A(G.remove_file(a[0], a[1]))}},
        {0x004e8f40, {"Card_OpenFile", 0xffffffff, false, A(G.open_file(a[0], a[1], a[2]))}},
        {0x004e8f70, {"Card_CloseFile", 0, false, V(G.close_file())}},
        {0x004e8f90, {"Card_ReadOpenFile", 0xffffffff, false, A(G.read_open_file(a[0], S(a[1]), a[2]))}},
        {0x004e9000, {"Card_WriteOpenFile", 0xffffffff, false, A(G.write_open_file(a[0], S(a[1]), a[2]))}},
        {0x004e9070, {"Card_ReadFile", 0xff, false, A(G.read_file(a[0], a[1], a[2], S(a[3]), a[4]))}},
        {0x004e90d0, {"Card_WriteFile", 0xff, false, A(G.write_file(a[0], a[1], a[2], S(a[3]), a[4]))}},
        {0x004e9130, {"Card_Nothing", 0xffffffff, false, A(G.card_nothing())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "memory card layer retail forced calls");
}
