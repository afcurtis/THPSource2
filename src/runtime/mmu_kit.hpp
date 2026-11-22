#pragma once

#include "heap_kit.hpp"
#include "pc_platform.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime::mmu {

// Phase 4.14b: the memory-card layer (mmu.cpp, 0x00473d20..0x0047ba00, and its backend 0x004e8d40..0x004e9130). The PC build keeps the PlayStation's save-card interface: a `MmuManager` object (the
// card manager and the save screen are one 0xdd0-byte object, its vtable 0x005192e4) lists the `*.SAV` files of the card directory, hands out the 16-bit-checked buffers of the game state to write or
// read, and moves them through a file backend emulating libcard (`0x004e8f00..0x004e90d0`). This class is the manager and the layers around it that are not the screen: the record table, the
// file names and codes, the scan, the save and the load, the setup functions that build the manager for each kind of save, the replay card helpers and the backend. Like the rest of Phase 4
// the functions work on the retail memory at the retail addresses; everything outside the 4.14b functions (the C runtime's directory and stat calls, the file backend's primitives, the career,
// replay and window engine) is a named service, reported in order and answered from the scripted values of the replay: `engine_<address>` for a retail function, `crt_<name>` for the C runtime.
namespace manager {

constexpr std::uint32_t kRecords = 0x004;          // 15 file records of 0x94 bytes: the file name (0x80), +0x84 flags, +0x88 the size in bytes (the directory scan leaves 0x2000 there)
constexpr std::uint32_t kRecordStride = 0x94;
constexpr std::uint32_t kRecordSize = 0x88;
constexpr std::uint32_t kTypes = 0x8b0;            // dword per record: 1 career, 2 replay, 3 park, 4 other
constexpr std::uint32_t kNames = 0x928;            // 0x21 bytes per record: the display name
constexpr std::uint32_t kNameStride = 0x21;
constexpr std::uint32_t kCount = 0xb18;
constexpr std::uint32_t kState = 0xb1c;
constexpr std::uint32_t kFreeBlocks = 0xb20;
constexpr std::uint32_t kBufferCount = 0xb28;
constexpr std::uint32_t kBuffers = 0xb2c;          // four pointers
constexpr std::uint32_t kBufferSizes = 0xb3c;      // four sizes
constexpr std::uint32_t kObjectSize = 0xdd0;       // the whole screen object (the global MmuManager); the card manager is its sub-object at +0x1f8
constexpr std::uint32_t kInScreen = 0x1f8;

constexpr std::uint32_t kVtable = 0x005192e4;      // the manager's own vtable
constexpr std::uint32_t kBaseVtable = 0x005184b4;  // what the base class leaves behind

}  // namespace manager

namespace globals {

constexpr std::uint32_t kLevelMode = 0x00533f38;           // LevelEventGameModeSelector (7: horse)
constexpr std::uint32_t kCareerLoadFlag = 0x005350fc;      // byte
constexpr std::uint32_t kReturnScreen = 0x00535100;        // dword (the screen a finished save returns to)
constexpr std::uint32_t kReturnScreenLoaded = 0x00535104;  // dword (the screen after a finished load)
constexpr std::uint32_t kCareerSaveImage = 0x00568a68;
constexpr std::uint32_t kCareerImageBytes = 0x1dc4;
constexpr std::uint32_t kCardNameFlag = 0x0056acf8;        // byte: a career file name is known
constexpr std::uint32_t kCardLoadedFlag = 0x0056acf9;      // byte
constexpr std::uint32_t kCardSkipFlag = 0x0056acfa;        // byte
constexpr std::uint32_t kParkNameFlag = 0x0056acfb;        // byte
constexpr std::uint32_t kScreenMode = 0x0056acfc;          // dword
constexpr std::uint32_t kManager = 0x0056ad00;
constexpr std::uint32_t kCareerLoadBuffer = 0x0056ad04;
constexpr std::uint32_t kReplayLoadBuffer = 0x0056ad08;
constexpr std::uint32_t kParkName = 0x0056ab18;            // the last park file name
constexpr std::uint32_t kLoadedFileName = 0x0056ab58;      // the last file loaded
constexpr std::uint32_t kLastParkFile = 0x0056abd8;
constexpr std::uint32_t kLastCareerFile = 0x0056abf8;
constexpr std::uint32_t kParkBuffer = 0x006a141c;          // the custom park to save / load
constexpr std::uint32_t kParkBytes = 0x006a1420;
constexpr std::uint32_t kBackendHandle = 0x0054b304;       // the open card file
constexpr std::uint32_t kCardFlag = 0x006a74bc;            // byte
constexpr std::uint32_t kCardState = 0x006a74c0;           // dword
constexpr std::uint32_t kCardDirectory = 0x006a71bc;       // the card directory prefix (a string)
constexpr std::uint32_t kPathSeparator = 0x00540c34;

}  // namespace globals

