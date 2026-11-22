// Replays retail forced calls (Phase 4.15d) through the native effects layer (bit_*.cpp: the transient bits, their lists and classes, the display of the bits, the dust clouds, the shatter and the
// effect object it spawns, the dynamic logos):
//
//   opentony_bit_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of bit_specs*.py) holds the retail memory regions the ported code touches and, for every case,
// the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "bit_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>
#include <string>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::bitkit;
using opentony::runtime::pcplatform::kScratchBase;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    BitKit bits;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), bits(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, 0x3000);                    // (the stack locals of the retail functions; the effects keep more of them than most)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using BitAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.bits

const std::map<std::uint32_t, BitAdapter>& adapters() {
    static const std::map<std::uint32_t, BitAdapter> table = {
        // ---- the lists and the base class (bit_core.cpp)
        {0x00403680, {"Bit_Init", 0, false, V(G.bit_init())}},
        {0x00403730, {"DeleteBitList", 0, false, V(G.delete_bit_list(a[0]))}},
        {0x00403760, {"Bit_DeleteAll", 0, false, V(G.bit_delete_all())}},
        {0x00403bd0, {"Bit_RemoveDeadBits", 0, false, V(G.remove_dead_bits())}},
        {0x00403c70, {"RemoveDeadBits", 0, false, V(G.remove_dead_list(a[0]))}},
        {0x00403ca0, {"Bit_ReduceRGB", 0, false, V(G.reduce_rgb(a[0], S(a[1])))}},
        {0x00403d30, {"CBit_New", 0xffffffff, false, A(G.bit_new(a[0]))}},
        {0x00403dc0, {"CBit_Delete", 0, false, V(G.bit_delete(a[0]))}},
        {0x00403df0, {"CBit_AttachTo", 0, false, V(G.attach_to(self, a[0]))}},
        {0x00403e10, {"CBit_DeleteFrom", 0, false, V(G.delete_from(self, a[0]))}},
        {0x00403e40, {"CBit_Die", 0, false, V(G.die(self))}},
        {0x00403ea0, {"CBit_SetPos", 0, false, V(G.set_pos(self, a[0]))}},
        {0x00403ec0, {"CBit_SetRandomVelocity", 0, false, V(G.set_random_velocity(self, S(a[0])))}},
        {0x00403f00, {"CBit_Construct", 0, false, V(G.bit_construct(self))}},
        {0x00403f50, {"CBit_ScalarDelete", 0xffffffff, false, A(G.bit_scalar_delete(self, a[0]))}},
        {0x00403f70, {"CBit_Destruct", 0, false, V(G.bit_destruct(self))}},
        {0x00403f90, {"CBit_Move", 0, false, V(G.bit_move(self))}},
        // ---- the simple classes
        {0x00403fc0, {"CSpecialDisplay_Construct", 0xffffffff, false, A(G.special_construct(self))}},
        {0x00403fe0, {"CSpecialDisplay_ScalarDelete", 0xffffffff, false, A(G.special_scalar_delete(self, a[0]))}},
        {0x00404000, {"CSpecialDisplay_Destruct", 0, false, V(G.special_destruct(self))}},
        {0x00404020, {"CNonRenderedBit_Construct", 0xffffffff, false, A(G.nonrendered_construct(self))}},
        {0x00404040, {"CNonRenderedBit_ScalarDelete", 0xffffffff, false, A(G.nonrendered_scalar_delete(self, a[0]))}},
        {0x00404060, {"CNonRenderedBit_Destruct", 0, false, V(G.nonrendered_destruct(self))}},
        {0x00404080, {"CFT4Bit_Construct", 0xffffffff, false, A(G.ft4_construct(self))}},
        {0x004040b0, {"CFT4Bit_ScalarDelete", 0xffffffff, false, A(G.ft4_scalar_delete(self, a[0]))}},
        {0x004040d0, {"CFT4Bit_Destruct", 0, false, V(G.ft4_destruct(self))}},
        {0x00404150, {"CFT4Bit_SetScale", 0, false, V(G.set_scale(self, a[0]))}},
        {0x00404170, {"CFT4Bit_SetSemiTransparent", 0, false, V(G.set_semi_transparent(self))}},
        {0x004041c0, {"CFT4Bit_SetTint", 0, false, V(G.set_tint(self, a[0], a[1], a[2]))}},
        {0x00404210, {"CFT4Bit_SetTransDecay", 0, false, V(G.set_trans_decay(self, a[0]))}},
        {0x00404220, {"CFT4Bit_SetAnim", 0, false, V(G.set_anim_by_name(self, a[0]))}},
        {0x00404250, {"CFT4Bit_SetAnimIndex", 0, false, V(G.set_anim(self, S(a[0])))}},
        {0x004042d0, {"CFT4Bit_SetFrame", 0, false, V(G.set_frame(self, S(a[0])))}},
        {0x00404390, {"CFT4Bit_IncFrame", 0, false, V(G.inc_frame(self))}},
        {0x004043d0, {"CFT4Bit_IncFrameWithWrap", 0, false, V(G.inc_frame_with_wrap(self))}},
        {0x00406990, {"CRibbonBit_Move", 0, false, V(G.inc_frame_with_wrap(self))}},
        {0x004068a0, {"CFrag_Move", 0, false, V(G.frag_move(self))}},
        {0x00404460, {"CFT4Bit_ChooseRandomFrame", 0, false, V(G.choose_random_frame(self))}},
        {0x00404490, {"CFT4Bit_Shrink", 0xffffffff, false, A(G.shrink(self, a[0]))}},
        {0x004044f0, {"CFT4Bit_Fade", 0xffffffff, false, A(G.fade(self, a[0]))}},
        {0x00404590, {"CQuadBit_Construct", 0xffffffff, false, A(G.quad_construct(self))}},
        {0x004045e0, {"CQuadBit_ScalarDelete", 0xffffffff, false, A(G.quad_scalar_delete(self, a[0]))}},
        {0x00404600, {"CQuadBit_Destruct", 0, false, V(G.quad_destruct(self))}},
        {0x00404620, {"CQuadBit_SetTextureIndex", 0, false, V(G.quad_set_texture_index(self, S(a[0]), S(a[1])))}},
        {0x004046f0, {"CQuadBit_SetTexture", 0, false, V(G.quad_set_texture(self, a[0], S(a[1])))}},
        {0x00404810, {"CQuadBit_FlagOn", 0, false, V(G.quad_flag_on(self, a[0]))}},
        {0x00404840, {"CQuadBit_SetSemiTransparent", 0, false, V(G.quad_set_semi_transparent(self))}},
        {0x00404860, {"CQuadBit_SetTransparency", 0, false, V(G.quad_set_transparency(self, a[0]))}},
        {0x004048c0, {"CQuadBit_OrientUsing", 0, false, V(G.quad_orient(self, a[0], a[1], a[2], a[3]))}},
        {0x00404af0, {"CGenPoly_Construct4", 0xffffffff, false, A(G.genpoly4_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x00404bd0, {"CGenPoly_ScalarDelete", 0xffffffff, false, A(G.genpoly_scalar_delete(self, a[0]))}},
        {0x00404bf0, {"CGenPoly_Construct3", 0xffffffff, false, A(G.genpoly3_construct(self, a[0], a[1], a[2]))}},
        {0x00404cb0, {"CGenPoly_Destruct", 0, false, V(G.genpoly_destruct(self))}},
        {0x004051c0, {"CChunkBit_Construct", 0xffffffff, false, A(G.chunk_construct(self, a[0], a[1], a[2]))}},
        {0x00405390, {"CChunkBit_ScalarDelete", 0xffffffff, false, A(G.chunk_scalar_delete(self, a[0]))}},
        {0x004053b0, {"CChunkBit_Destruct", 0, false, V(G.chunk_destruct(self))}},
        {0x004053d0, {"CChunkBit_SetRGB", 0, false, V(G.chunk_set_rgb(self, a[0], a[1], a[2]))}},
        {0x004054b0, {"CChunkBit_SetTexture", 0, false, V(G.chunk_set_texture(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]))}},
        {0x00405520, {"CChunkBit_CalculateWorldCoords", 0, false, V(G.chunk_world_coords(self))}},
        {0x004056c0, {"CGlow_Construct", 0xffffffff, false, A(G.glow_construct(self, S(a[0]), S(a[1])))}},
        {0x004057f0, {"CGlow_ScalarDelete", 0xffffffff, false, A(G.glow_scalar_delete(self, a[0]))}},
        {0x00405810, {"CGlow_ConstructSimple", 0xffffffff, false, A(G.glow_construct_simple(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]))}},
        {0x00405990, {"CGlow_Destruct", 0, false, V(G.glow_destruct(self))}},
        {0x00405a70, {"CGlow_SetRadius", 0, false, V(G.glow_set_radius(self, a[0]))}},
        {0x00405aa0, {"CGlow_SetFringeWidth", 0, false, V(G.glow_set_fringe_width(self, a[0], a[1]))}},
        {0x00405b20, {"CGlow_SetCentreRGB", 0, false, V(G.glow_set_centre_rgb(self, a[0], a[1], a[2]))}},
        {0x00405b50, {"CGlow_SetRGB", 0, false, V(G.glow_set_rgb(self, a[0], a[1], a[2]))}},
        {0x00405b90, {"CGlow_SetFringeRGB", 0, false, V(G.glow_set_fringe_rgb(self, a[0], a[1], a[2], a[3]))}},
        {0x00405c40, {"CGlow_SetRadialRGB", 0, false, V(G.glow_set_radial_rgb(self, a[0], a[1], a[2], a[3]))}},
        {0x00405d20, {"CGlow_Reduce", 0, false, V(G.glow_reduce(self, S(a[0])))}},
        {0x00405d60, {"CWobblyGlow_Construct", 0xffffffff, false, A(G.wobbly_construct(self, a[0], S(a[1]), a[2], S(a[3]), a[4], a[5], a[6], a[7], a[8], a[9]))}},
        {0x00405e30, {"CWobblyGlow_ScalarDelete", 0xffffffff, false, A(G.wobbly_scalar_delete(self, a[0]))}},
        {0x00405e50, {"CWobblyGlow_Destruct", 0, false, V(G.wobbly_destruct(self))}},
        {0x00405e60, {"CWobblyGlow_Move", 0, false, V(G.wobbly_move(self))}},
        {0x00405ec0, {"CFlatBit_Construct", 0xffffffff, false, A(G.flat_construct(self))}},
        {0x00405ef0, {"CFlatBit_ScalarDelete", 0xffffffff, false, A(G.flat_scalar_delete(self, a[0]))}},
        {0x00405f10, {"CFlatBit_Destruct", 0, false, V(G.flat_destruct(self))}},
        {0x00405f30, {"CLinked2EndedBit_Construct", 0xffffffff, false, A(G.linked_construct(self))}},
        {0x00405f70, {"CLinked2EndedBit_ScalarDelete", 0xffffffff, false, A(G.linked_scalar_delete(self, a[0]))}},
        {0x00405f90, {"CLinked2EndedBit_Destruct", 0, false, V(G.linked_destruct(self))}},
        {0x00405fb0, {"CPixel_Construct", 0xffffffff, false, A(G.pixel_construct(self))}},
        {0x00405fd0, {"CPixel_ScalarDelete", 0xffffffff, false, A(G.pixel_scalar_delete(self, a[0]))}},
        {0x00405ff0, {"CPixel_Destruct", 0, false, V(G.pixel_destruct(self))}},
        // ---- the spark settings and the animated classes (bit_spark.cpp)
        {0x00406070, {"Bit_SetSparkTrajectory", 0, false, V(G.set_spark_trajectory(a[0]))}},
        {0x00406090, {"Bit_SetSparkTrajectoryCone", 0, false, V(G.set_spark_cone(a[0]))}},
        {0x004060b0, {"Bit_CalculateSparkVelocity", 0, false, V(G.calculate_spark_velocity(a[0], a[1]))}},
        {0x00406160, {"Bit_SetSparkSize", 0, false, V(G.set_spark_size(a[0]))}},
        {0x004061c0, {"Bit_SetSparkRGB", 0, false, V(G.set_spark_rgb(a[0], a[1], a[2]))}},
        {0x004061e0, {"Bit_SetSparkFadeRGB", 0, false, V(G.set_spark_fade_rgb(a[0], a[1], a[2]))}},
        {0x00406200, {"CSpark_Construct", 0xffffffff, false, A(G.spark_construct(self, a[0], a[1], a[2], a[3]))}},
        {0x00406300, {"CSpark_ScalarDelete", 0xffffffff, false, A(G.spark_scalar_delete(self, a[0]))}},
        {0x00406320, {"CSpark_Destruct", 0, false, V(G.spark_destruct(self))}},
        {0x00406330, {"CSpark_Move", 0, false, V(G.spark_move(self))}},
        {0x004063e0, {"CSimpleAnim_Construct", 0xffffffff, false, A(G.simple_anim_construct(self, a[0], a[1], a[2], a[3], a[4], S(a[5]), false))}},
        {0x004064b0, {"CSimpleAnim_ConstructIndexed", 0xffffffff, false, A(G.simple_anim_construct(self, a[0], a[1], a[2], a[3], a[4], S(a[5]), true))}},
        {0x00406490, {"CSimpleAnim_ScalarDelete", 0xffffffff, false, A(G.simple_anim_scalar_delete(self, a[0]))}},
        {0x00406560, {"CSimpleAnim_Destruct", 0, false, V(G.simple_anim_destruct(self))}},
        {0x00406570, {"CSimpleAnim_Move", 0, false, V(G.simple_anim_move(self))}},
        {0x004065c0, {"CMotionBlur_Construct", 0xffffffff, false, A(G.motion_blur_construct(self, a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00406660, {"CMotionBlur_ScalarDelete", 0xffffffff, false, A(G.motion_blur_scalar_delete(self, a[0]))}},
        {0x00406680, {"CMotionBlur_Destruct", 0, false, V(G.motion_blur_destruct(self))}},
        {0x00406690, {"CMotionBlur_Move", 0, false, V(G.motion_blur_move(self, false))}},
        {0x00406710, {"CMotionBlurImmortal_ScalarDelete", 0xffffffff, false, A(G.immortal_scalar_delete(self, a[0]))}},
        {0x00406730, {"CMotionBlurImmortal_Destruct", 0, false, V(G.immortal_destruct(self))}},
        {0x00406740, {"CMotionBlurImmortal_Move", 0, false, V(G.motion_blur_move(self, true))}},
        {0x00406780, {"CFrag_Construct", 0xffffffff, false, A(G.frag_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6], S(a[7]), a[8], a[9]))}},
        {0x00406870, {"CFrag_ScalarDelete", 0xffffffff, false, A(G.frag_scalar_delete(self, a[0]))}},
        {0x00406890, {"CFrag_Destruct", 0, false, V(G.frag_destruct(self))}},
        {0x00406940, {"CRibbonBit_Construct", 0xffffffff, false, A(G.ribbon_bit_construct(self))}},
        {0x00406960, {"CRibbonBit_ScalarDelete", 0xffffffff, false, A(G.ribbon_bit_scalar_delete(self, a[0]))}},
        {0x00406980, {"CRibbonBit_Destruct", 0, false, V(G.ribbon_bit_destruct(self))}},
        {0x004069a0, {"CRibbon_Construct", 0xffffffff, false, A(G.ribbon_construct(self, a[0], S(a[1]), S(a[2]), a[3], a[4], a[5], a[6]))}},
        {0x00406b10, {"CRibbon_ScalarDelete", 0xffffffff, false, A(G.ribbon_scalar_delete(self, a[0]))}},
        {0x00406b30, {"CRibbon_Destruct", 0, false, V(G.ribbon_destruct(self))}},
        {0x00406bf0, {"CRibbon_SetTint", 0, false, V(G.ribbon_set_tint(self, a[0], a[1], a[2]))}},
        {0x00406c30, {"CRibbon_SetScale", 0, false, V(G.ribbon_set_scale(self, a[0]))}},
        {0x00406c60, {"CRibbon_SetPos", 0, false, V(G.ribbon_set_pos(self, a[0]))}},
        {0x00406d00, {"CSmokeTrail_Construct", 0xffffffff, false, A(G.smoke_trail_construct(self, a[0], S(a[1]), S(a[2]), S(a[3]), S(a[4])))}},
        {0x00406db0, {"CSmokeTrail_ScalarDelete", 0xffffffff, false, A(G.smoke_trail_scalar_delete(self, a[0]))}},
        {0x00406dd0, {"CSmokeTrail_Destruct", 0, false, V(G.smoke_trail_destruct(self))}},
        {0x00406de0, {"CSmokeTrail_Move", 0, false, V(G.smoke_trail_move(self))}},
        {0x00406e30, {"CGlassBit_Construct", 0xffffffff, false, A(G.glass_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], S(a[6]), S(a[7]), S(a[8])))}},
        {0x00407010, {"CGlassBit_ScalarDelete", 0xffffffff, false, A(G.glass_scalar_delete(self, a[0]))}},
        {0x00407030, {"CGlassBit_Destruct", 0, false, V(G.glass_destruct(self))}},
        {0x00407050, {"CGlassBit_Move", 0, false, V(G.glass_move(self))}},
        // ---- the sweeps, the polylines, the smoke generator, the line bits, the blood manager (bit_misc.cpp)
        {0x00407250, {"MoveList", 0, false, V(G.move_list(a[0]))}},
        {0x00407270, {"Bit_Move", 0, false, V(G.bit_move_all())}},
        {0x0040a740, {"CPolyLine_Construct", 0xffffffff, false, A(G.polyline_construct(self, S(a[0])))}},
        {0x0040a860, {"CPolyLine_ScalarDelete", 0xffffffff, false, A(G.polyline_scalar_delete(self, a[0]))}},
        {0x0040a880, {"CPolyLine_Destruct", 0, false, V(G.polyline_destruct(self))}},
        {0x0040a900, {"CPolyLine_SetSemiTransparent", 0, false, V(G.polyline_set_semi_transparent(self))}},
        {0x0040aa20, {"CGPolyLine_Construct", 0xffffffff, false, A(G.gpolyline_construct(self, S(a[0])))}},
        {0x0040ab50, {"CGPolyLine_ScalarDelete", 0xffffffff, false, A(G.gpolyline_scalar_delete(self, a[0]))}},
        {0x0040ab70, {"CGPolyLine_Destruct", 0, false, V(G.gpolyline_destruct(self))}},
        {0x0040ad30, {"CSmokeGenerator_Construct", 0xffffffff, false, A(G.smoke_generator_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]))}},
        {0x0040ae20, {"CSmokeGenerator_ScalarDelete", 0xffffffff, false, A(G.smoke_generator_scalar_delete(self, a[0]))}},
        {0x0040ae40, {"CSmokeGenerator_Destruct", 0, false, V(G.smoke_generator_destruct(self))}},
        {0x0040ae50, {"CSmokeGenerator_Move", 0, false, V(G.smoke_generator_move(self))}},
        {0x0040af70, {"CGLine_Construct", 0xffffffff, false, A(G.gline_construct(self))}},
        {0x0040afe0, {"CGLine_ScalarDelete", 0xffffffff, false, A(G.gline_scalar_delete(self, a[0]))}},
        {0x0040b000, {"CGLine_Destruct", 0, false, V(G.gline_destruct(self))}},
        {0x0040b060, {"CGLineBit_Construct", 0xffffffff, false, A(G.gline_bit_construct(self, a[0], a[1], S(a[2]), a[3], a[4]))}},
        {0x0040b1a0, {"CGLineBit_ScalarDelete", 0xffffffff, false, A(G.gline_bit_scalar_delete(self, a[0]))}},
        {0x0040b1c0, {"CGLineBit_Destruct", 0, false, V(G.gline_bit_destruct(self))}},
        {0x0040b1d0, {"CGLineBit_Move", 0, false, V(G.gline_bit_move(self))}},
        {0x0040b2e0, {"Blood_ScalarDelete", 0xffffffff, false, A(G.blood_scalar_delete(self, a[0]))}},
        {0x0040b300, {"Blood_Destruct", 0, false, V(G.blood_destruct(self))}},
        {0x0040b350, {"Blood_Reset", 0, false, V(G.blood_reset(self))}},
        {0x0040b3e0, {"CClass_Destruct", 0, false, V(G.class_destruct(self))}},
        {0x0040b3f0, {"CClass_ScalarDelete", 0xffffffff, false, A(G.class_scalar_delete(self, a[0]))}},
        {0x0040b410, {"Vector_Jitter", 0, false, V(G.jitter_vector(a[0], S(a[1])))}},
        {0x0040b4e0, {"Camera_RandSeed", 0, false, V(G.camera_seed(a[0]))}},
        {0x0040b500, {"Camera_Rand", 0xffffffff, false, A(G.camera_rand(a[0]))}},
        {0x0040b570, {"Camera_QuaternionIdentity", 0, false, V(G.camera_quaternion_identity())}},
        // ---- the dust and the dynamic logos (bit_dust.cpp)
        {0x00431260, {"Dust_Reset", 0, false, V(G.dust_reset(self, a[0], a[1]))}},
        {0x00431320, {"Dust_Update", 0, false, V(G.dust_update(self, a[0]))}},
        {0x00431650, {"Dust_InsertDust", 0, false, V(G.dust_insert(self, a[0]))}},
        {0x004317c0, {"Dust_GetTerrainParameters", 0, false, V(G.dust_terrain_parameters(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004318a0, {"Dust_GetLandTerrainParameters", 0, false, V(G.dust_land_parameters(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00431b80, {"Dust_GetBailTerrainParameters", 0, false, V(G.dust_bail_parameters(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00431e80, {"Dust_GetBrakeTerrainParameters", 0, false, V(G.dust_brake_parameters(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00432180, {"Dust_ApplyRandomOffset", 0, false, V(G.random_offset(a[0], S(a[1]), a[2]))}},
        {0x004321d0, {"DynamicLogo_NameMatches", 0xff, false, A(G.logo_name_matches(a[0], S(a[1])))}},
        {0x00432330, {"DynamicLogo_LoadAll", 0, false, V(G.logo_load_all())}},
        {0x00432250, {"DynamicLogo_FindNext", 0xffffffff, false, A(G.logo_next(a[0], a[1], S(a[2])))}},
        // ---- the shatter and the emitter (bit_shatter.cpp)
        {0x004add10, {"Emitter_Construct", 0xffffffff, false, A(G.emitter_construct(self, a[0], a[1], a[2], a[3], a[4]))}},
        {0x004ade60, {"Emitter_ScalarDelete", 0xffffffff, false, A(G.emitter_scalar_delete(self, a[0]))}},
        {0x004ade80, {"Emitter_Destruct", 0, false, V(G.emitter_destruct(self))}},
        {0x004adee0, {"Emitter_SetVelocity", 0, false, V(G.emitter_set_velocity(self, a[0]))}},
        {0x004ae020, {"Emitter_Move", 0, false, V(G.emitter_move(self))}},
        {0x004ae090, {"Shatter_Face", 0xffffffff, false, A(G.shatter_face(a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x004ae730, {"Shatter_Triangle", 0, false, V(G.shatter_triangle(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]))}},
        {0x004aeef0, {"Shatter_Corner", 0, false, V(G.shatter_corner(a[0], a[1], a[2], a[3]))}},
        {0x004aef70, {"Shatter_Colour", 0, false, V(G.shatter_colour(a[0], a[1], a[2], a[3]))}},
        {0x004af0b0, {"Shatter_Item", 0, false, V(G.shatter_item(a[0], a[1], a[2]))}},
        {0x004af190, {"Shatter_ForgetBurstThunk", 0, false, V(G.shatter_forget_burst())}},
        {0x004af1a0, {"Shatter_ForgetBurst", 0, false, V(G.shatter_forget_burst())}},
        {0x004af1c0, {"Shatter_Glass", 0, false, V(G.shatter_glass(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]))}},
        {0x004af630, {"Shatter_BurstSound", 0, false, V(G.shatter_burst_sound())}},
        // ---- Bit_Display (bit_display.cpp)
        // (the display runs under the render window's 24-bit x87 control word: the captures went through a trampoline that loads it)
        {0x00407310, {"Bit_Display", 0, false, V((G.math().set_precision(mathkit::x87::Precision::k24), G.core().math().set_precision(mathkit::x87::Precision::k24), G.bit_display()))}},
        // ---- the cheat codes (bit_cheat.cpp)
        {0x004af660, {"Cheat_Hash", 0, false, V(G.cheat_hash(a[0], a[1]))}},
        {0x004af6b0, {"Cheat_Code", 0xffffffff, false, A(G.cheat_code(a[0]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "effects layer retail forced calls");
}
