#pragma once

#include "collision_kit.hpp"
#include "core_kit.hpp"
#include "m3d_kit.hpp"
#include "math_kit.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace opentony::runtime::bitkit {

// Phase 4.15d: the game's effects: the transient "bits" (`bit.cpp`: sparks, glows, ribbons, smoke, glass, the motion blur and the polygons the display lists draw), the polyline and smoke classes of
// `bit2.cpp`, the blood manager, the skater's dust clouds (`dust.cpp`), the shatter of an object into shards (`shatter.cpp`), the effect object the shards spawn, and the dynamic logos. Like the rest of
// Phase 4 the functions work on the retail memory at the retail addresses (`self` is the bit); everything outside them (the allocators, the random numbers, the texture lookups, the sprite sort) is a
// service reported by address (`engine_<address>`).

// the record every bit starts with (the base class has vtable 0x005183d8; a list links the bits by +4 / +8)
namespace bit {

constexpr std::uint32_t kVtable = 0x00;
constexpr std::uint32_t kPrevious = 0x04;
constexpr std::uint32_t kNext = 0x08;
constexpr std::uint32_t kAge = 0x0c;                 // short: frames lived
constexpr std::uint32_t kLifetime = 0x0e;            // short: frames to live
constexpr std::uint32_t kPosition = 0x10;            // three dwords (Q12)
constexpr std::uint32_t kVelocity = 0x1c;            // three dwords
constexpr std::uint32_t kAcceleration = 0x28;        // three dwords
constexpr std::uint32_t kFriction = 0x34;            // three bytes: the shifts a velocity decays by
constexpr std::uint32_t kDead = 0x37;                // byte: the bit is removed by the next sweep
constexpr std::uint32_t kProtected = 0x3a;           // byte: Bit_DeleteAll keeps it (the owner deletes it)
constexpr std::uint32_t kType = 0x3b;                // byte

// the textured-quad bit (CFT4Bit)
constexpr std::uint32_t kTransDecay = 0x3c;          // short
constexpr std::uint32_t kScaleDecay = 0x3e;          // short
constexpr std::uint32_t kColour = 0x40;              // dword 0xccbbggrr
constexpr std::uint32_t kAnim = 0x44;                // dword: the animation (an array of frames, the frame count a byte before)
constexpr std::uint32_t kFrameEntry = 0x48;          // dword: the current frame's entry
constexpr std::uint32_t kFlags = 0x4c;               // byte
constexpr std::uint32_t kFrameCount = 0x4d;          // byte
constexpr std::uint32_t kFrame = 0x4e;               // byte (the whole part of the animation clock)
constexpr std::uint32_t kFrameFraction = 0x4f;       // byte
constexpr std::uint32_t kAnimSpeed = 0x50;           // short
constexpr std::uint32_t kScale = 0x52;               // short

}  // namespace bit

// the heads of the bit lists
namespace lists {

constexpr std::uint32_t kNonRendered = 0x0055f704;
constexpr std::uint32_t kFlat = 0x0055f6f0;
constexpr std::uint32_t kLinked2Ended = 0x0055f720;
constexpr std::uint32_t kPixel = 0x0055f6e4;
constexpr std::uint32_t kPolyLine = 0x0055f70c;
constexpr std::uint32_t kGPolyLine = 0x0055f6e0;
constexpr std::uint32_t kQuad = 0x0055f708;
constexpr std::uint32_t kGenPoly = 0x0055f724;
constexpr std::uint32_t kChunk = 0x0055f6c0;
constexpr std::uint32_t kGlow = 0x0055f6e8;
constexpr std::uint32_t kGlass = 0x0055f6ec;
constexpr std::uint32_t kGLine = 0x0055f6cc;
constexpr std::uint32_t kSpecial = 0x0055f710;

}  // namespace lists

constexpr std::uint32_t kBitCount = 0x0055f72c;          // dword: the live bits
constexpr std::uint32_t kBitBytes = 0x0055f728;          // dword: the bytes the bits hold
constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0xc00;

