#include "m3d_views.hpp"

// Phase 4.15a: the model transforms of the M3D collision code (0x004f4b00, 0x004f5160, 0x004f5540, 0x004f5a70, 0x004f5ca0): each loads the GTE registers from matrices of shorts, runs a GTE operation and
// stores the result. The register file is memory at 0x006a3e00, so every store is part of what the replay compares.

namespace opentony::runtime::m3d {

using namespace views;

// 0x004f4b00: the vertices of a model (the count at +2, four shorts each from +0x1c) moved by an offset and put through the colour matrix and translation (LCIRTR); each output record is x, y, z
// (the MAC values, 16 bits) and a clip word: bit 2 / 1 / 0 for a negative x / y / z, bit 3 when z is beyond the limit, bit 10 / 9 for a positive x / y. The answer is the AND of all the clip words with 0x60f
Word M3dKit::transform_vertices(Word model_at, Word out, std::int32_t limit, Word offset_at) {
    const CollisionModelDataHeaderView model(m_, model_at);
    const RuntimeIntVector3View offset(m_, offset_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View ir = gte.ir();
    const RuntimeIntVector3View mac = gte.mac();
    Word accumulated = 0x60f;
    const CollisionVertexRecordView first(m_, model_at + 0x1c);
    std::int32_t x = first.x() + offset.x();
    std::int32_t y = first.y() + offset.y();
    std::int32_t z = first.z() + offset.z();
    const Word count = model.vertex_count();
    if (count == 0) {
        return accumulated;
    }
    Word vertex = model_at + 0x1c;
    Word record = out;
    for (Word remaining = count; remaining != 0; --remaining) {
        set_vector3(ir, x, y, z);
        math_.lcirtr();
        const std::int32_t mac1 = mac.x();
        const std::int32_t mac2 = mac.y();
        const std::int32_t mac3 = mac.z();
        // (the next vertex: the loop reads one past the last, the y first)
        const CollisionVertexRecordView next(m_, vertex + 8);
        y = next.y() + offset.y();
        vertex += 8;
        z = next.z() + offset.z();
        x = next.x() + offset.x();
        Word flags = (mac1 < 0 ? 4u : 0u) | (mac2 < 0 ? 2u : 0u) | (mac3 < 0 ? 1u : 0u) | (limit < mac3 ? 8u : 0u) | (mac1 > 0 ? 0x400u : 0u) | (mac2 > 0 ? 0x200u : 0u);
        const RuntimeClippedVertexView target(m_, record);
        target.set_x(static_cast<std::int16_t>(mac1));
        target.set_z(static_cast<std::int16_t>(mac3));
        target.set_clip_flags(static_cast<std::uint16_t>(flags));
        target.set_y(static_cast<std::int16_t>(mac2));
        accumulated &= flags & 0xffff;
        record += 8;
    }
    return accumulated;
}

// 0x004f5160: a vector through a hook's transform and lights: V0 = the vector, RT and TR from the transform (nine shorts then three), BK from three dwords of the lights record at +0x14, RT * V0 + TR, then the light
// matrix (the first nine shorts of the lights record) times IR plus BK
void M3dKit::apply_hook_transform(Word vector_at, Word transform_at, Word lights_at) {
    const RuntimeShortVector3View vector(m_, vector_at);
    const RuntimeShortTransformView transform(m_, transform_at);
    const RuntimeLightSetView lights(m_, lights_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View v0 = gte.v0();
    v0.set_x(vector.x());
    v0.set_y(vector.y());
    v0.set_z(vector.z());
    views::copy_matrix(gte.rotation(), transform.rotation());
    const RuntimeShortVector3View shift = transform.translation();
    const RuntimeIntVector3View translation = gte.translation();
    translation.set_x(shift.x());
    translation.set_y(shift.y());
    translation.set_z(shift.z());
    const RuntimeIntVector3View background = gte.background();
    const RuntimeIntVector3View colour = lights.background();
    background.set_x(colour.x());
    background.set_y(colour.y());
    background.set_z(colour.z());
    math_.rt();
    views::copy_matrix(gte.light(), lights.light());
    math_.llirbk();
}

// 0x004f5540: each column of a 3x3 matrix scaled (GPF12: IR * IR0 >> 12) by its own factor, the three factors being shorts of the model record at +0x28, +0x2a and +0x2c; the nine words are read
// before the first multiplication and written after the last
void M3dKit::scale_object_matrix(Word model_at, Word matrix_at) {
    const RuntimeCollisionObjectView model(m_, model_at);
    const RuntimeQ12MatrixView matrix(m_, matrix_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View ir = gte.ir();
    std::int16_t w[9];
    for (Word k = 0; k < 9; ++k) {
        w[k] = matrix.elements(k);
    }
    const std::int16_t scale[3] = {model.matrix_scale(0), model.matrix_scale(1), model.matrix_scale(2)};
    std::int16_t result[3][3];
    for (Word column = 0; column < 3; ++column) {
        ir.set_x(w[column]);
        ir.set_y(w[column + 3]);
        ir.set_z(w[column + 6]);
        gte.set_ir0(scale[column]);
        math_.interpolate_gpf12();
        result[column][0] = ir.x();
        result[column][1] = ir.y();
        result[column][2] = ir.z();
    }
    for (Word column = 0; column < 3; ++column) {
        matrix.set_elements(column, result[column][0]);
        matrix.set_elements(column + 3, result[column][1]);
        matrix.set_elements(column + 6, result[column][2]);
    }
}

// 0x004f5a70: transform `outer` (a 3x3 and a translation, twelve shorts) applied to the three vectors of `inner` (V0..V2, nine shorts) and to its translation (three more shorts): the result is the
// product in out, the columns first, then the translation
void M3dKit::compose_transforms(Word out_at, Word outer_at, Word inner_at) {
    const RuntimeShortTransformView out(m_, out_at);
    const RuntimeShortTransformView outer(m_, outer_at);
    const RuntimeShortTransformView inner(m_, inner_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    const RuntimeShortVector3View shift_outer = outer.translation();
    const RuntimeIntVector3View translation = gte.translation();
    views::copy_matrix(gte.rotation(), outer.rotation());
    translation.set_x(shift_outer.x());
    translation.set_y(shift_outer.y());
    translation.set_z(shift_outer.z());
    // the three vectors are the words 0..2, 3..5, 6..8 of the inner matrix (V0 at 0x6a3e58, V1 at 0x6a3e60, V2 at 0x6a3e68)
    const RuntimeQ12MatrixView matrix = inner.rotation();
    const RuntimeShortVector3View v0 = gte.v0();
    const RuntimeShortVector3View v1 = gte.v1();
    const RuntimeShortVector3View v2 = gte.v2();
    v1.set_z(matrix.elements(5));
    v0.set_x(matrix.elements(0));
    v2.set_x(matrix.elements(6));
    v2.set_y(matrix.elements(7));
    v0.set_y(matrix.elements(1));
    v0.set_z(matrix.elements(2));
    v1.set_x(matrix.elements(3));
    v1.set_y(matrix.elements(4));
    v2.set_z(matrix.elements(8));
    const RuntimeShortVector3View inner_shift = inner.translation();
    const std::int16_t shift[3] = {inner_shift.x(), inner_shift.y(), inner_shift.z()};
    const RuntimeShortVector3View ir = gte.ir();
    std::int16_t r[3][3];
    math_.rtv0();
    r[0][0] = ir.x();
    r[0][1] = ir.y();
    r[0][2] = ir.z();
    math_.rtv1();
    r[1][0] = ir.x();
    r[1][1] = ir.y();
    r[1][2] = ir.z();
    math_.rtv2();
    r[2][0] = ir.x();
    r[2][1] = ir.y();
    r[2][2] = ir.z();
    ir.set_x(shift[0]);
    ir.set_y(shift[1]);
    ir.set_z(shift[2]);
    math_.rtirtr();
    const std::int16_t t[3] = {ir.x(), ir.y(), ir.z()};
    const RuntimeQ12MatrixView product = out.rotation();
    const RuntimeShortVector3View out_shift = out.translation();
    product.set_elements(0, r[0][0]);
    product.set_elements(3, r[1][0]);
    product.set_elements(4, r[1][1]);
    product.set_elements(5, r[1][2]);
    product.set_elements(6, r[2][0]);
    product.set_elements(7, r[2][1]);
    product.set_elements(1, r[0][1]);
    product.set_elements(8, r[2][2]);
    out_shift.set_z(t[2]);
    product.set_elements(2, r[0][2]);
    out_shift.set_x(t[0]);
    out_shift.set_y(t[1]);
}

// 0x004f5ca0: the columns of `inner` (as vectors V0..V2) through the 3x3 of `outer`, written as rows of `out`; then the translation: out's translation = outer's less (out's rotation times inner's translation)
void M3dKit::compose_with_transpose(Word out_at, Word outer_at, Word inner_at) {
    const RuntimeShortTransformView out(m_, out_at);
    const RuntimeShortTransformView outer(m_, outer_at);
    const RuntimeShortTransformView inner(m_, inner_at);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    views::copy_matrix(gte.rotation(), outer.rotation());
    const RuntimeQ12MatrixView matrix = inner.rotation();
    const RuntimeShortVector3View v0 = gte.v0();
    const RuntimeShortVector3View v1 = gte.v1();
    const RuntimeShortVector3View v2 = gte.v2();
    v1.set_y(matrix.elements(4));
    v1.set_z(matrix.elements(7));
    v0.set_x(matrix.elements(0));
    v0.set_y(matrix.elements(3));
    v0.set_z(matrix.elements(6));
    v1.set_x(matrix.elements(1));
    v2.set_x(matrix.elements(2));
    v2.set_y(matrix.elements(5));
    v2.set_z(matrix.elements(8));
    const RuntimeShortVector3View ir = gte.ir();
    std::int16_t r[3][3];
    math_.rtv0();
    r[0][0] = ir.x();
    r[0][1] = ir.y();
    r[0][2] = ir.z();
    math_.rtv1();
    r[1][0] = ir.x();
    r[1][1] = ir.y();
    r[1][2] = ir.z();
    math_.rtv2();
    const RuntimeQ12MatrixView product = out.rotation();
    product.set_elements(1, r[0][1]);
    product.set_elements(2, r[0][2]);
    product.set_elements(3, r[1][0]);
    product.set_elements(4, r[1][1]);
    product.set_elements(0, r[0][0]);
    product.set_elements(5, r[1][2]);
    product.set_elements(6, ir.x());
    product.set_elements(7, ir.y());
    product.set_elements(8, ir.z());
    views::copy_matrix(gte.rotation(), product);
    const RuntimeShortVector3View inner_shift = inner.translation();
    ir.set_x(inner_shift.x());
    ir.set_y(inner_shift.y());
    ir.set_z(inner_shift.z());
    math_.rtir();
    const RuntimeShortVector3View outer_shift = outer.translation();
    const RuntimeShortVector3View out_shift = out.translation();
    const std::int16_t y = static_cast<std::int16_t>(outer_shift.y() - ir.y());
    const std::int16_t z = static_cast<std::int16_t>(outer_shift.z() - ir.z());
    out_shift.set_x(static_cast<std::int16_t>(outer_shift.x() - ir.x()));
    out_shift.set_z(z);
    out_shift.set_y(y);
}

}  // namespace opentony::runtime::m3d
