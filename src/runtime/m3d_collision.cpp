#include "m3d_views.hpp"

#include "host_options.hpp"

#include <algorithm>
#include <bit>
#include <utility>

// Phase 4.15a: the collision of a line with the linked objects of the world (0x004f43e0, 0x004f4c50, 0x00463e50): the culling of an object list against a box, the test of a line against the faces of a model
// (through the GTE: the face's outer product tells which side the line starts on, the light matrix times the line's direction how far along it the face is) and the test against the faces of an object (the model
// moved, rotated and scaled into the line's frame first). The line is a record (`info`): the start (three dwords) at +0, the end at +0xc, the best distance so far at +0x40, the line's length at +0x44, the
// colour matrix at +0x48, the object hit at +0x68, the point hit at +0x6c, the face hit at +0x80 and that object's index at +0x84.

namespace opentony::runtime::m3d {

using namespace views;

namespace {

constexpr Word kVertexBuffer = 0x005620bc;                 // the transformed vertices of the model being tested (8 bytes each: x, y, z and the clip word)
constexpr Word kFaceMaskA = 0x00567a60;                    // faces with any of these flags are not tested
constexpr Word kFaceMaskB = 0x00567a68;                    // faces must have all the flags that are not in this mask
constexpr Word kTriggerFace = 0x00567a64;                  // the trigger face hit (-1: none)
constexpr Word kScratchMatrix = kBasis;                    // the object's matrix (nine shorts)
constexpr Word kScratchPoint = 0x00567bc4;                 // the object's position in the line's frame (three dwords)
constexpr Word kTriggerFlag = 0x00567c80;
constexpr Word kEnvironmentFlag = 0x0056db2c;
constexpr Word kEnvironmentLists = 0x0056d41c;             // per environment index, the array of trigger commands
constexpr Word kNormalX = kBestNormal;                     // the normal of the best face (three dwords) and the face
constexpr Word kBestFace = 0x0056438c;
constexpr Word kHitObject = 0x00567a6c;                    // the object a line hit (0: none)
constexpr Word kBestDistance = 0x005643a8;                 // the distance along the line to the nearest hit (Q12 units)
constexpr Word kGetEnvironmentIndex = 0x004b3990;          // Spool_GetEnvIndex(region) -> the index of the environment of a region (-1: none)
constexpr Word kTriggerCommandPoint = 0x004c4bd0;          // Trig_TriggerCommandPoint(commands, 1)
constexpr Word kMsgNoVertices = 0x00533ca0;                // "Collision check on a model with no vertices"
constexpr Word kMsgNoZone = 0x00533ac0;                    // "Found a trigger zone not in the environment"

// the locals of the retail frames
constexpr Word kLow = pcplatform::kScratchBase + 0x300;
constexpr Word kHigh = kLow + 0x20;
constexpr Word kFlags = kLow + 0x40;
constexpr Word kObjectLow = kLow + 0x60;
constexpr Word kObjectHigh = kLow + 0x80;
constexpr Word kAngles = kLow + 0xa0;
constexpr Word kMatrix = kLow + 0xc0;
constexpr Word kOffset = kLow + 0xe0;

}  // namespace

// 0x004f43e0: the culling of a chain of objects (linked at +0x20) against a box. Like 0x004f4940, but an object turned from the upright (angles at +0x14) has the box turned into its frame (the GTE rotates
// the corners) and an object scaled (bit 1 of +5) has its model box scaled; the flags of the corner order outlive the object
void M3dKit::cull_linked_objects(Word objects, Word, Word box, std::int32_t generation) {
    if (objects == 0) {
        return;
    }
    const RuntimeIntVector3View corner_low(m_, kLow);
    const RuntimeIntVector3View corner_high(m_, kHigh);
    const RuntimeIntVector3View object_low(m_, kObjectLow);
    const RuntimeIntVector3View object_high(m_, kObjectHigh);
    const RuntimeShortVector3View angles(m_, kAngles);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View ir = gte.ir();
    corners_to_units(box, kLow, kHigh, kFlags);
    const std::int32_t box_low[3] = {corner_low.x(), corner_low.y(), corner_low.z()};
    const std::int32_t box_high[3] = {corner_high.x(), corner_high.y(), corner_high.z()};
    std::int32_t low[3];
    std::int32_t high[3];
    for (Word at = objects; at != 0; at = RuntimeCollisionObjectView(m_, at).collision_next().address) {
        const RuntimeCollisionObjectView object(m_, at);
        const std::uint16_t word = object.flags();
        bool tag = false;
        if (((~word & 0x41) == 0) || (word & 0x20) != 0) {
            tag = true;
        } else if (sext16(object.query_stamp()) != sext16(u(generation))) {
            const Word model_at = model_of(m_, object);
            const CollisionModelDataHeaderView model(m_, model_at);
            const std::int32_t position[3] = {object.position_q12(0) >> 12, object.position_q12(1) >> 12, object.position_q12(2) >> 12};
            for (Word k = 0; k < 3; ++k) {
                low[k] = box_low[k] - position[k];
                high[k] = box_high[k] - position[k];
            }
            if (object.rotation(0) != 0 || object.rotation(1) != 0 || object.rotation(2) != 0) {
                std::int32_t radius = static_cast<std::int32_t>(model.radius()) >> 12;
                if (radius < low[0] || radius < low[1] || radius < low[2] || (radius = -radius, high[0] < radius || high[1] < radius) || high[2] < radius) {
                    tag = true;
                } else {
                    angles.set_x(object.rotation(0));
                    angles.set_y(object.rotation(1));
                    angles.set_z(object.rotation(2));
                    math_.rotation_from_angles(kAngles, kMatrix);
                    transpose_matrix(kMatrix, kMatrix);
                    views::copy_matrix(gte.rotation(), RuntimeQ12MatrixView(m_, kMatrix));
                    const Word flags = global_word(m_, kFlags);
                    if ((flags & 1) != 0) {
                        std::swap(low[0], high[0]);
                    }
                    if ((flags & 2) != 0) {
                        std::swap(low[1], high[1]);
                    }
                    if ((flags & 4) != 0) {
                        std::swap(low[2], high[2]);
                    }
                    set_global_word(m_, kFlags, 0);
                    set_vector3(ir, low[0], low[1], low[2]);
                    math_.rtir();
                    for (Word k = 0; k < 3; ++k) {
                        low[k] = component(ir, k);
                    }
                    set_vector3(ir, high[0], high[1], high[2]);
                    math_.rtir();
                    for (Word k = 0; k < 3; ++k) {
                        high[k] = component(ir, k);
                    }
                    for (Word k = 0; k < 3; ++k) {
                        set_component(object_low, k, u(low[k]));      // (the order helper works on memory)
                        set_component(object_high, k, u(high[k]));
                    }
                    order_corners(kObjectLow, kObjectHigh, kFlags);
                    for (Word k = 0; k < 3; ++k) {
                        low[k] = static_cast<std::int32_t>(component(object_low, k));
                        high[k] = static_cast<std::int32_t>(component(object_high, k));
                    }
                }
            }
            if (!tag) {
                const std::int32_t extent[3] = {high[0] - low[0], high[1] - low[1], high[2] - low[2]};
                object_corners_in_box(model_at, low[0], low[1], low[2], high[0], high[1], high[2], 0, 0, 0, global_word(m_, kFlags), kObjectLow, kObjectHigh);
                if ((object.flags() & 0x200) != 0) {
                    for (Word k = 0; k < 3; ++k) {
                        const std::int32_t scale = object.matrix_scale(k);
                        for (const RuntimeIntVector3View& corner : {object_low, object_high}) {
                            set_component(corner, k, u(mathkit::x87::ftol(static_cast<long double>(static_cast<std::int32_t>(component(corner, k))) * scale * mathkit::x87::kQuarterTurnStep)));
                        }
                    }
                }
                const std::int32_t args[15] = {low[0], low[1], low[2], high[0], high[1], high[2], object_low.x(), object_low.y(), object_low.z(), object_high.x(), object_high.y(), object_high.z(),
                                               extent[0], extent[1], extent[2]};
                tag = box_overlap_test(args) == 0;
            }
        }
        if (tag) {
            object.set_query_stamp(static_cast<std::uint16_t>(generation));
        }
    }
}

// 0x004f4c50: the faces of a model (the vertices already transformed into the line's frame: `vertices`) tested against the line `info`. A face counts when its flags are not in the first mask, include everything
// outside the second, and are not just the 0x10000 bit; all four corners must not be clipped away together. The side of the face the line starts on is the sign of the outer product of two of its edges
// (a face with bit 4 of its size word set has only one side); the light matrix (the line's direction in its first row, the length plus 2 in the last place) gives the distance along the line to the face's
// plane: a face before the start and after the end, nearer than the best so far, becomes the best (the trigger faces, flag 0x20000, only mark a zone). The point hit is the start moved along the line
void M3dKit::test_linked_model_faces(Word model_at, Word vertices, Word info_at, Word object_at) {
    const CollisionModelDataHeaderView model(m_, model_at);
    const SLineInfoView info(m_, info_at);
    const RuntimeCollisionObjectView object(m_, object_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeQ12MatrixView light = gte.light();
    const RuntimeQ12MatrixView rotation = gte.rotation();
    const RuntimeShortVector3View ir = gte.ir();
    const RuntimeShortVector3View v0 = gte.v0();
    const RuntimeIntVector3View mac = gte.mac();
    for (Word k = 0; k < 8; ++k) {
        light.set_elements(k, 0);
    }
    light.set_elements(8, static_cast<std::int16_t>(static_cast<std::uint16_t>(info.line_length()) + 2));
    const Word normals = model_at + 0x1c + 8 * Word{model.vertex_count()};
    Word face = normals + 8 * Word{model.normal_count()};
    for (Word remaining = model.face_count(); remaining != 0; --remaining) {
        const CollisionFacePrefixView face_record(m_, face);
        const Word size_word = face_word0(face_record);
        const Word flags = face_flags(face_record);
        if ((flags & global_word(m_, kFaceMaskA)) == 0 && (~(global_word(m_, kFaceMaskB) | flags)) == 0 && ((flags ^ 0x10000) & 0x30000) != 0) {
            const RuntimeClippedVertexView c0(m_, vertices + Word{face_record.vertex_indices(0)} * 8);
            const RuntimeClippedVertexView c1(m_, vertices + Word{face_record.vertex_indices(1)} * 8);
            const RuntimeClippedVertexView c2(m_, vertices + Word{face_record.vertex_indices(2)} * 8);
            const RuntimeClippedVertexView c3(m_, vertices + Word{face_record.vertex_indices(3)} * 8);
            if ((c3.clip_flags() & c2.clip_flags() & c1.clip_flags() & c0.clip_flags() & 0x60f) == 0) {
                const auto outer = [&](const RuntimeClippedVertexView& first) {
                    rotation.set_elements(0, first.x());
                    rotation.set_elements(4, c1.x());
                    rotation.set_elements(8, c2.x());
                    ir.set_x(first.y());
                    ir.set_y(c1.y());
                    ir.set_z(c2.y());
                    math_.cross_sf0();
                };
                outer(c0);
                std::int32_t second = mac.y();
                std::int32_t third = mac.z();
                bool skip = false;
                if (mac.x() < 0) {
                    if ((size_word & 0x10) != 0) {
                        skip = true;
                    } else {
                        outer(c3);
                        second = -mac.y();
                        third = -mac.z();
                    }
                }
                if (!skip && (second | third) >= 0) {
                    const CollisionNormalRecordView normal(m_, normals + ((flags >> 3) & 0x1fff) * 8);
                    v0.set_x(normal.x());
                    v0.set_y(normal.y());
                    v0.set_z(normal.z());
                    math_.lcv0();
                    light.set_elements(0, c0.x());
                    light.set_elements(1, c0.y());
                    light.set_elements(2, c0.z());
                    const std::int32_t divisor = ir.z();
                    math_.llir_sf0();
                    std::int32_t distance = mac.x();
                    if (!(distance > 0) && !(distance < mac.z())) {
                        if (divisor != 0) {
                            distance /= divisor;
                        }
                        if ((flags & 0x20000) == 0) {
                            if (info.hit_distance() - distance > 0) {
                                info.set_hit_body(GuestPtr<RuntimeCollisionObject>{object_at});
                                info.set_hit_distance(distance);
                                info.set_hit_face_record(GuestPtr<void>{face});
                                info.set_hit_model_index(object.model_index());
                                const RuntimeIntVector3View best(m_, kNormalX);
                                best.set_x(normal.x());
                                best.set_y(normal.y());
                                best.set_z(normal.z());
                                set_global_word(m_, kBestFace, face);
                            }
                        } else if (info.query_mask_mode() != 0) {
                            set_global_word(m_, kTriggerFace, object.model_index());
                        }
                    }
                }
            }
        }
        face += size_word >> 16;
    }
    const std::int32_t quotient = static_cast<std::int32_t>(u(info.hit_distance()) << 12) / info.line_length();
    mac.set_x(info.start_x());
    mac.set_y(info.start_y());
    mac.set_z(info.start_z());
    ir.set_x(static_cast<std::int16_t>((info.end_x() - info.start_x()) >> 12));
    ir.set_y(static_cast<std::int16_t>((info.end_y() - info.start_y()) >> 12));
    ir.set_z(static_cast<std::int16_t>((info.end_z() - info.start_z()) >> 12));
    gte.set_ir0(static_cast<std::int16_t>(quotient));
    math_.interpolate_gpl0();
    info.set_hit_position(0, mac.x());
    info.set_hit_position(1, mac.y());
    info.set_hit_position(2, mac.z());
}

// 0x00463e50: the line tested against an object's model. The colour matrix starts as the line's; the model is put into the line's frame (an upright, unscaled object by an offset alone, any other by the
// rotation and scale of the object turned by the line's matrix and moved by the offset the colour matrix gets), its vertices go through `Collision_TransformLinkedModelVertices` and, when they are not all
// clipped away, its faces are tested. A trigger face hit makes its zone's commands run
void M3dKit::test_linked_object_faces(Word object_at, Word info_at) {
    const RuntimeCollisionObjectView object(m_, object_at);
    const SLineInfoView info(m_, info_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeIntVector3View translation = gte.translation();
    const RuntimeIntVector3View mac = gte.mac();
    const RuntimeIntVector3View offset(m_, kOffset);
    const RuntimeIntVector3View point(m_, kScratchPoint);
    views::copy_matrix(gte.colour(), RuntimeQ12MatrixView(m_, info.line_basis_address()));
    const Word model_at = model_of(m_, object);
    check(CollisionModelDataHeaderView(m_, model_at).vertex_count() != 0, kMsgNoVertices);
    const Word vertices = global_word(m_, kVertexBuffer);
    const bool upright = object.rotation(0) == 0 && object.rotation(1) == 0 && object.rotation(2) == 0;
    if (upright && (object.flags() & 0x200) == 0) {
        offset.set_x((object.position_q12(0) - info.start_x()) >> 12);
        offset.set_y((object.position_q12(1) - info.start_y()) >> 12);
        offset.set_z((object.position_q12(2) - info.start_z()) >> 12);
        translation.set_x(0);
        translation.set_y(0);
        translation.set_z(0);
    } else {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        if (info.direction_flag() == 0) {
            x = (object.position_q12(0) - info.start_x()) >> 12;
            y = (object.position_q12(1) - info.start_y()) >> 12;
            point.set_x(x);
            point.set_y(y);
            z = (object.position_q12(2) - info.start_z()) >> 12;
            set_vector3(gte.v0(), x, y, z);
            point.set_z(z);
            math_.lcv0();
            x = mac.x();
            y = mac.y();
            z = mac.z();
            point.set_x(x);
            point.set_y(y);
        } else {
            x = (object.position_q12(0) - info.start_x()) >> 12;
            point.set_x(x);
            y = (info.start_z() - object.position_q12(2)) >> 12;
            point.set_y(y);
            z = (object.position_q12(1) - info.start_y()) >> 12;
        }
        point.set_z(z);
        translation.set_x(x);
        translation.set_y(y);
        translation.set_z(z);
        offset.set_x(0);
        offset.set_y(0);
        offset.set_z(0);
        if (upright) {
            set_identity_matrix(kScratchMatrix);
            scale_object_matrix(object_at, kScratchMatrix);
            multiply_by_light(kScratchMatrix, kScratchMatrix);
        } else {
            rotation_from_angles(object.rotation_address(), kScratchMatrix);
            if ((object.flags() & 0x200) != 0) {
                scale_object_matrix(object_at, kScratchMatrix);
            }
            multiply_by_light(kScratchMatrix, kScratchMatrix);
        }
        views::copy_matrix(gte.colour(), RuntimeQ12MatrixView(m_, kScratchMatrix));
    }
    const Word clipped = transform_vertices(model_at, vertices, info.line_length(), kOffset);
    if ((clipped & 0x60f) != 0) {
        return;
    }
    set_global_word(m_, kTriggerFace, 0xffffffffu);
    test_linked_model_faces(model_at, vertices, info_at, object_at);
    if (global_word(m_, kTriggerFace) == 0xffffffffu) {
        return;
    }
    const Word environment = engine_value(kGetEnvironmentIndex, {object.model_region_slot()});
    check(environment != 0xffffffffu, kMsgNoZone);
    set_global_word(m_, kTriggerFlag, 0);
    set_global_word(m_, kEnvironmentFlag, environment ^ 1);
    engine(kTriggerCommandPoint, {word_at(m_, word_at(m_, kEnvironmentLists + 4 * environment) + 4 * global_word(m_, kTriggerFace)), 1});
}

// 0x00464200: a line from `start` to `end` (Q12 positions) against the spheres of a chain of objects (linked at +0x20; the radius is the short at +0xae times `scale`; objects with bit 6 of +0x6a set, and the
// object `skip`, are ignored). The unit direction goes on the GTE diagonal and in the first row of the light matrix: the outer product with the vector to an object gives the distance of its centre from
// the line, the light matrix times that vector the position along the line. A sphere that the line passes through, or whose centre is nearer than the radius to the line's end or start, and whose
// position along the line is nearer than the best so far, becomes the hit. The answer is the hit object, with the hit point (the start moved the distance along the line) at `out`
Word M3dKit::line_to_sphere(Word start_at, Word end_at, Word out_at, Word list, Word skip, std::int32_t scale) {
    const RuntimeIntVector3View start(m_, start_at);
    const RuntimeIntVector3View end(m_, end_at);
    const RuntimeIntVector3View out(m_, out_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View ir = gte.ir();
    const RuntimeShortVector3View v0 = gte.v0();
    const RuntimeQ12MatrixView rotation = gte.rotation();
    const RuntimeQ12MatrixView light = gte.light();
    const RuntimeIntVector3View mac = gte.mac();
    set_global_word(m_, kHitObject, 0);
    set_global_word(m_, kBestDistance, 0x7fffffffu);
    std::int32_t direction[3];
    for (Word k = 0; k < 3; ++k) {
        direction[k] = static_cast<std::int32_t>(component(end, k) - component(start, k)) >> 12;
        set_component(ir, k, direction[k]);
    }
    math_.sqr0();
    const std::int32_t length = static_cast<std::int32_t>(sqrt_truncated(static_cast<Word>(mac.z()) + static_cast<Word>(mac.y()) + static_cast<Word>(mac.x())));
    if (length == 0) {
        return 0;
    }
    std::int32_t unit[3];
    for (Word k = 0; k < 3; ++k) {
        unit[k] = static_cast<std::int32_t>(u(direction[k]) << 12) / length;
    }
    rotation.set_elements(0, static_cast<std::int16_t>(unit[0]));
    rotation.set_elements(4, static_cast<std::int16_t>(unit[1]));
    rotation.set_elements(8, static_cast<std::int16_t>(unit[2]));
    light.set_elements(3, 0);
    std::int32_t low[3];
    std::int32_t high[3];
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t a = static_cast<std::int32_t>(component(start, k));
        const std::int32_t b = static_cast<std::int32_t>(component(end, k));
        low[k] = a < b ? a : b;
        high[k] = a < b ? b : a;
    }
    light.set_elements(0, static_cast<std::int16_t>(unit[0]));
    light.set_elements(1, static_cast<std::int16_t>(unit[1]));
    light.set_elements(2, static_cast<std::int16_t>(unit[2]));
    for (Word at = list; at != 0; at = RuntimeListObjectView(m_, at).next().address) {
        const RuntimeListObjectView object(m_, at);
        if ((object.protect_flags() & 0x40) != 0 || at == skip) {
            continue;
        }
        const RuntimeIntVector3View centre = object.position();
        const std::int32_t position[3] = {centre.x(), centre.y(), centre.z()};
        const std::int32_t radius = static_cast<std::int32_t>(Word{object.bounding_radius()} * u(scale));
        bool reach = true;
        for (Word k = 0; k < 3 && reach; ++k) {
            reach = static_cast<std::int32_t>(u(position[k]) + u(radius)) >= low[k] && static_cast<std::int32_t>(u(position[k]) - u(radius)) <= high[k];
        }
        if (!reach) {
            continue;
        }
        std::int32_t offset[3];
        for (Word k = 0; k < 3; ++k) {
            offset[k] = static_cast<std::int32_t>(u(position[k]) - component(start, k)) >> 12;
            set_component(ir, k, offset[k]);
        }
        math_.cross_sf12();
        math_.sqr0();
        const std::int32_t reduced = radius >> 12;
        const std::int32_t limit = static_cast<std::int32_t>(u(reduced) * u(reduced));
        const auto squared_sum = [&mac] { return static_cast<std::int32_t>(static_cast<Word>(mac.z()) + static_cast<Word>(mac.y()) + static_cast<Word>(mac.x())); };
        if (squared_sum() >= limit) {
            continue;
        }
        for (Word k = 0; k < 3; ++k) {
            set_component(v0, k, offset[k]);
        }
        math_.llv0();
        const std::int32_t along = mac.x();
        if (along >= global_int(m_, kBestDistance)) {
            continue;
        }
        bool hit = false;
        if (along < 0) {
            for (Word k = 0; k < 3; ++k) {
                set_component(ir, k, offset[k]);
            }
            math_.sqr0();
            hit = squared_sum() < limit;
        } else if (along <= length) {
            hit = true;
        } else {
            for (Word k = 0; k < 3; ++k) {
                set_component(ir, k, static_cast<std::int32_t>(u(position[k]) - component(end, k)) >> 12);
            }
            math_.sqr0();
            hit = squared_sum() < limit;
        }
        if (hit) {
            set_global_word(m_, kBestDistance, u(along));
            set_global_word(m_, kHitObject, at);
        }
    }
    if (global_word(m_, kHitObject) != 0) {
        ir.set_z(static_cast<std::int16_t>(unit[2]));
        ir.set_x(static_cast<std::int16_t>(unit[0]));
        ir.set_y(static_cast<std::int16_t>(unit[1]));
        gte.set_ir0(static_cast<std::int16_t>(global_word(m_, kBestDistance)));
        math_.interpolate_gpf12();
        out.set_x(mac.x());
        out.set_y(mac.y());
        out.set_z(mac.z());
        for (Word k = 0; k < 3; ++k) {
            set_component(out, k, component(out, k) << 12);
        }
        for (Word k = 0; k < 3; ++k) {
            set_component(out, k, component(out, k) + component(start, k));
        }
    }
    return global_word(m_, kHitObject);
}

// 0x004f39f0 (cdecl, a chain of objects and a mask of flags): the objects of the chain outside the view get bit 15 of their flag word (the render skips them). An object whose flags meet the mask (and bit 12)
// is outside at once; the others are tested against the view volume in floating point (the x87, at the precision the caller runs at): the position relative to the camera (whole units, as floats) must lie
// between two limits on x and y (and z, unless bit 13 is set); then the object bounding sphere (the radius of its model in whole units, scaled by the smallest of the scale shorts of the object when bit 9 is set, and by
// a factor when bit 1 is set) is tested against the six planes of the view (a plane is three shorts, Q12; the first two planes also have an offset); and an object that is neither scaled nor turned and whose model has more than
// ten faces has the eight corners of the box of its model tested against the planes the same way
void M3dKit::cull_objects_to_view(Word objects, Word mask) {
    if (objects == 0) {
        return;
    }
    constexpr Word kCamera = 0x005620e0;              // pointer to the camera record (the position as three dwords from +4)
    constexpr Word kPlanesA = 0x005620e8;             // three planes of three shorts
    constexpr Word kPlanesB = 0x005620c0;             // three more
    constexpr Word kNearOffset = 0x00563a96;          // short: the offset of the first plane (scaled by a float)
    constexpr Word kFarOffset = 0x00563a9e;           // short: the offset of the second plane
    constexpr Word kLimitHigh = 0x00519a7c;
    constexpr Word kLimitLow = 0x00519a78;
    constexpr Word kSphereScale = 0x00519a74;
    constexpr Word kQ12Step = 0x00518910;
    constexpr Word kOffsetScale = 0x00549d98;
    constexpr Word kDefaultOffset = 0x00518484;
    const auto precision = math_.precision();
    const auto round = [precision](long double v) { return mathkit::x87::round_to(precision, v); };
    const auto as_float = [this](Word at) { return std::bit_cast<float>(global_word(m_, at)); };
    const auto to_float = [](long double v) { return static_cast<float>(v); };
    const Word test_mask = mask | 0x1000;
    const float step = as_float(kQ12Step);
    // HostOptions::detail_distance also stretches the box around the camera (cull_reach), while the polygon arena is under three quarters used (the same rule as
    // RenderKit::render_objects' level of detail; at the game's 100 percent the box is the game's and this is not read)
    // and HostOptions::draw_hidden_areas draws the environment objects a level's area boxes hid, on the same condition
    bool box_room = false;
    if (host_options().detail_distance != 100 || host_options().draw_hidden_areas) {
        const Word capacity = global_word(m_, 0x00561150);
        const Word used = global_word(m_, 0x00560fd0) - (global_word(m_, 0x00568628) - capacity);
        box_room = used < capacity / 4 * 3;
    }
    for (Word at = objects; at != 0; at = RuntimeCollisionObjectView(m_, at).collision_next().address) {
        const RuntimeCollisionObjectView object(m_, at);
        object.set_flags(static_cast<std::uint16_t>(object.flags() & 0x7fff));
        Word flags = object.flags();
        bool outside = (test_mask & flags) != 0;
        if (outside && box_room && hidden_only_by_area(at, static_cast<std::uint16_t>(flags), static_cast<std::uint16_t>(test_mask & flags))) {
            outside = false;   // host side: a level's area box hid it (HostOptions::draw_hidden_areas); tested against the view as any other
        }
        if (!outside) {
            const RuntimeIntVector3View camera = RuntimeViewportRecordView(m_, global_word(m_, kCamera)).camera_position();
            const float rel[3] = {to_float(static_cast<long double>((object.position_q12(0) >> 12) - camera.x())), to_float(static_cast<long double>((object.position_q12(1) >> 12) - camera.y())),
                                  to_float(static_cast<long double>((object.position_q12(2) >> 12) - camera.z()))};
            const float high = cull_reach(as_float(kLimitHigh), box_room);
            const float low = cull_reach(as_float(kLimitLow), box_room);
            outside =!(rel[0] < high) || rel[0] < low || !(rel[1] < high) || rel[1] < low || (((flags & 0x2000) == 0) && !(rel[2] < high)) || rel[2] < low;
            if (!outside) {
                const CollisionModelDataHeaderView model(m_, model_of(m_, object));
                float radius = to_float(static_cast<long double>(static_cast<std::int32_t>(model.radius()) >> 12));
                if ((flags & 0x200) != 0) {
                    std::int32_t smallest = object.matrix_scale(0);
                    smallest = std::min(smallest, std::int32_t{object.matrix_scale(1)});
                    smallest = std::min(smallest, std::int32_t{object.matrix_scale(2)});
                    const std::int32_t scaled = static_cast<std::int32_t>(u(mathkit::x87::ftol(radius)) * u(smallest)) >> 12;
                    radius = to_float(static_cast<long double>(scaled));
                }
                if ((flags & 2) != 0) {
                    radius = to_float(round(static_cast<long double>(radius) * as_float(kSphereScale)));
                }
                const auto plane_shorts = [this](Word plane, std::int32_t out[3]) {
                    const RuntimeShortVector3View normal(m_, plane < 3 ? kPlanesA + 6 * plane : kPlanesB + 6 * (plane - 3));
                    for (Word k = 0; k < 3; ++k) {
                        out[k] = component(normal, k);
                    }
                };
                // the sphere against the planes
                for (Word plane = 0; plane < 6 && !outside; ++plane) {
                    if (plane == 0 && (flags & 0x2000) != 0) {
                        continue;
                    }
                    std::int32_t n[3];
                    plane_shorts(plane, n);
                    long double sum = round(round(round(static_cast<long double>(static_cast<float>(n[2])) * step) * rel[2]) + round(round(static_cast<long double>(static_cast<float>(n[1])) * step) * rel[1]));
                    sum = round(sum + round(round(static_cast<long double>(static_cast<float>(n[0])) * step) * rel[0]));
                    if (plane == 0) {
                        sum = round(sum + round(static_cast<long double>(global_short(m_, kNearOffset)) * as_float(kOffsetScale)));
                    } else if (plane == 1) {
                        sum = round(sum + static_cast<long double>(global_short(m_, kFarOffset)));
                    }
                    if (sum < -static_cast<long double>(radius)) {
                        outside = true;
                    }
                }
                if (!outside && (flags & 0x202) == 0 && object.rotation(2) == 0 && object.rotation(1) == 0 && object.rotation(0) == 0 && model.face_count() > 10) {
                    // the eight corners of the box of the model against the planes: outside when all eight are beyond one
                    float box_high[3];
                    float box_low[3];
                    for (Word k = 0; k < 3; ++k) {
                        box_high[k] = to_float(round(static_cast<long double>(model.bounds(2 * k + 1)) + rel[k]));
                        box_low[k] = to_float(round(static_cast<long double>(model.bounds(2 * k)) + rel[k]));
                    }
                    for (Word plane = 0; plane < 6 && !outside; ++plane) {
                        if (plane == 0 && (flags & 0x2000) != 0) {
                            continue;
                        }
                        std::int32_t n[3];
                        plane_shorts(plane, n);
                        const long double nx = static_cast<long double>(static_cast<float>(n[0])) * step;
                        const long double ny = static_cast<long double>(static_cast<float>(n[1])) * step;
                        const long double nz = static_cast<long double>(static_cast<float>(n[2])) * step;
                        long double offset = static_cast<long double>(as_float(kDefaultOffset));
                        if (plane == 0) {
                            offset = round(static_cast<long double>(global_short(m_, kNearOffset)) * as_float(kOffsetScale));
                        } else if (plane == 1) {
                            offset = static_cast<long double>(global_short(m_, kFarOffset));
                        }
                        bool all_beyond = true;
                        for (Word corner = 0; corner < 8 && all_beyond; ++corner) {
                            const float c[3] = {(corner & 4) ? box_low[0] : box_high[0], (corner & 2) ? box_low[1] : box_high[1], (corner & 1) ? box_low[2] : box_high[2]};
                            long double dot = round(round(nz * c[2]) + round(nx * c[0]));
                            dot = round(dot + round(ny * c[1]));
                            all_beyond = round(dot + offset) < -static_cast<long double>(radius);
                        }
                        if (all_beyond) {
                            outside = true;
                        }
                    }
                }
            }
        }
        if (outside) {
            object.set_flags(static_cast<std::uint16_t>(flags | 0x8000));
        }
    }
}

}  // namespace opentony::runtime::m3d
