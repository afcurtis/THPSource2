#include "collision_views.hpp"
#include "math_kit.hpp"

// Milestone 10g: the zone query (m3dzone.cpp), written from the machine code. The zone is a grid of cells over x and z (the blockmap at 0x00567f80); the line of a query record (collision_query.cpp
// sets it up) is clipped to the grid and walked cell by cell, each cell handed to 0x004638d0, then the best hit is committed (0x00463d50). The object list (0x004628f0), the cells and the commit are
// services; the x87 mul-div (0x004f5f10) runs here at the 53-bit precision of the physics.

namespace opentony::runtime::collision {

using namespace views;

namespace {

// the zone record at kBlockmap (views.hpp): present, the bounds, the cell size and counts, the cells
constexpr Word kFaceMaskA = 0x00567a60;           // faces with any of these flags are not tested
constexpr Word kFaceMaskB = 0x00567a68;           // faces must have all the flags that are not in this mask
constexpr Word kQueryMode = 0x00567c74;           // set while the camera's lean rays run
constexpr Word kTriggerFlag = 0x00567c80;
constexpr Word kCollisionBusy = 0x00567c84;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kMsgNoZone = 0x00533e20;           // "No zone information"
constexpr Word kMsgIndex = 0x00533e08;            // "Zone index out of range"
const std::string kZoneSource = "H:\\TonyHawk\\Pc2\\m3dzone.cpp";      // 0x00533e34

constexpr Word kPrepareObjects = 0x004628f0;      // M3D_PrepareCollisionObjectList(objects, record)
constexpr Word kCell = 0x004638d0;                // M3D_CollisionCell(cell, record)
constexpr Word kCommit = 0x00463d50;              // M3D_CommitCollisionSurface(record)

inline std::int32_t sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) - static_cast<Word>(b)); }
inline std::int32_t add(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) + static_cast<Word>(b)); }

}  // namespace

std::int32_t CollisionKit::mul_div(std::int32_t a, std::int32_t b, std::int32_t c) {
    using mathkit::x87::Precision;
    const long double product = mathkit::x87::round_to(Precision::k53, static_cast<long double>(a) * b);
    return mathkit::x87::ftol(mathkit::x87::round_to(Precision::k53, product / c));
}

// 0x00466090 (cdecl): the zone query of a record; returns 0
Word CollisionKit::query_zone_collision(Word record, Word objects) {
    zone_collision_broadphase(record, objects);
    return 0;
}

