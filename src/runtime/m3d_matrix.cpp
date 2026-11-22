#include "m3d_views.hpp"

// Phase 4.15a: the small matrix and GTE register helpers of the M3D transform code (0x004f4050..0x004f5f10).

namespace opentony::runtime::m3d {

using namespace views;

// 0x004f5390
Word M3dKit::rotation_from_angles(Word angles, Word matrix) { return math_.rotation_from_angles(angles, matrix); }

// 0x004f50d0: the background colour BK (GTE 0x006a3e48..0x006a3e50) from a vector of three dwords
void M3dKit::set_background_colour(Word vector) {
    const RuntimeIntVector3View source(m_, vector);
    const RuntimeIntVector3View background = gte_file(m_).background();
    background.set_x(source.x());
    background.set_y(source.y());
    background.set_z(source.z());
}

// 0x004f50f0: the translation TR from a Q12 vector in whole units (shifted down by 12)
void M3dKit::set_translation(Word vector) {
    const RuntimeIntVector3View source(m_, vector);
    const RuntimeIntVector3View translation = gte_file(m_).translation();
    translation.set_x(source.x() >> 12);
    translation.set_y(source.y() >> 12);
    translation.set_z(source.z() >> 12);
}

// 0x004f5f10: `fild a; fimul b; fidiv c; _ftol`: the product and the quotient rounded to the precision control, then truncated (a division by zero or an overflow is the integer indefinite)
Word M3dKit::mul_div_truncated(std::int32_t a, std::int32_t b, std::int32_t c) {
    const auto precision = math_.precision();
    const long double product = mathkit::x87::round_to(precision, static_cast<long double>(a) * b);
    const long double quotient = mathkit::x87::round_to(precision, product / c);
    return u(mathkit::x87::ftol(quotient));
}

// 0x004f5430: 0x1000 on the diagonal, 0 elsewhere
void M3dKit::set_identity_matrix(Word matrix) {
    const RuntimeQ12MatrixView target(m_, matrix);
    target.set_elements(8, 0x1000);
    target.set_elements(4, 0x1000);
    target.set_elements(0, 0x1000);
    for (const Word element : {7u, 6u, 5u, 3u, 2u, 1u}) {
        target.set_elements(element, 0);
    }
}

// 0x004f53e0: the transpose, copied word by word in this order (a matrix that is its own destination gets a different, partly transposed result)
void M3dKit::transpose_matrix(Word source, Word destination) {
    static constexpr Word kMap[9][2] = {{0, 0}, {3, 1}, {6, 2}, {1, 3}, {4, 4}, {7, 5}, {2, 6}, {5, 7}, {8, 8}};   // (source element, destination element)
    const RuntimeQ12MatrixView from(m_, source);
    const RuntimeQ12MatrixView to(m_, destination);
    for (const auto& pair : kMap) {
        to.set_elements(pair[1], from.elements(pair[0]));
    }
}

// 0x004f5730
void M3dKit::copy_matrix(Word source, Word destination) { views::copy_matrix(RuntimeQ12MatrixView(m_, destination), RuntimeQ12MatrixView(m_, source)); }

// 0x004f56c0: the matrix into the rotation registers, then RTIR (RT times IR) on whatever IR holds
void M3dKit::load_rotation_and_rotate(Word matrix) {
    views::copy_matrix(gte_file(m_).rotation(), RuntimeQ12MatrixView(m_, matrix));
    math_.rtir();
}

// 0x004f4fe0: nine shorts into RT, then three sign-extended shorts into TR
void M3dKit::load_rotation_and_translation(Word matrix) {
    const RuntimeShortTransformView transform(m_, matrix);
    const RuntimeGteRegisterFileView gte = gte_file(m_);
    views::copy_matrix(gte.rotation(), transform.rotation());
    const RuntimeShortVector3View shift = transform.translation();
    const RuntimeIntVector3View translation = gte.translation();
    translation.set_x(shift.x());
    translation.set_y(shift.y());
    translation.set_z(shift.z());
}

// 0x004f5460: RT times each column of a matrix (RTIR; the column c of a row-major 3x3 matrix holds the words c, c + 3, c + 6), each column's result written before the next one is read
void M3dKit::multiply_by_rotation(Word source, Word destination) {
    const RuntimeQ12MatrixView from(m_, source);
    const RuntimeQ12MatrixView to(m_, destination);
    const RuntimeShortVector3View ir = gte_file(m_).ir();
    for (Word column = 0; column < 3; ++column) {
        ir.set_x(from.elements(column));
        ir.set_y(from.elements(column + 3));
        ir.set_z(from.elements(column + 6));
        math_.rtir();
        to.set_elements(column, ir.x());
        to.set_elements(column + 3, ir.y());
        to.set_elements(column + 6, ir.z());
    }
}

// 0x004f5780: the light matrix times each column of a matrix (LCIR); all three columns are read before any result is written
void M3dKit::multiply_by_light(Word source, Word destination) {
    const RuntimeQ12MatrixView from(m_, source);
    const RuntimeQ12MatrixView to(m_, destination);
    const RuntimeShortVector3View ir = gte_file(m_).ir();
    std::int16_t results[9];
    for (Word column = 0; column < 3; ++column) {
        ir.set_x(from.elements(column));
        ir.set_y(from.elements(column + 3));
        ir.set_z(from.elements(column + 6));
        math_.lcir();
        results[3 * column] = ir.x();
        results[3 * column + 1] = ir.y();
        results[3 * column + 2] = ir.z();
    }
    for (Word column = 0; column < 3; ++column) {
        to.set_elements(column, results[3 * column]);
        to.set_elements(column + 3, results[3 * column + 1]);
        to.set_elements(column + 6, results[3 * column + 2]);
    }
}

// 0x004f4050: the corners of a box: per axis the lower value goes into `low`, the higher into `high` (flag bit 0, 1, 2 toggled for every axis swapped); the extents (high - low) go into the RT diagonal
void M3dKit::order_corners(Word low_at, Word high_at, Word flags) {
    const RuntimeIntVector3View low(m_, low_at);
    const RuntimeIntVector3View high(m_, high_at);
    const Word axis_bit[3] = {1, 2, 4};
    for (Word axis = 0; axis < 3; ++axis) {
        const std::int32_t upper = static_cast<std::int32_t>(component(high, axis));
        const std::int32_t lower = static_cast<std::int32_t>(component(low, axis));
        if (upper < lower) {
            set_component(high, axis, u(lower));
            set_component(low, axis, u(upper));
            write_scalar<Word>(m_, flags, read_scalar<Word>(m_, flags) ^ axis_bit[axis]);
        }
    }
    const RuntimeQ12MatrixView rotation = gte_file(m_).rotation();
    rotation.set_elements(4, static_cast<std::int16_t>(high.y() - low.y()));
    rotation.set_elements(0, static_cast<std::int16_t>(high.x() - low.x()));
    rotation.set_elements(8, static_cast<std::int16_t>(high.z() - low.z()));
}

// 0x004f40d0: a box given in Q12 (two corners of three dwords) to whole units, the flags cleared, the corners ordered
void M3dKit::corners_to_units(Word box_at, Word low_at, Word high_at, Word flags) {
    const RuntimeCullBoxView box(m_, box_at);
    const RuntimeIntVector3View low(m_, low_at);
    const RuntimeIntVector3View high(m_, high_at);
    const RuntimeIntVector3View box_low = box.low_q12();
    const RuntimeIntVector3View box_high = box.high_q12();
    for (Word k = 0; k < 3; ++k) {
        set_component(low, k, u(static_cast<std::int32_t>(component(box_low, k)) >> 12));
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(high, k, u(static_cast<std::int32_t>(component(box_high, k)) >> 12));
    }
    write_scalar<Word>(m_, flags, 0);
    order_corners(low_at, high_at, flags);
}

}  // namespace opentony::runtime::m3d
