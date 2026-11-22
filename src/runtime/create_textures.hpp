#pragma once

#include "create_services.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime {

// Phase 4.9: the create-a-skater texture database and texture-set manager (cre_tex.cpp,
// cre_texset.cpp). One 0x6a10-byte manager object holds
//
//   +0x0000  the set holder: +0 vtable, 0x180 set records of 0x44 bytes from +4, the set count at +0x6604
//   +0x6608  the texture database: +0 vtable, 0x100 pointers to 0x1c-byte entries from +4 (kept sorted
//            by name), the entry count at +0x404
//
// A texture-database entry is { +0 index, +8 name[16] (lower case), +0x18 flag }; a set record is
// { +0 id (= its index), +4 root set id or -1, +8 name[16], +0x18 display name[24], +0x30 four entry
// pointers, +0x40 texture count, +0x41 has-second-texture, +0x42 flag, +0x43 group }. Both are built
// as locals in retail and copied whole, so the bytes a local never set (the gaps after a name's NUL,
// the unused pointer slots, stack words) are whatever the stack held: `unspecified()` lists them and
// the replay does not compare them.
namespace create {

constexpr std::uint32_t kSetsOffset = 4;
constexpr std::uint32_t kSetStride = 0x44;
constexpr std::uint32_t kSetCountOffset = 0x6604;
constexpr std::uint32_t kMaxSets = 0x180;
constexpr std::uint32_t kDatabaseOffset = 0x6608;     // from the start of the manager
constexpr std::uint32_t kDatabaseEntries = 4;         // from the start of the database
constexpr std::uint32_t kDatabaseCountOffset = 0x404;
constexpr std::uint32_t kMaxTextures = 0x100;
constexpr std::uint32_t kEntrySize = 0x1c;

// Set record fields.
constexpr std::uint32_t kSetRoot = 4;
constexpr std::uint32_t kSetName = 8;
constexpr std::uint32_t kSetDisplay = 0x18;
constexpr std::uint32_t kSetTextures = 0x30;
constexpr std::uint32_t kSetTextureCount = 0x40;
constexpr std::uint32_t kSetHasSecond = 0x41;
constexpr std::uint32_t kSetFlag = 0x42;
constexpr std::uint32_t kSetGroup = 0x43;

// printf_log format strings (the stub reports only the format's address).
constexpr std::uint32_t kFormatDump = 0x00525bd4;          // "Texture set database contains %d entries:\n"
constexpr std::uint32_t kFormatSet = 0x00525bc0;           // "%s, %s, %d, %d, %d"
constexpr std::uint32_t kFormatSetCount = 0x00525a78;      // "mTextureSetCount = %d\n"
constexpr std::uint32_t kFormatTextureCount = 0x00525a64;  // "mTextureCount = %d\n"

// Assertion messages (addresses of the format strings).
constexpr std::uint32_t kMsgTooManyTextures = 0x005259ac;
constexpr std::uint32_t kMsgTooManySets = 0x00525b60;
constexpr std::uint32_t kMsgSetExists = 0x00525ba0;
constexpr std::uint32_t kMsgTooManyTexparts = 0x00525b80;
constexpr std::uint32_t kMsgNoCurrentId = 0x00525a2c;
constexpr std::uint32_t kMsgNoRoot = 0x00525a18;
constexpr std::uint32_t kMsgNoFile = 0x00525b78;
constexpr std::uint32_t kMsgTextureMissing = 0x00525b40;
constexpr std::uint32_t kMsgCapSkinTone = 0x00525b20;
constexpr std::uint32_t kMsgEmpty = 0x0055fe10;

class TextureSets final {
public:
    TextureSets(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- the texture database (this = the database) -----------------------------------------
    [[nodiscard]] std::uint32_t find_texture(std::uint32_t database, std::uint32_t name);      // 0x0041d860
    [[nodiscard]] std::uint32_t allocate_texture(std::uint32_t database, std::uint32_t entry); // 0x0041d9c0

    // ---- the set holder (this = the holder, the start of the manager) -------------------------
    [[nodiscard]] std::uint32_t find_by_id(std::uint32_t holder, std::int32_t id);                       // 0x0041dc30
    [[nodiscard]] std::int32_t find_by_group(std::uint32_t holder, std::int32_t group);                  // 0x0041dc60
    [[nodiscard]] std::int32_t find_root(std::uint32_t holder, std::int32_t root, std::int32_t id);      // 0x0041dcb0
    [[nodiscard]] std::int32_t step(std::uint32_t holder, std::int32_t root, std::int32_t id, std::int32_t by);  // 0x0041ddb0
    void commit(std::uint32_t holder, std::uint32_t record);                                              // 0x0041ef50
    void load_cretex(std::uint32_t holder, std::uint32_t file_name);                                      // 0x0041dea0
    void dump(std::uint32_t holder);                                                                      // 0x0041f060

    // Bytes the retail functions copied from uninitialised stack into memory during this object's life.
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& unspecified() const noexcept { return unspecified_; }

private:
    struct Record {                       // a set record built in a local, with which bytes were set
        std::array<std::uint8_t, kSetStride> bytes{};
        std::array<bool, kSetStride> defined{};
        void put32(std::uint32_t at, std::uint32_t v);
        void put8(std::uint32_t at, std::uint8_t v);
        void put_string(std::uint32_t at, const std::string& s);   // the string and its NUL
    };

    [[nodiscard]] std::uint32_t allocate_from(std::uint32_t database, std::array<std::uint8_t, kEntrySize> entry,
                                              const std::array<bool, kEntrySize>& defined, bool asserted = false);
    [[nodiscard]] std::uint32_t find_named(std::uint32_t database, const std::string& name);
    void commit_record(std::uint32_t holder, const Record& record);
    [[nodiscard]] std::int32_t strcmp_at(std::uint32_t a, std::uint32_t b);
    [[nodiscard]] std::uint32_t alloc(std::uint32_t size);

    RetailMemory& m_;
    RetailServices& s_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
};

}  // namespace create
}  // namespace opentony::runtime