// 0x004660b0 (cdecl): the record and whether the game objects are tested too
void CollisionKit::zone_collision_broadphase(Word record, Word objects) {
    const auto stamp = [&](Word line) {
        write_scalar<Word>(m_, kAssertionLine, line);
        set_string(kAssertionFile, kZoneSource);
    };
    const CollisionZoneGridRecordView zone(m_, kBlockmap);
    const SLineInfoView info(m_, record);
    const auto mask_a = [&] { return read_scalar<Word>(m_, kFaceMaskA); };
    const auto mask_b = [&] { return read_scalar<Word>(m_, kFaceMaskB); };
    stamp(0x106);
    check(zone.present() != 0, kMsgNoZone);
    write_scalar<Word>(m_, kFaceMaskA, 0);
    write_scalar<Word>(m_, kFaceMaskB, 0xffffffff);
    if (read_scalar<Word>(m_, kCollisionBusy) != 0) {
        write_scalar<Word>(m_, kFaceMaskA, 0x400000);
    }
    if (read_scalar<Word>(m_, globals::kQueryFlagB) != 0) {
        write_scalar<Word>(m_, kFaceMaskA, mask_a() ^ 0x400000);
    }
    if (read_scalar<Word>(m_, globals::kQueryFlagA) == 0) {
        write_scalar<Word>(m_, kFaceMaskA, mask_a() ^ 0x200000);
    }
    if (read_scalar<Word>(m_, kQueryMode) != 0) {
        write_scalar<Word>(m_, kFaceMaskB, 0xffefffff);
    }
    if (read_scalar<Word>(m_, kTriggerFlag) != 0) {
        write_scalar<Word>(m_, kFaceMaskB, mask_b() ^ 0x20000);
    }
    if (objects != 0) {
        engine(kPrepareObjects, {read_scalar<Word>(m_, globals::kGameList), record});
    }

    // (retail loops over one zone)
    if (zone.present() != 0) {
        const std::int32_t xmin = zone.min_x();
        const std::int32_t zmin = zone.min_z();
        const std::int32_t xmax = zone.max_x();
        const std::int32_t zmax = zone.max_z();
        const std::int32_t size = zone.cell_divisor();
        std::int32_t x0 = info.start_x();
        std::int32_t x1 = info.end_x();
        std::int32_t z0 = info.start_z();
        std::int32_t z1 = info.end_z();
        // (an index the line's own assertion would catch reads on, as retail does: the address is not range-tested)
        const auto visit = [&](std::int32_t ix, std::int32_t iz) {
            engine(kCell, {read_scalar<Word>(m_, zone.cells_address(static_cast<Word>(ix * static_cast<std::int32_t>(kCellColumn) + iz))), record});
        };
        const bool outside = (x0 < xmin && x1 < xmin) || (x0 > xmax && x1 > xmax) || (z0 < zmin && z1 < zmin) || (z0 > zmax && z1 > zmax);
        if (!outside && x0 == x1 && z0 == z1) {
            // a vertical line: one cell (the far edges belong to the last cells)
            std::int32_t ix = sub(x0, xmin) / size;
            std::int32_t iz = sub(z0, zmin) / size;
            const std::int32_t width = zone.cell_count_x();
            const std::int32_t depth = zone.cell_count_z();
            if (ix == width) {
                --ix;
            }
            if (iz == depth) {
                --iz;
            }
            stamp(0x149);
            check(ix >= 0 && ix < width && iz >= 0 && iz < depth, kMsgIndex);
            visit(ix, iz);
        } else if (!outside) {
            // the line clipped to the grid: first against x (the low side, then the high), then against z
            if (x0 < xmin) {
                z0 = z0 < z1 ? add(z0, mul_div(sub(xmin, x0), sub(z1, z0), sub(x1, x0))) : add(z1, mul_div(sub(x1, xmin), sub(z0, z1), sub(x1, x0)));
                x0 = xmin;
            }
            if (x1 < xmin) {
                z1 = z1 < z0 ? add(z1, mul_div(sub(xmin, x1), sub(z0, z1), sub(x0, x1))) : add(z0, mul_div(sub(x0, xmin), sub(z1, z0), sub(x0, x1)));
                x1 = xmin;
            }
            if (x0 > xmax) {
                z0 = z1 < z0 ? add(z1, mul_div(sub(xmax, x1), sub(z0, z1), sub(x0, x1))) : add(z0, mul_div(sub(x0, xmax), sub(z1, z0), sub(x0, x1)));
                x0 = xmax;
            }
            if (x1 > xmax) {
                z1 = z0 < z1 ? add(z0, mul_div(sub(xmax, x0), sub(z1, z0), sub(x1, x0))) : add(z1, mul_div(sub(x1, xmax), sub(z0, z1), sub(x1, x0)));
                x1 = xmax;
            }
            if (z0 < zmin) {
                x0 = x0 < x1 ? add(x0, mul_div(sub(zmin, z0), sub(x1, x0), sub(z1, z0))) : add(x1, mul_div(sub(z1, zmin), sub(x0, x1), sub(z1, z0)));
                z0 = zmin;
            }
            if (z1 < zmin) {
                x1 = x1 < x0 ? add(x1, mul_div(sub(zmin, z1), sub(x0, x1), sub(z0, z1))) : add(x0, mul_div(sub(z0, zmin), sub(x1, x0), sub(z0, z1)));
                z1 = zmin;
            }
            if (z0 > zmax) {
                x0 = x1 < x0 ? add(x1, mul_div(sub(zmax, z1), sub(x0, x1), sub(z0, z1))) : add(x0, mul_div(sub(z0, zmax), sub(x1, x0), sub(z0, z1)));
                z0 = zmax;
            }
            if (z1 > zmax) {
                x1 = x0 < x1 ? add(x0, mul_div(sub(zmax, z0), sub(x1, x0), sub(z1, z0))) : add(x1, mul_div(sub(z1, zmax), sub(x0, x1), sub(z1, z0)));
                z1 = zmax;
            }

            // the walk: one step along x or z at a time, on the sign of the error term
            std::int32_t run_x = sub(x1, x0);
            std::int32_t step_x = 1;
            if (run_x < 0) {
                run_x = sub(0, run_x);
                step_x = -1;
            }
            std::int32_t run_z = sub(z1, z0);
            std::int32_t step_z = 1;
            if (run_z < 0) {
                run_z = sub(0, run_z);
                step_z = -1;
            }
            const std::int32_t off_x = sub(x0, xmin);
            const std::int32_t off_z = sub(z0, zmin);
            std::int32_t ix = off_x / size;
            std::int32_t ix_end = sub(x1, xmin) / size;
            std::int32_t iz = off_z / size;
            std::int32_t iz_end = sub(z1, zmin) / size;
            std::int32_t frac_x = off_x % size;
            std::int32_t frac_z = off_z % size;
            const std::int32_t width = zone.cell_count_x();
            const std::int32_t depth = zone.cell_count_z();
            if (ix == width) {
                --ix;
                frac_x = add(frac_x, size);
            }
            if (iz == depth) {
                --iz;
                frac_z = add(frac_z, size);
            }
            if (ix_end == width) {
                --ix_end;
            }
            if (iz_end == depth) {
                --iz_end;
            }
            const std::int32_t along_x = mul_div(run_x, frac_z, size);
            const std::int32_t along_z = mul_div(run_z, frac_x, size);
            std::int32_t error = add(x0 < x1 ? sub(along_z, run_z) : sub(0, along_z), z0 < z1 ? sub(run_x, along_x) : along_x);
            const auto inside = [&] { return ix >= 0 && ix < width && iz >= 0 && iz < depth; };
            for (;;) {
                if (ix == ix_end && iz == iz_end) {
                    break;
                }
                if (ix < 0) {
                    break;
                }
                if (!inside()) {
                    break;
                }
                visit(ix, iz);
                if (error >= 0) {
                    ix += step_x;
                    error = sub(error, run_z);
                } else {
                    error = add(error, run_x);
                    iz += step_z;
                }
            }
            if (inside()) {
                visit(ix, iz);
            }
        }
    }
    engine(kCommit, {record});
}

}  // namespace opentony::runtime::collision
