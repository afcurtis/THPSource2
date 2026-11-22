#pragma once

#include "create_services.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>

namespace opentony::runtime {

// Phase 4.9: the part database of a custom skater (create.cpp). The editor builds a skater from the
// nineteen body parts of a PSX model region; this database names them, groups them (head, upper body,
// lower body, board), and maps each one to the slot the editor draws it in.
//
//   part database (0x208c bytes, PSX spool layout):
//     +0x0004 + i * 0x34  part i: name at +0, name index in its group +0x10, animation index +0x14,
//                         group +0x18, up to five texture checksums at +0x1c, their count at +0x30
//     +0x1864             part count
//     +0x1868 + i * 0x40 + g * 0x10   group g's i-th group name (16 bytes)
//     +0x1c68 + g * 4     number of names in group g
//     +0x1c78             the PSX region index of the source skater
//
//   region table (retail PsxRegionNamesBySlot): 0x44-byte records from 0x0056d428; a record has the
//   model table pointer at +0x14 (an array of model pointers) and the part count (u16) at +0x38.
//
//   editor object: +0x1c + slot * 4 animation index of each of the 19 slots, +0x84 the editor's
//   region, +0x280 the part database.
namespace create {

constexpr std::uint32_t kPartNameTable = 0x00525db0;     // four NULL-ended lists of part name pointers
constexpr std::uint32_t kRegionTable = 0x0056d428;
constexpr std::uint32_t kRegionStride = 0x44;
constexpr std::uint32_t kRegionModels = 0x14;
constexpr std::uint32_t kRegionParts = 0x38;

constexpr std::uint32_t kPartEntries = 4;
constexpr std::uint32_t kPartStride = 0x34;
constexpr std::uint32_t kPartCountOffset = 0x1864;
constexpr std::uint32_t kGroupNames = 0x1868;
constexpr std::uint32_t kGroupCounts = 0x1c68;
constexpr std::uint32_t kPartRegion = 0x1c78;
constexpr std::uint32_t kEditorSlots = 0x1c;
constexpr std::uint32_t kEditorRegion = 0x84;
constexpr std::uint32_t kEditorParts = 0x280;
constexpr std::uint32_t kEditorPalette = 0x88;      // allocated palette buffer; eight handles follow from +0x8c

// Assertion messages.
constexpr std::uint32_t kMsgNoPartName = 0x00526048;
constexpr std::uint32_t kMsgPartNotFound = 0x00526038;
constexpr std::uint32_t kMsgBadBodyGroup = 0x00526204;
constexpr std::uint32_t kMsgNoGroupName = 0x005261f4;
constexpr std::uint32_t kMsgNoPartDatabase = 0x005262c4;
constexpr std::uint32_t kMsgNoModelCount = 0x00526398;
constexpr std::uint32_t kMsgTooManyParts = 0x00526388;
constexpr std::uint32_t kMsgNoPartMap = 0x005263a8;
constexpr std::uint32_t kMsgBadRegion = 0x00526274;

class CreateParts final {
public:
    CreateParts(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // 0x0041f3d0: which group (0 head, 1 upper body, 2 lower body, 3 board) a part name belongs to; -1 if none.
    [[nodiscard]] std::int32_t group_index(std::uint32_t name);
    // 0x0041fe50: index of `name` among group `group`'s names in the database; -1 if absent.
    [[nodiscard]] std::int32_t find_body_group(std::uint32_t database, std::uint32_t name, std::int32_t group);
    // 0x00421040: the slot part `part` is drawn in: pelvis 0, stomach 1, chest 2, head 3, any other part the
    // next free slot from `*counter` (starting at 4, at most 0x13).
    [[nodiscard]] std::int32_t classify_part(std::uint32_t editor, std::int32_t part, std::uint32_t counter);
    // 0x00421250: fill `map` (8 bytes per part: part index, slot or -1) and the editor's slot animations.
    void build_part_map(std::uint32_t editor, std::uint32_t map);
    // 0x004203c0: the editor's region index, asserting it is set.
    [[nodiscard]] std::int32_t checked_region(std::uint32_t editor);
    // 0x004214d0: the address of the editor's region record.
    [[nodiscard]] std::uint32_t region_record(std::uint32_t editor);
    // 0x00420860: free the palette buffer and clear the eight palette handles.
    void free_palettes(std::uint32_t editor);

private:
    [[nodiscard]] std::int32_t compare(std::uint32_t a, std::uint32_t b);
    // classify_part without the pointer to the counter: retail's build_part_map keeps the counter in a local
    [[nodiscard]] std::int32_t classify(std::uint32_t editor, std::int32_t part, std::uint32_t& counter);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace create
}  // namespace opentony::runtime