// The scratch the stack locals of the retail functions live at in a replay (outside every region a replay compares): what an argument that points into the caller's frame is recorded as.
constexpr std::uint32_t kScratchA = pcplatform::kScratchBase + 0x400;
constexpr std::uint32_t kScratchB = pcplatform::kScratchBase + 0x420;
constexpr std::uint32_t kScratchFind = pcplatform::kScratchBase + 0x440;      // the directory entry the C runtime fills (0x118 bytes)
constexpr std::uint32_t kScratchStat = pcplatform::kScratchBase + 0x560;      // the stat record (0x24 bytes)
constexpr std::uint32_t kScratchPath = pcplatform::kScratchBase + 0x600;      // a path built in a local (0x100 bytes)
constexpr std::uint32_t kScratchHeader = pcplatform::kScratchBase + 0x700;    // the 0x200-byte file header a save builds / a scan reads in a local
constexpr std::uint32_t kScratchLabel = pcplatform::kScratchBase + 0x900;     // a type label in a local (36 bytes)
constexpr std::uint32_t kScratchDescription = pcplatform::kScratchBase + 0x980;   // label + user name (36 bytes)
constexpr std::uint32_t kScratchName = pcplatform::kScratchBase + 0xa00;      // a new file's name built in a local (128 bytes)
constexpr std::uint32_t kScratchToken = pcplatform::kScratchBase + 0xa80;     // the 4-character token of a file name (8 bytes)

class MmuKit final : public pcplatform::PcKit {
public:
    MmuKit(RetailMemory& memory, RetailServices& services) : PcKit(memory, services), heap_(memory, services) {}

