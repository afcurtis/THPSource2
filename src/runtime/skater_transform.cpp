#include "skater_views.hpp"

// Phase 4.15a: the temporary transform of a skater (physics.cpp, 0x0048ec00): the orientation and position are saved, then the orientation is turned by the rotation the skater's pending tricks ask for (the
// matrix is multiplied by a rotation about the skater's own centre, 0x46000 units above the feet, and the position moved to keep the centre in place), and by a lean that follows the balance bar. The
// matrices are 3x3 shorts at +0x2e58 (nine shorts, the translation words follow at +0x14); the GTE multiplies a rotation by the columns of the skater's matrix one at a time.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

namespace gte = mathkit::gte;

constexpr Word kTemporaries = pcplatform::kScratchBase + 0x800;
constexpr Word kRotation = kTemporaries;                  // a rotation matrix (0x20 bytes)
constexpr Word kZero = kTemporaries + 0x20;               // a vector of zeros
constexpr Word kCentre = kTemporaries + 0x30;             // the vector from the feet to the centre
constexpr Word kBefore = kTemporaries + 0x40;             // the centre before and after the turn
constexpr Word kAfter = kTemporaries + 0x50;
constexpr Word kAngles = kTemporaries + 0x60;             // three shorts
constexpr Word kLean = kTemporaries + 0x70;               // the lean's rotation matrix (0x20 bytes)
constexpr Word kBuildTransform = 0x00461bf0;              // M3d_BuildTransform(skater)

}  // namespace

// the orientation matrix (nine shorts at +0x2e58) multiplied by a rotation (`source`, nine shorts) one column at a time: the GTE's rotation registers hold the orientation as it was, the columns of the
// rotation go through them (RTIR) and the results replace the columns of the orientation
void SkaterKit::rotate_orientation(Word self, Word source) {
    const Word matrix = self + 0x2e58;
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, gte::kRotation + 2 * k, half_at(m_, matrix + 2 * k));
    }
    for (Word column = 0; column < 3; ++column) {
        set_global_half(m_, gte::kIr, half_at(m_, source + 2 * column));
        set_global_half(m_, gte::kIr + 2, half_at(m_, source + 2 * (column + 3)));
        set_global_half(m_, gte::kIr + 4, half_at(m_, source + 2 * (column + 6)));
        math_.rtir();
        set_half_at(m_, matrix + 2 * column, global_half(m_, gte::kIr));
        set_half_at(m_, matrix + 2 * (column + 3), global_half(m_, gte::kIr + 2));
        set_half_at(m_, matrix + 2 * (column + 6), global_half(m_, gte::kIr + 4));
    }
}

// 0x0048ec00 (fastcall)
void SkaterKit::temp_transform(Word self) {
    const Word matrix = self + 0x2e58;
    for (Word k = 0; k < 8; ++k) {
        set_word_at(m_, self + skater::kSavedOrientation + 4 * k, word_at(m_, matrix + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + skater::kSavedPosition + 4 * k, word_at(m_, self + skater::kPosition + 4 * k));
    }
    const auto turn_columns = [this, self](Word source) { rotate_orientation(self, source); };
    if (skater_at(m_, self).field_2c88() != 0 || word_at(m_, self + 0x2cac) != 0 || word_at(m_, self + 0x2cb4) != 0) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kZero + 4 * k, 0);
        }
        math_.matrix_set_translation(matrix, kZero);
        math_.matrix_set_translation(kRotation, kZero);
        set_global_word(m_, kCentre, 0);
        set_global_word(m_, kCentre + 4, 0x46000);
        set_global_word(m_, kCentre + 8, 0);
        math_.convert_vector(matrix, kCentre, kBefore);
        set_global_half(m_, kAngles, half_at(m_, self + 0x2cac));
        set_global_half(m_, kAngles + 2, 0);
        set_global_half(m_, kAngles + 4, half_at(m_, self + 0x2cb4) & 0xfff);
        if (skater_at(m_, self).field_2c88() != 0) {
            set_global_half(m_, kAngles, half_at(m_, self + 0x2c8c));
        }
        set_global_half(m_, kAngles, global_half(m_, kAngles) & 0xfff);
        math_.rotation_from_angles_zyx(kAngles, kRotation);
        turn_columns(kRotation);
        math_.convert_vector(matrix, kCentre, kAfter);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + skater::kPosition + 4 * k, word_at(m_, self + skater::kPosition + 4 * k) + (word_at(m_, kBefore + 4 * k) - word_at(m_, kAfter + 4 * k)));
        }
    }
    if (skater_at(m_, self).field_2f14() != 0) {
        const std::int32_t lean = s(u(s(skater_at(m_, self).field_2f14()) / 10) * 0x50u);
        set_global_half(m_, kAngles, u(lean / 100) & 0xfff);
        set_global_half(m_, kAngles + 2, 0);
        set_global_half(m_, kAngles + 4, 0);
        math_.rotation_from_angles(kAngles, kLean);
        turn_columns(kLean);
        fru_from_rotation_matrix(self);
        engine(kBuildTransform, {self});
        return;
    }
    engine(kBuildTransform, {self});
}

// 0x0049c330 (fastcall): the skater is turned upright a little: the cross product of its forward vector (+0x30f4) with the world's up vector (0x0056b7c0) gives a sideways vector and its dot product with the
// skater's up vector (+0x310c) says which way the skater leans; beyond +-41 (Q12) the orientation is turned by a fixed angle about z (0xb, or -0xb) through the GTE, then the basis is rebuilt from
// the matrix. Neither the state 2 nor the state 0 should get here (assertions)
void SkaterKit::upright_correction(Word self) {
    constexpr Word kMsgInvisible = 0x00536eb4;
    constexpr Word kMsgGround = 0x00536ea0;
    constexpr Word kWorldUp = 0x0056b7c0;
    constexpr Word kSideways = kTemporaries + 0x90;
    check(skater_at(m_, self).physics_state() != 2, kMsgInvisible);
    check(skater_at(m_, self).physics_state() != 0, kMsgGround);
    set_global_half(m_, gte::kRotation, half_at(m_, self + 0x30f4));
    set_global_half(m_, gte::kRotation + 8, half_at(m_, self + 0x30f8));
    set_global_half(m_, gte::kRotation + 16, half_at(m_, self + 0x30fc));
    set_global_half(m_, gte::kIr, global_half(m_, kWorldUp));
    set_global_half(m_, gte::kIr + 2, global_half(m_, kWorldUp + 4));
    set_global_half(m_, gte::kIr + 4, global_half(m_, kWorldUp + 8));
    math_.cross_sf12();
    set_global_word(m_, kSideways, global_word(m_, gte::kMac));
    set_global_word(m_, kSideways + 4, global_word(m_, gte::kMac + 4));
    set_global_word(m_, kSideways + 8, global_word(m_, gte::kMac + 8));
    const std::int32_t lean = math_.dot_q12(self + 0x310c, kSideways);
    if (lean > 0x29 || lean < -0x29) {
        set_global_half(m_, kAngles, 0);
        set_global_half(m_, kAngles + 2, 0);
        set_global_half(m_, kAngles + 4, lean > 0x29 ? 0xb : 0xff5);
        math_.rotation_from_angles(kAngles, kRotation);
        rotate_orientation(self, kRotation);
    } else {
        math_.dot_q12(self + 0x310c, kWorldUp);       // (the answer is not used)
    }
    fru_from_rotation_matrix(self);
}

}  // namespace opentony::runtime::skaterkit