class BitKit final : public collision::CollisionKit {
public:
    BitKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), core_(memory, services), m3d_(memory, services) { core_.use_native_square_root(); }

    // ---- the lists and the base class (bit_core.cpp, 0x00403680..0x00403fc0) ----
    void bit_init();                                                                            // 0x00403680 Bit_Init
    void delete_bit_list(std::uint32_t head);                                                   // 0x00403730 DeleteBitList (cdecl)
    void bit_delete_all();                                                                      // 0x00403760 Bit_DeleteAll
    void remove_dead_bits();                                                                    // 0x00403bd0 Bit_RemoveDeadBits
    void remove_dead_list(std::uint32_t head);                                                  // 0x00403c70 RemoveDeadBits (cdecl)
    void reduce_rgb(std::uint32_t colour, std::int32_t amount);                                 // 0x00403ca0 Bit_ReduceRGB (cdecl)
    std::uint32_t bit_new(std::uint32_t size);                                                  // 0x00403d30 CBit::operator new (cdecl)
    void bit_delete(std::uint32_t block);                                                       // 0x00403dc0 CBit::operator delete (cdecl)
    void attach_to(std::uint32_t self, std::uint32_t head);                                     // 0x00403df0 CBit::AttachTo (thiscall, one argument)
    void delete_from(std::uint32_t self, std::uint32_t head);                                   // 0x00403e10 CBit::DeleteFrom (thiscall, one argument)
    void die(std::uint32_t self);                                                               // 0x00403e40 CBit::Die (fastcall)
    void set_pos(std::uint32_t self, std::uint32_t position);                                   // 0x00403ea0 CBit::SetPos (thiscall, one argument)
    void set_random_velocity(std::uint32_t self, std::int32_t range);                           // 0x00403ec0 (thiscall, one argument)
    void bit_construct(std::uint32_t self);                                                     // 0x00403f00 CBit::CBit (fastcall)
    std::uint32_t bit_scalar_delete(std::uint32_t self, std::uint32_t flags);                   // 0x00403f50 (thiscall)
    void bit_destruct(std::uint32_t self);                                                      // 0x00403f70 (fastcall)
    void bit_move(std::uint32_t self);                                                          // 0x00403f90 CBit::Move (fastcall)

    // ---- the simple classes (bit_core.cpp, 0x00403fc0..0x00406200) ----
    std::uint32_t special_construct(std::uint32_t self);                                        // 0x00403fc0 CSpecialDisplay
    std::uint32_t special_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x00403fe0
    void special_destruct(std::uint32_t self);                                                  // 0x00404000
    std::uint32_t nonrendered_construct(std::uint32_t self);                                    // 0x00404020 CNonRenderedBit
    std::uint32_t nonrendered_scalar_delete(std::uint32_t self, std::uint32_t flags);           // 0x00404040
    void nonrendered_destruct(std::uint32_t self);                                              // 0x00404060
    std::uint32_t ft4_construct(std::uint32_t self);                                            // 0x00404080 CFT4Bit
    std::uint32_t ft4_scalar_delete(std::uint32_t self, std::uint32_t flags);                   // 0x004040b0
    void ft4_destruct(std::uint32_t self);                                                      // 0x004040d0
    void set_scale(std::uint32_t self, std::uint32_t scale);                                    // 0x00404150 CFT4Bit::SetScale (thiscall, one argument)
    void set_semi_transparent(std::uint32_t self);                                              // 0x00404170 (fastcall)
    void set_tint(std::uint32_t self, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x004041c0 CFT4Bit::SetTint (thiscall, three arguments)
    void set_trans_decay(std::uint32_t self, std::uint32_t decay);                              // 0x00404210 (thiscall, one argument)
    void set_anim_by_name(std::uint32_t self, std::uint32_t name);                              // 0x00404220 CFT4Bit::SetAnim (thiscall, one argument)
    void set_anim(std::uint32_t self, std::int32_t index);                                      // 0x00404250 (thiscall, one argument): the animation of the lookup table
    void set_frame(std::uint32_t self, std::int32_t frame);                                     // 0x004042d0 CFT4Bit::SetFrame (thiscall, one argument)
    void inc_frame(std::uint32_t self);                                                         // 0x00404390 (fastcall)
    void inc_frame_with_wrap(std::uint32_t self);                                               // 0x004043d0 (fastcall)
    void choose_random_frame(std::uint32_t self);                                               // 0x00404460 (fastcall)
    void frag_move(std::uint32_t self);                                                         // 0x004068a0 CFrag::Move (fastcall)
    std::uint32_t shrink(std::uint32_t self, std::uint32_t die);                                // 0x00404490 CFT4Bit::Shrink (thiscall, one argument)
    std::uint32_t fade(std::uint32_t self, std::uint32_t die);                                  // 0x004044f0 CFT4Bit::Fade (thiscall, one argument)
    std::uint32_t quad_construct(std::uint32_t self);                                           // 0x00404590 CQuadBit
    std::uint32_t quad_scalar_delete(std::uint32_t self, std::uint32_t flags);                  // 0x004045e0
    void quad_destruct(std::uint32_t self);                                                     // 0x00404600
    void quad_set_texture_index(std::uint32_t self, std::int32_t animation, std::int32_t frame);   // 0x00404620 (thiscall, two arguments)
    void quad_set_texture(std::uint32_t self, std::uint32_t name, std::int32_t frame);          // 0x004046f0 CQuadBit::SetTexture (thiscall, two arguments)
    void quad_flag_on(std::uint32_t self, std::uint32_t bits);                                  // 0x00404810 (thiscall, one argument)
    void quad_set_semi_transparent(std::uint32_t self);                                         // 0x00404840 (fastcall)
    void quad_set_transparency(std::uint32_t self, std::uint32_t level);                        // 0x00404860 (thiscall, one argument)
    void quad_orient(std::uint32_t self, std::uint32_t centre, std::uint32_t side, std::uint32_t half_width, std::uint32_t half_height);   // 0x004048c0 (thiscall, four arguments after the centre)
    std::uint32_t genpoly4_construct(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);   // 0x00404af0 (thiscall, four arguments)
    std::uint32_t genpoly_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x00404bd0
    std::uint32_t genpoly3_construct(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c);   // 0x00404bf0 (thiscall, three arguments)
    void genpoly_destruct(std::uint32_t self);                                                  // 0x00404cb0
    std::uint32_t chunk_construct(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c);   // 0x004051c0 CChunkBit (thiscall, three arguments)
    std::uint32_t chunk_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00405390
    void chunk_destruct(std::uint32_t self);                                                    // 0x004053b0
    void chunk_set_rgb(std::uint32_t self, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x004053d0 CChunkBit::SetRGB (thiscall, three arguments)
    void chunk_set_texture(std::uint32_t self, std::uint32_t page, std::uint32_t clut, std::uint32_t u0, std::uint32_t v0, std::uint32_t u1, std::uint32_t v1, std::uint32_t u2, std::uint32_t v2);   // 0x004054b0 (thiscall, eight arguments)
    void chunk_world_coords(std::uint32_t self);                                                // 0x00405520 CChunkBit::CalculateWorldCoords (fastcall)
    std::uint32_t glow_construct(std::uint32_t self, std::int32_t points, std::int32_t fringes);   // 0x004056c0 CGlow (thiscall, two arguments)
    std::uint32_t glow_scalar_delete(std::uint32_t self, std::uint32_t flags);                  // 0x004057f0
    std::uint32_t glow_construct_simple(std::uint32_t self, std::uint32_t position, std::uint32_t radius, std::uint32_t fringe, std::uint32_t red, std::uint32_t green, std::uint32_t blue,
                                        std::uint32_t centre_red, std::uint32_t centre_green, std::uint32_t centre_blue);   // 0x00405810 CGlow (thiscall, nine arguments)
    void glow_destruct(std::uint32_t self);                                                     // 0x00405990
    void glow_set_radius(std::uint32_t self, std::uint32_t radius);                             // 0x00405a70 (thiscall, one argument)
    void glow_set_fringe_width(std::uint32_t self, std::uint32_t fringe, std::uint32_t width);  // 0x00405aa0 (thiscall, two arguments)
    void glow_set_centre_rgb(std::uint32_t self, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x00405b20 (thiscall, three arguments)
    void glow_set_rgb(std::uint32_t self, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x00405b50 (thiscall, three arguments)
    void glow_set_fringe_rgb(std::uint32_t self, std::uint32_t fringe, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x00405b90 (thiscall, four arguments)
    void glow_set_radial_rgb(std::uint32_t self, std::uint32_t spoke, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x00405c40 (thiscall, four arguments)
    void glow_reduce(std::uint32_t self, std::int32_t amount);                                  // 0x00405d20 (thiscall, one argument)
    std::uint32_t wobbly_construct(std::uint32_t self, std::uint32_t a2, std::int32_t radius, std::uint32_t a4, std::int32_t scale, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11);   // 0x00405d60 (thiscall, ten arguments)
    std::uint32_t wobbly_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x00405e30
    void wobbly_destruct(std::uint32_t self);                                                   // 0x00405e50
    void wobbly_move(std::uint32_t self);                                                       // 0x00405e60 (thiscall: the sections' radii wobble)
    std::uint32_t flat_construct(std::uint32_t self);                                           // 0x00405ec0 CFlatBit
    std::uint32_t flat_scalar_delete(std::uint32_t self, std::uint32_t flags);                  // 0x00405ef0
    void flat_destruct(std::uint32_t self);                                                     // 0x00405f10
    std::uint32_t linked_construct(std::uint32_t self);                                         // 0x00405f30 CLinked2EndedBit
    std::uint32_t linked_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x00405f70
    void linked_destruct(std::uint32_t self);                                                   // 0x00405f90
    std::uint32_t pixel_construct(std::uint32_t self);                                          // 0x00405fb0 CPixel
    std::uint32_t pixel_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00405fd0
    void pixel_destruct(std::uint32_t self);                                                    // 0x00405ff0

    // ---- the spark settings and the animated classes (bit_spark.cpp, 0x00406070..0x00407050) ----
    void set_spark_trajectory(std::uint32_t angles);                                            // 0x00406070 Bit_SetSparkTrajectory (cdecl)
    void set_spark_cone(std::uint32_t angles);                                                  // 0x00406090 Bit_SetSparkTrajectoryCone (cdecl)
    void calculate_spark_velocity(std::uint32_t out, std::uint32_t magnitude);                  // 0x004060b0 Bit_CalculateSparkVelocity (cdecl)
    void set_spark_size(std::uint32_t size);                                                    // 0x00406160 Bit_SetSparkSize (cdecl)
    void set_spark_rgb(std::uint32_t red, std::uint32_t green, std::uint32_t blue);             // 0x004061c0 Bit_SetSparkRGB (cdecl)
    void set_spark_fade_rgb(std::uint32_t red, std::uint32_t green, std::uint32_t blue);        // 0x004061e0 Bit_SetSparkFadeRGB (cdecl)
    std::uint32_t spark_construct(std::uint32_t self, std::uint32_t position, std::uint32_t speed, std::uint32_t gravity, std::uint32_t life);   // 0x00406200 CSpark (thiscall, four arguments)
    std::uint32_t spark_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00406300
    void spark_destruct(std::uint32_t self);                                                    // 0x00406320
    void spark_move(std::uint32_t self);                                                        // 0x00406330 (fastcall)
    std::uint32_t simple_anim_construct(std::uint32_t self, std::uint32_t position, std::uint32_t animation, std::uint32_t scale, std::uint32_t semi, std::uint32_t a5, std::int32_t last, bool by_index);   // 0x004063e0 (by name) / 0x004064b0 (by index) CSimpleAnim (thiscall, six arguments)
    std::uint32_t simple_anim_scalar_delete(std::uint32_t self, std::uint32_t flags);           // 0x00406490
    void simple_anim_destruct(std::uint32_t self);                                              // 0x00406560
    void simple_anim_move(std::uint32_t self);                                                  // 0x00406570
    std::uint32_t motion_blur_construct(std::uint32_t self, std::uint32_t position, std::uint32_t velocity, std::uint32_t animation, std::uint32_t scale, std::uint32_t scale_decay, std::uint32_t tint_decay);   // 0x004065c0 (thiscall, six arguments)
    std::uint32_t motion_blur_scalar_delete(std::uint32_t self, std::uint32_t flags);           // 0x00406660
    void motion_blur_destruct(std::uint32_t self);                                              // 0x00406680
    void motion_blur_move(std::uint32_t self, bool immortal);                                   // 0x00406690 CMotionBlur::Move, 0x00406740 CMotionBlurImmortal::Move
    std::uint32_t immortal_scalar_delete(std::uint32_t self, std::uint32_t flags);              // 0x00406710
    void immortal_destruct(std::uint32_t self);                                                 // 0x00406730
    std::uint32_t frag_construct(std::uint32_t self, std::uint32_t position, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t animation, std::uint32_t scale, std::uint32_t semi, std::int32_t spread, std::uint32_t gravity, std::uint32_t life);   // 0x00406780 CFrag (thiscall, ten arguments)
    std::uint32_t frag_scalar_delete(std::uint32_t self, std::uint32_t flags);                  // 0x00406870
    void frag_destruct(std::uint32_t self);                                                     // 0x00406890
    std::uint32_t ribbon_bit_construct(std::uint32_t self);                                     // 0x00406940 CRibbonBit
    std::uint32_t ribbon_bit_scalar_delete(std::uint32_t self, std::uint32_t flags);            // 0x00406960
    void ribbon_bit_destruct(std::uint32_t self);                                               // 0x00406980
    std::uint32_t ribbon_construct(std::uint32_t self, std::uint32_t position, std::int32_t bits, std::int32_t points, std::uint32_t animation, std::uint32_t last_animation, std::uint32_t scale, std::uint32_t semi);   // 0x004069a0 CRibbon (thiscall, seven arguments)
    std::uint32_t ribbon_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x00406b10
    void ribbon_destruct(std::uint32_t self);                                                   // 0x00406b30 (fastcall)
    void ribbon_set_tint(std::uint32_t self, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x00406bf0 (thiscall, three arguments)
    void ribbon_set_scale(std::uint32_t self, std::uint32_t scale);                             // 0x00406c30 (thiscall, one argument)
    void ribbon_set_pos(std::uint32_t self, std::uint32_t position);                            // 0x00406c60 (thiscall, one argument)
    std::uint32_t smoke_trail_construct(std::uint32_t self, std::uint32_t position, std::int32_t bits, std::int32_t red, std::int32_t green, std::int32_t blue);   // 0x00406d00 CSmokeTrail (thiscall, five arguments)
    std::uint32_t smoke_trail_scalar_delete(std::uint32_t self, std::uint32_t flags);           // 0x00406db0
    void smoke_trail_destruct(std::uint32_t self);                                              // 0x00406dd0
    void smoke_trail_move(std::uint32_t self);                                                  // 0x00406de0
    std::uint32_t glass_construct(std::uint32_t self, std::uint32_t position, std::uint32_t velocity, std::uint32_t floor, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::int32_t dx, std::int32_t dy, std::int32_t dz);   // 0x00406e30 CGlassBit (thiscall, nine arguments)
    std::uint32_t glass_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00407010
    void glass_destruct(std::uint32_t self);                                                    // 0x00407030
    void glass_move(std::uint32_t self);                                                        // 0x00407050 (fastcall)

    // ---- the sweeps, the polylines, the smoke generator, the line bits, the blood manager and the helpers between them (bit_misc.cpp, 0x00407250..0x0040b570) ----
    void move_list(std::uint32_t head);                                                         // 0x00407250 MoveList (cdecl)
    void bit_move_all();                                                                        // 0x00407270 Bit_Move
    std::uint32_t polyline_construct(std::uint32_t self, std::int32_t segments);                // 0x0040a740 CPolyLine (thiscall, one argument)
    std::uint32_t polyline_scalar_delete(std::uint32_t self, std::uint32_t flags);              // 0x0040a860
    void polyline_destruct(std::uint32_t self);                                                 // 0x0040a880
    void polyline_set_semi_transparent(std::uint32_t self);                                     // 0x0040a900
    std::uint32_t gpolyline_construct(std::uint32_t self, std::int32_t segments);               // 0x0040aa20 CGPolyLine (thiscall, one argument)
    std::uint32_t gpolyline_scalar_delete(std::uint32_t self, std::uint32_t flags);             // 0x0040ab50
    void gpolyline_destruct(std::uint32_t self);                                                // 0x0040ab70
    std::uint32_t smoke_generator_construct(std::uint32_t self, std::uint32_t position, std::uint32_t duration, std::uint32_t a4, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11);   // 0x0040ad30 (thiscall, ten arguments)
    std::uint32_t smoke_generator_scalar_delete(std::uint32_t self, std::uint32_t flags);       // 0x0040ae20
    void smoke_generator_destruct(std::uint32_t self);                                          // 0x0040ae40
    void smoke_generator_move(std::uint32_t self);                                              // 0x0040ae50
    std::uint32_t gline_construct(std::uint32_t self);                                          // 0x0040af70 (fastcall)
    std::uint32_t gline_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x0040afe0
    void gline_destruct(std::uint32_t self);                                                    // 0x0040b000
    std::uint32_t gline_bit_construct(std::uint32_t self, std::uint32_t object, std::uint32_t position, std::int32_t speed, std::uint32_t gravity, std::uint32_t life);   // 0x0040b060 (thiscall, five arguments)
    std::uint32_t gline_bit_scalar_delete(std::uint32_t self, std::uint32_t flags);             // 0x0040b1a0
    void gline_bit_destruct(std::uint32_t self);                                                // 0x0040b1c0
    void gline_bit_move(std::uint32_t self);                                                    // 0x0040b1d0 CGLineBit::Move (fastcall)
    std::uint32_t blood_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x0040b2e0
    void blood_destruct(std::uint32_t self);                                                    // 0x0040b300
    void blood_reset(std::uint32_t self);                                                       // 0x0040b350
    void class_destruct(std::uint32_t self);                                                    // 0x0040b3e0
    std::uint32_t class_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x0040b3f0 (thiscall)
    void jitter_vector(std::uint32_t vector, std::int32_t range);                               // 0x0040b410 (cdecl)
    void camera_seed(std::uint32_t seed);                                                       // 0x0040b4e0 (cdecl)
    std::int32_t camera_rand(std::uint32_t limit);                                              // 0x0040b500 (cdecl)
    void camera_quaternion_identity();                                                          // 0x0040b570

    // ---- the dust and the dynamic logos (bit_dust.cpp, 0x00431260..0x00432330) ----
    void dust_reset(std::uint32_t self, std::uint32_t terrain, std::uint32_t type);                 // 0x00431260 Dust::Reset (thiscall, two arguments)
    void dust_update(std::uint32_t self, std::uint32_t skater);                                     // 0x00431320 Dust::Update (thiscall, one argument)
    void dust_insert(std::uint32_t self, std::uint32_t cloud);                                      // 0x00431650 Dust::insert_dust (thiscall, one argument)
    void dust_terrain_parameters(std::uint32_t self, std::uint32_t terrain, std::uint32_t scale, std::uint32_t tint, std::uint32_t fade_rate, std::uint32_t scale_decay, std::uint32_t clouds, std::uint32_t offset);   // 0x004317c0 (thiscall, seven arguments)
    void dust_land_parameters(std::uint32_t self, std::uint32_t terrain, std::uint32_t scale, std::uint32_t tint, std::uint32_t fade_rate, std::uint32_t scale_decay, std::uint32_t clouds, std::uint32_t offset);    // 0x004318a0
    void dust_bail_parameters(std::uint32_t self, std::uint32_t terrain, std::uint32_t scale, std::uint32_t tint, std::uint32_t fade_rate, std::uint32_t scale_decay, std::uint32_t clouds, std::uint32_t offset);    // 0x00431b80
    void dust_brake_parameters(std::uint32_t self, std::uint32_t terrain, std::uint32_t scale, std::uint32_t tint, std::uint32_t fade_rate, std::uint32_t scale_decay, std::uint32_t clouds, std::uint32_t offset);   // 0x00431e80
    void random_offset(std::uint32_t vector, std::int32_t range, std::uint32_t shift);              // 0x00432180 (cdecl)
    bool logo_name_matches(std::uint32_t name, std::int32_t index);                                 // 0x004321d0 (cdecl)
    std::uint32_t logo_next(std::uint32_t list, std::uint32_t first, std::int32_t index);           // 0x00432250 (cdecl)
    void logo_load_all();                                                                           // 0x00432330

    // ---- the shatter and the emitter (bit_shatter.cpp, 0x004add10..0x004af660) ----
    std::uint32_t emitter_construct(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t position, std::uint32_t unused);   // 0x004add10 (thiscall, five arguments)
    std::uint32_t emitter_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x004ade60
    void emitter_destruct(std::uint32_t self);                                                  // 0x004ade80
    void emitter_set_velocity(std::uint32_t self, std::uint32_t position);                      // 0x004adee0 (thiscall, one argument)
    void emitter_move(std::uint32_t self);                                                      // 0x004ae020 (fastcall)
    std::uint32_t shatter_face(std::uint32_t item, std::uint32_t face, std::uint32_t depth, std::uint32_t unused, std::uint32_t only_marked, std::uint32_t clear_mark, std::uint32_t quota);   // 0x004ae090 Shatter_Face (cdecl)
    void shatter_triangle(std::uint32_t v1, std::uint32_t v2, std::uint32_t v3, std::uint32_t u1, std::uint32_t w1, std::uint32_t u2, std::uint32_t w2, std::uint32_t u3, std::uint32_t w3, std::uint32_t context, std::uint32_t depth, std::uint32_t record);   // 0x004ae730 (cdecl)
    void shatter_corner(std::uint32_t out, std::uint32_t vertices, std::uint32_t indices, std::uint32_t which);   // 0x004aeef0 (cdecl)
    void shatter_colour(std::uint32_t indexed, std::uint32_t packed, std::uint32_t count, std::uint32_t table);   // 0x004aef70 (cdecl)
    void shatter_item(std::uint32_t item, std::uint32_t only_marked, std::uint32_t clear_mark); // 0x004af0b0 Shatter_Item (cdecl)
    void shatter_forget_burst();                                                                // 0x004af1a0
    void shatter_glass(std::uint32_t count, std::uint32_t v1, std::uint32_t v2, std::uint32_t v3, std::uint32_t wind, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x004af1c0 (cdecl)
    void shatter_burst_sound();                                                                 // 0x004af630

    // ---- the cheat codes (bit_cheat.cpp, 0x004af660, 0x004af6b0) ----
    void cheat_hash(std::uint32_t first, std::uint32_t second);                                 // 0x004af660 (cdecl)
    std::uint32_t cheat_code(std::uint32_t flag);                                               // 0x004af6b0 (cdecl)

    // ---- Bit_Display (bit_display.cpp, 0x00407310) ----
    void bit_display();                                                                         // 0x00407310 Bit_Display

    mathkit::MathKit& math() { return m3d_.math(); }
    core::CoreKit& core() { return core_; }
    m3d::M3dKit& m3d() { return m3d_; }

private:
    using Word = std::uint32_t;
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    // FileIO_AllocLoadedBuffer(size, heap, one, zero): the bits' allocator
    Word alloc(Word size) {
        s_.note("alloc", {size, 0, 1, 0});
        return static_cast<Word>(s_.value("alloc"));
    }
    std::int32_t rnd(Word limit) {
        s_.note("rnd", {limit});
        return s_.value("rnd");
    }
    Word block_size(Word block) { return engine_value(0x0046f510, {block}); }
    void mem_delete(Word block) { s_.note("mem_delete", {block}); }
    void thiscall(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        engine(target, std::move(args));
    }
    Word delete_with(Word self, Word flags, void (BitKit::*destruct)(Word)) {
        (this->*destruct)(self);
        if ((flags & 1u) != 0) {
            bit_delete(self);
        }
        return self;
    }
    void vec3_add(Word self, Word rhs);
    Word arena_take(Word bytes);
    void bucket_link(Word prim, Word slot, Word index);
    void scale_colour(Word at, Word alpha);
    bool display_glows(Word camera, Word half);
    bool display_gpolylines();
    bool display_polylines();
    bool display_flats(Word camera);
    bool display_glass(Word camera);
    bool display_glines(Word camera);
    bool display_quads(Word camera);
    bool display_chunks(Word camera);
    bool display_pixels(Word camera);
    bool display_linked(Word camera);
    void project(Word position, Word shift, Word camera);
    bool z_in_range(std::int32_t z);
    static Word depth_bucket(std::int32_t z, Word bias);
    std::int32_t project_box(Word box);
    void check_dust_pointers(Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset);
    void vec3_decay_shift(Word self, Word shifts);

    core::CoreKit core_;
    m3d::M3dKit m3d_;
    Word triangle_level_ = 0;      // the depth of the shatter's recursion (each level has its own scratch)
};

}  // namespace opentony::runtime::bitkit
