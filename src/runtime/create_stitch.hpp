#pragma once

#include "create_services.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <vector>

namespace opentony::runtime {

// Phase 4.9: stitching a custom skater's body (create_stitch.cpp). The editor builds a skater from the
// models of a source PSX region (one model per body part) and has to re-join the parts it draws:
// vertices on the seam between two parts come in pairs, a "stitch" vertex on one part and a "ghost"
// vertex on the other that mirrors it. The helpers here keep those pairs consistent while models are
// copied into the build region and while a new pair is added.
//
//   PSX model:   +0x00 flags (bit 0x20: the part is hidden)   +0x02 vertex count   +0x04 normal count
//                +0x06 face count   +0x1c vertices (8 bytes: x, y, z as s16, flags u16: bit 0 stitch,
//                bit 1 ghost), then the normals (8 bytes each, same layout), then the faces (variable
//                length: the top 14 bits of a face's first dword count its dwords)
//   region record: +0x14 pointer to the model pointer table (19 pointers), +0x38 part count (u16)
//   part map:    0x78 pairs of dwords (source part, build part); an unused pair holds -1
//
// The two functions that weld pairs by transforming vertices with the animation pose
// (PartStitcher_Weld, PartStitcher_WeldPair) need the PSX geometry transform and stay retail.
namespace create {

constexpr std::uint32_t kModelVertexCount = 2;
constexpr std::uint32_t kModelNormalCount = 4;
constexpr std::uint32_t kModelFaceCount = 6;
constexpr std::uint32_t kModelVertices = 0x1c;
constexpr std::uint32_t kVertexStride = 8;
constexpr std::uint32_t kVertexFlags = 6;
constexpr std::uint32_t kRegionModelTable = 0x14;
constexpr std::uint32_t kRegionPartCount = 0x38;
constexpr std::uint32_t kMapEntries = 0x78;
constexpr std::uint32_t kStitchModelSlots = 0x13;

constexpr std::uint32_t kMsgNoModel = 0x00526808;
constexpr std::uint32_t kMsgNoRegionToLog = 0x00526834;
constexpr std::uint32_t kMsgNoLogBuffer = 0x00526814;
constexpr std::uint32_t kMsgNoSourceRegionCopy = 0x005268b0;
constexpr std::uint32_t kMsgNoBuildRegionCopy = 0x0052688c;
constexpr std::uint32_t kMsgNoMapping = 0x00526794;
constexpr std::uint32_t kMsgModelLeak = 0x00526874;
constexpr std::uint32_t kMsgPartCount = 0x00526854;
constexpr std::uint32_t kMsgNoRegionToReorder = 0x00526934;
constexpr std::uint32_t kMsgNoRegionToRead = 0x00526910;
constexpr std::uint32_t kMsgNoSourceLog = 0x005268f0;
constexpr std::uint32_t kMsgBadGhost = 0x005268d4;
constexpr std::uint32_t kMsgNoModelToCount = 0x00526958;
constexpr std::uint32_t kMsgNoModelSpecified = 0x00526978;
constexpr std::uint32_t kMsgNoBuildRegion = 0x005267c0;
constexpr std::uint32_t kMsgNoStitchVertex = 0x00526a10;
constexpr std::uint32_t kMsgNoGhostVertex = 0x005269fc;
constexpr std::uint32_t kMsgNoStitchNormal = 0x005269e4;
constexpr std::uint32_t kMsgNoGhostNormal = 0x005269d0;

class CreateStitch final {
public:
    CreateStitch(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // 0x00425110: how many vertices of a model are stitch vertices.
    [[nodiscard]] std::int32_t count_stitch_vertices(std::uint32_t model);
    // 0x00425180: the size in bytes of a model.
    [[nodiscard]] std::uint32_t model_size(std::uint32_t model);
    // 0x00425200 / 0x00425270 / 0x004252a0: the part map in both directions, and membership.
    [[nodiscard]] std::int32_t map_lookup(std::int32_t source, std::uint32_t map);
    [[nodiscard]] std::int32_t map_reverse(std::int32_t build, std::uint32_t map);
    [[nodiscard]] bool map_has(std::int32_t source, std::uint32_t map);
    // 0x00424a90: log[i] = the stitch vertices in all parts of the region before part i.
    void log_stitches(std::uint32_t region, std::uint32_t log);
    // 0x00424bb0: replace the build region's models with copies of the source region's visible models,
    // each placed where the part map sends it.
    void copy_models(std::uint32_t source, std::uint32_t build, std::uint32_t map);
    // 0x00424e40: after the copy, shift every ghost vertex (and its normal) by the change in the running
    // stitch count of the part its stitch vertex lives in.
    void reorder_stitches(std::uint32_t source, std::uint32_t build, std::uint32_t source_log, std::uint32_t map);
    // 0x004257d0: make `stitch_vertex` a stitch vertex and `ghost_vertex` its ghost, numbered by how many stitch
    // vertices precede it, renumbering the ghosts after it; the normals get the same flags and number.
    void add_stitch(std::uint32_t region, std::uint32_t stitch_vertex, std::uint32_t ghost_vertex, std::uint32_t stitch_normal,
                    std::uint32_t ghost_normal);

private:
    [[nodiscard]] std::vector<std::int32_t> stitch_log(std::uint32_t region);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace create
}  // namespace opentony::runtime
