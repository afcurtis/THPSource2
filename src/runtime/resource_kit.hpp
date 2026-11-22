#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::resourcekit {

// Phase 5.4a, continued (7b): the resource layer on the retail memory model. src/assets/{resource,pre,fnt}_runtime keep the same behaviour on value-owned C++ types
// (a byte vector for a loaded file, a std::array of PRE containers); the objects other natives read are retail's: the file layer's words (0x00561518..0x005615e8),
// the PRE manager (0x144 bytes: vtable, sixteen buffer pointers, sixteen 16-byte names) and the font table (eight pointers at 0x0056161c and eight names at
// 0x0056163c). A program with no retail image needs the retail-memory form, so this Kit writes those words as retail does and reaches everything else (the file
// backend, the allocator, the texture layer) by address, as a service the resolver answers.
//
// Assertion bookkeeping (the file name and line that M3dUtils_InBetween's callers store in 0x0056881c/0x00568638) is not reproduced: only a failed check is reported.

// ---- the file layer's words -------------------------------------------------------------------------------------------------
constexpr std::uint32_t kLastName = 0x00561518;         // the name last given to FileIO_OpenResource (a pointer)
constexpr std::uint32_t kDirLength = 0x0056151c;        // length of the base directory at the start of kFileName
constexpr std::uint32_t kReadSize = 0x00561544;         // bytes to read
constexpr std::uint32_t kReadMark = 0x0056155c;
constexpr std::uint32_t kReadHandle = 0x00561560;       // a 16-bit backend handle
constexpr std::uint32_t kNameCopy = 0x00561564;         // the name, copied for the assertion text
constexpr std::uint32_t kSubDirName = 0x00561574;       // FileSubDirName: at most 15 characters, empty or ending in a backslash
constexpr std::uint32_t kFileSize = 0x0056158c;         // FileSize: the size of the file last opened
constexpr std::uint32_t kFileName = 0x00561590;         // FileName: base directory, sub directory, name
constexpr std::uint32_t kReadBuffer = 0x005615c8;
constexpr std::uint32_t kReadDone = 0x005615cc;
constexpr std::uint32_t kLoading = 0x005615d0;
constexpr std::uint32_t kReadPending = 0x005615d4;      // an opened file waits to be loaded
constexpr std::uint32_t kPlayerSpoolBusy = 0x005615dc;  // byte: the player spool manager owns the file layer
constexpr std::uint32_t kLoadFromPre = 0x005615e0;      // the file is a PRE-embedded copy (the same word Phase 4 calls the spooler flag)
constexpr std::uint32_t kPreData = 0x005615e4;          // where the embedded copy is
constexpr std::uint32_t kPreSize = 0x005615e8;          // its size
constexpr std::uint32_t kVerbose = 0x0056b048;          // byte: log the files read

// ---- the PRE manager --------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kPreManager = 0x0056b834;       // the one manager (a pointer)
constexpr std::uint32_t kPreSlots = 16;
constexpr std::uint32_t kPreVtable = 0x005196a0;
constexpr std::uint32_t kPreSlotName = 0x0055fe10;      // the string every slot's name starts as

// ---- the font table -----------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kFontSlots = 8;
constexpr std::uint32_t kFontTable = 0x0056161c;        // eight font object pointers
constexpr std::uint32_t kFontNames = 0x0056163c;        // eight 16-byte names
constexpr std::uint32_t kFontVtable = 0x005189f4;
constexpr std::uint32_t kPalX = 0x0056b6f8;             // pal16x
constexpr std::uint32_t kPalY = 0x0056b4f4;             // pal16y
constexpr std::uint32_t kLastClut = 0x00562010;         // what the last texture record creation answered

// ---- the texture records ------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kRecordCount = 0x006a04d4;      // records allocated
constexpr std::uint32_t kRecordHead = 0x006a051c;       // the first record of the list
constexpr std::uint32_t kRecordTail = 0x006a0520;       // the last
constexpr std::uint32_t kRecordSize = 0x2c;             // +0x24 next, +0x28 previous

class ResourceKit final {
public:
    ResourceKit(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- fileio.cpp ----
    std::uint32_t open_resource(std::uint32_t name);                                    // 0x00449030: the size
    void load_resource(std::uint32_t destination);                                      // 0x00449230
    void sync_resource();                                                               // 0x00449660
    void add_dir_to_file_name(std::uint32_t name);                                      // 0x00448d00
    void add_sub_dir_to_file_name(std::uint32_t name);                                  // 0x00448d30
    void set_sub_dir(std::uint32_t sub_dir);                                            // 0x00449700
    std::uint32_t file_exists(std::uint32_t name);                                      // 0x00449460
    void file_init();                                                                   // 0x00448f50
    [[nodiscard]] std::uint32_t get_sub_dir() const { return kSubDirName; }             // 0x004496f0
    [[nodiscard]] std::uint32_t find_no_case(std::uint32_t haystack, std::uint32_t needle);   // 0x004b82b0

    // ---- PRE.cpp ----
    void create_manager();                                                              // 0x004a8ee0
    std::uint32_t initialize_manager(std::uint32_t self);                               // 0x004a9000
    std::uint32_t find_embedded(std::uint32_t self, std::uint32_t name, std::uint32_t size_out);   // 0x004a9410
    std::uint32_t find_entry_in_buffer(std::uint32_t buffer, std::uint32_t name, std::uint32_t size_out);   // 0x004a9480
    void load_into_slot(std::uint32_t self, std::uint32_t name, std::uint32_t heap);    // 0x004a9110
    void load(std::uint32_t self, std::uint32_t name, std::uint32_t heap);              // 0x004a90f0
    void unload(std::uint32_t self, std::uint32_t name);                                // 0x004a9330

    // ---- FontTools.cpp ----
    std::uint32_t font_load(std::uint32_t name);                                        // 0x0044ada0
    std::uint32_t font_create(std::uint32_t data, std::uint32_t name);                  // 0x0044aea0
    std::uint32_t font_parse(std::uint32_t self, std::uint32_t data);                   // 0x00449cd0
    std::uint32_t font_find(std::uint32_t name);                                        // 0x0044b000
    void font_unload(std::uint32_t name);                                               // 0x0044b0a0

    // ---- D3D texture records (pc_texture.cpp) ----
    std::uint32_t alloc_record();                                                       // 0x004d9880

private:
    void service(std::uint32_t address, std::vector<std::uint32_t> args = {});
    [[nodiscard]] std::uint32_t service_value(std::uint32_t address);
    // a retail function reported by address, and its answer
    [[nodiscard]] std::uint32_t call(std::uint32_t address, std::vector<std::uint32_t> args = {});
    // a virtual call: the object first, the slot's target named so the resolver finds the method by address
    [[nodiscard]] std::uint32_t virtual_call(std::uint32_t target, std::vector<std::uint32_t> args);
    void put_string(std::uint32_t destination, const std::string& text);
    [[nodiscard]] std::uint32_t font_slot_of(std::uint32_t name);
    [[nodiscard]] std::uint32_t find_entry_text(std::uint32_t buffer, const std::string& name, std::uint32_t size_out);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace opentony::runtime::resourcekit
