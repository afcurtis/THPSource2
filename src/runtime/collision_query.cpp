#include "collision_views.hpp"
#include "math_kit.hpp"

// Milestone 10g: the zone query's record (m3dcolij.cpp), the cell and object tests that fill it and what reads its result, written from the machine code. The GTE helpers (0x004e22b0 the leading-bit count, 0x004e3130 RT x IR, 0x004e39a0
// RT x V0, 0x004e80e0 the matrix of three angles) and the root (0x004f53b0) run here, as the generation counter (0x00462490), the face cache's helpers (0x00463580, 0x00463690) and the x87 mul-div (0x004f5f10)
// do; the culls, the face tests and the trigger commands are services.
//
// 11c: the record is an SLineInfo, the cache entries SColCache, the boxes SFaceCache, the zone's objects RuntimeCollisionObject, the model CollisionModelDataHeader + vertex, normal and face records
// (re/types/collision.yml); what no layout holds (a model pointer table, the item lists of a cell, the trigger command arrays, single globals) is read as scalars.

namespace opentony::runtime::collision {

using namespace views;

namespace {

constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kMsgCacheEnd = 0x00533c88;
constexpr Word kMsgRotated = 0x00533b70;         // "Can't handle rotated objects"
constexpr Word kMsgCacheItem = 0x00533b3c;       // "pColCache->pItem is wrong in linetothisitemcached"
constexpr Word kMsgCacheModel = 0x00533b2c;      // "pModel Wrong"
constexpr Word kMsgCacheFaces = 0x00533b20;      // "Faces Wrong"
constexpr Word kMsgFaceCount = 0x00533b08;       // "Illegal number of faces"
constexpr Word kMsgTriggers = 0x00533aec;        // "Multiple trigger poly hit"
constexpr Word kMsgNoZone = 0x00533ac0;          // "Found a trigger zone not in the environment"
const std::string kColijSource = "H:\\TonyHawk\\Pc2\\m3dcolij.cpp";      // 0x00533b90
constexpr Word kFaceMaskA = 0x00567a60;          // faces with any of these flags are not tested
constexpr Word kFaceMaskB = 0x00567a68;          // faces must have all the flags that are not in this mask
constexpr Word kBestFace = 0x0056438c;
constexpr Word kTriggerFlag = 0x00567c80;
constexpr Word kEnvironmentFlag = 0x0056db2c;
constexpr Word kEnvironmentLists = 0x0056d41c;   // per environment index, the array of trigger commands
// the skater's collision globals M3D_ReadSurfaceFlags sets
constexpr Word kFlagSurface40 = 0x0056b768;
constexpr Word kFlagInverse23 = 0x0056b7a8;
constexpr Word kFlagFace80 = 0x0056b7ac;
constexpr Word kFlagInverse24 = 0x0056b7b8;
constexpr Word kSurfaceClass = 0x0056b7e8;
constexpr Word kSurfaceHit = 0x0056b77c;

constexpr Word kCullObjects = 0x004f4940;        // Collision_CullObjectsAgainstBox(objects, 0, record, generation)
constexpr Word kCullLinkedObjects = 0x004f43e0;  // Collision_CullLinkedObjects(objects, 0, record, generation)
constexpr Word kFaceTest = 0x00462a20;           // M3D_CollisionFaceTest(item, record, cache entry)
constexpr Word kLinkedObjectFaces = 0x00463e50;  // Collision_TestLinkedObjectFaces(object, record)
constexpr Word kGetEnvironmentIndex = 0x004b3990;  // Spool_GetEnvIndex(region) -> the index of the environment of a region (-1: none)
constexpr Word kTriggerCommandPoint = 0x004c4bd0;  // Trig_TriggerCommandPoint(commands, 1)

inline Word shl(Word v, Word n) { return v << (n & 31); }
inline std::int32_t sar(std::int32_t v, Word n) { return v >> (n & 31); }
inline std::int32_t sx16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }
inline std::int32_t div(Word a, Word b) { return static_cast<std::int32_t>(a) / static_cast<std::int32_t>(b); }

}  // namespace

