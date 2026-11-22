#pragma once

#include "pc_platform.hpp"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// 11b (docs/PORT64_AUDIT.md D5): the x87 emulation below computes in `long double` and needs its 64-bit mantissa; x86-64 MinGW and Linux have it, MSVC does not.
static_assert(LDBL_MANT_DIG == 64, "long double must be the x87 80-bit extended type");

namespace opentony::runtime::mathkit {

// Phase 4.14a: the game's math layer. The PC build emulates the PlayStation's geometry engine (GTE) in plain C: the "registers" are globals in 0x006a3e00..0x006a3f20, every libgte call of the PS1 code
// is a function that reads some of them, writes the MAC / IR registers and clamps (`gte_*`, `mygte_*`), and the fixed-point helpers on top of them (rotation matrices of three angles, vector
// normalisation, arctangent, the Q12 dot and cross products) are the game's own. Like the rest of Phase 4 the functions work on the retail memory at the retail addresses, so a captured image replays
// byte for byte; the CRT's `_ftol` (0x005004f4), `pow` (0x00501740) and the x87 transcendental instructions are reproduced by `x87` below, with the game's 24-bit precision control.
namespace gte {

constexpr std::uint32_t kH = 0x006a3e00;               // dword: distance of the projection plane (RTPS / RTPT divide it by SZ)
constexpr std::uint32_t kScreen = 0x006a3e04;          // six shorts: SX0 SY0 SX1 SY1 SX2 SY2 (RTPS fills the middle pair, RTPT all three in its own order)
constexpr std::uint32_t kRotation = 0x006a3e10;        // nine shorts, row major: the RT matrix (rows 0x6a3e10, 0x6a3e16, 0x6a3e1c)
constexpr std::uint32_t kIr0 = 0x006a3e34;             // short: the interpolation factor of GPF / GPL
constexpr std::uint32_t kBackground = 0x006a3e48;      // three dwords: the background colour (BK) the light ops add
constexpr std::uint32_t kV0 = 0x006a3e58;              // three shorts each, 8 bytes apart
constexpr std::uint32_t kV1 = 0x006a3e60;
constexpr std::uint32_t kV2 = 0x006a3e68;
constexpr std::uint32_t kRgbFifo = 0x006a3e70;         // three RGB triples (RGB0, RGB1, RGB2 at +0, +4, +8)
constexpr std::uint32_t kLight = 0x006a3e80;           // nine shorts: the light matrix (LLM)
constexpr std::uint32_t kTranslation = 0x006a3ea0;     // three dwords: TR
constexpr std::uint32_t kIr = 0x006a3eb0;              // three shorts: IR1 IR2 IR3
constexpr std::uint32_t kMac = 0x006a3eb8;             // three dwords: MAC1 MAC2 MAC3
constexpr std::uint32_t kColour = 0x006a3ec8;          // nine shorts: the colour matrix (LCM)
constexpr std::uint32_t kOfx = 0x006a3ee8;
constexpr std::uint32_t kOfy = 0x006a3eec;
constexpr std::uint32_t kOtz = 0x006a3ef8;             // short: the average Z AVSZ4 leaves
constexpr std::uint32_t kSz = 0x006a3efa;              // four shorts: SZ0 SZ1 SZ2 SZ3 (the RTPT vertices land in SZ3, SZ2 and SZ1)
constexpr std::uint32_t kLzcr = 0x006a3f04;
constexpr std::uint32_t kMac0 = 0x006a3f0c;            // NCLIP's result, stored twice
constexpr std::uint32_t kMac0b = 0x006a3f10;
constexpr std::uint32_t kWindow = 0x006a3e00;          // the whole register file the probe captures
constexpr std::uint32_t kWindowBytes = 0x120;

}  // namespace gte

// What the retail code gets from the x87 unit. Its precision control decides how the products, sums and quotients round: the game runs the render window at 24 bits (it sets the control word itself) and
// everything else at the process default of 53, which is what the forced calls of the capture see. `_ftol` truncates.
namespace x87 {

enum class Precision { k24, k53 };

// 0x005004f4 `_ftol`: truncate toward zero; a value that does not fit 64 bits is the "integer indefinite", whose low dword is 0
inline std::int32_t ftol(long double value) {
    if (!(std::fabs(value) < 9223372036854775808.0L)) {
        return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))));
}
// the result of an fadd / fsub / fmul / fdiv / fsqrt at the given precision
inline long double round_to(Precision precision, long double value) {
    return precision == Precision::k24 ? static_cast<long double>(static_cast<float>(value)) : static_cast<long double>(static_cast<double>(value));
}
// the transcendental instructions always work at full precision
inline long double fcos(long double x) {
#if defined(__x86_64__) || defined(__i386__)
    long double r = x;
    __asm__("fcos" : "+t"(r));
    return r;
#else
    return std::cos(x);
#endif
}
inline long double fsin(long double x) {
#if defined(__x86_64__) || defined(__i386__)
    long double r = x;
    __asm__("fsin" : "+t"(r));
    return r;
#else
    return std::sin(x);
#endif
}
// fpatan: atan(y / x) with the quadrant of both
inline long double fpatan(long double y, long double x) {
#if defined(__x86_64__) || defined(__i386__)
    long double r;
    __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
#else
    return std::atan2(y, x);
#endif
}
inline long double fsqrt(long double x) { return std::sqrt(x); }

