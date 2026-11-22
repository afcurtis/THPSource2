#pragma once

#include "pc_platform.hpp"

#include <cmath>
#include <cstdint>

namespace opentony::runtime::collision {

// Phase 4.15a: the M3D collision layer (m3dcolij.cpp and the collision/transform code at 0x004f39f0..0x004f5f4f). Like the rest of Phase 4 the functions work on the retail memory at the retail
// addresses and everything outside them is a service reported by address.

namespace globals {

constexpr std::uint32_t kCacheModelA = 0x00533ab8;         // the cached model slots (-1: none)
constexpr std::uint32_t kCacheModelB = 0x00533abc;
constexpr std::uint32_t kCacheCountA = 0x00567c88;
constexpr std::uint32_t kCacheCountB = 0x00567c8c;
constexpr std::uint32_t kCacheEntries = 0x00567a70;        // 20 entries of 0x10 bytes (the first word of each is cleared)
constexpr std::uint32_t kCacheEntriesEnd = 0x00567bb0;
constexpr std::uint32_t kQueryGeneration = 0x00533ab4;     // CollisionQueryGeneration (a word)
constexpr std::uint32_t kEnvironmentList = 0x0056db28;     // PsxEnvironmentObjectListHead
constexpr std::uint32_t kGameList = 0x0056af40;            // GameObjectListHead
constexpr std::uint32_t kQueryFlagA = 0x00567c78;          // set by the callers that query the world on their own behalf
constexpr std::uint32_t kQueryFlagB = 0x00567c7c;

}  // namespace globals

constexpr std::uint32_t kInitCollisionQuery = 0x004624d0;  // M3D_InitCollisionQuery(info)
constexpr std::uint32_t kZoneCollision = 0x00466090;       // M3D_QueryZoneCollision(info, 1)

// the query record (0xa4 bytes; the port builds it in the scratch area): the line is two points, the zone query answers in the fields below
namespace line {

constexpr std::uint32_t kStart = 0x00;           // three dwords
constexpr std::uint32_t kEnd = 0x0c;             // three dwords
constexpr std::uint32_t kHitBody = 0x68;         // dword: the object hit, 0 for none
constexpr std::uint32_t kHitY = 0x70;            // dword: hit position, y
constexpr std::uint32_t kSlope = 0x7a;           // short: the normal's y component
constexpr std::uint32_t kBytes = 0xa4;

}  // namespace line

// where the port builds the query record the retail code keeps in a stack local
constexpr std::uint32_t kInfoScratch = pcplatform::kScratchBase + 0x500;

class CollisionKit : public pcplatform::PcKit {
public:
    CollisionKit(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    void flush_cache();                                                        // 0x004629f0 M3dColij_FlushCache
    void advance_query_generation();                                           // 0x00462490 M3D_AdvanceCollisionQueryGeneration
    void init_collision_query(std::uint32_t record);                           // 0x004624d0 M3D_InitCollisionQuery (cdecl; collision_query.cpp)
    void prepare_collision_objects(std::uint32_t objects, std::uint32_t record);  // 0x004628f0 M3D_PrepareCollisionObjectList (cdecl)
    void collision_cell(std::uint32_t cell, std::uint32_t record);             // 0x004638d0 M3D_CollisionCell (cdecl)
    void collision_face_test(std::uint32_t item, std::uint32_t record, std::uint32_t entry);  // 0x00462a20 M3D_CollisionFaceTest (cdecl)
    std::uint32_t commit_collision_surface(std::uint32_t record);              // 0x00463d50 M3D_CommitCollisionSurface (cdecl)
    std::uint32_t read_surface_flags(std::uint32_t record);                    // 0x0048ea80 M3D_ReadSurfaceFlags (cdecl)
    std::uint32_t query_zone_collision(std::uint32_t record, std::uint32_t objects);  // 0x00466090 M3D_QueryZoneCollision (cdecl; collision_zone.cpp)
    void zone_collision_broadphase(std::uint32_t record, std::uint32_t objects);      // 0x004660b0 M3D_ZoneCollisionBroadphase (cdecl)
    std::uint32_t cache_can_take(std::int32_t faces);                          // 0x00463580 (cdecl): does the face cache have room for this many faces (al)
    void cache_remove_oldest();                                                // 0x00463690 (cdecl)
    std::uint32_t sqrt_truncated(std::uint32_t value) { return static_cast<std::uint32_t>(integer_sqrt(static_cast<std::int32_t>(value))); }   // 0x004f53b0 (the truncated square root of a positive argument)

protected:
    // M3D_InitCollisionQuery + M3D_QueryZoneCollision on the line start -> end: the service fills in three fields of the record (the body it hit, the slope, the height); returns the body
    // (a caller that can run inside another query - the shatter, from a trigger face's commands - keeps its own record, as the retail code's stack local is its own)
    std::uint32_t query_line(const std::uint32_t start[3], const std::uint32_t end[3], std::uint32_t record = kInfoScratch);

    // 0x004f5f10: a * b / c through the x87 (fild, fimul, fidiv) at the physics' 53 bits, truncated (collision_zone.cpp)
    static std::int32_t mul_div(std::int32_t a, std::int32_t b, std::int32_t c);

    // 0x004f53b0: the integer square root (an x87 fsqrt of the 64-bit value, truncated): the root of a positive argument, 0 otherwise
    static std::int32_t integer_sqrt(std::int32_t value) {
        if (value <= 0) {
            return 0;
        }
        auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(value)));
        while (root * root > value) {
            --root;
        }
        while ((root + 1) * (root + 1) <= value) {
            ++root;
        }
        return static_cast<std::int32_t>(root);
    }
};

}  // namespace opentony::runtime::collision