// 0x004624d0 (cdecl): the record's line is start (+0x00) -> end (+0x0c). It clears the results, keeps the length in whole units (+0x44) and a matrix (+0x48, nine shorts) that turns the line onto the z
// axis (loaded into the GTE's RT), the line's box (+0x18 lowest corner, +0x24 highest) and the query generation (+0x8a, after advancing it)
void CollisionKit::init_collision_query(Word record) {
    mathkit::MathKit math(m_, s_);
    const SLineInfoView info(m_, record);
    const RuntimeGteRegisterFileView gte(m_, kRegisters);
    const RuntimeShortVector3View ir = gte.ir();
    const RuntimeQ12MatrixView matrix(m_, info.line_basis_address());
    const RuntimeQ12MatrixView basis(m_, kBasis);
    const Word x0 = line_start(info, 0);
    const Word y0 = line_start(info, 1);
    const Word z0 = line_start(info, 2);
    info.set_hit_distance(0x7fffffff);
    info.set_hit_parameter(0x7fffffff);
    const std::int32_t dx = sx16(static_cast<Word>(static_cast<std::int32_t>(line_end(info, 0) - x0) >> 12));
    const std::int32_t dz = sx16(static_cast<Word>(static_cast<std::int32_t>(line_end(info, 2) - z0) >> 12));
    const Word dy = static_cast<Word>(static_cast<std::int32_t>(line_end(info, 1) - y0) >> 12);
    const Word horizontal = static_cast<Word>(dx) * static_cast<Word>(dx) + static_cast<Word>(dz) * static_cast<Word>(dz);
    info.set_hit_body(GuestPtr<RuntimeCollisionObject>{});
    info.set_hit_face_record(GuestPtr<void>{});
    info.set_hit_model_index(0xffffffffu);
    info.set_query_mask_mode(0);
    info.set_direction_flag(0);
    const auto put = [](const RuntimeQ12MatrixView& to, std::initializer_list<Word> shorts) {
        std::size_t k = 0;
        for (const Word v : shorts) {
            to.set_elements(k++, static_cast<std::int16_t>(v));
        }
    };
    if (horizontal != 0) {
        // slanted: the turn about y (from the horizontal direction) times the tilt about x (from the vertical part), the roots taken on values normalised to an even shift
        math.leading_zero_count(horizontal);
        const Word lz = gte.lzcr() - 1;
        const Word h = static_cast<Word>(static_cast<std::int32_t>(lz) >> 1);
        const Word root_h = sqrt_truncated(shl(horizontal, lz & 0x1e));
        const std::int32_t sin_y = div(shl(static_cast<Word>(dx), h + 12), root_h);
        const std::int32_t cos_y = div(shl(static_cast<Word>(dz), h + 12), root_h);
        const std::int32_t dy16 = sx16(dy);
        const Word total = static_cast<Word>(dy16) * static_cast<Word>(dy16) + horizontal;
        put(basis, {static_cast<Word>(cos_y), 0u, static_cast<Word>(-sin_y), 0u, 0x1000u, 0u, static_cast<Word>(sin_y), 0u, static_cast<Word>(cos_y)});
        math.leading_zero_count(total);
        const Word lt = gte.lzcr() - 1;
        const Word t = static_cast<Word>(static_cast<std::int32_t>(lt) >> 1);
        const Word root_t = sqrt_truncated(shl(total, lt & 0x1e));
        info.set_line_length(sar(static_cast<std::int32_t>(root_t), t));
        const std::int32_t sin_x = div(shl(static_cast<Word>(dy16), t + 12), root_t);
        const std::int32_t cos_x = div(shl(root_h, 12), shl(root_t, h - t));
        put(matrix, {0x1000, 0, 0, 0, static_cast<Word>(cos_x), static_cast<Word>(-sin_x), 0, static_cast<Word>(sin_x), static_cast<Word>(cos_x)});
        copy_matrix(gte.rotation(), matrix);
        // the record's matrix becomes RT x the turn, a column at a time
        for (Word column = 0; column < 3; ++column) {
            for (Word row = 0; row < 3; ++row) {
                set_component(ir, row, basis.elements(3 * row + column));
            }
            math.rtir();
            for (Word row = 0; row < 3; ++row) {
                matrix.set_elements(3 * row + column, static_cast<std::int16_t>(component(ir, row)));
            }
        }
    } else {
        // straight up or down: the length is the vertical part, the matrix a quarter turn about x (+0x89 is set for a line that does not go up)
        Word c = 0x1000;
        if (sx16(dy) < 0) {
            info.set_line_length(-sx16(dy));
            c = 0xfffff000;
        } else {
            info.set_line_length(sx16(dy));
            info.set_direction_flag(1);
        }
        put(matrix, {0x1000, 0, 0, 0, 0, 0u - c, 0, c, 0});
    }
    copy_matrix(gte.rotation(), matrix);
    for (Word axis = 0; axis < 3; ++axis) {
        const std::int32_t a = static_cast<std::int32_t>(line_start(info, axis));
        const std::int32_t b = static_cast<std::int32_t>(line_end(info, axis));
        set_line_low(info, axis, static_cast<Word>(a < b ? a : b));
        set_line_high(info, axis, static_cast<Word>(a < b ? b : a));
    }
    advance_query_generation();
    info.set_query_stamp(read_scalar<std::uint16_t>(m_, globals::kQueryGeneration));
}

