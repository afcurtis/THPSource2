#include "m3d_views.hpp"

// Phase 4.15a: the box tests of the linked-object culling (0x004f3f30, 0x004f4130, 0x004f4240, 0x004f4940).

namespace opentony::runtime::m3d {

using namespace views;

// 0x004f3f30: the two corners of a box (Q12, six dwords at the start of the record) relative to the camera's position (the three dwords after the record's first), each through RTPS; the screen
// position and depth of both are kept in the record (+0x18 the first corner's screen word, +0x1c the second's, +0x20 / +0x24 0 and 0x1000, +0x28 / +0x2c the depths) and the answer is the nearer depth
// shifted down by 2, at most 0xfff
Word M3dKit::project_box_corners(Word box_at, Word camera_at) {
    const RuntimeCullBoxView box(m_, box_at);
    const RuntimeIntVector3View camera = RuntimeViewportRecordView(m_, camera_at).camera_position();
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View v0 = gte.v0();
    const std::int32_t cx = sext16(u(camera.x()));
    const std::int32_t cy = sext16(u(camera.y()));
    const std::int32_t cz = sext16(u(camera.z()));
    const RuntimeIntVector3View low = box.low_q12();
    const RuntimeIntVector3View high = box.high_q12();
    set_vector3(v0, (low.x() >> 12) - cx, (low.y() >> 12) - cy, (low.z() >> 12) - cz);
    math_.rtps();
    const Word first_screen = screen_word(gte, 1);
    const Word first_depth = static_cast<Word>(gte.mac().z());
    set_vector3(v0, (high.x() >> 12) - cx, (high.y() >> 12) - cy, (high.z() >> 12) - cz);
    math_.rtps();
    const Word second_depth = static_cast<Word>(gte.mac().z());
    box.set_screen_high(screen_word(gte, 1));
    box.set_depth_low(first_depth);
    box.set_screen_low(first_screen);
    box.set_field_20(0);
    box.set_field_24(0x1000);
    box.set_depth_high(second_depth);
    std::int32_t nearer = static_cast<std::int32_t>(first_depth) >> 2;
    if (!(static_cast<std::int32_t>(first_depth) < static_cast<std::int32_t>(second_depth))) {
        nearer = static_cast<std::int32_t>(second_depth) >> 2;
    }
    if (nearer - 0x1000 >= 0) {
        nearer = 0xfff;
    }
    return u(nearer);
}

// 0x004f4130: an object's model box (the corners packed as a high and a low short in three dwords at +0xc) moved by the object's position and made a little larger (2 units each way); the axes whose
// flag bit is set are mirrored about the culling box (the bits come from `Box_OrderCorners`)
void M3dKit::object_corners_in_box(Word model_at, std::int32_t low_x, std::int32_t low_y, std::int32_t low_z, std::int32_t high_x, std::int32_t high_y, std::int32_t high_z, std::int32_t object_x,
                                   std::int32_t object_y, std::int32_t object_z, Word flags, Word out_low_at, Word out_high_at) {
    const CollisionModelDataHeaderView model(m_, model_at);
    const RuntimeIntVector3View out_low(m_, out_low_at);
    const RuntimeIntVector3View out_high(m_, out_high_at);
    // (the box is three dwords, each a high short over a low short: the two bounds of one axis)
    const std::int32_t offset[3] = {object_x, object_y, object_z};
    for (Word k = 0; k < 3; ++k) {
        set_component(out_low, k, u(model.bounds(2 * k + 1) + offset[k]));
        set_component(out_high, k, u(model.bounds(2 * k) + offset[k]));
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(out_low, k, component(out_low, k) - 2);
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(out_high, k, component(out_high, k) + 2);
    }
    const std::int32_t lows[3] = {low_x, low_y, low_z};
    const std::int32_t highs[3] = {high_x, high_y, high_z};
    for (Word k = 0; k < 3; ++k) {
        if ((flags & (1u << k)) != 0) {
            const Word kept = component(out_low, k);
            const Word sum = u(lows[k] + highs[k]);
            set_component(out_low, k, sum - component(out_high, k));
            set_component(out_high, k, sum - kept);
        }
    }
}

// 0x004f4240: do a box (given by its two corners and its extents on the GTE diagonal) and a second box overlap? The first corner pair must be ordered, the extents non-negative; the GTE's outer product
// (op0) of the extent vector with the offsets is the test
Word M3dKit::box_overlap_test(const std::int32_t (&a)[15]) {
    // a[0..2] the first box's low corner, a[3..5] its high corner, a[6..8] the second's low corner, a[9..11] its high corner, a[12..14] the extents handed to the GTE diagonal
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeQ12MatrixView rotation = gte.rotation();
    const RuntimeIntVector3View mac = gte.mac();
    rotation.set_elements(0, static_cast<std::int16_t>(a[12]));
    rotation.set_elements(4, static_cast<std::int16_t>(a[13]));
    rotation.set_elements(8, static_cast<std::int16_t>(a[14]));
    if (!(a[3] > a[6]) || !(a[4] > a[7]) || !(a[5] > a[8])) {
        return 0;
    }
    std::int32_t dx = a[9] - a[0];
    std::int32_t dy = a[10] - a[1];
    std::int32_t dz = a[11] - a[2];
    if (dx < 0 || dy < 0 || dz < 0) {
        return 0;
    }
    const auto cross = [this, &gte](std::int32_t x, std::int32_t y, std::int32_t z) {
        set_vector3(gte.ir(), x, y, z);
        math_.cross_sf0();
    };
    cross(dx, dy, dz);
    const std::int32_t ox = a[6] - a[0];            // (the second box's low corner relative to the first's)
    const std::int32_t oy = a[7] - a[1];
    const std::int32_t oz = a[8] - a[2];
    const std::int32_t mac1 = mac.x();
    if (mac1 < 0) {
        if (mac.y() < 0) {
            cross(dx, oy, oz);
            if (mac.y() < 0) {
                return 0;
            }
            return mac.z() <= 0 ? 1 : 0;
        }
        cross(ox, oy, dz);
        if (mac.x() < 0) {
            return 0;
        }
        return mac.y() <= 0 ? 1 : 0;
    }
    if (mac.z() < 0) {
        cross(ox, dy, oz);
        if (mac.x() > 0) {
            return 0;
        }
        return mac.z() >= 0 ? 1 : 0;
    }
    cross(dx, oy, oz);
    if (mac.y() < 0) {
        return 0;
    }
    return mac.z() <= 0 ? 1 : 0;
}

// 0x004f4940: for a null-terminated array of objects and a culling box: an object that is solid (the flag word at +4 is not "both 0x41 bits") and not marked (bit 0x20) whose tagged generation (the short at
// +6) is not the current one is tested against the box (its model's box moved by the object's position, in whole units); an object that does not overlap, or is not solid or is marked, gets the
// generation, which excludes it from the queries of this tick
void M3dKit::cull_objects_in_box(Word objects, Word, Word box, std::int32_t generation) {
    if (word_at(m_, objects) == 0) {
        return;
    }
    constexpr Word kLow = pcplatform::kScratchBase + 0x200;       // the locals of the retail frame: the box's corners (units), the object's corners and the flags
    constexpr Word kHigh = kLow + 0x20;
    constexpr Word kFlags = kLow + 0x40;
    constexpr Word kObjectLow = kLow + 0x60;
    constexpr Word kObjectHigh = kLow + 0x80;
    const RuntimeIntVector3View low(m_, kLow);
    const RuntimeIntVector3View high(m_, kHigh);
    const RuntimeIntVector3View object_low(m_, kObjectLow);
    const RuntimeIntVector3View object_high(m_, kObjectHigh);
    corners_to_units(box, kLow, kHigh, kFlags);
    const std::int32_t extent_y = high.y() - low.y();
    const std::int32_t extent_z = high.z() - low.z();
    const std::int32_t extent_x = high.x() - low.x();
    Word at = objects;
    while (word_at(m_, at) != 0) {
        const RuntimeCollisionObjectView object(m_, word_at(m_, at));
        const std::uint16_t word = object.flags();
        bool culled = false;
        if (((~word & 0x41) == 0) || (word & 0x20) != 0) {
            culled = true;
        } else if (static_cast<std::int16_t>(object.query_stamp()) != static_cast<std::int16_t>(generation)) {
            const Word model = model_of(m_, object);
            object_corners_in_box(model, low.x(), low.y(), low.z(), high.x(), high.y(), high.z(), object.position_q12(0) >> 12, object.position_q12(1) >> 12, object.position_q12(2) >> 12,
                                  global_word(m_, kFlags), kObjectLow, kObjectHigh);
            const std::int32_t args[15] = {low.x(), low.y(), low.z(), high.x(), high.y(), high.z(), object_low.x(), object_low.y(), object_low.z(), object_high.x(), object_high.y(), object_high.z(),
                                           extent_x, extent_y, extent_z};
            culled = box_overlap_test(args) == 0;
        }
        if (culled) {
            object.set_query_stamp(static_cast<std::uint16_t>(generation));
        }
        at += 4;
    }
}

}  // namespace opentony::runtime::m3d