    // ---- the manager: records, names, codes ----
    std::uint32_t validate_action_type(std::int32_t type, std::int32_t mode);                        // 0x00473d20 Mmu_ValidateSaveActionType
    void construct_card_state(std::uint32_t self);                                                   // 0x00473e50 (the card part of the manager's constructor)
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flags);                            // 0x00473ee0
    void reset_vtable(std::uint32_t self);                                                           // 0x00473f10
    std::int32_t newest_file(std::uint32_t self);                                                    // 0x00473f30 (the most recently written file)
    std::int32_t file_code(std::uint32_t self, std::int32_t index);                                  // 0x00474010
    void encode_code(std::uint32_t buffer, std::int32_t code);                                       // 0x00474100
    std::uint32_t file_count(std::uint32_t self);                                                    // 0x004741d0
    std::uint32_t count_of_type(std::uint32_t self, std::int32_t type);                              // 0x004741f0
    std::uint32_t file_record(std::uint32_t self, std::int32_t index);                               // 0x00474250
    std::uint32_t display_name(std::uint32_t self, std::int32_t index);                              // 0x004742e0
    bool name_taken(std::uint32_t self, std::uint32_t name);                                         // 0x00474380
    bool name_taken_by_other(std::uint32_t self, std::uint32_t name, std::int32_t skip);             // 0x004744a0
    bool file_exists(std::uint32_t self, std::uint32_t name);                                        // 0x004745d0
    std::int32_t find_file(std::uint32_t self, std::uint32_t name);                                  // 0x004746d0
    void build_save_filename(std::uint32_t self, std::uint32_t out, std::int32_t type, std::uint32_t name);   // 0x004747d0 Mmu_BuildSaveFilename
    void make_file_token(std::uint32_t self, std::uint32_t name, std::uint32_t out);                 // 0x00474950
    std::uint32_t file_type(std::uint32_t self, std::int32_t index);                                 // 0x00474a50
    void describe_file(std::uint32_t self, std::int32_t index, std::uint32_t description, std::uint32_t blocks_out);   // 0x00474ae0
    std::uint32_t free_blocks(std::uint32_t self);                                                   // 0x00474d50
    std::int32_t scan_save_files(std::uint32_t self, std::int32_t type);                             // 0x00474d70 Mmu_ScanSaveFiles
    void map_card_result(std::uint32_t self, std::uint32_t result_out);                              // 0x00475480 Mmu_MapCardResult
    std::uint32_t card_state(std::uint32_t self);                                                    // 0x00475620 Mmu_GetCardState
    bool card_ready(std::uint32_t self);                                                             // 0x00475640 Mmu_CheckCardReady
    bool delete_save_file(std::uint32_t self, std::int32_t index);                                   // 0x00475660 Mmu_DeleteSaveFile
    void build_save_header(std::uint32_t out, std::uint32_t blocks, std::int32_t type, std::uint32_t block_a, std::uint32_t block_b, std::uint32_t block_c, std::uint32_t name);   // 0x00475700
    void add_save_buffer(std::uint32_t self, std::uint32_t buffer, std::uint32_t size);              // 0x00475840 Mmu_AddSaveBuffer
    bool save_file(std::uint32_t self, std::uint32_t name, std::int32_t index, std::int32_t type);   // 0x004758e0 Mmu_SaveFile
    bool load_file(std::uint32_t self, std::int32_t index, std::int32_t type);                       // 0x00476200 Mmu_LoadFile

    // ---- the manager's flags, the registration and the set-up for each kind of save ----
    bool flag_8(std::uint32_t self);                                                                 // 0x00479f20
    bool flag_4(std::uint32_t self);                                                                 // 0x00479f40
    std::uint32_t screen_result(std::uint32_t self);                                                 // 0x00479f90
    void register_save_buffer(std::uint32_t screen, std::uint32_t buffer, std::uint32_t size);       // 0x0047a030 Mmu_RegisterSaveBuffer (a method of the screen: the card manager is its sub-object at +0x1f8)
    void init();                                                                                     // 0x0047a060 MMU_Init
    void shutdown();                                                                                 // 0x0047a080
    void refresh_card_state();                                                                       // 0x0047a090
    void setup_career_save(std::uint32_t return_a, std::uint32_t return_b, std::uint32_t argument);  // 0x0047a0b0 Mmu_SetupCareerSaveBuffer
    void setup_replay_save(std::uint32_t return_a, std::uint32_t return_b, std::int32_t players, std::uint32_t argument);   // 0x0047a220 Mmu_SetupReplaySaveBuffers
    void setup_park_save(std::uint32_t buffer, std::uint32_t size, std::uint32_t argument, std::uint8_t flag);   // 0x0047a480 Mmu_SetupParkSaveBuffer
    void setup_career_load(std::uint8_t a, std::uint8_t b, std::uint8_t c);                          // 0x0047a6c0 Mmu_SetupCareerLoadBuffer
    void setup_replay_load(std::uint32_t argument);                                                  // 0x0047a8a0 Mmu_SetupReplayLoadBuffer
    void setup_park_load(std::uint32_t buffer, std::uint32_t size);                                  // 0x0047aa50 Mmu_SetupParkLoadBuffer
    bool screen_exists();                                                                            // 0x0047ac40
    std::uint32_t update_screen();                                                                   // 0x0047ac60
    void draw_screen();                                                                              // 0x0047ace0
    bool screen_flag_8();                                                                            // 0x0047ad50
    bool screen_flag_4();                                                                            // 0x0047adb0
    std::uint32_t finalize_screen(std::int32_t key);                                                 // 0x0047ae10 Mmu_FinalizeIngameScreen
    void nothing() {}                                                                                // 0x0047b190

    // ---- the replay card helpers and the string helpers ----
    void replay_save_buffers(std::uint32_t name);                                                    // 0x0047b1a0 Replay_SaveCardBuffers
    std::int32_t replay_load_header(std::uint32_t name);                                             // 0x0047b2d0 Replay_LoadHeader
    bool replay_load_stream(std::uint32_t buffer_a, std::uint32_t buffer_b, std::uint32_t name);     // 0x0047b400 Replay_LoadValidatedStream
    std::uint32_t replay_load_buffers(std::uint32_t buffer_a, std::uint32_t buffer_b, std::uint32_t name);   // 0x0047b6e0 Replay_LoadCardBuffers
    void copy_loaded_file_name(std::uint32_t out);                                                   // 0x0047b7d0 Replay_BuildReturnBufferName
    void copy_park_name(std::uint32_t out);                                                          // 0x0047b850
    void clear_loaded_flag();                                                                        // 0x0047b8d0
    void type_label(std::uint32_t out, std::int32_t type);                                           // 0x0047b8e0
    void copy_string(std::uint32_t from, std::uint32_t to);                                          // 0x0047b9d0 and 0x0047ba00

    // ---- the card backend: libcard over files ----
    std::uint32_t scan_directory(std::uint32_t unused, std::uint32_t pattern, std::uint32_t records, std::uint32_t count_out, std::int32_t first, std::int32_t last);   // 0x004e8d40
    std::uint32_t create_file(std::uint32_t unused, std::uint32_t name);                             // 0x004e8f00
    std::uint32_t remove_file(std::uint32_t unused, std::uint32_t name);                             // 0x004e8f20
    std::uint32_t open_file(std::uint32_t unused, std::uint32_t name, std::uint32_t mode);           // 0x004e8f40
    void close_file();                                                                               // 0x004e8f70
    std::uint32_t read_open_file(std::uint32_t buffer, std::int32_t offset, std::uint32_t size);     // 0x004e8f90
    std::uint32_t write_open_file(std::uint32_t buffer, std::int32_t offset, std::uint32_t size);    // 0x004e9000
    bool read_file(std::uint32_t unused, std::uint32_t name, std::uint32_t buffer, std::int32_t offset, std::uint32_t size);    // 0x004e9070
    bool write_file(std::uint32_t unused, std::uint32_t name, std::uint32_t buffer, std::int32_t offset, std::uint32_t size);   // 0x004e90d0
    std::uint32_t card_nothing() { return 0; }                                                       // 0x004e9130

    // the memory card state service (0x004e8d10, a 4.14a function): its two out words are written to the scratch locals, and its answer is returned
    [[nodiscard]] heap::HeapKit& heap() { return heap_; }

private:
    using Word = std::uint32_t;
    Word card_get_state(Word which, Word state_out, Word flag_out);
    Word create_manager(Word type, Word argument, Word flag);
    Word backend_read(Word handle, Word buffer, Word size);
    Word backend_write(Word handle, Word buffer, Word size);
    void strncpy_zero(Word to, Word from, Word count);
    Word compare_strings(Word a, Word b);
    Word record(Word self, std::int32_t index) const { return self + manager::kRecords + static_cast<Word>(index) * manager::kRecordStride; }
    void assert_index(Word self, std::int32_t index, Word message);
    void set_string_at(Word address, const std::string& text) { set_string(address, text); }
    std::string str(Word address, std::size_t limit = 0x100) const { return m_.cstring(address, limit); }
    void copy_to(Word to, const std::string& text) { set_string(to, text); }
    // the C runtime services
    Word crt_stat(Word path, Word buffer);
    Word crt_findfirst(Word pattern, Word entry);
    Word crt_findnext(Word handle, Word entry);
    void crt_findclose(Word handle);

    heap::HeapKit heap_;
};

}  // namespace opentony::runtime::mmu