// 0x00463d50 (cdecl): for a hit, the best face's normal turned by the angles of the object hit (+0x14) into the record (+0x78, three shorts); 1 for a hit, 0 otherwise
Word CollisionKit::commit_collision_surface(Word record) {
    const SLineInfoView info(m_, record);
    const Word body = info.hit_body().address;
    if (body == 0) {
        return 0;
    }
    mathkit::MathKit math(m_, s_);
    const RuntimeGteRegisterFileView gte(m_, kRegisters);
    const RuntimeIntVector3View best(m_, kBestNormal);
    math.rotation_from_angles(RuntimeCollisionObjectView(m_, body).rotation_address(), kBasis);
    copy_matrix(gte.rotation(), RuntimeQ12MatrixView(m_, kBasis));
    const RuntimeShortVector3View v0 = gte.v0();
    for (Word k = 0; k < 3; ++k) {
        set_component(v0, k, static_cast<std::int32_t>(component(best, k)));      // (the low half of the dword)
    }
    math.rtv0();
    const RuntimeShortVector3View ir = gte.ir();
    for (Word k = 0; k < 3; ++k) {
        info.set_hit_normal(k, static_cast<std::int16_t>(component(ir, k)));
    }
    return 1;
}

// 0x0048ea80 (cdecl): the flags of the surface a record hit (+0x80: its words +0 and +0xc) into the skater's collision globals; returns the surface, or the record when nothing was hit
Word CollisionKit::read_surface_flags(Word record) {
    const SLineInfoView info(m_, record);
    if (info.hit_body().address == 0) {
        return record;
    }
    const Word surface = info.hit_face_record().address;
    const CollisionFacePrefixView face(m_, surface);
    const Word flags = face_flags(face);
    write_scalar<Word>(m_, kFlagSurface40, (flags >> 16) & 0x40);
    write_scalar<Word>(m_, kFlagInverse24, (~flags >> 24) & 1);
    write_scalar<Word>(m_, kFlagInverse23, (~flags >> 23) & 1);
    write_scalar<Word>(m_, kFlagFace80, face_word0(face) & 0x80);
    write_scalar<Word>(m_, kSurfaceClass, (flags >> 25) & 0xf);
    write_scalar<Word>(m_, kSurfaceHit, surface);
    return surface;
}

