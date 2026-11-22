#include "math_views.hpp"

#include <bit>
#include <string>

// Phase 4.14a: the game's math on top of the GTE (0x004e7c60..0x004e85a0, 0x004f3980, 0x004f5f50..0x004f5fc0). The Q12 matrices are nine signed shorts; the rotation builders drive the GTE's
// `gte_rtir` once per column, which is why they leave the RT matrix and the IR / MAC registers behind. The x87 sequences keep the game's precision control: every product, sum and quotient is
// rounded to the precision control's mantissa before the next operation (`r`: 24 or 53 bits), the transcendental instructions are exact, and `_ftol` truncates.

namespace opentony::runtime::mathkit {

using namespace gte;
using namespace views;

// RT = the matrix; for each of the three vectors: IR = vector, rtir(), and the IR becomes that column of the matrix
void MathKit::rotate_columns(std::uint32_t matrix_address, const std::int32_t (&vectors)[3][3]) {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeQ12MatrixView matrix(m_, matrix_address);
    const RuntimeQ12MatrixView rotation = g.rotation();
    const RuntimeShortVector3View ir = g.ir();
    for (std::uint32_t i = 0; i < 9; ++i) {
        rotation.set_elements(i, matrix.elements(i));
    }
    for (std::uint32_t column = 0; column < 3; ++column) {
        for (std::uint32_t k = 0; k < 3; ++k) {
            set_component(ir, k, vectors[column][k]);
        }
        rtir();
        for (std::uint32_t row = 0; row < 3; ++row) {
            matrix.set_elements(3 * row + column, static_cast<std::int16_t>(component(ir, row)));
        }
    }
}

std::uint32_t MathKit::rotate_matrix_x(std::int32_t angle, std::uint32_t matrix) {
    const std::int32_t c = x87::ftol(r(x87::fcos(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t s = x87::ftol(r(x87::fsin(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t vectors[3][3] = {{0x1000, 0, 0}, {0, c, s}, {0, -s, c}};
    rotate_columns(matrix, vectors);
    return matrix;
}

std::uint32_t MathKit::rotate_matrix_y(std::int32_t angle, std::uint32_t matrix) {
    const std::int32_t c = x87::ftol(r(x87::fcos(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t s = x87::ftol(r(x87::fsin(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t vectors[3][3] = {{c, 0, -s}, {0, 0x1000, 0}, {s, 0, c}};
    rotate_columns(matrix, vectors);
    return matrix;
}

std::uint32_t MathKit::rotate_matrix_z(std::int32_t angle, std::uint32_t matrix) {
    const std::int32_t c = x87::ftol(r(x87::fcos(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t s = x87::ftol(r(x87::fsin(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    const std::int32_t vectors[3][3] = {{c, s, 0}, {-s, c, 0}, {0, 0, 0x1000}};
    rotate_columns(matrix, vectors);
    return matrix;
}

namespace {

// the 18 bytes of an identity matrix, as the three builders below start them
void identity_matrix(RetailMemory& m, std::uint32_t matrix_address) {
    const RuntimeQ12MatrixView matrix(m, matrix_address);
    for (std::uint32_t i = 0; i < 9; ++i) {
        matrix.set_elements(i, 0);
    }
    matrix.set_elements(0, 0x1000);
    matrix.set_elements(4, 0x1000);
    matrix.set_elements(8, 0x1000);
}

}  // namespace

// the angles are a RuntimeShortVector3: x, y, z (4096 per turn)
std::uint32_t MathKit::rotation_from_angles(std::uint32_t angles_address, std::uint32_t matrix) {
    const RuntimeShortVector3View angles(m_, angles_address);
    identity_matrix(m_, matrix);
    rotate_matrix_y(angles.y(), matrix);
    rotate_matrix_x(angles.x(), matrix);
    rotate_matrix_z(angles.z(), matrix);
    return matrix;
}

std::uint32_t MathKit::rotation_from_angles_zyx(std::uint32_t angles_address, std::uint32_t matrix) {
    const RuntimeShortVector3View angles(m_, angles_address);
    identity_matrix(m_, matrix);
    rotate_matrix_z(angles.z(), matrix);
    rotate_matrix_y(angles.y(), matrix);
    rotate_matrix_x(angles.x(), matrix);
    return matrix;
}

std::uint32_t MathKit::rotation_from_angles_again(std::uint32_t angles, std::uint32_t matrix) {
    return rotation_from_angles(angles, matrix);
}

// 0x004e8200: out = (left * right) >> 12 in a local matrix, then 32 bytes of that local (its 18 bytes and 14 of stack) are copied to `out`
void MathKit::matrix_multiply(std::uint32_t out_address, std::uint32_t left_address, std::uint32_t right_address) {
    const RuntimeMatrixTransformView out(m_, out_address);
    const RuntimeQ12MatrixView left(m_, left_address);
    const RuntimeQ12MatrixView right(m_, right_address);
    std::int16_t product[9];
    for (std::uint32_t i = 0; i < 3; ++i) {
        for (std::uint32_t j = 0; j < 3; ++j) {
            const std::uint32_t sum = static_cast<std::uint32_t>(element(left, i, 0)) * static_cast<std::uint32_t>(element(right, 0, j))
                + static_cast<std::uint32_t>(element(left, i, 2)) * static_cast<std::uint32_t>(element(right, 2, j))
                + static_cast<std::uint32_t>(element(left, i, 1)) * static_cast<std::uint32_t>(element(right, 1, j));
            product[3 * i + j] = static_cast<std::int16_t>(static_cast<std::int32_t>(sum) >> 12);
        }
    }
    const RuntimeQ12MatrixView rotation = out.rotation();
    for (std::uint32_t k = 0; k < 9; ++k) {
        rotation.set_elements(k, product[k]);
    }
    scrap_.emplace_back(out.address() + guest::kRuntimeQ12MatrixSize, guest::kRuntimeMatrixTransformSize - guest::kRuntimeQ12MatrixSize);
}

// 0x004e8280: the matrix of three angles, as Rz * Rx * Ry built with the multiply above
std::uint32_t MathKit::rotation_chain(std::uint32_t angles_address, std::uint32_t matrix_address) {
    const RuntimeShortVector3View angles(m_, angles_address);
    const RuntimeQ12MatrixView matrix(m_, matrix_address);
    const auto trig = [this](std::int32_t angle, std::int32_t& c, std::int32_t& s) {
        c = x87::ftol(r(x87::fcos(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
        s = x87::ftol(r(x87::fsin(x87::angle_to_radians(precision_, angle)) * x87::kQ12));
    };
    std::int32_t c = 0, s = 0;
    trig(angles.y(), c, s);
    for (std::uint32_t k = 0; k < 9; ++k) {
        matrix.set_elements(k, 0);
    }
    matrix.set_elements(2, static_cast<std::int16_t>(s));
    matrix.set_elements(0, static_cast<std::int16_t>(c));
    matrix.set_elements(4, 0x1000);
    matrix.set_elements(6, static_cast<std::int16_t>(-s));
    matrix.set_elements(8, static_cast<std::int16_t>(c));
    // (the local matrices live in the function's frame: the multiply's own frame is not ours to model, so each is written to scratch memory)
    const std::uint32_t local = pcplatform::kScratchBase + 0x200;
    const RuntimeQ12MatrixView local_matrix(m_, local);
    const auto build = [&](const std::int16_t (&words)[9]) {
        for (std::uint32_t k = 0; k < 9; ++k) {
            local_matrix.set_elements(k, words[k]);
        }
    };
    trig(angles.x(), c, s);
    const std::int16_t rx[9] = {0x1000, 0, 0, 0, static_cast<std::int16_t>(c), static_cast<std::int16_t>(-s), 0, static_cast<std::int16_t>(s), static_cast<std::int16_t>(c)};
    build(rx);
    matrix_multiply(matrix_address, local, matrix_address);
    trig(angles.z(), c, s);
    const std::int16_t rz[9] = {static_cast<std::int16_t>(c), static_cast<std::int16_t>(-s), 0, static_cast<std::int16_t>(s), static_cast<std::int16_t>(c), 0, 0, 0, 0x1000};
    build(rz);
    matrix_multiply(matrix_address, local, matrix_address);
    return matrix_address;
}

std::int32_t MathKit::isqrt(std::int32_t value) {
    if (value <= 0) {
        return 0;
    }
    return x87::ftol(r(x87::fsqrt(static_cast<long double>(value))));
}

// (the reads and writes interleave, as retail's do: a transpose in place sees the elements it has already overwritten)
void MathKit::matrix_transpose(std::uint32_t source_address, std::uint32_t destination_address) {
    const RuntimeQ12MatrixView source(m_, source_address);
    const RuntimeQ12MatrixView destination(m_, destination_address);
    static constexpr std::uint32_t kFrom[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
    for (std::uint32_t k = 0; k < 9; ++k) {
        destination.set_elements(k, source.elements(kFrom[k]));
    }
}

void MathKit::matrix_set_translation(std::uint32_t matrix_address, std::uint32_t vector_address) {
    const RuntimeMatrixTransformView matrix(m_, matrix_address);
    const RuntimeIntVector3View vector(m_, vector_address);
    const std::int32_t x = vector.x(), y = vector.y(), z = vector.z();
    const RuntimeIntVector3View translation = matrix.translation();
    translation.set_x(x);
    translation.set_y(y);
    translation.set_z(z);
}

// 0x004e8480: out = vector * 4096 / |vector| (truncated); returns the squared length the x87 sum gives. All of it at 24-bit precision.
std::int32_t MathKit::normalize(std::uint32_t vector_address, std::uint32_t out_address) {
    const RuntimeIntVector3View vector(m_, vector_address);
    const RuntimeIntVector3View out(m_, out_address);
    const long double x = vector.x(), y = vector.y(), z = vector.z();
    const long double sum = r(r(r(z * z) + r(y * y)) + r(x * x));
    const std::int32_t squared = x87::ftol(sum);
    const std::int32_t length = isqrt(squared);
    const long double parts[3] = {x, y, z};
    for (std::uint32_t k = 0; k < 3; ++k) {
        long double v = r(parts[k] * x87::kQ12);
        if (length > 0) {
            v = r(v / static_cast<long double>(length));
        }
        set_component(out, k, static_cast<std::uint32_t>(x87::ftol(v)));
    }
    return squared;
}

std::int32_t MathKit::ratan2(std::int32_t y, std::int32_t x) {
    return x87::ftol(r(x87::fpatan(static_cast<long double>(y), static_cast<long double>(x)) * x87::kRadiansToAngle));
}

std::int32_t MathKit::atan_slope(std::int32_t value) {
    const long double slope = r(static_cast<long double>(value) * x87::kQuarterTurnStep);
    return x87::ftol(r(x87::fpatan(slope, 1.0L) * x87::kRadiansToAngle));
}

// 0x004e85a0: the matrix goes into RT, the vector is multiplied rows 1, 2, 0 (the order of the retail sums), each sum rounded to a float
std::uint32_t MathKit::convert_vector(std::uint32_t matrix_address, std::uint32_t vector_address, std::uint32_t out_address) {
    const RuntimeGteRegisterFileView g(m_, kRegisters);
    const RuntimeQ12MatrixView matrix(m_, matrix_address);
    const RuntimeIntVector3View vector(m_, vector_address);
    const RuntimeIntVector3View out(m_, out_address);
    const RuntimeQ12MatrixView rotation = g.rotation();
    for (std::uint32_t i = 0; i < 9; ++i) {
        rotation.set_elements(i, matrix.elements(i));
    }
    const long double x = vector.x(), y = vector.y(), z = vector.z();
    const auto m = [&](std::uint32_t i) { return static_cast<long double>(matrix.elements(i)); };
    const long double row1 = r(r(r(r(m(5) * z) + r(m(3) * x)) + r(m(4) * y)) * x87::kInvQ12);
    const long double row2 = r(r(r(r(m(6) * x) + r(m(7) * y)) + r(m(8) * z)) * x87::kInvQ12);
    const long double row0 = r(r(r(r(m(0) * x) + r(m(2) * z)) + r(m(1) * y)) * x87::kInvQ12);
    out.set_x(x87::ftol(row0));
    out.set_y(x87::ftol(row1));
    out.set_z(x87::ftol(row2));
    return out_address;
}

std::int32_t MathKit::cross_sign(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) {
    const long double sum = r(r(static_cast<long double>(a) * b) + r(static_cast<long double>(c) * d));
    const std::int32_t value = x87::ftol(r(sum * x87::kInvQ12));
    if (value == 0) {
        return 0;
    }
    return value > 0 ? 1 : -1;
}

std::int32_t MathKit::dot_q12(std::uint32_t a_address, std::uint32_t b_address) {
    const RuntimeIntVector3View a(m_, a_address);
    const RuntimeIntVector3View b(m_, b_address);
    const auto product = [&](std::uint32_t k) { return r(static_cast<long double>(static_cast<std::int32_t>(component(a, k))) * static_cast<std::int32_t>(component(b, k))); };
    const long double sum = r(r(product(2) + product(1)) + product(0));
    return x87::ftol(r(sum * x87::kInvQ12));
}

std::int32_t MathKit::mul_q12(std::int32_t a, std::int32_t b) {
    return x87::ftol(r(r(static_cast<long double>(a) * b) * x87::kInvQ12));
}

// 0x004ca0a0: the arccosine of value / 4096 in 4096 per turn: ftol(acos(v / 4096) * 4096 * (1 / 2 pi)); the C runtime's acos is (fpatan of sqrt((1 + x)(1 - x)) over x), exact at +-1
std::uint32_t MathKit::acos_ratio_to_angle(std::int32_t value) {
    long double x = static_cast<long double>(value) * x87::kInvQ12;
    long double angle;
    if (value == 4096) {
        angle = 0.0L;
    } else if (value == -4096) {
        angle = 3.14159265358979323846264338327950288L;
    } else {
        const long double product = r((1.0L + x) * (1.0L - x));
        const long double root = r(x87::fsqrt(product));
        angle = x87::fpatan(root, x);
    }
    angle = r(angle * x87::kQ12);
    angle = r(angle * std::bit_cast<double>(0x3fc45f306dc9c883ull));       // 1 / (2 pi) as the executable stores it (0x005198f8)
    return static_cast<std::uint32_t>(x87::ftol(angle));
}

}  // namespace opentony::runtime::mathkit