constexpr float kQuarterTurnStep = 0.000244140625F;       // 0x00518910 (1 / 4096)
constexpr float kTwoPi = 6.2831854820251465F;             // 0x00519a08
constexpr double kQ12 = 4096.0;                           // 0x00519900 (and the float at 0x005199bc)
constexpr double kInvQ12 = 0.000244140625;                // 0x00519908
constexpr double kRadiansToAngle = 651.8986287636712;     // 0x00519a10 (4096 / 2 pi)

// the angle (4096 per turn) in radians the way 0x004e7c60 and its relatives build it: the integer, times 1/4096, times 2 pi, each product rounded
inline long double angle_to_radians(Precision precision, std::int32_t angle) {
    return round_to(precision, round_to(precision, static_cast<long double>(angle) * kQuarterTurnStep) * kTwoPi);
}
inline std::int32_t cos_q12(Precision precision, std::int32_t angle) { return ftol(round_to(precision, fcos(angle_to_radians(precision, angle)) * kQ12)); }
inline std::int32_t sin_q12(Precision precision, std::int32_t angle) { return ftol(round_to(precision, fsin(angle_to_radians(precision, angle)) * kQ12)); }

}  // namespace x87

class MathKit final : public pcplatform::PcKit {
public:
    MathKit(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    // ---- the GTE: clamps and the colour conversion ----
    void clamp_ir_signed();                                                          // 0x004e2070 Math_CrossResultAngleClamp: IR = MAC clamped to [-32768, 32767]
    void clamp_ir_unsigned();                                                        // 0x004e2130 mygte_lvtosvU: IR = MAC clamped to [0, 32767]
    void colour_from_ir();                                                           // 0x004e21e0 mygte_svtorgb: IR >> 4 clamped to a byte into the three RGB slots
    void leading_zero_count(std::uint32_t value);                                    // 0x004e22b0 gte_ldlzc
    void average_z4();                                                               // 0x004e22f0 gte_avsz4 (no retail caller)
    void interpolate_gpf12();                                                        // 0x004e23a0 gte_gpf12
    void interpolate_gpl0();                                                         // 0x004e23f0 gte_gpl0
    void interpolate_gpl12();                                                        // 0x004e2450 gte_gpl12
    // matrix times vector (+ translation or background), >> 12 unless named sf0, then the signed (lm clear) or unsigned (lm set) clamp:
    void lcv0();                                                                     // 0x004e24b0 gte_lcv0
    void lcv0_lm();                                                                  // 0x004e2550 gte_lcv0_lm
    void lcv1();                                                                     // 0x004e25f0 gte_lcv1
    void lcv1_lm();                                                                  // 0x004e2690 gte_lcv1_lm
    void lcv2();                                                                     // 0x004e2730 gte_lcv2
    void lcir();                                                                     // 0x004e27d0 gte_lcir
    void lcirtr();                                                                   // 0x004e2870 gte_lcirtr
    void llir_sf0();                                                                 // 0x004e2930 gte_llir_sf0
    void llirbk();                                                                   // 0x004e29d0 gte_llirbk
    void llv0();                                                                     // 0x004e2a90 gte_llv0
    void llv0bk();                                                                   // 0x004e2b30 gte_llv0bk
    void llv0bk_lm();                                                                // 0x004e2bf0 gte_llv0bk_lm
    void llv1bk();                                                                   // 0x004e2cb0 gte_llv1bk
    void llv1bk_lm();                                                                // 0x004e2d70 gte_llv1bk_lm
    void llv2bk();                                                                   // 0x004e2e30 gte_llv2bk
    void mvmva(std::uint32_t mode, std::uint32_t source);                            // 0x004e2ef0 gte_mvmva (its 1st and 3rd arguments choose RT x V0 (shifted or not) or RT x IR; nothing else exists)
    void nclip();                                                                    // 0x004e2f20 gte_nclip
    void cross_sf0();                                                                // 0x004e2f80 Fixed_CrossProductS16 (gte_op0)
    void cross_sf12();                                                               // 0x004e2ff0 Math_CrossFixed12 (gte_op12)
    void rt();                                                                       // 0x004e3070 gte_rt
    void rtir();                                                                     // 0x004e3130 gte_rtir
    void rtirtr();                                                                   // 0x004e31d0 gte_rtirtr
    void rtir_sf0();                                                                 // 0x004e3290 gte_rtir_sf0
    void rtps();                                                                     // 0x004e3330 gte_rtps
    void rtpt();                                                                     // 0x004e34e0 gte_rtpt
    void rtv0();                                                                     // 0x004e39a0 Fixed_MatrixMultiplyQ12 (gte_rtv0)
    void rtv0tr();                                                                   // 0x004e3a40 gte_rtv0tr
    void rtv0_sf0();                                                                 // 0x004e3b00 gte_rtv0_sf0
    void rtv1();                                                                     // 0x004e3ba0 gte_rtv1
    void rtv2();                                                                     // 0x004e3c40 gte_rtv2
    void sqr0();                                                                     // 0x004e3ce0 gte_sqr0

    // ---- the game's math on top of the GTE ----
    std::uint32_t rotate_matrix_x(std::int32_t angle, std::uint32_t matrix);         // 0x004e7c60 Math_RotateMatrixX
    std::uint32_t rotate_matrix_y(std::int32_t angle, std::uint32_t matrix);         // 0x004e7de0 Math_RotateMatrixY
    std::uint32_t rotate_matrix_z(std::int32_t angle, std::uint32_t matrix);         // 0x004e7f60 Math_RotateMatrixZ
    std::uint32_t rotation_from_angles(std::uint32_t angles, std::uint32_t matrix);  // 0x004e80e0 Math_RotationMatrixFromAngles (Y, then X, then Z)
    std::uint32_t rotation_from_angles_zyx(std::uint32_t angles, std::uint32_t matrix);   // 0x004e8140 (Z, Y, X)
    std::uint32_t rotation_from_angles_again(std::uint32_t angles, std::uint32_t matrix); // 0x004e81a0 (the same as 0x004e80e0, compiled twice)
    void matrix_multiply(std::uint32_t out, std::uint32_t left, std::uint32_t right);     // 0x004e8200 Math_MatrixMultiplyQ12
    std::uint32_t rotation_chain(std::uint32_t angles, std::uint32_t matrix);        // 0x004e8280 Math_RotationMatrixChain
    std::int32_t isqrt(std::int32_t value);                                          // 0x004e83e0 Math_IntSqrt
    void matrix_transpose(std::uint32_t source, std::uint32_t destination);          // 0x004e8410 Math_MatrixTranspose
    void matrix_set_translation(std::uint32_t matrix, std::uint32_t vector);         // 0x004e8460 Matrix_SetTranslation
    std::int32_t normalize(std::uint32_t vector, std::uint32_t out);                 // 0x004e8480 Vector_NormalizeQ12
    std::int32_t ratan2(std::int32_t y, std::int32_t x);                             // 0x004e8560 orig_ratan2
    std::int32_t atan_slope(std::int32_t value);                                     // 0x004e8580 Math_AtanSlopeQ12
    std::uint32_t convert_vector(std::uint32_t matrix, std::uint32_t vector, std::uint32_t out);   // 0x004e85a0 Fixed_MatrixConvertQ12
    std::int32_t cos_q12(std::int32_t angle) { return x87::cos_q12(precision_, angle & 0xfff); }   // 0x004f3980 Math_CosQ12
    std::int32_t sin_q12(std::int32_t angle) { return x87::sin_q12(precision_, angle & 0xfff); }   // 0x004f39b0 Math_SinQ12
    std::int32_t cross_sign(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d);       // 0x004f5f50 Fixed_CrossSignQ12
    std::int32_t dot_q12(std::uint32_t a, std::uint32_t b);                          // 0x004f5f90 Fixed_DotProductQ12
    std::int32_t mul_q12(std::int32_t a, std::int32_t b);                            // 0x004f5fc0 Fixed_MulQ12
    std::uint32_t acos_ratio_to_angle(std::int32_t value);                           // 0x004ca0a0 Camera_AcosRatioToAngle12 (x87 acos, 4096 per turn)

    // ---- the PSX library stubs, the frame clock and the park file test ----
    std::uint32_t psx_return_argument(std::uint32_t value) { return value; }         // 0x004e86f0 ..: the stubs that answer their first argument
    std::uint32_t psx_return_second(std::uint32_t, std::uint32_t value) { return value; }   // 0x004e8a30
    std::uint32_t psx_return_zero() { return 0; }                                    // the stubs that answer 0
    void psx_nothing() {}                                                            // the stubs that are a bare `ret`
    void psx_init_rect_primitive(std::uint32_t primitive, std::uint32_t rect);       // 0x004e87f0
    std::uint32_t psx_load_clut(std::uint32_t clut, std::uint32_t x, std::uint32_t y, std::uint32_t width);   // 0x004e88a0 (256 wide) and 0x004e88f0 (16 wide)
    void psx_init_terminator(std::uint32_t primitive);                               // 0x004e8940
    std::uint32_t card_set_busy();                                                   // 0x004e8cd0
    std::uint32_t card_set_done();                                                   // 0x004e8cf0
    std::uint32_t card_get_state(std::uint32_t, std::uint32_t state_out, std::uint32_t flag_out);   // 0x004e8d10
    void update_frame_clock();                                                       // 0x004f5ff0 Timer_UpdateFrameClock
    void timer_callback(std::uint32_t state);                                        // 0x004dace0 SimulationTimeTimerCallback (stdcall, five; dwUser = TimerState)
    std::uint32_t change_extension(std::uint32_t path, std::uint32_t extension);     // 0x004f6080 Path_ChangeExtension
    bool park_file_exists(std::uint32_t name);                                       // 0x004f60d0 ParkFile_Exists

    // the precision control the x87 sequences run under (53 bits by default: the game logic; 24 bits inside the render window)
    void set_precision(x87::Precision precision) { precision_ = precision; }
    [[nodiscard]] x87::Precision precision() const { return precision_; }

    // the tail of a 32-byte copy of an 18-byte local (0x004e8200): bytes retail copies from uninitialised stack. The replay does not compare them.
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& scrap() const { return scrap_; }
    void forget_scrap() { scrap_.clear(); }

private:
    long double r(long double value) const { return x87::round_to(precision_, value); }
    x87::Precision precision_ = x87::Precision::k53;
    // MAC = (matrix * vector) [>> 12] [+ addend], then the clamp: the shared shape of the matrix ops (a RuntimeQ12Matrix, a RuntimeShortVector3 and, unless 0, a RuntimeIntVector3)
    void matrix_op(std::uint32_t matrix, std::uint32_t vector, std::uint32_t addend, bool shift, bool unsigned_clamp);
    // `sz_index` names the SZ register and `sx_index` the first of the two screen shorts of RuntimeGteRegisterFile
    void project_vertex(std::uint32_t vector, std::size_t sz_index, std::size_t sx_index, std::int32_t low);
    void rotate_columns(std::uint32_t matrix, const std::int32_t (&vectors)[3][3]);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> scrap_;
};

}  // namespace opentony::runtime::mathkit