// 0x004638d0 (cdecl): the items of a zone cell (a list of pointers ending in 0) against the record's line. The objects of the cell are culled against the line's box first (0x004f4940); each item not yet
// seen by this query (the generation stamped at +6) has its model's faces tested (0x00462a20) through the face cache: a model not in the cache gets the next entry (item, model, face count, first face)
// and a box per face (the face word, then the lowest and the highest corner, in world units: the vertices are in whole units about the item's position) in the ring at 0x005643b0
void CollisionKit::collision_cell(Word cell, Word record) {
    const SLineInfoView info(m_, record);
    if (cell == 0 || info.line_length() == 0) {
        return;
    }
    const RuntimeGteRegisterFileView gte(m_, kRegisters);
    copy_matrix(gte.rotation(), RuntimeQ12MatrixView(m_, info.line_basis_address()));
    // (retail loads the generation word into the register that holds the length, so the length's high half goes with it)
    engine(kCullObjects, {cell, 0, record, (static_cast<Word>(info.line_length()) & 0xffff0000u) | info.query_stamp()});
    for (Word at = cell; cell_item(m_, at) != 0; at += 4) {
        const Word item_address = cell_item(m_, at);
        const RuntimeCollisionObjectView item(m_, item_address);
        const std::uint16_t generation = info.query_stamp();
        if (item.query_stamp() == generation) {
            continue;
        }
        item.set_query_stamp(generation);
        Word entry = globals::kCacheEntries;
        while (entry < globals::kCacheEntriesEnd && SColCacheView(m_, entry).item().address != item_address) {
            entry += 0x10;
        }
        if (entry != globals::kCacheEntriesEnd) {
            engine(kFaceTest, {item_address, record, entry});
            continue;
        }

        const Word model = model_of(m_, item);
        const CollisionModelDataHeaderView header(m_, model);
        const Word faces = header.face_count();
        if ((cache_can_take(static_cast<std::int32_t>(faces)) & 0xff) == 0) {
            do {
                cache_remove_oldest();
            } while ((cache_can_take(static_cast<std::int32_t>(faces)) & 0xff) == 0);
        }
        const Word slot = read_scalar<Word>(m_, globals::kCacheCountA);
        const Word first = read_scalar<Word>(m_, globals::kCacheCountB);
        const SColCacheView taken = cache_slot(m_, slot);
        taken.set_item(GuestPtr<RuntimeCollisionObject>{item_address});
        taken.set_model(GuestPtr<void>{model});
        taken.set_face_count(faces);
        taken.set_face_cache_base_index(first);
        if (read_scalar<std::int32_t>(m_, globals::kCacheModelA) == -1) {
            write_scalar<Word>(m_, globals::kCacheModelA, slot);
        }
        if (read_scalar<std::int32_t>(m_, globals::kCacheModelB) == -1) {
            write_scalar<Word>(m_, globals::kCacheModelB, first);
        }
        write_scalar<Word>(m_, globals::kCacheCountB, first + faces);
        Word origin[3];
        for (Word k = 0; k < 3; ++k) {
            origin[k] = static_cast<Word>(item.position_q12(k));
        }
        const Word vertices = model + static_cast<Word>(guest::kCollisionModelDataHeaderSize);
        Word face = vertices + 8 * (Word{header.normal_count()} + Word{header.vertex_count()});
        Word box = kFaceBoxes + 0x1c * first;
        for (Word n = faces; n != 0; --n) {
            const CollisionFacePrefixView face_record(m_, face);
            const SFaceCacheView box_record(m_, box);
            box_record.set_packed_face_word(face_record.base_flags());
            std::int32_t low[3];
            std::int32_t high[3];
            const auto corner = [&](Word index, bool first_corner) {
                const CollisionVertexRecordView v(m_, vertices + 8 * Word{face_record.vertex_indices(index)});
                for (Word k = 0; k < 3; ++k) {
                    const std::int32_t c = component(v, k);
                    if (first_corner) {
                        low[k] = high[k] = c;
                    } else {
                        if (c > high[k]) {
                            high[k] = c;
                        }
                        if (c < low[k]) {
                            low[k] = c;
                        }
                    }
                }
            };
            corner(0, true);
            corner(1, false);
            corner(2, false);
            if ((face_record.base_flags() & 0x10) == 0) {      // (a quad)
                corner(3, false);
            }
            for (Word k = 0; k < 3; ++k) {
                set_box_low(box_record, k, (static_cast<Word>(low[k]) << 12) + origin[k]);
                set_box_high(box_record, k, (static_cast<Word>(high[k]) << 12) + origin[k]);
            }
            box += 0x1c;
            face += face_stride(face_record);
        }
        write_scalar<Word>(m_, kAssertionLine, 0x8a7);
        set_string(kAssertionFile, kColijSource);
        const Word end = read_scalar<Word>(m_, globals::kCacheCountB);
        check(box == kFaceBoxes + 0x1c * end || end == 0, kMsgCacheEnd);
        engine(kFaceTest, {item_address, record, cache_slot(m_, read_scalar<Word>(m_, globals::kCacheCountA)).address()});
        const Word next = read_scalar<Word>(m_, globals::kCacheCountA) + 1;
        write_scalar<Word>(m_, globals::kCacheCountA, next == 0x14 ? 0 : next);
    }
}

