// Replays retail forced calls (Phase 4.14a) through the native math layer (math_gte.cpp: the GTE operations, math_fixed.cpp: the rotation, matrix, vector and trigonometry helpers, math_psx.cpp: the PSX library
// stubs, the frame clock and the park file test):
//
//   opentony_math_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of math_specs.py) holds the retail memory regions the ported code touches and, for every case, the
// state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "math_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::mathkit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;

namespace {

// the x87 precision control the fixture was captured under: the forced calls of the game logic run at 53 bits, the twin captures (math-calls-p24-*.txt) set the control word to 24 bits first
x87::Precision g_precision = x87::Precision::k53;

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    MathKit math;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), math(mem, svc) {
        math.set_precision(g_precision);
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);         // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using MathAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.math

const std::map<std::uint32_t, MathAdapter>& adapters() {
    static const std::map<std::uint32_t, MathAdapter> table = {
        // ---- the GTE ----
        {0x004e2070, {"Math_CrossResultAngleClamp", 0, false, V(G.clamp_ir_signed())}},
        {0x004e2130, {"mygte_lvtosvU", 0, false, V(G.clamp_ir_unsigned())}},
        {0x004e21e0, {"mygte_svtorgb", 0, false, V(G.colour_from_ir())}},
        {0x004e22b0, {"gte_ldlzc", 0, false, V(G.leading_zero_count(a[0]))}},
        {0x004e22f0, {"gte_avsz4", 0, false, V(G.average_z4())}},
        {0x004e23a0, {"gte_gpf12", 0, false, V(G.interpolate_gpf12())}},
        {0x004e23f0, {"gte_gpl0", 0, false, V(G.interpolate_gpl0())}},
        {0x004e2450, {"gte_gpl12", 0, false, V(G.interpolate_gpl12())}},
        {0x004e24b0, {"gte_lcv0", 0, false, V(G.lcv0())}},
        {0x004e2550, {"gte_lcv0_lm", 0, false, V(G.lcv0_lm())}},
        {0x004e25f0, {"gte_lcv1", 0, false, V(G.lcv1())}},
        {0x004e2690, {"gte_lcv1_lm", 0, false, V(G.lcv1_lm())}},
        {0x004e2730, {"gte_lcv2", 0, false, V(G.lcv2())}},
        {0x004e27d0, {"gte_lcir", 0, false, V(G.lcir())}},
        {0x004e2870, {"gte_lcirtr", 0, false, V(G.lcirtr())}},
        {0x004e2930, {"gte_llir_sf0", 0, false, V(G.llir_sf0())}},
        {0x004e29d0, {"gte_llirbk", 0, false, V(G.llirbk())}},
        {0x004e2a90, {"gte_llv0", 0, false, V(G.llv0())}},
        {0x004e2b30, {"gte_llv0bk", 0, false, V(G.llv0bk())}},
        {0x004e2bf0, {"gte_llv0bk_lm", 0, false, V(G.llv0bk_lm())}},
        {0x004e2cb0, {"gte_llv1bk", 0, false, V(G.llv1bk())}},
        {0x004e2d70, {"gte_llv1bk_lm", 0, false, V(G.llv1bk_lm())}},
        {0x004e2e30, {"gte_llv2bk", 0, false, V(G.llv2bk())}},
        {0x004e2ef0, {"gte_mvmva", 0, false, V(G.mvmva(a[0], a[2]))}},
        {0x004e2f20, {"gte_nclip", 0, false, V(G.nclip())}},
        {0x004e2f80, {"Fixed_CrossProductS16", 0, false, V(G.cross_sf0())}},
        {0x004e2ff0, {"Math_CrossFixed12", 0, false, V(G.cross_sf12())}},
        {0x004e3070, {"gte_rt", 0, false, V(G.rt())}},
        {0x004e3130, {"gte_rtir", 0, false, V(G.rtir())}},
        {0x004e31d0, {"gte_rtirtr", 0, false, V(G.rtirtr())}},
        {0x004e3290, {"gte_rtir_sf0", 0, false, V(G.rtir_sf0())}},
        {0x004e3330, {"gte_rtps", 0, false, V(G.rtps())}},
        {0x004e34e0, {"gte_rtpt", 0, false, V(G.rtpt())}},
        {0x004e39a0, {"Fixed_MatrixMultiplyQ12", 0, false, V(G.rtv0())}},
        {0x004e3a40, {"gte_rtv0tr", 0, false, V(G.rtv0tr())}},
        {0x004e3b00, {"gte_rtv0_sf0", 0, false, V(G.rtv0_sf0())}},
        {0x004e3ba0, {"gte_rtv1", 0, false, V(G.rtv1())}},
        {0x004e3c40, {"gte_rtv2", 0, false, V(G.rtv2())}},
        {0x004e3ce0, {"gte_sqr0", 0, false, V(G.sqr0())}},
        // ---- the game's math ----
        {0x004e7c60, {"Math_RotateMatrixX", 0xffffffff, false, A(G.rotate_matrix_x(S(a[0]), a[1]))}},
        {0x004e7de0, {"Math_RotateMatrixY", 0xffffffff, false, A(G.rotate_matrix_y(S(a[0]), a[1]))}},
        {0x004e7f60, {"Math_RotateMatrixZ", 0xffffffff, false, A(G.rotate_matrix_z(S(a[0]), a[1]))}},
        {0x004e80e0, {"Math_RotationMatrixFromAngles", 0xffffffff, false, A(G.rotation_from_angles(a[0], a[1]))}},
        {0x004e8140, {"Math_RotationMatrixFromAnglesZYX", 0xffffffff, false, A(G.rotation_from_angles_zyx(a[0], a[1]))}},
        {0x004e81a0, {"Math_RotationMatrixFromAnglesAgain", 0xffffffff, false, A(G.rotation_from_angles_again(a[0], a[1]))}},
        {0x004e83e0, {"Math_IntSqrt", 0xffffffff, false, A(G.isqrt(S(a[0])))}},
        {0x004e8410, {"Math_MatrixTranspose", 0x0, false, V(G.matrix_transpose(a[0], a[1]))}},
        {0x004e8460, {"Matrix_SetTranslation", 0x0, false, V(G.matrix_set_translation(a[0], a[1]))}},
        {0x004e8480, {"Vector_NormalizeQ12", 0xffffffff, false, A(G.normalize(a[0], a[1]))}},
        {0x004e8560, {"orig_ratan2", 0xffffffff, false, A(G.ratan2(S(a[0]), S(a[1])))}},
        {0x004e8580, {"Math_AtanSlopeQ12", 0xffffffff, false, A(G.atan_slope(S(a[0])))}},
        {0x004e85a0, {"Fixed_MatrixConvertQ12", 0xffffffff, false, A(G.convert_vector(a[0], a[1], a[2]))}},
        {0x004f3980, {"Math_CosQ12", 0xffffffff, false, A(G.cos_q12(S(a[0])))}},
        {0x004f39b0, {"Math_SinQ12", 0xffffffff, false, A(G.sin_q12(S(a[0])))}},
        {0x004f5f50, {"Fixed_CrossSignQ12", 0xffffffff, false, A(G.cross_sign(S(a[0]), S(a[1]), S(a[2]), S(a[3])))}},
        {0x004f5f90, {"Fixed_DotProductQ12", 0xffffffff, false, A(G.dot_q12(a[0], a[1]))}},
        {0x004f5fc0, {"Fixed_MulQ12", 0xffffffff, false, A(G.mul_q12(S(a[0]), S(a[1])))}},
        {0x004f5ff0, {"Timer_UpdateFrameClock", 0x0, false, V(G.update_frame_clock())}},
        {0x004f6080, {"Path_ChangeExtension", 0x0, false, V(G.change_extension(a[0], a[1]))}},
        {0x004f60d0, {"ParkFile_Exists", 0xff, false, A(G.park_file_exists(a[0]))}},
        {0x004e87f0, {"PsxLib_InitRectPrimitive", 0x0, false, V(G.psx_init_rect_primitive(a[0], a[1]))}},
        {0x004e88a0, {"PsxLib_LoadClut256", 0xffffffff, false, A(G.psx_load_clut(a[0], a[1], a[2], 256))}},
        {0x004e88f0, {"PsxLib_LoadClut16", 0xffffffff, false, A(G.psx_load_clut(a[0], a[1], a[2], 16))}},
        {0x004e8940, {"PsxLib_InitTerminator", 0x0, false, V(G.psx_init_terminator(a[0]))}},
        {0x004e8cd0, {"PsxCard_SetBusy", 0xffffffff, false, A(G.card_set_busy())}},
        {0x004e8cf0, {"PsxCard_SetDone", 0xffffffff, false, A(G.card_set_done())}},
        {0x004e8d10, {"PsxCard_GetState", 0xffffffff, false, A(G.card_get_state(a[0], a[1], a[2]))}},
        {0x004e8200, {"Math_MatrixMultiplyQ12", 0, false, V(G.matrix_multiply(a[0], a[1], a[2])), [](Ctx& c) -> Unspecified { return c.math.scrap(); }}},
        {0x004e8280, {"Math_RotationMatrixChain", 0xFFFFFFFF, false, A(G.rotation_chain(a[0], a[1])), [](Ctx& c) -> Unspecified { return c.math.scrap(); }}},
        // ---- the PSX library stubs and the empty functions ----
        {0x004e86f0, {"PsxStub_ReturnArg_004e86f0", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8700, {"PsxStub_ReturnArg_004e8700", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8710, {"PsxStub_ReturnArg_004e8710", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8720, {"PsxStub_ReturnArg_004e8720", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8730, {"PsxStub_ReturnZero_004e8730", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8740, {"PsxStub_ReturnZero_004e8740", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8750, {"PsxStub_ReturnZero_004e8750", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8760, {"PsxStub_ReturnZero_004e8760", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8770, {"PsxStub_ReturnZero_004e8770", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8780, {"PsxStub_ReturnArg_004e8780", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8790, {"PsxStub_ReturnZero_004e8790", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e87a0, {"PsxStub_ReturnZero_004e87a0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e87b0, {"PsxStub_ReturnZero_004e87b0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e87c0, {"PsxStub_ReturnZero_004e87c0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e87d0, {"PsxStub_Nothing_004e87d0", 0, false, V(G.psx_nothing())}},
        {0x004e87e0, {"PsxStub_Nothing_004e87e0", 0, false, V(G.psx_nothing())}},
        {0x004e8840, {"PsxStub_ReturnZero_004e8840", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8850, {"PsxStub_Nothing_004e8850", 0, false, V(G.psx_nothing())}},
        {0x004e8860, {"PsxStub_ReturnZero_004e8860", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8870, {"PsxStub_ReturnZero_004e8870", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8880, {"PsxStub_ReturnZero_004e8880", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8890, {"PsxStub_Nothing_004e8890", 0, false, V(G.psx_nothing())}},
        {0x004e8960, {"PsxStub_ReturnZero_004e8960", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8970, {"PsxStub_ReturnZero_004e8970", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8980, {"PsxStub_ReturnZero_004e8980", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8990, {"PsxStub_ReturnZero_004e8990", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e89a0, {"PsxStub_Nothing_004e89a0", 0, false, V(G.psx_nothing())}},
        {0x004e89b0, {"PsxStub_ReturnZero_004e89b0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e89c0, {"PsxStub_ReturnZero_004e89c0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e89d0, {"PsxStub_ReturnZero_004e89d0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e89e0, {"PsxStub_ReturnZero_004e89e0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e89f0, {"PsxStub_Nothing_004e89f0", 0, false, V(G.psx_nothing())}},
        {0x004e8a00, {"PsxStub_ReturnZero_004e8a00", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8a10, {"PsxStub_Nothing_004e8a10", 0, false, V(G.psx_nothing())}},
        {0x004e8a20, {"PsxStub_ReturnZero_004e8a20", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8a30, {"PsxStub_ReturnSecondArg_004e8a30", 0xffffffff, false, A(G.psx_return_second(a[0], a[1]))}},
        {0x004e8a40, {"PsxStub_ReturnArg_004e8a40", 0xffffffff, false, A(G.psx_return_argument(a[0]))}},
        {0x004e8a50, {"PsxStub_ReturnZero_004e8a50", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8a60, {"PsxStub_ReturnZero_004e8a60", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8a70, {"PsxStub_Nothing_004e8a70", 0, false, V(G.psx_nothing())}},
        {0x004e8a80, {"PsxStub_ReturnZero_004e8a80", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8a90, {"PsxStub_ReturnZero_004e8a90", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8aa0, {"PsxStub_ReturnZero_004e8aa0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8ab0, {"PsxStub_Nothing_004e8ab0", 0, false, V(G.psx_nothing())}},
        {0x004e8ac0, {"PsxStub_ReturnZero_004e8ac0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8ad0, {"PsxStub_Nothing_004e8ad0", 0, false, V(G.psx_nothing())}},
        {0x004e8ae0, {"PsxStub_Nothing_004e8ae0", 0, false, V(G.psx_nothing())}},
        {0x004e8af0, {"PsxStub_ReturnZero_004e8af0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b00, {"PsxStub_ReturnZero_004e8b00", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b10, {"PsxStub_Nothing_004e8b10", 0, false, V(G.psx_nothing())}},
        {0x004e8b20, {"PsxStub_ReturnZero_004e8b20", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b30, {"PsxStub_Nothing_004e8b30", 0, false, V(G.psx_nothing())}},
        {0x004e8b40, {"PsxStub_ReturnZero_004e8b40", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b50, {"PsxStub_ReturnZero_004e8b50", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b60, {"PsxStub_ReturnZero_004e8b60", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8b70, {"PsxStub_Nothing_004e8b70", 0, false, V(G.psx_nothing())}},
        {0x004e8b80, {"PsxStub_Nothing_004e8b80", 0, false, V(G.psx_nothing())}},
        {0x004e8b90, {"PsxStub_ReturnZero_004e8b90", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8ba0, {"PsxStub_ReturnZero_004e8ba0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8bb0, {"PsxStub_Nothing_004e8bb0", 0, false, V(G.psx_nothing())}},
        {0x004e8bc0, {"PsxStub_ReturnZero_004e8bc0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8bd0, {"PsxStub_ReturnZero_004e8bd0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8be0, {"PsxStub_ReturnZero_004e8be0", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8bf0, {"PsxStub_Nothing_004e8bf0", 0, false, V(G.psx_nothing())}},
        {0x004e8c00, {"PsxStub_Nothing_004e8c00", 0, false, V(G.psx_nothing())}},
        {0x004e8c10, {"PsxStub_ReturnZero_004e8c10", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8c20, {"PsxStub_ReturnZero_004e8c20", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8c30, {"PsxStub_ReturnZero_004e8c30", 0xffffffff, false, A(G.psx_return_zero())}},
        {0x004e8c40, {"PsxStub_Nothing_004e8c40", 0, false, V(G.psx_nothing())}},
        {0x004e8c50, {"PsxStub_Nothing_004e8c50", 0, false, V(G.psx_nothing())}},
        {0x004e8c60, {"PsxStub_Nothing_004e8c60", 0, false, V(G.psx_nothing())}},
        {0x004e8c70, {"PsxStub_Nothing_004e8c70", 0, false, V(G.psx_nothing())}},
        {0x004e8c80, {"PsxStub_ReturnZero16_004e8c80", 0xffff, false, A(G.psx_return_zero())}},
        {0x004e8c90, {"PsxStub_Nothing_004e8c90", 0, false, V(G.psx_nothing())}},
        {0x004e8ca0, {"PsxStub_Nothing_004e8ca0", 0, false, V(G.psx_nothing())}},
        {0x004e8cb0, {"PsxStub_Nothing_004e8cb0", 0, false, V(G.psx_nothing())}},
        {0x004e8cc0, {"PsxStub_Nothing_004e8cc0", 0, false, V(G.psx_nothing())}},
        {0x004f39e0, {"Utils_Nothing_004f39e0", 0, false, V(G.psx_nothing())}},
        {0x004f5fe0, {"Utils_Nothing_004f5fe0", 0, false, V(G.psx_nothing())}},
        {0x004f6070, {"Utils_Nothing_004f6070", 0, false, V(G.psx_nothing())}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    replay.on_file = [](const std::string& path) {
        g_precision = path.find("-p24-") != std::string::npos ? x87::Precision::k24 : x87::Precision::k53;
    };
    return replay.run_files(argc, argv, "math layer retail forced calls");
}