// 0x00462a20 (cdecl): the faces of a cell's item against the record's line, through the item's entry in the face cache (the item, its model, the face count, the first face box). A face is tested when its box
// meets the line's (cut to whole units and widened by four), its flags pass the two masks and are not the 0x10000 bit alone (a trigger face, 0x20000, only on a trigger query: +0x88), the line starts on the
// normal's side of its plane and ends on the other (by half a unit at one end at least), and passes inside every edge: the sign of the line's direction against the outer product of two corners, in whole
// units about the start, in the x87 at the physics' 53 bits (a quad's corners go 0, 1, 3, 2). A trigger face marks the item's model; any other face nearer than the best so far (+0x8c: the fraction of the line
// in Q14) is the hit: the point (+0x6c), the item (+0x68), the face (+0x80, also 0x0056438c), the distance along the line (+0x40) and the face's normal (0x00564390). A trigger face hit runs the commands
// of the item's zone
void CollisionKit::collision_face_test(Word item_address, Word record, Word entry) {
    const auto stamp = [&](Word line) {
        write_scalar<Word>(m_, kAssertionLine, line);
        set_string(kAssertionFile, kColijSource);
    };
    const SLineInfoView info(m_, record);
    const RuntimeCollisionObjectView item(m_, item_address);
    const SColCacheView cached(m_, entry);
    stamp(0x1fe);
    check((item.rotation(2) | item.rotation(1) | item.rotation(0)) == 0, kMsgRotated);
    std::int32_t line_low_units[3];
    std::int32_t line_high_units[3];
    for (Word k = 0; k < 3; ++k) {
        line_low_units[k] = static_cast<std::int32_t>((line_low(info, k) & 0xfffff000u) - 0x4000);
        line_high_units[k] = static_cast<std::int32_t>((line_high(info, k) & 0xfffff000u) + 0x4000);
    }
    stamp(0x248);
    check(cached.item().address == item_address, kMsgCacheItem);
    const Word count = cached.face_count();
    Word box = kFaceBoxes + 0x1c * cached.face_cache_base_index();
    const Word model = model_of(m_, item);
    const CollisionModelDataHeaderView header(m_, model);
    const Word vertices = model + static_cast<Word>(guest::kCollisionModelDataHeaderSize);
    const Word normals = vertices + 8 * Word{header.vertex_count()};
    Word face = normals + 8 * Word{header.normal_count()};
    stamp(0x263);
    check(cached.model().address == model, kMsgCacheModel);
    stamp(0x264);
    check(count == header.face_count(), kMsgCacheFaces);
    stamp(0x265);
    check(static_cast<std::int32_t>(count) >= 0 && static_cast<std::int32_t>(count) < 1000, kMsgFaceCount);
    std::int32_t best = info.hit_parameter();
    Word trigger_model = 0xffffffffu;
    if (static_cast<std::int32_t>(count) <= 0) {
        return;
    }

    const auto test_face = [&] {
        const SFaceCacheView bounds(m_, box);
        const CollisionFacePrefixView face_record(m_, face);
        if (bounds.bounds_min_y() > line_high_units[1] || bounds.bounds_max_y() < line_low_units[1] || bounds.bounds_min_x() > line_high_units[0] || bounds.bounds_max_x() < line_low_units[0] ||
            bounds.bounds_min_z() > line_high_units[2] || bounds.bounds_max_z() < line_low_units[2]) {
            return;
        }
        const Word flags = face_flags(face_record);
        if ((flags & read_scalar<Word>(m_, kFaceMaskA)) != 0 || ~(flags | read_scalar<Word>(m_, kFaceMaskB)) != 0 || ((flags ^ 0x10000) & 0x30000) == 0) {
            return;
        }
        if (info.query_mask_mode() == 0 && (flags & 0x20000) != 0) {
            return;
        }
        const CollisionNormalRecordView normal(m_, normals + 8 * ((flags & 0xffff) >> 3));
        const auto corner = [&](Word index, Word out[3]) {
            const CollisionVertexRecordView v(m_, vertices + 8 * Word{face_record.vertex_indices(index)});
            for (Word k = 0; k < 3; ++k) {
                out[k] = static_cast<Word>(component(v, k));
            }
        };
        // the line's ends in whole units about the item's position
        Word start[3];
        Word end[3];
        for (Word k = 0; k < 3; ++k) {
            const Word origin = static_cast<Word>(item.position_q12(k) >> 12);
            start[k] = static_cast<Word>(static_cast<std::int32_t>(line_start(info, k)) >> 12) - origin;
            end[k] = static_cast<Word>(static_cast<std::int32_t>(line_end(info, k)) >> 12) - origin;
        }
        Word v0[3];
        corner(0, v0);
        const auto plane = [&](const Word p[3]) {
            Word d = 0;
            for (Word k = 0; k < 3; ++k) {
                d += (p[k] - v0[k]) * static_cast<Word>(component(normal, k));
            }
            return static_cast<std::int32_t>(d);
        };
        const std::int32_t start_side = plane(start);
        const std::int32_t end_side = plane(end);
        if (start_side < 0 || end_side > 0 || (start_side == 0 && end_side == 0) || (start_side < 0x800 && end_side > -0x800)) {
            return;
        }
        Word v1[3];
        Word v2[3];
        corner(1, v1);
        corner(2, v2);
        const bool quad = (face_record.base_flags() & 0x10) == 0;
        Word v3[3] = {};
        if (quad) {
            corner(3, v3);
        }
        double e[3];
        double a0[3];
        double a1[3];
        double a2[3];
        double a3[3];
        for (Word k = 0; k < 3; ++k) {
            const auto from_start = [&](Word c) { return static_cast<double>(static_cast<std::int32_t>(c - start[k])); };
            e[k] = from_start(end[k]);
            a0[k] = from_start(v0[k]);
            a1[k] = from_start(v1[k]);
            a2[k] = from_start(v2[k]);
            a3[k] = from_start(v3[k]);
        }
        // the line's direction against p x q (one rounding per operation, as the x87 at 53 bits)
        const auto inside = [&](const double p[3], const double q[3]) {
            const double px = p[1] * q[2];
            const double qx = p[2] * q[1];
            const double x = (px - qx) * e[0];
            const double py = p[2] * q[0];
            const double qy = p[0] * q[2];
            const double y = (py - qy) * e[1];
            const double pz = p[0] * q[1];
            const double qz = p[1] * q[0];
            const double z = (pz - qz) * e[2];
            const double xy = x + y;
            return !(xy + z < 0.0);
        };
        if (!inside(a2, a0) || !inside(a0, a1)) {
            return;
        }
        if (quad ? !inside(a1, a3) || !inside(a3, a2) : !inside(a1, a2)) {
            return;
        }
        info.set_hit_model_index(item.model_index());
        if ((flags & 0x20000) != 0) {
            if (info.query_mask_mode() == 0) {
                return;
            }
            stamp(0x3c7);
            check(trigger_model == 0xffffffffu || trigger_model == item.model_index(), kMsgTriggers);
            trigger_model = item.model_index();
            return;
        }
        const std::int32_t along = mul_div(start_side, 0x4000, static_cast<std::int32_t>(static_cast<Word>(start_side) - static_cast<Word>(end_side)));
        if (along >= best) {
            return;
        }
        // the point hit: the start moved that fraction of the line, each axis by its magnitude and then its sign
        Word point[3];
        for (Word k = 0; k < 3; ++k) {
            const auto d = static_cast<std::int32_t>(line_end(info, k) - line_start(info, k));
            const Word magnitude = static_cast<Word>(d < 0 ? 0u - static_cast<Word>(d) : static_cast<Word>(d));
            const Word moved = static_cast<Word>(mul_div(static_cast<std::int32_t>(magnitude), along, 0x4000)) * (d >= 0 ? 1u : 0xffffffffu);
            point[k] = line_start(info, k) + moved;
        }
        best = along;
        info.set_hit_parameter(along);
        for (Word k = 0; k < 3; ++k) {
            info.set_hit_position(k, static_cast<std::int32_t>(point[k]));
        }
        info.set_hit_body(GuestPtr<RuntimeCollisionObject>{item_address});
        info.set_hit_face_record(GuestPtr<void>{face});
        info.set_hit_model_index(item.model_index());
        mathkit::MathKit math(m_, s_);
        info.set_hit_distance(math.mul_q12(info.line_length(), along) >> 2);
        const RuntimeIntVector3View best_normal(m_, kBestNormal);
        for (Word k = 0; k < 3; ++k) {
            set_component(best_normal, k, static_cast<Word>(component(normal, k)));
        }
        write_scalar<Word>(m_, kBestFace, face);
    };

    for (Word remaining = count; remaining != 0; --remaining) {
        test_face();
        face += face_stride(CollisionFacePrefixView(m_, face));
        box += 0x1c;
    }
    if (trigger_model == 0xffffffffu) {
        return;
    }
    const Word environment = engine_value(kGetEnvironmentIndex, {item.model_region_slot()});
    stamp(0x47f);
    check(environment != 0xffffffffu, kMsgNoZone);
    write_scalar<Word>(m_, kTriggerFlag, 0);
    write_scalar<Word>(m_, kEnvironmentFlag, environment ^ 1);
    engine(kTriggerCommandPoint, {read_scalar<Word>(m_, read_scalar<Word>(m_, kEnvironmentLists + 4 * environment) + 4 * trigger_model), 1});
}

// 0x004628f0 (cdecl): a list of linked objects (+0x20) against the record's line: culled against the line's box (0x004f43e0), then each object not yet seen by this query (stamped at +6) has its faces
// tested (0x00463e50)
void CollisionKit::prepare_collision_objects(Word objects, Word record) {
    const SLineInfoView info(m_, record);
    if (objects == 0 || info.line_length() == 0) {
        return;
    }
    const RuntimeGteRegisterFileView gte(m_, kRegisters);
    copy_matrix(gte.rotation(), RuntimeQ12MatrixView(m_, info.line_basis_address()));
    engine(kCullLinkedObjects, {objects, 0, record, (static_cast<Word>(info.line_length()) & 0xffff0000u) | info.query_stamp()});
    for (Word at = objects; at != 0; at = RuntimeCollisionObjectView(m_, at).collision_next().address) {
        const RuntimeCollisionObjectView object(m_, at);
        const std::uint16_t generation = info.query_stamp();
        if (object.query_stamp() != generation) {
            object.set_query_stamp(generation);
            engine(kLinkedObjectFaces, {at, record});
        }
    }
}

}  // namespace opentony::runtime::collision
